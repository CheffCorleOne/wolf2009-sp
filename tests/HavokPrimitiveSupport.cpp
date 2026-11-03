// SPDX-License-Identifier: GPL-3.0-or-later
#include "HavokPrimitiveSupport.h"
#include "HavokTestFixture.h"
#include "HavokTriangleMesh.h"
#include "MapPhysics.h"
#include "WorldCollisionGeometry.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
using namespace wolf2009;
using namespace wolf2009_test;
int checks = 0;
void Check(bool ok, const char *message) { Need(ok, message); ++checks; }
template<class Fn> void Reject(Fn f) {
    bool caught = false;
    try { f(); } catch (const std::exception &) { caught = true; }
    Check(caught, "Malformed primitive support input accepted");
}
bool Near(double a, double b) { return std::abs(a - b) < .00001; }
bool Point(CollisionVec3 p, double x, double y, double z) {
    return Near(p.x, x) && Near(p.y, y) && Near(p.z, z);
}
struct Fixture : HavokFixture {
    Fixture() {
        types[64] = "hkpSphereShape";
        types[128] = "hkpCapsuleShape";
        types[256] = "hkpCylinderShape";
        F(data, 64 + 16, .0254f);
        // Sphere support is the origin, not fields in the unused header tail.
        U(data, 64 + 20, 0x7fc01234u); U(data, 64 + 24, 0xff800000u);
        U(data, 64 + 28, 0x12345678u);
        F(data, 128 + 16, .0127f);
        F(data, 128 + 32, .0254f); F(data, 128 + 36, -.0508f); F(data, 128 + 40, .0762f);
        F(data, 128 + 48, -.1016f); F(data, 128 + 52, .127f); F(data, 128 + 56, -.1524f);
        U(data, 128 + 44, 0x7fc01234u); U(data, 128 + 60, 0x80000000u);
        for (unsigned k = 0; k < 96; ++k) data[256 + k] = static_cast<unsigned char>(k * 3);
        F(data, 256 + 16, .0508f);
        U(data, 256 + 20, 0x7fc00000u); // unresolved cylinder fields stay raw
        // Relocation metadata does not authorize following a pointer in a leaf.
        links[64 + 8] = 2000; links[128 + 8] = 2004;
        U(data, 64 + 8, 0xdeadbeefu); U(data, 128 + 8, 0xcafebabeu);
    }
};
std::vector<HavokPrimitiveSelection> Selection() {
    return {{128, HavokPrimitiveKind::Capsule}, {64, HavokPrimitiveKind::Sphere},
            {256, HavokPrimitiveKind::Cylinder}};
}
HavokPrimitiveSupportDocument Decode(const Fixture &fixture,
    const HavokPrimitiveSupportLimits &limits = HavokPrimitiveSupportLimits()) {
    return DecodeHavokPrimitiveSupport(fixture.File(), Selection(), limits);
}
void Synthetic() {
    Fixture fixture;
    const auto bytes = fixture.File();
    const auto document = Decode(fixture);
    Check(document.shapes.size() == 3 && document.sourceBytes == bytes.size() &&
              document.sourceFingerprint == DecodeHavokTriangleMeshes(bytes).sourceFingerprint,
          "Primitive reader changed the common full-source fingerprint convention");
    const auto &capsule = document.shapes[0], &sphere = document.shapes[1], &cylinder = document.shapes[2];
    Check(capsule.sourceShapeOffset == 128 && sphere.sourceShapeOffset == 64 &&
              capsule.sourceType == "hkpCapsuleShape" && sphere.sourceType == "hkpSphereShape",
          "Exact selections were reordered or their types lost");
    Check(sphere.status == HavokPrimitiveSupportStatus::Resolved && sphere.radiusFinite &&
              sphere.radiusKnown && Near(sphere.radiusGameUnits, 1) && sphere.corePoints.size() == 1 &&
              Point(sphere.corePoints[0], 0, 0, 0),
          "Sphere did not preserve its origin support core plus physical radius");
    Check(capsule.status == HavokPrimitiveSupportStatus::Resolved && capsule.radiusKnown &&
              Near(capsule.radiusGameUnits, .5) && capsule.corePoints.size() == 2 &&
              Point(capsule.corePoints[0], 1, -2, 3) && Point(capsule.corePoints[1], -4, 5, -6),
          "Capsule endpoint XYZ/order or metres-to-game-units conversion changed");
    Check(capsule.rawHeaderBytes == 64 && sphere.rawHeaderBytes == 32 &&
              cylinder.rawHeaderBytes == 96 && capsule.rawHeader[44] == 0x34 &&
              capsule.rawHeader[47] == 0x7f && capsule.rawHeader[63] == 0x80,
          "Nonfinite/sign endpoint W payloads were interpreted or discarded");
    for (const auto &shape : document.shapes) {
        Check(std::equal(shape.rawHeader.begin(), shape.rawHeader.begin() + shape.rawHeaderBytes,
                         fixture.data.begin() + shape.sourceShapeOffset),
              "Raw primitive header bytes changed");
        Check(std::all_of(shape.rawHeader.begin() + shape.rawHeaderBytes, shape.rawHeader.end(),
                          [](unsigned char byte) { return byte == 0; }),
              "Reader copied bytes outside the selected primitive header");
    }
    Check(cylinder.status == HavokPrimitiveSupportStatus::Unresolved && !cylinder.reason.empty() &&
              cylinder.corePoints.empty() && !cylinder.radiusKnown && cylinder.radiusFinite &&
              cylinder.rawHeader[20] == 0 && cylinder.rawHeader[23] == 0x7f,
          "Unresolved cylinder was replaced by an inferred support geometry");
    const auto repeat = Decode(fixture);
    Check(repeat.sourceFingerprint == document.sourceFingerprint &&
              repeat.shapes[0].sourceRegion == capsule.sourceRegion && capsule.sourceRegion != sphere.sourceRegion,
          "Primitive source identities are unstable or collapse distinct objects");
    auto changed = fixture;
    changed.data[3500] = 1;
    const auto changedDocument = Decode(changed);
    Check(changedDocument.sourceFingerprint != document.sourceFingerprint &&
              changedDocument.shapes[0].sourceRegion != capsule.sourceRegion &&
              changedDocument.shapes[0].rawHeader == capsule.rawHeader,
          "Changed bounded source bytes reused the old provenance identity");

    auto duplicateEndpoints = fixture;
    std::copy(duplicateEndpoints.data.begin() + 160, duplicateEndpoints.data.begin() + 176,
              duplicateEndpoints.data.begin() + 176);
    F(duplicateEndpoints.data, 128 + 16, 0);
    const auto duplicate = Decode(duplicateEndpoints).shapes[0];
    Check(duplicate.corePoints.size() == 2 && duplicate.radiusGameUnits == 0 &&
              Point(duplicate.corePoints[0], 1, -2, 3) && Point(duplicate.corePoints[1], 1, -2, 3),
          "Degenerate capsule segment or zero radius was normalized/deduplicated");
    auto rawCylinder = fixture;
    U(rawCylinder.data, 256 + 16, 0x7fc00000u);
    const auto unknown = Decode(rawCylinder).shapes[2];
    Check(unknown.status == HavokPrimitiveSupportStatus::Unresolved && !unknown.radiusFinite &&
              !unknown.radiusKnown && unknown.corePoints.empty() && unknown.rawRadiusBits == 0x7fc00000u,
          "Unknown cylinder radius bits were silently promoted to known geometry");
    auto unrelated = fixture;
    U(unrelated.data, 128 + 32, 0x7fc00000u);
    const auto selectedSphere = DecodeHavokPrimitiveSupport(unrelated.File(), {{64, HavokPrimitiveKind::Sphere}});
    const auto empty = DecodeHavokPrimitiveSupport(unrelated.File(), {});
    Check(selectedSphere.shapes.size() == 1 && empty.shapes.empty() && empty.decodedBytes == 0 &&
              empty.sourceBytes == unrelated.File().size(),
          "Explicit selection followed or enumerated unrelated primitive objects");

    Reject([&] { DecodeHavokPrimitiveSupport(bytes, {{128, HavokPrimitiveKind::Sphere}}); });
    Reject([&] { DecodeHavokPrimitiveSupport(bytes, {{1024, HavokPrimitiveKind::Sphere}}); });
    Reject([&] { DecodeHavokPrimitiveSupport(bytes, {{64, static_cast<HavokPrimitiveKind>(99)}}); });
    Reject([&] { DecodeHavokPrimitiveSupport(bytes, {{64, HavokPrimitiveKind::Sphere},
                                                   {64, HavokPrimitiveKind::Sphere}}); });
    Reject([&] { DecodeHavokPrimitiveSupport(bytes, {{64, HavokPrimitiveKind::Sphere},
                                                   {64, HavokPrimitiveKind::Capsule}}); });
    auto bad = fixture;
    bad.types[128] = "hkpCapsuleShapeVariant"; Reject([&] { Decode(bad); });
    bad = fixture;
    bad.types[4080] = "hkpSphereShape";
    Reject([&] { DecodeHavokPrimitiveSupport(bad.File(), {{4080, HavokPrimitiveKind::Sphere}}); });
    bad = fixture;
    bad.types[4064] = "hkpCapsuleShape";
    Reject([&] { DecodeHavokPrimitiveSupport(bad.File(), {{4064, HavokPrimitiveKind::Capsule}}); });
    bad = fixture;
    bad.types[4048] = "hkpCylinderShape";
    Reject([&] { DecodeHavokPrimitiveSupport(bad.File(), {{4048, HavokPrimitiveKind::Cylinder}}); });
    for (unsigned field : {128u + 32, 128u + 36, 128u + 40, 128u + 48, 128u + 52, 128u + 56}) {
        bad = fixture; U(bad.data, field, 0x7fc00000u); Reject([&] { Decode(bad); });
    }
    for (unsigned object : {64u, 128u}) {
        bad = fixture; F(bad.data, object + 16, -1); Reject([&] { Decode(bad); });
        bad = fixture; U(bad.data, object + 16, 0x7f800000u); Reject([&] { Decode(bad); });
        bad = fixture; U(bad.data, object + 16, 0x7fc00000u); Reject([&] { Decode(bad); });
    }
    auto truncated = bytes; truncated.pop_back();
    Reject([&] { DecodeHavokPrimitiveSupport(truncated, Selection()); });
    auto wrongLayout = bytes; wrongLayout[16] = 8;
    Reject([&] { DecodeHavokPrimitiveSupport(wrongLayout, Selection()); });

    HavokPrimitiveSupportLimits limits;
    limits.coordinateGameUnits = 5; Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.maximumRadiusGameUnits = .25;
    Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.sourceBytes = bytes.size() - 1;
    Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.objects = 3;
    Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.fixups = 1;
    Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.shapes = 2;
    Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.decodedBytes = document.decodedBytes - 1;
    Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.coordinateGameUnits = std::numeric_limits<double>::infinity();
    Reject([&] { Decode(fixture, limits); });
    limits = HavokPrimitiveSupportLimits(); limits.maximumRadiusGameUnits = -1;
    Reject([&] { Decode(fixture, limits); });
    std::cout << "WOLF2009_HAVOK_PRIMITIVE_SUPPORT_SYNTHETIC_OK checks=" << checks << '\n';
}

struct Counts { std::size_t sphere = 0, capsule = 0, cylinder = 0, unique = 0; };
Counts Original(const char *file) {
    std::ifstream input(file, std::ios::binary);
    Need(bool(input), "Cannot open original map package");
    const auto archive = Archive::Scan(input);
    std::string name;
    for (const auto &chunk : archive.Chunks()) if (chunk.type == "PROC")
        for (const auto &entry : chunk.entries) {
            Need(name.empty(), "Ambiguous map resource"); name = entry.name;
        }
    Need(!name.empty(), "Map has no explicit PROC resource name");
    const auto bytes = ReadMapResource(input, archive, name, "HKXS", 64u << 20);
    const auto world = DecodeWorldCollisionGeometry(bytes, name);
    std::map<std::uint32_t, HavokPrimitiveKind> selected;
    Counts counts;
    for (const auto &omission : world.omissions) {
        HavokPrimitiveKind kind;
        if (omission.type == "hkpSphereShape") { kind = HavokPrimitiveKind::Sphere; ++counts.sphere; }
        else if (omission.type == "hkpCapsuleShape") { kind = HavokPrimitiveKind::Capsule; ++counts.capsule; }
        else if (omission.type == "hkpCylinderShape") { kind = HavokPrimitiveKind::Cylinder; ++counts.cylinder; }
        else continue;
        const auto inserted = selected.insert(std::make_pair(omission.sourceShapeOffset, kind));
        Need(inserted.second || inserted.first->second == kind, "One source primitive has contradictory types");
    }
    std::vector<HavokPrimitiveSelection> selection;
    for (const auto &entry : selected) selection.push_back({entry.first, entry.second});
    const auto document = DecodeHavokPrimitiveSupport(bytes, selection);
    Need(document.sourceFingerprint == world.sourceFingerprint && document.sourceBytes == world.sourceBytes,
         "Original selected-body and primitive source identities disagree");
    std::size_t resolved = 0, unresolved = 0;
    for (const auto &shape : document.shapes) {
        if (shape.kind == HavokPrimitiveKind::Cylinder) {
            Need(shape.status == HavokPrimitiveSupportStatus::Unresolved && shape.corePoints.empty() &&
                     !shape.radiusKnown && shape.rawHeaderBytes == 96,
                 "Original cylinder acquired an unverified support profile");
            ++unresolved;
        } else {
            Need(shape.status == HavokPrimitiveSupportStatus::Resolved && shape.radiusKnown && shape.radiusFinite &&
                     shape.corePoints.size() == (shape.kind == HavokPrimitiveKind::Sphere ? 1u : 2u),
                 "Original sphere/capsule did not resolve its exact support core");
            ++resolved;
        }
    }
    counts.unique = document.shapes.size();
    Need(counts.unique == selected.size(), "Selected original primitive object was lost");
    std::cout << "WOLF2009_HAVOK_PRIMITIVE_SUPPORT_REAL_OK map=" << name
              << " sphereOccurrences=" << counts.sphere << " capsuleOccurrences=" << counts.capsule
              << " cylinderOccurrences=" << counts.cylinder << " uniqueObjects=" << counts.unique
              << " resolvedRecords=" << resolved << " unresolvedRecords=" << unresolved
              << " decodedBytes=" << document.decodedBytes << " nativeCollisionVerified=0\n";
    return counts;
}
} // namespace
int main(int argc, char **argv) {
    try {
        Synthetic();
        const bool survey = argc > 1 && std::string(argv[1]) == "--survey";
        Counts total;
        int failures = 0;
        for (int i = survey ? 2 : 1; i < argc; ++i) {
            try {
                const auto count = Original(argv[i]);
                total.sphere += count.sphere; total.capsule += count.capsule;
                total.cylinder += count.cylinder; total.unique += count.unique;
            } catch (const std::exception &error) {
                if (!survey) throw;
                ++failures;
                std::cout << "WOLF2009_HAVOK_PRIMITIVE_SUPPORT_REAL_ERROR argument=" << i - 1
                          << " reason=" << error.what() << '\n';
            }
        }
        if (survey) std::cout << "WOLF2009_HAVOK_PRIMITIVE_SUPPORT_SURVEY maps=" << argc - 2
                             << " failures=" << failures << " sphereOccurrences=" << total.sphere
                             << " capsuleOccurrences=" << total.capsule << " cylinderOccurrences=" << total.cylinder
                             << " uniqueObjects=" << total.unique << '\n';
        return failures ? 1 : 0;
    } catch (const std::exception &error) {
        std::cerr << "Havok primitive support: " << error.what() << '\n'; return 1;
    }
}

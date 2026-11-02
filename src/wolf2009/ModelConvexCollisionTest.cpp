// SPDX-License-Identifier: GPL-3.0-or-later
#include "ModelConvexCollision.h"
#include "ModelCollision.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
void U(Bytes &b, std::size_t o, std::uint32_t n) {
    Need(o + 4 <= b.size(), "Synthetic writer bounds");
    for (int i = 0; i < 4; ++i)
        b[o + i] = static_cast<unsigned char>(n >> (8 * i));
}
void F(Bytes &b, std::size_t o, float n) {
    std::uint32_t u;
    std::memcpy(&u, &n, 4);
    U(b, o, u);
}
void Text(Bytes &b, std::size_t o, std::string s) {
    Need(o + s.size() < b.size(), "Synthetic text bounds");
    std::copy(s.begin(), s.end(), b.begin() + o);
    b[o + s.size()] = 0;
}
std::size_t Align(std::size_t x) { return (x + 15) / 16 * 16; }
template <class Fn> void Reject(Fn fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    Need(failed, "Malformed convex model accepted");
}
// Hand-authored packfile containing a translated cube hull. No game assets.
struct Fixture {
    Bytes data;
    std::map<std::uint32_t, std::uint32_t> links;
    std::map<std::uint32_t, std::string> types;
    Fixture() : data(4096, 0) {
        types[0] = "hkRootLevelContainer";
        types[64] = "hkpRigidBody";
        types[576] = "hkpMoppBvTreeShape";
        types[640] = "hkpListShape";
        types[736] = "hkpConvexTranslateShape";
        types[960] = "hkpConvexVerticesShape";
        links[64 + 112] = 880;
        Text(data, 880, "models/synthetic/hull");
        links[64 + 16] = 576;
        links[576 + 52] = 640;
        links[640 + 24] = 704;
        U(data, 640 + 28, 1);
        links[704] = 736;
        links[736 + 24] = 960;
        F(data, 736 + 40, .254f);
        for (int i = 0; i < 3; ++i)
            F(data, 64 + 224 + i * 20, 1);
        F(data, 64 + 284, 1);
        F(data, 64 + 332, 1);
        F(data, 64 + 348, 1);
        links[960 + 64] = 1152;
        U(data, 960 + 68, 2);
        U(data, 960 + 76, 8);
        links[960 + 80] = 1280;
        U(data, 960 + 84, 6);
        F(data, 960 + 16, .05f);
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 3; ++k)
                F(data, 1152 + (i / 4) * 48 + k * 16 + (i % 4) * 4, (i & (1 << k)) ? .254f : -.254f);
        for (int k = 0; k < 3; ++k)
            for (int sign = 0; sign < 2; ++sign) {
                const std::size_t o = 1280 + (k * 2 + sign) * 16;
                F(data, o + k * 4, sign ? 1.f : -1.f);
                F(data, o + 12, -.254f);
            }
    }
    Bytes File() const {
        Bytes classes(16, 0);
        std::map<std::string, std::uint32_t> names;
        for (const auto &t : types)
            if (!names.count(t.second)) {
                const std::size_t o = classes.size();
                names[t.second] = static_cast<std::uint32_t>(o);
                classes.resize(o + t.second.size() + 1);
                Text(classes, o, t.second);
            }
        classes.resize(Align(classes.size()), 255);
        const std::size_t absolute = 208 + classes.size(), local = data.size(),
                          global = Align(local + links.size() * 8),
                          end = Align(global + types.size() * 12);
        Bytes bytes(absolute + end, 255);
        std::fill(bytes.begin(), bytes.begin() + 208, 0);
        U(bytes, 0, 0x57e0e057);
        U(bytes, 4, 0x10c0c010);
        U(bytes, 12, 5);
        bytes[16] = 4;
        bytes[17] = 1;
        bytes[19] = 1;
        U(bytes, 20, 3);
        U(bytes, 24, 2);
        U(bytes, 36, names["hkRootLevelContainer"]);
        Text(bytes, 40, "Havok-5.6.0-r1");
        const char *tags[] = {"__classnames__", "__types__", "__data__"};
        for (int i = 0; i < 3; ++i) {
            const std::size_t h = 64 + i * 48;
            Text(bytes, h, tags[i]);
            U(bytes, h + 20, static_cast<std::uint32_t>(i == 0 ? 208 : absolute));
            for (int k = 0; k < 6; ++k)
                U(bytes, h + 24 + k * 4,
                  static_cast<std::uint32_t>(i == 0   ? classes.size()
                                             : i == 1 ? 0
                                             : k == 0 ? local
                                             : k <= 2 ? global
                                                      : end));
        }
        std::copy(classes.begin(), classes.end(), bytes.begin() + 208);
        std::copy(data.begin(), data.end(), bytes.begin() + absolute);
        std::size_t at = absolute + local;
        for (const auto &l : links) {
            U(bytes, at, l.first);
            U(bytes, at + 4, l.second);
            at += 8;
        }
        at = absolute + global;
        for (const auto &t : types) {
            U(bytes, at, t.first);
            U(bytes, at + 4, 0);
            U(bytes, at + 8, names[t.second]);
            at += 12;
        }
        return bytes;
    }
};
ModelConvexCollision Decode(const Fixture &f,
                            const ModelConvexCollisionLimits &l = ModelConvexCollisionLimits()) {
    return DecodeModelConvexCollision(f.File(), "models/synthetic/hull", l);
}
} // namespace
int RunModelConvexCollisionTests() {
    int passed = 0;
    try {
        Fixture f;
        auto model = Decode(f);
        Need(model.localPoseVerified && model.geometryComplete && !model.contentsComplete &&
                 model.localHulls.size() == 1 && model.sourceBodyOffset == 64 &&
                 model.rootShapeOffset == 576,
             "Hull model selection/coverage wrong");
        ++passed;
        const auto &h = model.localHulls[0];
        Need(h.vertices.size() == 8 && h.planes.size() == 6 && h.sourceShapeOffset == 960 &&
                 h.sourceRegion && std::abs(h.vertices[0].z) < 1e-5 &&
                 std::abs(h.radius - 1.96850397) < 1e-6 && !h.contentsKnown && h.rawContents == 0,
             "Hull decode coordinates/radius/flags wrong");
        ++passed;
        Need(Decode(f).localHulls[0].sourceRegion == h.sourceRegion, "Identical source region unstable");
        auto changed = f;
        changed.data[2000] = 1;
        Need(Decode(changed).localHulls[0].sourceRegion != h.sourceRegion,
             "Changed source reused provenance region");
        ++passed;
        auto known = f;
        U(known.data, 704 + 4, CollisionPlayerClip);
        U(known.data, 64 + 44, 3);
        const auto k = Decode(known);
        Need(k.contentsComplete && k.localHulls[0].contentsKnown && k.localHulls[0].rawContents == 8,
             "Explicit per-child flags lost");
        ++passed;
        auto direct = known;
        direct.links[64 + 16] = 960;
        U(direct.data, 64 + 44, CollisionBody);
        Need(Decode(direct).localHulls[0].rawContents == 256 &&
                 std::abs(Decode(direct).localHulls[0].vertices[0].z + 10) < 1e-5,
             "Direct body flags/translation wrong");
        ++passed;
        Need(DecodeModelCollision(f.File(), "models/synthetic/hull").localBoxes.empty() &&
                 !DecodeModelCollision(f.File(), "models/synthetic/hull").geometryComplete,
             "Opt-in hull changed legacy box subset");
        ++passed;
        auto bad = f;
        U(bad.data, 960 + 68, 1);
        Reject([&] { Decode(bad); });
        bad = f;
        U(bad.data, 960 + 68, 0xffffffffu);
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        U(bad.data, 960 + 76, 0xffffffffu);
        Reject([&] { Decode(bad); });
        bad = f;
        U(bad.data, 960 + 84, 0xffffffffu);
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        bad.links[960 + 64] = 4080;
        Reject([&] { Decode(bad); });
        bad = f;
        bad.links[960 + 80] = 4080;
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        bad.links.erase(960 + 64);
        Reject([&] { Decode(bad); });
        bad = f;
        bad.links.erase(960 + 80);
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        F(bad.data, 1152, (std::numeric_limits<float>::quiet_NaN)());
        Reject([&] { Decode(bad); });
        bad = f;
        F(bad.data, 1280, -2);
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        F(bad.data, 1280 + 12, -.1f);
        Reject([&] { Decode(bad); });
        bad = f;
        F(bad.data, 960 + 16, -.05f);
        Reject([&] { Decode(bad); });
        ++passed;
        ModelConvexCollisionLimits l;
        l.verticesPerHull = 7;
        Reject([&] { Decode(f, l); });
        l = ModelConvexCollisionLimits();
        l.planesPerHull = 5;
        Reject([&] { Decode(f, l); });
        ++passed;
        l = ModelConvexCollisionLimits();
        l.totalVertices = 7;
        Reject([&] { Decode(f, l); });
        l = ModelConvexCollisionLimits();
        l.totalPlanes = 5;
        Reject([&] { Decode(f, l); });
        l = ModelConvexCollisionLimits();
        l.hulls = 0;
        Reject([&] { Decode(f, l); });
        ++passed;
        l = ModelConvexCollisionLimits();
        l.maximumRadiusGameUnits = 1;
        Reject([&] { Decode(f, l); });
        l.maximumRadiusGameUnits = (std::numeric_limits<double>::infinity)();
        Reject([&] { Decode(f, l); });
        ++passed;
        l = ModelConvexCollisionLimits();
        l.graph.shapeVisits = 3;
        Reject([&] { Decode(f, l); });
        l = ModelConvexCollisionLimits();
        l.graph.coordinateGameUnits = 1;
        Reject([&] { Decode(f, l); });
        ++passed;
        bad = f;
        bad.links[736 + 24] = 640;
        Reject([&] { Decode(bad); });
        bad = f;
        F(bad.data, 64 + 272, 1);
        Reject([&] { Decode(bad); });
        ++passed;
        auto truncated = f.File();
        truncated.pop_back();
        Reject([&] { DecodeModelConvexCollision(truncated, "models/synthetic/hull"); });
        Reject([&] { DecodeModelConvexCollision(f.File(), "models/synthetic/other"); });
        ++passed;
        bad = f;
        bad.types[960] = "hkpConvexTransformShape";
        const auto omitted = Decode(bad);
        Need(!omitted.geometryComplete && omitted.localHulls.empty() &&
                 omitted.unsupportedGeometry.at("hkpConvexTransformShape") == 1,
             "Unverified transform became identity");
        ++passed;
        auto shared = f;
        U(shared.data, 640 + 28, 2);
        shared.links[720] = 736;
        U(shared.data, 704 + 4, 8);
        U(shared.data, 720 + 4, 16);
        const auto s = Decode(shared);
        Need(s.localHulls.size() == 2 && s.localHulls[0].rawContents == 8 &&
                 s.localHulls[1].rawContents == 16 &&
                 s.localHulls[0].sourceRegion != s.localHulls[1].sourceRegion,
             "Shared hull lost instance flags/provenance");
        ++passed;
        std::cout << "Model convex collision tests: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Model convex collision tests failed after " << passed << " checks: " << e.what()
                  << '\n';
        return 1;
    }
}
} // namespace wolf2009

// SPDX-License-Identifier: GPL-3.0-or-later
#include "ModelPrimitiveCollision.h"
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
void Text(Bytes &b, std::size_t o, const std::string &s) {
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
    Need(failed, "Invalid model primitive accepted");
}
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
        types[960] = "hkpConvexTransformShape";
        types[1152] = "hkpBoxShape";
        Text(data, 880, "models/synthetic/rounded");
        links[64 + 112] = 880;
        links[64 + 16] = 576;
        links[576 + 52] = 640;
        links[640 + 24] = 704;
        U(data, 640 + 28, 1);
        links[704] = 736;
        links[736 + 24] = 960;
        links[960 + 24] = 1152;
        for (int i = 0; i < 3; ++i)
            F(data, 64 + 224 + i * 20, 1);
        F(data, 64 + 284, 1);
        F(data, 64 + 332, 1);
        F(data, 64 + 348, 1);
        F(data, 736 + 40, .254f);
        const float s = std::sqrt(.5f);
        F(data, 960 + 32, s);
        F(data, 960 + 40, -s);
        F(data, 960 + 52, 1);
        F(data, 960 + 64, s);
        F(data, 960 + 72, s);
        F(data, 960 + 80, .254f);
        F(data, 960 + 88, .508f);
        F(data, 960 + 16, .05f);
        F(data, 1152 + 16, .05f);
        F(data, 1152 + 32, .0508f);
        F(data, 1152 + 36, .0762f);
        F(data, 1152 + 40, .1016f);
    }
    Bytes File() const {
        Bytes names(16, 0);
        std::map<std::string, std::uint32_t> offsets;
        for (const auto &t : types)
            if (!offsets.count(t.second)) {
                const std::size_t o = names.size();
                offsets[t.second] = static_cast<std::uint32_t>(o);
                names.resize(o + t.second.size() + 1);
                Text(names, o, t.second);
            }
        names.resize(Align(names.size()), 255);
        const std::size_t absolute = 208 + names.size(), local = data.size(),
                          global = Align(local + links.size() * 8),
                          end = Align(global + types.size() * 12);
        Bytes out(absolute + end, 255);
        std::fill(out.begin(), out.begin() + 208, 0);
        U(out, 0, 0x57e0e057);
        U(out, 4, 0x10c0c010);
        U(out, 12, 5);
        out[16] = 4;
        out[17] = 1;
        out[19] = 1;
        U(out, 20, 3);
        U(out, 24, 2);
        U(out, 36, offsets["hkRootLevelContainer"]);
        Text(out, 40, "Havok-5.6.0-r1");
        const char *tags[] = {"__classnames__", "__types__", "__data__"};
        for (int i = 0; i < 3; ++i) {
            const std::size_t h = 64 + i * 48;
            Text(out, h, tags[i]);
            U(out, h + 20, static_cast<std::uint32_t>(i == 0 ? 208 : absolute));
            for (int k = 0; k < 6; ++k)
                U(out, h + 24 + k * 4,
                  static_cast<std::uint32_t>(i == 0   ? names.size()
                                             : i == 1 ? 0
                                             : k == 0 ? local
                                             : k <= 2 ? global
                                                      : end));
        }
        std::copy(names.begin(), names.end(), out.begin() + 208);
        std::copy(data.begin(), data.end(), out.begin() + absolute);
        std::size_t p = absolute + local;
        for (const auto &l : links) {
            U(out, p, l.first);
            U(out, p + 4, l.second);
            p += 8;
        }
        p = absolute + global;
        for (const auto &t : types) {
            U(out, p, t.first);
            U(out, p + 4, 0);
            U(out, p + 8, offsets[t.second]);
            p += 12;
        }
        return out;
    }
};
ModelPrimitiveCollision Decode(const Fixture &f,
                               const ModelConvexCollisionLimits &l = ModelConvexCollisionLimits()) {
    return DecodeModelPrimitiveCollision(f.File(), "models/synthetic/rounded", l);
}
} // namespace
int RunModelPrimitiveCollisionTests() {
    int passed = 0;
    try {
        Fixture f;
        const auto m = Decode(f);
        Need(m.localPoseVerified && m.geometryComplete && !m.contentsComplete &&
                 m.localPrimitives.size() == 1,
             "Transformed primitive profile/coverage wrong");
        ++passed;
        const auto &p = m.localPrimitives[0];
        const auto &h = p.geometry;
        Need(p.kind == ModelPrimitiveKind::TransformedBox && h.sourceShapeOffset == 960 &&
                 p.sourceChildShapeOffset == 1152 && h.sourceRegion && !h.contentsKnown &&
                 h.rawContents == 0,
             "Primitive sources/unknown flags lost");
        ++passed;
        Need(h.vertices.size() == 8 && h.planes.size() == 6 && std::abs(h.radius - 1.968503966) < 1e-6,
             "Wrapper radius added twice");
        double loX = 100, hiX = -100, loZ = 100, hiZ = -100;
        for (const auto &v : h.vertices) {
            loX = std::min(loX, v.x);
            hiX = std::max(hiX, v.x);
            loZ = std::min(loZ, v.z);
            hiZ = std::max(hiZ, v.z);
        }
        Need(std::abs(loX - (10 - 6 * std::sqrt(.5))) < 1e-5 &&
                 std::abs(hiX - (10 + 6 * std::sqrt(.5))) < 1e-5 &&
                 std::abs(loZ - (30 - 6 * std::sqrt(.5))) < 1e-5 &&
                 std::abs(hiZ - (30 + 6 * std::sqrt(.5))) < 1e-5,
             "Transform columns/parent translation wrong");
        ++passed;
        ConvexContactQualification q;
        q.instanceId = "synthetic/cart";
        q.verifiedQueryMask = 265;
        q.sourceShapeOffset = h.sourceShapeOffset;
        q.sourceRegion = h.sourceRegion;
        q.planeIndex = 0;
        const double s = std::sqrt(.5);
        const auto r =
            SweepConvexContact(h, q, {}, {10 - 30 * s, 0, 30 + 30 * s}, {1, 1, 1}, {40 * s, 0, -40 * s});
        Need(r.status == ConvexContactStatus::ProvenFaceHit &&
                 std::abs(r.fraction - (30 - 2 - std::sqrt(2.) - h.radius) / 40) < 1e-7,
             "Decoded oblique face sweep failed");
        ++passed;
        auto direct = f;
        direct.links[64 + 16] = 1152;
        U(direct.data, 64 + 44, 8);
        const auto d = Decode(direct);
        Need(d.geometryComplete && d.contentsComplete &&
                 d.localPrimitives[0].kind == ModelPrimitiveKind::AxisBox &&
                 d.localPrimitives[0].geometry.rawContents == 8,
             "Rounded direct box/body flags wrong");
        ++passed;
        Need(
            DecodeModelCollision(direct.File(), "models/synthetic/rounded").localBoxes.empty() &&
                DecodeModelConvexCollision(direct.File(), "models/synthetic/rounded").localHulls.empty(),
            "Earlier opt-in subsets changed");
        ++passed;
        auto zero = direct;
        F(zero.data, 1152 + 16, 0);
        Need(Decode(zero).localPrimitives[0].geometry.radius == 0 &&
                 DecodeModelCollision(zero.File(), "models/synthetic/rounded").localBoxes.size() == 1,
             "Radius-zero compatibility lost");
        ++passed;
        auto bad = f;
        F(bad.data, 960 + 32, 2);
        Reject([&] { Decode(bad); });
        bad = f;
        F(bad.data, 960 + 32, -std::sqrt(.5f));
        F(bad.data, 960 + 40, std::sqrt(.5f));
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        F(bad.data, 960 + 44, 1);
        Reject([&] { Decode(bad); });
        bad = f;
        F(bad.data, 960 + 92, 1);
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        F(bad.data, 960 + 16, .1f);
        Reject([&] { Decode(bad); });
        bad = f;
        F(bad.data, 1152 + 32, -1);
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        F(bad.data, 960 + 80, (std::numeric_limits<float>::quiet_NaN)());
        Reject([&] { Decode(bad); });
        bad = f;
        F(bad.data, 1152 + 40, (std::numeric_limits<float>::infinity)());
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        bad.links.erase(960 + 24);
        Reject([&] { Decode(bad); });
        bad = f;
        bad.links[960 + 24] = 4080;
        bad.types[4080] = "hkpBoxShape";
        Reject([&] { Decode(bad); });
        ++passed;
        bad = f;
        bad.types[1152] = "hkpCylinderShape";
        const auto omitted = Decode(bad);
        Need(!omitted.geometryComplete && omitted.localPrimitives.empty() &&
                 omitted.unsupportedGeometry.at("convex-transform-child-hkpCylinderShape") == 1,
             "Unknown transformed child treated as box");
        ++passed;
        ModelConvexCollisionLimits l;
        l.graph.transforms = 1;
        Reject([&] { Decode(f, l); });
        l = ModelConvexCollisionLimits();
        l.graph.shapeVisits = 4;
        Reject([&] { Decode(f, l); });
        ++passed;
        l = ModelConvexCollisionLimits();
        l.hulls = 0;
        Reject([&] { Decode(f, l); });
        l = ModelConvexCollisionLimits();
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
        l.maximumRadiusGameUnits = 1;
        Reject([&] { Decode(f, l); });
        ++passed;
        bad = f;
        U(bad.data, 704 + 4, 16);
        U(bad.data, 64 + 44, 8);
        Need(Decode(bad).localPrimitives[0].geometry.rawContents == 16,
             "List override contents fabricated");
        ++passed;
        auto bytes = f.File();
        bytes.pop_back();
        Reject([&] { DecodeModelPrimitiveCollision(bytes, "models/synthetic/rounded"); });
        ++passed;
        std::cout << "Model primitive collision tests: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Model primitive collision tests failed after " << passed << " checks: " << e.what()
                  << '\n';
        return 1;
    }
}
} // namespace wolf2009

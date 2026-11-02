// SPDX-License-Identifier: GPL-3.0-or-later
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
void Need(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void Reject(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    Need(failed, "Unsupported/malformed model collision accepted");
}
void U(Bytes &b, std::size_t o, std::uint32_t v) {
    Need(o + 4 <= b.size(), "Synthetic writer bounds");
    for (int i = 0; i < 4; ++i)
        b[o + i] = static_cast<unsigned char>(v >> (8 * i));
}
void F(Bytes &b, std::size_t o, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    U(b, o, bits);
}
void Text(Bytes &b, std::size_t o, const std::string &s) {
    Need(o + s.size() < b.size(), "Synthetic string bounds");
    std::copy(s.begin(), s.end(), b.begin() + o);
    b[o + s.size()] = 0;
}
std::size_t Align(std::size_t n) {
    return (n + 15) / 16 * 16;
}
// Hand-authored synthetic asset-body packfile. Nonzero COM is intentional:
// decoding must leave the translated shape local instead of placing it at COM.
struct Fixture {
    Bytes data;
    std::map<std::uint32_t, std::uint32_t> links;
    std::map<std::uint32_t, std::string> types;
    Fixture() : data(1792, 0) {
        types[0] = "hkRootLevelContainer";
        types[64] = "hkpRigidBody";
        types[576] = "hkpMoppBvTreeShape";
        types[640] = "hkpListShape";
        types[736] = "hkpConvexTranslateShape";
        types[800] = "hkpBoxShape";
        links[64 + 112] = 880;
        Text(data, 880, "models/synthetic/platform");
        links[64 + 16] = 576;
        links[576 + 52] = 640;
        links[640 + 24] = 704;
        U(data, 640 + 28, 1);
        links[704] = 736;
        links[736 + 24] = 800;
        F(data, 736 + 40, -.0254f);
        F(data, 800 + 32, .0508f);
        F(data, 800 + 36, .0762f);
        F(data, 800 + 40, .0254f);
        for (int i = 0; i < 3; ++i)
            F(data, 64 + 224 + i * 20, 1);
        F(data, 64 + 284, 1);
        for (int i = 0; i < 3; ++i) {
            F(data, 64 + 288 + i * 4, float(i + 1));
            F(data, 64 + 304 + i * 4, float(i + 1));
        }
        F(data, 64 + 332, 1);
        F(data, 64 + 348, 1);
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
                          global = Align(local + links.size() * 8), end = Align(global + types.size() * 12);
        Bytes bytes(absolute + end, 255);
        std::fill(bytes.begin(), bytes.begin() + 208, 0);
        U(bytes, 0, 0x57e0e057);
        U(bytes, 4, 0x10c0c010);
        U(bytes, 12, 5);
        bytes[16] = 4;
        bytes[17] = 1;
        bytes[18] = 0;
        bytes[19] = 1;
        U(bytes, 20, 3);
        U(bytes, 24, 2);
        U(bytes, 32, 0);
        U(bytes, 36, offsets["hkRootLevelContainer"]);
        Text(bytes, 40, "Havok-5.6.0-r1");
        const char *tags[] = {"__classnames__", "__types__", "__data__"};
        for (int i = 0; i < 3; ++i) {
            const std::size_t h = 64 + i * 48;
            Text(bytes, h, tags[i]);
            U(bytes, h + 20, static_cast<std::uint32_t>(i == 0 ? 208 : absolute));
            for (int k = 0; k < 6; ++k)
                U(bytes, h + 24 + k * 4,
                  static_cast<std::uint32_t>(i == 0   ? names.size()
                                             : i == 1 ? 0
                                             : k == 0 ? local
                                             : k <= 2 ? global
                                                      : end));
        }
        std::copy(names.begin(), names.end(), bytes.begin() + 208);
        std::copy(data.begin(), data.end(), bytes.begin() + absolute);
        std::size_t p = absolute + local;
        for (const auto &link : links) {
            U(bytes, p, link.first);
            U(bytes, p + 4, link.second);
            p += 8;
        }
        p = absolute + global;
        for (const auto &t : types) {
            U(bytes, p, t.first);
            U(bytes, p + 4, 0);
            U(bytes, p + 8, offsets[t.second]);
            p += 12;
        }
        return bytes;
    }
};
ModelCollision Decode(const Fixture &f, const StaticCollisionLimits &l = StaticCollisionLimits()) {
    return DecodeModelCollision(f.File(), "models/synthetic/platform", l);
}
} // namespace
int RunModelCollisionTests() {
    int passed = 0;
    try {
        Fixture f;
        ModelCollision m = Decode(f);
        Need(m.localPoseVerified && m.geometryComplete && !m.contentsComplete && m.localBoxes.size() == 1 &&
                 m.sourceBodyOffset == 64 && m.rootShapeOffset == 576 && m.visitedShapes == 4,
             "Model-local profile/coverage failed");
        Need(std::abs(m.localBoxes[0].minimum.x + 2) < 1e-5 &&
                 std::abs(m.localBoxes[0].minimum.y + 3) < 1e-5 && std::abs(m.localBoxes[0].maximum.z) < 1e-5,
             "COM was incorrectly added to local shape coordinates");
        ++passed;
        Need(m.localBoxes[0].sourceShapeOffset == 800 && !m.localBoxes[0].contentsKnown &&
                 m.localBoxes[0].contents == 0 && m.unsupportedGeometry.empty(),
             "Unknown entity flags became solid or geometric omissions");
        ++passed;
        m = DecodeModelCollision(f.File(), "MODELS\\SYNTHETIC\\PLATFORM");
        Need(m.selectedBodyName == "models/synthetic/platform", "Explicit normalized model selection failed");
        Reject([&] { DecodeModelCollision(f.File(), "maps/game/synthetic/world"); });
        Reject([&] { DecodeModelCollision(f.File(), "models/synthetic/missing"); });
        ++passed;
        Fixture duplicate = f;
        duplicate.types[1280] = "hkpRigidBody";
        duplicate.links[1280 + 112] = 880;
        Reject([&] { Decode(duplicate); });
        ++passed;
        Reject([&] { DecodeStaticCollision(f.File(), "models/synthetic/platform"); });
        Fixture pose = f;
        F(pose.data, 64 + 272, 1);
        Reject([&] { Decode(pose); });
        pose = f;
        F(pose.data, 64 + 284, 0);
        Reject([&] { Decode(pose); });
        ++passed;
        pose = f;
        F(pose.data, 64 + 224, 2);
        Reject([&] { Decode(pose); });
        pose = f;
        F(pose.data, 64 + 332, 0);
        Reject([&] { Decode(pose); });
        ++passed;
        pose = f;
        F(pose.data, 64 + 304, 4);
        Reject([&] { Decode(pose); });
        pose = f;
        F(pose.data, 64 + 300, 1);
        Reject([&] { Decode(pose); });
        pose = f;
        F(pose.data, 64 + 288, (std::numeric_limits<float>::quiet_NaN)());
        Reject([&] { Decode(pose); });
        ++passed;
        Fixture rounded = f;
        F(rounded.data, 800 + 16, .05f);
        m = Decode(rounded);
        Need(!m.geometryComplete && m.localBoxes.empty() &&
                 m.unsupportedGeometry["box-with-unverified-convex-radius"] == 1,
             "Rounded box silently substituted with AABB");
        ++passed;
        for (const char *type : {"hkpConvexVerticesShape", "hkpCylinderShape", "hkpConvexTransformShape",
                                 "hkpStorageExtendedMeshShape"}) {
            Fixture unsupported = f;
            unsupported.types[800] = type;
            m = Decode(unsupported);
            Need(!m.geometryComplete && m.localBoxes.empty() && m.unsupportedGeometry[type] == 1,
                 "Unsupported local shape coverage incorrect");
        }
        ++passed;
        Fixture direct = f;
        direct.links[64 + 16] = 800;
        U(direct.data, 64 + 44, CollisionPlayerClip);
        m = Decode(direct);
        Need(m.geometryComplete && m.contentsComplete && m.localBoxes[0].contentsKnown &&
                 m.localBoxes[0].contents == 8 && std::abs(m.localBoxes[0].maximum.z - 1) < 1e-5,
             "Direct local box/body contents failed");
        ++passed;
        Fixture shared = f;
        U(shared.data, 640 + 28, 2);
        shared.links[720] = 736;
        U(shared.data, 64 + 44, 3);
        U(shared.data, 704 + 4, CollisionPlayerClip);
        U(shared.data, 720 + 4, CollisionMonsterClip);
        m = Decode(shared);
        Need(m.geometryComplete && m.contentsComplete && m.localBoxes.size() == 2 &&
                 m.localBoxes[0].sourceShapeOffset == m.localBoxes[1].sourceShapeOffset &&
                 m.localBoxes[0].contents == 8 && m.localBoxes[1].contents == 16,
             "Shared local shape lost per-instance contents");
        ++passed;
        Fixture broken = f;
        broken.links[736 + 24] = 640;
        Reject([&] { Decode(broken); });
        broken = f;
        broken.links.erase(64 + 16);
        Reject([&] { Decode(broken); });
        Bytes truncated = f.File();
        truncated.pop_back();
        Reject([&] { DecodeModelCollision(truncated, "models/synthetic/platform"); });
        ++passed;
        StaticCollisionLimits limits;
        limits.shapeVisits = 3;
        Reject([&] { Decode(f, limits); });
        limits = StaticCollisionLimits();
        limits.boxes = 0;
        Reject([&] { Decode(f, limits); });
        limits = StaticCollisionLimits();
        limits.transforms = 0;
        Reject([&] { Decode(f, limits); });
        ++passed;
        limits = StaticCollisionLimits();
        limits.coordinateGameUnits = 1;
        Reject([&] { Decode(f, limits); });
        limits = StaticCollisionLimits();
        limits.coordinateGameUnits = (std::numeric_limits<double>::quiet_NaN)();
        Reject([&] { Decode(f, limits); });
        ++passed;
        std::cout << "Model collision self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Model collision self-test failed after " << passed << " checks: " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldCollisionGeometry.h"
#include "MapPhysics.h"
#include "../../tests/HavokTestFixture.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace wolf2009;
using namespace wolf2009_test;
void Check(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void Reject(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    Check(rejected, "Malformed storage material profile accepted");
}
struct Fixture : HavokFixture {
    enum : std::uint32_t { body = 64, storage = 576, sub = 1024, owned = 1408, box = 2048 };
    Fixture() : HavokFixture(4096) {
        types[body] = "hkpRigidBody";
        types[storage] = "hkpStorageExtendedMeshShape";
        types[owned] = "hkpStorageExtendedMeshShapeShapeSubpartStorage";
        types[box] = "hkpBoxShape";
        Text(data, 800, "maps/synthetic/materials");
        links[body + 112] = 800;
        links[body + 16] = storage;
        for (int k = 0; k < 3; ++k)
            F(data, body + 224 + k * 20, 1);
        F(data, body + 332, 1);
        F(data, body + 348, 1);
        U(data, body + 44, 3);
        for (int k = 0; k < 3; ++k)
            F(data, storage + 96 + k * 4, 1);
        links[storage + 160] = sub;
        U(data, storage + 164, 1);
        links[storage + 204] = 1376;
        U(data, storage + 208, 1);
        links[1376] = owned;
        U(data, sub, 0x00000101u);
        U(data, sub + 8, 0x00010000u);
        links[sub + 16] = 1200;
        U(data, sub + 20, 1);
        links[1200] = box;
        const float q = std::sqrt(.5f);
        F(data, sub + 40, q);
        F(data, sub + 44, q);
        F(data, sub + 48, .254f);
        F(data, sub + 52, .508f);
        F(data, sub + 56, .762f);
        // Separate arrays referring to the same shape, as in serialized data.
        links[owned + 8] = 1536;
        U(data, owned + 12, 1);
        links[1536] = box;
        links[owned + 20] = 1568;
        U(data, owned + 24, 1);
        data[1568] = 0;
        links[owned + 32] = 1584;
        U(data, owned + 36, 1);
        U(data, 1584, 3);
        F(data, box + 32, .0254f);
        F(data, box + 36, .0508f);
        F(data, box + 40, .0762f);
    }
};
WorldCollisionGeometry
Decode(const Fixture &f, const WorldCollisionGeometryLimits &limits = WorldCollisionGeometryLimits()) {
    return DecodeWorldCollisionGeometry(f.File(), "maps/synthetic/materials", limits);
}
void Unknown(const Fixture &f, std::size_t count = 1) {
    const auto s = Decode(f);
    Check(s.geometryComplete && !s.contentsComplete && !s.coverageComplete &&
              s.primitives.size() == count,
          "Unknown material profile changed geometry or claimed coverage");
    for (const auto &p : s.primitives)
        Check(!p.local.contentsKnown && !p.world.contentsKnown, "Unknown storage material was inferred");
}
void Synthetic() {
    Fixture f;
    const auto s = Decode(f);
    Check(s.coverageComplete && s.primitives.size() == 1 && s.primitives[0].local.contentsKnown &&
              s.primitives[0].world.contentsKnown && s.primitives[0].world.rawContents == 3 &&
              s.primitives[0].transforms.size() == 1 &&
              std::abs(s.primitives[0].world.vertices[0].x - 12) < .0001,
          "Singleton material or rotated/translated shape lost data");
    auto changed = f;
    U(changed.data, 1584, 0xa5c3017fu);
    Check(Decode(changed).primitives[0].world.rawContents == 0xa5c3017fu, "Raw uint32 material changed");
    U(changed.data, 1584, 0);
    Check(Decode(changed).primitives[0].world.contentsKnown,
          "Valid raw zero material confused with an unknown profile");
    changed = f;
    U(changed.data, Fixture::sub, 0x00040101u);
    U(changed.data, Fixture::sub + 8, 0x00010001u);
    Check(Decode(changed).coverageComplete, "Existing explicit-stride material profile regressed");
    changed = f;
    U(changed.data, Fixture::sub + 20, 2);
    U(changed.data, Fixture::owned + 12, 2);
    U(changed.data, Fixture::owned + 24, 2);
    changed.links[1204] = Fixture::box;
    changed.links[1540] = Fixture::box;
    Unknown(changed, 2);
    changed = f;
    U(changed.data, Fixture::sub + 8, 0x00020000u);
    U(changed.data, Fixture::owned + 36, 2);
    Unknown(changed);
    changed = f;
    U(changed.data, Fixture::sub, 0x00000201u);
    Unknown(changed);
    changed = f;
    U(changed.data, Fixture::sub + 4, 4);
    Unknown(changed);
    changed = f;
    changed.links[Fixture::sub + 12] = 1584;
    Unknown(changed);
    changed = f;
    U(changed.data, Fixture::owned + 48, 1);
    Unknown(changed);
    changed = f;
    U(changed.data, Fixture::owned + 24, 2);
    Unknown(changed);
    changed = f;
    changed.data[1568] = 1;
    Reject([&] { Decode(changed); });
    changed = f;
    changed.links[1536] = Fixture::storage;
    Reject([&] { Decode(changed); });
    changed = f;
    changed.links.erase(Fixture::owned + 20);
    Reject([&] { Decode(changed); });
    changed = f;
    changed.links[Fixture::owned + 32] = 4095;
    Reject([&] { Decode(changed); });
    WorldCollisionGeometryLimits limits;
    limits.graph.compoundEntries = 1;
    changed = f;
    U(changed.data, Fixture::owned + 36, 2);
    Reject([&] { Decode(changed, limits); });
    std::cout << "WOLF2009_WORLD_MATERIAL_PROFILE_SYNTHETIC_OK\n";
}
void Original(const char *path) {
    std::ifstream file(path, std::ios::binary);
    Check(bool(file), "Cannot open supplied map package");
    const auto archive = Archive::Scan(file);
    std::string name;
    for (const auto &chunk : archive.Chunks())
        if (chunk.type == "PROC")
            for (const auto &entry : chunk.entries) {
                Check(name.empty(), "Ambiguous map resource");
                name = entry.name;
            }
    const auto scene =
        DecodeWorldCollisionGeometry(ReadMapResource(file, archive, name, "HKXS", 64u << 20), name);
    std::size_t known = 0, unknown = 0;
    std::map<std::uint32_t, std::size_t> words;
    for (const auto &p : scene.primitives) {
        if (p.world.contentsKnown) {
            ++known;
            ++words[p.world.rawContents];
        } else
            ++unknown;
    }
    std::cout << "WOLF2009_WORLD_MATERIAL_PROFILE_REAL_OK map=" << name
              << " primitives=" << scene.primitives.size() << " known=" << known
              << " unknown=" << unknown << " contentsComplete=" << scene.contentsComplete
              << " geometryComplete=" << scene.geometryComplete
              << " coverageComplete=" << scene.coverageComplete << '\n';
    for (const auto &w : words)
        std::cout << "RawMaterial " << w.first << ' ' << w.second << '\n';
    for (const auto &u : scene.unsupported)
        std::cout << "Unsupported " << u.first << ' ' << u.second << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        Synthetic();
        for (int i = 1; i < argc; ++i)
            Original(argv[i]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "World material profile test failed: " << error.what() << '\n';
        return 1;
    }
}

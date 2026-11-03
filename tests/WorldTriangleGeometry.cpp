// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldTriangleGeometry.h"
#include "HavokTestFixture.h"
#include "MapPhysics.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace wolf2009;
using namespace wolf2009_test;
bool Near(double a, double b) { return std::abs(a - b) < .00002; }
template <class F> void Reject(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    Need(bad, "Invalid triangle world join accepted");
}
struct Fixture : HavokFixture {
    Fixture() : HavokFixture(4096) {
        types[64] = "hkpRigidBody";
        types[576] = "hkpStorageExtendedMeshShape";
        types[1408] = "hkpStorageExtendedMeshShapeMeshSubpartStorage";
        Text(data, 800, "maps/synthetic/triangles");
        links[64 + 112] = 800;
        links[64 + 16] = 576;
        for (int k = 0; k < 3; ++k)
            F(data, 64 + 224 + k * 20, 1);
        F(data, 64 + 332, 1);
        F(data, 64 + 348, 1);
        U(data, 64 + 44, 3);
        for (int k = 0; k < 4; ++k)
            F(data, 576 + 96 + k * 4, 1);
        links[576 + 148] = 1024;
        U(data, 576 + 152, 1);
        links[576 + 192] = 1376;
        U(data, 576 + 196, 1);
        links[1376] = 1408;
        U(data, 1024 + 16, 1);
        U(data, 1024 + 24, 16);
        U(data, 1024 + 28, 3);
        F(data, 1024 + 32, .0254f);
        F(data, 1024 + 36, .0508f);
        U(data, 1024 + 52, 16);
        U(data, 1024 + 56, 2);
        U(data, 1024 + 60, 7);
        links[1408 + 8] = 1800;
        U(data, 1408 + 12, 3);
        links[1408 + 32] = 1900;
        U(data, 1408 + 36, 4);
        F(data, 1800, .0254f);
        F(data, 1816 + 4, .0508f);
        F(data, 1832 + 8, .0762f);
        U(data, 1812, 0x7fc01234u);
        U(data, 1900, 0);
        U(data, 1904, 1);
        U(data, 1908, 2);
        U(data, 1912, 0xdeadbeefu);
    }
};
WorldCollisionGeometry World(const Bytes &bytes) {
    return DecodeWorldCollisionGeometry(bytes, "maps/synthetic/triangles");
}
void Synthetic() {
    Fixture fixture;
    const auto bytes = fixture.File();
    const auto world = World(bytes);
    const auto result = DecodeWorldTriangleGeometry(bytes, world);
    Need(result.parts.size() == 1 && result.allReportedOccurrencesPlaced &&
             result.reportedTriangles == 1 && result.placedTriangles == 1 && result.placedVertices == 3,
         "Reported world triangle was not placed");
    const auto &part = result.parts[0];
    const auto &source = result.buffers.storages[part.storageIndex].subparts[part.subpartIndex];
    Need(Near(part.worldVertices[0].x, 1) && Near(part.worldVertices[1].y, 2) &&
             Near(part.worldVertices[2].z, 3) && source.vertices[0].rawWBits == 0x7fc01234u &&
             source.triangles[0].strideTail == Bytes({0xef, 0xbe, 0xad, 0xde}),
         "Placed positions changed raw topology, W bits or index padding");
    Need(source.indexType == 2 && source.rawFlipAlternateTriangles == 0 &&
             source.rawTriangleOffset == 7 && Near(part.worldExtrusion.x, 1) &&
             Near(part.worldExtrusion.y, 2),
         "Reflected triangle fields not retained");
    auto graph = world;
    auto &placed = graph.omissions[0];
    // Independently authored checked-graph data exercises instance placement,
    // without inventing an HKXS transform-wrapper layout for concave meshes.
    placed.placement.axis[0] = {0, 1, 0};
    placed.placement.axis[1] = {-1, 0, 0};
    placed.placement.axis[2] = {0, 0, 1};
    placed.placement.translation = {10, 20, 30};
    auto duplicate = placed;
    duplicate.occurrence++;
    duplicate.placement.translation.x = 40;
    graph.omissions.push_back(duplicate);
    const auto instances = DecodeWorldTriangleGeometry(bytes, graph);
    Need(instances.buffers.storages.size() == 1 && instances.parts.size() == 2 &&
             instances.placedTriangles == 2 && instances.placedVertices == 6 &&
             Near(instances.parts[0].worldVertices[0].x, 10) &&
             Near(instances.parts[0].worldVertices[0].y, 21) &&
             Near(instances.parts[1].worldVertices[0].x, 40) &&
             Near(instances.parts[0].worldExtrusion.x, -2) &&
             Near(instances.parts[0].worldExtrusion.y, 1) &&
             instances.parts[0].sourceRegion != instances.parts[1].sourceRegion,
         "Instances collapsed or extrusion translated");
    Need(!world.coverageComplete && world.omittedTriangles == 1,
         "Triangle data join changed physical world coverage");
    auto flipped = fixture;
    flipped.data[1024 + 57] = 1;
    const auto flip = DecodeWorldTriangleGeometry(flipped.File(), World(flipped.File()));
    const auto &flipSource = flip.buffers.storages[0].subparts[0];
    Need(flipSource.indexType == 2 && flipSource.rawIndexType == 0x102 &&
             flipSource.rawFlipAlternateTriangles == 1 && flipSource.triangles[0].indices[0] == 0 &&
             flipSource.triangles[0].indices[2] == 2,
         "Flip byte was conflated with index type or applied as an assumed winding rule");
    auto unknown = fixture;
    U(unknown.data, 1024 + 56, 3);
    const auto unknownBytes = unknown.File();
    const auto skipped = DecodeWorldTriangleGeometry(unknownBytes, World(unknownBytes));
    Need(!skipped.allReportedOccurrencesPlaced && skipped.placedTriangles == 0 &&
             skipped.parts.size() == 1 &&
             skipped.buffers.storages[0].subparts[0].arrays[2].bytes.size() == 16,
         "Unknown profile was guessed or raw data lost");
    auto badExtrusion = fixture;
    U(badExtrusion.data, 1024 + 32, 0x7fc00000u);
    const auto invalidBytes = badExtrusion.File();
    const auto invalid = DecodeWorldTriangleGeometry(invalidBytes, World(invalidBytes));
    Need(invalid.parts[0].positionsPlaced && !invalid.parts[0].extrusionPlaced &&
             !invalid.allReportedOccurrencesPlaced,
         "Nonfinite extrusion was normalized or treated as placed");
    auto changed = fixture;
    changed.data[3000] = 1;
    Reject([&] { DecodeWorldTriangleGeometry(changed.File(), world); });
    auto bad = world;
    bad.identityBodyPoseVerified = false;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, bad); });
    bad = world;
    bad.omissions[0].sourceSubpartIndex = 99;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, bad); });
    bad = world;
    bad.omissions[0].triangles = 2;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, bad); });
    bad = world;
    bad.omissions[0].placement.axis[0].x = -1;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, bad); });
    WorldTriangleGeometryLimits limits;
    limits.worldVertices = 2;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, world, limits); });
    limits = WorldTriangleGeometryLimits();
    limits.occurrences = 1;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, graph, limits); });
    limits = WorldTriangleGeometryLimits();
    limits.worldTriangles = 1;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, graph, limits); });
    limits = WorldTriangleGeometryLimits();
    limits.coordinateGameUnits = .5;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, world, limits); });
    auto noRequests = world;
    noRequests.omissions.clear();
    const auto empty = DecodeWorldTriangleGeometry(bytes, noRequests);
    Need(empty.parts.empty() && empty.buffers.storages.empty() && empty.reportedTriangles == 0,
         "Empty selected-body scope decoded unrelated storage objects");
    limits = WorldTriangleGeometryLimits();
    limits.buffers.sourceBytes = bytes.size() - 1;
    Reject([&] { DecodeWorldTriangleGeometry(bytes, noRequests, limits); });
    std::cout << "WOLF2009_WORLD_TRIANGLES_SYNTHETIC_OK\n";
}
void Original(const char *path) {
    std::ifstream input(path, std::ios::binary);
    Need(bool(input), "Cannot open source map");
    const auto archive = Archive::Scan(input);
    std::string name;
    for (const auto &c : archive.Chunks())
        if (c.type == "PROC")
            for (const auto &e : c.entries) {
                Need(name.empty(), "Ambiguous map resource");
                name = e.name;
            }
    const auto bytes = ReadMapResource(input, archive, name, "HKXS", 64u << 20);
    const auto world = DecodeWorldCollisionGeometry(bytes, name);
    const auto result = DecodeWorldTriangleGeometry(bytes, world);
    Need(result.reportedTriangles == world.omittedTriangles &&
             result.placedTriangles == result.reportedTriangles && result.allReportedOccurrencesPlaced,
         "Original reported triangle buffers were not completely placed");
    std::size_t extrusion = 0, flips = 0, offsets = 0;
    for (const auto &part : result.parts) {
        const auto &s = result.buffers.storages[part.storageIndex].subparts[part.subpartIndex];
        if (s.sourceExtrusion.x || s.sourceExtrusion.y || s.sourceExtrusion.z)
            ++extrusion;
        if (s.rawFlipAlternateTriangles)
            ++flips;
        if (s.rawTriangleOffset)
            ++offsets;
    }
    std::cout << "WOLF2009_WORLD_TRIANGLES_REAL_OK map=" << name
              << " storages=" << result.buffers.storages.size() << " occurrences=" << result.parts.size()
              << " vertices=" << result.placedVertices << " triangles=" << result.placedTriangles
              << " extrusionParts=" << extrusion << " flipParts=" << flips
              << " triangleOffsetParts=" << offsets
              << " remainingWorldOmissions=" << world.omissions.size() - result.parts.size()
              << " nativeCollisionVerified=0\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        Synthetic();
        for (int i = 1; i < argc; ++i)
            Original(argv[i]);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "World triangle geometry: " << e.what() << '\n';
        return 1;
    }
}

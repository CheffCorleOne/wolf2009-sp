// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_WORLD_TRIANGLE_GEOMETRY_H
#define WOLF2009_WORLD_TRIANGLE_GEOMETRY_H
#include "HavokTriangleMesh.h"
#include "WorldCollisionGeometry.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
struct WorldTriangleGeometryLimits {
    HavokTriangleMeshLimits buffers;
    std::size_t occurrences = 100000, worldVertices = 2000000, worldTriangles = 2000000;
    double coordinateGameUnits = 10000000;
};
struct WorldTrianglePart {
    // Indices into the owning result's buffers, not dangling source pointers.
    std::size_t storageIndex = 0, subpartIndex = 0;
    std::uint64_t sourceRegion = 0, occurrence = 0;
    CollisionRigidTransform placement;
    std::vector<CollisionVec3> worldVertices;
    CollisionVec3 worldExtrusion; // rotation only; never add translation
    bool positionsPlaced = false, extrusionPlaced = false;
};
struct WorldTriangleGeometry {
    std::string sourceBodyName;
    std::uint32_t sourceBodyOffset = 0;
    HavokTriangleMeshDocument buffers;
    std::vector<WorldTrianglePart> parts;
    std::size_t reportedTriangles = 0, placedTriangles = 0, placedVertices = 0;
    bool allReportedOccurrencesPlaced = false;
};

// Join only triangle occurrences reported by the checked selected-body walk
// to their exact typed storage/subpart buffers. Shared storages are decoded
// once, while each graph occurrence keeps its own rigid world placement.
// Raw index order, flip byte, extrusion and triangle offset remain available
// in buffers. This places data; it does not infer winding, welding, material
// contents or native collision shapes, and does not change world coverage.
WorldTriangleGeometry
PlaceWorldTriangleGeometry(const WorldCollisionGeometry &world, HavokTriangleMeshDocument buffers,
                           const WorldTriangleGeometryLimits &limits = WorldTriangleGeometryLimits());
WorldTriangleGeometry
DecodeWorldTriangleGeometry(const std::vector<unsigned char> &hkxs, const WorldCollisionGeometry &world,
                            const WorldTriangleGeometryLimits &limits = WorldTriangleGeometryLimits());
} // namespace wolf2009
#endif

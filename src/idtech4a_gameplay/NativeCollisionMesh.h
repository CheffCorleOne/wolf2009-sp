// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_COLLISION_MESH_H
#define WOLF2009_NATIVE_COLLISION_MESH_H
#include "ConvexContact.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
enum class NativeCollisionSides { Front, Both };
struct NativeCollisionTriangle {
    // Mathematical CCW order: the collidable front has normal (b-a) x (c-a).
    // The caller must establish source query policy; this adapter never infers
    // it from render winding. Both explicitly emits independent front/back
    // edge sheets so coplanar seams retain their correct adjacency on each.
    std::uint32_t vertex[3] = {0, 0, 0};
    std::uint32_t contents = 0;
    NativeCollisionSides sides = NativeCollisionSides::Front;
};
struct NativeCollisionMesh {
    std::vector<CollisionVec3> vertices; // game units, projected to native float32
    std::vector<NativeCollisionTriangle> triangles;
};
struct NativeCollisionMeshLimits {
    std::size_t vertices = 2000000, triangles = 2000000, edges = 3000000, textBytes = 256u << 20;
    double coordinate = 10000000;
    // A float32 plane at a large world offset can miss its own vertices.
    // Reject that projection instead of inventing a shift or widening traces.
    double planeProjectionError = 1.0 / 16;
};
struct NativeCollisionMeshFiles {
    std::string cmText;
    std::vector<std::uint32_t> vertexRemap;
    std::size_t vertices = 0, edges = 0, polygons = 0, internalEdges = 0; // edges excludes reserved zero
};

// Standard Doom3 CM 1.00 consumed by LoadModel/LoadMap. Exact float32 position
// equality may share vertices; there is no epsilon welding, hull conversion,
// thickness or radius approximation. Degenerate faces are errors, not holes.
// Shared, oppositely directed edges are internal only for two faces with equal
// encoded native planes and contents. Other edges remain explicit contacts.
// The initial adapter uses a leaf node: correct narrowphase, no BSP speedup.
// mapCRC is the caller's actual idMapFile geometry CRC. Zero disables native
// CRC validation and must never be described as source provenance validation.
NativeCollisionMeshFiles
BuildNativeCollisionMesh(const NativeCollisionMesh &mesh, const std::string &modelName,
                         std::uint32_t mapCRC = 0,
                         const NativeCollisionMeshLimits &limits = NativeCollisionMeshLimits());
} // namespace wolf2009
#endif

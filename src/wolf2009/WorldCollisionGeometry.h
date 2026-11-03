// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_WORLD_COLLISION_GEOMETRY_H
#define WOLF2009_WORLD_COLLISION_GEOMETRY_H
#include "ConvexContact.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wolf2009 {
struct CollisionRigidTransform {
    // Basis rows: world = translation + x*axis[0] + y*axis[1] + z*axis[2].
    CollisionVec3 axis[3] = {{1,0,0},{0,1,0},{0,0,1}};
    CollisionVec3 translation; // GAME UNITS, Z up
};
enum class WorldCollisionTransformKind { Translate, ConvexMatrix, StorageQuaternion };
struct WorldCollisionTransformRecord {
    WorldCollisionTransformKind kind = WorldCollisionTransformKind::Translate;
    std::uint32_t sourceObjectOffset = 0;
    std::size_t sourceSubpartIndex = static_cast<std::size_t>(-1);
    CollisionRigidTransform local;
    // Original quaternion is preserved for storage placements. Matrix and
    // translate wrappers have no quaternion payload in the observed profile.
    double quaternion[4] = {0,0,0,1};
    double radius = 0;
    bool hasRadius = false;
};
enum class WorldCollisionPrimitiveKind { Box, ConvexVertices };
struct WorldCollisionPrimitive {
    WorldCollisionPrimitiveKind kind = WorldCollisionPrimitiveKind::Box;
    ConvexContactHull local, world;
    CollisionRigidTransform placement;
    std::vector<WorldCollisionTransformRecord> transforms;
    // Occurrence differs for shared source shapes reached through different
    // compound links. sourceRegion also fingerprints bounded bytes/body/path.
    std::uint64_t occurrence = 0;
    bool radiusHierarchyVerified = true;
    bool sourcePlanesConsistent = true;
    bool volumeProfileVerified = true;
    double maximumPlaneVertexResidual = 0;
    double float32ErrorBoundAtMaximumResidual = 0;
    double maximumPlaneVertexBoundExcess = 0;
};
struct WorldCollisionOmission {
    std::string type;
    std::uint32_t sourceShapeOffset = 0;
    std::size_t sourceSubpartIndex = static_cast<std::size_t>(-1), triangles = 0;
    std::uint64_t occurrence = 0;
    CollisionRigidTransform placement;
    std::uint32_t rawContents = 0;
    bool contentsKnown = false;
};
struct WorldCollisionGeometryLimits {
    StaticCollisionLimits graph;
    std::size_t primitives = 100000, omissions = 100000;
    std::size_t verticesPerHull = 1024, planesPerHull = 1024;
    std::size_t totalVertices = 2000000, totalPlanes = 2000000;
    std::size_t planeVertexChecks = 50000000;
    double maximumRadiusGameUnits = 1000;
    // Roundoff allowance for source float vertices/planes, not a shape offset.
    double minimumPlaneToleranceGameUnits = 0;
    WorldCollisionGeometryLimits() {
        // Independent bounded world-data reader; the legacy walker and its
        // existing budgets are unchanged. Compound placements count visits.
        graph.shapeVisits = 300000;
        graph.transforms = 100000;
    }
};
struct WorldCollisionGeometry {
    std::uint64_t sourceFingerprint = 0; // same bounded source bytes as the typed readers
    std::size_t sourceBytes = 0;
    std::string selectedBodyName;
    std::uint32_t sourceBodyOffset = 0, rootShapeOffset = 0;
    std::vector<WorldCollisionPrimitive> primitives;
    std::vector<WorldCollisionOmission> omissions;
    bool identityBodyPoseVerified = false;
    bool geometryComplete = false, contentsComplete = true, coverageComplete = false;
    std::size_t otherSourceRigidBodies = 0, visitedShapes = 0, transformVisits = 0;
    std::size_t omittedTriangles = 0;
    std::map<std::string,std::size_t> unsupported, shapeOccurrences;
};

// Opt-in, data-only decoder for the same observed HKXS 5.6/32-bit LE profile
// as the checked model readers. Reads complete selected-body shape traversal:
// boxes, ConvexVertices, MOPP child links, List, ConvexTranslate, proper rigid
// ConvexTransform, and StorageExtendedMesh shape subparts with quaternion
// placements. Local/world hulls, original planes/radii and raw contents survive.
// World rigid-body pose must pass the existing measured identity profile.
// Triangles, cylinders, spheres, scaling and unknown classes remain explicit
// omissions. No MOPP interpretation, runtime entity filter, collision query,
// movement controller, rounded-hull native import or clear passage is claimed.
WorldCollisionGeometry DecodeWorldCollisionGeometry(
    const std::vector<unsigned char> &hkxs, const std::string &exactWorldBodyName,
    const WorldCollisionGeometryLimits &limits = WorldCollisionGeometryLimits());

CollisionVec3 TransformCollisionPoint(const CollisionRigidTransform &transform,CollisionVec3 p);
CollisionRigidTransform ComposeCollisionTransforms(const CollisionRigidTransform &parent,
                                                   const CollisionRigidTransform &child);
} // namespace wolf2009
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MODEL_CONVEX_COLLISION_H
#define WOLF2009_MODEL_CONVEX_COLLISION_H
#include "ConvexContact.h"
namespace wolf2009 {
struct ModelConvexCollisionLimits {
    StaticCollisionLimits graph;
    std::size_t hulls = 20000, verticesPerHull = 256, planesPerHull = 256;
    std::size_t totalVertices = 1000000, totalPlanes = 1000000;
    double maximumRadiusGameUnits = 1000;
};
struct ModelConvexCollision {
    std::string selectedBodyName;
    std::uint32_t sourceBodyOffset = 0, rootShapeOffset = 0;
    std::vector<ConvexContactHull> localHulls;
    bool localPoseVerified = false, geometryComplete = false, contentsComplete = false;
    std::size_t visitedShapes = 0;
    std::map<std::string, std::size_t> unsupportedGeometry;
    std::vector<std::string> diagnostics;
};
// Opt-in subset, sharing StaticCollision.cpp's checked packfile/graph reader.
// Only model-local direct or translated ConvexVertices shapes, in GAME UNITS.
// Boxes, cylinders, transforms and other shapes are omitted and counted; this
// does not replace DecodeModelCollision's independent box subset. No complete
// scene coverage or runtime contents is inferred. Source regions are bounded
// input/body provenance fingerprints, not cryptographic authentication.
ModelConvexCollision
DecodeModelConvexCollision(const std::vector<unsigned char> &hkxs, const std::string &exactModelBodyName,
                           const ModelConvexCollisionLimits &limits = ModelConvexCollisionLimits());
int RunModelConvexCollisionTests();
} // namespace wolf2009
#endif

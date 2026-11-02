// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MODEL_PRIMITIVE_COLLISION_H
#define WOLF2009_MODEL_PRIMITIVE_COLLISION_H
#include "ModelConvexCollision.h"
namespace wolf2009 {
enum class ModelPrimitiveKind { AxisBox, TransformedBox, ConvexHull };
struct ModelCollisionPrimitive {
    ConvexContactHull geometry;
    ModelPrimitiveKind kind = ModelPrimitiveKind::ConvexHull;
    // Transformed boxes retain both wrapper and typed child source. These
    // identify provenance, not a unique original trace's selected shape key.
    std::uint32_t sourceChildShapeOffset = 0;
};
struct ModelPrimitiveCollision {
    std::string selectedBodyName;
    std::uint32_t sourceBodyOffset = 0, rootShapeOffset = 0;
    std::vector<ModelCollisionPrimitive> localPrimitives;
    bool localPoseVerified = false, geometryComplete = false, contentsComplete = false;
    std::size_t visitedShapes = 0;
    std::map<std::string, std::size_t> unsupportedGeometry;
    std::vector<std::string> diagnostics;
};
// Opt-in measured model-local primitives, in GAME UNITS. Shared checked reader;
// limits.hulls counts all output primitives here. Supports ConvexVertices,
// Box including radius, Translate, proper ConvexTransform with direct Box child.
// Wrapper and child radii must agree and are applied once. Other classes are
// omitted/countable; unverified poses fail. Earlier decoders remain unchanged.
// No runtime contents, unique contact shape id or full walking is inferred.
ModelPrimitiveCollision
DecodeModelPrimitiveCollision(const std::vector<unsigned char> &hkxs,
                              const std::string &exactModelBodyName,
                              const ModelConvexCollisionLimits &limits = ModelConvexCollisionLimits());
int RunModelPrimitiveCollisionTests();
} // namespace wolf2009
#endif

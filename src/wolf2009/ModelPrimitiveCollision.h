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
struct ModelPrimitiveOmission {
    std::string type;
    StaticCollisionBox conservativeBounds;
    std::uint64_t sourceRegion = 0;
    bool boundsKnown = false;
};
struct ModelPrimitiveCollision {
    std::string selectedBodyName;
    std::uint32_t sourceBodyOffset = 0, rootShapeOffset = 0;
    std::vector<ModelCollisionPrimitive> localPrimitives;
    // Exclusion bounds only, never exact collision substitutes. Unknown or
    // overlapping omissions cannot establish a clear central movement path.
    std::vector<ModelPrimitiveOmission> omittedGeometryBounds;
    bool omittedBoundsComplete = false;
    bool localPoseVerified = false, geometryComplete = false, contentsComplete = false;
    std::size_t visitedShapes = 0;
    std::map<std::string, std::size_t> unsupportedGeometry;
    std::vector<std::string> diagnostics;
};
// Opt-in measured model-local primitives, in GAME UNITS. Shared checked reader;
// limits.hulls counts output primitives plus omission records here. Supports ConvexVertices,
// Box including radius, Translate, proper ConvexTransform with direct Box child.
// Wrapper and child radii must agree and are applied once. Other classes are
// omitted/countable; unverified poses fail. Earlier decoders remain unchanged.
// No runtime contents, unique contact shape id or full walking is inferred.
// Measured cylinders remain unsupported for exact contacts; conservative
// capsule enclosures may exclude them from a particular queried volume.
ModelPrimitiveCollision
DecodeModelPrimitiveCollision(const std::vector<unsigned char> &hkxs,
                              const std::string &exactModelBodyName,
                              const ModelConvexCollisionLimits &limits = ModelConvexCollisionLimits());
int RunModelPrimitiveCollisionTests();
} // namespace wolf2009
#endif

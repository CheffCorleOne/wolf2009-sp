// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MODEL_COLLISION_H
#define WOLF2009_MODEL_COLLISION_H
#include "StaticCollision.h"
namespace wolf2009 {
struct ModelCollision {
    std::string selectedBodyName;
    std::uint32_t sourceBodyOffset = 0, rootShapeOffset = 0;
    std::vector<StaticCollisionBox> localBoxes;
    bool localPoseVerified = false;
    // These scopes are independent: complete local geometry does not imply
    // known entity contents or complete world/character collision coverage.
    bool geometryComplete = false, contentsComplete = false;
    std::size_t visitedShapes = 0;
    std::map<std::string, std::size_t> unsupportedGeometry;
    std::vector<std::string> diagnostics;
};
// Shares the bounded HKXS parser and checked shape graph in StaticCollision.cpp.
// Selects exactly one named models/... body, returning model-local GAME UNITS.
// Only the measured identity/origin-zero asset pose is accepted. COM slots are
// validated, never interpreted as an instance translation. Runtime placement,
// bind state and per-entity contents belong to the caller.
// Supported: MOPP child links, List, ConvexTranslate, radius-zero Box. Rounded
// boxes, hulls, cylinders, ConvexTransform and storage compounds are explicitly
// omitted and counted. Unknown raw contents stay unknown; no mask qualification
// or SOLID bit is inferred from a runtime trace. This does not enable walking.
ModelCollision DecodeModelCollision(const std::vector<unsigned char> &hkxs,
                                    const std::string &exactModelBodyName,
                                    const StaticCollisionLimits &limits = StaticCollisionLimits());
int RunModelCollisionTests();
} // namespace wolf2009
#endif

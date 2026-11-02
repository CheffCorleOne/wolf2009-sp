// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MODEL_PLACEMENT_H
#define WOLF2009_MODEL_PLACEMENT_H
#include "Archive.h"
#include "Entities.h"
#include "Geometry.h"
#include <istream>
#include <string>
#include <vector>
namespace wolf2009 {
enum class ModelRotationConvention {
    IdentityOnly,
    // Explicit caller opt-in: world = origin + x*row0 + y*row1 + z*row2.
    // Matches upstream idTech basis axes; nonidentity Wolf runtime placement
    // is not independently established by this module.
    BasisRows
};
enum class ModelRotationSource { ImplicitIdentity, ZeroAngle, ExplicitMatrix };
struct ModelPlacementOptions {
    ModelRotationConvention rotation = ModelRotationConvention::IdentityOnly;
    GeometryLimits geometry;
};
struct PlacedStaticModel {
    EntityPlacement authored;
    std::string resourceName, skin, bind, brain;
    ModelRotationSource rotationSource = ModelRotationSource::ImplicitIdentity;
    bool nonIdentityRotation = false, angleSupersededByMatrix = false;
    bool authoredBasePoseOnly = true;
    bool unresolvedParent = false, unresolvedAnimation = false, unresolvedBehavior = false;
    bool unresolvedSkin = false;
    // Present only if the loader could find the parent uniquely in this ENTS
    // document. Its runtime transform is still not evaluated or applied.
    bool parentRecordFound = false, parentRecordAmbiguous = false;
    std::size_t entityIndex = static_cast<std::size_t>(-1);
    std::size_t parentEntityIndex = static_cast<std::size_t>(-1);
    std::vector<std::string> dynamicPropertyKeys;
    GeometryScene geometry; // Render vertices in authored/base world pose.
};
// Pure transform of an already decoded static model. Requires explicit origin;
// only proper rigid matrices are supported. Angle-only nonzero rotation,
// scaling/reflection and malformed geometry throw. UV/materials are preserved;
// skin remaps, bind parents, brains and animation are recorded, never applied.
PlacedStaticModel ApplyAuthoredModelPlacement(const GeometryScene &localGeometry, const EntityRecord &entity,
                                              const std::string &resourceName,
                                              const ModelPlacementOptions &options = ModelPlacementOptions());
// Selects a unique named entity and matching MODL resource in the supplied
// archive, decodes strict static MD5R1, resolves its PBBF in the same archive,
// then applies authored placement. All virtual-name lookups are bounded by
// existing Archive/Geometry limits; no host path or extraction is performed.
PlacedStaticModel LoadPlacedStaticModel1(const Archive &archive, std::istream &input,
                                         const EntityDocument &entities, const std::string &entityName,
                                         const ModelPlacementOptions &options = ModelPlacementOptions());
int RunModelPlacementTests();
} // namespace wolf2009
#endif

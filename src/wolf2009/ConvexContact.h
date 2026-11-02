// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_CONVEX_CONTACT_H
#define WOLF2009_CONVEX_CONTACT_H
#include "StaticCollision.h"
namespace wolf2009 {
struct ConvexContactPlane {
    CollisionVec3 normal;
    // Outward plane: dot(normal, point) + distance <= 0.
    double distance = 0;
};
struct ConvexContactHull {
    std::vector<CollisionVec3> vertices;
    std::vector<ConvexContactPlane> planes;
    double radius = 0;
    std::uint32_t sourceShapeOffset = 0, rawContents = 0;
    // Identifies bounded source bytes, selected body and graph occurrence.
    std::uint64_t sourceRegion = 0;
    bool contentsKnown = false;
};
struct ConvexContactQualification {
    std::string instanceId;
    // External CURRENT runtime evidence for THIS source face/entity and exact
    // mask, not raw contents reconstruction. Caller must revoke the witness
    // after entity clipping/state changes. Empty/zero fields are unqualified.
    std::uint32_t verifiedQueryMask = 0, sourceShapeOffset = 0;
    std::uint64_t sourceRegion = 0;
    std::size_t planeIndex = static_cast<std::size_t>(-1);
};
struct ConvexContactLimits {
    std::size_t vertices = 256, planes = 256;
    double coordinate = 10000000, geometryTolerance = .0001;
    double faceInteriorMargin = .001;
};
enum class ConvexContactStatus {
    ProvenFaceHit,
    NoHitForThisHull,
    IndeterminateEdge,
    IndeterminateInitialOverlap,
    Unqualified
};
struct ConvexContactResult {
    ConvexContactStatus status = ConvexContactStatus::Unqualified;
    // For IndeterminateEdge this is only a conservative candidate, not a hit.
    double fraction = 1;
    CollisionVec3 normal, center, hullContact;
    std::uint32_t sourceShapeOffset = 0;
    // Always false: this function never certifies an entire model/world path.
    bool sceneClearanceVerified = false;
};
// Translation-only sweep against convex(vertices) plus an optional sphere.
// Expanded planes supply a conservative candidate. A hit is reported only
// when an actual supporting point lies strictly inside a source face polygon.
// Only qualification.planeIndex is runtime-qualified to block; another face
// returns Unqualified. Rounded edges/corners and initial overlap are unknown.
// NoHitForThisHull is scoped to this one input hull, never a partial scene.
// Original point-ray probes use includeRadius=false; AABB shape casts use true.
// Invalid/nonfinite/unbounded input throws. No state is modified.
ConvexContactResult
SweepConvexContact(const ConvexContactHull &hull, const ConvexContactQualification &qualification,
                   CollisionVec3 translation, CollisionVec3 center, CollisionVec3 halfExtents,
                   CollisionVec3 delta, std::uint32_t queryMask = CollisionPlayerMask,
                   bool includeRadius = true, const ConvexContactLimits &limits = ConvexContactLimits());
int RunConvexContactTests();
} // namespace wolf2009
#endif

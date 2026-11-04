// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ROUNDED_CONVEX_H
#define WOLF2009_ROUNDED_CONVEX_H
#include "WorldCollisionGeometry.h"
#include <cstddef>
#include <vector>

namespace wolf2009 {
// Geometry only: convex(corePoints) plus a sphere of radius, in GAME UNITS.
// Points are borrowed during each query and are never cooked, shrunk or changed.
// Repeated/lower-dimensional cores, including a point or segment, are legal.
struct RoundedConvexShape {
    std::vector<CollisionVec3> corePoints;
    double radius = 0;
};
enum class RoundedConvexStatus { Hit, Separated, InitialOverlap, Indeterminate, Invalid };
enum class RoundedConvexReason {
    None, InvalidShape, InvalidTransform, InvalidLimits, NoClosestPoints,
    IterationLimit, NoProgress, OutsideInterval, NonApproachingNormal,
    TangentOrTouching, UncertifiedSeparation, ConflictingNormal,
    WitnessMismatch, WithinTargetSeparation, SeparationCertificate
};
struct RoundedConvexLimits {
    std::size_t maxVertices = 1024;
    double coordinateGameUnits = 10000000;
    double maximumRadiusGameUnits = 1000;
    double distanceToleranceGameUnits = .000001; // explicit numeric budget, 1e-12..0.01
    std::size_t maxCastIterations = 64; // strict advancement budget, 1..64
    // Cast only: isotropic query clearance, separate from immutable source
    // radius and solver tolerance. This is not native CM edge-epsilon policy.
    double targetSeparationGameUnits = 0;
};
struct RoundedConvexResult {
    RoundedConvexStatus status = RoundedConvexStatus::Invalid;
    RoundedConvexReason reason = RoundedConvexReason::InvalidShape;
    double signedDistance = 0; // physical geometry only when hasDistance; otherwise diagnostic-only
    double fraction = 1;
    CollisionVec3 normalBtoA, pointOnA, pointOnB;
    bool hasDistance = false, hasWitnesses = false, hasNormal = false;
    bool hasFraction = false;
    // A candidate passed the original support-witness and extra vector-relation
    // checks. Cast clearance/raw-normal/approaching-motion guards still apply.
    bool normalRefined = false;
    std::size_t gjkIterations = 0, castIterations = 0;
    // Positive only for Separated, from an independently checked support plane.
    // Cast certificates cover the entire interpolated motion, not one pose.
    double separationLowerBoundGameUnits = 0;
};
struct RoundedConvexBoundsResult {
    bool valid = false;
    RoundedConvexReason reason = RoundedConvexReason::InvalidShape;
    CollisionVec3 minimum, maximum;
};

// Basis rows must be finite/proper and orthonormal within 2e-5 (source float32
// representation allowance). Orientation is interpreted through its normalized
// quaternion; raw input is unchanged. This is a declared rigid-pose policy,
// not bit-exact emulation of an original float matrix or a scaling operation.
// Queries do not establish source contents, active entities, ray-plane policy,
// world coverage, a complete manifold or original movement/controller behavior.
// Expose that SAME pose conversion for explicit caller-side pivot/core baking.
// Validates limits and pose; out is unchanged on false. Input/output may alias.
bool CanonicalRoundedConvexPose(const CollisionRigidTransform &in,
    CollisionRigidTransform &out,
    const RoundedConvexLimits &limits = RoundedConvexLimits());
// Distance/Overlap may certify Separated with an independent support plane
// even when approximate closest witnesses fail: then hasDistance/hasWitnesses
// are false and only separationLowerBoundGameUnits is qualified. Hit and
// InitialOverlap retain strict witness requirements. Cast contacts always
// require qualified physical witnesses; this one-pose plane-only fallback is
// limited to Distance/Overlap. Cast-query inflation is qualified separately.
RoundedConvexResult DistanceRoundedConvex(const RoundedConvexShape &a,
    const CollisionRigidTransform &poseA, const RoundedConvexShape &b,
    const CollisionRigidTransform &poseB,
    const RoundedConvexLimits &limits = RoundedConvexLimits());
RoundedConvexResult OverlapRoundedConvex(const RoundedConvexShape &a,
    const CollisionRigidTransform &poseA, const RoundedConvexShape &b,
    const CollisionRigidTransform &poseB,
    const RoundedConvexLimits &limits = RoundedConvexLimits());

// Origins interpolate linearly; orientations follow shortest quaternion slerp.
// An arbitrary pivot trajectory must be expressed explicitly by the caller.
// Hit requires valid physical witnesses and an approaching surface normal.
// InitialOverlap also reports an already violated query clearance, with reason
// WithinTargetSeparation. Tangencies/unstable normals/failures stay Indeterminate.
// A failed Bullet cast NEVER establishes Separated. Independent support-plane
// certificates alone may establish separation over the complete motion.
RoundedConvexResult CastRoundedConvex(const RoundedConvexShape &a,
    const CollisionRigidTransform &fromA, const CollisionRigidTransform &toA,
    const RoundedConvexShape &b, const CollisionRigidTransform &fromB,
    const CollisionRigidTransform &toB,
    const RoundedConvexLimits &limits = RoundedConvexLimits());

// Physical source geometry only, with the SAME canonical pose conversion as
// queries. Every scalar multiply/add is outward-rounded for conservative bounds;
// no guessed epsilon, source radius change or raw source AABB is used.
// Cast clearance is excluded; use the motion helper for the casting actor.
RoundedConvexBoundsResult RoundedConvexBounds(const RoundedConvexShape &shape,
    const CollisionRigidTransform &pose,
    const RoundedConvexLimits &limits = RoundedConvexLimits());
// Conservative enclosure AND the same intermediate-motion admission as Cast's
// actor A. Includes targetSeparation once as query-only isotropic clearance.
// Constant orientation: union of outward-rounded endpoint bounds. Rotation:
// linear origin range plus a support-radius enclosure over every orientation,
// including the normalized-quaternion matrix's floating arithmetic bound.
// A broad phase must call this even when its index has no candidate shapes.
RoundedConvexBoundsResult RoundedConvexMotionBounds(const RoundedConvexShape &shape,
    const CollisionRigidTransform &from, const CollisionRigidTransform &to,
    const RoundedConvexLimits &limits = RoundedConvexLimits());
const char *RoundedConvexStatusName(RoundedConvexStatus status);
const char *RoundedConvexReasonName(RoundedConvexReason reason);
} // namespace wolf2009
#endif

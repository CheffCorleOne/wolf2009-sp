// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SUPPORT_COLLISION_INDEX_H
#define WOLF2009_SUPPORT_COLLISION_INDEX_H
#include "RoundedConvex.h"
#include "SupportCollisionWorld.h"
#include <memory>

namespace wolf2009 {
enum class SupportFilterDecision { Include, Exclude, Unknown };
// Callers explicitly supply a query policy. No raw bit word is silently
// reinterpreted as gameplay contents or a current entity activation state.
class SupportCollisionQueryFilter {
  public:
    virtual ~SupportCollisionQueryFilter() {}
    virtual SupportFilterDecision Select(const SupportCollisionIdentity &,
                                        const SupportCollisionFilter &) const = 0;
};
struct SupportCollisionIndexLimits {
    std::size_t shapes = 300000, corePoints = 8000000, bytes = 256u << 20;
    std::size_t leafShapes = 8, depth = 64;
    RoundedConvexLimits geometry;
};
enum class SupportClearancePolicy { Strict, NonBlockingPhysical };
struct SupportCollisionQueryLimits {
    std::size_t candidates = 100000;
    RoundedConvexLimits geometry;
    // Explicit controller policy: a skin-only uncertain/touching collider can
    // be nonblocking ONLY if a second cast independently certifies positive
    // physical separation for the entire motion. Source radii stay unchanged.
    SupportClearancePolicy clearancePolicy = SupportClearancePolicy::Strict;
};
enum class SupportCollisionQueryStatus {
    Hit, Separated, InitialOverlap, IncompleteSource, Indeterminate, Invalid
};
struct SupportCollisionQueryResult {
    SupportCollisionQueryStatus status = SupportCollisionQueryStatus::Invalid;
    RoundedConvexResult candidate;
    RoundedConvexResult firstUncertain;
    std::size_t firstUncertainShapeIndex = static_cast<std::size_t>(-1);
    SupportCollisionIdentity source;
    SupportCollisionKind sourceKind = SupportCollisionKind::Box;
    // Owned interpretation metadata survives destruction of the source world.
    // A cylinder envelope is not proof of the retail direction selector.
    HavokCylinderGeometryMetadata cylinder;
    std::size_t shapeIndex = static_cast<std::size_t>(-1);
    std::size_t tested = 0, excluded = 0, unknownFilters = 0, uncertainQueries = 0;
    std::size_t physicalSeparationRetries = 0, physicalSeparationFallbacks = 0;
    bool hasCandidate = false;
    bool selectedBodyQueryComplete = false;
    bool responseMetadataQualified = false;
    // This index concerns one selected source body. Other bodies, current
    // entity bindings and original point-ray semantics are separate systems.
    bool sceneClearanceVerified = false;
};
struct SupportCollisionCastCollection {
    // The same completeness/uncertainty contract as Cast. No returned hits
    // are exposed unless the entire selected-body query is complete.
    SupportCollisionQueryResult summary;
    // Per-hit collision metadata; aggregate query diagnostics live in summary.
    // Stable source shape order, not TOI order. Bounded storage does not stop
    // traversal: an uncertain later collider still invalidates the query.
    std::vector<SupportCollisionQueryResult> hits;
    std::size_t totalHits = 0;
    bool truncated = false;
    // Includes hits beyond the returned prefix, so truncation cannot hide an
    // unqualified contact-response profile from a native adapter.
    bool allHitResponseMetadataQualified = false;
};

// Immutable owning BVH. Geometry is copied once, with a checked memory budget.
// Bounds come from the SAME canonical rigid-pose conversion as the solver;
// the data reader's raw-placement bounds are never used for culling.
class SupportCollisionIndex {
  public:
    explicit SupportCollisionIndex(const SupportCollisionWorld &,
                                  const SupportCollisionIndexLimits & = SupportCollisionIndexLimits());
    ~SupportCollisionIndex();
    SupportCollisionIndex(SupportCollisionIndex &&) noexcept;
    SupportCollisionIndex &operator=(SupportCollisionIndex &&) noexcept;
    SupportCollisionIndex(const SupportCollisionIndex &) = delete;
    SupportCollisionIndex &operator=(const SupportCollisionIndex &) = delete;
    std::size_t ShapeCount() const;
    SupportCollisionQueryResult Cast(const RoundedConvexShape &actor,
        const CollisionRigidTransform &from, const CollisionRigidTransform &to,
        const SupportCollisionQueryFilter &,
        const SupportCollisionQueryLimits & = SupportCollisionQueryLimits()) const;
    SupportCollisionQueryResult Overlap(const RoundedConvexShape &actor,
        const CollisionRigidTransform &pose, const SupportCollisionQueryFilter &,
        const SupportCollisionQueryLimits & = SupportCollisionQueryLimits()) const;
    // One traversal/one narrowphase per candidate. maximumResults is in
    // [0,4096]; even zero validates the full query. The closest hit remains in
    // summary independently of the source-ordered returned prefix.
    SupportCollisionCastCollection CastAll(const RoundedConvexShape &actor,
        const CollisionRigidTransform &from, const CollisionRigidTransform &to,
        const SupportCollisionQueryFilter &, std::size_t maximumResults,
        const SupportCollisionQueryLimits & = SupportCollisionQueryLimits()) const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wolf2009
#endif

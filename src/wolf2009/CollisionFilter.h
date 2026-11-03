// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_COLLISION_FILTER_H
#define WOLF2009_COLLISION_FILTER_H
#include <cstdint>

namespace wolf2009 {
struct CollisionFilterWord {
    std::uint32_t value = 0;
    bool known = false;
    CollisionFilterWord() = default;
    explicit CollisionFilterWord(std::uint32_t raw,bool resolved=true):value(raw),known(resolved) {}
};
enum class CollisionFilterDecision { Enabled, Disabled, Unknown };
enum class CollisionFilterWordSource { Child, RootFallback, Unknown };
struct CollisionFilterResolvedWord {
    CollisionFilterWord word;
    CollisionFilterWordSource source = CollisionFilterWordSource::Unknown;
};
// Context is supplied by the caller; HKXS alone does not establish live
// userData, ignore IDs, object flags or the query's selected runtime bodies.
enum class CollisionFilterRuntimeGate { Passed, Rejected, Unknown };
enum class CollisionFilterContactPriority { FirstMask, SecondMask, Unknown };

// Measured Wolf2009 BitsCollisionFilter word rules, NOT hkpGroupFilter layers.
// Contact child0 inherits the topmost CdBody's raw filter; nonzero overrides.
CollisionFilterResolvedWord ResolveBitsContactChildFilter(
    CollisionFilterWord child,CollisionFilterWord root);

// Ray COLLECTION callback only: child0 OR query0 is a wildcard; otherwise an
// intersecting bit is required. A zero child does NOT inherit the root here.
// This does not certify that collection filtering was enabled by the caller,
// nor that the separate root/runtime checks passed.
CollisionFilterDecision EvaluateBitsRayChildFilter(
    CollisionFilterWord query,CollisionFilterWord child);

// Ray ROOT callback: query0 is a wildcard, but root0 is not. Passed means the
// caller separately established present/eligible runtime userData and passed
// all original ignore/object-state checks. Default Unknown cannot activate it.
CollisionFilterDecision EvaluateBitsRayRootFilter(
    CollisionFilterWord query,CollisionFilterWord root,
    CollisionFilterRuntimeGate runtime = CollisionFilterRuntimeGate::Unknown);

// The selected first nonzero runtime mask tests the OTHER body's effective
// filter info; if both masks are zero the word stage allows the pair. These
// masks come from runtime userData, not another spelling of filterInfo.
// Original callers can reorder priority (including the measured id0x1ffe
// path). Without that context a disagreement stays Unknown. Runtime vetoes
// remain a separate mandatory prerequisite, unresolved by source geometry.
CollisionFilterDecision EvaluateBitsContactPairFilter(
    CollisionFilterWord firstFilter,CollisionFilterWord secondFilter,
    CollisionFilterWord firstRuntimeMask,CollisionFilterWord secondRuntimeMask,
    CollisionFilterContactPriority priority = CollisionFilterContactPriority::Unknown,
    CollisionFilterRuntimeGate runtime = CollisionFilterRuntimeGate::Unknown);

// Conjunction for separately required stages: a proved rejection dominates,
// while an unresolved prerequisite can never become an enabled world query.
CollisionFilterDecision CombineCollisionFilterDecisions(
    CollisionFilterDecision first,CollisionFilterDecision second);
} // namespace wolf2009
#endif

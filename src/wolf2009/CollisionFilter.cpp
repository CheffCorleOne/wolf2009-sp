// SPDX-License-Identifier: GPL-3.0-or-later
#include "CollisionFilter.h"

namespace wolf2009 {
namespace {
CollisionFilterDecision Match(CollisionFilterWord a,CollisionFilterWord b) {
    if (!a.known || !b.known) return CollisionFilterDecision::Unknown;
    return (a.value & b.value) ? CollisionFilterDecision::Enabled : CollisionFilterDecision::Disabled;
}
CollisionFilterDecision Runtime(CollisionFilterDecision words,CollisionFilterRuntimeGate gate) {
    const auto decision=gate==CollisionFilterRuntimeGate::Passed ? CollisionFilterDecision::Enabled :
        gate==CollisionFilterRuntimeGate::Rejected ? CollisionFilterDecision::Disabled : CollisionFilterDecision::Unknown;
    return CombineCollisionFilterDecisions(words,decision);
}
CollisionFilterDecision ContactWords(CollisionFilterWord firstInfo,CollisionFilterWord secondInfo,
                                    CollisionFilterWord firstMask,CollisionFilterWord secondMask) {
    if (!firstMask.known) return CollisionFilterDecision::Unknown;
    if (firstMask.value) return Match(secondInfo,firstMask);
    if (!secondMask.known) return CollisionFilterDecision::Unknown;
    if (secondMask.value) return Match(firstInfo,secondMask);
    return CollisionFilterDecision::Enabled;
}
} // namespace
CollisionFilterResolvedWord ResolveBitsContactChildFilter(CollisionFilterWord child,CollisionFilterWord root) {
    CollisionFilterResolvedWord out;
    out.word=child; // preserve an unresolved retained word without interpreting it
    if (!child.known) return out;
    out.source=child.value ? CollisionFilterWordSource::Child : CollisionFilterWordSource::RootFallback;
    out.word=child.value ? child : root;
    return out;
}
CollisionFilterDecision EvaluateBitsRayChildFilter(CollisionFilterWord query,CollisionFilterWord child) {
    // Native ray-collection callback: zero child/query accepts before AND.
    if ((query.known && !query.value) || (child.known && !child.value)) return CollisionFilterDecision::Enabled;
    return Match(query,child);
}
CollisionFilterDecision EvaluateBitsRayRootFilter(CollisionFilterWord query,CollisionFilterWord root,
                                                 CollisionFilterRuntimeGate runtime) {
    const auto words=query.known && !query.value ? CollisionFilterDecision::Enabled : Match(query,root);
    return Runtime(words,runtime);
}
CollisionFilterDecision EvaluateBitsContactPairFilter(CollisionFilterWord firstFilter,CollisionFilterWord secondFilter,
    CollisionFilterWord firstRuntimeMask,CollisionFilterWord secondRuntimeMask,
    CollisionFilterContactPriority priority,CollisionFilterRuntimeGate runtime) {
    CollisionFilterDecision words=CollisionFilterDecision::Unknown;
    if (priority==CollisionFilterContactPriority::FirstMask)
        words=ContactWords(firstFilter,secondFilter,firstRuntimeMask,secondRuntimeMask);
    else if (priority==CollisionFilterContactPriority::SecondMask)
        words=ContactWords(secondFilter,firstFilter,secondRuntimeMask,firstRuntimeMask);
    else {
        const auto a=ContactWords(firstFilter,secondFilter,firstRuntimeMask,secondRuntimeMask);
        const auto b=ContactWords(secondFilter,firstFilter,secondRuntimeMask,firstRuntimeMask);
        if (a==b) words=a;
    }
    return Runtime(words,runtime);
}
CollisionFilterDecision CombineCollisionFilterDecisions(CollisionFilterDecision first,CollisionFilterDecision second) {
    if (first==CollisionFilterDecision::Disabled || second==CollisionFilterDecision::Disabled)
        return CollisionFilterDecision::Disabled;
    if (first==CollisionFilterDecision::Enabled && second==CollisionFilterDecision::Enabled)
        return CollisionFilterDecision::Enabled;
    return CollisionFilterDecision::Unknown;
}
} // namespace wolf2009

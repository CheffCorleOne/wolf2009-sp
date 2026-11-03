// SPDX-License-Identifier: GPL-3.0-or-later
#include "CollisionFilter.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace wolf2009;
using Decision=CollisionFilterDecision;
using Gate=CollisionFilterRuntimeGate;
using Priority=CollisionFilterContactPriority;
int checks=0;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); ++checks; }
CollisionFilterWord K(std::uint32_t n) { return CollisionFilterWord(n); }
void PartialKnowledgeProperties() {
    // Deterministic numeric completion domain includes zero, disjoint bits,
    // overlapping bits, the sign bit and all bits. Removing knowledge may
    // retain a decision or return Unknown, never contradict a completion.
    const std::uint32_t values[]={0,1,2,3,0x80000000u,0x80000003u,0xffffffffu};
    const Priority priorities[]={Priority::FirstMask,Priority::SecondMask,Priority::Unknown};
    const Gate gates[]={Gate::Passed,Gate::Rejected,Gate::Unknown};
    for (auto a:values) for (auto b:values) for (auto maskA:values) for (auto maskB:values) {
        for (unsigned unknowns=0;unknowns<16;++unknowns) {
            const CollisionFilterWord aw(a,!(unknowns&1)),bw(b,!(unknowns&2));
            const CollisionFilterWord am(maskA,!(unknowns&4)),bm(maskB,!(unknowns&8));
            for (auto priority:priorities) for (auto gate:gates) {
                const auto complete=EvaluateBitsContactPairFilter(K(a),K(b),K(maskA),K(maskB),priority,gate);
                const auto partial=EvaluateBitsContactPairFilter(aw,bw,am,bm,priority,gate);
                Check(partial==Decision::Unknown || partial==complete,"Partial contact knowledge contradicts a numeric completion");
                Check(gate!=Gate::Unknown || partial!=Decision::Enabled,"Unknown runtime gate activated contact");
            }
            Check(EvaluateBitsContactPairFilter(aw,bw,am,bm,Priority::FirstMask,Gate::Passed)==
                  EvaluateBitsContactPairFilter(bw,aw,bm,am,Priority::SecondMask,Gate::Passed),
                  "Swapping bodies and mask priority changed the pair word predicate");
        }
    }
    for (auto query:values) for (auto target:values) for (unsigned unknowns=0;unknowns<4;++unknowns) {
        const CollisionFilterWord q(query,!(unknowns&1)),t(target,!(unknowns&2));
        const auto partial=EvaluateBitsRayChildFilter(q,t);
        Check(partial==Decision::Unknown || partial==EvaluateBitsRayChildFilter(K(query),K(target)),
              "Partial ray-child knowledge contradicts a numeric completion");
        for (auto gate:gates) {
            const auto root=EvaluateBitsRayRootFilter(q,t,gate);
            Check(root==Decision::Unknown || root==EvaluateBitsRayRootFilter(K(query),K(target),gate),
                  "Partial ray-root knowledge contradicts a numeric completion");
            Check(gate!=Gate::Unknown || root!=Decision::Enabled,"Unknown runtime gate activated ray root");
        }
    }
}
void Synthetic() {
    const CollisionFilterWord unknown;
    struct RayCase { std::uint32_t query,filter; Decision child,root; };
    const RayCase rays[]={
        {0,0,Decision::Enabled,Decision::Enabled},
        {0,0x80000000u,Decision::Enabled,Decision::Enabled},
        {1,0,Decision::Enabled,Decision::Disabled},
        {1,1,Decision::Enabled,Decision::Enabled},
        {1,2,Decision::Disabled,Decision::Disabled},
        {3,2,Decision::Enabled,Decision::Enabled},
        {0x80000000u,0x80000000u,Decision::Enabled,Decision::Enabled},
        {0x80000000u,0x7fffffffu,Decision::Disabled,Decision::Disabled},
        {0xffffffffu,0,Decision::Enabled,Decision::Disabled}
    };
    for (const auto& row:rays) {
        Check(EvaluateBitsRayChildFilter(K(row.query),K(row.filter))==row.child,"Ray child truth table changed");
        Check(EvaluateBitsRayRootFilter(K(row.query),K(row.filter),Gate::Passed)==row.root,"Ray root truth table changed");
    }
    Check(EvaluateBitsRayChildFilter(unknown,K(0))==Decision::Enabled &&
          EvaluateBitsRayChildFilter(K(0),unknown)==Decision::Enabled,"Known ray wildcard depends on unknown unused word");
    Check(EvaluateBitsRayChildFilter(unknown,K(2))==Decision::Unknown &&
          EvaluateBitsRayChildFilter(K(2),unknown)==Decision::Unknown,"Unknown ray word became filter0");
    Check(EvaluateBitsRayRootFilter(K(0),unknown,Gate::Passed)==Decision::Enabled,
          "Root query0 wildcard requires an unused root word");
    Check(EvaluateBitsRayRootFilter(K(1),unknown,Gate::Passed)==Decision::Unknown &&
          EvaluateBitsRayRootFilter(unknown,K(0),Gate::Passed)==Decision::Unknown,"Unknown root/query guessed");
    Check(EvaluateBitsRayRootFilter(K(1),K(1))==Decision::Unknown &&
          EvaluateBitsRayRootFilter(K(0),K(0))==Decision::Unknown,"Source words alone passed runtime userData gate");
    Check(EvaluateBitsRayRootFilter(K(1),K(2))==Decision::Disabled &&
          EvaluateBitsRayRootFilter(K(1),unknown,Gate::Rejected)==Decision::Disabled,
          "Proved word/runtime rejection lost behind an unresolved stage");
    const auto override=ResolveBitsContactChildFilter(K(8),K(3));
    Check(override.word.known && override.word.value==8 && override.source==CollisionFilterWordSource::Child,
          "Nonzero contact child merged with root");
    const auto inherited=ResolveBitsContactChildFilter(K(0),K(3));
    Check(inherited.word.known && inherited.word.value==3 && inherited.source==CollisionFilterWordSource::RootFallback,
          "Contact child0 did not inherit raw root");
    const auto rootZero=ResolveBitsContactChildFilter(K(0),K(0));
    Check(rootZero.word.known && rootZero.word.value==0 && rootZero.source==CollisionFilterWordSource::RootFallback,
          "Resolved zero root lost its known flag");
    Check(!ResolveBitsContactChildFilter(K(0),unknown).word.known &&
          ResolveBitsContactChildFilter(K(0),unknown).source==CollisionFilterWordSource::RootFallback &&
          !ResolveBitsContactChildFilter(unknown,K(3)).word.known,
          "Unknown child/root became a fallback value");
    Check(ResolveBitsContactChildFilter(K(8),unknown).word.value==8,
          "Known overriding child requires unused root data");
    struct ContactCase { std::uint32_t a,b,maskA,maskB; Decision first,second; };
    const ContactCase contacts[]={
        {1,2,0,0,Decision::Enabled,Decision::Enabled},
        {0,0,0,0,Decision::Enabled,Decision::Enabled},
        {1,2,2,0,Decision::Enabled,Decision::Enabled},
        {1,2,1,0,Decision::Disabled,Decision::Disabled},
        {1,2,0,1,Decision::Enabled,Decision::Enabled},
        {1,2,0,2,Decision::Disabled,Decision::Disabled},
        {1,2,2,2,Decision::Enabled,Decision::Disabled},
        {1,2,1,1,Decision::Disabled,Decision::Enabled},
        {0,0,1,1,Decision::Disabled,Decision::Disabled},
        {0,0x80000000u,0x80000000u,0,Decision::Enabled,Decision::Enabled}
    };
    for (const auto& row:contacts) {
        Check(EvaluateBitsContactPairFilter(K(row.a),K(row.b),K(row.maskA),K(row.maskB),Priority::FirstMask,Gate::Passed)==row.first,
              "First runtime-mask contact priority changed");
        Check(EvaluateBitsContactPairFilter(K(row.a),K(row.b),K(row.maskA),K(row.maskB),Priority::SecondMask,Gate::Passed)==row.second,
              "Second runtime-mask contact priority changed");
        const auto expected=row.first==row.second ? row.first : Decision::Unknown;
        Check(EvaluateBitsContactPairFilter(K(row.a),K(row.b),K(row.maskA),K(row.maskB),Priority::Unknown,Gate::Passed)==expected,
              "Unresolved contact ordering selected a convenient branch");
    }
    Check(EvaluateBitsContactPairFilter(unknown,unknown,K(0),K(0),Priority::Unknown,Gate::Passed)==Decision::Enabled,
          "Both zero masks require unused filter words");
    Check(EvaluateBitsContactPairFilter(unknown,K(2),K(2),unknown,Priority::FirstMask,Gate::Passed)==Decision::Enabled,
          "Unused lower-priority mask/first info changed known first-mask result");
    Check(EvaluateBitsContactPairFilter(K(1),K(2),unknown,K(1),Priority::FirstMask,Gate::Passed)==Decision::Unknown,
          "Unknown first mask was silently replaced by zero");
    Check(EvaluateBitsContactPairFilter(K(1),K(2),K(0),unknown,Priority::FirstMask,Gate::Passed)==Decision::Unknown,
          "Unknown second mask was silently replaced by zero");
    Check(EvaluateBitsContactPairFilter(unknown,K(2),K(0),K(1),Priority::FirstMask,Gate::Passed)==Decision::Unknown,
          "Selected target filter word was guessed");
    Check(EvaluateBitsContactPairFilter(K(1),K(2),K(0),K(0))==Decision::Unknown,
          "Source contact words bypassed unproven runtime state");
    Check(EvaluateBitsContactPairFilter(unknown,unknown,unknown,unknown,Priority::Unknown,Gate::Rejected)==Decision::Disabled,
          "Runtime contact veto did not reject unresolved words");
    Check(CombineCollisionFilterDecisions(Decision::Enabled,Decision::Unknown)==Decision::Unknown &&
          CombineCollisionFilterDecisions(Decision::Disabled,Decision::Unknown)==Decision::Disabled &&
          CombineCollisionFilterDecisions(Decision::Enabled,Decision::Enabled)==Decision::Enabled,
          "Required-stage conjunction promoted an unknown world filter");
    // A matching leaf cannot bypass a rejected root. Conversely a root match
    // does not mean a collection child with disjoint nonzero bits is enabled.
    Check(CombineCollisionFilterDecisions(EvaluateBitsRayRootFilter(K(2),K(1),Gate::Passed),
          EvaluateBitsRayChildFilter(K(2),K(2)))==Decision::Disabled,"Matching ray child bypassed root rejection");
    Check(CombineCollisionFilterDecisions(EvaluateBitsRayRootFilter(K(1),K(1),Gate::Passed),
          EvaluateBitsRayChildFilter(K(1),K(2)))==Decision::Disabled,"Root match bypassed ray child rejection");
    CollisionFilterWord retained(0xffffffffu,false);
    Check(EvaluateBitsRayChildFilter(K(1),retained)==Decision::Unknown && retained.value==0xffffffffu && !retained.known,
          "Unresolved retained raw word normalized or input mutated");
    const auto unresolved=ResolveBitsContactChildFilter(retained,K(3));
    Check(!unresolved.word.known && unresolved.word.value==0xffffffffu && unresolved.source==CollisionFilterWordSource::Unknown,
          "Unresolved retained child word erased or interpreted as root fallback");
    PartialKnowledgeProperties();
    std::cout << "WOLF2009_COLLISION_FILTER_SYNTHETIC_OK checks=" << checks << '\n';
}
} // namespace
int main() {
    try { Synthetic(); return 0; }
    catch (const std::exception& e) { std::cerr << "Collision filter test failed: " << e.what() << '\n'; return 1; }
}

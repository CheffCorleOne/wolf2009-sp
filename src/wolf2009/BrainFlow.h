// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_BRAIN_FLOW_H
#define WOLF2009_BRAIN_FLOW_H
#include "BrainGraph.h"
#include <cstdint>

namespace wolf2009 {
enum class BrainConditionValue { False, True, MissingPrimary, Unsupported };

// A -1 record references the owning task's primary condition; it is not true.
// MissingPrimary is valid only for tasks which have no primary condition.
class BrainFlowConditions {
  public:
    virtual ~BrainFlowConditions() {}
    virtual BrainConditionValue Primary(const BrainGraph &, std::size_t node) = 0;
    virtual BrainConditionValue Explicit(const BrainGraph &, std::size_t record) = 0;
};
class BrainFlowRandom {
  public:
    virtual ~BrainFlowRandom() {}
    virtual std::uint32_t Next15() = 0; // Inclusive range 0..32767.
};
struct BrainFlowOutputState {
    std::uint32_t flags = 0;
    std::vector<std::uint32_t> weights;
    bool flagsKnown = false, supported = false;
    std::string unsupportedReason;
};
struct BrainFlowState {
    const BrainGraph *graph = NULL; // The document must remain alive and immutable.
    std::vector<BrainFlowOutputState> outputs;
};
struct BrainFlowDecision {
    enum Status { NoTransition, Transition, Unsupported } status = NoTransition;
    std::size_t output = BrainRecord::None(), destination = BrainRecord::None(),
                targetNode = BrainRecord::None();
    std::string reason;
    // destination indexes output.destinations. Its .slot indexes the original
    // triple in records[output.record].tokens; auxiliary is tokens[4 + slot*3].
};

// Initial selector state from the observed fixed 32-slot layout. Raw auxiliary
// destination fields remain opaque; this does not initialize a Task/Network.
BrainFlowState MakeBrainFlowState(const BrainGraph &graph);

// One normal output pass: AND conditions, first passing output in source order,
// one weighted destination. At most one transition is selected per call.
// No task activation, event delivery, condition lifecycle or entity side effects.
// The caller owns activation/reset and supplies an established condition context.
// Alternate passes/activation modes and weight decay other than 1 fail closed.
// The normal selector does not test flag 0x10 or the 0x40000 probe-hit marker;
// both are preserved. This is not a claim about other Task/Network phases.
BrainFlowDecision SelectBrainTransition(const BrainGraph &graph, std::size_t node,
                                       BrainFlowState &state, BrainFlowConditions &conditions,
                                       BrainFlowRandom &random);

// Probe pass (output flag 0x20) over one task's outputs in source order. Every
// eligible probe output whose AND conditions pass receives the 0x40000 marker;
// the pass does not stop at the first one and selects no destination.
// Eligibility: not disabled (0x100000), at least one destination, and not
// processed (0x80000) while once (1) or probe (0x20). A processed probe output
// therefore stays ineligible until its source re-enters and clears it.
// Probe flags other than 0x20, 1, 0x80000 and 0x40000 refuse. Conditions are
// evaluated here, before any marked output fires. Returns NoTransition on success.
BrainFlowDecision MarkBrainProbeOutputs(const BrainGraph &graph, std::size_t node,
                                        BrainFlowState &state, BrainFlowConditions &conditions,
                                        std::size_t &marked);
// Flush one marked probe output: clear 0x40000, run the common weighted
// destination selection and set 0x80000. Returns Transition with the chosen
// target; the caller owns probe activation. A refusal leaves the state unchanged.
BrainFlowDecision FireBrainMarkedProbeOutput(const BrainGraph &graph, std::size_t output,
                                             BrainFlowState &state, BrainFlowRandom &random);

// Timer predicate only, in the caller's integer clock units. Arming a timer and
// resolving duration expressions are separate operations. Overflow is unsupported.
BrainConditionValue BrainTimerElapsed(std::int32_t now, std::int32_t activeTaskStart,
                                     std::int32_t duration, bool inverted = false);
int RunBrainFlowTests();
} // namespace wolf2009
#endif

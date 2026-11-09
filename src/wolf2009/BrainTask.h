// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_BRAIN_TASK_H
#define WOLF2009_BRAIN_TASK_H
#include "BrainFlow.h"

namespace wolf2009 {
enum class BrainTaskPhase { Initialize, Bind, Enter, Exit };
enum class BrainTaskHook { Applied, Unsupported };
// Named, explicit runtime profiles. NormalV1 is the default and refuses probe
// outputs. ProbeLeafPassV1 adds the statically established probe pass/flush
// (output flag 0x20) for leaf destinations only; see docs/BRAINS.md.
enum class BrainTaskProfile { NormalV1, ProbeLeafPassV1 };
struct BrainTaskTraits {
    enum Kind { Unsupported, Normal, Root } kind = Unsupported;
    // Explicit host admission: known normal, non-yielding activation only.
    bool normalActivationKnown = false, hasPrimary = false, forceOnceOnInitialize = false;
    // ProbeLeafPassV1 only: the host declares that this task may be entered by a
    // probe (Enter without a later Exit from the network, task flag 0x20).
    bool probeLeafEntryKnown = false;
    // Root callbacks and dispatch use ascending priority. Equal priorities
    // retain graph order, including when the host leaves every root at zero.
    std::uint32_t rootPriority = 0;
};
struct BrainTaskCondition {
    std::size_t node = BrainRecord::None(), record = BrainRecord::None();
    bool IsPrimary() const { return record == BrainRecord::None(); }
};
struct BrainTaskUse {
    bool pending = false;
    std::string key;
    bool Notify(const std::string &value); // One latch, latest notification wins.
    bool Matches(const std::string &expected) const;
    bool Consume(const std::string &expected);
    void Clear();
};
struct BrainTaskTimer {
    std::int32_t actualDuration = 0, timeSet = 0;
    void Initialize() { timeSet = 0; }
    // Entry in the same clock tick reuses the existing sample. -1 is infinite;
    // maximum=0 selects the fixed minimum. Unproven overflow profiles refuse.
    BrainTaskHook Enter(std::int32_t now, std::int32_t minimum, std::int32_t maximum,
                        BrainFlowRandom &random);
    BrainConditionValue Evaluate(std::int32_t now, std::int32_t activeTaskStart,
                                 bool inverted = false) const;
};
struct BrainTaskNodeState { std::uint32_t flags = 0; };
struct BrainTaskState {
    std::vector<BrainTaskNodeState> nodes;
    BrainFlowState flow;
    BrainTaskUse use;
    std::size_t activeNode = BrainRecord::None();
    std::int32_t clock = 0, activeTaskStart = 0;
    bool initialized = false, failed = false;
};
struct BrainTaskContext {
    const void *instance = NULL; // Stable per-runtime key for host-owned condition/action state.
    std::uint64_t owner = 0;
    std::int32_t now = 0, activeTaskStart = 0;
    std::size_t activeNode = BrainRecord::None();
    std::uint32_t taskFlags = 0;
    BrainTaskUse *use = NULL;
    BrainFlowRandom *random = NULL;
};
class BrainTaskHost {
  public:
    virtual ~BrainTaskHost() {}
    virtual BrainTaskTraits Describe(const BrainGraph &, std::size_t node) = 0;
    virtual bool SupportsCondition(const BrainGraph &, const BrainTaskCondition &) = 0;
    virtual BrainTaskHook Task(const BrainGraph &, std::size_t node, BrainTaskPhase,
                               const BrainTaskContext &) = 0;
    virtual BrainTaskHook Condition(const BrainGraph &, const BrainTaskCondition &, BrainTaskPhase,
                                    const BrainTaskContext &) = 0;
    virtual BrainConditionValue Evaluate(const BrainGraph &, const BrainTaskCondition &,
                                        const BrainTaskContext &) = 0;
    virtual BrainConditionValue RootReady(const BrainGraph &, std::size_t node,
                                         const BrainTaskContext &) = 0;
};
struct BrainTaskResult {
    enum Status { Idle, Advanced, Unsupported, BudgetExceeded } status = Idle;
    std::size_t transitions = 0, probes = 0; // probes stays 0 under NormalV1.
    std::string reason;
};

// Common normal-mode lifecycle, with explicitly admitted host implementations.
// Does not implement stack/yield modes, save-state restore or campaign actions.
// Probe outputs run only under ProbeLeafPassV1: after each normal pass, the
// active task's eligible probe outputs are marked, then the active task and the
// roots are flushed. A probe entry keeps the active task and its start time and
// exits nothing; the entered task sees task flag 0x20 in its context. Probe
// destinations with outputs, destinations also reached by normal outputs,
// probe outputs on roots and hosts without probeLeafEntryKnown are refused.
// Hooks run synchronously. NotifyUse is allowed inside hooks; recursive Tick/Reset is not.
// Callback exceptions and recursive entry poison the instance. Further hooks stop;
// external side effects are not rolled back. The busy guard is released on every exit.
class BrainTaskRuntime {
  public:
    BrainTaskRuntime(const BrainGraph &, BrainTaskHost &, BrainFlowRandom &, std::uint64_t owner = 0,
                     BrainTaskProfile profile = BrainTaskProfile::NormalV1);
    BrainTaskRuntime(const BrainTaskRuntime &) = delete;
    BrainTaskRuntime &operator=(const BrainTaskRuntime &) = delete;
    BrainTaskResult Tick(std::int32_t now, std::size_t transitionBudget = 50);
    // Reset an initialized instance: clear network input/active state, bind, re-enter roots.
    // It preserves host condition state and does not repeat Initialize callbacks.
    BrainTaskResult Reset(std::int32_t now);
    bool NotifyUse(const std::string &key) { return state_.use.Notify(key); }
    const BrainTaskState &State() const { return state_; }

  private:
    class Conditions;
    const BrainGraph &graph_;
    BrainTaskHost &host_;
    BrainFlowRandom &random_;
    std::uint64_t owner_;
    BrainTaskProfile profile_;
    BrainTaskState state_;
    std::vector<BrainTaskTraits> traits_;
    std::vector<std::vector<BrainTaskCondition>> conditions_;
    std::vector<std::size_t> nodeOrder_, roots_;
    bool prepared_ = false, busy_ = false, haveInputClock_ = false;
    std::int32_t inputClock_ = 0;
    std::string failure_;
    BrainTaskContext Context(std::size_t node, std::int32_t now);
    bool Prepare();
    bool Initialize(std::int32_t now);
    bool Hooks(std::size_t node, BrainTaskPhase phase, std::int32_t now);
    enum class Entry { Root, Normal, Probe };
    bool Enter(std::size_t node, std::int32_t now, Entry mode);
    bool Activate(std::size_t target, std::int32_t now);
    bool ProbePass(Conditions &resolver);
    bool Flush(std::size_t node, std::int32_t now, BrainTaskResult &result);
    bool Fail(const char *reason);
    BrainTaskResult Failed(BrainTaskResult::Status status = BrainTaskResult::Unsupported);
};
// Reads only the native per-type ^ID flag pair. Other properties remain host-owned.
bool ReadBrainTaskFlags(const BrainGraph &, std::size_t node, std::uint32_t &flags);
int RunBrainTaskTests();
} // namespace wolf2009
#endif

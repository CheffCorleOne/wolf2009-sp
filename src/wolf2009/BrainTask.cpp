// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainTask.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace wolf2009 {
namespace {
struct BusyScope {
    bool &value;
    explicit BusyScope(bool &flag) : value(flag) { value = true; }
    ~BusyScope() { value = false; }
};
unsigned char Fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool EqualKey(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (Fold(static_cast<unsigned char>(a[i])) != Fold(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
bool Unsigned(const std::string &text, std::uint32_t &value) {
    if (text.empty()) return false;
    value = 0;
    for (unsigned char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<unsigned>(c - '0');
        if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}
} // namespace
bool BrainTaskUse::Notify(const std::string &value) {
    if (value.size() > 1023 || value.find('\0') != std::string::npos) return false;
    pending = true;
    key = value;
    return true;
}
bool BrainTaskUse::Matches(const std::string &expected) const { return pending && EqualKey(key, expected); }
bool BrainTaskUse::Consume(const std::string &expected) {
    if (!Matches(expected)) return false;
    Clear();
    return true;
}
void BrainTaskUse::Clear() { pending = false; key.clear(); }

BrainTaskHook BrainTaskTimer::Enter(std::int32_t now, std::int32_t minimum, std::int32_t maximum,
                                   BrainFlowRandom &random) {
    if (now < 0 || minimum < -1 || maximum < 0) return BrainTaskHook::Unsupported;
    if (now <= timeSet) return BrainTaskHook::Applied;
    std::int32_t value = minimum;
    if (minimum != -1 && maximum != 0) {
        const auto range = static_cast<std::int64_t>(maximum) - minimum;
        if (range < 0 || range > 65535) return BrainTaskHook::Unsupported;
        const auto sample = random.Next15();
        if (sample > 32767) return BrainTaskHook::Unsupported;
        const auto selected = minimum + ((sample * range) >> 15);
        if (selected > std::numeric_limits<std::int32_t>::max()) return BrainTaskHook::Unsupported;
        value = static_cast<std::int32_t>(selected);
    }
    actualDuration = value;
    timeSet = now;
    return BrainTaskHook::Applied;
}
BrainConditionValue BrainTaskTimer::Evaluate(std::int32_t now, std::int32_t activeTaskStart,
                                            bool inverted) const {
    return BrainTimerElapsed(now, activeTaskStart, actualDuration, inverted);
}

bool ReadBrainTaskFlags(const BrainGraph &graph, std::size_t node, std::uint32_t &flags) {
    if (node >= graph.nodes.size()) return false;
    const auto &tokens = graph.records[graph.nodes[node].record].tokens;
    if (tokens.size() < 5 || (tokens.size() - 5) % 2 != 0) return false;
    const auto key = '^' + std::to_string(graph.nodes[node].type);
    flags = 0;
    bool found = false;
    for (std::size_t i = 5; i + 1 < tokens.size(); i += 2) {
        if (tokens[i] == key) {
            if (found || !Unsigned(tokens[i + 1], flags)) return false;
            found = true;
        }
    }
    return true;
}

class BrainTaskRuntime::Conditions : public BrainFlowConditions {
  public:
    Conditions(BrainTaskRuntime &runtime, std::int32_t now) : runtime_(runtime), now_(now) {}
    BrainConditionValue Primary(const BrainGraph &, std::size_t node) override {
        if (runtime_.state_.failed) return BrainConditionValue::Unsupported;
        if (!runtime_.traits_[node].hasPrimary) return BrainConditionValue::MissingPrimary;
        BrainTaskCondition ref;
        ref.node = node;
        const auto value = runtime_.host_.Evaluate(runtime_.graph_, ref, runtime_.Context(node, now_));
        return runtime_.state_.failed ? BrainConditionValue::Unsupported : value;
    }
    BrainConditionValue Explicit(const BrainGraph &, std::size_t record) override {
        if (runtime_.state_.failed) return BrainConditionValue::Unsupported;
        BrainTaskCondition ref;
        ref.node = runtime_.graph_.records[record].node;
        ref.record = record;
        const auto value = runtime_.host_.Evaluate(runtime_.graph_, ref, runtime_.Context(ref.node, now_));
        return runtime_.state_.failed ? BrainConditionValue::Unsupported : value;
    }
  private:
    BrainTaskRuntime &runtime_;
    std::int32_t now_;
};
BrainTaskRuntime::BrainTaskRuntime(const BrainGraph &graph, BrainTaskHost &host,
                                 BrainFlowRandom &random, std::uint64_t owner)
    : graph_(graph), host_(host), random_(random), owner_(owner) {}
BrainTaskContext BrainTaskRuntime::Context(std::size_t node, std::int32_t now) {
    BrainTaskContext result;
    result.instance = this;
    result.owner = owner_;
    result.now = now;
    result.activeTaskStart = state_.activeTaskStart;
    result.activeNode = state_.activeNode;
    result.taskFlags = state_.nodes[node].flags;
    result.use = &state_.use;
    result.random = &random_;
    return result;
}
bool BrainTaskRuntime::Fail(const char *reason) {
    state_.failed = true;
    failure_ = reason;
    return false;
}
BrainTaskResult BrainTaskRuntime::Failed(BrainTaskResult::Status status) {
    BrainTaskResult result;
    result.status = status;
    result.reason = failure_;
    return result;
}
bool BrainTaskRuntime::Prepare() {
    if (prepared_) return true;
    state_.flow = MakeBrainFlowState(graph_);
    state_.nodes.resize(graph_.nodes.size());
    conditions_.resize(graph_.outputs.size());
    for (std::size_t n = 0; n < graph_.nodes.size(); ++n) {
        const auto traits = host_.Describe(graph_, n);
        if (state_.failed) return false;
        if ((traits.kind != BrainTaskTraits::Normal && traits.kind != BrainTaskTraits::Root) ||
            !traits.normalActivationKnown)
            return Fail("Unsupported Task implementation or activation traits");
        std::uint32_t flags = 0;
        if (!ReadBrainTaskFlags(graph_, n, flags) || (flags & ~(1u | 0x10u)))
            return Fail("Unsupported serialized Task flags");
        state_.nodes[n].flags = flags;
        traits_.push_back(traits);
        nodeOrder_.push_back(n);
        if (traits.kind == BrainTaskTraits::Root) roots_.push_back(n);
        if (traits.hasPrimary) {
            BrainTaskCondition ref; ref.node = n;
            if (!host_.SupportsCondition(graph_, ref) || state_.failed) return Fail("Unsupported primary condition");
        }
        for (auto index : graph_.nodes[n].outputs) {
            const auto &output = graph_.outputs[index];
            const auto &flow = state_.flow.outputs[index];
            if (!flow.supported || !flow.flagsKnown || (flow.flags & ~(1u | 0x10u)))
                return Fail("Task runtime supports only normal, unit-decay outputs");
            for (auto record : output.conditions) {
                const auto &row = graph_.records[record];
                if (record + 1 < graph_.records.size() && graph_.records[record + 1].depth > 2)
                    return Fail("Nested condition semantics are unsupported");
                BrainTaskCondition ref; ref.node = n;
                if (row.tokens == std::vector<std::string>({"-1"})) {
                    if (!traits.hasPrimary) continue;
                } else {
                    ref.record = record;
                    if (!row.tokens.empty() && row.tokens[0] == "-1")
                        return Fail("Malformed primary condition reference");
                }
                if (!host_.SupportsCondition(graph_, ref) || state_.failed) return Fail("Unsupported output condition");
                conditions_[index].push_back(ref);
            }
        }
    }
    // Roots are independent event sources in this admitted normal profile.
    // Activating one as the ordinary current task would mix both exit paths;
    // that separate retail activation profile has not been established.
    for (const auto &output : graph_.outputs)
        for (const auto &destination : output.destinations)
            if (destination.targetNode >= traits_.size() ||
                traits_[destination.targetNode].kind != BrainTaskTraits::Normal)
                return Fail("Root destinations require an unsupported activation profile");
    std::sort(nodeOrder_.begin(), nodeOrder_.end(), [&](std::size_t a, std::size_t b) {
        return graph_.nodes[a].id < graph_.nodes[b].id;
    });
    if (state_.failed) return false;
    prepared_ = true;
    return true;
}
bool BrainTaskRuntime::Hooks(std::size_t node, BrainTaskPhase phase, std::int32_t now) {
    for (auto output : graph_.nodes[node].outputs) {
        const auto &refs = conditions_[output];
        for (auto i = refs.rbegin(); i != refs.rend(); ++i)
            if (host_.Condition(graph_, *i, phase, Context(node, now)) != BrainTaskHook::Applied || state_.failed)
                return Fail("Unsupported condition lifecycle hook");
    }
    if (phase == BrainTaskPhase::Initialize) {
        state_.nodes[node].flags |= 0x2000u;
        if (traits_[node].forceOnceOnInitialize) state_.nodes[node].flags |= 1u;
    }
    if (host_.Task(graph_, node, phase, Context(node, now)) != BrainTaskHook::Applied || state_.failed)
        return Fail("Unsupported Task lifecycle hook");
    return true;
}
bool BrainTaskRuntime::Enter(std::size_t node, std::int32_t now, bool changeActive) {
    auto &flags = state_.nodes[node].flags;
    if (changeActive) {
        flags = (flags & ~0x60u) | 0x10u;
        state_.activeNode = node;
        state_.activeTaskStart = state_.clock;
    }
    flags |= 0x8000u;
    if (!(flags & 1u)) flags &= ~0x80000u;
    for (auto output : graph_.nodes[node].outputs) {
        auto &outputFlags = state_.flow.outputs[output].flags;
        if (!(outputFlags & 1u)) outputFlags &= ~0x80000u;
        const auto &refs = conditions_[output];
        for (auto i = refs.rbegin(); i != refs.rend(); ++i)
            if (host_.Condition(graph_, *i, BrainTaskPhase::Enter, Context(node, now)) != BrainTaskHook::Applied || state_.failed)
                return Fail("Unsupported condition entry hook");
    }
    if (!(flags & 0x80000u) && (host_.Task(graph_, node, BrainTaskPhase::Enter, Context(node, now)) != BrainTaskHook::Applied || state_.failed))
        return Fail("Unsupported Task entry hook");
    return true;
}
bool BrainTaskRuntime::Initialize(std::int32_t now) {
    state_.initialized = true;
    for (auto node : nodeOrder_) {
        if (!Hooks(node, BrainTaskPhase::Initialize, now)) return false;
    }
    for (auto node : roots_)
        if (!Enter(node, now, false)) return false;
    return true;
}
bool BrainTaskRuntime::Activate(std::size_t target, std::int32_t now) {
    if (state_.activeNode != BrainRecord::None() && !Hooks(state_.activeNode, BrainTaskPhase::Exit, now))
        return false;
    return Enter(target, now, true);
}
BrainTaskResult BrainTaskRuntime::Tick(std::int32_t now, std::size_t transitionBudget) {
    if (busy_) { Fail("Recursive Task runtime entry is unsupported"); return Failed(); }
    if (state_.failed) return Failed();
    if (now < 0 || (haveInputClock_ && now < inputClock_) || transitionBudget == 0 || transitionBudget > 50) {
        Fail("Unsupported clock or transition budget"); return Failed();
    }
    BusyScope busy(busy_);
    try {
    if (!Prepare()) return Failed();
    haveInputClock_ = true;
    inputClock_ = now;
    if (!state_.initialized && !Initialize(now)) return Failed();
    bool work = state_.activeNode != BrainRecord::None();
    for (auto root : roots_)
        for (auto output : graph_.nodes[root].outputs)
            work = work || !graph_.outputs[output].destinations.empty();
    BrainTaskResult result;
    if (!work) return result;
    state_.clock = now;
    bool checkRoots = true;
    Conditions resolver(*this, now);
    while (!state_.failed) {
        BrainFlowDecision decision;
        BrainFlowState next;
        std::size_t source = BrainRecord::None();
        if (checkRoots) {
            for (auto root : roots_) {
                const auto flags = state_.nodes[root].flags;
                if ((flags & 0x80000u) && (flags & 1u)) continue;
                bool linked = false;
                for (auto out : graph_.nodes[root].outputs) linked = linked || !graph_.outputs[out].destinations.empty();
                if (!linked) continue;
                const auto ready = host_.RootReady(graph_, root, Context(root, now));
                if (state_.failed || (ready != BrainConditionValue::True && ready != BrainConditionValue::False)) {
                    Fail("Unsupported root predicate"); break;
                }
                if (ready == BrainConditionValue::False) continue;
                next = state_.flow;
                decision = SelectBrainTransition(graph_, root, next, resolver, random_);
                if (decision.status != BrainFlowDecision::NoTransition) { source = root; break; }
            }
        }
        if (state_.failed) break;
        if (decision.status == BrainFlowDecision::NoTransition && state_.activeNode != BrainRecord::None()) {
            source = state_.activeNode;
            next = state_.flow;
            decision = SelectBrainTransition(graph_, source, next, resolver, random_);
        }
        if (state_.failed) break;
        if (decision.status == BrainFlowDecision::Unsupported) { Fail(decision.reason.c_str()); break; }
        if (decision.status == BrainFlowDecision::NoTransition) break;
        if (result.transitions == transitionBudget) {
            Fail("Normal Task transition budget exceeded");
            result.status = BrainTaskResult::BudgetExceeded;
            break;
        }
        state_.flow = std::move(next);
        if (!Activate(decision.targetNode, now)) break;
        state_.nodes[source].flags |= 0x80000u;
        if (traits_[source].kind == BrainTaskTraits::Root && !Hooks(source, BrainTaskPhase::Exit, now)) break;
        ++result.transitions;
        checkRoots = false;
    }
    if (state_.failed) {
        if (result.status != BrainTaskResult::BudgetExceeded) result.status = BrainTaskResult::Unsupported;
        result.reason = failure_;
    } else if (result.transitions) result.status = BrainTaskResult::Advanced;
    return result;
    } catch (...) {
        Fail("Task runtime poisoned by a callback or allocation exception");
        return Failed();
    }
}
BrainTaskResult BrainTaskRuntime::Reset(std::int32_t now) {
    if (busy_) { Fail("Recursive Task runtime reset is unsupported"); return Failed(); }
    if (state_.failed) return Failed();
    if (!state_.initialized || now < 0 || (haveInputClock_ && now < inputClock_)) {
        Fail("Reset requires an initialized instance and a nondecreasing clock"); return Failed();
    }
    BusyScope busy(busy_);
    try {
    state_.use.Clear();
    state_.activeNode = BrainRecord::None();
    state_.clock = state_.activeTaskStart = 0;
    inputClock_ = now;
    for (auto node : nodeOrder_) {
        state_.nodes[node].flags &= 0xffe07fffu;
        for (auto output : graph_.nodes[node].outputs) state_.flow.outputs[output].flags &= ~0x80000u;
        if (!Hooks(node, BrainTaskPhase::Bind, now)) return Failed();
    }
    for (auto root : roots_)
        if (!Enter(root, now, false)) return Failed();
    if (state_.failed) return Failed();
    return BrainTaskResult();
    } catch (...) {
        Fail("Task runtime poisoned by a reset callback or allocation exception");
        return Failed();
    }
}
} // namespace wolf2009

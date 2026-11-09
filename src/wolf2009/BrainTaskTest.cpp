// SPDX-License-Identifier: GPL-3.0-or-later
// TestHost supplies declared actions/external truth. It is not campaign gameplay.
#include "BrainTask.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
std::string Wire(std::string text) { for (auto &c : text) if (c == '|') c = 0; return text; }
std::string OutputF(unsigned flags, unsigned target, const std::string &conditions = "") {
    std::string result = "\t";
    for (unsigned i = 0; i < 99; ++i)
        result += (i == 0 ? std::to_string(flags) : i == 1 ? "1" : i == 2 ? std::to_string(target) :
                   i == 98 ? "1.000000" : "0") + std::string("|");
    return result + "\n" + conditions;
}
std::string Output(unsigned target, const std::string &conditions = "\t\t-1|\n") {
    return OutputF(16, target, conditions);
}
std::string Node(unsigned type, unsigned id, const std::string &properties = "") {
    return std::to_string(type) + '|' + std::to_string(id) + "|0|0|0|^" +
           std::to_string(type) + "|0|" + properties + '\n';
}
BrainGraph Decode(const std::string &body) {
    const auto text = Wire("h1023|32|32|32|0|\n" + body);
    return DecodeBrainGraph({text.begin(), text.end()});
}
BrainGraph DelayGraph(const std::string &minimum = "50 0", const std::string &maximum = "0 0",
                      const std::string &extraConditions = "") {
    // Deliberately not ID order: Initialize uses ascending IDs; roots retain file order.
    return Decode(Node(1, 20, "^74|1024|") + Output(9) + Node(68, 50, "mUseTarget|target|") +
                  Node(30, 9, "^65|1024|mDuration|" + minimum + "|mDurationMax|" + maximum +
                                  "|mActualDuration|0|mTimeSet|0|") +
                  Output(50, "\t\t-1|\n" + extraConditions));
}
std::size_t Index(const BrainGraph &graph, unsigned id) {
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) if (graph.nodes[i].id == id) return i;
    throw std::runtime_error("Missing test node");
}
std::string Property(const std::vector<std::string> &tokens, std::size_t start,
                     const std::string &key, const std::string &fallback = "") {
    for (std::size_t i = start; i + 1 < tokens.size(); i += 2)
        if (tokens[i] == key) return tokens[i + 1];
    return fallback;
}
std::string Property(const BrainGraph &graph, std::size_t node, const std::string &key,
                     const std::string &fallback = "") {
    return Property(graph.records[graph.nodes[node].record].tokens, 5, key, fallback);
}
bool Literal(const std::string &text, std::int32_t &value, bool expressionPair) {
    std::istringstream input(text); input.imbue(std::locale::classic());
    std::int64_t wide = 0; int mode = 0;
    if (!(input >> wide) || (expressionPair && (!(input >> mode) || mode != 0))) return false;
    input >> std::ws;
    if (!input.eof() || wide < std::numeric_limits<std::int32_t>::min() ||
        wide > std::numeric_limits<std::int32_t>::max()) return false;
    value = static_cast<std::int32_t>(wide); return true;
}
struct Random : BrainFlowRandom {
    unsigned calls = 0; std::uint32_t sample = 0;
    std::uint32_t Next15() override { ++calls; return sample; }
};
struct Event {
    bool condition; BrainTaskPhase phase; unsigned node; std::size_t record;
    std::int32_t start; unsigned active; bool used; std::uint32_t flags;
};
struct Effect { std::uint64_t owner; unsigned type; std::string target, key; bool used; };
class TestHost : public BrainTaskHost {
  public:
    std::vector<Event> events;
    std::vector<Effect> effects;
    std::map<std::uint64_t, bool> exists, near, animation;
    std::map<std::string, int> variables;
    std::map<unsigned, BrainConditionValue> external;
    std::map<const void *, std::map<std::size_t, BrainTaskTimer>> timers;
    std::vector<std::pair<std::size_t, std::size_t>> evaluated; // (events.size(), explicit record)
    std::set<unsigned> probeTypes{68}; // Declared TestHost probe-entry admission.
    BrainTaskRuntime *attackRuntime = NULL;
    std::string attack;
    bool attacked = false;
    void Attack(const char *place, std::int32_t now) {
        if (attacked || attack != place) return;
        attacked = true;
        if (attack == "throw") throw std::runtime_error("test host exception");
        if (attack == "reset") attackRuntime->Reset(now);
        else attackRuntime->Tick(now);
    }
    BrainTaskTraits Describe(const BrainGraph &graph, std::size_t node) override {
        Attack("describe", 100);
        BrainTaskTraits traits;
        const auto type = graph.nodes[node].type;
        if (type != 1 && type != 3 && type != 30 && type != 68 && type != 51 && type != 220) return traits;
        traits.kind = type == 1 || type == 3 ? BrainTaskTraits::Root : BrainTaskTraits::Normal;
        traits.normalActivationKnown = true; // Explicit TestHost model for action nodes.
        traits.hasPrimary = type == 1 || type == 3 || type == 30 || type == 51;
        traits.forceOnceOnInitialize = type == 1;
        traits.probeLeafEntryKnown = probeTypes.count(type) != 0;
        return traits;
    }
    bool SupportsCondition(const BrainGraph &graph, const BrainTaskCondition &ref) override {
        Attack("supports", 100);
        if (!ref.IsPrimary()) {
            const auto &type = graph.records[ref.record].tokens[0];
            return type == "21" || type == "221" || type == "600" || type == "601";
        }
        if (graph.nodes[ref.node].type != 30) return true;
        std::int32_t minimum = 0, maximum = 0;
        return Literal(Property(graph, ref.node, "mDuration", "2000 0"), minimum, true) &&
               Literal(Property(graph, ref.node, "mDurationMax", "0 0"), maximum, true) &&
               minimum >= -1 && maximum >= 0 &&
               (minimum == -1 || maximum == 0 || (maximum >= minimum &&
                 static_cast<std::int64_t>(maximum) - minimum <= 65535));
    }
    void Record(const BrainGraph &graph, std::size_t node, bool condition, std::size_t record,
                BrainTaskPhase phase, const BrainTaskContext &ctx) {
        events.push_back({condition, phase, graph.nodes[node].id, record, ctx.activeTaskStart,
                          ctx.activeNode == BrainRecord::None() ? 0 : graph.nodes[ctx.activeNode].id,
                          ctx.use->pending, ctx.taskFlags});
    }
    BrainTaskHook Task(const BrainGraph &graph, std::size_t node, BrainTaskPhase phase,
                       const BrainTaskContext &ctx) override {
        Record(graph, node, false, BrainRecord::None(), phase, ctx);
        if (phase == BrainTaskPhase::Enter && (ctx.taskFlags & 0x20u)) {
            if (attack == "probethrow") { attacked = true; throw std::runtime_error("probe host exception"); }
            if (attack == "probeuse") ctx.use->Notify("from_probe");
            Attack("probe", ctx.now);
        }
        if (phase == BrainTaskPhase::Enter && graph.nodes[node].type == 30) {
            if (attack == "throw") { Attack("throw", ctx.now); return BrainTaskHook::Applied; }
            Attack("enter", ctx.now);
        }
        const auto type = graph.nodes[node].type;
        if (phase == BrainTaskPhase::Exit && type == 3 && !(ctx.taskFlags & 1u))
            ctx.use->Consume(Property(graph, node, "mUseKey"));
        if (phase != BrainTaskPhase::Enter || type == 1 || type == 3 || type == 30)
            return BrainTaskHook::Applied;
        if (type == 220) {
            std::int32_t value = 0;
            if (!Literal(Property(graph, node, "mExpression"), value, false)) return BrainTaskHook::Unsupported;
            variables[Property(graph, node, "mVar")] = value;
        }
        effects.push_back({ctx.owner, type,
                           Property(graph, node, type == 51 ? "mActor" : "mUseTarget"),
                           Property(graph, node, "mUseKey"), ctx.use->pending});
        return BrainTaskHook::Applied;
    }
    BrainTaskHook Condition(const BrainGraph &graph, const BrainTaskCondition &ref,
                            BrainTaskPhase phase, const BrainTaskContext &ctx) override {
        Record(graph, ref.node, true, ref.record, phase, ctx);
        if (phase == BrainTaskPhase::Initialize) Attack("condition", ctx.now);
        if (phase == BrainTaskPhase::Bind) Attack("reset", ctx.now);
        if (ref.IsPrimary() && graph.nodes[ref.node].type == 30) {
            auto &timer = timers[ctx.instance][ref.node];
            if (phase == BrainTaskPhase::Initialize) timer.Initialize();
            if (phase == BrainTaskPhase::Enter) {
                std::int32_t minimum = 0, maximum = 0;
                if (!Literal(Property(graph, ref.node, "mDuration", "2000 0"), minimum, true) ||
                    !Literal(Property(graph, ref.node, "mDurationMax", "0 0"), maximum, true))
                    return BrainTaskHook::Unsupported;
                return timer.Enter(ctx.now, minimum, maximum, *ctx.random);
            }
        }
        return BrainTaskHook::Applied;
    }
    BrainConditionValue Value(const std::map<std::uint64_t, bool> &values, std::uint64_t owner) {
        const auto i = values.find(owner);
        return i == values.end() ? BrainConditionValue::Unsupported :
               i->second ? BrainConditionValue::True : BrainConditionValue::False;
    }
    BrainConditionValue Evaluate(const BrainGraph &graph, const BrainTaskCondition &ref,
                                 const BrainTaskContext &ctx) override {
        if (ref.IsPrimary()) {
            const auto type = graph.nodes[ref.node].type;
            if (type == 1) return Value(exists, ctx.owner);
            if (type == 3) return ctx.use->Matches(Property(graph, ref.node, "mUseKey")) ?
                                 BrainConditionValue::True : BrainConditionValue::False;
            if (type == 30) return timers[ctx.instance][ref.node].Evaluate(ctx.now, ctx.activeTaskStart);
            if (type == 51) return Value(animation, ctx.owner);
            return BrainConditionValue::MissingPrimary;
        }
        const auto &tokens = graph.records[ref.record].tokens;
        evaluated.push_back(std::make_pair(events.size(), ref.record));
        if (tokens[0] == "21") return Value(near, ctx.owner);
        if (tokens[0] == "221") {
            // Explicit TestHost expression model, not a retail expression interpreter.
            std::istringstream input(Property(tokens, 1, "mExpression"));
            std::string name, op; int value = 0;
            if (!(input >> name >> op >> value) || op != "==" || !variables.count(name))
                return BrainConditionValue::Unsupported;
            return variables[name] == value ? BrainConditionValue::True : BrainConditionValue::False;
        }
        const auto found = external.find(static_cast<unsigned>(std::stoul(tokens[0])));
        return found == external.end() ? BrainConditionValue::Unsupported : found->second;
    }
    BrainConditionValue RootReady(const BrainGraph &graph, std::size_t node,
                                  const BrainTaskContext &ctx) override {
        BrainTaskCondition ref; ref.node = node;
        return Evaluate(graph, ref, ctx);
    }
};
std::size_t Find(const std::vector<Event> &events, unsigned node, bool condition,
                 BrainTaskPhase phase, std::size_t start = 0) {
    for (std::size_t i = start; i < events.size(); ++i)
        if (events[i].node == node && events[i].condition == condition && events[i].phase == phase) return i;
    return events.size();
}
void PrepareOwner(TestHost &host, std::uint64_t owner = 1) { host.exists[owner] = true; }
// Task Enter/Exit order as "E<id>"/"X<id>", separated by spaces.
std::string Trace(const std::vector<Event> &events, std::size_t start = 0) {
    std::string result;
    for (std::size_t i = start; i < events.size(); ++i) {
        if (events[i].condition ||
            (events[i].phase != BrainTaskPhase::Enter && events[i].phase != BrainTaskPhase::Exit)) continue;
        if (!result.empty()) result += ' ';
        result += (events[i].phase == BrainTaskPhase::Enter ? 'E' : 'X') + std::to_string(events[i].node);
    }
    return result;
}
const BrainTaskProfile kProbe = BrainTaskProfile::ProbeLeafPassV1;
// A: P11, P12, N20, P13 (the greeter node 35 output shape); B=20: P21.
BrainGraph ProbeGraph() {
    return Decode(Node(1, 1, "^74|1024|") + Output(10) +
                  Node(68, 10, "mUseTarget|a|") + OutputF(32, 11) + OutputF(32, 12) + OutputF(16, 20) +
                  OutputF(32, 13) + Node(68, 11) + Node(68, 12) + Node(68, 13) +
                  Node(68, 20, "mUseTarget|b|") + OutputF(32, 21) + Node(68, 21));
}
BrainTaskResult Refusal(const BrainGraph &graph, std::size_t &events, bool clearProbeTypes = false,
                        BrainTaskProfile profile = BrainTaskProfile::ProbeLeafPassV1) {
    TestHost host; PrepareOwner(host); Random random;
    if (clearProbeTypes) host.probeTypes.clear();
    BrainTaskRuntime runtime(graph, host, random, 1, profile);
    const auto result = runtime.Tick(100);
    events = host.events.size() + host.effects.size();
    return result;
}
} // namespace

int RunBrainTaskTests() {
    int passed = 0;
    try {
        const auto graph = DelayGraph();
        Random random; TestHost host; PrepareOwner(host);
        BrainTaskRuntime runtime(graph, host, random, 1);
        auto result = runtime.Tick(100);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 1 &&
                 runtime.State().activeNode == Index(graph, 9) && runtime.State().activeTaskStart == 100,
             "Spawn did not enter the generic Wait task");
        Need(Find(host.events, 9, false, BrainTaskPhase::Initialize) <
                 Find(host.events, 20, false, BrainTaskPhase::Initialize),
             "Initialization did not follow authored node ID order");
        const auto enteredWait = Find(host.events, 9, false, BrainTaskPhase::Enter);
        const auto exitedRoot = Find(host.events, 20, false, BrainTaskPhase::Exit);
        Need(enteredWait < exitedRoot && host.events[exitedRoot].active == 9,
             "Source root exited before target entry");
        Need(runtime.Tick(150).status == BrainTaskResult::Idle && host.effects.empty() &&
                 runtime.State().activeTaskStart == 100,
             "Timer equality passed or OnSpawn restarted the active task");
        Need(runtime.Tick(151).transitions == 1 && host.effects.size() == 1,
             "Elapsed Wait did not enter its action");
        Need(Find(host.events, 9, false, BrainTaskPhase::Exit) < Find(host.events, 50, false, BrainTaskPhase::Enter),
             "Old active Task exited after the new Task entered");
        Need(runtime.Tick(200).status == BrainTaskResult::Idle && host.effects.size() == 1,
             "A terminal task replayed its action");
        ++passed;

        const auto usedGraph = Decode(Node(3, 1, "^64|1024|mUseKey|switch_off|") + Output(2) + Node(68, 2));
        TestHost usedHost; Random usedRandom;
        BrainTaskRuntime used(usedGraph, usedHost, usedRandom, 1);
        used.NotifyUse("wrong"); Need(used.Tick(100).status == BrainTaskResult::Idle && used.State().use.pending,
                                    "Unmatched Use was automatically consumed");
        used.NotifyUse("SWITCH_OFF");
        Need(used.Tick(101).transitions == 1 && usedHost.effects.size() == 1 && usedHost.effects[0].used &&
                 !used.State().use.pending,
             "Use key matching or Enter-before-consume order changed");
        Need(used.Tick(102).status == BrainTaskResult::Idle, "Use retriggered without a notification");
        used.NotifyUse("switch_off");
        Need(used.Tick(103).transitions == 1 && usedHost.effects.size() == 2,
             "Repeatable OnUsed root remained processed");
        ++passed;
        BrainTaskUse input;
        input.Notify("first"); input.Notify("LaSt");
        Need(input.Matches("last") && !input.Matches("first") && input.Consume("LAST") && !input.pending,
             "Use is not a latest-key ASCII latch");
        input.Notify(std::string("A") + '\xd0');
        Need(input.Matches(std::string("a") + '\xd0') && !input.Matches(std::string("a") + '\xf0') &&
                 !input.Notify(std::string(1024, 'x')) && input.pending,
             "Use bounds or non-ASCII byte preservation changed");
        ++passed;

        const auto zeroGraph = DelayGraph("0 0"); TestHost zeroHost; PrepareOwner(zeroHost); Random zeroRandom;
        BrainTaskRuntime zero(zeroGraph, zeroHost, zeroRandom, 1);
        Need(zero.Tick(100, 1).transitions == 1 && zero.Tick(100, 1).status == BrainTaskResult::Idle &&
                 zeroHost.effects.empty() && zero.Tick(101, 1).transitions == 1,
             "Zero-duration Wait was instantaneous in its entry tick");
        ++passed;
        const auto cycleGraph = Decode(Node(1, 1, "^74|1024|") + Output(2) + Node(68, 2) + Output(2));
        TestHost cycleHost; PrepareOwner(cycleHost); Random cycleRandom;
        BrainTaskRuntime cycle(cycleGraph, cycleHost, cycleRandom, 1);
        result = cycle.Tick(100, 4);
        Need(result.status == BrainTaskResult::BudgetExceeded && result.transitions == 4 &&
                 cycleHost.effects.size() == 4 && cycle.Tick(101).status == BrainTaskResult::Unsupported &&
                 cycleHost.effects.size() == 4,
             "Same-node action cycle exceeded its bounded effect budget");
        ++passed;

        const auto sampledGraph = DelayGraph("10 0", "20 0"); TestHost sampledHost; PrepareOwner(sampledHost);
        Random sampledRandom; BrainTaskRuntime sampled(sampledGraph, sampledHost, sampledRandom, 1);
        sampled.Tick(100); Need(sampledRandom.calls == 1, "Random timer was not armed once");
        const auto hooksBeforeReset = sampledHost.events.size();
        Need(sampled.Reset(100).status == BrainTaskResult::Idle && !sampled.State().use.pending &&
                 sampled.State().activeNode == BrainRecord::None(), "Reset did not clear network state");
        for (std::size_t i = hooksBeforeReset; i < sampledHost.events.size(); ++i)
            Need(sampledHost.events[i].phase != BrainTaskPhase::Initialize &&
                     sampledHost.events[i].phase != BrainTaskPhase::Exit,
                 "Reset invented Initialize/Exit callbacks");
        sampled.Tick(100); Need(sampledRandom.calls == 1, "Same-clock reset resampled the timer");
        sampled.Reset(101); sampled.Tick(101); Need(sampledRandom.calls == 2, "Later entry did not rearm the timer");
        ++passed;

        BrainTaskTimer timer; Random timerRandom; timerRandom.sample = 32767;
        Need(timer.Enter(100, 10, 20, timerRandom) == BrainTaskHook::Applied && timer.actualDuration == 19 &&
                 timer.Evaluate(119, 100) == BrainConditionValue::False &&
                 timer.Evaluate(120, 100) == BrainConditionValue::True, "Timer range or strict boundary changed");
        timer.Enter(100, 10, 20, timerRandom); Need(timerRandom.calls == 1, "Same-tick entry drew RNG twice");
        timer.Enter(101, 20, 20, timerRandom); Need(timerRandom.calls == 2 && timer.actualDuration == 20,
                                                "Equal random endpoints skipped the native RNG step");
        Need(timer.Enter(102, 0, 65536, timerRandom) == BrainTaskHook::Unsupported &&
                 timer.Enter(102, std::numeric_limits<std::int32_t>::max(), 0, timerRandom) == BrainTaskHook::Applied &&
                 timer.Evaluate(103, 1) == BrainConditionValue::Unsupported,
             "Timer overflow profile did not fail closed");
        ++passed;

        TestHost shared; PrepareOwner(shared, 1); PrepareOwner(shared, 2); Random sharedRandom;
        BrainTaskRuntime first(graph, shared, sharedRandom, 1), second(graph, shared, sharedRandom, 2);
        first.Tick(100); second.Tick(120); first.Tick(151); second.Tick(151);
        Need(shared.effects.size() == 1 && shared.effects[0].owner == 1 &&
                 second.State().activeTaskStart == 120, "Instances shared active/timer state");
        ++passed;

        const auto unknown = Decode(Node(999, 1)); TestHost unknownHost; Random unknownRandom;
        BrainTaskRuntime unknownRuntime(unknown, unknownHost, unknownRandom);
        Need(unknownRuntime.Tick(100).status == BrainTaskResult::Unsupported && unknownHost.events.empty(),
             "Unknown Task traits were accepted");
        const auto rootChain = Decode(Node(1, 1, "^74|1024|") + Output(2) +
                                      Node(3, 2, "^64|1024|mUseKey|switch|") + Output(3) + Node(68, 3));
        TestHost rootHost; PrepareOwner(rootHost); Random rootRandom;
        BrainTaskRuntime rootRuntime(rootChain, rootHost, rootRandom, 1);
        rootRuntime.NotifyUse("switch");
        Need(rootRuntime.Tick(100).status == BrainTaskResult::Unsupported && rootHost.events.empty() &&
                 rootHost.effects.empty(), "Root activation mixed ordinary and event-source exit paths");
        const auto duplicate = Decode(Node(1, 1, "^1|1|^74|1024|"));
        TestHost duplicateHost; Random duplicateRandom;
        BrainTaskRuntime duplicateRuntime(duplicate, duplicateHost, duplicateRandom);
        Need(duplicateRuntime.Tick(100).status == BrainTaskResult::Unsupported && duplicateHost.events.empty(),
             "Duplicate flag key was silently selected");
        const auto expressionGraph = DelayGraph("@unbound 1"); TestHost expressionHost; Random expressionRandom;
        BrainTaskRuntime expression(expressionGraph, expressionHost, expressionRandom);
        Need(expression.Tick(100).status == BrainTaskResult::Unsupported && expressionHost.events.empty(),
             "Unknown duration expression became a literal");
        ++passed;

        for (const std::string attack : {"describe", "supports", "condition", "enter", "throw"}) {
            TestHost hostile; PrepareOwner(hostile); Random hostileRandom;
            BrainTaskRuntime target(graph, hostile, hostileRandom, 1);
            hostile.attackRuntime = &target; hostile.attack = attack;
            result = target.Tick(100);
            Need(result.status == BrainTaskResult::Unsupported && target.State().failed,
                 "Host recursive entry/exception did not poison the instance");
            const auto eventCount = hostile.events.size();
            const auto reason = result.reason;
            Need(target.Tick(101).reason == reason && target.Reset(101).reason == reason &&
                     hostile.events.size() == eventCount,
                 "Poisoned runtime continued callbacks or kept a stale busy guard");
            if (attack == "describe" || attack == "supports") Need(hostile.events.empty(), "Admission reentry reached lifecycle hooks");
            if (attack == "condition") Need(hostile.events.size() == 1, "Condition reentry reached another hook");
            if (attack == "enter" || attack == "throw")
                Need(Find(hostile.events, 20, false, BrainTaskPhase::Exit) == hostile.events.size(),
                     "Failed target entry still exited the source root");
        }
        ++passed;
        TestHost resetHost; PrepareOwner(resetHost); Random resetRandom;
        BrainTaskRuntime reset(graph, resetHost, resetRandom, 1); resetHost.attackRuntime = &reset;
        reset.Tick(100); resetHost.events.clear(); resetHost.attack = "reset";
        Need(reset.Reset(100).status == BrainTaskResult::Unsupported && reset.State().failed &&
                 resetHost.events.size() == 1, "Reset reentry returned success or re-entered roots");
        ++passed;

        // ProbeLeafPassV1: probe order, several probes, probe plus normal transition in one Tick.
        const auto probeGraph = ProbeGraph();
        TestHost probeHost; PrepareOwner(probeHost); Random probeRandom;
        BrainTaskRuntime probe(probeGraph, probeHost, probeRandom, 1, kProbe);
        result = probe.Tick(100);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 2 && result.probes == 4 &&
                 probe.State().activeNode == Index(probeGraph, 20) && probe.State().activeTaskStart == 100,
             "Probe Tick did not combine normal transitions and probe flushes");
        Need(Trace(probeHost.events) == "E1 E10 X1 E11 E12 E13 X10 E20 E21",
             "Probe pass/flush order differs from the inspected Tick order");
        for (unsigned id : {11u, 12u, 13u}) {
            const auto &event = probeHost.events[Find(probeHost.events, id, false, BrainTaskPhase::Enter)];
            Need(event.active == 10 && event.start == 100 && (event.flags & 0x20u) && !(event.flags & 0x50u),
                 "Probe entry changed the active task or missed probe task flags");
        }
        Need(probeHost.events[Find(probeHost.events, 21, false, BrainTaskPhase::Enter)].active == 20,
             "Probe pass did not run on the task entered in the same iteration");
        Need(probeHost.effects.size() == 6 && probeRandom.calls == 0, "Probe entries did not reach the host once");
        auto before = probeHost.events.size();
        result = probe.Tick(101);
        Need(result.status == BrainTaskResult::Idle && result.probes == 0 && probeHost.events.size() == before,
             "Processed probe outputs fired again without source re-entry");
        ++passed;

        // Late probe condition: fires without changing active task/start time or exiting it.
        const auto lateGraph = Decode(Node(1, 1, "^74|1024|") + Output(10) + Node(68, 10) +
                                      OutputF(32, 11, "\t\t600|\n") + OutputF(16, 20, "\t\t601|\n") +
                                      Node(68, 11) + Node(68, 20));
        TestHost lateHost; PrepareOwner(lateHost); Random lateRandom;
        lateHost.external[600] = lateHost.external[601] = BrainConditionValue::False;
        BrainTaskRuntime late(lateGraph, lateHost, lateRandom, 1, kProbe);
        Need(late.Tick(100).transitions == 1 && late.Tick(120).status == BrainTaskResult::Idle,
             "False probe condition fired");
        lateHost.external[600] = BrainConditionValue::True;
        before = lateHost.events.size();
        result = late.Tick(150);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 0 && result.probes == 1 &&
                 Trace(lateHost.events, before) == "E11" && late.State().activeNode == Index(lateGraph, 10) &&
                 late.State().activeTaskStart == 100 && lateHost.events.back().start == 100,
             "Late probe changed the active task, start time or exited it");
        Need(late.Tick(160).status == BrainTaskResult::Idle, "Late probe fired twice");
        lateHost.external[601] = BrainConditionValue::True;
        before = lateHost.events.size();
        Need(late.Tick(170).transitions == 1 && Trace(lateHost.events, before) == "X10 E20" &&
                 late.State().activeTaskStart == 170, "Normal transition after a probe changed");
        ++passed;

        // Conditions of all probes are evaluated in the pass, before the first probe entry.
        const auto evalGraph = Decode(Node(1, 1, "^74|1024|") + Output(10) + Node(68, 10) +
                                      OutputF(32, 11, "\t\t600|\n") + OutputF(32, 12, "\t\t601|\n") +
                                      Node(68, 11) + Node(68, 12));
        TestHost evalHost; PrepareOwner(evalHost); Random evalRandom;
        evalHost.external[600] = evalHost.external[601] = BrainConditionValue::True;
        BrainTaskRuntime evaluation(evalGraph, evalHost, evalRandom, 1, kProbe);
        Need(evaluation.Tick(100).probes == 2 && evalHost.evaluated.size() == 2, "Probe conditions were not evaluated once");
        const auto firstProbe = Find(evalHost.events, 11, false, BrainTaskPhase::Enter);
        Need(evalHost.evaluated[0].first <= firstProbe && evalHost.evaluated[1].first <= firstProbe &&
                 evalHost.evaluated[0].second < evalHost.evaluated[1].second,
             "A probe fired before all probe conditions were evaluated");
        ++passed;

        // One-shot probes stay processed; repeatable probes re-arm when the source re-enters.
        const auto onceGraph = Decode(Node(3, 1, "^64|1024|mUseKey|go|") + Output(10) + Node(68, 10) +
                                      OutputF(32, 11) + OutputF(33, 12) + Node(68, 11) + Node(68, 12));
        TestHost onceHost; Random onceRandom;
        BrainTaskRuntime once(onceGraph, onceHost, onceRandom, 1, kProbe);
        once.NotifyUse("go");
        result = once.Tick(100);
        Need(result.probes == 2 && Trace(onceHost.events) == "E1 E10 X1 E11 E12", "First probe entry differed");
        before = onceHost.events.size();
        once.NotifyUse("go");
        result = once.Tick(101);
        Need(result.transitions == 1 && result.probes == 1 && Trace(onceHost.events, before) == "X10 E10 X1 E11" &&
                 (once.State().flow.outputs[2].flags & 0x80001u) == 0x80001u,
             "Source re-entry did not re-arm only repeatable probes");
        ++passed;

        // Explicit refusals before any host hook.
        std::size_t touched = 0;
        const char *const refusals[] = {
            "ProbeLeafPassV1 refuses a probe destination with outputs",
            "ProbeLeafPassV1 refuses a probe destination that is also a normal destination",
            "ProbeLeafPassV1 refuses probe outputs on root tasks",
            "Probe destination entry is not admitted by the host",
            "ProbeLeafPassV1 admits only unit-decay probe outputs with flags 0x20/0x21",
        };
        const std::string root = Node(1, 1, "^74|1024|") + Output(10);
        const BrainGraph refusedGraphs[] = {
            Decode(root + Node(68, 10) + OutputF(32, 11) + Node(68, 11) + OutputF(16, 12) + Node(68, 12)),
            Decode(root + Node(68, 10) + OutputF(32, 11) + OutputF(16, 11) + Node(68, 11)),
            Decode(root + OutputF(32, 11) + Node(68, 10) + Node(68, 11)),
            Decode(root + Node(68, 10) + OutputF(32, 11) + Node(68, 11)),
            Decode(root + Node(68, 10) + OutputF(0x60, 11) + Node(68, 11)),
        };
        for (std::size_t i = 0; i < 5; ++i) {
            result = Refusal(refusedGraphs[i], touched, i == 3);
            Need(result.status == BrainTaskResult::Unsupported && result.reason == refusals[i] && touched == 0,
                 "Probe profile refusal changed or reached host hooks");
        }
        for (const unsigned flags : {0x30u, 0x100020u, 0x40020u, 0x80020u}) {
            result = Refusal(Decode(root + Node(68, 10) + OutputF(flags, 11) + Node(68, 11)), touched);
            Need(result.status == BrainTaskResult::Unsupported && touched == 0,
                 "Unknown, disabled or runtime-marked serialized probe flags were admitted");
        }
        result = Refusal(probeGraph, touched, false, static_cast<BrainTaskProfile>(7));
        Need(result.status == BrainTaskResult::Unsupported && result.reason == "Unknown Task runtime profile" &&
                 touched == 0, "Unknown runtime profile was admitted");
        // Serialized ^ID 32 (saved probe-entered state) is raw state, admitted only by the profile.
        const auto savedProbe = Decode(root + "68|10|0|0|0|^68|32|\n");
        result = Refusal(savedProbe, touched, false, BrainTaskProfile::NormalV1);
        Need(result.status == BrainTaskResult::Unsupported && result.reason == "Unsupported serialized Task flags" &&
                 touched == 0, "Serialized probe-entered Task flag was admitted without the probe profile");
        result = Refusal(savedProbe, touched);
        Need(result.status == BrainTaskResult::Advanced && result.probes == 0,
             "Probe profile refused saved probe-entered Task state");
        ++passed;

        // Profile off: probe outputs refuse exactly as before; normal graphs behave identically.
        result = Refusal(probeGraph, touched, false, BrainTaskProfile::NormalV1);
        Need(result.status == BrainTaskResult::Unsupported &&
                 result.reason == "Task runtime supports only normal, unit-decay outputs" && touched == 0,
             "NormalV1 admitted probe outputs");
        TestHost offHost, onHost; PrepareOwner(offHost); PrepareOwner(onHost); Random offRandom, onRandom;
        BrainTaskRuntime off(graph, offHost, offRandom, 1), on(graph, onHost, onRandom, 1, kProbe);
        for (const std::int32_t now : {100, 150, 151, 200}) {
            const auto a = off.Tick(now), b = on.Tick(now);
            Need(a.status == b.status && a.transitions == b.transitions && b.probes == 0,
                 "Probe profile changed a graph without probe outputs");
        }
        Need(offHost.events.size() == onHost.events.size() && Trace(offHost.events) == Trace(onHost.events) &&
                 offHost.effects.size() == onHost.effects.size(),
             "Probe profile changed the normal lifecycle trace");
        ++passed;

        // Reentry and exceptions from a probe entry poison the instance; NotifyUse stays allowed.
        for (const std::string attack : {"probe", "probethrow"}) {
            TestHost hostile; PrepareOwner(hostile); Random hostileRandom;
            BrainTaskRuntime target(probeGraph, hostile, hostileRandom, 1, kProbe);
            hostile.attackRuntime = &target; hostile.attack = attack;
            result = target.Tick(100);
            Need(result.status == BrainTaskResult::Unsupported && target.State().failed && hostile.attacked &&
                     Trace(hostile.events) == "E1 E10 X1 E11",
                 "Probe entry reentry/exception did not stop the network");
            const auto eventCount = hostile.events.size();
            Need(target.Tick(101).reason == result.reason && target.Reset(101).reason == result.reason &&
                     hostile.events.size() == eventCount,
                 "Poisoned probe runtime continued callbacks");
        }
        TestHost useHost; PrepareOwner(useHost); useHost.attack = "probeuse"; Random useRandom;
        BrainTaskRuntime useProbe(probeGraph, useHost, useRandom, 1, kProbe);
        Need(useProbe.Tick(100).probes == 4 && useProbe.State().use.Matches("from_probe"),
             "NotifyUse from a probe entry was refused");
        ++passed;
        std::cout << "BRAI Task lifecycle self-tests: " << passed << " passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "BRAI Task test failed after " << passed << ": " << error.what() << '\n'; return 1;
    }
}

int ReplayBrainTaskFixture(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open private original BRAI fixture");
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto graph = DecodeBrainGraph(bytes);
    std::size_t usedNode = BrainRecord::None(), variableNode = BrainRecord::None();
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].type == 3) usedNode = i;
        if (graph.nodes[i].type == 220) variableNode = i;
    }
    if (graph.nodes.size() == 3 && usedNode == BrainRecord::None()) {
        TestHost host; PrepareOwner(host); host.near[1] = false; Random random;
        BrainTaskRuntime runtime(graph, host, random, 1);
        Need(runtime.Tick(100).transitions == 1 && runtime.Tick(600).status == BrainTaskResult::Idle &&
                 runtime.Tick(601).transitions == 1 && host.effects.empty(),
             "Original cyclic Wait fixture did not re-enter its timer");
        host.near[1] = true;
        Need(runtime.Tick(1101).status == BrainTaskResult::Idle && runtime.Tick(1102).transitions == 2 &&
                 host.effects.size() == 1 && host.effects[0].type == 68,
             "Original Wait/conditional action fixture trace differed");
        std::cout << "Original graph replay: cyclic Wait500, external near=false/true, one host UseTarget; PASS\n";
    } else if (graph.nodes.size() == 6 && usedNode != BrainRecord::None() && variableNode != BrainRecord::None()) {
        const auto variable = Property(graph, variableNode, "mVar");
        TestHost initial; PrepareOwner(initial); initial.variables[variable] = 1; initial.animation[1] = false;
        Random initialRandom; BrainTaskRuntime startup(graph, initial, initialRandom, 1);
        Need(startup.Tick(100).transitions == 1 && startup.Tick(150).status == BrainTaskResult::Idle &&
                 startup.Tick(151).transitions == 1 && initial.effects.size() == 1 && initial.effects[0].type == 51,
             "Original Wait50 startup/host animation trace differed");
        TestHost keyed; PrepareOwner(keyed); keyed.variables[variable] = 0; keyed.animation[1] = false;
        Random keyedRandom; BrainTaskRuntime used(graph, keyed, keyedRandom, 1);
        used.Tick(100); used.NotifyUse(Property(graph, usedNode, "mUseKey"));
        Need(used.Tick(120).transitions == 2 && !used.State().use.pending && keyed.variables[variable] == 1 &&
                 keyed.effects.size() == 2 && keyed.effects[0].type == 220 && keyed.effects[1].type == 68,
             "Original keyed Use/host variable/UseTarget trace differed");
        std::cout << "Original graph replay: Wait50 startup and keyed Use, TestHost variable/actions; PASS\n";
    } else throw std::runtime_error("Fixture shape is outside the declared replay profiles");
    std::cout << "TestHost actions/external truth only; not original runtime observation or campaign compatibility.\n";
    return 0;
}

namespace {
// Declared probe replay TestHost: every task is an opaque recorded action and
// every condition is scripted external truth. Roots are OnSpawn/OnUsed only.
// It does not implement any original task or condition behavior.
class ProbeReplayHost : public BrainTaskHost {
  public:
    std::vector<Event> events;
    std::set<unsigned> truePrimaries;
    static bool HasPrimary(const BrainGraph &graph, std::size_t node) {
        const auto &tokens = graph.records[graph.nodes[node].record].tokens;
        const auto own = '^' + std::to_string(graph.nodes[node].type);
        for (std::size_t i = 5; i + 1 < tokens.size(); i += 2)
            if (!tokens[i].empty() && tokens[i][0] == '^' && tokens[i] != own) return true;
        return false;
    }
    BrainTaskTraits Describe(const BrainGraph &graph, std::size_t node) override {
        BrainTaskTraits traits;
        const auto type = graph.nodes[node].type;
        traits.kind = type == 1 || type == 3 ? BrainTaskTraits::Root : BrainTaskTraits::Normal;
        traits.normalActivationKnown = true;
        traits.hasPrimary = HasPrimary(graph, node);
        traits.forceOnceOnInitialize = type == 1;
        traits.probeLeafEntryKnown = traits.kind == BrainTaskTraits::Normal;
        return traits;
    }
    bool SupportsCondition(const BrainGraph &, const BrainTaskCondition &) override { return true; }
    BrainTaskHook Task(const BrainGraph &graph, std::size_t node, BrainTaskPhase phase,
                       const BrainTaskContext &ctx) override {
        events.push_back({false, phase, graph.nodes[node].id, BrainRecord::None(), ctx.activeTaskStart,
                          ctx.activeNode == BrainRecord::None() ? 0 : graph.nodes[ctx.activeNode].id,
                          ctx.use->pending, ctx.taskFlags});
        if (phase == BrainTaskPhase::Exit && graph.nodes[node].type == 3 && !(ctx.taskFlags & 1u))
            ctx.use->Consume(Property(graph, node, "mUseKey"));
        return BrainTaskHook::Applied;
    }
    BrainTaskHook Condition(const BrainGraph &, const BrainTaskCondition &, BrainTaskPhase,
                            const BrainTaskContext &) override { return BrainTaskHook::Applied; }
    BrainConditionValue Evaluate(const BrainGraph &graph, const BrainTaskCondition &ref,
                                 const BrainTaskContext &ctx) override {
        if (!ref.IsPrimary()) return BrainConditionValue::False;
        const auto type = graph.nodes[ref.node].type;
        if (type == 1) return BrainConditionValue::True;
        if (type == 3) return ctx.use->Matches(Property(graph, ref.node, "mUseKey")) ?
                              BrainConditionValue::True : BrainConditionValue::False;
        return truePrimaries.count(graph.nodes[ref.node].id) ? BrainConditionValue::True : BrainConditionValue::False;
    }
    BrainConditionValue RootReady(const BrainGraph &graph, std::size_t node, const BrainTaskContext &ctx) override {
        BrainTaskCondition ref; ref.node = node;
        return Evaluate(graph, ref, ctx);
    }
};
bool Shape(const BrainGraph &graph, unsigned id, unsigned type) {
    for (const auto &node : graph.nodes) if (node.id == id) return node.type == type;
    return false;
}
void ProbeEntries(const ProbeReplayHost &host, std::size_t start, const std::set<unsigned> &ids,
                  unsigned active, std::int32_t begin) {
    std::size_t seen = 0;
    for (std::size_t i = start; i < host.events.size(); ++i) {
        const auto &event = host.events[i];
        if (!ids.count(event.node) || event.phase == BrainTaskPhase::Initialize) continue;
        Need(event.phase == BrainTaskPhase::Enter && event.active == active && event.start == begin &&
                 (event.flags & 0x20u) && !(event.flags & 0x10u),
             "Original probe destination was exited or entered as the active task");
        ++seen;
    }
    Need(seen == ids.size(), "Original probe destination was not entered exactly once");
}
} // namespace

// Declared ProbeLeafPassV1 replays of private original trainyard BRAI resources.
int ReplayBrainProbeFixture(const std::string &profile, const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open private original BRAI fixture");
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto graph = DecodeBrainGraph(bytes);
    ProbeReplayHost host; Random random;
    BrainTaskRuntime runtime(graph, host, random, 1, BrainTaskProfile::ProbeLeafPassV1);
    if (profile == "greeter") {
        Need(graph.nodes.size() == 73 && Shape(graph, 35, 59) && Shape(graph, 10, 243) && Shape(graph, 3, 3) &&
                 Property(graph, Index(graph, 3), "mUseKey") == "train_stop",
             "Fixture is not the declared train_greeter shape");
        auto result = runtime.Tick(100);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 3 && result.probes == 4 &&
                 Trace(host.events) == "E1 E3 E4 E5 E2 X1 E7 E61 E68 E21 X2 E24 X24 E73",
             "Original greeter spawn/probe trace differed");
        ProbeEntries(host, 0, {7, 61, 68, 21}, 2, 100);
        host.truePrimaries.insert(29); // Declared external truth: VO bank streamed in.
        runtime.NotifyUse("train_stop");
        auto before = host.events.size();
        result = runtime.Tick(200);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 3 && result.probes == 4 &&
                 Trace(host.events, before) == "X73 E29 X3 X29 E35 E6 E33 E72 X35 E10 E58" &&
                 runtime.State().activeNode == Index(graph, 10) && runtime.State().activeTaskStart == 200,
             "Original greeter node 35 probe/normal trace differed");
        ProbeEntries(host, before, {6, 33, 72}, 35, 200);
        ProbeEntries(host, before, {58}, 10, 200);
        before = host.events.size();
        Need(runtime.Tick(300).status == BrainTaskResult::Idle && host.events.size() == before,
             "Original greeter probes fired again without source re-entry");
        std::cout << "Original greeter replay: spawn probes 7/61/68/21; train_stop -> 29 -> 35 probes 6/33/72, "
                     "normal 35 -> 10 and probe 58 in one Tick; PASS\n";
    } else if (profile == "interrogator") {
        Need(graph.nodes.size() == 47 && Shape(graph, 25, 51) && Shape(graph, 17, 33) && Shape(graph, 12, 3) &&
                 Property(graph, Index(graph, 12), "mUseKey") == "player_leaves_train",
             "Fixture is not the declared nazi_interragator shape");
        auto result = runtime.Tick(100);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 4 && result.probes == 12 &&
                 Trace(host.events) == "E1 E12 E11 X1 X11 E20 X20 E2 E3 E23 E24 E29 E30 E31 E32 E44 E45 "
                                       "X2 E38 E39 E36 E37",
             "Original interrogator spawn/probe trace differed");
        ProbeEntries(host, 0, {3, 23, 24, 29, 30, 31, 32, 44, 45}, 2, 100);
        ProbeEntries(host, 0, {39, 36, 37}, 38, 100);
        runtime.NotifyUse("player_leaves_train");
        auto before = host.events.size();
        result = runtime.Tick(200);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 4 && result.probes == 6 &&
                 Trace(host.events, before) == "X38 E43 X12 E42 E41 E40 X43 E21 X21 E22 X22 E25 E26 E33 E35" &&
                 runtime.State().activeNode == Index(graph, 25),
             "Original interrogator node 25 entry trace differed");
        ProbeEntries(host, before, {26, 33, 35}, 25, 200);
        before = host.events.size();
        Need(runtime.Tick(250).status == BrainTaskResult::Idle && host.events.size() == before,
             "Original interrogator node 25 probes fired twice");
        host.truePrimaries.insert(25); // Declared external truth: AnimFinished.
        result = runtime.Tick(300);
        Need(result.status == BrainTaskResult::Advanced && result.transitions == 3 && result.probes == 4 &&
                 Trace(host.events, before) == "X25 E17 E16 E46 E47 X17 E6 E10 X6 E7" &&
                 runtime.State().activeNode == Index(graph, 7) && runtime.State().activeTaskStart == 300,
             "Original interrogator node 17 probe/normal trace differed");
        ProbeEntries(host, before, {16, 46, 47}, 17, 300);
        ProbeEntries(host, before, {10}, 6, 300);
        std::cout << "Original interrogator replay: spawn probes from 2 and 38; player_leaves_train -> 43 -> 25 "
                     "probes 26/33/35; AnimFinished -> 17 probes, 6 probe 10, normal to 7; PASS\n";
    } else throw std::runtime_error("Unknown declared probe replay profile");
    std::cout << "ProbeLeafPassV1 with a TestHost: opaque recorded actions and scripted external truth only; "
                 "not original runtime observation, AI, animation or campaign compatibility.\n";
    return 0;
}
} // namespace wolf2009

#ifdef WOLF2009_BRAIN_TASK_TEST_MAIN
int main(int argc, char **argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--self-test")) return wolf2009::RunBrainTaskTests();
        if (argc == 3 && std::string(argv[1]) == "--fixture") return wolf2009::ReplayBrainTaskFixture(argv[2]);
        if (argc == 4 && std::string(argv[1]) == "--probe-fixture")
            return wolf2009::ReplayBrainProbeFixture(argv[2], argv[3]);
        std::cerr << "Usage: wolf2009-brain-task --self-test | --fixture <private original BRAI> |\n"
                     "       --probe-fixture greeter|interrogator <private original BRAI>\n"; return 2;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif

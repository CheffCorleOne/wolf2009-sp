// SPDX-License-Identifier: GPL-3.0-or-later
// TestHost supplies declared actions/external truth. It is not campaign gameplay.
#include "BrainTask.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
std::string Wire(std::string text) { for (auto &c : text) if (c == '|') c = 0; return text; }
std::string Output(unsigned target, const std::string &conditions = "\t\t-1|\n") {
    std::string result = "\t";
    for (unsigned i = 0; i < 99; ++i)
        result += (i == 0 ? "16" : i == 1 ? "1" : i == 2 ? std::to_string(target) :
                   i == 98 ? "1.000000" : "0") + std::string("|");
    return result + "\n" + conditions;
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
    std::int32_t start; unsigned active; bool used;
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
                          ctx.use->pending});
    }
    BrainTaskHook Task(const BrainGraph &graph, std::size_t node, BrainTaskPhase phase,
                       const BrainTaskContext &ctx) override {
        Record(graph, node, false, BrainRecord::None(), phase, ctx);
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
} // namespace wolf2009

#ifdef WOLF2009_BRAIN_TASK_TEST_MAIN
int main(int argc, char **argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--self-test")) return wolf2009::RunBrainTaskTests();
        if (argc == 3 && std::string(argv[1]) == "--fixture") return wolf2009::ReplayBrainTaskFixture(argv[2]);
        std::cerr << "Usage: wolf2009-brain-task --self-test | --fixture <private original BRAI>\n"; return 2;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
// SourceAIHuntV1 portable checks (docs/AI-COMBAT.md §7.1). Synthetic always:
// authored graphs and dictionaries with the same key layout as the trainyard
// data. With --source-base the owner's read-only SP/base is used: the
// trainyard BRAI and ENTS resources (maps/game/trainyard/trainyard.mpk) and the
// editor .def text of assets.pk4. No game process, engine or emulator.
#include "Archive.h"
#include "BrainActions.h"
#include "BrainTask.h"
#include "Entities.h"
#include "SourceAICombat.h"
#include "SourceDefinitions.h"
#include "ZipArchive.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace wolf2009;
using namespace wolf2009::aicombat;

namespace {
unsigned checks = 0;
void Need(bool value, const std::string &message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

// ---------------------------------------------------------------------------
// Authored BRAI text (same helpers as BrainActionsTest).
std::string Wire(std::string text) { for (auto &c : text) if (c == '|') c = 0; return text; }
std::string Output(unsigned flags, unsigned target, const std::string &conditions = "") {
    std::string result = "\t";
    for (unsigned i = 0; i < 99; ++i)
        result += (i == 0 ? std::to_string(flags) : i == 1 ? "1" : i == 2 ? std::to_string(target) :
                   i == 98 ? "1.000000" : "0") + std::string("|");
    return result + "\n" + conditions;
}
std::string Node(unsigned type, unsigned id, unsigned flags, const std::string &properties = "") {
    return std::to_string(type) + '|' + std::to_string(id) + "|0|0|0|^" + std::to_string(type) + '|' +
           std::to_string(flags) + '|' + properties + '\n';
}
BrainGraph Decode(const std::string &body) {
    const auto text = Wire("h1023|32|32|32|0|\n" + body);
    return DecodeBrainGraph({text.begin(), text.end()});
}
std::size_t Index(const BrainGraph &graph, unsigned id) {
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) if (graph.nodes[i].id == id) return i;
    throw std::runtime_error("Missing node " + std::to_string(id));
}
std::string Property(const BrainGraph &graph, std::size_t node, const std::string &key) {
    const auto args = ReadBrainNodeArguments(graph, node);
    const auto *value = args.Find(key);
    return value ? *value : std::string();
}

// ---------------------------------------------------------------------------
// Host fakes. The world is declared, not simulated.
struct FakeWorld : CombatTaskHost {
    std::string owner;                 // what "Me" resolves to
    std::set<std::string> ais;         // named idAI entities
    std::vector<std::pair<std::string, HuntCall> > hunts; // resolved hunter + call
    std::vector<std::string> log;
    std::map<std::string, bool> ignoreEnemies, noTarget;
    std::string Name(const BrainEntityReference &ref) const {
        switch (ref.mode) {
        case BrainEntityMode::Me: return owner;
        case BrainEntityMode::Player: return "player1";
        case BrainEntityMode::Named: return ref.name;
        case BrainEntityMode::None: return "";
        }
        return "";
    }
    SubjectClass Classify(const BrainEntityReference &ref) override {
        const auto name = Name(ref);
        if (name.empty()) return SubjectClass::Missing;
        if (name == "player1") return SubjectClass::Actor;
        return ais.count(name) ? SubjectClass::AI : SubjectClass::Entity;
    }
    void StopScriptedMove(const BrainEntityReference &hunter) override { log.push_back("stop " + Name(hunter)); }
    void Hunt(const HuntCall &call) override {
        hunts.push_back(std::make_pair(Name(call.hunter), call));
        log.push_back("hunt " + Name(call.hunter) + " " + Name(call.prey));
    }
    void ClearEnemy(const BrainEntityReference &ai) override { log.push_back("clear " + Name(ai)); }
    void SetIgnoreEnemies(const BrainEntityReference &ai, bool value) override { ignoreEnemies[Name(ai)] = value; }
    void SetNoTarget(const BrainEntityReference &entity, bool value) override { noTarget[Name(entity)] = value; }
};

// ProbeLeafPassV1 replay host: combat tasks run through SourceAIHuntV1, every
// other task is an opaque recorded action, other primaries are scripted truth.
class ReplayHost : public BrainTaskHost {
  public:
    explicit ReplayHost(FakeWorld &world) : world_(world) {}
    std::vector<std::string> trace;
    std::set<unsigned> truePrimaries;
    std::vector<CombatTaskReceipt> receipts;
    std::size_t enemyDeadEvaluations = 0;
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
        const auto &n = graph.nodes[node];
        if (phase == BrainTaskPhase::Enter) trace.push_back(std::string(ctx.taskFlags & 0x20u ? "P" : "E") + std::to_string(n.id));
        if (phase == BrainTaskPhase::Exit) {
            trace.push_back("X" + std::to_string(n.id));
            if (n.type == 3 && !(ctx.taskFlags & 1u)) ctx.use->Consume(Property(graph, node, "mUseKey"));
        }
        if (phase != BrainTaskPhase::Enter) return BrainTaskHook::Applied;
        std::string why;
        if (IsBrainCombatTaskType(n.type)) {
            BrainCombatTaskSpec spec;
            if (!DecodeBrainCombatTaskV1(graph, node, spec, why)) throw std::runtime_error("combat decode: " + why);
            receipts.push_back(EnterCombatTaskV1(spec, world_));
        } else if (n.type == 102) {
            BrainActionSpec spec;
            if (!DecodeBrainActionV1(graph, node, spec, why)) throw std::runtime_error("NoTargetSet decode: " + why);
            receipts.push_back(EnterNoTargetSetV1(spec, world_));
        }
        return BrainTaskHook::Applied;
    }
    BrainTaskHook Condition(const BrainGraph &, const BrainTaskCondition &, BrainTaskPhase,
                            const BrainTaskContext &) override { return BrainTaskHook::Applied; }
    BrainConditionValue Evaluate(const BrainGraph &graph, const BrainTaskCondition &ref,
                                 const BrainTaskContext &ctx) override {
        if (!ref.IsPrimary()) return BrainConditionValue::False;
        const auto type = graph.nodes[ref.node].type;
        if (type == 1) return BrainConditionValue::True;
        if (type == 3) return ctx.use->Matches(Property(graph, ref.node, "mUseKey")) ? BrainConditionValue::True
                                                                                      : BrainConditionValue::False;
        if (type == 43) {
            // Hunt's primary EnemyDead: the declared player is alive.
            BrainCombatConditionSpec spec;
            std::string why;
            if (!DecodeBrainCombatPrimaryV1(graph, ref.node, spec, why)) throw std::runtime_error(why);
            EnemyDeadInputV1 in;
            in.hunter = world_.Classify(spec.subject);
            in.hasEnemy = true;
            in.enemyHealth = 100;
            ++enemyDeadEvaluations;
            return EvaluateEnemyDeadV1(spec, in);
        }
        return truePrimaries.count(graph.nodes[ref.node].id) ? BrainConditionValue::True : BrainConditionValue::False;
    }
    BrainConditionValue RootReady(const BrainGraph &graph, std::size_t node, const BrainTaskContext &ctx) override {
        BrainTaskCondition ref;
        ref.node = node;
        return Evaluate(graph, ref, ctx);
    }
  private:
    FakeWorld &world_;
};
struct Random : BrainFlowRandom {
    std::uint32_t Next15() override { throw std::runtime_error("no weighted choice expected"); }
};
std::string Join(const std::vector<std::string> &items, std::size_t from = 0) {
    std::string out;
    for (std::size_t i = from; i < items.size(); ++i) out += (out.empty() ? "" : " ") + items[i];
    return out;
}

// Interrogator-shaped Hunt checks shared by the authored and original graphs.
void CheckInterrogatorHunts(const FakeWorld &world, const std::string &second, const std::string &label) {
    Need(world.hunts.size() == 2, label + ": exactly one Hunt per interrogator");
    const auto &one = world.hunts[0], &two = world.hunts[1];
    Need(one.first == "nazi_interragator_1" && one.second.prey.mode == BrainEntityMode::Player && !one.second.locked &&
             one.second.updatePosition && one.second.node == 6, label + ": node 6 Hunt Me -> Player");
    Need(two.first == second && two.second.prey.mode == BrainEntityMode::Player && !two.second.locked &&
             two.second.updatePosition && two.second.node == 10, label + ": node 10 Hunt interrogator 2 -> Player");
    Need(std::find(world.log.begin(), world.log.end(), "stop nazi_interragator_1") != world.log.end() &&
             std::find(world.log.begin(), world.log.end(), "stop " + second) != world.log.end(),
         label + ": each Hunt stops the hunter's scripted move first");
}

void BrainHandlers() {
    // Authored interrogator tail: OnSpawn -> 17 (P 16) -> 6 Hunt (P 10 Hunt) -> 7 Say -> 8.
    const auto graph = Decode(
        Node(1, 1, 1, "^74|1024|") + Output(16, 17, "\t\t-1|\n") +
        Node(33, 17, 16, "mSetToTrue|false|") + Output(32, 16) + Output(16, 6) +
        Node(33, 16, 32, "mActor|nazi_interragator_2|mSetToTrue|false|") +
        Node(43, 6, 16, "^42|1024|mPrey|Player|mUpdatePosition|true|") + Output(32, 10) + Output(16, 7) +
        Node(43, 10, 32, "^42|1024|mHunter|nazi_interragator_2|mPrey|Player|mUpdatePosition|true|") +
        Node(12, 7, 16, "^10|1024|mLine|x|") + Output(16, 8, "\t\t-1|\n") + Node(12, 8, 0, "^10|1024|mLine|y|"));
    FakeWorld world;
    world.owner = "nazi_interragator_1";
    world.ais = {"nazi_interragator_1", "nazi_interragator_2"};
    ReplayHost host(world);
    Random random;
    BrainTaskRuntime runtime(graph, host, random, 1, BrainTaskProfile::ProbeLeafPassV1);
    const auto result = runtime.Tick(100);
    Need(result.status == BrainTaskResult::Advanced && Join(host.trace) == "E1 E17 X1 P16 X17 E6 P10 X6 E7" &&
             graph.nodes[runtime.State().activeNode].id == 7,
         "authored tail: one Tick enters 6, probes 10 and enters 7 (" + Join(host.trace) + ")");
    CheckInterrogatorHunts(world, "nazi_interragator_2", "authored tail");
    Need(host.receipts.size() == 2 && host.receipts[0].outcome == CombatTaskReceipt::Applied &&
             host.receipts[0].completesInEnterTick && !host.receipts[0].hasExit, "Hunt receipts: applied, no Exit");
    Need(runtime.Tick(200).status == BrainTaskResult::Idle && world.hunts.size() == 2, "Hunt is not repeated");

    // A hunter that is not an idAI: Enter returns without stopping or hunting.
    BrainCombatTaskSpec hunt;
    std::string why;
    Need(DecodeBrainCombatTaskV1(graph, Index(graph, 10), hunt, why), "decode node 10");
    FakeWorld empty;
    const auto noop = EnterCombatTaskV1(hunt, empty);
    Need(noop.outcome == CombatTaskReceipt::NoOp && empty.log.empty(), "non-AI hunter is a no-op");

    // IgnoreEnemiesSet: true clears the enemy first; false only writes the flag.
    const auto ignore = Decode(Node(49, 1, 0, "") + Node(49, 2, 0, "mIgnore|false|mActor|contact_1|") +
                               Node(49, 3, 0, "mActor|door_1|") + Node(102, 4, 0, "mNoTarget|fasle|mActor|Player|") +
                               Node(102, 5, 0, "mActor|None|"));
    FakeWorld w;
    w.owner = "greeter";
    w.ais = {"greeter", "contact_1"};
    BrainCombatTaskSpec spec;
    Need(DecodeBrainCombatTaskV1(ignore, 0, spec, why) && EnterCombatTaskV1(spec, w).outcome == CombatTaskReceipt::Applied &&
             w.log == std::vector<std::string>{"clear greeter"} && w.ignoreEnemies["greeter"],
         "IgnoreEnemiesSet default: ClearEnemy then ignoreEnemies=1");
    Need(DecodeBrainCombatTaskV1(ignore, 1, spec, why) && EnterCombatTaskV1(spec, w).outcome == CombatTaskReceipt::Applied &&
             w.log.size() == 1 && !w.ignoreEnemies["contact_1"], "IgnoreEnemiesSet false: no ClearEnemy");
    Need(DecodeBrainCombatTaskV1(ignore, 2, spec, why) && EnterCombatTaskV1(spec, w).outcome == CombatTaskReceipt::NoOp &&
             !w.ignoreEnemies.count("door_1"), "IgnoreEnemiesSet on a non-AI is a no-op");
    BrainActionSpec action;
    Need(DecodeBrainActionV1(ignore, 3, action, why) && EnterNoTargetSetV1(action, w).outcome == CombatTaskReceipt::Applied &&
             w.noTarget.count("player1") && !w.noTarget["player1"], "NoTargetSet \"fasle\" writes false");
    Need(DecodeBrainActionV1(ignore, 4, action, why) && EnterNoTargetSetV1(action, w).outcome == CombatTaskReceipt::NoOp,
         "NoTargetSet without an entity writes nothing");
    Need(!ParseBrainBoolTextV1("fasle") && ParseBrainBoolTextV1("True") && !ParseBrainBoolTextV1("1") &&
             !ParseBrainBoolTextV1(""), "boolean rule: Icmp(text, \"true\") == 0");
}

void Conditions() {
    BrainCombatConditionSpec dead;
    dead.kind = BrainCombatConditionKind::EnemyDead;
    EnemyDeadInputV1 in;
    in.hunter = SubjectClass::AI;
    in.hasEnemy = true;
    in.enemyHealth = 10;
    Need(EvaluateEnemyDeadV1(dead, in) == BrainConditionValue::False, "EnemyDead: live player");
    in.enemyHealth = 0;
    Need(EvaluateEnemyDeadV1(dead, in) == BrainConditionValue::True, "EnemyDead: dead player counts at once");
    in.enemyIsAI = true;
    in.enemyDeathTime = 1000;
    in.now = 3000;
    Need(EvaluateEnemyDeadV1(dead, in) == BrainConditionValue::False, "EnemyDead: AI enemy dead exactly 2000 ms");
    in.now = 3001;
    Need(EvaluateEnemyDeadV1(dead, in) == BrainConditionValue::True, "EnemyDead: AI enemy dead longer than debounce");
    in.hunter = SubjectClass::Actor;
    Need(EvaluateEnemyDeadV1(dead, in) == BrainConditionValue::False, "EnemyDead: non-AI hunter is false");
    dead.invert = true;
    Need(EvaluateEnemyDeadV1(dead, in) == BrainConditionValue::True, "EnemyDead: NOT applies to a non-AI hunter");
    in.hunter = SubjectClass::AI;
    in.hasEnemy = false;
    Need(EvaluateEnemyDeadV1(dead, in) == BrainConditionValue::True, "EnemyDead NOT: no enemy is not dead");

    BrainCombatConditionSpec aware;
    aware.kind = BrainCombatConditionKind::Aware;
    Need(EvaluateAwareV1(aware) == BrainConditionValue::False, "Aware is constant false");
    aware.invert = true;
    Need(EvaluateAwareV1(aware) == BrainConditionValue::True, "Aware NOT is constant true");
    Need(EvaluateAwareV1(dead) == BrainConditionValue::Unsupported, "Aware evaluator refuses other kinds");

    // HasTakenPain over the Think pain shift (+0x5e8 -> +0x5ec, clear).
    BrainCombatConditionSpec pain;
    pain.kind = BrainCombatConditionKind::HasTakenPain;
    float accumulator = 0, lastFrame = 0;
    std::vector<bool> seen;
    const float damage[] = {0, 6, 0, 0, 12.5f, 0};
    for (float d : damage) {
        PainInputV1 p;
        p.actor = SubjectClass::AI;
        p.health = 50;
        p.lastFramePain = lastFrame;
        seen.push_back(EvaluateHasTakenPainV1(pain, p) == BrainConditionValue::True);
        accumulator += d;                        // damage during this frame
        lastFrame = accumulator; accumulator = 0; // Think step 4
    }
    Need(seen == std::vector<bool>({false, false, true, false, false, true}), "HasTakenPain only in the frame after damage");
    PainInputV1 p;
    p.actor = SubjectClass::Actor;
    p.health = 50;
    p.lastFramePain = 6;
    pain.minThreshold = 6;
    pain.maxThreshold = 6;
    Need(EvaluateHasTakenPainV1(pain, p) == BrainConditionValue::True, "pain thresholds are inclusive");
    pain.minThreshold = 7;
    Need(EvaluateHasTakenPainV1(pain, p) == BrainConditionValue::False, "pain below the minimum");
    pain.minThreshold = -1;
    pain.maxThreshold = 5;
    Need(EvaluateHasTakenPainV1(pain, p) == BrainConditionValue::False, "pain above the maximum");
    p.lastFramePain = 0.5f;
    pain.maxThreshold = -1;
    Need(EvaluateHasTakenPainV1(pain, p) == BrainConditionValue::True, "non-zero damage truncating to 0 is inside -1/-1");
    p.health = 0;
    pain.invert = true;
    Need(EvaluateHasTakenPainV1(pain, p) == BrainConditionValue::False, "dead actor: plain false, NOT not applied");
    p.health = 1;
    p.actor = SubjectClass::Entity;
    Need(EvaluateHasTakenPainV1(pain, p) == BrainConditionValue::False, "non-actor: plain false");

    // CombatJustStarted: rising edge, once per frame stamp.
    BrainCombatConditionSpec combat;
    combat.kind = BrainCombatConditionKind::CombatJustStarted;
    combat.timeChecked = 86565;
    auto state = CombatJustStartedStateV1::FromSpec(combat);
    CombatJustStartedInputV1 c;
    c.actor = SubjectClass::AI;
    c.health = 100;
    c.frameStamp = 1;
    auto r = EvaluateCombatJustStartedV1(combat, state, c);
    Need(r.value == BrainConditionValue::Unsupported && state.timeChecked == 86565 && !r.reason.empty(),
         "CombatJustStarted without an enemy needs the Unknown timer and shifts nothing");
    c.policy = CombatTimerPolicy::InactiveStandInV1;
    std::vector<bool> edge;
    const bool enemy[] = {false, false, true, true, true, false, true};
    for (std::size_t i = 0; i < 7; ++i) {
        c.frameStamp = static_cast<std::int32_t>(10 + i);
        c.hasEnemy = enemy[i];
        r = EvaluateCombatJustStartedV1(combat, state, c);
        const bool first = r.value == BrainConditionValue::True;
        const auto again = EvaluateCombatJustStartedV1(combat, state, c); // same frame
        Need(again.value == r.value, "second evaluation in the same frame repeats the result");
        Need(enemy[i] || r.standIn == "InactiveStandInV1", "stand-in is named when used");
        edge.push_back(first);
    }
    Need(edge == std::vector<bool>({false, false, true, false, false, false, true}),
         "CombatJustStarted is true in exactly one frame after the enemy appears");
    c.health = 0;
    combat.invert = true;
    Need(EvaluateCombatJustStartedV1(combat, state, c).value == BrainConditionValue::False, "dead AI: plain false");

    const auto t = HuntTimersFor(300, true, false, 0);
    Need(t.timeEnemySet == 300 && t.timeUpdateEnemy == 5300 && t.updateWritten, "Hunt timers perceived +5000");
    Need(HuntTimersFor(300, false, false, 0).timeUpdateEnemy == 800, "Hunt timers not perceived +500");
    Need(!HuntTimersFor(300, false, true, 77).updateWritten && HuntTimersFor(300, false, true, 77).timeUpdateEnemy == 77,
         "ai+0x95e bit 0x1 keeps timeUpdateEnemy");
}

// The interrogator spawn keys (AI-COMBAT §2.2): ENTS values over the .def chain.
const std::map<std::string, std::string> &InterrogatorKeys() {
    static const std::map<std::string, std::string> keys = {
        {"disableCover", "1"}, {"tactical_object_bounce", "0"}, {"tactical_cover", "1"}, {"tactical_hide", "1"},
        {"tactical_relocate", "1"}, {"tactical_invis_object", "1"}, {"tactical_rush", "1"}, {"tactical_disarm", "1"},
        {"team", "1"}};
    return keys;
}
std::uint32_t ExpectedInterrogatorMask() {
    std::uint32_t mask = 0;
    for (auto t : {Tactical::Idle, Tactical::MoveFollow, Tactical::Cover, Tactical::Hide, Tactical::Hunt,
                   Tactical::Relocate, Tactical::KickTable, Tactical::CheckCorpse, Tactical::GotoVehicle,
                   Tactical::GotoTurret, Tactical::Wander, Tactical::Charge, Tactical::CheckInvisObject,
                   Tactical::CounterDefend, Tactical::MeleeReady})
        mask |= TacticalInfoFor(t).mask;
    return mask;
}
TacticalFilterInputV1 FreshEnemy(bool disableCover) {
    TacticalFilterInputV1 in;
    in.hasEnemy = true;
    in.movementEnabled = true;
    in.flagDisableCover = disableCover;
    in.enemySubRecord = true;
    in.enemySeenWithinDelay = Tri::True; // a freshly set enemy was just seen
    return in;
}
void CheckInterrogatorTactical(std::uint32_t mask, const TacticalFilterInputV1 &in, const std::string &label) {
    Need(mask == ExpectedInterrogatorMask(), label + ": available mask equals the AI-COMBAT §5.3 list");
    const auto filter = FilterTacticalV1(mask, in);
    const auto removed = [&](Tactical t) { return (filter.removed & TacticalInfoFor(t).mask) != 0; };
    Need(removed(Tactical::Idle) && removed(Tactical::Cover) && removed(Tactical::Hunt) &&
             removed(Tactical::CounterDefend) && removed(Tactical::CheckInvisObject) && removed(Tactical::Relocate) &&
             removed(Tactical::MeleeReady), label + ": IDLE/COVER/HUNT/COUNTER/INVIS/RELOCATE/MELEE drop out");
    bool coverByFlag = false;
    for (const auto &v : filter.verdicts)
        if (v.value == Tactical::Cover) coverByFlag = v.reason.find("combat.fl.disableCover") != std::string::npos;
    Need(coverByFlag, label + ": disableCover 1 fails the cover predicate");
    const auto undecided = SelectTacticalV1(filter, UnknownTacticalPolicy::Undecided);
    Need(!undecided.decided && undecided.blockedBy.find("ai+0x141c") != std::string::npos,
         label + ": selection is blocked by the unread HIDE input");
    const auto named = SelectTacticalV1(filter, UnknownTacticalPolicy::RefuseByNameV1);
    Need(named.decided && named.value == Tactical::NoCover && named.refused.size() == 8,
         label + ": RefuseByNameV1 selects NOCOVER with eight named refusals");
}

void Tactics() {
    Need(TacticalTableV1().size() == TacticalCount && TacticalPriorityV1().size() == 18, "tactical tables complete");
    for (std::size_t i = 0; i < TacticalCount; ++i) {
        const auto &info = TacticalTableV1()[i];
        Need(static_cast<std::size_t>(info.value) == i, "tactical table index equals value");
        Need(info.mask == 0 || info.mask == (1u << i), "mask bit = 1 << value");
    }
    Need(std::string(TacticalInfoFor(Tactical::NoCover).state) == "State_CombatNoCover" &&
             std::string(TacticalInfoFor(Tactical::None).state) == "State_Combat", "state names");
    Need(SourceAtoi(" 12x") == 12 && SourceAtoi("-3") == -3 && SourceAtoi("abc") == 0 && SourceAtoi("") == 0, "atoi");
    Need(BuildTacticalAvailableMaskV1(SpawnLookup()) ==
             (0x2u | 0x4 | 0x40 | 0x80 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x40000 | 0x80000),
         "default mask from key defaults");
    const auto lookup = [](const std::string &key) -> const std::string * {
        const auto &keys = InterrogatorKeys();
        const auto found = keys.find(key);
        return found == keys.end() ? nullptr : &found->second;
    };
    CheckInterrogatorTactical(BuildTacticalAvailableMaskV1(lookup), FreshEnemy(true), "authored interrogator");

    // HIDE wins at once; a closed HIDE removal lets the next unknown block.
    auto in = FreshEnemy(true);
    in.hideEntityAlive = Tri::True;
    auto filter = FilterTacticalV1(ExpectedInterrogatorMask(), in);
    Need(filter.hideImmediate && SelectTacticalV1(filter, UnknownTacticalPolicy::Undecided).value == Tactical::Hide,
         "HIDE wins at once");
    in.hideEntityAlive = Tri::False;
    filter = FilterTacticalV1(ExpectedInterrogatorMask(), in);
    Need(SelectTacticalV1(filter, UnknownTacticalPolicy::Undecided).blockedBy == "UnknownPredicate:AITACTICAL_CHARGE",
         "CHARGE predicate is unknown");
    // Without an enemy IDLE stays; RELOCATE with combat.relocate outranks it.
    TacticalFilterInputV1 calm;
    calm.movementEnabled = true;
    calm.relocate = true;
    filter = FilterTacticalV1(0x2u | 0x80u, calm);
    Need(SelectTacticalV1(filter, UnknownTacticalPolicy::Undecided).value == Tactical::Relocate, "RELOCATE outranks IDLE");
    filter = FilterTacticalV1(0x2u, calm);
    Need(SelectTacticalV1(filter, UnknownTacticalPolicy::Undecided).value == Tactical::Idle, "IDLE without an enemy");
    // COVER with every closed gate passing still needs the unread cover feature.
    auto cover = FreshEnemy(false);
    filter = FilterTacticalV1(0x8u, cover);
    Need(filter.unknown == 0x8u, "COVER waits on the cover feature");
    cover.coverFeature = Tri::True;
    filter = FilterTacticalV1(0x8u, cover);
    Need(SelectTacticalV1(filter, UnknownTacticalPolicy::Undecided).value == Tactical::Cover, "COVER kept");
    // HUNT after the enemy is lost for the delay.
    auto hunt = FreshEnemy(false);
    hunt.enemySeenWithinDelay = Tri::False;
    hunt.huntDelayElapsed = Tri::True;
    hunt.huntOrderAdmits = Tri::True;
    filter = FilterTacticalV1(0x40u, hunt);
    Need(SelectTacticalV1(filter, UnknownTacticalPolicy::Undecided).value == Tactical::Hunt, "HUNT kept");
    Need(HuntDelayMsV1(0, 1) == 10000 && HuntDelayMsV1(0, 0) == 15000 && HuntDelayMsV1(2, 1) == 20000, "hunt delay");
    Need(ApplyTacticalV1(Tactical::NoCover, Tactical::NoCover).changed == false &&
             ApplyTacticalV1(Tactical::None, Tactical::NoCover).postState == "State_CombatNoCover" &&
             ApplyTacticalV1(Tactical::NoCover, Tactical::None).postState == "State_Combat", "SetTactical");
    TacticalGateInputV1 gate;
    gate.animationStateBlocks = Tri::False;
    gate.delay = 1000;
    gate.now = 1500;
    gate.lastTacticalTime = 600;
    std::string why;
    Need(TacticalUpdateAllowedV1(gate, why) == Tri::False, "tactical delay window");
    gate.now = 1600;
    Need(TacticalUpdateAllowedV1(gate, why) == Tri::True, "tactical delay elapsed");
    gate.animationStateBlocks = Tri::Unknown;
    Need(TacticalUpdateAllowedV1(gate, why) == Tri::Unknown, "unread animation states stay unknown");
}

// ---------------------------------------------------------------------------
// Burst model
const std::map<std::string, std::string> &StatsMp40() {
    static const std::map<std::string, std::string> keys = {
        {"minShots", "3"}, {"maxShots", "8"}, {"shotWaitTime", "120"}, {"minBursts", "1"}, {"maxBursts", "2"},
        {"burstWaitTimeMin", "500"}, {"burstWaitTimeMax", "1000"}, {"clipSize", "32"}, {"attackRange", "0 2000"},
        {"weaponSpread", "3.1"}, {"minAimDot", "0.95"}};
    return keys;
}
struct BurstHost : BurstHostV1 {
    std::vector<std::int32_t> shots;
    std::int32_t now = 0;
    std::size_t chatter = 0, kicks = 0;
    bool attackSucceeds = true;
    bool AttackHitscan(const char *) override {
        if (attackSucceeds) shots.push_back(now);
        return attackSucceeds;
    }
    void AfterAttackDraws(CombatRandomV1 &random) override { random.Range(0, 0); random.Range(0, 0); kicks += 2; }
    void Chatter() override { ++chatter; }
};
BurstThinkInputV1 Ready(std::int32_t now) {
    BurstThinkInputV1 in;
    in.now = now;
    in.active = true;
    in.hold64c = Tri::False;
    in.checks.aimDot = 1;
    in.checks.hasEnemy = true;
    in.checks.enemyHealth = 100;
    in.checks.enemyDistance = 500;
    in.checks.targetLockedGate = Tri::True;
    in.checks.notInBlockedAction = Tri::True;
    in.checks.enemySeenRecently = Tri::True;
    in.checks.enemyKnownLongEnough = Tri::True;
    in.checks.finalCheckClear = Tri::True;
    return in;
}
// Replays the controller every frameMs and checks the §7.1 burst contract.
void ReplayBursts(const WeaponStatsV1 &stats, std::uint32_t seed, std::int32_t frameMs, const std::string &label) {
    SourceGameLcgV1 lcg(seed);
    BurstHost host;
    BurstStateV1 state;
    std::vector<std::int32_t> burstShots, burstWaits;
    // Per-frame and per-shot facts are counted and checked once each.
    std::size_t refusals = 0, early = 0, badWaits = 0, badShotGaps = 0, badBurstGaps = 0;
    std::string refusal;
    for (std::int32_t now = frameMs; now <= 120000; now += frameMs) {
        host.now = now;
        const std::int32_t waitTime = state.waitTime;
        const std::size_t before = host.shots.size();
        const auto result = ThinkBurstV1(state, stats, Ready(now), lcg, host);
        if (result.refused) { ++refusals; refusal = result.reason; }
        if (host.shots.size() != before && !(now > waitTime)) ++early;
        if (result.burstStarted) {
            burstShots.push_back(result.burstShots);
            burstWaits.push_back(result.burstWait);
        }
    }
    Need(refusals == 0, label + ": refused " + refusal);
    Need(early == 0, label + ": no shot before weapon.waitTime");
    Need(burstShots.size() > 50, label + ": enough bursts");
    Need(burstWaits[0] == 0, label + ": first-burst wait 0");
    for (std::size_t i = 1; i < burstWaits.size(); ++i)
        badWaits += burstWaits[i] >= stats.burstWaitTimeMin && burstWaits[i] <= stats.burstWaitTimeMax ? 0 : 1;
    Need(badWaits == 0, label + ": next-burst wait inside [burstWaitTimeMin, burstWaitTimeMax]");
    Need(*std::min_element(burstShots.begin(), burstShots.end()) == stats.minShots &&
             *std::max_element(burstShots.begin(), burstShots.end()) == stats.maxShots,
         label + ": burst sizes cover [minShots, maxShots]");
    // Walk shots back into bursts: gaps inside a burst are > shotWaitTime and
    // < shotWaitTime + 2 frames; gaps between bursts include the burst wait.
    std::size_t at = 0;
    for (std::size_t b = 0; b + 1 < burstShots.size() && at + burstShots[b] <= host.shots.size(); ++b) {
        for (std::int32_t k = 1; k < burstShots[b]; ++k) {
            const auto gap = host.shots[at + k] - host.shots[at + k - 1];
            badShotGaps += gap > stats.shotWaitTime && gap <= stats.shotWaitTime + frameMs ? 0 : 1;
        }
        at += burstShots[b];
        if (at < host.shots.size()) {
            const auto gap = host.shots[at] - host.shots[at - 1];
            const auto wait = burstWaits[b + 1];
            badBurstGaps += gap > stats.shotWaitTime + wait && gap <= stats.shotWaitTime + wait + frameMs ? 0 : 1;
        }
    }
    Need(at > host.shots.size() / 2, label + ": shots walked back into bursts");
    Need(badShotGaps == 0, label + ": shots inside a burst are shotWaitTime apart (one frame of quantization)");
    Need(badBurstGaps == 0, label + ": burst gap = shotWaitTime + burst wait");
    Need(host.chatter > 0 && host.chatter < burstShots.size() / 4, label + ": chatter about one burst in ten");
    std::cout << "BURST " << label << " seed=" << seed << " frame=" << frameMs << "ms bursts=" << burstShots.size()
              << " shots=" << host.shots.size() << " chatter=" << host.chatter << " draws=" << lcg.Draws() << "\n";
}

void Bursts() {
    WeaponStatsV1 stats;
    std::string why;
    const auto lookup = [](const std::string &key) -> const std::string * {
        const auto found = StatsMp40().find(key);
        return found == StatsMp40().end() ? nullptr : &found->second;
    };
    Need(ReadWeaponStatsV1(lookup, stats, why) && stats.minShots == 3 && stats.maxShots == 8 && stats.shotWaitTime == 120 &&
             stats.burstWaitTimeMin == 500 && stats.burstWaitTimeMax == 1000 && stats.attackRangeMax == 2000 &&
             stats.minAimDot > 0.94f && stats.minAimDot < 0.96f, "stats_mp40_ai shape: " + why);
    const auto partial = [](const std::string &key) -> const std::string * {
        if (key == "maxShots") return nullptr;
        const auto found = StatsMp40().find(key);
        return found == StatsMp40().end() ? nullptr : &found->second;
    };
    WeaponStatsV1 refused;
    Need(!ReadWeaponStatsV1(partial, refused, why) && why.find("maxShots") != std::string::npos,
         "absent maxShots has no recovered default");

    // The LCG contract.
    SourceGameLcgV1 lcg(12345);
    const std::uint32_t expected = 12345u * 0x343fdu + 0x269ec3u;
    const std::int32_t draw = lcg.Range(100, 300);
    Need(lcg.Seed() == expected && draw == 100 + static_cast<std::int32_t>((201u * (expected >> 17))) / 32768,
         "LCG step and Range mapping");
    std::int32_t low = 1000, high = -1;
    std::size_t tens = 0;
    for (int i = 0; i < 40000; ++i) {
        const auto v = lcg.Range(3, 8);
        low = std::min(low, v);
        high = std::max(high, v);
        tens += lcg.OneInTen() ? 1 : 0;
    }
    Need(low == 3 && high == 8 && tens > 3600 && tens < 4400, "Range bounds and OneInTen rate");

    for (std::uint32_t seed : {1u, 0x2468aceu, 0xdeadbeefu}) ReplayBursts(stats, seed, 16, "stats_mp40_ai");
    ReplayBursts(stats, 7, 1, "stats_mp40_ai 1ms");

    // Predicate false from aim: wait pushed to now + 100, no burst.
    BurstHost host;
    BurstStateV1 state;
    auto in = Ready(1000);
    in.checks.aimDot = 0.5f;
    auto result = ThinkBurstV1(state, stats, in, lcg, host);
    Need(!result.burstStarted && result.aimWaitPushed && state.waitTime == 1100, "low aim pushes the wait");
    in.checks.forcedFire = true;
    result = ThinkBurstV1(state, stats, in, lcg, host);
    Need(result.burstStarted && state.shots >= 3, "forced fire starts a burst despite the aim");
    // No shot while dead/disabled/ignoring: the checks fail.
    BurstStateV1 idle;
    for (int c = 0; c < 3; ++c) {
        auto blocked = Ready(2000);
        if (c == 0) blocked.checks.cvarDisableAttacks = true;
        if (c == 1) blocked.checks.enemyDead = true;
        if (c == 2) blocked.checks.hasEnemy = false; // ignoreEnemies leaves no enemy
        Need(!ThinkBurstV1(idle, stats, blocked, lcg, host).burstStarted && idle.shots == 0, "blocked predicate");
    }
    // The shot step does not test the enemy: shots left in a burst still fire
    // after the enemy dies unless an unread gate (+0x64c, +0x2108) stops them.
    // Recorded as a finding for AI-COMBAT §7.3, not forced either way.
    BurstStateV1 running;
    running.shots = 2;
    auto afterDeath = Ready(2500);
    afterDeath.checks.enemyDead = true;
    result = ThinkBurstV1(running, stats, afterDeath, lcg, host);
    Need(result.fired && running.shots == 1 && !result.burstStarted, "remaining burst shot fires after the enemy died");
    // Unknown inputs refuse before any state change.
    BurstStateV1 fresh;
    auto unknown = Ready(3000);
    unknown.checks.finalCheckClear = Tri::Unknown;
    result = ThinkBurstV1(fresh, stats, unknown, lcg, host);
    Need(result.refused && result.reason.find("0x201aea20") != std::string::npos && fresh.waitTime == 0, "unread final check");
    unknown = Ready(3000);
    unknown.hold64c = Tri::Unknown;
    Need(ThinkBurstV1(fresh, stats, unknown, lcg, host).refused, "unread +0x64c refuses");
    unknown = Ready(3000);
    unknown.continuousDef = true;
    Need(ThinkBurstV1(fresh, stats, unknown, lcg, host).refused, "def_continuous refuses");
    unknown = Ready(3000);
    unknown.checks.enemyDistance = 2000;
    Need(ThinkBurstV1(fresh, stats, unknown, lcg, host).refused, "distance on the attackRange bound is unknown");
    unknown.checks.enemyDistance = 2001;
    Need(!ThinkBurstV1(fresh, stats, unknown, lcg, host).burstStarted, "outside attackRange");
    // A projectile weapon refuses at the shot step, before any change.
    BurstStateV1 loaded;
    loaded.shots = 3;
    auto projectile = Ready(5000);
    projectile.projectileDef = true;
    Need(ThinkBurstV1(loaded, stats, projectile, lcg, host).refused && loaded.shots == 3, "projectile path refused");
    // A failed attack keeps the shot and its wait.
    host.attackSucceeds = false;
    auto failed = Ready(5000);
    result = ThinkBurstV1(loaded, stats, failed, lcg, host);
    Need(!result.fired && loaded.shots == 3 && loaded.waitTime == 0, "failed attack changes nothing");
    host.attackSucceeds = true;
    // Second attack name while the action requests it.
    failed.secondAttack = true;
    result = ThinkBurstV1(loaded, stats, failed, lcg, host);
    Need(result.fired && result.attack == "second" && loaded.shots == 2 && loaded.waitTime == 5120 &&
             loaded.lastShotTime == 5000 && loaded.shotCounter == 1, "shot bookkeeping");
    // maxShots 1: exactly one shot with 0-60 ms extra wait, no chatter roll.
    WeaponStatsV1 single = stats;
    single.minShots = 1;
    single.maxShots = 1;
    BurstStateV1 one;
    one.waitTime = 7000;
    const auto drawsBefore = lcg.Draws();
    result = ThinkBurstV1(one, single, Ready(6000), lcg, host);
    Need(result.burstStarted && one.shots == 1 && result.burstWait <= 60 && one.waitTime == 7000 + result.burstWait &&
             lcg.Draws() == drawsBefore + 1, "maxShots 1 path");
    // Last burst ended: bursts decrement and ai+0xa68 in [now+100, now+300].
    BurstStateV1 last;
    last.shots = 1;
    last.bursts = 0;
    auto end = Ready(9000);
    end.checks.aimDot = 0.5f;
    result = ThinkBurstV1(last, stats, end, lcg, host);
    Need(result.fired && result.burstEnded && last.bursts == 0 && last.postBurstTime >= 9100 && last.postBurstTime <= 9300,
         "burst end bookkeeping");
}

// ---------------------------------------------------------------------------
// Original data (read-only)
std::vector<unsigned char> ReadArchiveEntry(const std::string &path, const std::string &type, const std::string &suffix,
                                            std::string *found = nullptr) {
    std::ifstream stream(path, std::ios::binary);
    Need(static_cast<bool>(stream), "cannot open " + path);
    const auto archive = Archive::Scan(stream);
    std::vector<std::pair<std::size_t, std::size_t> > matches;
    for (std::size_t c = 0; c < archive.Chunks().size(); ++c) {
        if (archive.Chunks()[c].type != type) continue;
        for (std::size_t f = 0; f < archive.Chunks()[c].entries.size(); ++f) {
            const auto &name = archive.Chunks()[c].entries[f].name;
            if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                matches.push_back(std::make_pair(c, f));
        }
    }
    Need(matches.size() == 1, type + " entry " + suffix + " missing or ambiguous");
    if (found) *found = archive.Chunks()[matches[0].first].entries[matches[0].second].name;
    return archive.ReadEntry(stream, matches[0].first, matches[0].second);
}

void SourceBrains(const std::string &mpk) {
    std::string name;
    const auto graph = DecodeBrainGraph(ReadArchiveEntry(mpk, "BRAI", "nazi_interragator", &name));
    Need(graph.FindNode(6) && graph.FindNode(6)->type == 43 && graph.FindNode(10) && graph.FindNode(10)->type == 43 &&
             graph.FindNode(17) && graph.FindNode(17)->type == 33 && graph.FindNode(7) && graph.FindNode(7)->type == 12,
         "nazi_interragator shape (17/33, 6/43, 10/43, 7/12)");
    FakeWorld world;
    world.owner = "nazi_interragator_1";
    world.ais = {"nazi_interragator_1", "nazi_interragator_2"};
    ReplayHost host(world);
    Random random;
    BrainTaskRuntime runtime(graph, host, random, 1, BrainTaskProfile::ProbeLeafPassV1);
    Need(runtime.Tick(100).status == BrainTaskResult::Advanced, "original spawn Tick");
    runtime.NotifyUse("player_leaves_train");
    Need(runtime.Tick(200).status == BrainTaskResult::Advanced && world.hunts.empty(), "original node 25 Tick, no Hunt yet");
    host.truePrimaries.insert(25); // declared external truth: AnimFinished
    const auto before = host.trace.size();
    const auto result = runtime.Tick(300);
    const auto tail = Join(host.trace, before);
    Need(result.status == BrainTaskResult::Advanced && tail == "X25 E17 P16 P46 P47 X17 E6 P10 X6 E7" &&
             graph.nodes[runtime.State().activeNode].id == 7,
         "original: one Tick enters 6, probes 10 and enters 7 (" + tail + ")");
    CheckInterrogatorHunts(world, "nazi_interragator_2", "original nazi_interragator");
    Need(runtime.Tick(400).status == BrainTaskResult::Idle && world.hunts.size() == 2, "original: Hunt not repeated");
    std::size_t noTargets = 0;
    for (const auto &r : host.receipts) noTargets += r.action == "NoTargetSet" ? 1 : 0;
    std::cout << "SOURCE brain=" << name << " tail=\"" << tail << "\" hunts=" << world.hunts.size()
              << " noTargetReceipts=" << noTargets << " enemyDeadEvaluations=" << host.enemyDeadEvaluations << "\n";

    // nazi_patrol: IgnoreEnemiesSet(false), roots' embedded conditions and the
    // Hunt with the default prey (refused by the reference profile).
    const auto patrol = DecodeBrainGraph(ReadArchiveEntry(mpk, "BRAI", "nazi_patrol"));
    BrainCombatTaskSpec task;
    std::string why;
    Need(DecodeBrainCombatTaskV1(patrol, Index(patrol, 5), task, why) && !task.value &&
             task.subject.name == "allies_kreisau_contact_1", "nazi_patrol 5 IgnoreEnemiesSet(false): " + why);
    Need(!DecodeBrainCombatTaskV1(patrol, Index(patrol, 15), task, why) && why.find("outside") != std::string::npos,
         "nazi_patrol 15 Hunt with default prey MyEnemy refuses");
    BrainCombatConditionSpec condition;
    Need(DecodeBrainCombatPrimaryV1(patrol, Index(patrol, 2), condition, why) &&
             condition.kind == BrainCombatConditionKind::Aware && EvaluateAwareV1(condition) == BrainConditionValue::False,
         "nazi_patrol OnAware 2: constant-false Aware");
    Need(DecodeBrainCombatPrimaryV1(patrol, Index(patrol, 3), condition, why) &&
             condition.kind == BrainCombatConditionKind::CombatJustStarted && condition.timeChecked == 86565 &&
             !condition.wasInCombat && !condition.isInCombat, "nazi_patrol OnCombat 3 saved fields");
    Need(DecodeBrainCombatPrimaryV1(patrol, Index(patrol, 4), condition, why) &&
             condition.kind == BrainCombatConditionKind::HasTakenPain && condition.minDefaulted &&
             condition.maxThreshold == -1, "nazi_patrol OnPain 4 default thresholds");
    std::size_t hunts = 0, refusedHunts = 0, ignores = 0;
    for (std::size_t n = 0; n < patrol.nodes.size(); ++n) {
        if (patrol.nodes[n].type == 43) { ++hunts; refusedHunts += DecodeBrainCombatTaskV1(patrol, n, task, why) ? 0 : 1; }
        if (patrol.nodes[n].type == 49) { Need(DecodeBrainCombatTaskV1(patrol, n, task, why), "patrol 49: " + why); ++ignores; }
    }
    std::cout << "SOURCE nazi_patrol hunts=" << hunts << " refusedDefaultPrey=" << refusedHunts
              << " ignoreEnemiesSet=" << ignores << "\n";

    const auto shake = DecodeBrainGraph(ReadArchiveEntry(mpk, "BRAI", "shake_train"));
    BrainActionSpec action;
    Need(DecodeBrainActionV1(shake, Index(shake, 32), action, why) && action.kind == BrainActionKind::NoTargetSet &&
             action.rawValue == "fasle" && !action.value && action.subject.mode == BrainEntityMode::Player,
         "shake_train 32 NoTargetSet(Player, \"fasle\") is false");
}

struct SourceDefs {
    std::unique_ptr<SourceDefinitions> definitions;
    std::size_t members = 0, skipped = 0;
};
SourceDefs LoadDefs(const std::string &pk4) {
    std::ifstream stream(pk4, std::ios::binary);
    Need(static_cast<bool>(stream), "cannot open " + pk4);
    const auto zip = ZipArchive::Scan(stream);
    SourceDefinitionLimits limits;
    limits.members = 4096;
    limits.totalSourceBytes = 256u << 20;
    limits.tokens = 8000000;
    limits.declarations = 100000;
    SourceDefs out;
    out.definitions.reset(new SourceDefinitions(limits));
    SourceDefinitionArchiveIdentity identity;
    identity.name = "assets.pk4";
    stream.seekg(0, std::ios::end);
    identity.bytes = static_cast<std::size_t>(stream.tellg());
    for (std::size_t i = 0; i < zip.Entries().size(); ++i) {
        const auto &entry = zip.Entries()[i];
        const auto &n = entry.name;
        if (n.size() < 8 || n.compare(0, 4, "def/") != 0 || n.compare(n.size() - 4, 4, ".def") != 0) continue;
        try {
            out.definitions->AddMember(identity, n, zip.ReadEntry(stream, i));
            ++out.members;
        } catch (const std::exception &) {
            ++out.skipped; // malformed or duplicate members stay out, counted
        }
    }
    return out;
}
std::string Literal(const ResolvedSourceDefinition &def, const std::string &key) {
    const auto *found = def.Find(key);
    if (!found || found->value.size() != 1) return std::string();
    return found->value[0].text;
}

void SourceTactical(const std::string &mpk, const SourceDefs &defs) {
    const auto ents = DecodeEntities4(ReadArchiveEntry(mpk, "ENTS", "trainyard"));
    for (const char *who : {"nazi_interragator_1", "nazi_interragator_2"}) {
        const EntityRecord *record = nullptr;
        std::size_t index = 0;
        for (std::size_t i = 0; i < ents.entities.size(); ++i) {
            const auto *n = ents.entities[i].Find("name");
            if (n && n->kind == EntityProperty::Text && n->text == who) { record = &ents.entities[i]; index = i; }
        }
        Need(record != nullptr && index == (std::string(who) == "nazi_interragator_1" ? 2091u : 2092u),
             std::string("ENTS entity ") + who + " at 2091/2092");
        const auto *classname = record->Find("classname");
        Need(classname && classname->kind == EntityProperty::Text, "ENTS classname");
        const auto def = defs.definitions->Resolve(SourceDefinitionKind::EntityDef, classname->text);
        std::map<std::string, std::string> merged;
        for (const auto &field : def.literalFields)
            if (field.second.value.size() == 1) merged[field.first] = field.second.value[0].text;
        for (const auto &p : record->properties) {
            std::string key = p.key;
            for (auto &c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (p.kind == EntityProperty::Text) merged[key] = p.text;
            else if (p.kind == EntityProperty::Integer) merged[key] = std::to_string(p.integer);
            else if (p.numbers.size() == 1) {
                std::ostringstream text;
                text << p.numbers[0];
                merged[key] = text.str();
            }
        }
        const auto lookup = [&](const std::string &key) -> const std::string * {
            std::string folded = key;
            for (auto &c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            const auto found = merged.find(folded);
            return found == merged.end() ? nullptr : &found->second;
        };
        const auto mask = BuildTacticalAvailableMaskV1(lookup);
        const auto *disable = lookup("disableCover");
        Need(disable && SourceAtoi(*disable) == 1, std::string(who) + " disableCover 1");
        Need(lookup("def_weapon") && *lookup("def_weapon") == "weapon_mp40", std::string(who) + " def_weapon weapon_mp40");
        CheckInterrogatorTactical(mask, FreshEnemy(SourceAtoi(*disable) != 0), std::string("original ") + who);
        std::cout << "SOURCE " << who << " ents=" << index << " class=" << classname->text << " chain="
                  << def.declarations.size() << " mask=0x" << std::hex << mask << std::dec << "\n";
    }
}

void SourceWeapon(const SourceDefs &defs) {
    const auto weapon = defs.definitions->Resolve(SourceDefinitionKind::EntityDef, "weapon_mp40");
    Need(Literal(weapon, "def_stats_ai") == "stats_mp40_ai" && Literal(weapon, "def_hitscan_ai") == "hitscan_mp40_ai" &&
             Literal(weapon, "def_projectile").empty() && Literal(weapon, "def_continuous").empty(),
         "weapon_mp40 AI keys");
    const auto hitscan = defs.definitions->Resolve(SourceDefinitionKind::EntityDef, "hitscan_mp40_ai");
    Need(Literal(hitscan, "def_damage") == "damage_mp40_ai" && Literal(hitscan, "num_projectiles") == "1",
         "hitscan_mp40_ai keys");
    const auto damage = defs.definitions->Resolve(SourceDefinitionKind::EntityDef, "damage_mp40_ai");
    Need(SourceAtoi(Literal(damage, "damage")) == 6, "damage_mp40_ai damage 6");
    const auto statsDef = defs.definitions->Resolve(SourceDefinitionKind::EntityDef, "stats_mp40_ai");
    std::map<std::string, std::string> values;
    for (const auto &field : statsDef.literalFields)
        if (field.second.value.size() == 1) values[field.first] = field.second.value[0].text;
    const auto lookup = [&](const std::string &key) -> const std::string * {
        std::string folded = key;
        for (auto &c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        const auto found = values.find(folded);
        return found == values.end() ? nullptr : &found->second;
    };
    WeaponStatsV1 stats;
    std::string why;
    Need(ReadWeaponStatsV1(lookup, stats, why), "original stats_mp40_ai: " + why);
    Need(stats.minShots == 3 && stats.maxShots == 8 && stats.shotWaitTime == 120 && stats.minBursts == 1 &&
             stats.maxBursts == 2 && stats.burstWaitTimeMin == 500 && stats.burstWaitTimeMax == 1000 &&
             stats.clipSize == 32 && stats.attackRangeMin == 0 && stats.attackRangeMax == 2000,
         "original stats_mp40_ai values");
    for (std::uint32_t seed : {11u, 0x5eedu}) ReplayBursts(stats, seed, 16, "original stats_mp40_ai");
}

void Source(const std::string &base) {
    const std::string mpk = base + "/maps/game/trainyard/trainyard.mpk";
    SourceBrains(mpk);
    const auto defs = LoadDefs(base + "/assets.pk4");
    std::cout << "SOURCE assets.pk4 def members=" << defs.members << " skipped=" << defs.skipped << "\n";
    SourceTactical(mpk, defs);
    SourceWeapon(defs);
}
} // namespace

namespace wolf2009 {
namespace aicombat {
int RunSourceAICombatTests() {
    BrainHandlers();
    Conditions();
    Tactics();
    Bursts();
    return 0;
}
} // namespace aicombat
} // namespace wolf2009

int main(int argc, char **argv) {
    try {
        RunSourceAICombatTests();
        std::string base;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--source-base") base = argv[i + 1];
        if (!base.empty()) Source(base);
        std::cout << "SOURCE_AI_COMBAT_OK checks=" << checks << " source=" << (base.empty() ? 0 : 1) << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "SOURCE_AI_COMBAT_FAIL checks=" << checks << " reason=" << e.what() << "\n";
        return 1;
    }
}

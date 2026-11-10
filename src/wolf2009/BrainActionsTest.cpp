// SPDX-License-Identifier: GPL-3.0-or-later
// Authored graphs only (no original data). The TestHost declares which types
// it admits; it is not campaign gameplay.
#include "BrainActions.h"
#include "BrainTask.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace wolf2009 {
namespace {
unsigned checks = 0;
void Need(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
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
    throw std::runtime_error("Missing test node");
}
bool Throws(void (*body)()) {
    try { body(); } catch (const std::exception &) { return true; }
    return false;
}

void Arguments() {
    const auto graph = Decode(
        Node(102, 1, 0, "mNoTarget|fasle|mActor|Player|") + Node(102, 2, 0, "mNoTarget|TRUE|") +
        Node(102, 3, 0, "") + Node(33, 4, 0, "mActor|nazi_one|mSetToTrue|false|") +
        Node(54, 5, 0, "mActor|nazi_one|") + Node(54, 6, 0, "mAnimName||mActor|nazi_one|") +
        Node(19, 7, 0, "^18|1024|mOwner|nazi_two|mDefName|scripted_voice_wi2_trn|") +
        Node(357, 8, 0, "") + Node(468, 9, 0, "mEnable|false|") + Node(253, 10, 0, "mActor|Player|mEnable|false|") +
        Node(102, 11, 0, "mNoTarget|@flag|") + Node(102, 12, 0, "mActor|MyEnemy|") +
        Node(102, 13, 0, "mBogus|1|") + Node(51, 14, 0, "mAnimName|clip|") + Node(19, 15, 0, "mDefName|x|") +
        Node(57, 16, 0, "mActor|hostage|") + Node(102, 17, 0, "mActor|MyTarget 1|"));
    BrainActionSpec spec;
    std::string why;
    Need(DecodeBrainActionV1(graph, Index(graph, 1), spec, why) && !spec.value && spec.rawValue == "fasle" &&
         spec.subject.mode == BrainEntityMode::Player && spec.profile == "SourceEntityNoTargetV1", "fasle is false");
    Need(DecodeBrainActionV1(graph, Index(graph, 2), spec, why) && spec.value && spec.subject.mode == BrainEntityMode::Me,
         "TRUE is true and mActor defaults to Me");
    Need(DecodeBrainActionV1(graph, Index(graph, 3), spec, why) && spec.value && spec.valueDefaulted, "absent bool uses default true");
    Need(DecodeBrainActionV1(graph, Index(graph, 4), spec, why) && !spec.value && spec.subject.mode == BrainEntityMode::Named &&
         spec.subject.name == "nazi_one", "SetScriptedManual named false");
    Need(DecodeBrainActionV1(graph, Index(graph, 5), spec, why) && spec.text == "test" && spec.textDefaulted,
         "AnimOverrideIdle default name is the descriptor default");
    Need(DecodeBrainActionV1(graph, Index(graph, 6), spec, why) && spec.text.empty() && !spec.textDefaulted,
         "Explicit empty idle name resets");
    Need(DecodeBrainActionV1(graph, Index(graph, 7), spec, why) && spec.text == "scripted_voice_wi2_trn" &&
         spec.subject.name == "nazi_two" && spec.profile == "ScriptedVOStreamInWholeBankV1", "type19 owner/def");
    Need(DecodeBrainActionV1(graph, Index(graph, 8), spec, why) && spec.value && spec.subject.mode == BrainEntityMode::Player,
         "EnablePlayerWeapons default acts on the local player");
    Need(DecodeBrainActionV1(graph, Index(graph, 9), spec, why) && !spec.value, "EnableCredits false");
    Need(DecodeBrainActionV1(graph, Index(graph, 10), spec, why) && !spec.value && spec.subject.mode == BrainEntityMode::Player,
         "SetUndying Player false");
    Need(!DecodeBrainActionV1(graph, Index(graph, 11), spec, why) && !why.empty(), "'@' bool refused");
    Need(!DecodeBrainActionV1(graph, Index(graph, 12), spec, why), "MyEnemy refused");
    Need(!DecodeBrainActionV1(graph, Index(graph, 13), spec, why), "unknown key refused");
    Need(!DecodeBrainActionV1(graph, Index(graph, 14), spec, why), "unadmitted type refused");
    Need(!DecodeBrainActionV1(graph, Index(graph, 15), spec, why), "type19 without its primary condition refused");
    Need(DecodeBrainActionV1(graph, Index(graph, 16), spec, why) && spec.text.empty() && spec.textDefaulted,
         "AnimOverrideDeath default resets");
    Need(!DecodeBrainActionV1(graph, Index(graph, 17), spec, why), "indexed reference refused");
    Need(ParseBrainEntityReferenceV1("").mode == BrainEntityMode::None && ParseBrainEntityReferenceV1("me").mode == BrainEntityMode::Me,
         "None/me keywords");
    Need(Throws([] { ParseBrainEntityReferenceV1("bad name"); }), "non-plain names refused");
}

// SourceAIScriptedAnimV1 argument shapes (authored records, same key layout
// as the original Animate/KillTarget records).
void ScriptedAnimArguments() {
    const auto graph = Decode(
        Node(51, 1, 0, "^50|1024|mAnimName|clip_one|mActor|actor_1|mAnimSucceeded|true|mAnimFailTime|0|") +
        Node(51, 2, 0, "^50|1024|mAnimName|clip_two|mBlendFrames|4|mNumLoops|-1|mEndWithIdle|false|") +
        Node(51, 3, 0, "^50|1024|") + Node(51, 4, 0, "^50|1024|mBlendFrames|two|") +
        Node(51, 5, 0, "^50|1024|mNumLoops|-2|") + Node(69, 6, 0, "mKillTarget|hostage_1|") +
        Node(69, 7, 0, "mKillTarget|hostage_2|mBypassDeathAnim|TRUE|") + Node(69, 8, 0, "") +
        Node(51, 9, 0, "^50|1024|mBogus|1|"));
    BrainActionSpec spec;
    std::string why;
    Need(DecodeBrainActionV1(graph, Index(graph, 1), spec, why) && spec.kind == BrainActionKind::Animate &&
         spec.text == "clip_one" && spec.subject.name == "actor_1" && spec.blendFrames == 2 && spec.blendDefaulted &&
         spec.numLoops == 0 && spec.value && spec.valueDefaulted && spec.savedState.size() == 2 &&
         spec.profile == "SourceAIScriptedAnimV1", "Animate defaults with saved state retained");
    Need(DecodeBrainActionV1(graph, Index(graph, 2), spec, why) && spec.blendFrames == 4 && spec.numLoops == -1 &&
         !spec.value && spec.subject.mode == BrainEntityMode::Me, "Animate explicit blend, loops and endWithIdle");
    Need(DecodeBrainActionV1(graph, Index(graph, 3), spec, why) && spec.text == "test" && spec.textDefaulted,
         "Animate default name");
    Need(!DecodeBrainActionV1(graph, Index(graph, 4), spec, why), "non-decimal mBlendFrames refused");
    Need(!DecodeBrainActionV1(graph, Index(graph, 5), spec, why), "mNumLoops below -1 refused");
    Need(DecodeBrainActionV1(graph, Index(graph, 6), spec, why) && spec.kind == BrainActionKind::KillTarget &&
         spec.subject.name == "hostage_1" && !spec.value && spec.valueDefaulted && spec.rawValue == "False",
         "KillTarget default bypass is False");
    Need(DecodeBrainActionV1(graph, Index(graph, 7), spec, why) && spec.value, "KillTarget TRUE bypass");
    Need(!DecodeBrainActionV1(graph, Index(graph, 8), spec, why), "KillTarget default Entity keyword refused");
    Need(!DecodeBrainActionV1(graph, Index(graph, 9), spec, why), "Animate unknown key refused");
}

// SourceAIHuntV1: Hunt 43, IgnoreEnemiesSet 49 and the combat conditions
// (authored records with the same key layout as the trainyard records).
void CombatArguments() {
    const auto graph = Decode(
        Node(43, 1, 16, "^42|1024|mPrey|Player|mUpdatePosition|true|") +
        Node(43, 2, 32, "^42|1024|mHunter|soldier_2|mPrey|Player|mUpdatePosition|true|") +
        Node(43, 3, 0, "^42|1024|mHunter|soldier_3|mUpdatePosition|true|") +
        Node(43, 4, 0, "^42|1024|mPrey|Player|mLocked|True|mUpdatePosition|1|") +
        Node(43, 5, 0, "mPrey|Player|") + Node(43, 6, 0, "^42|1024|mPrey|Player|mBogus|x|") +
        Node(49, 7, 0, "mIgnore|false|mActor|contact_1|") + Node(49, 8, 0, "") +
        Node(49, 9, 0, "mIgnore|@flag|") + Node(102, 10, 0, "mNoTarget|fasle|mActor|Player|") +
        Node(7, 11, 0, "^108|1024|mWasInCombat|false|mIsInCombat|false|mTimeChecked|86565|") +
        Node(112, 12, 0, "^104|1024|") + Node(111, 13, 1, "^103|1024|") +
        Node(112, 14, 0, "^104|1024|mMinThreshold|5|mMaxThreshold|20|") + Node(30, 15, 0, "^65|1024|") +
        Node(7, 16, 0, "^108|1024|mTimeChecked|x|") + Node(51, 17, 0, "^50|1024|") +
        Node(30, 18, 16, "") + Output(16, 1, "\t\t42|^42|2|mHunter|Player|\n") +
        Output(16, 1, "\t\t104|^104|0|mMaxThreshold|30|\n") + Output(16, 1, "\t\t108|^108|1|\n") +
        Output(16, 1, "\t\t103|^103|0|mBogus|1|\n") + Output(16, 1, "\t\t448|^448|0|\n"));
    BrainCombatTaskSpec task;
    std::string why;
    Need(DecodeBrainCombatTaskV1(graph, Index(graph, 1), task, why) && task.kind == BrainCombatTaskKind::Hunt &&
         task.subject.mode == BrainEntityMode::Me && task.prey.mode == BrainEntityMode::Player && !task.locked &&
         task.lockedDefaulted && task.updatePosition && !task.updateDefaulted && task.primaryCondition == 42 &&
         task.profile == "SourceAIHuntV1", "Hunt node 6 shape: Me hunts Player, unlocked, updatePosition");
    Need(DecodeBrainCombatTaskV1(graph, Index(graph, 2), task, why) && task.subject.mode == BrainEntityMode::Named &&
         task.subject.name == "soldier_2" && task.prey.mode == BrainEntityMode::Player, "Hunt node 10 shape: named hunter");
    Need(!DecodeBrainCombatTaskV1(graph, Index(graph, 3), task, why) && !why.empty(), "Hunt default prey MyEnemy refused");
    Need(DecodeBrainCombatTaskV1(graph, Index(graph, 4), task, why) && task.locked && !task.updatePosition &&
         task.rawUpdatePosition == "1", "Hunt bool rule: True is true, 1 is false");
    Need(!DecodeBrainCombatTaskV1(graph, Index(graph, 5), task, why), "Hunt without its EnemyDead primary refused");
    Need(!DecodeBrainCombatTaskV1(graph, Index(graph, 6), task, why), "Hunt unknown key refused");
    Need(DecodeBrainCombatTaskV1(graph, Index(graph, 7), task, why) && task.kind == BrainCombatTaskKind::IgnoreEnemiesSet &&
         !task.value && task.subject.name == "contact_1" && task.profile == "SourceAIIgnoreEnemiesV1", "IgnoreEnemiesSet false");
    Need(DecodeBrainCombatTaskV1(graph, Index(graph, 8), task, why) && task.value && task.valueDefaulted &&
         task.subject.mode == BrainEntityMode::Me, "IgnoreEnemiesSet defaults: Me, true");
    Need(!DecodeBrainCombatTaskV1(graph, Index(graph, 9), task, why), "IgnoreEnemiesSet '@' refused");
    Need(!DecodeBrainCombatTaskV1(graph, Index(graph, 10), task, why), "NoTargetSet is an action, not a combat task");
    Need(!IsBrainActionType(43) && !IsBrainActionType(49) && IsBrainCombatTaskType(43) && IsBrainCombatTaskType(49),
         "combat tasks stay outside the native single-procedure table");
    BrainActionSpec action;
    Need(DecodeBrainActionV1(graph, Index(graph, 10), action, why) && !action.value && action.rawValue == "fasle",
         "NoTargetSet fasle stays false");

    BrainCombatConditionSpec condition;
    Need(DecodeBrainCombatPrimaryV1(graph, Index(graph, 1), condition, why) &&
         condition.kind == BrainCombatConditionKind::EnemyDead && condition.embedded && !condition.invert &&
         condition.subject.mode == BrainEntityMode::Me, "Hunt primary EnemyDead with default hunter");
    Need(DecodeBrainCombatPrimaryV1(graph, Index(graph, 2), condition, why) && condition.subject.name == "soldier_2",
         "Hunt primary EnemyDead carries mHunter");
    Need(DecodeBrainCombatPrimaryV1(graph, Index(graph, 11), condition, why) &&
         condition.kind == BrainCombatConditionKind::CombatJustStarted && !condition.wasInCombat &&
         !condition.isInCombat && condition.timeChecked == 86565 && condition.profile == "SourceAICombatJustStartedV1",
         "OnCombat embedded CombatJustStarted saved fields");
    Need(DecodeBrainCombatPrimaryV1(graph, Index(graph, 12), condition, why) &&
         condition.kind == BrainCombatConditionKind::HasTakenPain && condition.minThreshold == -1 &&
         condition.maxThreshold == -1 && condition.minDefaulted && condition.maxDefaulted, "OnPain default thresholds -1");
    Need(DecodeBrainCombatPrimaryV1(graph, Index(graph, 13), condition, why) &&
         condition.kind == BrainCombatConditionKind::Aware, "OnAware embedded Aware");
    Need(DecodeBrainCombatPrimaryV1(graph, Index(graph, 14), condition, why) && condition.minThreshold == 5 &&
         condition.maxThreshold == 20 && !condition.minDefaulted, "explicit pain thresholds");
    Need(!DecodeBrainCombatPrimaryV1(graph, Index(graph, 15), condition, why), "Timer primary is not a combat condition");
    Need(!DecodeBrainCombatPrimaryV1(graph, Index(graph, 16), condition, why), "non-decimal mTimeChecked refused");
    Need(!DecodeBrainCombatPrimaryV1(graph, Index(graph, 17), condition, why), "Animate owner refused");
    const auto &outputs = graph.nodes[Index(graph, 18)].outputs;
    Need(outputs.size() == 5, "five authored combat condition outputs");
    std::vector<std::size_t> records;
    for (auto output : outputs) records.push_back(graph.outputs[output].conditions.at(0));
    Need(DecodeBrainCombatConditionV1(graph, records[0], condition, why) && condition.invert && condition.flags == 2 &&
         condition.subject.mode == BrainEntityMode::Player && !condition.embedded, "explicit NOT EnemyDead");
    Need(DecodeBrainCombatConditionV1(graph, records[1], condition, why) && !condition.invert &&
         condition.minThreshold == -1 && condition.maxThreshold == 30, "explicit HasTakenPain max 30");
    Need(!DecodeBrainCombatConditionV1(graph, records[2], condition, why), "ONCE_ONLY flag on a combat condition refused");
    Need(!DecodeBrainCombatConditionV1(graph, records[3], condition, why), "unknown Aware key refused");
    Need(!DecodeBrainCombatConditionV1(graph, records[4], condition, why), "PlayerHasWeapon is not a combat condition");
}

// EnableDamage 252, SetUsable 75, LockPlayer 185, UnlockPlayer 186.
void PlayerFlags() {
    const auto graph = Decode(
        Node(252, 1, 0, "mEntity|Player|mEnable|false|") + Node(252, 2, 0, "") +
        Node(75, 3, 0, "mEntity|Me|mUsable|false|") + Node(75, 4, 0, "mUsable|false|") +
        Node(185, 5, 0, "") + Node(186, 6, 0, "") + Node(185, 7, 0, "mBogus|1|") +
        Node(75, 8, 0, "mEntity|train_greeter|"));
    BrainActionSpec spec;
    std::string why;
    Need(DecodeBrainActionV1(graph, Index(graph, 1), spec, why) && !spec.value && spec.subject.mode == BrainEntityMode::Player &&
         spec.kind == BrainActionKind::EnableDamage && spec.profile == "SourceEntityTakeDamageV1", "EnableDamage Player false");
    Need(DecodeBrainActionV1(graph, Index(graph, 2), spec, why) && spec.value && spec.valueDefaulted && spec.rawValue == "True" &&
         spec.subject.mode == BrainEntityMode::Me, "EnableDamage defaults: Me, \"True\"");
    Need(DecodeBrainActionV1(graph, Index(graph, 3), spec, why) && !spec.value && spec.subject.mode == BrainEntityMode::Me &&
         spec.kind == BrainActionKind::SetUsable, "SetUsable Me false");
    Need(!DecodeBrainActionV1(graph, Index(graph, 4), spec, why) && !why.empty(), "SetUsable default Usable keyword refused");
    Need(DecodeBrainActionV1(graph, Index(graph, 5), spec, why) && spec.kind == BrainActionKind::LockPlayer &&
         spec.subject.mode == BrainEntityMode::Player && spec.profile == "SourcePlayerLockV1", "LockPlayer has no arguments");
    Need(DecodeBrainActionV1(graph, Index(graph, 6), spec, why) && spec.kind == BrainActionKind::UnlockPlayer, "UnlockPlayer");
    Need(!DecodeBrainActionV1(graph, Index(graph, 7), spec, why), "LockPlayer with a property refused");
    Need(DecodeBrainActionV1(graph, Index(graph, 8), spec, why) && spec.value && spec.valueDefaulted &&
         spec.subject.name == "train_greeter", "SetUsable named with default true");
}

// PlayerHasWeapon 448 as an explicit output condition record.
void Conditions() {
    const auto graph = Decode(
        Node(30, 1, 16, "") + Output(16, 2, "\t\t448|^448|0|mWeaponName|weapon_mp40|\n") +
        Output(16, 2, "\t\t448|^448|0|\n") + Output(16, 2, "\t\t448|^448|1|mWeaponName|weapon_mp40|\n") +
        Output(16, 2, "\t\t100|^100|0|mLocation|x|\n") + Output(16, 2, "\t\t448|^448|0|mOther|x|\n") +
        Node(30, 2, 16, ""));
    std::vector<std::size_t> records;
    for (const auto &output : graph.outputs) {
        Need(output.conditions.size() == 1, "authored output carries one condition record");
        records.push_back(output.conditions[0]);
    }
    Need(records.size() == 5, "five authored condition records");
    BrainConditionSpec spec;
    std::string why;
    Need(DecodeBrainConditionV1(graph, records[0], spec, why) && spec.text == "weapon_mp40" && !spec.textDefaulted &&
         spec.flags == 0 && spec.profile == "SourcePlayerHasWeaponV1", "PlayerHasWeapon weapon_mp40");
    Need(DecodeBrainConditionV1(graph, records[1], spec, why) && spec.text == "weapon_plasma_cannon" && spec.textDefaulted,
         "PlayerHasWeapon descriptor default");
    Need(!DecodeBrainConditionV1(graph, records[2], spec, why) && !why.empty(), "unmapped flag word (inversion) refused");
    Need(!DecodeBrainConditionV1(graph, records[3], spec, why), "other condition types refused");
    Need(!DecodeBrainConditionV1(graph, records[4], spec, why), "unknown condition property refused");
}

struct Host : BrainHeldStepHost {
    std::vector<std::pair<unsigned, bool> > entered;
    std::vector<unsigned> exits;
    BrainTaskUse use;
    bool Root(const BrainGraph &graph, std::size_t node, std::uint32_t &priority) override {
        const auto type = graph.nodes[node].type;
        priority = type == 1 ? 10 : 30;
        return type == 1 || type == 3;
    }
    BrainConditionValue RootReady(const BrainGraph &graph, std::size_t root) override {
        if (graph.nodes[root].type == 1) return BrainConditionValue::True;
        return use.Matches("go") ? BrainConditionValue::True : BrainConditionValue::False;
    }
    BrainConditionValue Primary(const BrainGraph &graph, std::size_t node) override {
        return graph.nodes[node].type == 1 ? BrainConditionValue::True
               : graph.nodes[node].type == 3 ? (use.Matches("go") ? BrainConditionValue::True : BrainConditionValue::False)
                                              : BrainConditionValue::Unsupported;
    }
    BrainConditionValue Explicit(const BrainGraph &, std::size_t) override { return BrainConditionValue::Unsupported; }
    bool Admits(const BrainGraph &graph, std::size_t node, std::string &reason) override {
        if (IsBrainActionType(graph.nodes[node].type)) return true;
        reason = "unadmitted";
        return false;
    }
    void Enter(const BrainGraph &graph, std::size_t node, bool probe) override {
        entered.push_back(std::make_pair(graph.nodes[node].id, probe));
    }
    void ExitRoot(const BrainGraph &graph, std::size_t root) override {
        exits.push_back(graph.nodes[root].id);
        if (graph.nodes[root].type == 3) use.Consume("go");
    }
};

void Stepper() {
    // OnSpawn -> 11 -> 20 -> 2 (probes 3, 29, 31, 44) -> 38 (probe 39). OnUsed
    // "go" -> 43 -> 21 -> 25 (Hunt, unadmitted).
    const auto graph = Decode(
        Node(1, 1, 1, "^74|1024|") + Output(16, 11, "\t\t-1|\n") +
        Node(33, 2, 16) + Output(32, 3) + Output(32, 29) + Output(16, 38) + Output(32, 31) + Output(32, 44) +
        Node(33, 3, 32, "mActor|two|") + Node(19, 11, 16, "^18|1024|mDefName|a|") + Output(16, 20) +
        Node(3, 12, 0, "^64|1024|mUseKey|go|") + Output(16, 43, "\t\t-1|\n") +
        Node(19, 20, 16, "^18|1024|mOwner|two|mDefName|b|") + Output(16, 2) +
        Node(57, 21, 0, "mActor|h1|mDeathAnim|die|") + Output(16, 25) +
        Node(43, 25, 16, "^42|1024|mPrey|Player|") + Node(57, 29, 32, "mActor|h1|") +
        Node(102, 31, 32, "mActor|h1|") + Node(54, 38, 16, "mAnimName|idle|") + Output(32, 39) +
        Node(54, 39, 32, "mAnimName|idle2|mActor|two|") + Node(54, 43, 16, "mAnimName||") + Output(16, 21) +
        Node(253, 44, 32, "mActor|one|"));
    Host host;
    BrainHeldStepper stepper(graph, host);
    auto result = stepper.Tick();
    if (std::getenv("BRAIN_ACTIONS_TRACE")) { std::cerr << result.status << " t=" << result.transitions << " p=" << result.probes << " " << result.reason << "\n"; for (auto &e : host.entered) std::cerr << e.first << (e.second ? "p " : " "); std::cerr << "\n"; }
    const std::vector<std::pair<unsigned, bool> > first = {{11, false}, {20, false}, {2, false}, {3, true}, {29, true},
        {31, true}, {44, true}, {38, false}, {39, true}};
    Need(result.status == BrainHeldStepResult::Advanced && result.transitions == 4 && result.probes == 5 &&
         host.entered == first && graph.nodes[stepper.Active()].id == 38 && host.exits == std::vector<unsigned>{1},
         "OnSpawn chain did not follow normal/probe order to 38");
    result = stepper.Tick();
    Need(result.status == BrainHeldStepResult::Idle && host.entered.size() == first.size(), "Idle repeat entered a task");
    host.use.Notify("go");
    result = stepper.Tick();
    Need(result.status == BrainHeldStepResult::Held && graph.nodes[result.heldNode].id == 25 &&
         graph.nodes[result.heldSource].id == 21 && graph.nodes[stepper.Active()].id == 21 &&
         host.exits == std::vector<unsigned>({1, 12}) && !host.use.pending && host.entered.size() == first.size() + 2 &&
         host.entered.back().first == 21, "OnUsed did not reach 21 and hold before Hunt 25");
    const auto entered = host.entered.size();
    result = stepper.Tick();
    Need(result.status == BrainHeldStepResult::Held && host.entered.size() == entered && !stepper.Failed(),
         "Held destination was re-entered or poisoned the stepper");

    // A held root keeps its pending key: unadmitted first destination.
    const auto blocked = Decode(Node(3, 1, 0, "^64|1024|mUseKey|go|") + Output(16, 2, "\t\t-1|\n") +
                                Node(43, 2, 16, "^42|1024|mPrey|Player|"));
    Host second;
    BrainHeldStepper held(blocked, second);
    second.use.Notify("go");
    result = held.Tick();
    Need(result.status == BrainHeldStepResult::Held && second.use.pending && second.exits.empty() &&
         second.entered.empty() && held.Active() == BrainRecord::None(), "Held root transition consumed its key");

    // An unadmitted probe destination holds the normal transition into its source.
    const auto probe = Decode(Node(1, 1, 1, "^74|1024|") + Output(16, 2, "\t\t-1|\n") +
                              Node(33, 2, 16) + Output(32, 3) + Node(43, 3, 32, "^42|1024|mPrey|Player|"));
    Host third;
    BrainHeldStepper probeHeld(probe, third);
    result = probeHeld.Tick();
    Need(result.status == BrainHeldStepResult::Held && probe.nodes[result.heldNode].id == 3 && third.entered.empty(),
         "Unadmitted probe destination did not hold before entering its source");
}
} // namespace

int RunBrainActionTests() {
    checks = 0;
    try {
        Arguments();
        ScriptedAnimArguments();
        CombatArguments();
        PlayerFlags();
        Conditions();
        Stepper();
        std::cout << "BRAIN_ACTIONS_PORTABLE_OK checks=" << checks << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "BRAIN_ACTIONS_PORTABLE_FAIL checks=" << checks << " reason=" << error.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

#ifdef WOLF2009_BRAIN_ACTIONS_TEST_MAIN
int main() { return wolf2009::RunBrainActionTests(); }
#endif

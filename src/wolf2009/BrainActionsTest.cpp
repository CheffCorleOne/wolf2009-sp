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
    // "go" -> 43 -> 21 -> 25 (Animate, unadmitted).
    const auto graph = Decode(
        Node(1, 1, 1, "^74|1024|") + Output(16, 11, "\t\t-1|\n") +
        Node(33, 2, 16) + Output(32, 3) + Output(32, 29) + Output(16, 38) + Output(32, 31) + Output(32, 44) +
        Node(33, 3, 32, "mActor|two|") + Node(19, 11, 16, "^18|1024|mDefName|a|") + Output(16, 20) +
        Node(3, 12, 0, "^64|1024|mUseKey|go|") + Output(16, 43, "\t\t-1|\n") +
        Node(19, 20, 16, "^18|1024|mOwner|two|mDefName|b|") + Output(16, 2) +
        Node(57, 21, 0, "mActor|h1|mDeathAnim|die|") + Output(16, 25) +
        Node(51, 25, 16, "^50|1024|mAnimName|kill|") + Node(57, 29, 32, "mActor|h1|") +
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
         host.entered.back().first == 21, "OnUsed did not reach 21 and hold before Animate 25");
    const auto entered = host.entered.size();
    result = stepper.Tick();
    Need(result.status == BrainHeldStepResult::Held && host.entered.size() == entered && !stepper.Failed(),
         "Held destination was re-entered or poisoned the stepper");

    // A held root keeps its pending key: unadmitted first destination.
    const auto blocked = Decode(Node(3, 1, 0, "^64|1024|mUseKey|go|") + Output(16, 2, "\t\t-1|\n") +
                                Node(51, 2, 16, "^50|1024|mAnimName|x|"));
    Host second;
    BrainHeldStepper held(blocked, second);
    second.use.Notify("go");
    result = held.Tick();
    Need(result.status == BrainHeldStepResult::Held && second.use.pending && second.exits.empty() &&
         second.entered.empty() && held.Active() == BrainRecord::None(), "Held root transition consumed its key");

    // An unadmitted probe destination holds the normal transition into its source.
    const auto probe = Decode(Node(1, 1, 1, "^74|1024|") + Output(16, 2, "\t\t-1|\n") +
                              Node(33, 2, 16) + Output(32, 3) + Node(51, 3, 32, "^50|1024|mAnimName|x|"));
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

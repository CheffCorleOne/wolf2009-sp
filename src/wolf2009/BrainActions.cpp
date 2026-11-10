// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainActions.h"
#include "BrainTask.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
unsigned char Fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool FoldEqual(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (Fold(static_cast<unsigned char>(a[i])) != Fold(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
bool StartsFolded(const std::string &text, const std::string &prefix) {
    return text.size() >= prefix.size() && FoldEqual(text.substr(0, prefix.size()), prefix);
}
bool Canonical(const std::string &text, std::uint32_t &value) {
    if (text.empty() || text.size() > 10 || (text.size() > 1 && text[0] == '0')) return false;
    std::uint64_t wide = 0;
    for (unsigned char c : text) {
        if (c < '0' || c > '9') return false;
        wide = wide * 10 + (c - '0');
    }
    if (wide > 0xffffffffull) return false;
    value = static_cast<std::uint32_t>(wide);
    return true;
}
// Focus enum keywords (Static, enum table used by parse 0x2020f920).
const char *const FocusKeywords[] = {"None", "Me", "MyEnemy", "MyLeader", "MyPartner", "Player", "Friend",
    "Enemy", "Actor", "Usable", "Waypoint", "Weapon", "Mover", "Door", "Light", "Trigger", "Tgt",
    "Tgt_Music", "Tgt_Remove", "Tgt_Use", "Vehicle", "Sound", "Entity", "MyTarget", "MyVehicle",
    "MyTgtOccupier"};

struct Property {
    const char *name;
    // Blend/Loops: kind 0x15 integer text. Saved: runtime fields the record
    // stores (kinds 0x4/0x6), reset by Enter and retained verbatim.
    enum Kind { Entity, Bool, Text, Blend, Loops, Saved } kind;
    const char *fallback; // descriptor default text
};
bool IntegerText(const std::string &text, std::int32_t &value) {
    if (text.empty() || text.size() > 10) return false;
    std::size_t at = text[0] == '-' ? 1 : 0;
    if (at == text.size()) return false;
    long long wide = 0;
    for (; at < text.size(); ++at) {
        if (text[at] < '0' || text[at] > '9') return false;
        wide = wide * 10 + (text[at] - '0');
    }
    if (text[0] == '-') wide = -wide;
    if (wide < -2147483647ll || wide > 2147483647ll) return false;
    value = static_cast<std::int32_t>(wide);
    return true;
}
struct Descriptor {
    BrainActionKind kind;
    std::uint32_t primary;
    const char *profile;
    std::vector<Property> properties;
};
// Recovered property tables (records 0x34 bytes; names, kinds and defaults).
const std::vector<Descriptor> &Descriptors() {
    static const std::vector<Descriptor> table = {
        {BrainActionKind::ScriptedVOStreamIn, 18, "ScriptedVOStreamInWholeBankV1",
         {{"mOwner", Property::Entity, "Me"}, {"mDefName", Property::Text, ""}}},
        {BrainActionKind::SetScriptedManual, 0, "SourceAIScriptedManualV1",
         {{"mActor", Property::Entity, "Me"}, {"mSetToTrue", Property::Bool, "true"}}},
        {BrainActionKind::AnimOverrideIdle, 0, "SourceAIIdleOverrideV1",
         {{"mAnimName", Property::Text, "test"}, {"mActor", Property::Entity, "Me"}}},
        {BrainActionKind::AnimOverrideDeath, 0, "SourceAIDeathOverrideStoreV1",
         {{"mActor", Property::Entity, "Me"}, {"mDeathAnim", Property::Text, ""}}},
        // Animate 51 table 0x205ca510 with the embedded AnimFinished 50 table
        // 0x205c29c8 (mAnimName, mActor, mBlendFrames, saved fields).
        {BrainActionKind::Animate, 50, "SourceAIScriptedAnimV1",
         {{"mAnimName", Property::Text, "test"}, {"mActor", Property::Entity, "Me"},
          {"mBlendFrames", Property::Blend, "2"}, {"mAnimSucceeded", Property::Saved, ""},
          {"mAnimFailTime", Property::Saved, ""}, {"mNumLoops", Property::Loops, "0"},
          {"mEndWithIdle", Property::Bool, "true"}}},
        // KillTarget 69 table 0x205c5328. The default "Entity" keyword is
        // outside this reference profile and refuses at decode.
        {BrainActionKind::KillTarget, 0, "SourceAIKillTargetV1",
         {{"mKillTarget", Property::Entity, "Entity"}, {"mBypassDeathAnim", Property::Bool, "False"}}},
        {BrainActionKind::NoTargetSet, 0, "SourceEntityNoTargetV1",
         {{"mNoTarget", Property::Bool, "true"}, {"mActor", Property::Entity, "Me"}}},
        {BrainActionKind::SetUndying, 0, "SourceActorUndyingV1",
         {{"mActor", Property::Entity, "Me"}, {"mEnable", Property::Bool, "true"}}},
        {BrainActionKind::EnablePlayerWeapons, 0, "SourcePlayerWeaponsV1",
         {{"mEnabled", Property::Bool, "true"}}},
        {BrainActionKind::EnableCredits, 0, "SourcePlayerCreditsEventV1",
         {{"mEnable", Property::Bool, "true"}}}};
    return table;
}
const Descriptor *FindDescriptor(std::uint32_t type) {
    for (const auto &descriptor : Descriptors())
        if (static_cast<std::uint32_t>(descriptor.kind) == type) return &descriptor;
    return nullptr;
}
} // namespace

const std::string *BrainNodeArguments::Find(const std::string &exactKey) const {
    for (const auto &value : values)
        if (value.first == exactKey) return &value.second;
    return nullptr;
}

BrainNodeArguments ReadBrainNodeArguments(const BrainGraph &graph, std::size_t node) {
    Need(node < graph.nodes.size(), "Brain node index is outside the graph");
    const auto &tokens = graph.records[graph.nodes[node].record].tokens;
    Need(tokens.size() >= 7, "Brain node header is truncated");
    BrainNodeArguments out;
    out.type = graph.nodes[node].type;
    out.id = graph.nodes[node].id;
    Need(tokens[5] == "^" + std::to_string(out.type), "Brain node type marker differs");
    Need(Canonical(tokens[6], out.serializedFlags), "Brain node serialized flag word is not canonical");
    std::size_t at = 7;
    if (at + 1 < tokens.size() && !tokens[at].empty() && tokens[at][0] == '^') {
        Need(tokens[at + 1] == "1024" && Canonical(tokens[at].substr(1), out.primaryCondition) &&
             out.primaryCondition, "Brain node embedded condition marker differs");
        at += 2;
        Need(at >= tokens.size() || tokens[at].empty() || tokens[at][0] != '^',
             "Brain node carries a second embedded condition");
    }
    Need((tokens.size() - at) % 2 == 0, "Brain node property list is not key/value pairs");
    for (; at < tokens.size(); at += 2) {
        Need(!tokens[at].empty() && out.Find(tokens[at]) == nullptr, "Brain node property key is empty or duplicated");
        out.values.push_back(std::make_pair(tokens[at], tokens[at + 1]));
    }
    return out;
}

BrainEntityReference ParseBrainEntityReferenceV1(const std::string &raw) {
    BrainEntityReference out;
    out.raw = raw;
    if (raw.empty()) { out.mode = BrainEntityMode::None; return out; }
    Need(raw[0] != '@', "Entity reference '@' resolution is outside SourceBrainActionsV1");
    for (const char *keyword : FocusKeywords) {
        if (!FoldEqual(raw, keyword)) continue;
        if (FoldEqual(raw, "None")) out.mode = BrainEntityMode::None;
        else if (FoldEqual(raw, "Me")) out.mode = BrainEntityMode::Me;
        else if (FoldEqual(raw, "Player")) out.mode = BrainEntityMode::Player;
        else throw std::runtime_error("Entity reference keyword is outside SourceBrainActionsV1");
        return out;
    }
    for (const char *indexed : {"MyTarget", "MyTgtOccupier"}) {
        if (StartsFolded(raw, indexed) && std::atoi(raw.c_str() + std::string(indexed).size()) != 0)
            throw std::runtime_error("Indexed entity reference is outside SourceBrainActionsV1");
    }
    Need(raw.size() <= 256, "Entity reference name budget");
    for (unsigned char c : raw)
        Need((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_',
             "Entity reference is not a plain entity name");
    out.mode = BrainEntityMode::Named;
    out.name = raw;
    return out;
}

bool ParseBrainBoolTextV1(const std::string &raw) {
    Need(raw.empty() || raw[0] != '@', "Bool '@' resolution is outside SourceBrainActionsV1");
    return FoldEqual(raw, "true");
}

const char *BrainActionKindName(BrainActionKind kind) {
    switch (kind) {
    case BrainActionKind::ScriptedVOStreamIn: return "ScriptedVOStreamIn";
    case BrainActionKind::SetScriptedManual: return "SetScriptedManual";
    case BrainActionKind::AnimOverrideIdle: return "AnimOverrideIdle";
    case BrainActionKind::AnimOverrideDeath: return "AnimOverrideDeath";
    case BrainActionKind::Animate: return "Animate";
    case BrainActionKind::KillTarget: return "KillTarget";
    case BrainActionKind::NoTargetSet: return "NoTargetSet";
    case BrainActionKind::SetUndying: return "SetUndying";
    case BrainActionKind::EnablePlayerWeapons: return "EnablePlayerWeapons";
    case BrainActionKind::EnableCredits: return "EnableCredits";
    }
    return "unknown";
}
bool IsBrainActionType(std::uint32_t type) { return FindDescriptor(type) != nullptr; }

bool DecodeBrainActionV1(const BrainGraph &graph, std::size_t node, BrainActionSpec &spec, std::string &reason) {
    reason.clear();
    try {
        const auto args = ReadBrainNodeArguments(graph, node);
        const auto *descriptor = FindDescriptor(args.type);
        if (!descriptor) {
            reason = "Brain node type " + std::to_string(args.type) + " has no SourceBrainActionsV1 handler";
            return false;
        }
        Need(args.primaryCondition == descriptor->primary, "Brain action embedded condition differs from its descriptor");
        BrainActionSpec out;
        out.nodeId = args.id;
        out.nodeType = args.type;
        out.kind = descriptor->kind;
        out.profile = descriptor->profile;
        out.subject.mode = BrainEntityMode::Player;
        out.subject.raw = "Player";
        for (const auto &value : args.values) {
            bool known = false;
            for (const auto &property : descriptor->properties) known = known || value.first == property.name;
            Need(known, "Brain action carries a property outside its descriptor table");
        }
        for (const auto &property : descriptor->properties) {
            const auto *value = args.Find(property.name);
            const std::string text = value ? *value : property.fallback;
            switch (property.kind) {
            case Property::Entity: out.subject = ParseBrainEntityReferenceV1(text); break;
            case Property::Bool:
                out.value = ParseBrainBoolTextV1(text);
                out.rawValue = text;
                out.valueDefaulted = value == nullptr;
                break;
            case Property::Text:
                Need(text.size() <= 1024 && (text.empty() || text[0] != '@'),
                     "Brain action text '@' resolution is outside SourceBrainActionsV1");
                out.text = text;
                out.textDefaulted = value == nullptr;
                break;
            case Property::Blend:
            case Property::Loops: {
                std::int32_t number = 0;
                Need(IntegerText(text, number), "Brain action integer text is not a canonical decimal");
                if (property.kind == Property::Blend) {
                    Need(number >= 0 && number <= 1000, "Brain action mBlendFrames is outside this profile");
                    out.blendFrames = number;
                    out.blendDefaulted = value == nullptr;
                } else {
                    Need(number >= -1 && number <= 100000, "Brain action mNumLoops is outside this profile");
                    out.numLoops = number;
                    out.loopsDefaulted = value == nullptr;
                }
                break;
            }
            case Property::Saved:
                if (value) out.savedState.push_back(std::make_pair(std::string(property.name), *value));
                break;
            }
        }
        spec = out;
        return true;
    } catch (const std::exception &error) {
        reason = error.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
struct BrainHeldStepper::Single : BrainFlowRandom {
    std::uint32_t Next15() override { throw std::runtime_error("SourceHeldStepV1 admits no weighted random choice"); }
};
namespace {
class Resolver : public BrainFlowConditions {
  public:
    explicit Resolver(BrainHeldStepHost &host) : host_(host) {}
    BrainConditionValue Primary(const BrainGraph &graph, std::size_t node) override { return host_.Primary(graph, node); }
    BrainConditionValue Explicit(const BrainGraph &graph, std::size_t record) override { return host_.Explicit(graph, record); }
  private:
    BrainHeldStepHost &host_;
};
struct Busy {
    bool &flag;
    explicit Busy(bool &value) : flag(value) { flag = true; }
    ~Busy() { flag = false; }
};
} // namespace

BrainHeldStepper::BrainHeldStepper(const BrainGraph &graph, BrainHeldStepHost &host) : graph_(graph), host_(host) {}

bool BrainHeldStepper::Prepare() {
    if (prepared_) return true;
    flow_ = MakeBrainFlowState(graph_);
    nodeFlags_.assign(graph_.nodes.size(), 0);
    std::vector<std::pair<std::uint32_t, std::size_t> > roots;
    for (std::size_t n = 0; n < graph_.nodes.size(); ++n) {
        std::uint32_t flags = 0;
        if (!ReadBrainTaskFlags(graph_, n, flags) || (flags & ~(1u | 0x10u | 0x20u))) {
            failed_ = true; failure_ = "SourceHeldStepV1 refuses unknown serialized task flags"; return false;
        }
        nodeFlags_[n] = flags;
        std::uint32_t priority = 0;
        if (host_.Root(graph_, n, priority)) {
            for (auto output : graph_.nodes[n].outputs)
                if (flow_.outputs[output].flags & 0x20u) {
                    failed_ = true; failure_ = "SourceHeldStepV1 refuses probe outputs on roots"; return false;
                }
            roots.push_back(std::make_pair(priority, n));
        }
    }
    std::stable_sort(roots.begin(), roots.end(),
                     [](const std::pair<std::uint32_t, std::size_t> &a, const std::pair<std::uint32_t, std::size_t> &b) {
                         return a.first < b.first;
                     });
    for (const auto &root : roots) {
        roots_.push_back(root.second);
        nodeFlags_[root.second] |= 0x8000u; // root entry at Initialize
    }
    prepared_ = true;
    return true;
}

// The destination and each of its probe destinations must be admitted leaves.
bool BrainHeldStepper::Admit(std::size_t node, std::size_t &refused, std::string &reason) {
    refused = node;
    if (!host_.Admits(graph_, node, reason)) return false;
    for (auto output : graph_.nodes[node].outputs) {
        if (!(flow_.outputs[output].flags & 0x20u)) continue;
        for (const auto &destination : graph_.outputs[output].destinations) {
            refused = destination.targetNode;
            if (!graph_.nodes[destination.targetNode].outputs.empty()) {
                reason = "Probe destination has outputs (outside the probe-leaf profile)";
                return false;
            }
            if (!host_.Admits(graph_, destination.targetNode, reason)) return false;
        }
    }
    refused = BrainRecord::None();
    return true;
}

void BrainHeldStepper::Enter(std::size_t node, bool probe) {
    auto &flags = nodeFlags_[node];
    if (probe) flags = (flags & ~0x50u) | 0x20u;
    else flags = (flags & ~0x60u) | 0x10u;
    flags |= 0x8000u;
    if (!(flags & 1u)) flags &= ~0x80000u;
    for (auto output : graph_.nodes[node].outputs)
        if (!(flow_.outputs[output].flags & 1u)) flow_.outputs[output].flags &= ~0x80000u;
    if (!(flags & 0x80000u)) host_.Enter(graph_, node, probe);
}

BrainHeldStepResult BrainHeldStepper::Tick(std::size_t transitionBudget) {
    BrainHeldStepResult result;
    if (busy_ || failed_ || transitionBudget == 0 || transitionBudget > 50) {
        if (!failed_) { failed_ = true; failure_ = busy_ ? "Recursive held stepper entry" : "Unsupported transition budget"; }
        result.status = BrainHeldStepResult::Failed;
        result.reason = failure_;
        return result;
    }
    Busy busy(busy_);
    try {
        if (!Prepare()) { result.status = BrainHeldStepResult::Failed; result.reason = failure_; return result; }
        Resolver resolver(host_);
        Single random;
        bool checkRoots = true;
        for (;;) {
            BrainFlowDecision decision;
            BrainFlowState next;
            std::size_t source = BrainRecord::None();
            if (checkRoots) {
                for (auto root : roots_) {
                    if ((nodeFlags_[root] & 0x80000u) && (nodeFlags_[root] & 1u)) continue;
                    const auto ready = host_.RootReady(graph_, root);
                    Need(ready == BrainConditionValue::True || ready == BrainConditionValue::False,
                         "SourceHeldStepV1 root predicate is unsupported");
                    if (ready == BrainConditionValue::False) continue;
                    next = flow_;
                    decision = SelectBrainTransition(graph_, root, next, resolver, random);
                    if (decision.status != BrainFlowDecision::NoTransition) { source = root; break; }
                }
            }
            if (decision.status == BrainFlowDecision::NoTransition && active_ != BrainRecord::None()) {
                source = active_;
                next = flow_;
                decision = SelectBrainTransition(graph_, source, next, resolver, random);
            }
            if (decision.status == BrainFlowDecision::Unsupported) {
                result.status = BrainHeldStepResult::Held;
                result.heldSource = source;
                result.heldNode = decision.targetNode;
                result.reason = decision.reason.empty() ? "Unsupported output or condition" : decision.reason;
                return result;
            }
            const bool transitioned = decision.status == BrainFlowDecision::Transition;
            if (transitioned) {
                Need(result.transitions < transitionBudget, "SourceHeldStepV1 transition budget exceeded");
                std::size_t refused = BrainRecord::None();
                std::string why;
                if (!Admit(decision.targetNode, refused, why)) {
                    // Hold: nothing committed, no exit and no side effect.
                    result.status = BrainHeldStepResult::Held;
                    result.heldSource = source;
                    result.heldNode = refused;
                    result.reason = why;
                    return result;
                }
                flow_ = std::move(next);
                // The previous active task's Exit is a no-op for every admitted
                // action type. A root's own Exit follows the entry (OnUsed
                // Exit 0x201e6ee0 clears the matched key after its transition).
                const bool fromRoot = source != active_;
                active_ = decision.targetNode;
                Enter(active_, false);
                nodeFlags_[source] |= 0x80000u;
                if (fromRoot) host_.ExitRoot(graph_, source);
                ++result.transitions;
                checkRoots = false;
            }
            // Probe pass on the current active task, then flush it.
            if (active_ != BrainRecord::None()) {
                BrainFlowState marked = flow_;
                std::size_t count = 0;
                const auto pass = MarkBrainProbeOutputs(graph_, active_, marked, resolver, count);
                Need(pass.status == BrainFlowDecision::NoTransition, pass.reason.empty() ? "Unsupported probe pass" : pass.reason.c_str());
                flow_ = std::move(marked);
                for (auto output : graph_.nodes[active_].outputs) {
                    if (!(flow_.outputs[output].flags & 0x40000u)) continue;
                    const auto fired = FireBrainMarkedProbeOutput(graph_, output, flow_, random);
                    Need(fired.status == BrainFlowDecision::Transition, fired.reason.empty() ? "Unsupported probe flush" : fired.reason.c_str());
                    std::size_t refused = BrainRecord::None();
                    std::string why;
                    Need(Admit(fired.targetNode, refused, why), "Probe destination lost its admission after the pre-check");
                    Enter(fired.targetNode, true);
                    ++result.probes;
                }
            }
            if (!transitioned) break;
        }
        if (result.transitions || result.probes) result.status = BrainHeldStepResult::Advanced;
        return result;
    } catch (const std::exception &error) {
        failed_ = true;
        failure_ = error.what();
        result.status = BrainHeldStepResult::Failed;
        result.reason = failure_;
        return result;
    }
}
} // namespace wolf2009

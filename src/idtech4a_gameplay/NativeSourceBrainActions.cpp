// SPDX-License-Identifier: GPL-3.0-or-later
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "BrainActions.h"
#include "SourceInventory.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceBrainActions.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceScriptedVoiceReadiness.h"

// Wolf player events posted by EnableCredits (0x201e2160); their idEventDef
// static initializers carry these names. The GPL player has no handler.
static const idEventDef EV_WolfEnableCreditsScreen("enableCreditsScreen");
static const idEventDef EV_WolfDisableCreditsScreen("disableCreditsScreen");

namespace {
int liveVoiceStreamOwners = 0;
// Entity +0x159 bit 7, keyed by native spawn id (one generation per entity).
std::map<int, bool> usableBySpawnId;
idEntity *SubjectEntity(const WolfSourceSubject &subject) {
    return subject.kind == WolfSourceSubject::AI ? subject.ai->NativeBody() : subject.entity;
}
const char *const VoiceLocale = "english"; // only qualified scripted-voice locale

std::string SubjectName(const WolfSourceSubject &subject) {
    switch (subject.kind) {
    case WolfSourceSubject::AI: return subject.ai->SourceName();
    case WolfSourceSubject::Player:
    case WolfSourceSubject::Entity: return subject.entity ? subject.entity->GetName() : "-";
    case WolfSourceSubject::Null: break;
    }
    return "-";
}
void Finish(WolfSourceBrainActionReceipt &receipt, const char *outcome, const std::string &effect) {
    receipt.outcome = outcome;
    receipt.effect = effect;
    gameLocal.Printf("WOLF2009_SOURCE_BRAIN_ACTION brain=%s node=%u type=%u action=%s profile=%s probe=%d subject=%s "
                     "outcome=%s time=%d effect=\"%s\"\n",
                     receipt.brain.empty() ? "-" : receipt.brain.c_str(), receipt.node, receipt.type,
                     receipt.action.c_str(), receipt.profile.c_str(), int(receipt.probe), receipt.subject.c_str(),
                     outcome, receipt.timeMs, effect.c_str());
}

// SetScriptedManual (Enter 0x201f7190 -> idAI 0x201ac600).
void ScriptedManual(WolfSourceAIActionTarget &ai, bool enable, WolfSourceBrainActionReceipt &receipt) {
    auto &s = ai.Script();
    if (ai.Dead()) { Finish(receipt, "no-op", "AI is dead (ai+0x954 bit 0x4)"); return; }
    if (s.scriptedManual == enable) {
        ++s.manualNoops;
        Finish(receipt, "no-op", enable ? "already scripted manual" : "already out of scripted manual");
        return;
    }
    if (enable) {
        std::string shown = "visible";
        if (ai.Hidden()) {
            if (ai.PostSpawnPending()) shown = "hidden; post-spawn flag set, not shown";
            else {
                ai.ScriptShow();
                ++s.scriptShows;
                shown = "shown (hidden AI, post-spawn flag clear)";
            }
        }
        // +0x4e0 stop runs only without a vehicle and outside the game-state
        // check; this actor profile has no vehicle and no native locomotion.
        s.movementStop = "no native locomotion to stop (no vehicle)";
        s.ignoreEnemies = true;
        s.scriptedManual = true;
        s.announcementsOff = true;
        s.idleLookForEnemy = false;
        s.postedStates.push_back("Wait_ScriptedManual");
        s.postedStates.push_back("initial-state(+0x76c)");
        ++s.manualEnters;
        Finish(receipt, "applied", "scripted manual on; " + shown +
                                   "; announcements off; ignore enemies; idle look-for-enemy off; enemy cleared; "
                                   "posted Wait_ScriptedManual and the initial state (state thread not native)");
        return;
    }
    s.ignoreEnemies = false;
    s.scriptedManual = false;
    s.announcementsOff = s.spawnDisableAnnouncements;
    s.idleLookForEnemy = s.spawnIdleLookForEnemy;
    ++s.manualLeaves;
    Finish(receipt, "applied", std::string("scripted manual off; disableAnnouncements=") +
                               (s.announcementsOff ? "1" : "0") + " idleLookForEnemy=" + (s.idleLookForEnemy ? "1" : "0") +
                               " restored from spawn args");
}
} // namespace

bool Wolf2009_ApplySourceBrainAction(const wolf2009::BrainActionSpec &spec, WolfSourceBrainActionResolver &resolver,
                                     WolfSourceBrainActionReceipt &receipt) {
    receipt.node = spec.nodeId;
    receipt.type = spec.nodeType;
    receipt.action = wolf2009::BrainActionKindName(spec.kind);
    receipt.profile = spec.profile;
    receipt.timeMs = gameLocal.time;
    try {
        if (spec.kind == wolf2009::BrainActionKind::ScriptedVOStreamIn) {
            receipt.subject = spec.subject.raw;
            Finish(receipt, "refused", "type19 is a stream request (WolfSourceVoiceStreams), not an immediate action");
            return false;
        }
        const auto subject = resolver.Resolve(spec.subject);
        receipt.subject = SubjectName(subject);
        switch (spec.kind) {
        case wolf2009::BrainActionKind::SetScriptedManual:
            if (subject.kind != WolfSourceSubject::AI) {
                Finish(receipt, subject.kind == WolfSourceSubject::Null ? "null-subject" : "no-op", "subject is not an idAI");
                return true;
            }
            ScriptedManual(*subject.ai, spec.value, receipt);
            return true;
        case wolf2009::BrainActionKind::AnimOverrideIdle:
            if (subject.kind != WolfSourceSubject::AI) {
                Finish(receipt, subject.kind == WolfSourceSubject::Null ? "null-subject" : "no-op", "subject is not an idAI");
                return true;
            }
            subject.ai->Script().idleOverride = spec.text;
            subject.ai->Script().idleStale = true;
            Finish(receipt, "applied", "idle override=\"" + spec.text + "\"; idle marked stale (ai+0x95b bit 0x80)");
            return true;
        case wolf2009::BrainActionKind::AnimOverrideDeath:
            if (subject.kind != WolfSourceSubject::AI) {
                Finish(receipt, subject.kind == WolfSourceSubject::Null ? "null-subject" : "no-op", "subject is not an idAI");
                return true;
            }
            subject.ai->Script().deathOverride = spec.text;
            Finish(receipt, "applied", "death override=\"" + spec.text + "\" stored (ai+0x388c); consumer outside this profile");
            return true;
        case wolf2009::BrainActionKind::NoTargetSet: {
            idEntity *entity = subject.kind == WolfSourceSubject::AI ? subject.ai->NativeBody() : subject.entity;
            if (!entity) { Finish(receipt, "null-subject", "no entity"); return true; }
            entity->fl.notarget = spec.value;
            if (subject.kind == WolfSourceSubject::AI) subject.ai->Script().notarget = spec.value;
            Finish(receipt, "applied", std::string("native fl.notarget=") + (spec.value ? "1" : "0") + " (raw \"" +
                                       spec.rawValue + "\"); no native source-AI acquisition consumes it yet");
            return true;
        }
        case wolf2009::BrainActionKind::SetUndying:
            if (subject.kind == WolfSourceSubject::AI) {
                subject.ai->Script().undying = spec.value;
                Finish(receipt, "applied", std::string("undying=") + (spec.value ? "1" : "0") +
                                           " recorded (actor+0x5f9); no native source-AI damage consumer");
                return true;
            } else if (subject.kind == WolfSourceSubject::Player && subject.entity) {
                resolver.PlayerState().undying = spec.value;
                auto *flags = Wolf2009_SourcePlayerFlags(subject.entity);
                if (!flags) { Finish(receipt, "refused", "player is not a native source player"); return false; }
                flags->undying = spec.value;
                ++flags->undyingWrites;
                Finish(receipt, "applied", std::string("player undying=") + (spec.value ? "1" : "0") +
                                           " (actor+0x5f9); native player Damage keeps health at 1");
                return true;
            }
            Finish(receipt, subject.kind == WolfSourceSubject::Null ? "null-subject" : "no-op", "subject is not an idActor");
            return true;
        case wolf2009::BrainActionKind::EnablePlayerWeapons: {
            auto *player = gameLocal.GetLocalPlayer();
            if (!player) { Finish(receipt, "null-subject", "no local player"); return true; }
            receipt.subject = player->GetName();
            auto &state = resolver.PlayerState();
            auto *flags = Wolf2009_SourcePlayerFlags(player);
            state.weaponsLocked = !spec.value;
            state.lastWeaponEvent = spec.value ? "weapons-enabled" : "weapons-disabled";
            state.weaponEventHandled = flags != nullptr;
            if (!flags) { Finish(receipt, "refused", "local player is not a native source player"); return false; }
            // player+0x958 bit 3 = !mEnabled. Consumers: the source weapon's
            // attack input and idItem CanPickup/GiveItem for inv_weapon items.
            flags->weaponsDisabled = !spec.value;
            ++flags->weaponsDisabledWrites;
            Finish(receipt, "applied", std::string("player+0x958 bit3=") + (spec.value ? "0" : "1") +
                                       "; gates source weapon attack input and inv_weapon pickups; GPL weapon events not sent");
            return true;
        }
        case wolf2009::BrainActionKind::EnableDamage: {
            idEntity *entity = SubjectEntity(subject);
            if (!entity) { Finish(receipt, "null-subject", "no entity"); return true; }
            // ent+0x158 bit 3 = GPL fl.takedamage; native Damage returns early when clear.
            entity->fl.takedamage = spec.value;
            Finish(receipt, "applied", std::string("native fl.takedamage=") + (spec.value ? "1" : "0") + " (raw \"" +
                                       spec.rawValue + "\")");
            return true;
        }
        case wolf2009::BrainActionKind::SetUsable: {
            idEntity *entity = SubjectEntity(subject);
            if (!entity) { Finish(receipt, "null-subject", "no entity"); return true; }
            Wolf2009_SetSourceEntityUsable(entity, spec.value);
            Finish(receipt, "applied", std::string("ent+0x159 bit7 usable=") + (spec.value ? "1" : "0") +
                                       "; player use-focus selection (0x203142a0) is not native");
            return true;
        }
        case wolf2009::BrainActionKind::LockPlayer:
        case wolf2009::BrainActionKind::UnlockPlayer: {
            auto *player = gameLocal.GetLocalPlayer();
            if (!player) { Finish(receipt, "null-subject", "no local player"); return true; }
            receipt.subject = player->GetName();
            auto *flags = Wolf2009_SourcePlayerFlags(player);
            if (!flags) { Finish(receipt, "refused", "local player is not a native source player"); return false; }
            const bool lock = spec.kind == wolf2009::BrainActionKind::LockPlayer;
            flags->locked = lock;
            ++flags->lockWrites;
            resolver.PlayerState().locked = lock;
            Finish(receipt, "applied", std::string("player+0x957 bit 0x10=") + (lock ? "1" : "0") +
                                       "; crouch toggle gated; other suppressed input fields refused (not mapped)");
            return true;
        }
        case wolf2009::BrainActionKind::EnableCredits: {
            auto *player = gameLocal.GetLocalPlayer();
            if (!player) { Finish(receipt, "null-subject", "no local player"); return true; }
            receipt.subject = player->GetName();
            const idEventDef &event = spec.value ? EV_WolfEnableCreditsScreen : EV_WolfDisableCreditsScreen;
            auto &state = resolver.PlayerState();
            state.creditsHandled = player->ProcessEvent(&event);
            state.lastCreditsEvent = event.GetName();
            ++state.creditsEvents;
            Finish(receipt, "applied", std::string("processed ") + event.GetName() + " on the local player; handled=" +
                                       (state.creditsHandled ? "1" : "0") + (state.creditsHandled ? "" : " (no native credits UI)"));
            return true;
        }
        case wolf2009::BrainActionKind::KillTarget: {
            // 0x201ed6b0: an idAI target receives <kill>(mBypassDeathAnim).
            if (subject.kind == WolfSourceSubject::Null) { Finish(receipt, "null-subject", "no entity"); return true; }
            if (subject.kind != WolfSourceSubject::AI) {
                Finish(receipt, "refused", "non-idAI kill virtual +0x218 is outside SourceAIKillTargetV1");
                return false;
            }
            if (!subject.ai->ScriptedAnimAdmitted()) {
                Finish(receipt, "refused", "actor has no SourceAIScriptedAnimV1 death path");
                return false;
            }
            std::string effect;
            const bool ok = subject.ai->Kill(spec.value, effect);
            Finish(receipt, ok ? "applied" : "refused", effect);
            return ok;
        }
        case wolf2009::BrainActionKind::Animate:
            Finish(receipt, "refused", "Animate owns task state; use Wolf2009_EnterSourceAnimate");
            return false;
        case wolf2009::BrainActionKind::ScriptedVOStreamIn: break;
        }
        Finish(receipt, "refused", "unadmitted action");
        return false;
    } catch (const std::exception &error) {
        Finish(receipt, "refused", error.what());
        return false;
    }
}

bool Wolf2009_EnterSourceAnimate(const wolf2009::BrainActionSpec &spec, WolfSourceBrainActionResolver &resolver,
                                 WolfSourceBrainActionReceipt &receipt, WolfSourceAnimateTask &task, const std::string &label) {
    receipt.node = spec.nodeId;
    receipt.type = spec.nodeType;
    receipt.action = wolf2009::BrainActionKindName(spec.kind);
    receipt.profile = spec.profile;
    receipt.timeMs = gameLocal.time;
    task = WolfSourceAnimateTask();
    task.entered = true;
    task.enterTimeMs = gameLocal.time;
    task.succeeded = false;
    task.failTimeMs = gameLocal.time + 1;
    try {
        if (spec.kind != wolf2009::BrainActionKind::Animate) {
            Finish(receipt, "refused", "not an Animate node");
            return false;
        }
        const auto subject = resolver.Resolve(spec.subject);
        receipt.subject = SubjectName(subject);
        task.subject = receipt.subject;
        if (subject.kind == WolfSourceSubject::Null) {
            Finish(receipt, "null-subject", "no entity; AnimFinished completes on the next later tick");
            return true;
        }
        if (subject.kind != WolfSourceSubject::AI) {
            Finish(receipt, "refused",
                   "rvVehicle/idAnimated Animate branch is outside SourceAIScriptedAnimV1 (trainyard does not use it)");
            return false;
        }
        task.subjectAI = true;
        if (!subject.ai->ScriptedAnimAdmitted()) {
            Finish(receipt, "refused", "actor has no SourceAIScriptedAnimV1 clip profile");
            return false;
        }
        std::string effect;
        task.succeeded = subject.ai->ScriptedAnim(spec.text, spec.blendFrames, spec.numLoops, spec.value, label, effect);
        Finish(receipt, "applied", std::string("mAnimSucceeded=") + (task.succeeded ? "1" : "0") + " mAnimFailTime=" +
                                   std::to_string(task.failTimeMs) + "; " + effect);
        return true;
    } catch (const std::exception &error) {
        Finish(receipt, "refused", error.what());
        return false;
    }
}

bool Wolf2009_EvaluateSourceAnimFinished(const wolf2009::BrainActionSpec &spec, WolfSourceBrainActionResolver &resolver,
                                         WolfSourceAnimateTask &task) {
    const int now = gameLocal.time;
    bool value = false;
    int remaining = -1;
    std::string current, detail;
    if (!task.succeeded) {
        value = now > task.failTimeMs;
        detail = "mAnimSucceeded false: now > mAnimFailTime";
    } else {
        const auto subject = resolver.Resolve(spec.subject);
        if (subject.kind == WolfSourceSubject::AI) {
            const bool done = subject.ai->AnimDoneByName(spec.text, spec.blendFrames, remaining, current);
            value = now > task.failTimeMs && done;
            detail = "idAI: now > mAnimFailTime and +0x33c";
        } else {
            value = true;
            detail = "no resolvable idAI";
        }
    }
    ++task.evaluations;
    task.lastValue = value;
    task.lastRemainingMs = remaining;
    task.lastCurrent = current;
    task.lastDetail = detail;
    if (!value) {
        task.lastFalseRemainingMs = remaining;
    } else if (task.firstTrueMs < 0) {
        task.firstTrueMs = now;
        task.firstTrueRemainingMs = remaining;
        gameLocal.Printf("WOLF2009_SOURCE_AI_ANIM_FINISHED node=%u clip=%s subject=%s value=1 time=%d enter=%d remaining=%d "
                         "previous_remaining=%d current=%s blend_frames=%d evaluations=%u detail=\"%s\"\n",
                         spec.nodeId, spec.text.c_str(), task.subject.c_str(), now, task.enterTimeMs, remaining,
                         task.lastFalseRemainingMs, current.empty() ? "-" : current.c_str(), spec.blendFrames,
                         task.evaluations, detail.c_str());
    }
    return value;
}

bool Wolf2009_UpdateSourceAIIdle(WolfSourceAIActionTarget &ai, bool (*declared)(const void *, const std::string &),
                                 const void *context) {
    auto &s = ai.Script();
    if (ai.Dead() || !s.idleStale) return false;
    s.idleStale = false;
    ++s.idleConsumptions;
    if (s.idleOverride.empty()) {
        s.currentIdle.clear();
        s.idleClipDeclared = false;
        s.idlePlayback = "refused: default idle name (+0x6a8 without override) is not recovered";
    } else {
        s.currentIdle = s.idleOverride;
        s.idleClipDeclared = declared && declared(context, s.currentIdle);
        s.idlePlayback = s.idleClipDeclared
                             ? "refused: SourceAIShellV1 presents the reference pose; clip playback is SourceAIScriptedAnimV1"
                             : "refused: idle clip is not declared by the actor's model animations";
    }
    gameLocal.Printf("WOLF2009_SOURCE_AI_IDLE actor=%s consumed=%u current_idle=%s declared=%d time=%d playback=\"%s\"\n",
                     ai.SourceName().c_str(), s.idleConsumptions, s.currentIdle.empty() ? "-" : s.currentIdle.c_str(),
                     int(s.idleClipDeclared), gameLocal.time, s.idlePlayback.c_str());
    return true;
}

void Wolf2009_SetSourceEntityUsable(idEntity *entity, bool usable) {
    if (entity) usableBySpawnId[gameLocal.GetSpawnId(entity)] = usable;
}
int Wolf2009_SourceEntityUsable(const idEntity *entity) {
    if (!entity) return -1;
    const auto found = usableBySpawnId.find(gameLocal.GetSpawnId(entity));
    return found == usableBySpawnId.end() ? -1 : int(found->second);
}
void Wolf2009_ClearSourceEntityFlags() { usableBySpawnId.clear(); }
int Wolf2009_LiveSourceEntityFlags() { return int(usableBySpawnId.size()); }

bool Wolf2009_EvaluateSourceBrainCondition(const wolf2009::BrainConditionSpec &spec, WolfSourceConditionReceipt &receipt) {
    receipt.record = spec.record;
    receipt.profile = spec.profile;
    receipt.condition = wolf2009::BrainConditionKindName(spec.kind);
    receipt.argument = spec.text;
    receipt.timeMs = gameLocal.time;
    receipt.raw = false;
    if (spec.kind != wolf2009::BrainConditionKind::PlayerHasWeapon || spec.flags != 0) {
        receipt.outcome = "refused";
        receipt.reason = "condition outside SourceBrainConditionsV1";
        return false;
    }
    auto *player = gameLocal.GetLocalPlayer();
    if (!player) { receipt.outcome = "no-player"; receipt.reason = "no local player: raw false"; return true; }
    const auto *inventory = Wolf2009_SourcePlayerInventory(static_cast<const idEntity *>(player));
    if (!inventory) { receipt.outcome = "refused"; receipt.reason = "local player has no source inventory"; return false; }
    receipt.raw = inventory->HasWeapon(spec.text);
    receipt.outcome = "evaluated";
    receipt.reason = "inventory slot +0x90: ownership of the slot named by mWeaponName";
    return true;
}

std::uint32_t Wolf2009_MeasuredScriptedVoiceMembers(const std::string &definition) {
    if (definition == "scripted_voice_wi1_trn") return 11;
    if (definition == "scripted_voice_wi2_trn") return 9;
    if (definition == "scripted_voice_kc1_trn") return 19;
    return 0;
}

struct WolfSourceVoiceStreams::Impl {
    struct Stream {
        WolfSourceVoiceStreamReceipt receipt;
        std::unique_ptr<WolfScriptedVoiceReadinessRequest> request;
    };
    std::vector<Stream> streams;
};

WolfSourceVoiceStreams::WolfSourceVoiceStreams() : impl_(new Impl) { ++liveVoiceStreamOwners; }
WolfSourceVoiceStreams::~WolfSourceVoiceStreams() {
    impl_.reset();
    --liveVoiceStreamOwners;
}

void WolfSourceVoiceStreams::Start(const wolf2009::BrainActionSpec &spec, const std::string &brainResource,
                                   idEntity *owner, std::uint64_t levelGeneration, std::uint64_t eventId) {
    Impl::Stream stream;
    auto &r = stream.receipt;
    r.brain = brainResource;
    r.node = spec.nodeId;
    r.definition = spec.text;
    r.owner = owner ? owner->GetName() : spec.subject.raw;
    r.expectedMembers = Wolf2009_MeasuredScriptedVoiceMembers(spec.text);
    r.requestTimeMs = gameLocal.time;
    if (!owner) {
        r.state = "owner-absent";
        r.reason = "ScriptedVOStreamIn: Can't find ent '" + spec.subject.raw + "'; no stream requested";
    } else if (spec.text.empty()) {
        r.state = "refused";
        r.reason = "Empty mDefName (def_scripted_voice fallback) is outside this profile";
    } else {
        WolfScriptedVoiceReadinessSource source;
        source.brainResource = brainResource;
        source.definition = spec.text;
        source.locale = VoiceLocale;
        source.nodeId = spec.nodeId;
        source.nodeType = 19;
        source.levelGeneration = levelGeneration;
        source.eventId = eventId;
        std::string why;
        stream.request = Wolf2009_RequestScriptedVoiceReadiness("ScriptedVOStreamInWholeBankV1", source, owner, why);
        if (!stream.request) {
            r.state = "refused";
            r.reason = why;
        } else {
            r.state = Wolf2009_ScriptedVoiceReadinessText(stream.request->Status());
            r.reason = stream.request->Reason();
            r.bankBacked = stream.request->BankBacked();
        }
    }
    gameLocal.Printf("WOLF2009_SOURCE_AI_VO_STREAMIN brain=%s node=%u definition=%s owner=%s expected_members=%u "
                     "state=%s bank_backed=%d time=%d waits=0 reason=\"%s\"\n",
                     r.brain.c_str(), r.node, r.definition.c_str(), r.owner.c_str(), r.expectedMembers, r.state.c_str(),
                     int(r.bankBacked), r.requestTimeMs, r.reason.c_str());
    impl_->streams.push_back(std::move(stream));
}

void WolfSourceVoiceStreams::Update() {
    for (auto &stream : impl_->streams) {
        if (!stream.request) continue;
        auto &r = stream.receipt;
        const auto status = stream.request->Update();
        if (status == WolfScriptedVoiceReadinessStatus::Ready && !r.readyConsumed) {
            if (!stream.request->ConsumeReady()) continue;
            r.readyConsumed = true;
            r.readyTimeMs = gameLocal.time;
            r.bankGeneration = stream.request->BankGeneration();
            r.members = stream.request->BankMembers();
            r.inventoryQualified = !r.expectedMembers || r.members == r.expectedMembers;
            r.state = "ready";
            if (!r.inventoryQualified) {
                r.reason = "Bank inventory differs from the measured pack (" + std::to_string(r.members) + " of " +
                           std::to_string(r.expectedMembers) + " members)";
                stream.request->Cancel(r.reason.c_str());
                stream.request.reset();
                r.state = "inventory-refused";
            }
            gameLocal.Printf("WOLF2009_SOURCE_AI_VO_READY brain=%s node=%u definition=%s owner=%s ready_consumed=1 "
                             "bank_generation=%llu members=%u expected=%u qualified=%d time=%d\n",
                             r.brain.c_str(), r.node, r.definition.c_str(), r.owner.c_str(),
                             static_cast<unsigned long long>(r.bankGeneration), r.members, r.expectedMembers,
                             int(r.inventoryQualified), r.readyTimeMs);
            continue;
        }
        if (status == WolfScriptedVoiceReadinessStatus::Failed || status == WolfScriptedVoiceReadinessStatus::Lost ||
            status == WolfScriptedVoiceReadinessStatus::Cancelled) {
            r.state = Wolf2009_ScriptedVoiceReadinessText(status);
            r.reason = stream.request->Reason();
            stream.request.reset();
            gameLocal.Printf("WOLF2009_SOURCE_AI_VO_ENDED brain=%s node=%u definition=%s owner=%s state=%s "
                             "ready_consumed=%d time=%d reason=\"%s\"\n",
                             r.brain.c_str(), r.node, r.definition.c_str(), r.owner.c_str(), r.state.c_str(),
                             int(r.readyConsumed), gameLocal.time, r.reason.c_str());
        }
    }
}

void WolfSourceVoiceStreams::CancelAll(const char *reason) {
    for (auto &stream : impl_->streams) {
        if (!stream.request) continue;
        stream.request->Cancel(reason);
        stream.receipt.state = "cancelled";
        stream.receipt.reason = reason ? reason : "";
        stream.request.reset();
    }
}

std::vector<WolfSourceVoiceStreamReceipt> WolfSourceVoiceStreams::Receipts() const {
    std::vector<WolfSourceVoiceStreamReceipt> view;
    for (const auto &stream : impl_->streams) view.push_back(stream.receipt);
    return view;
}

unsigned WolfSourceVoiceStreams::Live() const {
    unsigned live = 0;
    for (const auto &stream : impl_->streams) live += stream.request ? 1u : 0u;
    return live;
}

int Wolf2009_LiveSourceVoiceStreams() { return liveVoiceStreamOwners; }

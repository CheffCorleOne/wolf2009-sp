// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "NativeSourceAIShell.h"
#include "NativeSourceTrainyardExit.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceAIShell.h"
#include "NativeSourceDirector.h"
#include "NativeSourceMap.h"
#include "NativeSourceTrainyardExit.h"
#include "SourceSession.h"

namespace {
const char* const First = "nazi_interragator_1";
const char* const Second = "nazi_interragator_2";
const char* const Hostage1 = "trainyard_hostage_1";
const char* const Hostage2 = "trainyard_hostage_2";
const char* const Greeter = "train_greeter";
const char* const ExitKey = "player_leaves_train";

struct Accessory {
    const char* asset;
    const char* joint;
    bool onHead;
    idVec3 origin;
    idAngles angles;
};
const Accessory Helmet = {"helmet_ewm_inf", "head", true, idVec3(0, 5.7f, -.2f), idAngles(8, 90, -90)};
const Accessory Hat = {"hat_civ_01", "head", true, idVec3(-.06f, 6.1f, .11f), idAngles(6.76f, 90.2f, -93.15f)};
const Accessory Holster = {"holster_luger", "chest", false, idVec3(4.88f, -2.4f, 4.33f), idAngles(33.88f, 9.56f, -67.48f)};
const Accessory Pouch = {"pouch_faction", "hips", false, idVec3(8.29f, 1.09f, .53f), idAngles(-81.22f, -148.06f, -126.48f)};
struct Actor {
    const char* name;
    const char* body;
    const char* head;
    int headJoints;
    idVec3 origin;
    float yaw;
    bool hidden, brain;
    const Accessory* accessories[2];
};
const Actor Actors[] = {
    {First, "body_ewm_inf_v2", "head_em02", 32, idVec3(64, 628, 296), 270, true, true, {&Helmet, nullptr}},
    {Second, "body_ewm_inf_v2", "head_em02", 32, idVec3(160, 684, 296), 270, true, false, {&Helmet, nullptr}},
    {Hostage1, "body_cm02", "head_abm_agt02", 27, idVec3(72, 728, 296), 270, true, false, {&Holster, &Hat}},
    {Hostage2, "body_cm04", "head_civilian_male_03_mesh", 27, idVec3(120, 728, 296), 270, true, false, {&Pouch, nullptr}},
    {Greeter, "body_cm04", "head_civilian_male_03_mesh", 27, idVec3(176.75f, 271.75f, 296), 0, false, true, {&Pouch, nullptr}},
};
struct QA {
    unsigned checks=0, frames=0, snapshots=0;
    std::set<int> identities;
    void Need(bool ok, const std::string& reason) {
        ++checks;
        if (!ok) throw std::runtime_error(reason);
    }
    void NeedReason(bool ok, const idStr& reason, const char* fallback) {
        Need(ok, reason.IsEmpty() ? fallback : reason.c_str());
    }
    void Identity(idEntity* entity, const std::string& context) {
        Need(entity && gameLocal.FindEntity(entity->GetName()) == entity &&
             identities.insert(gameLocal.GetSpawnId(entity)).second, context + ": invalid native identity");
    }
};
struct Pose { idVec3 origin; idMat3 axis; };
Pose RenderPose(idEntity* entity) {
    const auto* render = entity->GetRenderEntity();
    return {render->origin, render->axis};
}
void Near(QA& qa, const Pose& actual, const Pose& expected, const std::string& context) {
    qa.Need(actual.origin.Compare(expected.origin, .01f) && actual.axis.Compare(expected.axis, .001f),
            context + ": native world pose differs");
}
Pose WorldJoint(QA& qa, idEntity* entity, const char* name) {
    auto* animator = entity->GetAnimator();
    qa.Need(animator != nullptr, std::string(entity->GetName()) + ": animator absent");
    const auto joint = animator->GetJointHandle(name);
    idVec3 translation;
    idMat3 rotation;
    qa.Need(joint != INVALID_JOINT && animator->GetJointTransform(joint, gameLocal.time, translation, rotation),
            std::string(entity->GetName()) + ": native joint unavailable: " + name);
    const auto parent = RenderPose(entity);
    return {parent.origin + translation * parent.axis, rotation * parent.axis};
}
void Binding(QA& qa, idEntity* child, idEntity* master, const char* joint) {
    auto* animator = master->GetAnimator();
    qa.Need(animator != nullptr, "Bind master animator absent");
    const auto expected = animator->GetJointHandle(joint);
    qa.Need(expected != INVALID_JOINT && child->GetBindMaster() == master && child->GetBindJoint() == expected,
            std::string(child->GetName()) + ": wrong native bind master or joint");
}
struct FreeModel {
    void operator()(idRenderModel* model) const { renderModelManager->FreeModel(model); }
};
void Mesh(QA& qa, idEntity* entity, const char* token, int joints, bool presented) {
    auto* render = entity->GetRenderEntity();
    auto* model = render ? render->hModel : nullptr;
    qa.Need(model && model->IsLoaded() && !model->IsDefaultModel() && model->NumJoints() == joints &&
            idStr::FindText(model->Name(), token, false) >= 0,
            std::string(entity->GetName()) + ": wrong or absent source model: " + token);
    if (!presented) return;

    const idRenderModel* visible = model;
    std::unique_ptr<idRenderModel, FreeModel> snapshot;
    if (joints) {
        qa.Need(render->numJoints == joints && render->joints,
                std::string(entity->GetName()) + ": ordinary frames did not present a joint palette");
        auto* posed = model->InstantiateDynamicModel(render, nullptr, nullptr);
        qa.Need(posed && posed != model, std::string(entity->GetName()) + ": native skinned snapshot failed");
        snapshot.reset(posed);
        visible = posed;
        ++qa.snapshots;
    }
    unsigned triangles = 0;
    for (int i = 0; i < visible->NumSurfaces(); ++i) {
        const auto* surface = visible->Surface(i);
        if (!surface || !surface->geometry) continue;
        const auto* geometry = surface->geometry;
        qa.Need(geometry->numVerts > 0 && geometry->verts && geometry->numIndexes > 0 &&
                geometry->indexes && geometry->numIndexes % 3 == 0, "Invalid native triangle surface");
        bool finite = true;
        for (int vertex = 0; vertex < geometry->numVerts; ++vertex) {
            const auto& pos = geometry->verts[vertex].xyz;
            finite = finite && std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z);
        }
        qa.Need(finite, std::string(entity->GetName()) + ": nonfinite posed vertex");
        triangles += unsigned(geometry->numIndexes / 3);
    }
    qa.Need(triangles > 0, std::string(entity->GetName()) + ": no original native triangles");
}
void Appearance(QA& qa, const WolfSourceAIShell& shell, const Actor& actor, bool initial) {
    auto* body = shell.Body(actor.name);
    auto* head = shell.Head(actor.name);
    const bool hidden = initial && actor.hidden;
    qa.Need(body && head && body != head && gameLocal.FindEntity(actor.name) == body,
            std::string(actor.name) + ": body or separate head unavailable");
    if (initial) {
        qa.Identity(body, actor.name);
        qa.Identity(head, actor.name);
    }
    qa.Need(body->IsHidden() == hidden && head->IsHidden() == hidden,
            std::string(actor.name) + ": native body and head visibility differ");
    Near(qa, {body->GetPhysics()->GetOrigin(), body->GetPhysics()->GetAxis()},
         {actor.origin, idAngles(0, actor.yaw, 0).ToMat3()}, actor.name);
    Mesh(qa, body, actor.body, 52, !initial);
    Mesh(qa, head, actor.head, actor.headJoints, !initial);
    Binding(qa, head, body, "chest");
    if (!initial) {
        Near(qa, RenderPose(head), WorldJoint(qa, body, "chest"), std::string(actor.name) + " chest/head");
        for (const char* joint : {"neck", "shoulders", "head"})
            Near(qa, WorldJoint(qa, head, joint), WorldJoint(qa, body, joint),
                 std::string(actor.name) + " copied " + joint);
    }
    const unsigned count = actor.accessories[1] ? 2 : 1;
    qa.Need(shell.AccessoryCount(actor.name) == count && !shell.Accessory(actor.name, count),
            std::string(actor.name) + ": unexpected accessory count");
    for (unsigned i = 0; i < count; ++i) {
        const auto& source = *actor.accessories[i];
        auto* child = shell.Accessory(actor.name, i);
        auto* master = source.onHead ? head : body;
        qa.Need(child && child->IsHidden() == hidden, std::string(actor.name) + ": accessory visibility differs");
        if (initial) qa.Identity(child, actor.name);
        Mesh(qa, child, source.asset, 0, !initial);
        Binding(qa, child, master, source.joint);
        if (!initial) {
            const auto joint = WorldJoint(qa, master, source.joint);
            Near(qa, RenderPose(child), {joint.origin + source.origin * joint.axis, source.angles.ToMat3() * joint.axis},
                 std::string(actor.name) + " " + source.asset);
        }
    }
}

WolfSourceAIActorState State(QA& qa, const WolfSourceAIShell& shell, const char* name) {
    WolfSourceAIActorState state;
    idStr reason;
    qa.NeedReason(shell.Query(name, state, reason), reason, "Source actor state unavailable");
    qa.Need(state.poseReason.empty(), state.poseReason);
    return state;
}
WolfSourceTrainyardExitState Exit(QA& qa, const WolfSourceTrainyardExit& exit) {
    WolfSourceTrainyardExitState state;
    idStr reason;
    qa.NeedReason(exit.Query(state, reason), reason, "Trainyard exit state unavailable");
    return state;
}
void Frame(QA& qa, idPlayer* player, const idVec2* goal=nullptr) {
    usercmd_t commands[MAX_CLIENTS] = {};
    if (goal) {
        const auto origin = player->GetPhysics()->GetOrigin();
        idVec2 direction(goal->x - origin.x, goal->y - origin.y);
        const float distance = direction.Length();
        if (distance > .5f) {
            direction /= distance;
            const float yaw = DEG2RAD(player->viewAngles.yaw);
            const idVec2 forward(idMath::Cos(yaw), idMath::Sin(yaw));
            const idVec2 right(idMath::Sin(yaw), -idMath::Cos(yaw));
            auto& command = commands[player->entityNumber];
            command.forwardmove = static_cast<signed char>(idMath::ClampInt(-127, 127, int(127.f * (direction * forward))));
            command.rightmove = static_cast<signed char>(idMath::ClampInt(-127, 127, int(127.f * (direction * right))));
        }
    }
    gameLocal.RunFrame(commands);
    ++qa.frames;
}
idVec2 DoorGoal(idPlayer* player, const idVec2& outside) {
    const auto origin = player->GetPhysics()->GetOrigin();
    if (std::fabs(origin.y - outside.y) > 3.f && origin.x > outside.x + 40.f)
        return idVec2(origin.x, outside.y);
    return outside;
}
void Receipt(QA& qa, const WolfSourceTrainyardExitState& state, const char* source, const char* target,
             const char* operation, const char* key, idEntity* player) {
    unsigned matches = 0;
    for (const auto& receipt : state.receipts) {
        if (receipt.source != source || receipt.target != target || receipt.operation != operation) continue;
        ++matches;
        qa.Need(receipt.outcome == "delivered" && receipt.key == key && receipt.actor == player->GetName(),
                std::string(target) + ": source dispatch receipt differs");
    }
    qa.Need(matches == 1, std::string(target) + ": source dispatch receipt missing or duplicated");
}
void Wake(QA& qa, WolfSourceAIShell& shell, const char* actor, idPlayer* player) {
    idStr reason;
    qa.NeedReason(shell.Activate(actor, player, reason), reason, "Native EV_Activate refused");
    const auto after = State(qa, shell, actor);
    qa.Need(after.activated && !after.hidden && after.wakes == 1,
            std::string(actor) + ": native activation did not preserve once-only wake");
}

void VisibleActivateKey(QA& qa, WolfSourceAIShell& shell, const WolfSourceAIActorState& before) {
    idDict args;
    args.Set("name", "wolf2009_qa_ai_keyed_activator");
    args.Set("useKey", "player_safe");
    args.Set("noclipmodel", "1");
    std::unique_ptr<idEntity> activator(gameLocal.SpawnEntityType(idTarget::Type, &args));
    qa.Need(bool(activator), "Could not create the QA-only keyed activator");
    idStr reason;
    qa.NeedReason(shell.Activate(Greeter, activator.get(), reason), reason, "Visible keyed Activate refused");
    const auto after = State(qa, shell, Greeter);
    qa.Need(!after.hidden && after.wakes == before.wakes && after.rootRefusals == before.rootRefusals &&
            after.activations == before.activations + 1 && after.useNotifications == before.useNotifications + 1 &&
            after.pendingUse && after.pendingKey == "player_safe" && after.lastActivator == activator->GetName(),
            "Visible Activate did not latch its caller's source key once");
}

bool Flag(const char* path) {
    idFile* file = fileSystem->OpenFileRead(path, false);
    if (file) fileSystem->CloseFile(file);
    return file != nullptr;
}
enum class ChainCase { Main, Stale, MissingMember, ExecStale, ExecMissingTarget };
const char* CaseName(ChainCase value) {
    return value == ChainCase::Stale ? "stale" : value == ChainCase::MissingMember ? "missing-member"
         : value == ChainCase::ExecStale ? "exec-stale" : value == ChainCase::ExecMissingTarget ? "exec-missing-target" : "main";
}

const WolfSourceBrainActionReceipt& Action(QA& qa, const WolfSourceAIActorState& state, unsigned node) {
    const WolfSourceBrainActionReceipt* found = nullptr;
    for (const auto& receipt : state.actions)
        if (receipt.node == node) {
            qa.Need(!found, "Brain action node entered twice: " + std::to_string(node));
            found = &receipt;
        }
    qa.Need(found != nullptr, "Brain action node was not entered: " + std::to_string(node));
    return *found;
}
void Applied(QA& qa, const WolfSourceAIActorState& state, unsigned node, unsigned type, bool probe, const char* subject) {
    const auto& r = Action(qa, state, node);
    qa.Need(r.type == type && r.probe == probe && r.outcome == "applied" && r.subject == subject &&
            r.timeMs == state.firstBrainTimeMs, "Brain action receipt differs at node " + std::to_string(node));
}
void NullSubject(QA& qa, const WolfSourceAIActorState& state, unsigned node) {
    const auto& r = Action(qa, state, node);
    qa.Need(r.outcome == "null-subject", "Stale subject was not the original no-op at node " + std::to_string(node));
}

// OnSpawn 1 -> 11 -> 20 -> 2 (probes 3,23,24,29,30,31,32,44,45) -> 38 (probes 39,36,37).
void OnSpawnChain(QA& qa, WolfSourceAIShell& shell, ChainCase chainCase, const WolfSourceAIActorState& first) {
    const bool stale = chainCase == ChainCase::Stale;
    qa.Need(first.chainProfile == "SourceInterrogatorChainV1" && !first.brainFailed && first.brainTransitions == 4 &&
            first.brainProbes == 12 && first.actions.size() == 14 && !first.holds,
            "Interrogator OnSpawn chain did not reach node 38 with its twelve probes");
    Applied(qa, first, 2, 33, false, First);
    Applied(qa, first, 38, 54, false, First);
    for (unsigned node : {23u, 24u, 29u, 30u, 31u, 32u, 36u, 37u})
        Applied(qa, first, node, Action(qa, first, node).type, true,
                node == 23 || node == 29 || node == 31 || node == 36 ? Hostage1 : Hostage2);
    Applied(qa, first, 44, 253, true, First);
    if (stale) for (unsigned node : {3u, 39u, 45u}) NullSubject(qa, first, node);
    else {
        Applied(qa, first, 3, 33, true, Second);
        Applied(qa, first, 39, 54, true, Second);
        Applied(qa, first, 45, 253, true, Second);
    }
    const auto& s = first.script;
    qa.Need(s.scriptedManual && s.manualEnters == 1 && s.announcementsOff && s.ignoreEnemies && !s.idleLookForEnemy &&
            !s.scriptShows && s.undying && s.postedStates.size() == 2 && s.postedStates[0] == "Wait_ScriptedManual" &&
            s.idleOverride == "srp_tyd_execute_nzi_01_start_idle" && !s.idleStale && s.idleConsumptions == 1 &&
            s.currentIdle == s.idleOverride && !s.idlePlayback.empty(),
            "SetScriptedManual/AnimOverrideIdle did not apply to the native first interrogator");
    if (!stale) {
        const auto second = State(qa, shell, Second);
        qa.Need(second.script.scriptedManual && !second.script.scriptShows && second.script.undying &&
                second.script.idleOverride == "srp_tyd_execute_nzi_02_start_idle" && second.script.idleConsumptions == 1 &&
                !second.script.idleStale, "Second interrogator did not receive its probe actions");
    } else {
        WolfSourceAIActorState gone;
        idStr reason;
        qa.Need(!shell.Query(Second, gone, reason) && !shell.Body(Second), "Stale second interrogator still answers");
    }
    const char* idles[] = {"srp_tyd_execute_kc_01_start_idle", "srp_tyd_execute_kc_02_start_idle"};
    unsigned index = 0;
    for (const char* hostage : {Hostage1, Hostage2}) {
        const auto h = State(qa, shell, hostage);
        qa.Need(!h.hidden && h.thinking && !h.wakes && !h.activated && h.script.scriptShows == 1 &&
                h.script.scriptedManual && h.script.notarget && h.script.deathOverride.empty() &&
                h.script.idleOverride == idles[index] && h.script.idleConsumptions == 1 && !h.script.idleStale &&
                shell.Body(hostage)->fl.notarget && !shell.Body(hostage)->IsHidden(),
                std::string(hostage) + ": SetScriptedManual probe did not Show the hidden hostage with its overrides");
        ++index;
    }

    // Type19 nodes 11 and 20: one stream request each, no wait; Ready once.
    qa.Need(first.voice.size() == 2 && first.voice[0].node == 11 && first.voice[1].node == 20 &&
            first.voice[0].definition == "scripted_voice_wi1_trn" && first.voice[1].definition == "scripted_voice_wi2_trn" &&
            first.voice[0].owner == First, "Type19 stream receipts differ");
    const auto& wi1 = first.voice[0];
    const auto& wi2 = first.voice[1];
    qa.Need(wi1.bankBacked && wi1.readyConsumed && wi1.state == "ready" && wi1.members == 11 && wi1.expectedMembers == 11 &&
            wi1.inventoryQualified && wi1.bankGeneration, "wi1 bank was not Ready once with all 11 members");
    if (chainCase != ChainCase::Stale && chainCase != ChainCase::MissingMember) {
        qa.Need(wi2.owner == Second && wi2.bankBacked && wi2.readyConsumed && wi2.state == "ready" && wi2.members == 9 &&
                wi2.expectedMembers == 9 && wi2.inventoryQualified && wi2.bankGeneration != wi1.bankGeneration,
                "wi2 bank was not Ready once with all 9 members");
    } else if (chainCase == ChainCase::MissingMember) {
        qa.Need(wi2.owner == Second && wi2.readyConsumed && wi2.members == 8 && wi2.expectedMembers == 9 &&
                !wi2.inventoryQualified && wi2.state == "inventory-refused" && !wi2.reason.empty(),
                "wi2 pack without its final member was not refused by the measured inventory");
    } else {
        qa.Need(wi2.state == "owner-absent" && !wi2.bankBacked && !wi2.readyConsumed && !wi2.bankGeneration,
                "Stale wi2 owner did not skip the stream request");
    }
    common->Printf("WOLF2009_TRAINYARD_AI_CHAIN_ONSPAWN case=%s transitions=%u probes=%u actions=%u wi1_ready=%d wi1_members=%u "
                   "wi2_state=%s wi2_members=%u hostages_shown=2 idle=%s first_brain_time=%d\n",
                   CaseName(chainCase), first.brainTransitions, first.brainProbes, unsigned(first.actions.size()),
                   int(wi1.readyConsumed), wi1.members, wi2.state.c_str(), wi2.members, s.currentIdle.c_str(),
                   first.firstBrainTimeMs);
}

void Run(QA& qa) {
    auto* map = Wolf2009_SourceSessionMapRuntime();
    qa.Need(map && Wolf2009_HasSourceSession(), "Ordinary source trainyard session absent");
    auto* shell = map->AIShell();
    auto* exit = map->TrainyardExit();
    auto* director = map->Director();
    auto* player = gameLocal.GetLocalPlayer();
    qa.Need(shell && shell->Count() == 5 && exit && director && player,
            "Expected five native actors and the original arrival/exit owners");
    const ChainCase chainCase = Flag("fixtures/trainyard-ai-chain-stale.enabled") ? ChainCase::Stale
                              : Flag("fixtures/trainyard-ai-chain-missing.enabled") ? ChainCase::MissingMember
                              : Flag("fixtures/trainyard-ai-exec-stale.enabled") ? ChainCase::ExecStale
                              : Flag("fixtures/trainyard-ai-exec-missing-target.enabled") ? ChainCase::ExecMissingTarget
                                                                                    : ChainCase::Main;
    const bool stale = chainCase == ChainCase::Stale;
    std::uint64_t generation = 0;
    for (const auto& actor : Actors) {
        Appearance(qa, *shell, actor, true);
        const auto state = State(qa, *shell, actor.name);
        if (!generation) generation = state.levelGeneration;
        qa.Need(state.levelGeneration == generation && generation &&
                state.spawnId == unsigned(gameLocal.GetSpawnId(shell->Body(actor.name))) &&
                state.hidden == actor.hidden && state.brainBound == actor.brain &&
                !state.noUnhideOnUse && !state.wakes, std::string(actor.name) + ": initial shell state differs");
    }
    qa.Need(qa.identities.size() == 16 && Wolf2009_LiveSourceAIActors() == 5 &&
            Wolf2009_LiveSourceAIBrains() == 2, "Actor, head, accessory or BRAI owner count differs");
    if (stale) {
        // Stale actor: the second interrogator's native body ends before any
        // source event; its shell record stays owned until map teardown.
        delete shell->Body(Second);
        qa.Need(!shell->Body(Second) && !gameLocal.FindEntity(Second) && Wolf2009_LiveSourceAIActors() == 5,
                "QA could not end the second interrogator's native generation");
    }

    unsigned guard = 0;
    auto event = Exit(qa, *exit);
    while (!event.armedTrigger.fires) {
        qa.Need(++guard <= 1600, "Wagon did not contact the armed trigger_once_1");
        for (const char* name : {First, Second, Hostage1, Hostage2}) {
            if (stale && name == Second) continue;
            const auto state = State(qa, *shell, name);
            qa.Need(state.hidden && !state.thinking && !state.brainTicks && !state.rootRefusals && !state.wakes &&
                    !state.brainTransitions && state.actions.empty(),
                    std::string(name) + ": woke or ticked before the native trigger contact");
        }
        Frame(qa, player);
        event = Exit(qa, *exit);
    }
    qa.Need(event.armedTrigger.arms == 1 && event.armedTrigger.fires == 1 &&
            event.armedTrigger.fireTimeMs >= event.armedTrigger.armTimeMs &&
            event.armedTrigger.fireActivator == player->GetName(), "Native trigger_once_1 did not fire once for the player");
    // shake_train ran its tail in the arming tick (SourceBrainActionsV1).
    qa.Need(event.shakeEnded && event.shakeStopNode == 34 && event.shakeStopType == 253 && event.shakeActions.size() == 4 &&
            event.shakePlayer.lastWeaponEvent == "enableWeapon" && event.shakePlayer.weaponEventHandled &&
            !event.shakePlayer.weaponsLocked && !player->fl.notarget &&
            event.shakePlayer.lastCreditsEvent == "disableCreditsScreen" && !event.shakePlayer.undying,
            "shake_train did not reach node 34 with its native effects");
    Receipt(qa, event, "trigger_once_1", First, "activate", "", player);
    if (stale) {
        unsigned skipped = 0;
        for (const auto& r : event.receipts)
            skipped += r.source == "trigger_once_1" && r.target == Second && r.outcome == "stale-skipped";
        qa.Need(skipped == 1, "Stale second interrogator was not skipped by trigger_once_1");
    } else {
        Receipt(qa, event, "trigger_once_1", Second, "activate", "", player);
    }
    Receipt(qa, event, "trigger_once_1", Greeter, "activate", "", player);
    // Whole-bank registration happens in ordinary Think frames; Ready is never
    // produced by a frame count, so the loop only bounds the wait.
    WolfSourceAIActorState first;
    guard = 0;
    do {
        qa.Need(++guard <= 120, "Type19 stream requests did not settle");
        Frame(qa, player);
        first = State(qa, *shell, First);
    } while (first.voice.size() != 2 ||
             first.voice[0].state == "pending" || (first.voice[1].state == "pending"));
    const auto greeter = State(qa, *shell, Greeter);
    qa.Need(first.activated && !first.hidden && first.wakes == 1 && first.brainTicks &&
            first.wakeTimeMs == event.armedTrigger.fireTimeMs && first.firstBrainTimeMs >= first.wakeTimeMs,
            "Woken interrogator did not tick its network");
    OnSpawnChain(qa, *shell, chainCase, first);
    if (!stale) {
        const auto second = State(qa, *shell, Second);
        qa.Need(second.activated && !second.hidden && second.wakes == 1 && !second.brainBound && !second.brainTicks,
                "Second interrogator did not wake once without a fabricated brain");
    }
    qa.Need(greeter.activated && !greeter.hidden && !greeter.wakes &&
            greeter.brainTicks && greeter.rootRefusals == 1 && greeter.stopRoot == 1 &&
            greeter.stopNode == 2 && greeter.stopType == 49 && !greeter.reason.empty(),
            "Greeter did not stop at original OnSpawn 1 to type49 node 2");
    for (unsigned i : {0u, 1u, 4u}) if (!(stale && i == 1)) Appearance(qa, *shell, Actors[i], false);
    Wake(qa, *shell, First, player);
    if (!stale) Wake(qa, *shell, Second, player);

    WolfSourceDirectorState arrival;
    idStr reason;
    guard = 0;
    do {
        qa.Need(++guard <= 120, "Wagon did not deliver the arrival relay after trigger_once_1");
        Frame(qa, player);
        qa.NeedReason(director->Query(arrival, reason), reason, "Arrival state unavailable");
    } while (arrival.phase == WolfSourceDirectorPhase::WaitingForArrival);
    Frame(qa, player);
    const auto afterArrival = State(qa, *shell, Greeter);
    qa.Need(arrival.phase == WolfSourceDirectorPhase::PartialArrival &&
            afterArrival.relayDeliveries == 1 && afterArrival.pendingUse && afterArrival.pendingKey == "train_stop",
            "Source arrival relay did not latch the greeter's train_stop key");

    // Follow the actual wagon doorway with ordinary player input.
    event = Exit(qa, *exit);
    const idVec2 outside(event.blockerMins[0] - 48.f, (event.blockerMins[1] + event.blockerMaxs[1]) * .5f);
    guard = 0;
    while (!event.exitTrigger.fires) {
        qa.Need(++guard <= 850, "Player did not reach exit_train_trigger through the ordinary doorway");
        const auto goal = DoorGoal(player, outside);
        Frame(qa, player, &goal);
        event = Exit(qa, *exit);
    }
    qa.Need(event.removals == 1 && !event.blockerAlive && event.relayActivations == 1 && event.exitTrigger.fires == 1,
            "Measured exit did not remove its blocker and fire the relay once");
    Receipt(qa, event, "train_greeter_brainuse_2", Greeter, "use", ExitKey, player);
    Receipt(qa, event, "train_greeter_brainuse_2", First, "use", ExitKey, player);
    for (unsigned i = 0; i < 4; ++i) Frame(qa, player);
    const auto afterExit = State(qa, *shell, Greeter);
    const auto interrogatorExit = State(qa, *shell, First);
    qa.Need(!exit->Trigger("exit_train_trigger") && afterExit.pendingUse && afterExit.pendingKey == ExitKey &&
            afterExit.useNotifications == afterArrival.useNotifications + 1 && afterExit.rootRefusals == 1,
            "E4 delivery lost the greeter's pending key or advanced its unsupported transition");
    // Interrogator OnUsed 12 consumed the key: 43 (probes 42, 41, 40) -> 21 -> 22
    // -> 25 Animate (probes 26 Animate, 33/35 KillTarget) in one Tick
    // (SourceAIScriptedAnimV1). AnimFinished keeps 25 active.
    qa.Need(!interrogatorExit.pendingUse && interrogatorExit.useNotifications == first.useNotifications + 1 &&
            interrogatorExit.wakes == 1 && interrogatorExit.lastPlayerUseTimeMs == event.exitTrigger.fireTimeMs &&
            interrogatorExit.brainTransitions == first.brainTransitions + 4 &&
            interrogatorExit.brainProbes == first.brainProbes + 6 && interrogatorExit.actions.size() == first.actions.size() + 10 &&
            !interrogatorExit.holds && !interrogatorExit.brainFailed &&
            interrogatorExit.script.idleOverride.empty() && interrogatorExit.script.idleConsumptions == 2,
            "Interrogator OnUsed chain did not enter Animate node 25 with probes 26/33/35 in one Tick");
    const auto& enter25 = Action(qa, interrogatorExit, 25);
    qa.Need(enter25.type == 51 && !enter25.probe && enter25.outcome == "applied" && enter25.subject == First &&
            enter25.profile == "SourceAIScriptedAnimV1", "Animate node 25 receipt differs");
    const int sceneTick = enter25.timeMs;
    for (unsigned node : {43u, 21u, 22u, 26u, 33u, 35u}) qa.Need(Action(qa, interrogatorExit, node).timeMs == sceneTick,
                                                                 "Execution chain node ran in another Tick: " + std::to_string(node));
    qa.Need(Action(qa, interrogatorExit, 26).probe && Action(qa, interrogatorExit, 26).outcome == (stale ? "null-subject" : "applied") &&
            Action(qa, interrogatorExit, 33).probe && Action(qa, interrogatorExit, 33).outcome == "applied" &&
            Action(qa, interrogatorExit, 33).subject == Hostage1 && Action(qa, interrogatorExit, 35).probe &&
            Action(qa, interrogatorExit, 35).outcome == "applied" && Action(qa, interrogatorExit, 35).subject == Hostage2,
            "Probes 26/33/35 receipts differ");
    // Probe order inside the Tick: 26, 33, 35 follow 25 in the receipt list.
    {
        std::vector<unsigned> order;
        for (const auto& r : interrogatorExit.actions) if (r.timeMs == sceneTick) order.push_back(r.node);
        const std::vector<unsigned> expected = {43, 42, 41, 40, 21, 22, 25, 26, 33, 35};
        qa.Need(order == expected, "Execution Tick entered nodes in another order");
    }
    const auto& a1 = interrogatorExit.animation;
    qa.Need(interrogatorExit.animationAdmitted && a1.plays == 1 && a1.currentClip == "srp_tyd_execute_nzi_one" &&
            a1.scripted && a1.endWithIdle && a1.playStartMs == sceneTick && a1.playBlendMs == 83 && !a1.playCycle &&
            a1.statePosts.size() >= 2 && a1.statePosts[0] == "State_ScriptedAnim" && a1.statePosts[1] == "State_ScriptedStop",
            "Node 25 did not start nzi_one once through ScriptedAnim");
    const int clipLength = a1.playEndMs - a1.playStartMs;
    if (!stale) {
        const auto second = State(qa, *shell, Second);
        qa.Need(second.animation.plays == 1 && second.animation.currentClip == "srp_tyd_execute_nzi_two" &&
                second.animation.playStartMs == sceneTick && second.animation.scripted && second.animation.playBlendMs == 83,
                "Probe 26 did not start nzi_two once on the second interrogator");
    }
    const char* killClips[] = {"srp_tyd_execute_kc_one", "srp_tyd_execute_kc_two"};
    for (unsigned i = 0; i < 2; ++i) {
        const auto h = State(qa, *shell, i ? Hostage2 : Hostage1);
        const auto& a = h.animation;
        qa.Need(a.dead && a.kills == 1 && a.killFlags == 0x1 && a.deathPredicate && a.deathChoice == killClips[i] &&
                a.deathChoiceSource.find("override") == 0 && a.deathClipPlayed && a.plays == 1 && a.currentClip == killClips[i] &&
                a.playStartMs == sceneTick && a.playBlendMs == 166 && a.waitingDeathAnim &&
                a.ragdoll.find("deferred") == 0 && !a.statePosts.empty() && a.statePosts.back() == "State_Wait_DeathAnim",
                std::string(i ? Hostage2 : Hostage1) + ": KillTarget did not play the override death clip");
    }

    // Mid-scene events chosen by the case.
    if (chainCase == ChainCase::ExecMissingTarget) {
        idEntity* decal = shell->ExecutionTargetEntity("blood_decal_2");
        qa.Need(decal != nullptr, "blood_decal_2 has no native owner");
        delete decal;
        qa.Need(!gameLocal.FindEntity("blood_decal_2"), "QA could not remove blood_decal_2");
    }
    bool staleDone = false;
    guard = 0;
    WolfSourceAIActorState scene = State(qa, *shell, First);
    while (!scene.holds) {
        qa.Need(++guard <= 900, "Interrogator did not leave Animate node 25");
        if (chainCase == ChainCase::ExecStale && !staleDone && gameLocal.time >= sceneTick + 3000) {
            delete shell->Body(Second);
            qa.Need(!shell->Body(Second) && !gameLocal.FindEntity(Second), "QA could not end the second interrogator mid-clip");
            staleDone = true;
        }
        // AnimFinished must not be true before the native remaining time allows it.
        for (const auto& task : scene.animateTasks)
            if (task.first == 25) qa.Need(!task.second.lastValue || task.second.lastRemainingMs <= 83,
                                          "AnimFinished became true early");
        Frame(qa, player);
        scene = State(qa, *shell, First);
    }
    // 25 -> 17 (SetScriptedManual false; probes 16, 46, 47) -> hold before Hunt 6.
    qa.Need(scene.holds == 1 && scene.stopNode == 6 && scene.stopType == 43 && scene.heldSourceNode == 17 &&
            scene.brainTransitions == interrogatorExit.brainTransitions + 1 &&
            scene.brainProbes == interrogatorExit.brainProbes + 3 && !scene.brainFailed && !scene.reason.empty(),
            "Interrogator did not run 25 -> 17 and hold before Hunt node 6");
    qa.Need(Action(qa, scene, 17).outcome == "applied" && Action(qa, scene, 16).probe && Action(qa, scene, 46).outcome == "applied" &&
            Action(qa, scene, 47).probe && !scene.script.scriptedManual && !scene.script.undying,
            "Node 17 and its probes did not apply");
    const WolfSourceAnimateTask* task25 = nullptr;
    for (const auto& task : scene.animateTasks) if (task.first == 25) task25 = &task.second;
    qa.Need(task25 && task25->succeeded && task25->failTimeMs == sceneTick + 1 && task25->firstTrueMs > sceneTick &&
            task25->firstTrueRemainingMs >= 0 && task25->firstTrueRemainingMs <= 83 && task25->lastFalseRemainingMs > 83 &&
            task25->firstTrueMs - sceneTick >= clipLength - 83 && task25->firstTrueMs - sceneTick <= clipLength - 83 + 50,
            "AnimFinished on node 25 did not follow the native remaining-time boundary");
    const int finishedAt = task25->firstTrueMs - sceneTick;
    const auto& done1 = scene.animation;
    qa.Need(done1.scriptedStops == 1 && done1.scriptedStopMs < task25->firstTrueMs && done1.scriptedStopRemainingMs <= 166 &&
            done1.scriptedStopRemainingMs > 83 && !done1.scripted, "State_ScriptedAnim did not end 4 frames before the clip end");

    // Frame commands: one dispatch each, at their zero-based frames.
    auto commands = [&](const WolfSourceAIActorState& state, const std::vector<std::string>& expected, const std::string& who) {
        std::vector<std::string> actual;
        for (const auto& r : state.animation.frameCommands)
            actual.push_back(r.command + "@" + std::to_string(r.frame) + ":" + r.outcome);
        qa.Need(actual == expected, who + ": frame command receipts differ");
        for (const auto& r : state.animation.frameCommands)
            qa.Need(r.clipTimeMs >= r.frame * 1000 / 24 && r.clipTimeMs <= r.frame * 1000 / 24 + 50 && r.play == 1,
                    who + ": frame command fired at the wrong clip time");
    };
    commands(scene, {"speak@111:refused", "trigger@206:activated"}, First);
    if (chainCase != ChainCase::ExecStale && !stale) {
        commands(State(qa, *shell, Second), {"speak@96:refused", "trigger@206:activated"}, Second);
        // nzi_two has 281 frames against nzi_one's 273, so its own 4-frame
        // stop comes about 333 ms after node 25 finishes. Its brainless shell
        // keeps running the ordinary animator; only frames are bounded here.
        auto second = State(qa, *shell, Second).animation;
        const int secondLength = second.playEndMs - second.playStartMs;
        qa.Need(secondLength > clipLength, "nzi_two is not longer than nzi_one");
        guard = 0;
        while (!second.scriptedStops) {
            qa.Need(++guard <= 120, "Second interrogator's scripted animation did not stop");
            Frame(qa, player);
            second = State(qa, *shell, Second).animation;
        }
        qa.Need(second.scriptedStops == 1 && !second.scripted && second.scriptedStopRemainingMs <= 166 &&
                second.scriptedStopRemainingMs > 83 && second.scriptedStopMs - sceneTick >= secondLength - 166 &&
                second.scriptedStopMs - sceneTick <= secondLength - 83,
                "Second interrogator's State_ScriptedAnim did not end 4 frames before the clip end");
    }
    commands(State(qa, *shell, Hostage1), {"fx_offset@206:refused", "trigger@206:activated", "interactive_ragdoll@229:refused"}, Hostage1);
    commands(State(qa, *shell, Hostage2), {"fx_offset@206:refused",
             chainCase == ChainCase::ExecMissingTarget ? "trigger@206:missing-target" : "trigger@206:activated",
             "interactive_ragdoll@229:refused"}, Hostage2);
    struct TargetCase { const char* name; const char* activator; unsigned activations; };
    const bool gun2 = chainCase != ChainCase::ExecStale && !stale;
    const TargetCase targets[] = {{"execution_gunshot", First, 1}, {"execution_gunshot2", Second, gun2 ? 1u : 0u},
                                  {"blood_decal_1", Hostage1, 1}, {"blood_decal_2", Hostage2, 1}};
    for (const auto& t : targets) {
        WolfSourceAIExecutionTargetState target;
        const bool found = shell->ExecutionTarget(t.name, target);
        if (chainCase == ChainCase::ExecMissingTarget && std::string(t.name) == "blood_decal_2") {
            qa.Need(!found || !target.live, "Removed blood_decal_2 still answers");
            continue;
        }
        qa.Need(found && target.live && target.activations == t.activations &&
                (!t.activations || (target.lastActivator == t.activator && target.lastTimeMs - sceneTick >= 8583 &&
                                    target.lastTimeMs - sceneTick <= 8583 + 50)),
                std::string(t.name) + ": native activate count, activator or time differs");
    }
    if (chainCase == ChainCase::ExecStale) {
        WolfSourceAIActorState gone;
        qa.Need(!shell->Query(Second, gone, reason) && staleDone, "Stale second interrogator still answers");
    }
    for (unsigned i = 0; i < 8; ++i) Frame(qa, player);
    const auto settled = State(qa, *shell, First);
    qa.Need(settled.holds == 1 && settled.actions.size() == scene.actions.size() &&
            settled.brainTransitions == scene.brainTransitions, "Held interrogator re-entered a task");

    // Synthetic cases on the native handlers: an absent clip and an absent
    // unchecked death override.
    {
        WolfSourceAnimateTask task;
        std::string effect;
        qa.Need(shell->QAAnimate(First, "srp_tyd_execute_absent_clip", task, effect) && task.entered && !task.succeeded &&
                effect.find("Missing 'srp_tyd_execute_absent_clip' animation") != std::string::npos,
                "Absent clip did not report the missing animation");
        const auto after = State(qa, *shell, First);
        qa.Need(after.animation.scripted && !after.animation.lastMissing.empty() &&
                after.animation.scriptedFailures == 1 && after.animation.plays == 1,
                "Absent clip changed playback or cleared the scripted bit");
        qa.Need(!shell->QAAnimFinished(First, "srp_tyd_execute_absent_clip", task), "Failed Animate finished in its Enter ms");
        Frame(qa, player);
        qa.Need(shell->QAAnimFinished(First, "srp_tyd_execute_absent_clip", task), "Failed Animate did not finish on the next tick");
    }
    std::string deathEffect = "-";
    if (gun2) {
        qa.Need(shell->QAKill(Second, "srp_tyd_absent_death", deathEffect), "Absent death override kill refused");
        const auto dead = State(qa, *shell, Second).animation;
        qa.Need(dead.dead && dead.deathChoice == "srp_tyd_absent_death" && dead.deathChoiceSource.find("unchecked") != std::string::npos &&
                !dead.deathClipPlayed && dead.deathPlayback.find("names an absent clip") != std::string::npos,
                "Absent death override was not recorded as the unchecked choice's failure");
    }
    common->Printf("WOLF2009_TRAINYARD_AI_EXECUTION_QA case=%s scene_tick=%d clip_length=%d anim_finished=%d "
                   "remaining_true=%d remaining_false=%d scripted_stop=%d interrogator_stop=6 hostages_dead=2 "
                   "gunshot=1 gunshot2=%u decal1=1 decal2=%s\n",
                   CaseName(chainCase), sceneTick, clipLength, finishedAt, task25->firstTrueRemainingMs,
                   task25->lastFalseRemainingMs, done1.scriptedStopMs - sceneTick, gun2 ? 1u : 0u,
                   chainCase == ChainCase::ExecMissingTarget ? "missing" : "1");
    VisibleActivateKey(qa, *shell, afterExit);

    // The hostages were shown by the source graph (probes 23/24), not woken:
    // a native Activate on these visible AIs neither wakes nor shows them again.
    for (const char* hostage : {Hostage1, Hostage2}) {
        const auto before = State(qa, *shell, hostage);
        qa.NeedReason(shell->Activate(hostage, player, reason), reason, "Native EV_Activate refused");
        const auto after = State(qa, *shell, hostage);
        qa.Need(!after.hidden && !after.wakes && after.activations == before.activations + 1 &&
                after.script.scriptShows == 1, std::string(hostage) + ": visible Activate changed the script-shown hostage");
    }
    for (unsigned i = 0; i < 3; ++i) Frame(qa, player);
    Appearance(qa, *shell, Actors[2], false);
    Appearance(qa, *shell, Actors[3], false);
    const unsigned snapshots = stale ? 8 : 10;
    qa.Need(qa.snapshots == snapshots, "Did not observe every live body and head after ordinary game frames");
    common->Printf("WOLF2009_TRAINYARD_AI_SHELL_QA_OK checks=%u frames=%u actors=5 heads=5 accessories=6 "
                   "snapshots=%u case=%s trigger_wakes=%u exit_key_deliveries=2 chain_hold=6 shake_end=34 "
                   "qa_visible_activate_key=1 qa_hostage_activations=2\n",
                   qa.checks, qa.frames, qa.snapshots, CaseName(chainCase), stale ? 1u : 2u);
}
}
extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardAIShellQA() {
    QA qa;
    try {
        Run(qa);
        return 1;
    } catch (const std::exception& error) {
        common->Warning("WOLF2009_TRAINYARD_AI_SHELL_QA_FAIL checks=%u frames=%u reason=%s",
                        qa.checks, qa.frames, error.what());
        return 0;
    }
}

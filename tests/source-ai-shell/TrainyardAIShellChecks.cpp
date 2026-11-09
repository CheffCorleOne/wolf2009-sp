// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
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

void Run(QA& qa) {
    auto* map = Wolf2009_SourceSessionMapRuntime();
    qa.Need(map && Wolf2009_HasSourceSession(), "Ordinary source trainyard session absent");
    auto* shell = map->AIShell();
    auto* exit = map->TrainyardExit();
    auto* director = map->Director();
    auto* player = gameLocal.GetLocalPlayer();
    qa.Need(shell && shell->Count() == 5 && exit && director && player,
            "Expected five native actors and the original arrival/exit owners");
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

    unsigned guard = 0;
    auto event = Exit(qa, *exit);
    while (!event.armedTrigger.fires) {
        qa.Need(++guard <= 1600, "Wagon did not contact the armed trigger_once_1");
        for (const char* name : {First, Second, Hostage1, Hostage2}) {
            const auto state = State(qa, *shell, name);
            qa.Need(state.hidden && !state.thinking && !state.brainTicks && !state.rootRefusals && !state.wakes,
                    std::string(name) + ": woke or ticked before the native trigger contact");
        }
        Frame(qa, player);
        event = Exit(qa, *exit);
    }
    qa.Need(event.armedTrigger.arms == 1 && event.armedTrigger.fires == 1 &&
            event.armedTrigger.fireTimeMs >= event.armedTrigger.armTimeMs &&
            event.armedTrigger.fireActivator == player->GetName(), "Native trigger_once_1 did not fire once for the player");
    Receipt(qa, event, "trigger_once_1", First, "activate", "", player);
    Receipt(qa, event, "trigger_once_1", Second, "activate", "", player);
    Receipt(qa, event, "trigger_once_1", Greeter, "activate", "", player);
    for (unsigned i = 0; i < 3; ++i) Frame(qa, player);
    const auto first = State(qa, *shell, First);
    const auto second = State(qa, *shell, Second);
    const auto greeter = State(qa, *shell, Greeter);
    qa.Need(first.activated && !first.hidden && first.wakes == 1 && first.brainTicks &&
            first.wakeTimeMs == event.armedTrigger.fireTimeMs && first.firstBrainTimeMs >= first.wakeTimeMs &&
            first.rootRefusals == 1 && first.stopRoot == 1 && first.stopNode == 11 &&
            first.stopType == 19 && !first.reason.empty(),
            "Woken interrogator did not stop at original OnSpawn 1 to type19 node 11");
    qa.Need(second.activated && !second.hidden && second.wakes == 1 && !second.brainBound && !second.brainTicks,
            "Second interrogator did not wake once without a fabricated brain");
    qa.Need(greeter.activated && !greeter.hidden && !greeter.wakes &&
            greeter.brainTicks && greeter.rootRefusals == 1 && greeter.stopRoot == 1 &&
            greeter.stopNode == 2 && greeter.stopType == 49 && !greeter.reason.empty(),
            "Greeter did not stop at original OnSpawn 1 to type49 node 2");
    for (unsigned i : {0u, 1u, 4u}) Appearance(qa, *shell, Actors[i], false);
    Wake(qa, *shell, First, player);
    Wake(qa, *shell, Second, player);

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
            afterExit.useNotifications == afterArrival.useNotifications + 1 && afterExit.rootRefusals == 1 &&
            interrogatorExit.pendingUse && interrogatorExit.pendingKey == ExitKey &&
            interrogatorExit.useNotifications == first.useNotifications + 1 && interrogatorExit.wakes == 1 &&
            interrogatorExit.rootRefusals == 1 && interrogatorExit.lastPlayerUseTimeMs == event.exitTrigger.fireTimeMs,
            "E4 delivery lost pending keys, replayed a wake or advanced an unsupported transition");
    VisibleActivateKey(qa, *shell, afterExit);

    // The execution graph owns the later hostage Show. The QA activates
    // these two through the same native callback to inspect their poses.
    for (const char* hostage : {Hostage1, Hostage2}) {
        const auto stillHidden = State(qa, *shell, hostage);
        qa.Need(stillHidden.hidden && !stillHidden.wakes && !stillHidden.brainTicks,
                std::string(hostage) + ": source events woke the hostage prematurely");
        Wake(qa, *shell, hostage, player);
        Wake(qa, *shell, hostage, player);
    }
    for (unsigned i = 0; i < 3; ++i) Frame(qa, player);
    Appearance(qa, *shell, Actors[2], false);
    Appearance(qa, *shell, Actors[3], false);
    qa.Need(qa.snapshots == 10, "Did not observe all five bodies and five heads after ordinary game frames");
    common->Printf("WOLF2009_TRAINYARD_AI_SHELL_QA_OK checks=%u frames=%u actors=5 heads=5 accessories=6 "
                   "snapshots=%u trigger_wakes=2 exit_key_deliveries=2 qa_visible_activate_key=1 qa_hostage_activations=2\n",
                   qa.checks, qa.frames, qa.snapshots);
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

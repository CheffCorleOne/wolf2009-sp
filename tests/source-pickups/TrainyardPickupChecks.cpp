// SPDX-License-Identifier: GPL-3.0-or-later
// SourcePickupInventoryV1 / SourcePlayerFlagsV1 on the original trainyard map.
// QA stand-ins, all declared in docs/PLAYER-WEAPON.md:
// - the player is teleported next to the pickups (the route there needs the
//   sewer, ladder and allies stages that are not native);
// - activate_weapons is activated directly, standing in for frame 600 of
//   srp_trn_nml_2005_ee on allies_contact_1 (no native AI clip playback);
// - EnablePlayerWeapons(false), EnableDamage, SetUndying, Lock/UnlockPlayer
//   and SetUsable are applied as synthetic specs through the same native
//   handlers that the brain networks use; the undying-until time is written
//   directly, standing in for rvFuncSaveGame (M7).
// Everything else runs through ordinary frames, user commands, native touch
// contacts, events and teardown.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "BrainActions.h"
#include "BrainGraph.h"
#include "SourceInventory.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceAIShell.h"
#include "NativeSourceBrainActions.h"
#include "NativeSourceMap.h"
#include "NativeSourcePickups.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceTrainyardExit.h"
#include "NativeSourceWeapon.h"
#include "SourceSession.h"

namespace {
struct QA {
    unsigned checks = 0, frames = 0;
    void Need(bool ok, const std::string& reason) {
        ++checks;
        if (!ok) throw std::runtime_error(reason);
    }
};
bool Flag(const char* path) {
    idFile* file = fileSystem->OpenFileRead(path, false);
    if (file) fileSystem->CloseFile(file);
    return file != nullptr;
}
struct Input {
    const idVec2* goal = nullptr;
    bool attack = false, crouch = false;
};
void Frame(QA& qa, idPlayer* player, const Input& input = Input()) {
    usercmd_t commands[MAX_CLIENTS] = {};
    auto& command = commands[player->entityNumber];
    if (input.goal) {
        const auto origin = player->GetPhysics()->GetOrigin();
        idVec2 direction(input.goal->x - origin.x, input.goal->y - origin.y);
        const float distance = direction.Length();
        if (distance > .5f) {
            direction /= distance;
            const float yaw = DEG2RAD(player->viewAngles.yaw);
            const idVec2 forward(idMath::Cos(yaw), idMath::Sin(yaw));
            const idVec2 right(idMath::Sin(yaw), -idMath::Cos(yaw));
            command.forwardmove = static_cast<signed char>(idMath::ClampInt(-127, 127, int(127.f * (direction * forward))));
            command.rightmove = static_cast<signed char>(idMath::ClampInt(-127, 127, int(127.f * (direction * right))));
        }
    }
    if (input.attack) command.buttons |= BUTTON_ATTACK;
    if (input.crouch) command.upmove = -127;
    gameLocal.RunFrame(commands);
    ++qa.frames;
}
WolfSourcePickupsState Pickups(QA& qa, const WolfSourcePickups& owner) {
    WolfSourcePickupsState s;
    idStr why;
    qa.Need(owner.Query(s, why), why.IsEmpty() ? "pickup state unavailable" : why.c_str());
    return s;
}
const WolfSourceItemState& ItemState(QA& qa, const WolfSourcePickupsState& s, const char* name) {
    for (const auto& item : s.items) if (item.name == name) return item;
    qa.Need(false, std::string("pickup item absent: ") + name);
    return s.items.front();
}
WolfSourceTrainyardExitState Exit(QA& qa, const WolfSourceTrainyardExit& exit) {
    WolfSourceTrainyardExitState s;
    idStr why;
    qa.Need(exit.Query(s, why), why.IsEmpty() ? "exit state unavailable" : why.c_str());
    return s;
}
WolfSourceWeaponState Weapon(QA& qa, idPlayer* player) {
    WolfSourceWeaponState s;
    idStr why;
    qa.Need(Wolf2009_QuerySourceWeapon(player, s, why), why.IsEmpty() ? "weapon state unavailable" : why.c_str());
    return s;
}
unsigned Count(const WolfSourcePickupsState& s, const char* target, const char* operation, const char* outcome) {
    unsigned n = 0;
    for (const auto& r : s.receipts) n += r.target == target && r.operation == operation && r.outcome == outcome;
    return n;
}
// Synthetic specs through the production handlers (labelled QA).
class Resolver : public WolfSourceBrainActionResolver {
  public:
    WolfSourceSubject Resolve(const wolf2009::BrainEntityReference& reference) override {
        WolfSourceSubject out;
        idEntity* entity = nullptr;
        if (reference.mode == wolf2009::BrainEntityMode::Player) entity = gameLocal.GetLocalPlayer();
        else if (reference.mode == wolf2009::BrainEntityMode::Named) entity = gameLocal.FindEntity(reference.name.c_str());
        if (entity) { out.kind = entity == gameLocal.GetLocalPlayer() ? WolfSourceSubject::Player : WolfSourceSubject::Entity; out.entity = entity; }
        return out;
    }
    WolfSourcePlayerScriptState& PlayerState() override { return player; }
    WolfSourcePlayerScriptState player;
};
Resolver resolver;
void Apply(QA& qa, wolf2009::BrainActionKind kind, bool value, const char* named = nullptr) {
    wolf2009::BrainActionSpec spec;
    spec.kind = kind;
    spec.nodeType = static_cast<std::uint32_t>(kind);
    spec.value = value;
    spec.rawValue = value ? "true" : "false";
    spec.subject.mode = named ? wolf2009::BrainEntityMode::Named : wolf2009::BrainEntityMode::Player;
    spec.subject.raw = named ? named : "Player";
    if (named) spec.subject.name = named;
    spec.profile = std::string("qa-synthetic:") + wolf2009::BrainActionKindName(kind);
    WolfSourceBrainActionReceipt receipt;
    receipt.brain = "qa-synthetic";
    qa.Need(Wolf2009_ApplySourceBrainAction(spec, resolver, receipt) && receipt.outcome == "applied",
            std::string("QA brain action refused: ") + receipt.action + " " + receipt.effect);
}
// allies_contact_1 nodes 293/294: the original PlayerHasWeapon(weapon_mp40)
// condition records, evaluated against the native player each frame.
struct HasWeaponProbe {
    std::vector<wolf2009::BrainConditionSpec> specs;
    bool last = false;
    unsigned flips = 0, evaluations = 0;
    int flipTimeMs = -1;
    void Load(QA& qa) {
        void* buffer = nullptr;
        const int length = fileSystem->ReadFile("wolf2009/main/BRAI/brains/trainyard/allies_contact_1", &buffer);
        qa.Need(length > 0 && buffer, "allies_contact_1 BRAI is absent");
        std::vector<unsigned char> bytes(static_cast<unsigned char*>(buffer), static_cast<unsigned char*>(buffer) + length);
        fileSystem->FreeFile(buffer);
        const auto graph = wolf2009::DecodeBrainGraph(bytes);
        for (std::uint32_t id : {293u, 294u}) {
            const auto* node = graph.FindNode(id);
            qa.Need(node != nullptr, "allies_contact_1 wait node is absent");
            unsigned found = 0;
            for (const auto output : node->outputs)
                for (const auto record : graph.outputs[output].conditions) {
                    if (graph.records[record].tokens.empty() || graph.records[record].tokens[0] != "448") continue;
                    wolf2009::BrainConditionSpec spec;
                    std::string why;
                    qa.Need(wolf2009::DecodeBrainConditionV1(graph, record, spec, why) && spec.text == "weapon_mp40", "PlayerHasWeapon record refused: " + why);
                    specs.push_back(spec);
                    ++found;
                }
            qa.Need(found == 1, "allies node does not carry exactly one PlayerHasWeapon record");
        }
    }
    bool Evaluate(QA& qa) {
        bool value = false;
        for (std::size_t i = 0; i < specs.size(); ++i) {
            WolfSourceConditionReceipt receipt;
            qa.Need(Wolf2009_EvaluateSourceBrainCondition(specs[i], receipt) && receipt.outcome == "evaluated", "PlayerHasWeapon evaluation refused");
            qa.Need(i == 0 || receipt.raw == value, "allies 293/294 disagree");
            value = receipt.raw;
        }
        ++evaluations;
        if (value != last) { ++flips; flipTimeMs = gameLocal.time; last = value; }
        return value;
    }
};
// QA-only native damage definition (scalar "damage", no knockback).
const char* DamageDef(QA& qa, int amount) {
    static std::string name;
    name = "wolf2009/qa-player-damage/v1/" + std::to_string(amount);
    const std::string text = "{\n\"damage\" \"" + std::to_string(amount) + "\"\n\"knockback\" \"0\"\n}\n";
    if (!declManager->FindDeclWithoutParsing(DECL_ENTITYDEF, name.c_str(), false)) {
        idDecl* created = declManager->CreateNewDecl(DECL_ENTITYDEF, name.c_str(), "<wolf2009-qa-player-damage>");
        qa.Need(created != nullptr, "QA damage declaration allocation failed");
        created->SetText(text.c_str());
        created->Invalidate();
    }
    const auto* parsed = gameLocal.FindEntityDef(name.c_str(), false);
    qa.Need(parsed && parsed->dict.GetInt("damage") == amount, "QA damage declaration parse failed");
    return name.c_str();
}
void Damage(idPlayer* player, QA& qa, int amount) {
    player->Damage(gameLocal.world, gameLocal.world, idVec3(1, 0, 0), DamageDef(qa, amount), 1.0f, INVALID_JOINT);
}
// Walk with ordinary user commands until the predicate holds.
template <class F> void Walk(QA& qa, idPlayer* player, const idVec2& goal, unsigned budget, const char* what, HasWeaponProbe& probe, F done) {
    Input input;
    input.goal = &goal;
    for (unsigned i = 0;; ++i) {
        if (done()) return;
        qa.Need(i < budget, what);
        Frame(qa, player, input);
        probe.Evaluate(qa);
    }
}

void Run(QA& qa, bool disabledCase) {
    auto* map = Wolf2009_SourceSessionMapRuntime();
    qa.Need(map && Wolf2009_HasSourceSession(), "Ordinary source trainyard session absent");
    auto* pickups = map->Pickups();
    auto* exit = map->TrainyardExit();
    auto* player = gameLocal.GetLocalPlayer();
    qa.Need(pickups && exit && map->AIShell() && player, "Pickup, exit, AI shell owners or player absent");
    auto* flags = Wolf2009_SourcePlayerFlags(player);
    auto* inventory = Wolf2009_SourcePlayerInventory(player);
    qa.Need(flags && inventory, "Native source player flags/inventory absent");
    HasWeaponProbe probe;
    probe.Load(qa);
    qa.Need(!probe.Evaluate(qa), "PlayerHasWeapon true at map start");

    // Start: unarmed (weapon_none) and no inventory weapon. No producer of an
    // initial weapons-disabled bit exists in the trainyard data.
    auto start = Pickups(qa, *pickups);
    auto weapon = Weapon(qa, player);
    qa.Need(std::string(weapon.sourceWeapon) == "weapon_none" && !inventory->HasWeapon("weapon_mp40") &&
            !flags->weaponsDisabled && !flags->weaponsDisabledWrites && !flags->locked && !flags->undying,
            "Player did not start unarmed with clear source flags");
    qa.Need(start.items.size() == 4 && Wolf2009_LiveSourceItems() == 3 && start.clipSize == 32, "Pickup items or MP40 clip size differ");
    for (const char* name : {"weapon_mp_2", "item_dynamite", "ammo_mdl24_large"}) {
        const auto& item = ItemState(qa, start, name);
        qa.Need(item.alive && !item.armed && !item.hidden && item.triggerFirst && item.triggerSize == 32 && !item.inhibited,
                std::string(name) + ": initial item state differs");
        qa.Need(pickups->Item(name) && pickups->Item(name)->GetPhysics()->GetContents() == CONTENTS_TRIGGER, std::string(name) + ": no native touch volume");
    }
    qa.Need(ItemState(qa, start, "layer54_weapon_flammenwerfer_1").inhibited && !pickups->Item("layer54_weapon_flammenwerfer_1") &&
            Count(start, "layer54_target_tip_1", "spawn", "spawn-inhibited") == 1, "DLC flamethrower/tip were not spawn-inhibited");

    // shake_train node 31 EnablePlayerWeapons(true) writes the native flag.
    unsigned guard = 0;
    auto shake = Exit(qa, *exit);
    while (!shake.shakeEnded) {
        qa.Need(++guard <= 1600, "shake_train did not reach node 34");
        Frame(qa, player);
        shake = Exit(qa, *exit);
    }
    qa.Need(shake.shakeActions.size() == 4 && shake.shakeActions[0].node == 31 && shake.shakeActions[0].outcome == "applied" &&
            flags->weaponsDisabledWrites == 1 && !flags->weaponsDisabled && shake.shakePlayer.lastWeaponEvent == "weapons-enabled" &&
            flags->undyingWrites == 1 && !flags->undying, "shake_train nodes 31/34 did not write the native player flags");

    // Declared QA teleport next to the pickups: floor below a point north of the MP40.
    const auto* mp40Entity = pickups->Item("weapon_mp_2");
    const idVec3 mp40 = mp40Entity->GetPhysics()->GetOrigin();
    const idVec2 approach(mp40.x, mp40.y + 80.f), mp40Goal(mp40.x, mp40.y);
    trace_t floor;
    gameLocal.clip.TracePoint(floor, idVec3(approach.x, approach.y, mp40.z + 60.f), idVec3(approach.x, approach.y, mp40.z - 200.f), MASK_PLAYERSOLID, player);
    qa.Need(floor.fraction < 1 && floor.c.normal.z > .7f, "No floor below the QA approach point");
    player->SetOrigin(floor.endpos + idVec3(0, 0, 1));
    player->SetViewAngles(idAngles(0, -90, 0));
    player->GetPhysics()->SetLinearVelocity(vec3_origin);
    common->Printf("WOLF2009_TRAINYARD_PICKUP_QA_TELEPORT declared=1 origin=(%.2f %.2f %.2f) mp40=(%.2f %.2f %.2f)\n",
                   floor.endpos.x, floor.endpos.y, floor.endpos.z, mp40.x, mp40.y, mp40.z);
    for (unsigned i = 0; i < 10; ++i) Frame(qa, player);
    const auto settled = player->GetPhysics()->GetOrigin();
    qa.Need(std::fabs(settled.x - approach.x) < 1 && std::fabs(settled.y - approach.y) < 1 && settled.z > mp40.z - 120,
            "Player did not settle at the QA approach point");

    // Before activate_weapons: touching the MP40 does nothing.
    Walk(qa, player, mp40Goal, 240, "Player did not reach the unarmed MP40", probe,
         [&] { return ItemState(qa, Pickups(qa, *pickups), "weapon_mp_2").touches > 0; });
    for (unsigned i = 0; i < 5; ++i) Frame(qa, player);
    auto unarmed = Pickups(qa, *pickups);
    qa.Need(ItemState(qa, unarmed, "weapon_mp_2").refusedTouches > 0 && ItemState(qa, unarmed, "weapon_mp_2").pickups == 0 &&
            Count(unarmed, "weapon_mp_2", "pickup", "applied") == 0 && !inventory->HasWeapon("weapon_mp40"),
            "Unarmed MP40 touch was not refused");
    // Step back outside every touch volume.
    Walk(qa, player, approach, 240, "Player did not return to the approach point", probe, [&] {
        const auto o = player->GetPhysics()->GetOrigin();
        return std::fabs(o.x - approach.x) < 2 && std::fabs(o.y - approach.y) < 2;
    });
    if (disabledCase) Apply(qa, wolf2009::BrainActionKind::EnablePlayerWeapons, false);

    // QA stand-in for the allies frame-600 trigger: activate_weapons arms the items.
    pickups->Relay("activate_weapons")->ProcessEvent(&EV_Activate, static_cast<idEntity*>(nullptr));
    auto armed = Pickups(qa, *pickups);
    qa.Need(armed.activateRelay.activations == 1 && Count(armed, "layer54_weapon_flammenwerfer_1", "activate", "spawn-inhibited") == 1,
            "activate_weapons did not fire once");
    for (const char* name : {"weapon_mp_2", "item_dynamite", "ammo_mdl24_large"}) {
        const auto& item = ItemState(qa, armed, name);
        qa.Need(item.armed && item.arms == 1 && item.pickups == 0 && !item.hidden, std::string(name) + ": not armed once by activate_weapons");
    }
    Frame(qa, player);

    if (disabledCase) {
        // Weapons disabled: CanPickup refuses the MP40 on touch.
        const unsigned before = ItemState(qa, Pickups(qa, *pickups), "weapon_mp_2").refusedTouches;
        Walk(qa, player, mp40Goal, 240, "Player did not reach the MP40 while disabled", probe,
             [&] { return ItemState(qa, Pickups(qa, *pickups), "weapon_mp_2").refusedTouches > before; });
        auto refused = Pickups(qa, *pickups);
        qa.Need(ItemState(qa, refused, "weapon_mp_2").pickups == 0 && !inventory->HasWeapon("weapon_mp40") &&
                ItemState(qa, refused, "weapon_mp_2").lastRefusal.find("disabled") != std::string::npos, "MP40 pickup was not refused while disabled");
        // Dynamite has no inv_weapon: its touch is accepted and the relay runs.
        const auto* dynamite = pickups->Item("item_dynamite");
        const idVec3 d = dynamite->GetPhysics()->GetOrigin();
        const idVec2 dynamiteGoal(d.x, d.y);
        Walk(qa, player, dynamiteGoal, 240, "Player did not collect the dynamite", probe,
             [&] { return ItemState(qa, Pickups(qa, *pickups), "item_dynamite").pickups > 0; });
        auto partial = Pickups(qa, *pickups);
        const auto& mp = ItemState(qa, partial, "weapon_mp_2");
        const auto& dy = ItemState(qa, partial, "item_dynamite");
        const auto& am = ItemState(qa, partial, "ammo_mdl24_large");
        qa.Need(partial.pickupRelay.activations == 1 && dy.pickups == 2 && dy.hidden && mp.pickups == 0 && mp.giveRefusals == 1 &&
                !mp.hidden && mp.armed && am.pickups == 0 && am.giveRefusals == 1 && !am.hidden && !inventory->HasWeapon("weapon_mp40") &&
                !inventory->HasWeapon("weapon_grenade") && !probe.last, "Disabled relay pass: dynamite accepted, MP40/grenades refused");
        // The volumes overlap: leave them all so the next touch is the MP40's.
        Walk(qa, player, approach, 300, "Player did not leave the pickup volumes", probe, [&] {
            const auto o = player->GetPhysics()->GetOrigin();
            return std::fabs(o.x - approach.x) < 2 && std::fabs(o.y - approach.y) < 2;
        });
        Apply(qa, wolf2009::BrainActionKind::EnablePlayerWeapons, true);
    }

    // Touch one MP40: the relay collects the set synchronously.
    const auto beforeTouch = Pickups(qa, *pickups);
    Walk(qa, player, mp40Goal, 240, "Player did not collect the MP40", probe,
         [&] { return ItemState(qa, Pickups(qa, *pickups), "weapon_mp_2").pickups > 0; });
    const int pickupTime = gameLocal.time;
    auto collected = Pickups(qa, *pickups);
    const auto& mp = ItemState(qa, collected, "weapon_mp_2");
    const auto& dy = ItemState(qa, collected, "item_dynamite");
    const auto& am = ItemState(qa, collected, "ammo_mdl24_large");
    // MP40 gives, in order: touch pickup (weapon + 32), relay re-activation (weapon overflow, + 32).
    std::vector<std::string> mpGives;
    for (const auto& g : collected.gives)
        if (g.item == "weapon_mp_2" && (g.give.key == "weapon" || g.give.key == "ammo_mp40"))
            mpGives.push_back(std::to_string(g.pickup) + ":" + g.give.key + ":" + g.give.outcome + ":" + std::to_string(int(g.give.after)));
    const std::vector<std::string> expectedGives = {"1:ammo_mp40:given:32", "1:weapon:given:1", "2:ammo_mp40:given:64", "2:weapon:refused:1"};
    qa.Need(mpGives == expectedGives, "MP40 give sequence differs: touched item is given twice by the relay re-activation");
    qa.Need(mp.pickups == 2 && mp.hidden && collected.pickupRelay.activations == beforeTouch.pickupRelay.activations + 1 &&
            collected.pickupRelay.reentries == beforeTouch.pickupRelay.reentries + (disabledCase ? 2u : 3u) && collected.maxDepth == 2 &&
            am.pickups == 1 && am.hidden && inventory->HasWeapon("weapon_mp40") && inventory->HasWeapon("weapon_grenade"),
            "One MP40 touch did not collect the set through the relay");
    if (disabledCase) qa.Need(dy.pickups == 2 && dy.shows == 1 && !dy.hidden, "Hidden dynamite was not shown again by the relay");
    else qa.Need(dy.pickups == 1 && dy.hidden && dy.shows == 0, "Dynamite was not collected once by the relay");
    for (const char* name : {"weapon_mp_2", "ammo_mdl24_large"})
        qa.Need(pickups->Item(name)->GetPhysics()->GetContents() == 0, std::string(name) + ": contents not cleared after pickup");
    qa.Need(collected.equips == 1 && collected.equipPool == 64 && collected.equipClip == 32 && collected.equipReserve == 32,
            "MP40 inventory pool did not become the source weapon's 32/32");
    weapon = Weapon(qa, player);
    qa.Need(std::string(weapon.sourceWeapon) == "weapon_mp40" && weapon.clip == 32 && weapon.reserve == 32 && weapon.hasViewModel,
            "Source weapon controller does not hold the picked-up MP40");
    const float grenadeAmmo = inventory->Ammo(5).amount;
    qa.Need(probe.Evaluate(qa) && probe.flips == 1, "PlayerHasWeapon did not flip once");

    // Double touch: the player stays in the volumes; nothing is picked up again.
    for (unsigned i = 0; i < 20; ++i) { Frame(qa, player, Input()); probe.Evaluate(qa); }
    auto still = Pickups(qa, *pickups);
    qa.Need(ItemState(qa, still, "weapon_mp_2").pickups == 2 && ItemState(qa, still, "ammo_mdl24_large").pickups == 1 &&
            still.equips == 1 && probe.flips == 1, "A second touch picked up again");

    // Firing: allowed, then blocked while bit 3 is set, then allowed again.
    Input fire;
    fire.attack = true;
    const unsigned shots0 = Weapon(qa, player).shots;
    for (unsigned i = 0; i < 30; ++i) Frame(qa, player, fire);
    const unsigned shots1 = Weapon(qa, player).shots;
    for (unsigned i = 0; i < 10; ++i) Frame(qa, player);
    Apply(qa, wolf2009::BrainActionKind::EnablePlayerWeapons, false);
    const unsigned suppressed0 = flags->attackSuppressedFrames;
    for (unsigned i = 0; i < 30; ++i) Frame(qa, player, fire);
    const auto blocked = Weapon(qa, player);
    qa.Need(blocked.shots == shots1 && flags->attackSuppressedFrames >= suppressed0 + 30 && blocked.hasViewModel && !blocked.unsupported,
            "Attack was not blocked while weapons were disabled");
    for (unsigned i = 0; i < 5; ++i) Frame(qa, player);
    Apply(qa, wolf2009::BrainActionKind::EnablePlayerWeapons, true);
    for (unsigned i = 0; i < 30; ++i) Frame(qa, player, fire);
    const unsigned shots2 = Weapon(qa, player).shots;
    qa.Need(shots1 > shots0 && shots2 > shots1, "MP40 did not fire before and after the disabled interval");

    // Item removal 5000 ms after the pickup, then a stale relay fire.
    guard = 0;
    while (pickups->Item("weapon_mp_2") || pickups->Item("ammo_mdl24_large") || pickups->Item("item_dynamite")) {
        qa.Need(++guard <= 1000, "Items were not removed after 5000 ms");
        Frame(qa, player);
    }
    auto removed = Pickups(qa, *pickups);
    for (const char* name : {"weapon_mp_2", "item_dynamite", "ammo_mdl24_large"}) {
        const auto& item = ItemState(qa, removed, name);
        qa.Need(item.removed && !item.alive && item.removedTimeMs >= item.removalDueMs && item.removalDueMs > 0 &&
                item.removedTimeMs - item.firstPickupMs >= 5000 && item.removedTimeMs - item.firstPickupMs < 5100,
                std::string(name) + ": removal time differs from the 5000 ms contract");
    }
    qa.Need(Wolf2009_LiveSourceItems() == 0, "Removed items are still live");
    pickups->Relay("trigger_relay_weapon_pickup")->ProcessEvent(&EV_Activate, player);
    auto stale = Pickups(qa, *pickups);
    qa.Need(Count(stale, "weapon_mp_2", "activate", "target-expired") == 1 && Count(stale, "item_dynamite", "activate", "target-expired") == 1 &&
            Count(stale, "ammo_mdl24_large", "activate", "target-expired") == 1 && stale.equips == 1 && inventory->Ammo(5).amount == grenadeAmmo,
            "Stale relay targets were not skipped");

    // takedamage and undying gate player Damage.
    const int healthStart = player->health;
    Apply(qa, wolf2009::BrainActionKind::EnableDamage, false);
    Damage(player, qa, 10);
    qa.Need(player->health == healthStart && !player->fl.takedamage, "Damage applied without takedamage");
    Apply(qa, wolf2009::BrainActionKind::EnableDamage, true);
    Damage(player, qa, 10);
    const int damaged = player->health;
    qa.Need(damaged < healthStart && damaged > 0, "Damage was not applied with takedamage");
    Apply(qa, wolf2009::BrainActionKind::SetUndying, true);
    Damage(player, qa, 1000);
    qa.Need(player->health == 1 && flags->undyingClamps == 1, "Undying did not keep health at 1");
    Apply(qa, wolf2009::BrainActionKind::SetUndying, false);
    player->health = 100;
    flags->undyingUntilMs = gameLocal.time + 3500;  // QA stand-in for rvFuncSaveGame (+0x960)
    Damage(player, qa, 1000);
    qa.Need(player->health == 1 && flags->undyingClamps == 2, "Undying-until time did not keep health at 1");
    player->health = 100;
    guard = 0;
    while (gameLocal.time < flags->undyingUntilMs) { qa.Need(++guard <= 400, "Undying window did not end"); Frame(qa, player); }
    Damage(player, qa, 10);
    qa.Need(player->health < 100 && player->health > 1 && flags->undyingClamps == 2, "Damage after the undying window differs");
    common->Printf("WOLF2009_TRAINYARD_PICKUP_DAMAGE health_start=%d after_takedamage=%d undying_clamps=%u final=%d\n",
                   healthStart, damaged, flags->undyingClamps, player->health);

    // Lock gates the crouch toggle; unlock restores it.
    WolfSourcePlayerState ps;
    idStr why;
    Input crouch;
    crouch.crouch = true;
    Apply(qa, wolf2009::BrainActionKind::LockPlayer, true);
    for (unsigned i = 0; i < 10; ++i) Frame(qa, player, crouch);
    qa.Need(Wolf2009_QuerySourcePlayer(player, ps, why) && !ps.ducked && flags->crouchToggleRefusals > 0, "Locked player crouched");
    Apply(qa, wolf2009::BrainActionKind::UnlockPlayer, true);
    for (unsigned i = 0; i < 10; ++i) Frame(qa, player, crouch);
    qa.Need(Wolf2009_QuerySourcePlayer(player, ps, why) && ps.ducked && !flags->locked, "Unlocked player did not crouch");
    for (unsigned i = 0; i < 20; ++i) Frame(qa, player);
    qa.Need(Wolf2009_QuerySourcePlayer(player, ps, why) && !ps.ducked, "Player did not stand after crouch release");

    // SetUsable on the shell's greeter body (use-focus consumer is not native).
    auto* greeter = gameLocal.FindEntity("train_greeter");
    qa.Need(greeter && Wolf2009_SourceEntityUsable(greeter) == -1, "Greeter usable flag already written");
    Apply(qa, wolf2009::BrainActionKind::SetUsable, false, "train_greeter");
    qa.Need(Wolf2009_SourceEntityUsable(greeter) == 0 && Wolf2009_LiveSourceEntityFlags() == 1, "SetUsable did not record the greeter");

    common->Printf("WOLF2009_TRAINYARD_PICKUP_QA_OK checks=%u frames=%u case=%s teleport=declared activate_weapons=qa "
                   "mp40_pickups=2 mp40_pool=%g clip=%d reserve=%d grenade_ammo=%g relay_reentries=%u has_weapon_flips=%u "
                   "shots=%u/%u/%u pickup_time=%d\n",
                   qa.checks, qa.frames, disabledCase ? "disabled" : "main", double(collected.equipPool), collected.equipClip,
                   collected.equipReserve, double(grenadeAmmo), collected.pickupRelay.reentries, probe.flips, shots0, shots1,
                   shots2, pickupTime);
}
}  // namespace

extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardPickupQA() {
    QA qa;
    try {
        Run(qa, Flag("fixtures/trainyard-pickups-disabled.enabled"));
        return 1;
    } catch (const std::exception& error) {
        common->Warning("WOLF2009_TRAINYARD_PICKUP_QA_FAIL checks=%u frames=%u reason=%s", qa.checks, qa.frames, error.what());
        return 0;
    }
}

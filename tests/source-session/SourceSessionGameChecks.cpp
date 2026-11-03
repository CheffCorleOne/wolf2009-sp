// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceSession.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceWeapon.h"
#include "NativeSourceWeaponView.h"

// Forward declarations of native test runners in game module
extern "C" int Wolf2009_RunSourcePlayerHullNativeChecks(void);
bool Wolf2009_CheckNativeSourcePlayerQA(idPlayer* player);
bool Wolf2009_CheckNativeSourceWeaponQA(idPlayer* player, const WolfSourceDeclarationSet& set, const WolfSourceWeaponVisual* visual);

namespace {
struct ReleaseView {void operator()(WolfSourceWeaponView* view)const{if(view)Wolf2009_ReleaseSourceWeaponView(view);}};
unsigned checks = 0;
void Need(bool condition, const char* reason) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(reason);
    }
}
}

extern "C" __attribute__((visibility("default"))) int Wolf2009_RunSourceSessionQA(void) {
    checks = 0;
    try {
        Need(Wolf2009_HasSourceSession(), "Wolf2009 source session is not active");
        const WolfSourceDeclarationSet* set = Wolf2009_SourceSessionDeclarations();
        Need(set != NULL, "Wolf2009 source session declaration set is absent");

        idPlayer* player = gameLocal.GetLocalPlayer();
        Need(player != NULL, "Local SP player is absent in source session");
        Need(Wolf2009_IsSourcePlayer(player), "Local player is not an idWolfSourcePlayer");

        // 1. Run native wide-TRM hull and cache checks
        Need(Wolf2009_RunSourcePlayerHullNativeChecks() == 1, "Native player hull QA checks failed");

        // 2. Run source player lifecycle and collision checks
        Need(Wolf2009_CheckNativeSourcePlayerQA(player), "Native source player QA checks failed");

        // 3. Exercise stock input guards: NextWeapon, PrevWeapon, SelectWeapon, NextBestWeapon, DropWeapon
        // Source player must not enter infinite stock loops or crash when these are invoked.
        player->NextWeapon();
        player->PrevWeapon();
        player->SelectWeapon(0, false);
        player->SelectWeapon(1, false);
        player->SelectWeapon(2, false);
        player->NextBestWeapon();
        player->DropWeapon(false);
        Need(player->health > 0, "Stock weapon input guard corrupted player state");

        // 4. Run source weapon MP40 controller, damage, and reload checks (with explicit null view)
        Need(Wolf2009_CheckNativeSourceWeaponQA(player, *set, NULL), "Native source weapon QA checks failed");
        // Repeat the observable input/damage/ammo lifecycle with the real
        // model, source-time clips and presentation owner attached.
        {
            const int previousViews=Wolf2009_LiveSourceWeaponViews();
            WolfSourceWeaponViewRequest request;Wolf2009_SourceMP40FixtureViewRequest(request);
            idStr viewReason;
            std::unique_ptr<WolfSourceWeaponView,ReleaseView> view(
                Wolf2009_CreateSourceWeaponView(*set,request,viewReason));
            Need(bool(view),viewReason.IsEmpty()?"Native MP40 view creation failed":viewReason.c_str());
            WolfSourceWeaponVisual visual;
            Need(Wolf2009_SourceWeaponViewDescriptor(view.get(),visual,viewReason),
                viewReason.IsEmpty()?"Native MP40 view descriptor failed":viewReason.c_str());
            Need(Wolf2009_CheckNativeSourceWeaponQA(player,*set,&visual),"Native visible source weapon QA checks failed");
            view.reset();
            Need(Wolf2009_LiveSourceWeaponViews()==previousViews,"Native visible weapon QA leaked its view owner");
        }

        // 5. Test explicit dev-equip helper for the current session
        const unsigned previousViews=Wolf2009_LiveSourceWeaponViews();
        idStr equipReason;
        Need(Wolf2009_DevEquipSourceSessionMP40(player, equipReason),
            equipReason.IsEmpty() ? "Dev MP40 equip failed" : equipReason.c_str());
        WolfSourceWeaponState equippedState;
        idStr queryReason;
        Need(Wolf2009_QuerySourceWeapon(player, equippedState, queryReason),
            queryReason.IsEmpty() ? "Source weapon query failed after dev equip" : queryReason.c_str());
        Need(equippedState.sourceWeapon && std::string(equippedState.sourceWeapon) == "weapon_mp40",
            "Dev equip did not activate weapon_mp40");
        Need(equippedState.clip == 32 && equippedState.reserve == 32,
            "Dev equip did not load 32 clip and 32 reserve");
        Need(equippedState.hasViewModel&&Wolf2009_LiveSourceWeaponViews()==previousViews+1,
            "Dev equip did not retain its original MP40 view");

        // Reset back to none after dev equip verification
        Wolf2009_ResetSourceWeaponForPlayer(player);
        Need(Wolf2009_QuerySourceWeapon(player, equippedState, queryReason),
            queryReason.IsEmpty() ? "Source weapon query failed after dev reset" : queryReason.c_str());
        Need(equippedState.sourceWeapon && std::string(equippedState.sourceWeapon) == "weapon_none",
            "Reset did not restore weapon_none");
        Need(!equippedState.hasViewModel&&Wolf2009_LiveSourceWeaponViews()==previousViews,
            "Dev reset leaked its MP40 view/presentation owner");

        // 6. Test transactional reentry safety: a repeated BeginSourceSession must refuse without clearing active state
        idStr reentryReason;
        bool reentered = Wolf2009_BeginSourceSession("wolf2009/main/PROC/maps/game/empty/empty", reentryReason);
        Need(!reentered, "Reentrant BeginSourceSession was unexpectedly accepted");
        Need(!reentryReason.IsEmpty(), "Reentrant BeginSourceSession gave no failure reason");
        Need(Wolf2009_HasSourceSession(), "Reentrant BeginSourceSession destroyed active session");
        Need(Wolf2009_SourceSessionDeclarations() == set, "Reentrant BeginSourceSession altered active declarations");

        common->Printf("WOLF2009_SOURCE_SESSION_GAME_QA_OK checks=%u player_hull=1 player_lifecycle=1 weapon_qa=1 stock_input_guarded=1 dev_equip=1 reentry_safe=1 dev_visual=1\n", checks);
        return 1;
    } catch (const std::exception& error) {
        common->Warning("WOLF2009_SOURCE_SESSION_GAME_QA_FAIL checks=%u reason=%s", checks, error.what());
        return 0;
    }
}

extern "C" __attribute__((visibility("default"))) int Wolf2009_FinishSourceSessionQA(void) {
    checks = 0;
    try {
        gameLocal.MapShutdown();

        Need(Wolf2009_LiveSourcePlayers() == 0, "Source players leaked after MapShutdown");
        Need(Wolf2009_LiveSourceWeapons() == 0, "Source weapons leaked after MapShutdown");
        Need(Wolf2009_LiveSourceWeaponViews()==0,"Source weapon view leaked after MapShutdown");
        Need(!Wolf2009_HasSourcePlayerProfile(), "Source player profile remained installed after MapShutdown");
        Need(!Wolf2009_HasSourceSession(), "Source session remained active after MapShutdown");

        common->Printf("WOLF2009_SOURCE_SESSION_TEARDOWN_QA_OK live_players=0 live_weapons=0 live_profile=0 live_session=0\n");
        return 1;
    } catch (const std::exception& error) {
        common->Warning("WOLF2009_SOURCE_SESSION_TEARDOWN_QA_FAIL reason=%s", error.what());
        return 0;
    }
}

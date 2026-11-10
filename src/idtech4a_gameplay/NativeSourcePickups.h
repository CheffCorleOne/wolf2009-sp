// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_PICKUPS_H
#define WOLF2009_NATIVE_SOURCE_PICKUPS_H
// Game-private C++11 owner for the trainyard weapon pickups
// (SourcePickupInventoryV1): the activate_weapons and
// trigger_relay_weapon_pickup relays (idTrigger_Multi, wait 0, delay 0) and the
// weapon_mp_2, item_dynamite and ammo_mdl24_large idItems with native touch
// volumes. The DLC flamethrower and its tip are spawn-inhibited, as the
// original does without the pre-order entitlement. Every unmodelled step is
// recorded as an explicit receipt. Contracts: docs/PLAYER-WEAPON.md.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SourceInventory.h"
class idEntity;class idPlayer;class idStr;class WolfSourceDeclarationSet;
namespace wolf2009 {struct SourceMapBodyPlan;}

struct WolfSourceItemState {
    std::string name,classname,inhibitReason,lastRefusal;
    unsigned entityIndex=0,spawnId=0;
    bool inhibited=false,alive=false,triggerFirst=false,armed=false,hidden=false,removed=false;
    unsigned touches=0,refusedTouches=0,activations=0,arms=0,shows=0,pickups=0,giveRefusals=0,removalPosts=0;
    int armTimeMs=-1,firstPickupMs=-1,lastPickupMs=-1,removalDueMs=-1,removedTimeMs=-1;
    float origin[3]={0,0,0},triggerSize=0;
    std::vector<std::string> targets;
};
struct WolfSourcePickupRelayState {
    std::string name;
    unsigned activations=0,reentries=0;
    int lastFireMs=-1;
    std::vector<std::string> targets;
};
// operation: touch, activate, pickup, give, fire, equip, spawn.
// outcome: applied, refused, reentry-ignored, spawn-inhibited, target-expired,
// not-modelled.
struct WolfSourcePickupReceipt {
    std::string source,target,operation,outcome,reason,actor;
    unsigned depth=0;
    int timeMs=-1;
};
struct WolfSourcePickupGive {
    std::string item;
    unsigned pickup=0;
    wolf2009::SourceGiveReceipt give;
    int timeMs=-1;
};
struct WolfSourcePickupsState {
    std::uint64_t generation=0;
    std::vector<WolfSourceItemState> items;
    WolfSourcePickupRelayState activateRelay,pickupRelay;
    std::vector<WolfSourcePickupReceipt> receipts;
    std::vector<WolfSourcePickupGive> gives;
    unsigned equips=0,maxDepth=0,touchDispatches=0;
    int equipClip=-1,equipReserve=-1,equipTimeMs=-1,clipSize=0;
    float equipPool=0;
    std::string equipOutcome;
};
class WolfSourcePickups {
  public:
    ~WolfSourcePickups();
    bool Spawn(idStr&);
    bool Query(WolfSourcePickupsState&,idStr&)const;
    idEntity* Item(const std::string& name)const;
    idEntity* Relay(const std::string& name)const;
    WolfSourcePickups(const WolfSourcePickups&)=delete;
    WolfSourcePickups& operator=(const WolfSourcePickups&)=delete;
    struct Impl;
  private:
    std::shared_ptr<Impl> impl;
    explicit WolfSourcePickups(std::shared_ptr<Impl>);
    friend std::unique_ptr<WolfSourcePickups> Wolf2009_PrepareSourcePickups(const WolfSourceDeclarationSet&,
        const wolf2009::SourceMapBodyPlan&,const std::string& profile,idStr&);
};
// Data-only qualification of the original ENTS and class definitions.
// plan must outlive the owner.
std::unique_ptr<WolfSourcePickups> Wolf2009_PrepareSourcePickups(const WolfSourceDeclarationSet&,
    const wolf2009::SourceMapBodyPlan&,const std::string& profile,idStr&);
int Wolf2009_LiveSourcePickupOwners();
int Wolf2009_LiveSourceItems();
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceWeapon.h"
#include "SourceDeclarationSetRuntime.h"
#include "SourceWeaponProfile.h"

class idWolfSourceWeaponQaTarget : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceWeaponQaTarget);
    void Spawn(){
        health=1000;fl.takedamage=true;
        const idTraceModel box(idBounds(idVec3(-6,-6,-6),idVec3(6,6,6)));
        GetPhysics()->SetClipModel(new idClipModel(box),1.0f);
        GetPhysics()->SetContents(CONTENTS_BODY);GetPhysics()->LinkClip();
    }
};
CLASS_DECLARATION(idEntity,idWolfSourceWeaponQaTarget)
END_CLASS

namespace {
unsigned checks=0,frames=0;
void Need(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
WolfSourceWeaponState State(idPlayer* player){WolfSourceWeaponState state;idStr reason;
    Need(Wolf2009_QuerySourceWeapon(player,state,reason),reason.IsEmpty()?"Native source weapon state is absent":reason.c_str());
    Need(!state.unsupported,reason.IsEmpty()?"Native source weapon refused runtime input":reason.c_str());return state;
}
void Frame(idPlayer* player,bool fire){
    usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));commands[player->entityNumber].buttons=fire?BUTTON_ATTACK:0;
    gameLocal.RunFrame(commands);++frames;
}
struct Cleanup {
    idEntityPtr<idEntity> target;
    idEntityPtr<idPlayer> player;
    bool reset=true;
    ~Cleanup(){if(auto* value=target.GetEntity())delete value;if(reset&&player.GetEntity())Wolf2009_ResetSourceWeaponForPlayer(player.GetEntity());}
};
}
bool Wolf2009_CheckNativeSourceWeaponQA(idPlayer* player,const WolfSourceDeclarationSet& set,const WolfSourceWeaponVisual* visual){
    checks=frames=0;try{
        Need(player&&gameLocal.GameState()==GAMESTATE_ACTIVE&&!gameLocal.isClient&&!gameLocal.isMultiplayer,"Native weapon QA requires an isolated active SP player");
        Cleanup cleanup;cleanup.player=player;
        idStr reason;if(!Wolf2009_IsSourceWeaponOwner(player))Need(Wolf2009_AttachSourceWeapon(player,set,"weapon_none",reason),reason.c_str());
        const int before=Wolf2009_LiveSourceWeapons();Need(before==1,"Native weapon QA requires one source player controller");
        auto state=State(player);Need(state.sourceWeapon&&std::string(state.sourceWeapon)=="weapon_none"&&state.clip==0&&state.shots==0,"Campaign source startup invented a gun/ammo");
        for(int i=0;i<3;++i)Frame(player,true);Need(State(player).shots==0,"weapon_none handled attack as a bullet");Frame(player,false);
        const auto profile=wolf2009::PrepareSourceWeaponProfile(set.Definitions(),"weapon_mp40",wolf2009::SourceWeaponPolicy::MP40PrimaryNativeV1);
        const int size=profile->Configuration().clipSize,interval=profile->Configuration().autoFireIntervalMs;
        Need(size==32&&interval==120&&profile->Configuration().damage==5,"Native weapon QA selected a substitute weapon configuration");
        Need(Wolf2009_EquipSourceWeapon(player,set,"weapon_mp40",visual,size,5,reason),reason.IsEmpty()?"Source MP40 equip failed":reason.c_str());
        state=State(player);Need(state.hasViewModel==(visual!=NULL)&&state.clip==size&&state.reserve==5&&Wolf2009_LiveSourceWeapons()==before,"MP40 equip lost view/ammo ownership or leaked previous controller");
        Need(player->inventory.clip[2]==size&&player->inventory.ammo[1]==5,"Native source inventory adapter was not updated");
        Need(!Wolf2009_EquipSourceWeapon(player,set,"weapon_unsupported",visual,size,5,reason)&&!reason.IsEmpty(),"Unimplemented equipped class was silently substituted");
        auto unchanged=State(player);Need(unchanged.clip==size&&unchanged.shots==0&&Wolf2009_LiveSourceWeapons()==before,"Rejected equip changed ammo/controller allocation");
        if(visual){auto invalid=*visual;invalid.reloadDurationSeconds=-1;
            Need(!Wolf2009_EquipSourceWeapon(player,set,"weapon_mp40",&invalid,size,5,reason)&&!reason.IsEmpty(),"Invalid source view timing was accepted");
            Need(State(player).clip==size&&State(player).shots==0&&Wolf2009_LiveSourceWeapons()==before,"Rejected visual mutated a live controller");}

        Frame(player,false);idVec3 eye;idMat3 axes;player->GetViewPos(eye,axes);
        trace_t available;gameLocal.clip.TracePoint(available,eye,eye+axes[0]*80,MASK_SHOT_BOUNDINGBOX,player);
        const float distance=available.fraction*80;
        Need(distance>24,"Native source player has no short forward QA ray clear of world geometry");
        auto* target=static_cast<idWolfSourceWeaponQaTarget*>(gameLocal.SpawnEntityType(idWolfSourceWeaponQaTarget::Type,NULL));
        Need(target!=NULL,"Native source weapon target allocation failed");cleanup.target=target;
        const idVec3 targetOrigin=eye+axes[0]*std::min(48.0f,distance*.5f);target->SetOrigin(targetOrigin);target->GetPhysics()->LinkClip();
        const int initialHealth=target->health;
        Frame(player,true);state=State(player);
        Need(state.shots==1&&state.clip==size-1&&state.frameCommands==1&&state.damageCalls==1&&state.lastHitEntity==target->entityNumber&&
             target->health==initialHealth-int(profile->Configuration().damage),"Source frame1 weapon_attack did not reach native TracePoint/Damage exactly once");
        const int firstTime=state.lastShotTime;Need(firstTime==gameLocal.time,"Native source weapon uses a different simulation clock");
        Need(Wolf2009_UpdateSourceWeapon(player,true,true)&&State(player).shots==1,"Repeated same native frame fired twice");
        target->SetOrigin(targetOrigin+axes[1]*48);target->GetPhysics()->LinkClip();
        const int afterHitHealth=target->health;
        do{Frame(player,true);state=State(player);if(gameLocal.time<=firstTime+interval)Need(state.shots==1,"Native Hold fired before its strict cooldown");}while(state.shots<2&&gameLocal.time<firstTime+interval+64);
        Need(state.shots==2&&state.clip==size-2&&state.misses>=1&&state.damageCalls==1&&target->health==afterHitHealth,"Native miss damaged an off-ray target or consumed incorrect ammo");
        Frame(player,false);const unsigned released=State(player).shots;for(int i=0;i<12;++i)Frame(player,false);
        Need(State(player).shots==released,"Released trigger kept firing");
        const int deadline=gameLocal.time+(size+1)*(interval+32);
        while(State(player).clip>0&&gameLocal.time<deadline)Frame(player,true);
        state=State(player);Need(state.clip==0&&state.shots==unsigned(size)&&state.frameCommands==unsigned(size),"Held MP40 did not empty its source clip exactly once per weapon_attack");
        for(int i=0;i<12;++i)Frame(player,true);state=State(player);
        Need(state.clip==0&&state.emptyClicks==1&&state.shots==unsigned(size),"Empty held MP40 fired negative ammo or repeated empty click");
        Frame(player,false);Frame(player,true);Need(State(player).emptyClicks==2,"Native empty click did not observe a release/press");Frame(player,false);
        Need(Wolf2009_ReloadSourceWeapon(player),"Source owner did not consume reload input");Frame(player,false);state=State(player);
        Need(state.phase==int(wolf2009::SourceWeaponPhase::Reloading)&&state.clip==0&&state.reserve==5&&state.reloadEndTime>gameLocal.time,
            "Native reload committed reserve immediately or failed to start");
        const int reloadEnd=state.reloadEndTime;const unsigned beforeReloadCommands=state.frameCommands;
        while(gameLocal.time<reloadEnd){Frame(player,true);state=State(player);if(gameLocal.time<reloadEnd)Need(state.clip==0&&state.shots==unsigned(size),"Reload fired or transferred ammo before source endpoint");}
        Frame(player,false);state=State(player);
        // Held input may fire once on the first ready frame at/after completion.
        Need(state.reserve==0&&state.clip>=4&&state.clip<=5&&state.clip+int(state.shots)-size==5&&state.reloads==1&&
            state.frameCommands>=beforeReloadCommands+5,"Native source-duration reload lost ammo conservation/frame crossings");
        Need(player->inventory.clip[2]==state.clip&&player->inventory.ammo[1]==state.reserve,"Native source ammo state diverged from player inventory");
        const unsigned shotsBeforeDisable=state.shots;Need(Wolf2009_UpdateSourceWeapon(player,true,false),"Source input disable was not consumed");
        Need(State(player).shots==shotsBeforeDisable,"Disabled source weapon fired");
        Wolf2009_ResetSourceWeaponForPlayer(player);state=State(player);
        Need(std::string(state.sourceWeapon)=="weapon_none"&&state.clip==0&&state.reserve==0&&!state.hasViewModel&&player->inventory.clip[2]==0&&player->inventory.ammo[1]==0,
            "Native respawn-to-none retained stale MP40/view/ammo");
        Wolf2009_DetachSourceWeapon(player);Need(Wolf2009_LiveSourceWeapons()==before-1&&!Wolf2009_IsSourceWeaponOwner(player),"Native source weapon detach leaked entities/owner refs");
        Need(Wolf2009_AttachSourceWeapon(player,set,"weapon_none",reason)&&Wolf2009_LiveSourceWeapons()==before,"Source none reattachment failed after teardown");
        gameLocal.Printf("WOLF2009_NATIVE_SOURCE_WEAPON_QA_OK checks=%u frames=%u source_clip=%d interval_ms=%d primary_weapon_attack=1 native_damage=1 miss=1 hold_release=1 reload=1 reset_none=1 actual_view=%d source_sound_audible=0 retail_clock=0 retail_spread=0 campaign=0\n",checks,frames,size,interval,int(visual!=NULL));
        return true;
    }catch(const std::exception& error){gameLocal.Warning("WOLF2009_NATIVE_SOURCE_WEAPON_QA_FAIL checks=%u frames=%u reason=%s",checks,frames,error.what());return false;}
}

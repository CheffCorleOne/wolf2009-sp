// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceWeapon.h"
#include "NativeSourcePlayer.h"
#include "SourceWeaponProfile.h"
#include "SourceDeclarationSetRuntime.h"
#include "EngineAnimationSet.h"

namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool Refuse(idStr& reason,const char* text){reason=text;return false;}
int liveWeapons=0;
struct NativeVisual {
    WolfSourceWeaponVisual hooks;
    std::string modelDefinition;
    const idDeclModelDef* model=NULL;
    int idle=0,fire=0,reload=0;
    bool retained=false;
    explicit NativeVisual(const WolfSourceWeaponVisual& request):hooks(request){
        Need(request.modelDefinition&&request.modelDefinition[0],"Source weapon visual has no prepared model definition");
        std::size_t length=0;while(length<=4096&&request.modelDefinition[length])++length;
        Need(length<=4096,"Source weapon visual definition name is excessive");modelDefinition.assign(request.modelDefinition,length);
        Need(hooks.context&&hooks.retain&&hooks.release&&hooks.preflight&&hooks.dispatch,
            "Source weapon visual requires a retained owner and complete command dispatch");
        idStr reason;Need(hooks.preflight(hooks.context,reason),reason.IsEmpty()?"Source weapon visual preflight refused":reason.c_str());
        model=Wolf2009_FindRegisteredSourceAnimationSet(modelDefinition.c_str());Need(model!=NULL,"Source weapon visual is not an unchanged registered source animation set");
        idle=model->GetSpecificAnim("idle");fire=model->GetSpecificAnim("fire");reload=model->GetSpecificAnim("reload");
        Need(idle>0&&fire>0&&reload>0,"Source weapon visual is missing idle/fire/reload");
        const auto* fireClip=model->GetAnim(fire);const auto* reloadClip=model->GetAnim(reload);const auto* idleClip=model->GetAnim(idle);
        Need(fireClip&&reloadClip&&idleClip&&!fireClip->HasFrameCommands()&&!reloadClip->HasFrameCommands()&&!idleClip->HasFrameCommands(),
            "Source weapon commands must have exactly one external owner");
        Need(fireClip->MD5Anim(0)&&reloadClip->MD5Anim(0)&&fireClip->MD5Anim(0)->IsSourceAnimationV2()&&reloadClip->MD5Anim(0)->IsSourceAnimationV2(),
            "Source weapon visual requires qualified source-v2 animations");
        Need(unsigned(fireClip->NumFrames())==hooks.fireFrames&&unsigned(reloadClip->NumFrames())==hooks.reloadFrames&&
            std::isfinite(hooks.fireDurationSeconds)&&std::isfinite(hooks.reloadDurationSeconds)&&hooks.fireDurationSeconds>0&&hooks.reloadDurationSeconds>0&&
            hooks.fireDurationSeconds<=60&&hooks.reloadDurationSeconds<=60&&
            int(std::ceil(hooks.fireDurationSeconds*1000))==fireClip->Length()&&int(std::ceil(hooks.reloadDurationSeconds*1000))==reloadClip->Length(),
            "Source weapon visual timing does not match its native source clips");
        hooks.retain(hooks.context);retained=true;
    }
    ~NativeVisual(){if(retained)hooks.release(hooks.context);}
    NativeVisual(const NativeVisual&)=delete;
    NativeVisual& operator=(const NativeVisual&)=delete;
};
std::string NativeDamage(const wolf2009::SourceWeaponProfile& profile){
    // NativeDamageScalarOnlyV1: a separately owned native definition consumes
    // the source scalar. Original wound/head/armor/impulse/effect semantics are
    // retained in Profile::Damage and are not passed as Doom dictionary keys.
    const int amount=int(profile.Configuration().damage);
    const std::string text="{\n\"damage\" \""+std::to_string(amount)+"\"\n\"knockback\" \"0\"\n\"gib\" \"0\"\n}\n";
    const std::string name="wolf2009/native-primary-damage/v1/"+std::to_string(amount);
    const idDecl* found=declManager->FindDeclWithoutParsing(DECL_ENTITYDEF,name.c_str(),false);
    if(!found){
        // Avoid occupying an unparsed foreign catalog entry with a generated
        // declaration; FindDeclWithoutParsing can return null before allocation.
        for(int i=0;i<declManager->GetNumDecls(DECL_ENTITYDEF);++i){const auto* d=declManager->DeclByIndex(DECL_ENTITYDEF,i,false);
            Need(!d||idStr::Icmp(d->GetName(),name.c_str())!=0,"Native source damage namespace is already catalogued");}
        idDecl* created=declManager->CreateNewDecl(DECL_ENTITYDEF,name.c_str(),"<wolf2009-native-primary-damage>");
        Need(created!=NULL,"Native source damage declaration allocation failed");created->SetText(text.c_str());created->Invalidate();found=created;
    }
    Need(found->GetTextLength()==int(text.size()),"Native source damage declaration was replaced");
    std::vector<char> bytes(text.size()+1,0);found->GetText(bytes.data());Need(std::string(bytes.data(),text.size())==text,"Native source damage declaration text changed");
    const auto* parsed=gameLocal.FindEntityDef(name.c_str(),false);
    Need(parsed&&parsed->GetState()!=DS_DEFAULTED&&parsed->dict.GetInt("damage")==amount,"Native source damage parse failed");return name;
}
wolf2009::SourceWeaponTiming NativeTiming(const NativeVisual* visual){
    wolf2009::SourceWeaponTiming timing;timing.policy=wolf2009::SourceWeaponClockPolicy::NativeSimulationMillisecondsV1;
    if(visual){timing.fireFrames=visual->hooks.fireFrames;timing.reloadFrames=visual->hooks.reloadFrames;
        timing.fireDurationSeconds=visual->hooks.fireDurationSeconds;timing.reloadDurationSeconds=visual->hooks.reloadDurationSeconds;
    }else{
        // NativeNoViewDiagnosticV1 is selected explicitly by a null visual.
        // These authored test timings are not inferred retail HKXA metadata.
        timing.fireFrames=2;timing.reloadFrames=64;timing.fireDurationSeconds=.1;timing.reloadDurationSeconds=2;
    }return timing;
}
}

class idWolfSourceWeapon : public idAnimatedEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceWeapon);
    idWolfSourceWeapon():reloadRequested(false),unsupported(false),lastAnimationEnd(-1),damageCalls(0),misses(0),lastHit(-1){++liveWeapons;}
    ~idWolfSourceWeapon(){StopWeaponChannel();controller.reset();visual.reset();--liveWeapons;}
    void Spawn(){GetPhysics()->SetContents(0);Hide();}
    void Bind(idPlayer* player,std::unique_ptr<wolf2009::SourceWeaponController> prepared,std::shared_ptr<const wolf2009::SourceWeaponProfile> none,
        std::shared_ptr<NativeVisual> view,const std::string& damage){
        owner=player;controller=std::move(prepared);noneProfile=std::move(none);visual=std::move(view);damageDefinition=damage;
        if(visual){
            SetModel(visual->modelDefinition.c_str());Need(animator.ModelDef()==visual->model,"Source weapon native animator bound a different model");
            animator.RemoveOriginOffset(false);renderEntity.allowSurfaceInViewID=player->entityNumber+1;
            renderEntity.noShadow=true;renderEntity.weaponDepthHack=true;
            // The same qualified actor animation-set guard admits source-time
            // cycles; the external controller is the sole frame-command owner.
            animator.CycleAnim(ANIMCHANNEL_ALL,visual->idle,gameLocal.time,0);Show();BecomeActive(TH_ANIMATE);
        }
        Need(Place(),"Prepared source weapon placement refused before publication");UpdateVisuals();SyncInventory();
    }
    idPlayer* Owner()const{return owner.GetEntity();}
    bool Broken()const{return unsupported;}
    void RequestReload(){reloadRequested=true;}
    void ResetToNone(){
        try{
            std::unique_ptr<wolf2009::SourceWeaponController> prepared(new wolf2009::SourceWeaponController(noneProfile,NativeTiming(NULL),0,0));
            StopWeaponChannel();
            Hide();animator.ClearAllAnims(gameLocal.time,0);SetModel("");visual.reset();controller=std::move(prepared);damageDefinition.clear();failure.clear();
            reloadRequested=false;unsupported=false;lastAnimationEnd=-1;damageCalls=0;misses=0;lastHit=-1;
            if(Owner()){Owner()->inventory.clip[2]=0;Owner()->inventory.ammo[1]=0;}
        }catch(const std::exception& error){Fail(error.what());}
    }
    void StopWeaponChannel(){if(GetSoundEmitter())GetSoundEmitter()->StopSound(SND_CHANNEL_WEAPON);}
    void Fail(const char* reason){StopWeaponChannel();unsupported=true;failure=reason;reloadRequested=false;Hide();}
    void SyncInventory(){
        idPlayer* player=Owner();if(!player||!controller)return;
        if(controller->Profile().Policy()==wolf2009::SourceWeaponPolicy::MP40PrimaryNativeV1){
            // Explicit NativeSourceAmmoSlotsV1. Clip2 preserves the source slot
            // table; ammo1 is this source adapter's reserve, not a retail index.
            player->inventory.clip[2]=controller->State().clip;player->inventory.ammo[1]=controller->State().reserve;
        }
    }
    bool Place(){
        if(!visual)return true;idPlayer* player=Owner();if(!player)return false;
        idVec3 origin;idMat3 axis;player->GetViewPos(origin,axis);SetOrigin(origin);SetAxis(axis);
        if(visual->hooks.place){idStr reason;if(!visual->hooks.place(visual->hooks.context,this,player,gameLocal.time,reason)){Fail(reason.c_str());return false;}}
        return true;
    }
    bool Preflight(){
        if(!visual)return true;idStr reason;
        if(!visual->hooks.preflight(visual->hooks.context,reason)||Wolf2009_FindRegisteredSourceAnimationSet(visual->modelDefinition.c_str())!=visual->model){
            Fail(reason.IsEmpty()?"Source weapon visual capability changed":reason.c_str());return false;}return true;
    }
    void Attack(){
        idPlayer* player=Owner();Need(player!=NULL,"Source weapon owner expired before attack");
        idVec3 origin;idMat3 axis;player->GetViewPos(origin,axis);const idVec3 direction=axis[0];
        for(int i=0;i<3;++i)Need(std::isfinite(origin[i])&&std::isfinite(direction[i]),"Source weapon native ray has nonfinite coordinates");
        trace_t trace;const idVec3 end=origin+direction*controller->Profile().Configuration().maxRange;
        gameLocal.clip.TracePoint(trace,origin,end,MASK_SHOT_BOUNDINGBOX,player);
        lastHit=trace.fraction<1?trace.c.entityNum:-1;
        if(trace.fraction<1&&trace.c.entityNum>=0&&trace.c.entityNum<MAX_GENTITIES){
            idEntity* target=gameLocal.entities[trace.c.entityNum];
            if(target&&target!=this&&target!=player&&target->fl.takedamage){
                target->Damage(this,player,direction,damageDefinition.c_str(),1.0f,trace.c.id);++damageCalls;return;
            }
        }++misses;
    }
    void Dispatch(const wolf2009::SourceWeaponEvent& event){
        using namespace wolf2009;
        if(event.kind==SourceWeaponEventKind::StartFire){
            if(visual){animator.PlayAnim(ANIMCHANNEL_ALL,visual->fire,event.timeMs,0);lastAnimationEnd=event.timeMs+int(std::ceil(visual->hooks.fireDurationSeconds*1000));Show();}
        }else if(event.kind==SourceWeaponEventKind::StartReload){
            if(visual){animator.PlayAnim(ANIMCHANNEL_ALL,visual->reload,event.timeMs,0);lastAnimationEnd=controller->State().reloadEndTimeMs;Show();}
        }else if(event.kind==SourceWeaponEventKind::ReloadComplete){
            if(visual){animator.CycleAnim(ANIMCHANNEL_ALL,visual->idle,event.timeMs,0);lastAnimationEnd=-1;}
        }else if(event.kind==SourceWeaponEventKind::Disabled){
            StopWeaponChannel();
            if(visual)animator.CycleAnim(ANIMCHANNEL_ALL,visual->idle,event.timeMs,0);
            Hide();lastAnimationEnd=-1;
        }else if(event.kind==SourceWeaponEventKind::FrameCommand){
            const auto& list=event.reloadCommand?controller->Profile().ReloadCommands():controller->Profile().FireCommands();
            Need(event.command<list.size(),"Source weapon event command index is invalid");const auto& source=list[event.command];
            if(source.kind==SourceWeaponCommandKind::WeaponAttack){Attack();return;}
            if(visual){
                WolfSourceWeaponFrameCommand command;command.kind=static_cast<WolfSourceWeaponCommandKind>(source.kind);command.frame=source.frame;
                command.argument=source.argument.c_str();command.valueCount=unsigned(source.values.size());
                Need(source.values.size()<=5,"Source weapon frame command exceeds native argument budget");
                for(unsigned i=0;i<5;++i)command.values[i]=i<source.values.size()?source.values[i]:0;
                idStr reason;Need(visual->hooks.dispatch(visual->hooks.context,this,command,event.timeMs,reason),
                    reason.IsEmpty()?"Source weapon visual frame command refused":reason.c_str());
            }
            // No-view diagnostics retain/count each command; they explicitly
            // do not claim execution of audiovisual/retarget behavior.
        }
    }
    void Input(bool held,bool enabled){
        if(unsupported||!controller)return;idPlayer* player=Owner();if(!player)return;
        // SourcePlayerFlagsV1: weapons disabled (player+0x958 bit 3) clears the
        // attack input (0x20300bfc). The weapon stays raised and visible.
        if(auto* flags=Wolf2009_SourcePlayerFlags(player))if(flags->weaponsDisabled&&held){held=false;++flags->attackSuppressedFrames;}
        enabled=enabled&&player->health>0&&!player->IsHidden()&&!gameLocal.inCinematic&&!gameLocal.isMultiplayer&&!gameLocal.isClient;
        try{
            if(!Preflight())return;
            if(controller->Profile().Policy()==wolf2009::SourceWeaponPolicy::MP40PrimaryNativeV1){
                Need(NativeDamage(controller->Profile())==damageDefinition,"Native source damage capability changed");
                idVec3 origin;idMat3 axis;player->GetViewPos(origin,axis);
                for(int i=0;i<3;++i)Need(std::isfinite(origin[i])&&std::isfinite(axis[0][i])&&
                    std::isfinite(origin[i]+axis[0][i]*controller->Profile().Configuration().maxRange),"Source weapon view/ray preflight is nonfinite");
                if(visual)Need(gameLocal.time<=std::numeric_limits<int>::max()-int(std::ceil(visual->hooks.fireDurationSeconds*1000)),
                    "Source weapon fire animation exceeds native clock horizon");
            }
            const bool request=reloadRequested;reloadRequested=false;
            const auto events=controller->Step(gameLocal.time,held,request,enabled);for(const auto& event:events)Dispatch(event);
            SyncInventory();
            if(enabled&&visual){
                if(lastAnimationEnd>=0&&gameLocal.time>=lastAnimationEnd&&controller->State().phase==wolf2009::SourceWeaponPhase::Ready){
                    animator.CycleAnim(ANIMCHANNEL_ALL,visual->idle,gameLocal.time,0);lastAnimationEnd=-1;}
                Show();Place();UpdateAnimation();UpdateVisuals();Present();
            }
        }catch(const std::exception& error){Fail(error.what());}
    }
    virtual void Think(){
        idPlayer* player=Owner();if(!player){PostEventMS(&EV_Remove,0);return;}
        if(player->health<=0||player->IsHidden()||gameLocal.inCinematic){Hide();return;}
        if(!unsupported&&visual){Place();UpdateAnimation();Present();}
    }
    bool Query(WolfSourceWeaponState& out,idStr& reason)const{
        if(!controller)return Refuse(reason,"Source weapon controller is unbound");
        const auto& s=controller->State();WolfSourceWeaponState value;
        value.clip=s.clip;value.reserve=s.reserve;value.phase=int(s.phase);value.lastShotTime=s.lastShotTimeMs;value.reloadEndTime=s.reloadEndTimeMs;
        value.shots=s.shots;value.emptyClicks=s.emptyClicks;value.reloads=s.reloads;value.frameCommands=s.frameCommands;
        value.damageCalls=damageCalls;value.misses=misses;value.lastHitEntity=lastHit;value.hasViewModel=bool(visual);value.unsupported=unsupported;
        value.sourceWeapon=controller->Profile().Configuration().name.c_str();out=value;reason=failure.c_str();return true;
    }
  private:
    idEntityPtr<idPlayer> owner;
    std::unique_ptr<wolf2009::SourceWeaponController> controller;
    std::shared_ptr<const wolf2009::SourceWeaponProfile> noneProfile;
    std::shared_ptr<NativeVisual> visual;
    std::string damageDefinition,failure;
    bool reloadRequested,unsupported;
    int lastAnimationEnd;
    unsigned damageCalls,misses;
    int lastHit;
};
CLASS_DECLARATION(idAnimatedEntity,idWolfSourceWeapon)
END_CLASS

namespace {
std::vector<idEntityPtr<idWolfSourceWeapon> > controllers;
void Prune(){controllers.erase(std::remove_if(controllers.begin(),controllers.end(),[](const idEntityPtr<idWolfSourceWeapon>& p){return p.GetEntity()==NULL;}),controllers.end());}
idWolfSourceWeapon* Find(const idPlayer* player){
    if(!player)return NULL;Prune();for(const auto& handle:controllers){auto* entity=handle.GetEntity();if(entity&&entity->Owner()==player)return entity;}return NULL;
}
}
bool Wolf2009_EquipSourceWeapon(idPlayer* player,const WolfSourceDeclarationSet& set,const char* sourceWeapon,
    const WolfSourceWeaponVisual* request,int clip,int reserve,idStr& reason){
    reason.Clear();try{
        Need(player&&player->entityNumber>=0&&player->entityNumber<MAX_CLIENTS&&gameLocal.entities[player->entityNumber]==player,
            "Source weapon requires a live reserved-slot native player");
        Need(!gameLocal.isMultiplayer&&!gameLocal.isClient&&gameLocal.time>=0,"Source weapon is single-player/server only");
        Need(sourceWeapon!=NULL,"Source weapon selection is absent");const std::string name(sourceWeapon);
        const auto policy=name=="weapon_none"?wolf2009::SourceWeaponPolicy::NoneV1:wolf2009::SourceWeaponPolicy::MP40PrimaryNativeV1;
        auto profile=wolf2009::PrepareSourceWeaponProfile(set.Definitions(),name,policy);
        auto none=policy==wolf2009::SourceWeaponPolicy::NoneV1?profile:wolf2009::PrepareSourceWeaponProfile(set.Definitions(),"weapon_none",wolf2009::SourceWeaponPolicy::NoneV1);
        Need(policy!=wolf2009::SourceWeaponPolicy::NoneV1||(!request&&clip==0&&reserve==0),"weapon_none cannot retain a view or ammo");
        std::shared_ptr<NativeVisual> view;if(request)view.reset(new NativeVisual(*request));
        auto timing=NativeTiming(view.get());std::unique_ptr<wolf2009::SourceWeaponController> prepared(new wolf2009::SourceWeaponController(profile,timing,clip,reserve));
        const std::string damage=policy==wolf2009::SourceWeaponPolicy::NoneV1?"":NativeDamage(*profile);
        Prune();Need(controllers.size()<MAX_CLIENTS+1,"Source weapon controller budget exceeded");controllers.reserve(MAX_CLIENTS+1);
        idWolfSourceWeapon* previous=Find(player);
        std::unique_ptr<idWolfSourceWeapon> entity(static_cast<idWolfSourceWeapon*>(gameLocal.SpawnEntityType(idWolfSourceWeapon::Type,NULL)));
        Need(bool(entity),"Source weapon native entity allocation failed");entity->Bind(player,std::move(prepared),std::move(none),std::move(view),damage);
        idEntityPtr<idWolfSourceWeapon> handle;handle=entity.get();controllers.push_back(handle);entity.release();
        delete previous;Prune();return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool Wolf2009_AttachSourceWeapon(idPlayer* player,const WolfSourceDeclarationSet& set,const char* weapon,idStr& reason){
    if(!weapon||std::string(weapon)!="weapon_none")return Refuse(reason,"Initial source weapon attachment requires the explicit weapon_none map profile");
    if(Find(player))return Refuse(reason,"Source player already owns a weapon controller");return Wolf2009_EquipSourceWeapon(player,set,weapon,NULL,0,0,reason);
}
void Wolf2009_DetachSourceWeapon(idPlayer* player){delete Find(player);Prune();}
void Wolf2009_ResetSourceWeaponForPlayer(idPlayer* player){auto* weapon=Find(player);if(weapon)weapon->ResetToNone();}
bool Wolf2009_UpdateSourceWeapon(idPlayer* player,bool held,bool enabled){auto* weapon=Find(player);if(!weapon)return false;weapon->Input(held,enabled);return true;}
bool Wolf2009_ReloadSourceWeapon(idPlayer* player){auto* weapon=Find(player);if(!weapon)return false;weapon->RequestReload();return true;}
bool Wolf2009_QuerySourceWeapon(const idPlayer* player,WolfSourceWeaponState& out,idStr& reason){
    auto* weapon=Find(player);if(!weapon)return Refuse(reason,"Player has no source weapon controller");return weapon->Query(out,reason);
}
bool Wolf2009_IsSourceWeaponOwner(const idPlayer* player){return Find(player)!=NULL;}
int Wolf2009_LiveSourceWeapons(){return liveWeapons;}
#ifdef WOLF2009_ANIMATION_V2_QA
idAnimatedEntity* Wolf2009_SourceWeaponEntityForQA(const idPlayer* player){return Find(player);}
#endif

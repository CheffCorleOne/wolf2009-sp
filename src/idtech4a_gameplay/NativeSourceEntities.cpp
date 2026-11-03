// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "game/Item.h"
#include "SourceEntityRuntime.h"
#include "EngineSourceAF.h"
#include "SourceDeclarationSetRuntime.h"
#include "EngineAnimationSet.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "SourceActorBinding.h"

namespace {
const idEventDef EV_SourceContainerDone("<wolfSourceContainerDone>","d");
const idEventDef EV_SourceContainerGive("<wolfSourceContainerGive>","d");
const idEventDef EV_SourceContainerReopen("<wolfSourceContainerReopen>",NULL);
int liveEntities=0;
const int MaximumEntities=128,MaximumGiveJobs=64;
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool Refuse(idStr& reason,const char* text){reason=text;return false;}
void Close(idFile* file){if(file)fileSystem->CloseFile(file);}
typedef std::unique_ptr<idFile,void(*)(idFile*)> SourceFile;
bool Active(){return gameLocal.GameState()==GAMESTATE_ACTIVE&&!gameLocal.isMultiplayer&&!gameLocal.isClient&&gameLocal.time>=0;}
bool SafeName(const std::string& name){
    if(name.empty()||name.size()>127||name=="NULL"||name=="null_entity")return false;
    for(std::size_t i=0;i<name.size();++i){const auto c=name[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i&&c>='0'&&c<='9')))return false;}return true;
}
std::string Words(const float* words,int count){std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(std::numeric_limits<float>::max_digits10);
    for(int i=0;i<count;++i){if(i)text<<' ';text<<words[i];}return text.str();}
std::string SessionKey(const std::string& entityName){
    std::string session=gameLocal.GetMapName();const auto slash=session.find_last_of("/\\");if(slash!=std::string::npos)session.erase(0,slash+1);
    const auto dot=session.find_last_of('.');if(dot!=std::string::npos)session.resize(dot);Need(!session.empty()&&session.size()<=4096,"Source container persistence session name is unavailable");
    // NativeSessionDictionaryV1 uses the measured basename_entity source key
    // shape in an explicitly owned native namespace. Retail save is unqualified.
    return "wolf2009.source-container:"+session+"_"+entityName;
}
bool SameOrigin(const wolf2009::SourceDefinitionProvenance& a,const wolf2009::SourceDefinitionProvenance& b){
    const auto& x=a.member;const auto& y=b.member;
    if(a.declarationKind!=b.declarationKind||a.declarationName!=b.declarationName||a.rawDeclarationKind!=b.rawDeclarationKind||a.rawDeclarationName!=b.rawDeclarationName||
       a.declarationBegin!=b.declarationBegin||a.declarationEnd!=b.declarationEnd||x.archive.name!=y.archive.name||x.archive.bytes!=y.archive.bytes||x.archive.fingerprint!=y.archive.fingerprint||
       x.name!=y.name||x.bytes!=y.bytes||x.fingerprint!=y.fingerprint||!x.compiledRecord||!y.compiledRecord)return false;
    const auto& left=*x.compiledRecord;const auto& right=*y.compiledRecord;
    return left.IsUnmodified()&&right.IsUnmodified()&&left.section==right.section&&left.group==right.group&&left.record==right.record&&
        left.source->path==right.source->path&&left.source->loadIdentity==right.source->loadIdentity&&left.source->role==right.source->role&&left.source->epoch==right.source->epoch;
}
bool SameClosure(const wolf2009::ResolvedSourceDefinition& a,const wolf2009::ResolvedSourceDefinition& b){
    if(!SameOrigin(a.selected,b.selected)||a.declarations.size()!=b.declarations.size())return false;
    for(std::size_t i=0;i<a.declarations.size();++i)if(!SameOrigin(a.declarations[i],b.declarations[i]))return false;return true;
}
std::int32_t Integer(const wolf2009::SourceEntityPlan& plan,const char* key,std::int32_t fallback){
    const auto value=plan.Find(key);if(!value.instance&&!value.declaration)return fallback;
    std::int32_t result=0;Need(value.Integer(result),"Source container integer field is unqualified");return result;
}
bool Boolean(const wolf2009::SourceEntityPlan& plan,const char* key,bool fallback){const auto value=Integer(plan,key,fallback?1:0);Need(value==0||value==1,"Source container flag is not explicitly zero/one");return value!=0;}
std::string Text(const wolf2009::SourceEntityPlan& plan,const char* key,const char* fallback=""){
    const auto value=plan.Find(key);if(!value.instance&&!value.declaration)return fallback;std::string result;
    Need(value.Text(result)&&result.size()<=4096,"Source container text field is unqualified/excessive");return result;
}
struct Configuration {
    idVec3 origin;idMat3 axis;bool placementApproximated=false;
    double placementScaleError=0,placementMatrixErrorNorm=0;
    std::vector<std::string> sequence;std::string initial,ragdoll,afcore;
    int health=0,giveDelay=-1,blendMs=0;
    bool usable=false,persist=false,hideFinish=false,highlight=false;
};
Configuration Configure(const wolf2009::SourceEntityPlan& plan,const WolfSourceActorBinding& binding){
    Configuration c;c.origin.Zero();c.axis.Identity();
    const std::set<std::string> fields={"classname","spawnclass","name","model","origin","rotation","angle","anim","start_anim","num_anims","health","noclipmodel",
        "usable","hide_finish","persist","givedelay","ragdoll","def_afcore","applyhilighttemplate","snd_open","snd_close","blend_in","cycle","loop_last_anim","afphysicsstartactive"};
    const int count=Integer(plan,"num_anims",0);Need(count>=1&&count<=64,"Source container requires a bounded numeric animation sequence");
    std::set<std::string> admitted=fields;for(int i=1;i<=count;++i)admitted.insert("anim"+std::to_string(i));
    for(const auto& key:plan.Keys())Need(admitted.count(key)!=0,"Source container has an unimplemented effective field");
    Need(Boolean(plan,"noclipmodel",false),"Source container native clipmodel binding is unimplemented");
    Need(Integer(plan,"cycle",1)==1&&!Boolean(plan,"loop_last_anim",false),"Source container animation loops are unimplemented");
    Need(!Boolean(plan,"afPhysicsStartActive",false),"Source container dynamic AF at Spawn is unimplemented");
    c.health=Integer(plan,"health",0);Need(c.health>=0,"Negative source container health is unimplemented");
    c.giveDelay=Integer(plan,"giveDelay",-1);Need(c.giveDelay>=-1,"Invalid source container giveDelay");
    const int blend=Integer(plan,"blend_in",0);Need(blend>=0&&blend<=240,"Source container blend-frame budget exceeded");
    c.blendMs=blend*1000/24; // Observed declared blend-frame conversion, not clip FPS.
    c.usable=Boolean(plan,"usable",false);c.persist=Boolean(plan,"persist",false);c.hideFinish=Boolean(plan,"hide_finish",true);c.highlight=Boolean(plan,"applyHilightTemplate",false);
    c.ragdoll=Text(plan,"ragdoll");c.afcore=Text(plan,"def_afcore");c.initial=Text(plan,"start_anim",Text(plan,"anim").c_str());
    Need(!c.initial.empty()&&binding.FindClip(c.initial.c_str()),"Source container initial animation is absent from its exact actor binding");
    Need(binding.FindClip(c.initial.c_str())->frameSounds.empty(),"Source container initial cycle cannot carry authored frame commands");
    for(int i=1;i<=count;++i){const auto key="anim"+std::to_string(i),name=Text(plan,key.c_str());const auto* clip=binding.FindClip(name.c_str());
        Need(clip&&clip->lengthMs>0&&c.blendMs<=clip->lengthMs,"Source container sequence clip is absent or blend exceeds its length");c.sequence.push_back(name);}
    const auto placement=wolf2009::PrepareSourceEntityPlacement(plan,wolf2009::SourceEntityPlacementPolicy::NativeNearUnitPlacementV1);
    for(int i=0;i<3;++i)c.origin[i]=placement.origin[std::size_t(i)];
    for(int i=0;i<9;++i)c.axis[i/3][i%3]=placement.physicsAxis[std::size_t(i)];
    c.placementApproximated=placement.placementApproximated;c.placementScaleError=placement.maximumScaleError;c.placementMatrixErrorNorm=placement.matrixDifferenceNorm;return c;
}
struct Busy {bool& value;explicit Busy(bool& v):value(v){value=true;}~Busy(){value=false;}};
}

class idWolfSourceAnimatedContainer : public idAnimatedEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceAnimatedContainer);
    idWolfSourceAnimatedContainer():source(NULL,Close),assigned(false),bound(false),busy(false){++liveEntities;animator.RemoveOriginOffset(false);}
    ~idWolfSourceAnimatedContainer(){
        CancelEvents(&EV_SourceContainerDone);CancelEvents(&EV_SourceContainerGive);CancelEvents(&EV_SourceContainerReopen);
        af.reset(); // Restores/detaches physics while owner and animator live.
        for(const auto* animation:refs)animation->DecreaseRefs();binding.reset();plan.reset();source.reset();--liveEntities;
    }
    void SetModel(const char* name) override {
        if(assigned||bound||!Wolf2009_FindRegisteredSourceAnimationSet(name)){gameLocal.Warning("Source container model replacement refused");return;}
        idAnimatedEntity::SetModel(name);assigned=true;
    }
    bool Bind(SourceFile& file,std::shared_ptr<const wolf2009::SourceEntityPlan> facts,std::shared_ptr<const WolfSourceActorBinding> model,const Configuration& config,const WolfSourceAFPrepared* prepared,idStr& reason){
        if(!assigned||bound||animator.ModelDef()!=Wolf2009_FindRegisteredSourceAnimationSet(model->NativeModelDefinition())||animator.RemoveOrigin())return Refuse(reason,"Source container native model differs after Spawn");
        plan=std::move(facts);binding=std::move(model);configuration=config;source=std::move(file);
        for(std::size_t i=0;i<binding->ClipCount();++i){const auto& clip=binding->Clip(i);const int number=animator.ModelDef()->GetSpecificAnim(clip.logicalName.c_str());const idAnim* anim=number>0?animator.GetAnim(number):NULL;
            if(!anim||anim->NumAnims()!=1||!anim->MD5Anim(0)||!anim->MD5Anim(0)->IsSourceAnimationV2())return Refuse(reason,"Source container native clip identity differs");refs.push_back(anim->MD5Anim(0));refs.back()->IncreaseRefs();}
        bound=true;state.usable=config.usable;state.highlightUnimplemented=config.highlight;
        state.placementApproximated=config.placementApproximated;state.placementScaleError=config.placementScaleError;state.placementMatrixErrorNorm=config.placementMatrixErrorNorm;
        state.persistenceKey=SessionKey(plan->Name());
        state.persisted=config.persist&&gameLocal.persistentLevelInfo.GetBool(state.persistenceKey.c_str(),"0");
        Start(config.initial,0,false);
        if(prepared){af.reset(WolfSourceAF::Bind(*prepared,*this,reason));if(!af)return false;}
        if(state.persisted)PostEventMS(&EV_SourceContainerReopen,0);
        BecomeActive(TH_ANIMATE);UpdateVisuals();return true;
    }
    bool Query(WolfSourceEntityState& out)const{if(!bound)return false;out=state;return true;}
    bool QueryAF(WolfSourceAFState& out,idStr& reason)const{if(!af)return Refuse(reason,"Source entity has no prepared AF binding");return af->Query(out);}
    bool CanUse()const{return bound&&state.usable;}
    bool DeliverActivate(idEntity* actor,idStr& reason){eventAccepted=false;eventReason.Clear();ProcessEvent(&EV_Activate,actor);reason=eventReason;return eventAccepted;}
    bool Activate(idEntity* actor,idStr& reason){
        if(!bound||busy)return Refuse(reason,"Source container is unbound or reentered");Busy guard(busy);
        if(af&&af->Active())return Refuse(reason,"Reverse source AF-to-animation handoff is unimplemented");
        if(!Active()||!binding->MatchesNative(reason))return false;
        Need(gameLocal.time<=std::numeric_limits<int>::max()-std::max(configuration.giveDelay,0),"Source container give timer overflow");
        if(actor!=this&&configuration.giveDelay>=0)Need(jobs.size()<MaximumGiveJobs,"Source container give timer budget exceeded");
        Need(state.activations!=(std::numeric_limits<unsigned>::max)(),"Source container activation count overflow");
        bool play=true;if(state.sequence>=int(configuration.sequence.size())){if(configuration.hideFinish){Hide();play=false;}else state.sequence=0;}
        if(play){Show();Start(configuration.sequence[std::size_t(state.sequence)],configuration.blendMs,true);++state.sequence;}
        ++state.activations;state.usable=false;
        if(configuration.persist){gameLocal.persistentLevelInfo.SetBool(state.persistenceKey.c_str(),true);state.persisted=true;}
        if(actor!=this&&configuration.giveDelay>=0){GiveJob job;job.opener=actor;job.literalNull=actor==NULL;jobs.push_back(job);++state.pendingGiveEvents;PostEventMS(&EV_SourceContainerGive,configuration.giveDelay,int(jobs.size()-1));}
        return true;
    }
    void Think() override {try{
        if(af&&af->Active()){idStr reason;Need(af->Step(gameLocal.msec,reason),reason.IsEmpty()?"Source AF Step failed":reason.c_str());
            BecomeActive(TH_ANIMATE);UpdateAnimation();Present();UpdateDamageEffects();}
        else idAnimatedEntity::Think();UpdatePlacementBound();
    }catch(const std::exception& error){gameLocal.Error("Source container Think: %s",error.what());}}
    bool GetPhysicsToVisualTransform(idVec3& origin,idMat3& axis) override {
        if(af&&af->Active())return af->GetPhysicsToVisualTransform(origin,axis);
        return idAnimatedEntity::GetPhysicsToVisualTransform(origin,axis);
    }
    void MarkUseNotification(idEntity* actor){if(actor){state.useActorNotificationUnsupported=true;gameLocal.Printf("WOLF2009_SOURCE_ENTITY_UNSUPPORTED entity=%s phase=UseActorNotification\n",GetName());}}
    void Damage(idEntity*,idEntity*,const idVec3&,const char*,const float,const int) override {gameLocal.Error("Source container damage/AF body routing is unimplemented");}
    void Save(idSaveGame*)const{gameLocal.Error("Source container save-state serialization is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Source container restore is unimplemented");}
  private:
    struct GiveJob {idEntityPtr<idEntity> opener;bool done=false,literalNull=false;};
    SourceFile source;
    std::shared_ptr<const wolf2009::SourceEntityPlan> plan;
    std::shared_ptr<const WolfSourceActorBinding> binding;
    std::unique_ptr<WolfSourceAF> af;
    Configuration configuration;WolfSourceEntityState state;
    std::vector<const idMD5Anim*> refs;std::vector<GiveJob> jobs;
    bool assigned,bound,busy;
    bool eventAccepted=false;idStr eventReason;
    void UpdatePlacementBound(){
        if(!bound||renderEntity.bounds.IsCleared())return;double radius=renderEntity.bounds.GetRadius();
        int count=0;idJointMat* joints=NULL;animator.GetJoints(&count,&joints);if(joints)for(int i=0;i<count;++i)radius=std::max(radius,double(joints[i].ToVec3().Length()));
        Need(std::isfinite(radius)&&radius>=0,"Source model placement radius is invalid");state.placementModelRadius=radius;
        state.placementDisplacementBound=radius*state.placementMatrixErrorNorm;state.placementBoundKnown=true;
    }
    void Start(const std::string& name,int blend,bool completion){
        const auto* clip=binding->FindClip(name.c_str());Need(clip&&gameLocal.time<=std::numeric_limits<int>::max()-clip->lengthMs,"Source container animation time overflow");
        const int number=animator.ModelDef()->GetSpecificAnim(name.c_str());Need(number>0,"Source container logical clip disappeared");
        if(completion)animator.PlayAnim(ANIMCHANNEL_ALL,number,gameLocal.time,blend);
        else{animator.CycleAnim(ANIMCHANNEL_ALL,number,gameLocal.time,blend);animator.CurrentAnim(ANIMCHANNEL_ALL)->SetCycleCount(state.initialCycleCount);}
        Need(animator.CurrentAnim(ANIMCHANNEL_ALL)->AnimNum()==number&&animator.CurrentAnim(ANIMCHANNEL_ALL)->GetCycleCount()==(completion?1:state.initialCycleCount),
            "Source container native animation mode/count was refused");
        animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowMovement(false);
        animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowFrameCommands(true);animator.ForceUpdate();
        state.animation=name;state.animationEndMs=completion?gameLocal.time+clip->lengthMs:-1;state.phase=completion?WolfSourceEntity_Playing:WolfSourceEntity_Idle;
        if(completion)PostEventMS(&EV_SourceContainerDone,clip->lengthMs,state.sequence+1);
        BecomeActive(TH_ANIMATE);UpdateVisuals();
    }
    void Event_Activate(idEntity* actor){idStr reason;try{eventAccepted=Activate(actor,reason);eventReason=reason;if(!eventAccepted)gameLocal.Warning("Source container Activate refused: %s",reason.c_str());}
        catch(const std::exception& error){gameLocal.Error("Source container Activate: %s",error.what());}}
    void Event_Reopen(){ProcessEvent(&EV_Activate,this);}
    void Event_AnimDone(int sequence){
        (void)sequence; // The measured source handler does not compare this argument.
        if(!bound)return;
        try{
        ++state.animationCompletions;
        if(af&&af->Active()){state.phase=WolfSourceEntity_Completed;return;}
        // Native events can run before the stopped-animation Think. Keep the
        // actual current clip's endpoint explicit before handing its pose to AF.
        // An old event during a newer clip still snapshots the CURRENT pose.
        if(state.animationEndMs>=0&&gameLocal.time>=state.animationEndMs){
            const auto* clip=binding->FindClip(state.animation.c_str());Need(clip!=NULL,"Source completion clip disappeared");
            const int number=animator.ModelDef()->GetSpecificAnim(state.animation.c_str());Need(number>0,"Source completion logical name disappeared");
            animator.SetFrame(ANIMCHANNEL_ALL,number,clip->frames,gameLocal.time,0);
            Need(animator.CurrentAnim(ANIMCHANNEL_ALL)->GetFrameNumber(gameLocal.time)==clip->frames,"Source completion final-frame hold was refused");
            animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowMovement(false);animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowFrameCommands(false);animator.ForceUpdate();
            BecomeActive(TH_ANIMATE);UpdateVisuals();
        }
        animator.CreateFrame(gameLocal.time,true);
        if(af){idStr reason;if(af->Activate(reason)){state.afHandoffUnsupported=false;state.reason.clear();state.phase=WolfSourceEntity_Completed;BecomeActive(TH_PHYSICS|TH_ANIMATE);return;}
            state.afHandoffUnsupported=true;state.phase=WolfSourceEntity_UnsupportedAFHandoff;state.reason=reason.c_str();
            gameLocal.Printf("WOLF2009_SOURCE_ENTITY_UNSUPPORTED entity=%s phase=AFActivate reason=%s\n",GetName(),state.reason.c_str());return;}
        if(!configuration.ragdoll.empty()||!configuration.afcore.empty()){
            state.afHandoffUnsupported=true;state.phase=WolfSourceEntity_UnsupportedAFHandoff;state.reason="Source AFPhysicsSetActive handoff is unimplemented";
            gameLocal.Printf("WOLF2009_SOURCE_ENTITY_UNSUPPORTED entity=%s phase=AnimDone reason=%s\n",GetName(),state.reason.c_str());return;
        }
        state.phase=WolfSourceEntity_Completed;
        }catch(const std::exception& error){gameLocal.Error("Source container animation completion: %s",error.what());}
    }
    void Event_Give(int index){
        try{
            Need(bound&&index>=0&&std::size_t(index)<jobs.size()&&!jobs[std::size_t(index)].done,"Source container give job is invalid");
            GiveJob& job=jobs[std::size_t(index)];job.done=true;--state.pendingGiveEvents;++state.giveEvents;
            idEntityPtr<idEntity> opener=job.opener,self;self=this;const bool literalNull=job.literalNull;
            idEntity* actor=opener.GetEntity();if(!actor&&!literalNull){++state.expiredOpeners;return;}
            idBounds local;Need(animator.GetBounds(gameLocal.time,local),"Source container has no current animated bounds");idBounds world;
            UpdateModelTransform();world.FromTransformedBounds(local,renderEntity.origin,renderEntity.axis);
            // NativeIdItemBoundsQueryV1 is explicit: native idItem, transformed
            // animated bounds, native entity order, at most50. Source mask0x9000
            // is retained metadata, never reinterpreted as Doom3 content bits.
            idList< idEntityPtr<idEntity> > selected;
            for(int i=0;i<ENTITYNUM_MAX_NORMAL&&selected.Num()<50;++i){idEntity* item=gameLocal.entities[i];
                if(item&&item!=this&&item->IsType(idItem::Type)&&item->GetPhysics()&&(item->GetPhysics()->GetContents()&CONTENTS_TRIGGER)&&world.IntersectsBounds(item->GetPhysics()->GetAbsBounds())){idEntityPtr<idEntity> ref;ref=item;selected.Append(ref);}}
            for(int i=0;i<selected.Num();++i){actor=opener.GetEntity();idEntity* item=selected[i].GetEntity();if(!actor&&!literalNull)break;if(!item||!item->IsType(idItem::Type))continue;
                ++state.activatedItems;item->ProcessEvent(&EV_Activate,actor);if(!self.GetEntity())return;}
        }catch(const std::exception& error){gameLocal.Error("Source container giveContents: %s",error.what());}
    }
};
CLASS_DECLARATION(idAnimatedEntity,idWolfSourceAnimatedContainer)
    EVENT(EV_Activate,idWolfSourceAnimatedContainer::Event_Activate)
    EVENT(EV_SourceContainerDone,idWolfSourceAnimatedContainer::Event_AnimDone)
    EVENT(EV_SourceContainerGive,idWolfSourceAnimatedContainer::Event_Give)
    EVENT(EV_SourceContainerReopen,idWolfSourceAnimatedContainer::Event_Reopen)
END_CLASS

bool Wolf2009_IsSourceEntity(const idEntity* entity){return entity&&entity->GetType()==&idWolfSourceAnimatedContainer::Type;}
int Wolf2009_LiveSourceEntities(){return liveEntities;}
bool Wolf2009_QuerySourceEntity(const idEntity* entity,WolfSourceEntityState& out,idStr& reason){reason.Clear();if(!Wolf2009_IsSourceEntity(entity))return Refuse(reason,"Entity is not a registered source class");return static_cast<const idWolfSourceAnimatedContainer*>(entity)->Query(out);}
bool Wolf2009_QuerySourceEntityAF(const idEntity* entity,WolfSourceAFState& out,idStr& reason){reason.Clear();if(!Wolf2009_IsSourceEntity(entity))return Refuse(reason,"Entity is not a registered source class");return static_cast<const idWolfSourceAnimatedContainer*>(entity)->QueryAF(out,reason);}
bool Wolf2009_CanUseSourceEntity(const idEntity* entity){return Wolf2009_IsSourceEntity(entity)&&static_cast<const idWolfSourceAnimatedContainer*>(entity)->CanUse();}
bool Wolf2009_ActivateSourceEntity(idEntity* entity,idEntity* actor,idStr& reason){reason.Clear();try{if(!Wolf2009_IsSourceEntity(entity))return Refuse(reason,"Entity is not a registered source class");return static_cast<idWolfSourceAnimatedContainer*>(entity)->DeliverActivate(actor,reason);}catch(const std::exception& error){reason=error.what();return false;}}
bool Wolf2009_UseSourceEntity(idEntity* entity,idEntity* actor,const char* key,idStr& reason){(void)key;if(!Wolf2009_ActivateSourceEntity(entity,actor,reason))return false;static_cast<idWolfSourceAnimatedContainer*>(entity)->MarkUseNotification(actor);return true;}
static bool SpawnSourceEntity(const char* path,std::size_t index,const WolfSourceDeclarationSet& declarations,std::shared_ptr<const WolfSourceActorBinding> binding,const WolfSourceAFRequest* afRequest,idEntity*& out,idStr& reason){
    out=NULL;reason.Clear();try{
        Need(Active()&&liveEntities<MaximumEntities,"Source container requires active local single-player game and a free entity budget");
        Need(path&&binding&&binding->MatchesNative(reason),"Source container actor binding is unavailable/changed");
        SourceFile source(fileSystem->OpenFileRead(path,false),Close);Need(bool(source),"Selected source ENTS file is unavailable");
        const int length=source->Length();Need(length>0&&length<=64*1024*1024,"Selected source ENTS byte budget exceeded");std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
        Need(source->Read(bytes.data(),length)==length,"Short source ENTS read");char extra=0;Need(source->Read(&extra,1)==0,"Source ENTS grew during the read");
        auto plan=wolf2009::PrepareSourceEntity(bytes,path,index,declarations.Definitions(),wolf2009::SourceEntityPolicy::AnimatedContainerV1,binding->SourcePlan().Context());
        Need(binding->MatchesEntityModel(plan->Classname(),plan->ModelDefinition(),binding->SourcePlan().Context()),"Source entity/defaults/model identity differs from bound actor");
        const auto currentModel=declarations.Definitions().ResolveModelAnimations(plan->ModelDefinition(),binding->SourcePlan().Context());
        Need(SameClosure(plan->Defaults(),binding->SourcePlan().Entity())&&SameClosure(currentModel.model,binding->SourcePlan().Model().model),"Source entity/model declaration closure differs from bound source bytes");
        Need(SafeName(plan->Name())&&!gameLocal.FindEntity(plan->Name().c_str()),"Source entity name is invalid or occupied");
        bool free=false;for(int i=MAX_CLIENTS;i<ENTITYNUM_MAX_NORMAL;++i)if(!gameLocal.entities[i]){free=true;break;}Need(free,"Native entity slots exhausted");
        const auto config=Configure(*plan,*binding);std::unique_ptr<WolfSourceAFPrepared> prepared;
        if(afRequest){Need(config.placementScaleError<=2e-5&&config.placementMatrixErrorNorm<=2e-5,"Source AF V1 requires a rigid placement; scaled approximation is unqualified");
            prepared.reset(Wolf2009_PrepareSourceAF(declarations,*plan,*binding,*afRequest,reason));Need(bool(prepared),reason.IsEmpty()?"Source AF preparation failed":reason.c_str());}
        idDict args;args.Set("classname",plan->Classname().c_str());args.Set("name",plan->Name().c_str());args.Set("model",binding->NativeModelDefinition());
        for(const auto& sound:binding->Sounds()){Need(Text(*plan,sound.sourceKey.c_str())==sound.sourceSound,"Source container sound field differs from its actor binding");
            Need(!sound.sourceKey.empty()&&!sound.nativeSoundAlias.empty(),"Source container native sound mapping is incomplete");args.Set(sound.sourceKey.c_str(),sound.nativeSoundAlias.c_str());}
        args.Set("origin",Words(config.origin.ToFloatPtr(),3).c_str());args.Set("rotation",Words(config.axis.ToFloatPtr(),9).c_str());args.SetInt("health",config.health);
        args.SetBool("noclipmodel",true);args.SetBool("networkSync",false);args.SetBool("neverDormant",true);args.SetBool("cinematic",true);
        std::unique_ptr<idWolfSourceAnimatedContainer> entity(static_cast<idWolfSourceAnimatedContainer*>(gameLocal.SpawnEntityType(idWolfSourceAnimatedContainer::Type,&args)));
        Need(bool(entity),"Native source container allocation failed");if(!entity->Bind(source,std::move(plan),std::move(binding),config,prepared.get(),reason))return false;
        out=entity.release();return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool Wolf2009_SpawnSourceEntity(const char* path,std::size_t index,const WolfSourceDeclarationSet& declarations,std::shared_ptr<const WolfSourceActorBinding> binding,idEntity*& out,idStr& reason){return SpawnSourceEntity(path,index,declarations,std::move(binding),NULL,out,reason);}
bool Wolf2009_SpawnSourceEntityWithAF(const char* path,std::size_t index,const WolfSourceDeclarationSet& declarations,std::shared_ptr<const WolfSourceActorBinding> binding,const WolfSourceAFRequest& request,idEntity*& out,idStr& reason){return SpawnSourceEntity(path,index,declarations,std::move(binding),&request,out,reason);}

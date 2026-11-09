// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
// Standard headers used by the source owners must precede idlib's printf guards.
#include <array>
#include <functional>
#include <map>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "NativeSourceTrainyardExit.h"
#include "NativeSourceDirector.h"
#include "NativeSourceGreeterRoute.h"
#include "NativeSourceMover.h"
#include "NativeSourceRegistry.h"
#include "NativeSourceScriptedActor.h"
#include "SourceDeclarationSetRuntime.h"
#include "SourceDefinitions.h"
#include "SourceMapBodies.h"
#include "TrainyardExit.h"

namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){if(!ok)throw std::runtime_error(reason.IsEmpty()?fallback:reason.c_str());}
const char* const Profile="MeasuredTrainyardExitV1";
// Measured ENTS identities of this slice (rechecked against names/classes).
const std::size_t ExitIndex=2094,ArmedIndex=2033,DormantIndex=2209,RelayIndex=2032,BlockerIndex=2194,ShakerIndex=2132;
const char* const GreeterName="train_greeter";
const char* const WagonName="starting_boxcar_model";
std::string Fold(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
std::string Text(const wolf2009::EntityRecord& e,const char* key){const auto* p=e.Find(key);
    Need(p&&p->kind==wolf2009::EntityProperty::Text&&!p->text.empty()&&p->text.size()<=256,"Exit source ENTS text field is absent/unqualified");return p->text;}
int Integer(const wolf2009::EntityRecord& e,const char* key){const auto* p=e.Find(key);Need(p&&p->kind==wolf2009::EntityProperty::Integer,"Exit source ENTS integer field is absent/unqualified");return p->integer;}
void Keys(const wolf2009::EntityRecord& e,const std::set<std::string>& admitted){std::set<std::string> seen;
    for(const auto& p:e.properties){const auto key=Fold(p.key);Need(admitted.count(key)&&seen.insert(key).second,"Exit source ENTS entity has an unadmitted or duplicate field");}
    Need(seen.size()==admitted.size(),"Exit source ENTS entity lacks a measured field");}
std::size_t Unique(const wolf2009::EntityDocument& d,const std::string& name){std::size_t found=0,count=0;
    for(std::size_t i=0;i<d.entities.size();++i){const auto* p=d.entities[i].Find("name");if(p&&p->kind==wolf2009::EntityProperty::Text&&Fold(p->text)==Fold(name)){found=i;++count;}}
    Need(count==1,"Exit source name is absent or ambiguous");return found;}
std::string LitText(const wolf2009::ResolvedSourceDefinition& d,const char* key){const auto* f=d.Find(key);
    Need(f&&(f->encoding==wolf2009::SourceLiteralEncoding::CompiledString||f->encoding==wolf2009::SourceLiteralEncoding::PlainText)&&f->value.size()==1,"Exit class text field differs");return f->value[0].text;}
int LitInt(const wolf2009::ResolvedSourceDefinition& d,const char* key){const auto* f=d.Find(key);
    Need(f&&f->encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&f->numericWords.size()==1,"Exit class integer field differs");return int(std::int32_t(f->numericWords[0]));}
std::string Words(const double* v,unsigned n){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);for(unsigned i=0;i<n;++i){if(i)out<<' ';out<<float(v[i]);}return out.str();}
int liveExits=0,liveTriggers=0;
// Clip model bounds at the origin (identity axis), without the clip-link epsilon of absBounds.
idBounds SourceBox(idEntity* e){const auto* physics=e->GetPhysics();return physics->GetBounds().Translate(physics->GetOrigin());}
class ExitHost {
  public:
    virtual ~ExitHost(){}
    virtual void Tick()=0;
    virtual bool Active()const=0;
    virtual void Touched(idEntity* self,idEntity* other)=0;
    virtual void Activated(idEntity* self,idEntity* activator)=0;
};
const std::shared_ptr<ExitHost>* pendingHost=NULL;
struct HostContext {
    explicit HostContext(const std::shared_ptr<ExitHost>& value){Need(!pendingHost,"Recursive trainyard exit spawn");pendingHost=&value;}
    ~HostContext(){pendingHost=NULL;}
};
}

// Native source trigger volume (Wolf idTrigger_Multi subset, static Spawn
// ~0x20119300 and activate 0x20114560): CONTENTS_TRIGGER box, default touch
// filter = players only, triggerFirst armed by activate without firing,
// wait -1 fires once and then removes itself. Behaviour lives in the owner.
class idWolfSourceTrigger : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceTrigger);
    idWolfSourceTrigger(){Need(pendingHost&&bool(*pendingHost),"Source trigger requires its map owner");host=*pendingHost;++liveTriggers;}
    ~idWolfSourceTrigger(){host.reset();--liveTriggers;}
    void Spawn(){fl.networkSync=false;GetPhysics()->SetContents(CONTENTS_TRIGGER);}
    void Save(idSaveGame*)const{gameLocal.Error("Source trigger save state is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Source trigger restore is unimplemented");}
  private:
    void Event_Touch(idEntity* other,trace_t*){if(const auto owner=host.lock())owner->Touched(this,other);}
    void Event_Activate(idEntity* activator){if(const auto owner=host.lock())owner->Activated(this,activator);}
    std::weak_ptr<ExitHost> host;
};
CLASS_DECLARATION(idEntity,idWolfSourceTrigger)
    EVENT(EV_Touch,idWolfSourceTrigger::Event_Touch)
    EVENT(EV_Activate,idWolfSourceTrigger::Event_Activate)
END_CLASS

class idWolfSourceExitThink : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceExitThink);
    idWolfSourceExitThink(){Need(pendingHost&&bool(*pendingHost),"Exit Think requires its map owner");host=*pendingHost;}
    ~idWolfSourceExitThink(){host.reset();}
    void Spawn(){fl.networkSync=false;fl.neverDormant=true;cinematic=true;BecomeActive(TH_THINK);}
    void Think()override{const auto owner=host.lock();if(!owner){BecomeInactive(TH_THINK);return;}owner->Tick();if(!owner->Active())BecomeInactive(TH_THINK);}
    void Save(idSaveGame*)const{gameLocal.Error("Trainyard exit save state is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Trainyard exit restore is unimplemented");}
  private:std::weak_ptr<ExitHost> host;
};
CLASS_DECLARATION(idEntity,idWolfSourceExitThink)
END_CLASS

const char* Wolf2009_SourceTriggerPhaseText(WolfSourceTriggerPhase v){switch(v){case WolfSourceTriggerPhase::Dormant:return "dormant";case WolfSourceTriggerPhase::Live:return "live";case WolfSourceTriggerPhase::Fired:return "fired";}return "unknown";}
const char* Wolf2009_SourceExitStageText(WolfSourceExitStage v){switch(v){case WolfSourceExitStage::Prepared:return "prepared";case WolfSourceExitStage::Waiting:return "waiting";
    case WolfSourceExitStage::Held:return "held";case WolfSourceExitStage::Failed:return "failed";}return "unknown";}

struct WolfSourceTrainyardExit::Impl : ExitHost {
    struct Volume {std::size_t index=0;std::string name;wolf2009::SourceBoxBody box;bool triggerFirst=false;std::vector<std::string> targets;idEntityPtr<idEntity> entity;};
    // Registry receiver for UseTarget on an armable trigger: no key, base Use = activate.
    class ArmReceiver : public WolfSourceUseReceiver {
      public:
        std::string name;
        bool Check(idEntity* target,idEntity* source,const WolfSourceEvent& event,idStr& reason)const override{
            if(!target||!source||event.targetName!=name||!event.useKey.empty()||!target->IsType(idWolfSourceTrigger::Type)){reason="Source trigger Use name/key/class differs";return false;}
            reason.Clear();return true;}
        bool Use(idEntity* target,idEntity* source,const WolfSourceEvent& event,idStr& reason)override{
            if(!Check(target,source,event,reason))return false;target->ProcessEvent(&EV_Activate,source);reason.Clear();return true;}
    };
    const wolf2009::SourceMapBodyPlan* plan=NULL;
    wolf2009::TrainyardMovetrainTail tail;wolf2009::TrainyardShakeArming shake;
    Volume volumes[3];wolf2009::SourceBoxBody blockerBox;
    std::string relayName,shakerName,blockerName;std::vector<std::string> relayTargets;
    std::shared_ptr<WolfNativeSourceRegistry> registry;std::shared_ptr<ArmReceiver> arm;
    WolfSourceGreeterRoute* route=NULL;WolfSourceDirector* director=NULL;
    idEntityPtr<idEntity> blocker,relay,shaker,think,wagon,greeter;
    unsigned greeterSpawnId=0,wagonSpawnId=0;bool busy=false;
    WolfSourceTrainyardExitState state;
    Impl(){++liveExits;}
    ~Impl(){Clear();--liveExits;}
    void Clear(){
        if(auto* e=think.GetEntity())delete e;think=NULL;
        for(auto& v:volumes){if(auto* e=v.entity.GetEntity())delete e;v.entity=NULL;}
        if(auto* e=relay.GetEntity())delete e;relay=NULL;
        if(auto* e=shaker.GetEntity())delete e;shaker=NULL;
        if(auto* e=blocker.GetEntity())delete e;blocker=NULL;
    }
    WolfSourceTriggerState& Trigger(unsigned i){return i==0?state.exitTrigger:i==1?state.armedTrigger:state.dormantTrigger;}
    int Find(idEntity* self){for(unsigned i=0;i<3;++i)if(volumes[i].entity.GetEntity()==self)return int(i);return -1;}
    void Receipt(const std::string& source,const std::string& target,const char* operation,const char* outcome,const std::string& reason,const std::string& key,idEntity* actor){
        WolfSourceTargetReceipt r;r.source=source;r.target=target;r.operation=operation;r.outcome=outcome;r.reason=reason;r.key=key;r.actor=actor?actor->GetName():"";r.timeMs=gameLocal.time;
        if(state.receipts.size()<256)state.receipts.push_back(r);
        gameLocal.Printf("WOLF2009_SOURCE_EXIT_RECEIPT source=%s target=%s op=%s outcome=%s key=%s actor=%s time=%d reason=\"%s\"\n",source.c_str(),target.c_str(),operation,outcome,
            key.empty()?"-":key.c_str(),r.actor.empty()?"-":r.actor.c_str(),r.timeMs,reason.c_str());
    }
    bool Active()const override{return state.shake==WolfSourceExitStage::Waiting||state.tail==WolfSourceExitStage::Waiting;}
    // rvTarget_BRAINUse activate (static 0x20108560): every live target in
    // target-list order gets Use with the relay's own key and the original
    // actor; stale references are skipped.
    void RelayActivate(const std::string& source,idEntity* activator){
        auto* r=relay.GetEntity();
        if(!r||registry->NativeEntity(relayName)!=r){Receipt(source,relayName,"activate","target-expired","Captured native relay generation expired","",activator);return;}
        ++state.relayActivations;Receipt(source,relayName,"activate","delivered","rvTarget_BRAINUse: Use every live target with the relay's own key",state.relayKey,activator);
        for(const auto& target:relayTargets){
            if(target==GreeterName){auto* g=greeter.GetEntity();
                if(!g||unsigned(gameLocal.GetSpawnId(g))!=greeterSpawnId||gameLocal.FindEntity(GreeterName)!=g||!route){Receipt(relayName,target,"use","stale-skipped","Captured greeter generation expired; a stale target is skipped",state.relayKey,activator);continue;}
                WolfSourceAIUseReceipt use;idStr why;
                if(route->ReceiveKeyedUse(state.relayKey,activator,use,why)){++state.greeterUses;
                    Receipt(relayName,target,"use","delivered","idAI Use: CanUseKey root "+std::to_string(use.root)+" latched; "+use.reason+(use.playerBookkeepingOmitted?"; local-player AI bookkeeping omitted":""),state.relayKey,activator);}
                else Receipt(relayName,target,"use","refused",why.c_str(),state.relayKey,activator);
                continue;}
            Receipt(relayName,target,"use","implementation-absent","ENTS target has no native idAI receiver in this profile",state.relayKey,activator);
        }
    }
    void Dispatch(const Volume& volume,idEntity* activator){
        for(const auto& target:volume.targets){
            if(target==relayName)RelayActivate(volume.name,activator);
            else if(target==GreeterName)Receipt(volume.name,target,"activate","refused","idAI activate/unhide handler is not established","",activator);
            else Receipt(volume.name,target,"activate","implementation-absent","ENTS target has no native implementation in this profile","",activator);
        }
    }
    void Touched(idEntity* self,idEntity* other)override{
        const int i=Find(self);if(i<0||!other)return;auto& s=Trigger(unsigned(i));auto& v=volumes[i];
        if(s.phase==WolfSourceTriggerPhase::Fired){++s.lateTouches;return;}
        if(s.phase==WolfSourceTriggerPhase::Dormant){++s.dormantTouches;return;}
        // Default touch filter (no anyTouch/noClient): players only.
        if(!other->IsType(idPlayer::Type)){++s.refusedTouches;return;}
        s.phase=WolfSourceTriggerPhase::Fired;++s.fires;s.fireTimeMs=gameLocal.time;s.fireActivator=other->GetName();
        gameLocal.Printf("WOLF2009_SOURCE_TRIGGER_FIRED name=%s activator=%s time=%d targets=%u wait=-1 removal=posted\n",v.name.c_str(),other->GetName(),gameLocal.time,unsigned(v.targets.size()));
        Dispatch(v,other);
        // wait -1: once, then the trigger removes itself (posted, like the original).
        if(auto* e=v.entity.GetEntity())e->PostEventMS(&EV_Remove,0);
    }
    void Activated(idEntity* self,idEntity* activator)override{
        const int i=Find(self);if(i<0)return;auto& s=Trigger(unsigned(i));
        if(s.phase==WolfSourceTriggerPhase::Dormant&&s.triggerFirst){s.phase=WolfSourceTriggerPhase::Live;s.triggerFirst=false;++s.arms;s.armTimeMs=gameLocal.time;s.armActivator=activator?activator->GetName():"";
            gameLocal.Printf("WOLF2009_SOURCE_TRIGGER_ARMED name=%s activator=%s time=%d trigger_first_cleared=1 fired=0\n",volumes[i].name.c_str(),s.armActivator.c_str(),gameLocal.time);return;}
        ++s.activationRefusals;Receipt(activator?activator->GetName():"-",volumes[i].name,"activate","refused","Activation of a live trigger is outside the measured profile","",activator);
    }
    void Fail(WolfSourceExitStage& stage,const std::string& why){stage=WolfSourceExitStage::Failed;state.reason=why;gameLocal.Warning("WOLF2009_SOURCE_EXIT_STOP reason=%s",why.c_str());}
    WolfSourceEvent Event(WolfSourceEvent::Kind kind,const std::string& source,const std::string& target,std::uint32_t node,std::uint64_t id)const{
        WolfSourceEvent e;e.kind=kind;e.sourceName=source;e.targetName=target;e.levelGeneration=registry->Generation();e.eventId=id;e.sourceNodeId=node;return e;}
    void TickShake(){
        if(state.shake!=WolfSourceExitStage::Waiting)return;
        // Timer: strict now > start + duration (integer ms clock).
        if(!(gameLocal.time>state.shakeStartMs+state.shakeWaitMs))return;
        idStr why;const auto outcome=registry->Deliver(Event(WolfSourceEvent::UseTarget,shakerName,shake.useTarget,shake.useNode,1),why);
        state.shakeUseOutcome=Wolf2009_SourceDeliveryText(outcome);state.shakeUseMs=gameLocal.time;
        if(outcome!=WolfSourceDelivery::Applied){Fail(state.shake,std::string("shake_train node 19 UseTarget failed: ")+why.c_str());return;}
        ++state.shakeUses;state.shakeStopNode=shake.stopNode;state.shakeStopType=shake.stopType;
        state.shakeStopReason="node 31 EnablePlayerWeapons (type 357) has no native handler; nodes 32-34 are not entered";state.shake=WolfSourceExitStage::Held;
        gameLocal.Printf("WOLF2009_SOURCE_SHAKE_ARMING start=%d use_time=%d wait=%s wait_ms=%d node19_use=%s target=%s stop_node=%u stop_type=%u stop=refused\n",
            state.shakeStartMs,state.shakeUseMs,state.shakeWaitRaw.c_str(),state.shakeWaitMs,state.shakeUseOutcome.c_str(),shake.useTarget.c_str(),state.shakeStopNode,state.shakeStopType);
    }
    void TickTail(){
        if(state.tail!=WolfSourceExitStage::Waiting)return;
        if(state.waitStartMs<0){
            WolfSourceDirectorState d;idStr why;if(!director->Query(d,why)){Fail(state.tail,std::string("Director lifetime expired: ")+why.c_str());return;}
            if(d.phase==WolfSourceDirectorPhase::WaitingForArrival)return;
            if(d.phase!=WolfSourceDirectorPhase::PartialArrival||d.arrivals!=1||d.relay==WolfSourceDelivery::Ready){Fail(state.tail,"Director did not enter movetrain node 6");return;}
            // Node 6 UseTarget Enter and its unconditioned output enter node 7
            // in the arrival tick: the Wait's Timer starts at that time.
            state.waitStartMs=d.arrivalTimeMs;tailEvent=d.arrivalEventId;
            gameLocal.Printf("WOLF2009_SOURCE_MOVETRAIN_WAIT node=%u start=%d duration=%s wait_ms=%d relay=%s\n",tail.waitNode,state.waitStartMs,state.waitRaw.c_str(),state.waitMs,Wolf2009_SourceDeliveryText(d.relay));
        }
        if(!(gameLocal.time>state.waitStartMs+state.waitMs))return;
        idStr why;const auto outcome=registry->Deliver(Event(WolfSourceEvent::RemoveTarget,WagonName,tail.removeTarget,tail.removeNode,tailEvent),why);
        state.removalOutcome=Wolf2009_SourceDeliveryText(outcome);state.removalTimeMs=gameLocal.time;
        if(outcome!=WolfSourceDelivery::Applied){Fail(state.tail,std::string("movetrain node 8 RemoveTarget failed: ")+why.c_str());return;}
        ++state.removals;state.blockerAlive=false;
        // Node 8 -> node 9 is unconditioned: ExecConsoleCommand enters in this tick.
        state.consoleNode=tail.consoleNode;state.consoleType=tail.consoleType;state.consoleCommand=tail.consoleCommand;
        state.consoleReason="ExecConsoleCommand CheckOneOfMany is not established; not executed";state.tail=WolfSourceExitStage::Held;
        gameLocal.Printf("WOLF2009_SOURCE_BLOCKER_REMOVED node=%u target=%s wait_start=%d removal_time=%d elapsed=%d outcome=%s wagon_bind_kept=%d next_node=%u next_type=%u command=%s next=refused\n",
            tail.removeNode,tail.removeTarget.c_str(),state.waitStartMs,state.removalTimeMs,state.removalTimeMs-state.waitStartMs,state.removalOutcome.c_str(),
            int(registry->NativeEntity(WagonName)==wagon.GetEntity()&&wagon.GetEntity()!=NULL),state.consoleNode,state.consoleType,state.consoleCommand.c_str());
    }
    std::uint64_t tailEvent=0;
    void Tick()override{
        if(!Active())return;
        if(busy)return;struct Scope{bool& b;explicit Scope(bool& v):b(v){b=true;}~Scope(){b=false;}}scope(busy);
        ++state.thinkFrames;
        try{TickShake();TickTail();}catch(const std::exception& error){if(state.shake==WolfSourceExitStage::Waiting)Fail(state.shake,error.what());if(state.tail==WolfSourceExitStage::Waiting)Fail(state.tail,error.what());}
    }
};

WolfSourceTrainyardExit::WolfSourceTrainyardExit(std::shared_ptr<Impl> value):impl(std::move(value)){}
WolfSourceTrainyardExit::~WolfSourceTrainyardExit(){impl->Clear();}
idEntity* WolfSourceTrainyardExit::Blocker()const{return impl->blocker.GetEntity();}
idEntity* WolfSourceTrainyardExit::Relay()const{return impl->relay.GetEntity();}
idEntity* WolfSourceTrainyardExit::Trigger(const std::string& name)const{for(const auto& v:impl->volumes)if(v.name==name)return v.entity.GetEntity();return NULL;}
int WolfSourceTrainyardExit::Inspect(const WolfSourceEvent& event,idStr& reason)const{if(!impl->registry){reason="Trainyard exit is not spawned";return -1;}return int(impl->registry->Inspect(event,reason));}

std::unique_ptr<WolfSourceTrainyardExit> Wolf2009_PrepareSourceTrainyardExit(const WolfSourceDeclarationSet& set,const wolf2009::SourceMapBodyPlan& plan,
    const std::vector<unsigned char>& movetrain,const std::vector<unsigned char>& shakeTrain,idStr& reason){reason.Clear();try{
    const auto& d=plan.entities;Need(d.entities.size()==2238,"Measured trainyard ENTS entity count differs");
    std::shared_ptr<WolfSourceTrainyardExit::Impl> out(new WolfSourceTrainyardExit::Impl);out->plan=&plan;
    // Classes from the compiled declaration namespace.
    {const auto t=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"trigger_once");
        Need(t.opaqueFacts.empty()&&t.literalFields.size()==9,"trigger_once has unadmitted effective fields");
        Need(LitText(t,"spawnclass")=="idTrigger_Multi"&&LitText(t,"classname")=="trigger_once"&&LitInt(t,"wait")==-1&&LitInt(t,"random")==0&&LitInt(t,"delay")==0&&
            LitInt(t,"anyTouch")==0&&LitInt(t,"noTouch")==0&&LitInt(t,"noClient")==0&&LitInt(t,"noModel")==1,"trigger_once effective fields differ from the measured idTrigger_Multi profile");
        const auto r=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"target_brainuse");
        Need(r.opaqueFacts.empty()&&r.literalFields.size()==3&&LitText(r,"spawnclass")=="rvTarget_BRAINUse"&&LitInt(r,"noModel")==1,"target_brainuse class differs");
        const auto s=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"func_scriptrunner");
        Need(s.opaqueFacts.empty()&&s.literalFields.size()==3&&LitText(s,"spawnclass")=="rvTarget_ScriptRunner"&&LitInt(s,"allowZoneDeactivate")==0,"func_scriptrunner class differs");
        const auto f=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"func_static");
        Need(LitText(f,"spawnclass")=="idStaticEntity"&&LitInt(f,"solid")==1&&LitInt(f,"noclipmodel")==0,"func_static class differs");}
    // Instances.
    const char* names[3]={"exit_train_trigger","trigger_once_1","layer3_trigger_once_2"};const std::size_t indices[3]={ExitIndex,ArmedIndex,DormantIndex};
    for(unsigned i=0;i<3;++i){auto& v=out->volumes[i];v.index=Unique(d,names[i]);Need(v.index==indices[i],"Exit trigger ENTS index differs");v.name=names[i];
        const auto& e=d.entities[v.index];Need(Text(e,"classname")=="trigger_once"&&Text(e,"model")==v.name,"Exit trigger class/model differs");
        if(i==0){Keys(e,{"classname","name","model","origin","target"});v.targets.push_back(Text(e,"target"));v.triggerFirst=false;}
        else{Keys(e,{"classname","name","model","origin","frame","target1","target2","target3","target4","triggerfirst"});Need(Integer(e,"triggerFirst")==1,"Armable trigger triggerFirst differs");
            // Raw editor frame is retained, not interpreted.
            Need(Integer(e,"frame")==182,"Armable trigger raw frame differs");v.triggerFirst=true;
            for(const char* k:{"target1","target2","target3","target4"})v.targets.push_back(Text(e,k));}
        v.box=wolf2009::DecodeSourceEntityBoxBody(plan,v.index,wolf2009::SourceBoxBodyProfile::PhantomTriggerBoxV1);}
    Need(out->volumes[0].targets==std::vector<std::string>({"train_greeter_brainuse_2"}),"exit_train_trigger target differs");
    const std::vector<std::string> armedTargets={"nazi_interragator_1","nazi_interragator_2",GreeterName,"layer38_target_lightswitch_1"};
    Need(out->volumes[1].targets==armedTargets&&out->volumes[2].targets==armedTargets,"Armable trigger targets differ");
    for(const auto& t:armedTargets)Unique(d,t);
    {const auto& e=d.entities[Unique(d,"train_greeter_brainuse_2")];Need(Unique(d,"train_greeter_brainuse_2")==RelayIndex,"Exit relay index differs");
        Keys(e,{"classname","name","origin","target","usekey","target1"});Need(Text(e,"classname")=="target_brainuse","Exit relay class differs");
        out->relayName=Text(e,"name");out->state.relayKey=Text(e,"useKey");Need(out->state.relayKey=="player_leaves_train","Exit relay own key differs");
        // Target-list order follows the ENTS dictionary order: target, target1.
        for(const auto& p:e.properties){const auto k=Fold(p.key);if(k=="target"||k=="target1")out->relayTargets.push_back(Text(e,p.key.c_str()));}
        Need(out->relayTargets==std::vector<std::string>({GreeterName,"nazi_interragator_1"}),"Exit relay target list differs");out->state.relayName=out->relayName;}
    {const auto& e=d.entities[BlockerIndex];Need(Unique(d,"layer3_func_static_3")==BlockerIndex,"Blocker index differs");
        Keys(e,{"classname","name","model","origin","rendersideonly","bind"});Need(Text(e,"classname")=="func_static"&&Text(e,"bind")==WagonName&&Text(e,"model")=="layer3_func_static_3"&&Integer(e,"renderSideOnly")==0,"Blocker instance differs");
        out->blockerName=Text(e,"name");out->blockerBox=wolf2009::DecodeSourceEntityBoxBody(plan,BlockerIndex,wolf2009::SourceBoxBodyProfile::SolidBoxV1);}
    {const auto& e=d.entities[ShakerIndex];Need(Unique(d,"train_shaker")==ShakerIndex,"train_shaker index differs");
        Keys(e,{"classname","name","origin","brain","windspeed"});Need(Text(e,"classname")=="func_scriptrunner"&&Text(e,"brain")=="trainyard/shake_train.brain","train_shaker instance differs");
        out->shakerName=Text(e,"name");}
    out->tail=wolf2009::DecodeTrainyardMovetrainTailH1023(movetrain);
    Need(out->tail.relayTarget=="train_greeter_brainuse_1"&&out->tail.relayKey=="train_Stop"&&out->tail.removeTarget==out->blockerName&&out->tail.consoleCommand=="CheckOneOfMany"&&
        out->tail.wait.raw=="6000 0"&&out->tail.wait.milliseconds==6000,"Movetrain tail differs from the measured profile");
    out->shake=wolf2009::DecodeTrainyardShakeArmingH1023(shakeTrain);
    Need(out->shake.useTarget=="trigger_once_1"&&out->shake.wait.raw=="1 1"&&out->shake.wait.milliseconds==1000,"shake_train arming prefix differs");
    auto& s=out->state;s.waitMs=out->tail.wait.milliseconds;s.waitRaw=out->tail.wait.raw;s.shakeWaitMs=out->shake.wait.milliseconds;s.shakeWaitRaw=out->shake.wait.raw;
    for(unsigned i=0;i<3;++i){auto& t=out->Trigger(i);const auto& v=out->volumes[i];t.name=v.name;t.entityIndex=unsigned(v.index);t.triggerFirst=v.triggerFirst;
        t.phase=v.triggerFirst?WolfSourceTriggerPhase::Dormant:WolfSourceTriggerPhase::Live;
        for(unsigned k=0;k<3;++k){t.origin[k]=float(v.box.origin[k]);t.mins[k]=float(v.box.center[k]-v.box.halfExtents[k]);t.maxs[k]=float(v.box.center[k]+v.box.halfExtents[k]);}}
    return std::unique_ptr<WolfSourceTrainyardExit>(new WolfSourceTrainyardExit(std::move(out)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceTrainyardExit>();}}

bool WolfSourceTrainyardExit::Spawn(std::shared_ptr<WolfNativeSourceRegistry> registry,idEntity* wagon,WolfSourceGreeterRoute* route,idStr& reason){reason.Clear();auto& p=*impl;try{
    Need(registry&&registry->Generation()&&!p.registry&&!p.think.GetEntity(),"Trainyard exit assembly state/registry");
    Need(!gameLocal.isMultiplayer&&!gameLocal.isClient&&gameLocal.GameState()==GAMESTATE_ACTIVE,"Trainyard exit requires an active local single-player game");
    Need(wagon&&registry->NativeEntity(WagonName)==wagon&&Wolf2009_IsSourceMover(wagon),"Trainyard exit requires the registered source wagon");
    WolfSourceMoverState mover;NeedReason(Wolf2009_QuerySourceMover(wagon,mover,reason),reason,"Trainyard exit wagon query failed");
    Need(mover.spawnDeliveries==0&&mover.arrivals==0,"Trainyard exit assembly must precede wagon OnSpawn");
    Need(route&&route->Actor()&&route->Actor()->NativeEntity(),"Trainyard exit requires the spawned greeter route");
    auto* greeter=route->Actor()->NativeEntity();Need(gameLocal.FindEntity(GreeterName)==greeter,"Greeter native name differs");
    for(const auto* name:{p.blockerName.c_str(),p.relayName.c_str(),p.shakerName.c_str(),"exit_train_trigger","trigger_once_1","layer3_trigger_once_2"})
        Need(!gameLocal.FindEntity(name),"Trainyard exit native name is occupied");
    const std::shared_ptr<ExitHost> host=impl;HostContext context(host);
    // Doorway blocker: func_static/idStaticEntity, solid, its decoded HKXS box
    // around the authored origin, then bound to the wagon at its authored pose.
    {idDict args;args.Set("classname","wolf2009_source_exit_blocker");args.Set("name",p.blockerName.c_str());args.Set("origin",Words(p.blockerBox.origin,3).c_str());
        double mins[3],maxs[3];for(unsigned k=0;k<3;++k){mins[k]=p.blockerBox.center[k]-p.blockerBox.halfExtents[k];maxs[k]=p.blockerBox.center[k]+p.blockerBox.halfExtents[k];}
        args.Set("mins",Words(mins,3).c_str());args.Set("maxs",Words(maxs,3).c_str());args.Set("solid","1");args.Set("networkSync","0");
        std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idStaticEntity::Type,&args));Need(entity&&entity->GetPhysics()->GetClipModel()&&entity->GetPhysics()->GetContents()==CONTENTS_SOLID,"Native doorway blocker allocation failed");
        entity->Bind(wagon,true);Need(entity->GetBindMaster()==wagon,"Doorway blocker did not bind to the wagon");p.blocker=entity.release();}
    for(auto& v:p.volumes){idDict args;args.Set("classname","wolf2009_source_trigger");args.Set("name",v.name.c_str());args.Set("origin",Words(v.box.origin,3).c_str());
        double mins[3],maxs[3];for(unsigned k=0;k<3;++k){mins[k]=v.box.center[k]-v.box.halfExtents[k];maxs[k]=v.box.center[k]+v.box.halfExtents[k];}
        args.Set("mins",Words(mins,3).c_str());args.Set("maxs",Words(maxs,3).c_str());args.Set("networkSync","0");
        std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourceTrigger::Type,&args));
        Need(entity&&entity->GetPhysics()->GetClipModel()&&entity->GetPhysics()->GetContents()==CONTENTS_TRIGGER,"Native source trigger allocation failed");v.entity=entity.release();}
    for(const auto& name:{p.relayName,p.shakerName}){const auto& source=p.plan->entities.entities[name==p.relayName?RelayIndex:ShakerIndex];const auto pose=wolf2009::ReadEntityPlacement(source);
        double origin[3]={pose.origin[0],pose.origin[1],pose.origin[2]};idDict args;args.Set("classname",name==p.relayName?"wolf2009_source_exit_relay":"wolf2009_source_scriptrunner");
        args.Set("name",name.c_str());args.Set("origin",Words(origin,3).c_str());args.Set("noclipmodel","1");args.Set("networkSync","0");
        std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idTarget::Type,&args));Need(bool(entity),"Native exit relay/scriptrunner allocation failed");
        if(name==p.relayName)p.relay=entity.release();else p.shaker=entity.release();}
    p.arm.reset(new WolfSourceTrainyardExit::Impl::ArmReceiver);p.arm->name=p.shake.useTarget;
    registry->Bind(p.shakerName,"func_scriptrunner",p.shaker.GetEntity(),std::shared_ptr<WolfSourceUseReceiver>());
    registry->Bind(p.relayName,"target_brainuse",p.relay.GetEntity(),std::shared_ptr<WolfSourceUseReceiver>());
    registry->Bind(p.blockerName,"func_static",p.blocker.GetEntity(),std::shared_ptr<WolfSourceUseReceiver>(),true);
    for(auto& v:p.volumes)registry->Bind(v.name,"trigger_once",v.entity.GetEntity(),v.name==p.shake.useTarget?std::shared_ptr<WolfSourceUseReceiver>(p.arm):std::shared_ptr<WolfSourceUseReceiver>());
    p.registry=std::move(registry);p.route=route;p.wagon=wagon;p.wagonSpawnId=unsigned(p.wagon.GetSpawnId());p.greeter=greeter;p.greeterSpawnId=unsigned(gameLocal.GetSpawnId(greeter));
    p.state.levelGeneration=p.registry->Generation();p.state.blockerAlive=p.state.blockerBound=true;
    for(unsigned i=0;i<3;++i){auto& t=p.Trigger(i);t.alive=true;t.spawnId=unsigned(gameLocal.GetSpawnId(p.volumes[i].entity.GetEntity()));}
    const auto bounds=SourceBox(p.blocker.GetEntity());for(unsigned k=0;k<3;++k){p.state.blockerMins[k]=bounds[0][k];p.state.blockerMaxs[k]=bounds[1][k];}
    gameLocal.Printf("WOLF2009_SOURCE_EXIT_ASSEMBLED generation=%llu blocker=%s bind=%s blocker_abs=(%.3f %.3f %.3f)-(%.3f %.3f %.3f) triggers=3 dormant=%u relay=%s relay_targets=%u shaker=%s body_rotation=identity trigger_contents=4096 blocker_contents=1 blocker_visual=refused windSpeed=unconsumed\n",
        static_cast<unsigned long long>(p.state.levelGeneration),p.blockerName.c_str(),WagonName,bounds[0][0],bounds[0][1],bounds[0][2],bounds[1][0],bounds[1][1],bounds[1][2],2u,p.relayName.c_str(),unsigned(p.relayTargets.size()),p.shakerName.c_str());
    return true;
}catch(const std::exception& error){reason=error.what();p.Clear();p.registry.reset();return false;}}

bool WolfSourceTrainyardExit::Start(WolfSourceDirector* director,idStr& reason){reason.Clear();auto& p=*impl;try{
    Need(p.registry&&director&&!p.think.GetEntity()&&p.state.shake==WolfSourceExitStage::Prepared,"Trainyard exit start is not assembled");
    WolfSourceDirectorState current;NeedReason(director->Query(current,reason),reason,"Trainyard exit director is unavailable");
    Need(current.phase==WolfSourceDirectorPhase::WaitingForArrival&&current.levelGeneration==p.state.levelGeneration,"Trainyard exit director generation differs");
    const std::string name="wolf2009_source_trainyard_exit_"+std::to_string(p.state.levelGeneration);Need(!gameLocal.FindEntity(name.c_str()),"Trainyard exit Think name is occupied");
    idDict args;args.Set("classname","wolf2009_source_trainyard_exit");args.Set("name",name.c_str());args.Set("noclipmodel","1");args.Set("networkSync","0");
    const std::shared_ptr<ExitHost> host=impl;HostContext context(host);
    std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourceExitThink::Type,&args));Need(entity&&entity->GetType()==&idWolfSourceExitThink::Type,"Trainyard exit Think allocation failed");
    p.director=director;p.think=entity.release();
    // shake_train OnSpawn (root 1) -> node 30 Wait at map spawn.
    p.state.shakeStartMs=gameLocal.time;p.state.shake=WolfSourceExitStage::Waiting;p.state.tail=WolfSourceExitStage::Waiting;
    gameLocal.Printf("WOLF2009_SOURCE_SHAKE_ONSPAWN start=%d wait_node=%u wait=%s wait_ms=%d use_node=%u target=%s\n",p.state.shakeStartMs,p.shake.waitNode,p.state.shakeWaitRaw.c_str(),p.state.shakeWaitMs,p.shake.useNode,p.shake.useTarget.c_str());
    return true;
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceTrainyardExit::Query(WolfSourceTrainyardExitState& out,idStr& reason)const{reason.Clear();try{const auto& p=*impl;
    if(p.registry){
        if(p.state.shake==WolfSourceExitStage::Waiting||p.state.tail==WolfSourceExitStage::Waiting)Need(p.think.GetEntity()!=NULL,"Trainyard exit Think lifetime expired");
        if(p.state.blockerAlive){auto* b=p.blocker.GetEntity();Need(b&&b->GetBindMaster()==p.wagon.GetEntity()&&p.wagon.GetEntity()&&unsigned(p.wagon.GetSpawnId())==p.wagonSpawnId,"Doorway blocker/wagon binding expired");}
        else Need(!p.blocker.GetEntity(),"Removed doorway blocker is still alive");}
    out=p.state;
    for(unsigned i=0;i<3;++i){auto& t=i==0?out.exitTrigger:i==1?out.armedTrigger:out.dormantTrigger;t.alive=p.volumes[i].entity.GetEntity()!=NULL;}
    out.blockerBound=p.blocker.GetEntity()&&p.blocker.GetEntity()->GetBindMaster()==p.wagon.GetEntity();
    if(auto* b=p.blocker.GetEntity()){const auto bounds=SourceBox(b);for(unsigned k=0;k<3;++k){out.blockerMins[k]=bounds[0][k];out.blockerMaxs[k]=bounds[1][k];}}
    return true;
}catch(const std::exception& error){reason=error.what();return false;}}

int Wolf2009_LiveSourceTrainyardExits(){return liveExits;}
int Wolf2009_LiveSourceTriggers(){return liveTriggers;}

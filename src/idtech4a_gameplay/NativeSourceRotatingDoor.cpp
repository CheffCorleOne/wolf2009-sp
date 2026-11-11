// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
// Standard headers used by the source owners must precede idlib's printf guards.
#include <map>
#include <set>
#include <array>
#include <cstddef>
#include <functional>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceRotatingDoor.h"
#include "NativeSourceMover.h"
#include "NativeSourceRegistry.h"
#include "SourceDeclarationSetRuntime.h"
#include "SourceDefinitions.h"
#include "SourceMapBodies.h"
#include "RotatingDoorOld.h"
#include "RenderData.h"

namespace {
namespace door=wolf2009::rotatingdoor;
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
bool Refuse(idStr& reason,const char* why){reason=why;return false;}
const char* const Profile="SourceRotatingDoorOldV1";
// HingedSolidContentsV1: the named stand-in for original physics contents
// 0x10003 (raw HKXS filter 3); ParametricPushStandInV1: GPL mover push.
const int HingedContents=CONTENTS_SOLID|CONTENTS_OPAQUE;
int liveDoors=0;
std::string Words(const float* values,unsigned count){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
    for(unsigned i=0;i<count;++i){if(i)out<<' ';out<<values[i];}return out.str();}
double Number(const wolf2009::ResolvedSourceDefinition& d,const char* key,bool& has){
    const auto* f=d.Find(key);has=f!=NULL;if(!f)return 0;
    if(f->encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&f->numericWords.size()==1)return double(std::int32_t(f->numericWords[0]));
    if(f->encoding==wolf2009::SourceLiteralEncoding::CompiledFloat32&&f->numericWords.size()==1){std::uint32_t w=f->numericWords[0];float v;std::memcpy(&v,&w,4);
        Need(std::isfinite(v),std::string("Rotating door class field is not finite: ")+key);return v;}
    if((f->encoding==wolf2009::SourceLiteralEncoding::CompiledString||f->encoding==wolf2009::SourceLiteralEncoding::PlainText)&&f->value.size()==1){
        char* end=NULL;const double v=std::strtod(f->value[0].text.c_str(),&end);Need(end&&!*end&&std::isfinite(v),std::string("Rotating door class numeric text differs: ")+key);return v;}
    throw std::runtime_error(std::string("Rotating door class numeric field has an unqualified encoding: ")+key);}
std::string Literal(const wolf2009::ResolvedSourceDefinition& d,const char* key){const auto* f=d.Find(key);if(!f)return std::string();
    Need((f->encoding==wolf2009::SourceLiteralEncoding::CompiledString||f->encoding==wolf2009::SourceLiteralEncoding::PlainText)&&f->value.size()==1,
        std::string("Rotating door class text field is not one literal: ")+key);return f->value[0].text;}
std::string Lower(std::string v){for(char& c:v)if(c>='A'&&c<='Z')c=char(c+32);return v;}
float Normalize180(float degrees){idAngles a(degrees,0,0);a.Normalize180();return a.pitch;}
idBounds WorldBounds(const idClipModel* clip,const idVec3& origin,const idMat3& axis){idBounds out;out.FromTransformedBounds(clip->GetBounds(),origin,axis);return out;}
void Store(float* out,const idBounds& b){for(int i=0;i<3;++i){out[i]=b[0][i];out[3+i]=b[1][i];}}
struct DoorData {
    door::DoorProfile profile;wolf2009::SourceBoxBody body;door::LocalBox box;
    std::string name,sourceClass,nativeModel,keyReceipts;unsigned index=0;
    idVec3 origin;idBounds localBox;door::LocalBox expectedOpen;
};
std::shared_ptr<const DoorData> spawningDoor;
struct SpawnContext {explicit SpawnContext(std::shared_ptr<const DoorData> value){Need(!spawningDoor,"Rotating door Spawn reentry refused");spawningDoor=std::move(value);}~SpawnContext(){spawningDoor.reset();}};
}

// Native rvRotatingDoor stand-in. Activation runs the portable
// RotatingDoorOldStateV1 decision; motion is the physics' own angular
// extrapolation; completion is the first Think whose physics is at rest at
// the target angle with gameLocal.time at or past the end time.
class idWolfSourceRotatingDoor : public idMover {
  public:
    CLASS_PROTOTYPE(idWolfSourceRotatingDoor);
    idWolfSourceRotatingDoor():data(spawningDoor){++liveDoors;}
    ~idWolfSourceRotatingDoor(){physicsObj.UnlinkClip();data.reset();--liveDoors;}
    void Spawn(){
        Need(bool(data),"Rotating door allocated without prepared data");
        Need(GetPhysics()==&physicsObj&&physicsObj.GetClipModel()&&physicsObj.GetClipModel()->IsTraceModel()&&physicsObj.IsPusher(),
            "Rotating door did not get a parametric pusher with its trace-model box");
        Need(physicsObj.GetClipModel()->GetBounds().Compare(data->localBox,.001f),"Rotating door clip box differs from HingedSolidBoxV1");
        // Like the other source owners the door keeps the game clock in a
        // cinematic (no time shift of its extrapolation while skipped).
        physicsObj.SetContents(HingedContents);physicsObj.SetClipMask(MASK_SOLID);fl.networkSync=false;fl.neverDormant=true;cinematic=true;
        Need(GetPhysics()->GetOrigin().Compare(data->origin,.0001f)&&GetPhysics()->GetAxis().Compare(mat3_identity,.0001f),"Rotating door placement differs after Spawn");
        Need(renderEntity.hModel&&idStr::Icmp(renderEntity.hModel->Name(),data->nativeModel.c_str())==0,"Rotating door visual model differs after Spawn");
        // rvRotatingDoor Spawn ends with BecomeInactive(all).
        BecomeInactive(TH_THINK|TH_PHYSICS);
    }
    void Save(idSaveGame*)const{gameLocal.Error("Rotating door save-state serialization is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Rotating door restore is unimplemented");}
    bool Check(idStr& reason)const{
        if(!data)return Refuse(reason,"Rotating door has no prepared data");
        if(GetBindMaster()||GetPhysics()!=&physicsObj||!physicsObj.IsPusher()||!physicsObj.GetClipModel()||!physicsObj.GetClipModel()->IsTraceModel()||
            physicsObj.GetContents()!=HingedContents||physicsObj.GetClipMask()!=MASK_SOLID)return Refuse(reason,"Rotating door physics/collision capability changed");
        if(!renderEntity.hModel||idStr::Icmp(renderEntity.hModel->Name(),data->nativeModel.c_str())!=0)return Refuse(reason,"Rotating door visual model changed");
        reason.Clear();return true;}
    float AxisAngle()const{idAngles a;physicsObj.GetLocalAngles(a);return Normalize180(a.pitch);}
    // activate gates + Toggle (RotatingDoorOldStateV1) with the door's state.
    bool SourceActivate(door::Activator who,idStr& reason){
        if(!failure.empty())return Refuse(reason,failure.c_str());
        if(!Check(reason)){Fail(reason.c_str());return false;}
        door::ToggleInput in;in.state=doorState;in.activator=who;in.enabled=true;in.toggle=data->profile.keys.toggle;in.axis=data->profile.keys.axis;
        in.openDegrees=data->profile.openDegrees;in.time=data->profile.time;in.currentAngle=AxisAngle();in.lastDelta=lastDelta;
        in.waitMs=data->profile.waitMs;in.now=gameLocal.time;
        ++activations;
        door::ToggleResult r;try{r=door::ActivateV1(in);}catch(const std::exception& error){Fail(error.what());return Refuse(reason,error.what());}
        if(r.effect==door::ToggleResult::Effect::Ignored){++ignoredActivations;lastIgnored=r.reason;reason.Clear();
            gameLocal.Printf("WOLF2009_SOURCE_ROTATING_DOOR_IGNORED name=%s state=%s reason=\"%s\" time=%d\n",data->name.c_str(),door::DoorStateText(doorState),r.reason.c_str(),gameLocal.time);return true;}
        if(r.effect!=door::ToggleResult::Effect::Move){Fail("RotatingDoorOldStateV1 re-post after wait is not admitted by SourceRotatingDoorOldV1 (toggle)");return Refuse(reason,failure.c_str());}
        Need(r.axis==door::Axis::Y,"SourceRotatingDoorOldV1 admits only the y_axis (pitch) hinge");
        const idAngles base(in.currentAngle,0,0),speed(float(r.speed),0,0);
        physicsObj.SetAngularExtrapolation(extrapolation_t(EXTRAPOLATION_LINEAR),gameLocal.time,r.durationMs,base,speed,ang_zero);
        doorState=r.next;lastDelta=r.delta;target=float(r.target);delta=float(r.delta);speedValue=float(r.speed);durationMs=r.durationMs;
        moveStartTimeMs=r.startTime;moveEndTimeMs=r.endTime;++moveStarts;
        BecomeActive(TH_THINK|TH_PHYSICS);
        if(!r.soundKey.empty())SoundReceipt(r.soundKey,r.soundKey=="snd_open"?data->profile.sndOpen:data->profile.sndClose);
        if(r.portalOpen)portalCalls+=1;
        gameLocal.Printf("WOLF2009_SOURCE_ROTATING_DOOR_MOVE name=%s activator=%s state=%s delta=%.3f target=%.3f speed=%.3f duration_ms=%d start=%d end=%d portal=AreaPortalAbsentV1\n",
            data->name.c_str(),who==door::Activator::Self?"self":"other",door::DoorStateText(doorState),r.delta,r.target,r.speed,r.durationMs,r.startTime,r.endTime);
        reason.Clear();return true;}
    void SoundReceipt(const std::string& key,const std::string& shader){
        if(!soundReceipts.empty())soundReceipts+=';';
        // A missing sound binding is a receipt, not a gate (TRAPDOOR T1 step 6).
        soundReceipts+=key+"="+shader+":SourceSoundBindingAbsentV1";}
    void Think() override {
        if(!failure.empty()){BecomeInactive(TH_THINK|TH_PHYSICS);return;}
        idStr reason;if(!Check(reason)){Fail(reason.c_str());return;}
        idMover::Think();++thinkFrames;
        if(auto* blocker=physicsObj.GetBlockingEntity()){++blockedFrames;(void)blocker;}
        if(doorState==door::DoorState::Open)++openThinkFrames;
        door::ThinkInput in;in.state=doorState;in.toggle=data->profile.keys.toggle;in.now=gameLocal.time;in.endTime=moveEndTimeMs;in.waitMs=data->profile.waitMs;
        in.triggerClosed.push_back(data->profile.keys.triggerClosed);in.sndClose=data->profile.sndClose;
        const auto r=door::ThinkV1(in);
        if(r.velocityReset)++velocityResets;
        if(!r.completed)return;
        // Completion requires the physics' own end state, not the clock alone.
        const float angle=AxisAngle();
        if(!physicsObj.IsAtRest()||physicsObj.GetAngularEndTime()>gameLocal.time||std::fabs(angle-target)>.001f){++unsettledFrames;return;}
        if(r.next==door::DoorState::Open){
            doorState=door::DoorState::Open;++openCompletions;openCompletionTimeMs=gameLocal.time;
            // The door keeps thinking while open; toggle posts no auto-close.
            BecomeActive(TH_THINK);
        }else{
            doorState=door::DoorState::Closed;++closeCompletions;closeCompletionTimeMs=gameLocal.time;BecomeInactive(TH_THINK|TH_PHYSICS);
            SoundReceipt(r.soundKey,r.sound);
            for(const auto& name:r.activateNames)if(!gameLocal.FindEntity(name.c_str()))++triggerClosedAbsent;
        }
        gameLocal.Printf("WOLF2009_SOURCE_ROTATING_DOOR_COMPLETE name=%s state=%s angle=%.4f physics_end=%d time=%d at_rest=%d\n",
            data->name.c_str(),door::DoorStateText(doorState),angle,physicsObj.GetAngularEndTime(),gameLocal.time,int(physicsObj.IsAtRest()));
    }
    void Fail(const char* why){failure=why;idAngles a;physicsObj.GetLocalAngles(a);
        physicsObj.SetAngularExtrapolation(EXTRAPOLATION_NONE,gameLocal.time,0,a,ang_zero,ang_zero);BecomeInactive(TH_THINK|TH_PHYSICS);
        gameLocal.Warning("WOLF2009_SOURCE_ROTATING_DOOR_FAIL name=%s reason=%s",data?data->name.c_str():"?",why);}
    void Event_SourceActivate(idEntity*){
        // Stock idMover activate is not this class; outside activations are T2.
        ++genericActivateRefusals;}
    // idEntity::RunPhysics posts <reachedang>/<reachedpos> at the physics end
    // time. Stock idMover would run its own rotation stages there and replace
    // this door's extrapolation; rvRotatingDoor completes in its Think, so the
    // notification is only counted.
    void Event_ReachedAng(){++reachedAngEvents;}
    void Event_ReachedPos(){++reachedPosEvents;}
    std::shared_ptr<const DoorData> data;
    unsigned reachedAngEvents=0,reachedPosEvents=0;
    door::DoorState doorState=door::DoorState::Closed;double lastDelta=0;
    float target=0,delta=0,speedValue=0;int durationMs=0,moveStartTimeMs=-1,moveEndTimeMs=-1,openCompletionTimeMs=-1,closeCompletionTimeMs=-1;
    unsigned activations=0,ignoredActivations=0,moveStarts=0,openCompletions=0,closeCompletions=0,thinkFrames=0,openThinkFrames=0,velocityResets=0,
        genericActivateRefusals=0,triggerClosedAbsent=0,blockedFrames=0,unsettledFrames=0,portalCalls=0;
    std::string failure,soundReceipts,lastIgnored;
};
CLASS_DECLARATION(idMover,idWolfSourceRotatingDoor)
    EVENT(EV_Activate,idWolfSourceRotatingDoor::Event_SourceActivate)
    EVENT(EV_ReachedAng,idWolfSourceRotatingDoor::Event_ReachedAng)
    EVENT(EV_ReachedPos,idWolfSourceRotatingDoor::Event_ReachedPos)
END_CLASS
namespace {
idWolfSourceRotatingDoor* DoorEntity(idEntity* e){return e&&e->IsType(idWolfSourceRotatingDoor::Type)?static_cast<idWolfSourceRotatingDoor*>(e):NULL;}
const idWolfSourceRotatingDoor* DoorEntity(const idEntity* e){return e&&e->IsType(idWolfSourceRotatingDoor::Type)?static_cast<const idWolfSourceRotatingDoor*>(e):NULL;}
// Registry receiver: outside activation (shut_door, T2) is an explicit refusal.
class OutsideActivation : public WolfSourceUseReceiver {
  public:
    bool Check(idEntity*,idEntity*,const WolfSourceEvent&,idStr& reason)const override{
        reason="SourceRotatingDoorOldV1: outside activation of sewer_trapdoor (shut_door, TRAPDOOR T2) is not admitted";return false;}
    bool Use(idEntity* t,idEntity* s,const WolfSourceEvent& e,idStr& reason)override{return Check(t,s,e,reason);}
};
}

struct WolfSourceRotatingDoor::Impl {
    std::shared_ptr<const DoorData> data;std::shared_ptr<WolfNativeSourceRegistry> registry;idEntityPtr<idEntity> entity;
    WolfSourceRotatingDoorState initial;unsigned postSpawn=0,duplicatePostSpawn=0;
    ~Impl(){if(auto* e=entity.GetEntity())delete e;entity=NULL;}
    void Trace(float& fraction,int& hit)const{trace_t tr;gameLocal.clip.TracePoint(tr,idVec3(-256,576,330),idVec3(-256,576,290),MASK_SOLID,NULL);
        fraction=tr.fraction;hit=tr.fraction<1.f?tr.c.entityNum:-1;}
};
WolfSourceRotatingDoor::WolfSourceRotatingDoor(std::unique_ptr<Impl> value):impl(std::move(value)){}
WolfSourceRotatingDoor::~WolfSourceRotatingDoor(){}
idEntity* WolfSourceRotatingDoor::Entity()const{return impl->entity.GetEntity();}

std::unique_ptr<WolfSourceRotatingDoor> Wolf2009_PrepareSourceRotatingDoor(const WolfSourceDeclarationSet& set,const wolf2009::SourceMapBodyPlan& plan,
    const WolfSourceRotatingDoorRequest& request,idStr& reason){reason.Clear();try{
    Need(request.moverPolicy==WolfSourceMover_NativeRotatingDoorOldV1&&request.profile==Profile&&request.sourceMapName=="maps/game/trainyard/trainyard",
        "Explicit SourceRotatingDoorOldV1 mover policy/profile required");
    std::shared_ptr<DoorData> data(new DoorData);std::size_t matches=0;
    for(std::size_t i=0;i<plan.entities.entities.size();++i){const auto* c=plan.entities.entities[i].Find("classname");
        if(c&&c->kind==wolf2009::EntityProperty::Text&&c->text=="func_door_rotating_old"){data->index=unsigned(i);++matches;}}
    Need(matches==1,"SourceRotatingDoorOldV1: trainyard func_door_rotating_old is absent or ambiguous");
    const auto keys=door::QualifyTrainyardSewerTrapdoorKeysV1(plan.entities.entities[data->index]);
    const auto def=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,keys.classname);
    door::ClassFacts facts;facts.spawnclass=Literal(def,"spawnclass");facts.sndOpen=Literal(def,"snd_open");facts.sndClose=Literal(def,"snd_close");
    facts.time=Number(def,"time",facts.hasTime);facts.wait=Number(def,"wait",facts.hasWait);facts.openDegrees=Number(def,"openDegrees",facts.hasOpenDegrees);
    for(const auto& field:def.literalFields){facts.keys.push_back(Lower(field.first));if(field.second.encoding==wolf2009::SourceLiteralEncoding::CompiledOpaque)facts.opaqueKeys.push_back(Lower(field.first));}
    for(const auto& fact:def.opaqueFacts)facts.opaqueKeys.push_back(Lower(fact.data.key.text));
    data->profile=door::ResolveTrainyardSewerTrapdoorV1(keys,facts);
    data->body=wolf2009::DecodeSourceEntityBoxBody(plan,data->index,wolf2009::SourceBoxBodyProfile::HingedSolidBoxV1);
    data->box=door::HingedBoxLocalBounds(data->body);
    data->localBox=idBounds(idVec3(float(data->box.min[0]),float(data->box.min[1]),float(data->box.min[2])),idVec3(float(data->box.max[0]),float(data->box.max[1]),float(data->box.max[2])));
    data->name=keys.name;data->sourceClass=keys.classname;data->origin.Set(keys.origin[0],keys.origin[1],keys.origin[2]);
    for(const auto& r:keys.receipts){if(!data->keyReceipts.empty())data->keyReceipts+=';';data->keyReceipts+=r;}
    const double origin[3]={keys.origin[0],keys.origin[1],keys.origin[2]};
    data->expectedOpen=door::RotatedWorldBounds(data->box,origin,data->profile.openDegrees,0,0);
    // The PROC submodel registered by Wolf2009_LoadRenderWorld: <proc>#<model>.
    std::string proc=wolf2009::idtech4a::ParseResourcePath(request.exactEntities).Path("PROC",request.sourceMapName)+".proc#"+keys.model;
    data->nativeModel=proc;
    std::unique_ptr<WolfSourceRotatingDoor::Impl> impl(new WolfSourceRotatingDoor::Impl);impl->data=data;
    return std::unique_ptr<WolfSourceRotatingDoor>(new WolfSourceRotatingDoor(std::move(impl)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceRotatingDoor>();}}

bool WolfSourceRotatingDoor::Spawn(std::shared_ptr<WolfNativeSourceRegistry> registry,idStr& reason){reason.Clear();auto& p=*impl;try{
    Need(p.data&&!p.entity.GetEntity()&&registry&&registry->Generation(),"Rotating door assembly state/registry");
    Need(!gameLocal.isMultiplayer&&!gameLocal.isClient&&gameLocal.GameState()==GAMESTATE_ACTIVE,"Rotating door requires an active local single-player game");
    const auto& d=*p.data;Need(!gameLocal.FindEntity(d.name.c_str()),"Rotating door native name is occupied");
    // Visual: the PROC submodel; refuse if absent or if its bounds differ.
    idRenderModel* model=renderModelManager->CheckModel(d.nativeModel.c_str());
    Need(model&&!model->IsDefaultModel(),"SourceRotatingDoorOldV1: PROC submodel alias "+d.nativeModel+" is absent");
    const idBounds mb=model->Bounds(NULL);door::LocalBox modelBox;for(int i=0;i<3;++i){modelBox.min[i]=mb[0][i];modelBox.max[i]=mb[1][i];}
    door::RequireHingedBoxMatchesBounds(d.body,modelBox,.01);
    unsigned triangles=0;for(int i=0;i<model->NumSurfaces();++i){const auto* s=model->Surface(i);if(s&&s->geometry)triangles+=unsigned(s->geometry->numIndexes/3);}
    idDict args;args.Set("classname","wolf2009_source_rotating_door");args.Set("name",d.name.c_str());
    const float o[3]={d.origin.x,d.origin.y,d.origin.z};args.Set("origin",Words(o,3).c_str());args.Set("rotation","1 0 0 0 1 0 0 0 1");
    args.Set("model",d.nativeModel.c_str());
    const float mins[3]={d.localBox[0].x,d.localBox[0].y,d.localBox[0].z},maxs[3]={d.localBox[1].x,d.localBox[1].y,d.localBox[1].z};
    args.Set("mins",Words(mins,3).c_str());args.Set("maxs",Words(maxs,3).c_str());
    args.Set("solid","1");args.Set("nopush","0");args.Set("damage","0");args.Set("move_time","0");args.Set("networkSync","0");args.Set("neverDormant","1");
    std::unique_ptr<idEntity> entity;{SpawnContext context(p.data);entity.reset(gameLocal.SpawnEntityType(idWolfSourceRotatingDoor::Type,&args));}
    auto* native=DoorEntity(entity.get());
    Need(native&&entity->entityNumber>=MAX_CLIENTS&&gameLocal.entities[entity->entityNumber]==entity.get(),"Rotating door native allocation failed");
    idStr why;Need(native->Check(why),why.IsEmpty()?"Rotating door post-Spawn capability changed":why.c_str());
    auto& s=p.initial;s.renderTriangles=triangles;Store(s.modelBounds,mb);
    Store(s.closedBounds,WorldBounds(native->GetPhysics()->GetClipModel(),native->GetPhysics()->GetOrigin(),native->GetPhysics()->GetAxis()));
    s.spawnTimeMs=gameLocal.time;s.levelGeneration=registry->Generation();
    // ParametricPushStandInV1 carries no world clip (push flags 0); portals
    // do not exist in the single-area native render world.
    s.portalReceipt=gameRenderWorld&&gameRenderWorld->FindPortal(native->GetPhysics()->GetAbsBounds())?"portal-present-unbound":"AreaPortalAbsentV1";
    idEntityPtr<idEntity> held;held=entity.release();p.entity=held;
    p.Trace(s.closedTraceFraction,s.closedTraceEntity);
    registry->Bind(d.name,d.sourceClass,native,std::make_shared<OutsideActivation>());p.registry=std::move(registry);
    gameLocal.Printf("WOLF2009_SOURCE_ROTATING_DOOR_SPAWN name=%s entity=%d model=%s triangles=%u closed=(%.2f %.2f %.2f)-(%.2f %.2f %.2f) hole_trace=%.3f/%d portal=%s keys=\"%s\" time=%d\n",
        d.name.c_str(),native->entityNumber,d.nativeModel.c_str(),triangles,s.closedBounds[0],s.closedBounds[1],s.closedBounds[2],s.closedBounds[3],s.closedBounds[4],s.closedBounds[5],
        s.closedTraceFraction,s.closedTraceEntity,s.portalReceipt.c_str(),d.keyReceipts.c_str(),gameLocal.time);
    return true;
}catch(const std::exception& error){reason=error.what();if(auto* e=p.entity.GetEntity())delete e;p.entity=NULL;return false;}}

bool WolfSourceRotatingDoor::DeliverPostSpawn(idStr& reason){reason.Clear();auto& p=*impl;
    auto* native=DoorEntity(p.entity.GetEntity());if(!native)return Refuse(reason,"Rotating door native entity expired");
    if(!p.data->profile.keys.startOpen)return Refuse(reason,"Rotating door has no start_open");
    if(p.postSpawn){++p.duplicatePostSpawn;return Refuse(reason,"DeferredPostSpawnActivateV1 was already delivered for this generation");}
    ++p.postSpawn;
    // rvDoor post-spawn: start_open -> activate processed synchronously with
    // the door itself as activator; no team, no target light.
    return native->SourceActivate(door::Activator::Self,reason);}

bool WolfSourceRotatingDoor::Query(WolfSourceRotatingDoorState& out,idStr& reason)const{reason.Clear();try{
    const auto& p=*impl;const auto* native=DoorEntity(p.entity.GetEntity());Need(native!=NULL,"Rotating door native entity expired");
    idStr why;if(native->failure.empty())Need(native->Check(why),why.IsEmpty()?"Rotating door capability changed":why.c_str());
    WolfSourceRotatingDoorState s=p.initial;const auto& d=*p.data;
    s.profile=Profile;s.name=d.name;s.sourceClass=d.sourceClass;s.nativeModel=d.nativeModel;s.sndOpen=d.profile.sndOpen;s.sndClose=d.profile.sndClose;
    s.triggerClosed=d.profile.keys.triggerClosed;s.doorState=wolf2009::rotatingdoor::DoorStateText(native->doorState);s.failure=native->failure;
    s.soundReceipts=native->soundReceipts;s.keyReceipts=d.keyReceipts;s.contentsProfile="HingedSolidContentsV1";s.pushProfile="ParametricPushStandInV1";
    s.activateRefusal="outside activation is TRAPDOOR T2";s.entityIndex=d.index;
    s.postSpawnDeliveries=p.postSpawn;s.duplicatePostSpawns=p.duplicatePostSpawn;s.activations=native->activations;s.ignoredActivations=native->ignoredActivations;
    s.moveStarts=native->moveStarts;s.openCompletions=native->openCompletions;s.closeCompletions=native->closeCompletions;s.thinkFrames=native->thinkFrames;
    s.openThinkFrames=native->openThinkFrames;s.velocityResets=native->velocityResets;s.genericActivateRefusals=native->genericActivateRefusals;
    s.triggerClosedAbsent=native->triggerClosedAbsent;s.blockedFrames=native->blockedFrames;s.physicsReachedAngEvents=native->reachedAngEvents;
    s.moveStartTimeMs=native->moveStartTimeMs;s.moveEndTimeMs=native->moveEndTimeMs;s.durationMs=native->durationMs;
    s.openCompletionTimeMs=native->openCompletionTimeMs;s.closeCompletionTimeMs=native->closeCompletionTimeMs;
    s.physicsEndTimeMs=native->moveStarts?native->GetPhysics()->IsType(idPhysics_Parametric::Type)?static_cast<const idPhysics_Parametric*>(native->GetPhysics())->GetAngularEndTime():-1:-1;
    const idVec3 at=native->GetPhysics()->GetOrigin();for(int i=0;i<3;++i)s.origin[i]=at[i];
    idAngles a;static_cast<const idPhysics_Parametric*>(native->GetPhysics())->GetLocalAngles(a);a.Normalize180();s.localAngles[0]=a.pitch;s.localAngles[1]=a.yaw;s.localAngles[2]=a.roll;
    s.target=native->target;s.delta=native->delta;s.speed=native->speedValue;
    Store(s.bounds,WorldBounds(native->GetPhysics()->GetClipModel(),native->GetPhysics()->GetOrigin(),native->GetPhysics()->GetAxis()));
    for(int i=0;i<3;++i){s.expectedOpenBounds[i]=float(d.expectedOpen.min[i]);s.expectedOpenBounds[3+i]=float(d.expectedOpen.max[i]);}
    p.Trace(s.traceFraction,s.traceEntity);
    s.entityNumber=native->entityNumber;s.contents=native->GetPhysics()->GetContents();s.clipMask=native->GetPhysics()->GetClipMask();
    s.spawned=true;s.thinking=(native->thinkFlags&TH_THINK)!=0;s.physicsActive=(native->thinkFlags&TH_PHYSICS)!=0;
    s.physicsAtRest=native->GetPhysics()->IsAtRest();s.pusher=static_cast<const idPhysics_Parametric*>(native->GetPhysics())->IsPusher();
    s.clipIsTraceModel=native->GetPhysics()->GetClipModel()->IsTraceModel();
    out=s;return true;
}catch(const std::exception& error){reason=error.what();return false;}}

int Wolf2009_LiveSourceRotatingDoors(){return liveDoors;}

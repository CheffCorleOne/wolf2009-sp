// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
// Standard headers used by the source owners must precede idlib's printf guards.
#include <array>
#include <functional>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "NativeSourceGreeterRoute.h"
#include "NativeSourceAnimatedDoor.h"
#include "NativeSourceDirector.h"
#include "NativeSourceMover.h"
#include "NativeSourceRegistry.h"
#include "NativeSourceScriptedActor.h"
#include "SourceControlledEntity.h"
#include "SourceDeclarationSetRuntime.h"
#include "AnimationWireV2.h"
#include "RenderData.h"

namespace {
namespace actor=wolf2009::actor;
namespace render=wolf2009::idtech4a;
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){if(!ok)throw std::runtime_error(reason.IsEmpty()?fallback:reason.c_str());}
bool Refuse(idStr& reason,const char* why){reason=why;return false;}
// Measured trainyard profile facts. Each is rechecked against the source data
// or by the selected actor/door owners; none selects a resource by search.
const char* const Profile="MeasuredTrainyardGreeterRouteV1";
const char* const GreeterBrain="brains/trainyard/train_greeter";
const char* const RelayName="train_greeter_brainuse_1";
const char* const MapModelDefinition="model_trainyard_base";
const char* const ActorClip="models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx";
const char* const ActorClipSkeleton="models/characters/_humanoid_male/humanoid_male.hkx";
const char* const DoorSkeleton="models/props/train_door/train_door_anim_skel.hkx";
const char* const DoorModel="models/props/train_door/train_door_anim.md5r";
const char* const DoorLogical[3]={"idle","door_move","end_idle"};
const char* const DoorClips[3]={"models/props/train_door/train_door_pose.hkx","models/props/train_door/srp_tyd_nml_train_door_anim.hkx","models/props/train_door/train_door_end.hkx"};
struct Close {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
std::vector<unsigned char> ReadExact(const std::string& path){std::unique_ptr<idFile,Close> file(fileSystem->OpenFileRead(path.c_str(),false));
    Need(file&&file->Tell()==0,"Greeter route exact source is absent");const int length=file->Length();Need(length>0&&length<=64*1024*1024,"Greeter route source byte budget");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));std::size_t at=0;
    while(at<bytes.size()){const int n=file->Read(bytes.data()+at,int(bytes.size()-at));Need(n>0&&std::size_t(n)<=bytes.size()-at,"Greeter route short source read");at+=std::size_t(n);}
    unsigned char extra=0;Need(file->Read(&extra,1)==0&&file->Tell()==length,"Greeter route source EOF/length changed");return bytes;}
actor::ExpectedSource Identity(const std::vector<unsigned char>& bytes){actor::ExpectedSource out;out.byteIdentityKnown=true;out.bytes=bytes.size();
    out.fingerprint=wolf2009::animation2::Fingerprint(bytes.data(),bytes.size());return out;}
std::string Instance(const wolf2009::EntityRecord& source,const char* key){const auto* value=source.Find(key);
    Need(value&&value->kind==wolf2009::EntityProperty::Text&&!value->text.empty()&&value->text.size()<=1024,"Greeter route selected ENTS text is absent/unqualified");return value->text;}
std::string Literal(const wolf2009::ResolvedSourceDefinition& source,const char* key){const auto* value=source.Find(key);
    Need(value&&(value->encoding==wolf2009::SourceLiteralEncoding::CompiledString||value->encoding==wolf2009::SourceLiteralEncoding::PlainText)&&value->value.size()==1&&!value->value[0].text.empty(),
        "Greeter route selected source field is not one literal");return value->value[0].text;}
std::size_t Unique(const wolf2009::EntityDocument& document,const std::string& name){std::size_t found=0,matches=0;
    for(std::size_t i=0;i<document.entities.size();++i){const auto* value=document.entities[i].Find("name");
        if(value&&value->kind==wolf2009::EntityProperty::Text&&value->text==name){found=i;++matches;}}
    Need(matches==1,"Greeter route source name is absent or ambiguous");return found;}
int liveRoutes=0;
class RouteThinkHost {
  public:
    virtual ~RouteThinkHost(){}
    virtual void Tick()=0;
    virtual bool Active()const=0;
};
const std::shared_ptr<RouteThinkHost>* pendingThink=NULL;
struct ThinkContext {
    explicit ThinkContext(const std::shared_ptr<RouteThinkHost>& value){Need(!pendingThink,"Recursive greeter route Spawn");pendingThink=&value;}
    ~ThinkContext(){pendingThink=NULL;}
};
}

const char* Wolf2009_SourceGreeterRoutePhaseText(WolfSourceGreeterRoutePhase value){switch(value){
    case WolfSourceGreeterRoutePhase::Prepared:return "prepared";case WolfSourceGreeterRoutePhase::WaitingForRelay:return "waiting-for-relay";
    case WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness:return "waiting-for-type19-readiness";case WolfSourceGreeterRoutePhase::DoorMoving:return "door-moving";
    case WolfSourceGreeterRoutePhase::Completed:return "completed";case WolfSourceGreeterRoutePhase::Refused:return "refused";
    case WolfSourceGreeterRoutePhase::Cancelled:return "cancelled";case WolfSourceGreeterRoutePhase::Failed:return "failed";}return "unknown";}

struct WolfSourceGreeterRoute::Impl : RouteThinkHost {
    const WolfSourceDeclarationSet* set=NULL;
    WolfSourceGreeterRouteRequest request;
    wolf2009::TrainDoorSignalPlan plan;
    WolfSourceScriptedActorRequest actorRequest;
    WolfSourceAnimatedDoorRequest doorRequest;
    std::string wagonName;
    std::shared_ptr<WolfNativeSourceRegistry> registry;
    std::unique_ptr<wolf2009::TrainDoorSignalRuntime> signal;
    std::unique_ptr<WolfSourceScriptedActor> actor;
    std::unique_ptr<WolfSourceAnimatedDoor> door;
    std::shared_ptr<WolfScriptedVoiceReadiness> voice;
    WolfSourceDirector* director=NULL;
    idEntityPtr<idEntity> wagon,nativeThink;
    wolf2009::TrainDoorCommand voiceCommand;
    std::uint64_t ticket=0;bool ticketLive=false,busy=false;
    WolfSourceGreeterRouteState state;
    Impl(){++liveRoutes;}
    ~Impl(){ReleaseTicket(false);door.reset();actor.reset();--liveRoutes;}
    void ReleaseTicket(bool counted){if(ticketLive&&voice){voice->Release(ticket);ticketLive=false;if(counted)++state.voiceReleases;}}
    bool Active()const override{return state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay||state.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness||
        state.phase==WolfSourceGreeterRoutePhase::DoorMoving;}
    // Owned native lifetimes: exact names, captured spawn generations and
    // each owner's own preflight. Never rebinds a same-name replacement.
    void Owners()const{
        idStr reason;Need(actor&&door,"Greeter route actor/door owners are absent");
        if(!actor->Preflight(reason))throw std::runtime_error(std::string("Greeter route greeter actor owner refused: ")+reason.c_str());
        auto* greeter=actor->NativeEntity();Need(greeter&&unsigned(gameLocal.GetSpawnId(greeter))==state.greeterSpawnId&&gameLocal.FindEntity(plan.ownerEntityName.c_str())==greeter,
            "Greeter route native greeter generation expired or its exact name resolves elsewhere");
        WolfSourceAnimatedDoorState current;if(!door->Query(current,reason))throw std::runtime_error(std::string("Greeter route door owner refused: ")+reason.c_str());
        auto* child=door->Entity();Need(child&&unsigned(gameLocal.GetSpawnId(child))==state.doorSpawnId&&gameLocal.FindEntity(plan.targetEntityName.c_str())==child,
            "Greeter route native door generation expired or its exact name resolves elsewhere");
        auto* parent=wagon.GetEntity();Need(parent&&unsigned(wagon.GetSpawnId())==state.wagonSpawnId&&registry&&registry->NativeEntity(wagonName)==parent&&child->GetBindMaster()==parent,
            "Greeter route source wagon/door binding expired");
    }
    bool Admission(const wolf2009::TrainDoorUseEvent& event,idEntity* originalActor,idStr& reason)const{reason.Clear();try{
        Need(state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay&&signal&&signal->Status()==wolf2009::TrainDoorSignalStatus::AwaitingUse,"Greeter route OnUsed was already accepted or the route is not waiting");
        Need(registry&&event.levelGeneration==registry->Generation()&&event.levelGeneration==state.levelGeneration&&event.eventId!=0,"Greeter route relay event generation/identity differs");
        Need(event.sourceEntityName==plan.signalEntityName,"Greeter route relay source differs from the qualified target_brainuse");
        Need(event.ownerEntityName==plan.ownerEntityName,"Greeter route relay target is not the exact source greeter");
        // Exact own relay key; the incoming upstream key is not folded into it.
        Need(event.useKey==plan.useKey&&event.useKey==state.forwardedUseKey,"Greeter route relay key differs from the greeter's exact OnUsed key");
        Need(registry->NativeEntity(plan.signalEntityName)&&gameLocal.FindEntity(plan.signalEntityName.c_str())==registry->NativeEntity(plan.signalEntityName),"Greeter route native relay generation expired");
        Need(originalActor&&originalActor==wagon.GetEntity(),"Greeter route original activator is not the captured source wagon");
        Owners();
        WolfSourceAnimatedDoorState current;idStr why;NeedReason(door->Query(current,why),why,"Greeter route door query failed");
        Need(current.phase==WolfSourceAnimatedDoor_Idle&&!current.activations,"Greeter route door is not idle before OnUsed");
        WolfSourceActorStartBinding binding;NeedReason(actor->StartBinding(binding,why),why,"Greeter route actor authority unavailable");
        WolfSourceActorStartEvidence proof;NeedReason(binding.query(binding.context,proof,why),why,"Greeter route actor evidence unavailable");
        Need(!proof.committed&&proof.nativeSpawnId==state.greeterSpawnId,"Greeter route actor was started before its OnUsed");
        return true;
    }catch(const std::exception& error){reason=error.what();return false;}}
    void Fail(WolfSourceGreeterRoutePhase phase,const std::string& why){ReleaseTicket(true);state.phase=phase;state.reason=why;
        gameLocal.Warning("WOLF2009_SOURCE_GREETER_ROUTE_STOP phase=%s reason=%s",Wolf2009_SourceGreeterRoutePhaseText(phase),why.c_str());}
    void DeliverRelay(){
        wolf2009::TrainDoorUseEvent event;idEntity* original=NULL;bool pending=false;idStr why;
        if(!director->TakeRelayRequest(event,original,pending,why)){Fail(WolfSourceGreeterRoutePhase::Failed,why.IsEmpty()?"Greeter route relay generation expired":why.c_str());return;}
        if(!pending)return;
        ++state.relayTakes;state.relayTimeMs=gameLocal.time;state.relayEventId=event.eventId;
        if(!Admission(event,original,why)){++state.refusals;Fail(WolfSourceGreeterRoutePhase::Refused,why.c_str());return;}
        const auto result=signal->OnUsed(event);state.signal=signal->Status();
        Need(result.status==wolf2009::TrainDoorSignalStatus::VoicePreparationRequested&&result.command.kind==wolf2009::TrainDoorCommand::PrepareVoiceResources&&
            result.command.sourceNodeId==plan.voiceNodeId&&result.command.resourceName==plan.voiceDefinition&&result.command.eventId==event.eventId,
            "Greeter OnUsed did not enter its exact type19 node");
        voiceCommand=result.command;++state.onUsedDeliveries;state.onUsedTimeMs=gameLocal.time;state.onUsedKey=event.useKey;
        WolfScriptedVoiceReadinessRequest ask;ask.definition=plan.voiceDefinition;ask.ownerName=plan.ownerEntityName;ask.brainResource=plan.brainResource;ask.locale=request.voiceLocale;
        ask.levelGeneration=event.levelGeneration;ask.eventId=event.eventId;ask.sourceNodeId=plan.voiceNodeId;ask.sourceNodeType=19;ask.ownerSpawnId=state.greeterSpawnId;
        std::string reason;std::uint64_t value=0;const auto status=voice->Request(ask,value,reason);++state.voiceRequests;state.voiceRequestTimeMs=gameLocal.time;
        state.voice=status;state.voiceReason=reason;if(value){ticket=value;ticketLive=true;}
        state.phase=WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROUTE_ONUSED generation=%llu event=%llu relay=%s target=%s incoming_key=%s forwarded_key=%s onused_node=%u type19_node=%u definition=%s readiness=%s reason=\"%s\" actor_start_ack=0 door_activation=0\n",
            static_cast<unsigned long long>(event.levelGeneration),static_cast<unsigned long long>(event.eventId),event.sourceEntityName.c_str(),event.ownerEntityName.c_str(),
            state.incomingUseKey.c_str(),event.useKey.c_str(),plan.onUsedNodeId,plan.voiceNodeId,plan.voiceDefinition.c_str(),Wolf2009_ScriptedVoiceReadinessText(status),reason.c_str());
        if(status==WolfScriptedVoiceReadinessStatus::Failed||status==WolfScriptedVoiceReadinessStatus::Lost||status==WolfScriptedVoiceReadinessStatus::Cancelled)
            Fail(WolfSourceGreeterRoutePhase::Failed,"Type19 readiness request failed: "+reason);
    }
    // Reached only after the provider observed whole-bank completion.
    void ContinueAfterReadiness(){
        state.postReadinessExecuted=true;
        const auto started=signal->VoiceResourcesReady(voiceCommand,true);state.signal=signal->Status();
        Need(started.status==wolf2009::TrainDoorSignalStatus::ActorStartRequested&&started.command.kind==wolf2009::TrainDoorCommand::StartActorAnimation&&
            started.command.sourceNodeId==plan.actorStartNodeId&&started.command.resourceName==plan.actorAnimation,"Greeter type19 readiness did not request the exact actor start node");
        WolfSourceScriptedActorStartAck ack;idStr why;NeedReason(actor->Start(started.command,ack,why),why,"Greeter actor owner Start failed");
        Need(ack.committed&&!ack.duplicate&&ack.actualPoseEvaluated&&ack.committedTimeMs==gameLocal.time&&ack.nativeSpawnId==state.greeterSpawnId,"Greeter actor owner did not issue a same-tick actual start ACK");
        ++state.actorStarts;state.actorStartTimeMs=ack.committedTimeMs;
        const auto activation=signal->ActorStartCommitted(started.command,ack.committed);state.signal=signal->Status();
        Need(activation.status==wolf2009::TrainDoorSignalStatus::TargetActivationRequested&&activation.command.kind==wolf2009::TrainDoorCommand::ActivateTarget&&
            activation.command.targetEntityName==plan.targetEntityName,"Greeter actor start ACK did not request the exact door target");
        NeedReason(door->Activate(activation.command,why),why,"Bound door refused the actor-issued activation");
        WolfSourceAnimatedDoorState current;NeedReason(door->Query(current,why),why,"Greeter route door query failed after activation");
        Need(current.phase==WolfSourceAnimatedDoor_Moving&&current.activations==1&&current.activationTimeMs==state.actorStartTimeMs,"Bound door activation is not the actor's same-tick start");
        ++state.doorActivations;state.doorActivationTimeMs=current.activationTimeMs;state.phase=WolfSourceGreeterRoutePhase::DoorMoving;
    }
    void ObserveDoor(){
        idStr why;NeedReason(door->Tick(why),why,"Bound door Tick failed");WolfSourceAnimatedDoorState current;NeedReason(door->Query(current,why),why,"Bound door query failed");
        if(current.phase!=WolfSourceAnimatedDoor_Completed)return;
        WolfSourceAnimatedDoorCompletion done;bool pending=false;NeedReason(door->TakeCompletion(pending,done,why),why,"Bound door completion read failed");
        Need(pending&&current.completions==1&&done.entityName==plan.targetEntityName&&done.levelGeneration==state.levelGeneration&&done.eventId==state.relayEventId&&done.endClip=="end_idle",
            "Bound door completion identity differs or was already consumed");
        ++state.doorCompletions;state.doorCompletionTimeMs=done.observedTimeMs;ReleaseTicket(true);state.phase=WolfSourceGreeterRoutePhase::Completed;
    }
    void Tick()override{
        if(!Active())return;
        try{Need(!busy,"Greeter route Think reentry");struct Scope{bool& b;explicit Scope(bool& v):b(v){b=true;}~Scope(){b=false;}}scope(busy);
            ++state.thinkFrames;
            if(state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay){DeliverRelay();return;}
            Owners();
            if(state.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness){
                std::string reason;const auto status=ticketLive?voice->Query(ticket,reason):state.voice;++state.voiceQueries;
                if(ticketLive){state.voice=status;state.voiceReason=reason;}
                if(status==WolfScriptedVoiceReadinessStatus::Pending||status==WolfScriptedVoiceReadinessStatus::Unsupported)return;
                if(status!=WolfScriptedVoiceReadinessStatus::Ready){Fail(WolfSourceGreeterRoutePhase::Failed,std::string("Type19 readiness ended without Ready: ")+reason);return;}
                ContinueAfterReadiness();return;
            }
            if(state.phase==WolfSourceGreeterRoutePhase::DoorMoving)ObserveDoor();
        }catch(const std::exception& error){Fail(WolfSourceGreeterRoutePhase::Failed,error.what());}
    }
};

// Weak host reference only; the map owner deletes this entity before the
// route, door, actor, director and wagon owners.
class idWolfSourceGreeterRouteThink : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceGreeterRouteThink);
    idWolfSourceGreeterRouteThink(){Need(pendingThink&&bool(*pendingThink),"Greeter route Think requires its map owner");host=*pendingThink;}
    ~idWolfSourceGreeterRouteThink(){host.reset();}
    void Spawn(){fl.networkSync=false;fl.neverDormant=true;cinematic=true;BecomeActive(TH_THINK);}
    void Think()override{const auto owner=host.lock();if(!owner){BecomeInactive(TH_THINK);return;}owner->Tick();if(!owner->Active())BecomeInactive(TH_THINK);}
    void Save(idSaveGame*)const{gameLocal.Error("Greeter route save state is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Greeter route restore is unimplemented");}
  private:std::weak_ptr<RouteThinkHost> host;
};
CLASS_DECLARATION(idEntity,idWolfSourceGreeterRouteThink)
END_CLASS

WolfSourceGreeterRoute::WolfSourceGreeterRoute(std::shared_ptr<Impl> value):impl(std::move(value)){}
WolfSourceGreeterRoute::~WolfSourceGreeterRoute(){if(auto* entity=impl->nativeThink.GetEntity())delete entity;impl->nativeThink=NULL;impl->ReleaseTicket(false);impl->door.reset();impl->actor.reset();}
WolfSourceScriptedActor* WolfSourceGreeterRoute::Actor()const{return impl->actor.get();}
WolfSourceAnimatedDoor* WolfSourceGreeterRoute::Door()const{return impl->door.get();}

std::unique_ptr<WolfSourceGreeterRoute> Wolf2009_PrepareSourceGreeterRoute(const WolfSourceDeclarationSet& set,const WolfSourceGreeterRouteRequest& request,idStr& reason){reason.Clear();try{
    Need(request.profile==Profile&&request.sourceMapName=="maps/game/trainyard/trainyard"&&!request.voiceLocale.empty()&&request.voiceLocale.size()<=64,"Explicit measured greeter route profile required");
    std::shared_ptr<WolfSourceGreeterRoute::Impl> out(new WolfSourceGreeterRoute::Impl);out->set=&set;out->request=request;
    const auto entities=render::ParseResourcePath(request.exactEntities);Need(entities.type=="ents"&&entities.name==request.sourceMapName,"Greeter route ENTS path differs from the source map");
    const auto prefix=entities.Prefix();const auto entsBytes=ReadExact(request.exactEntities);const auto document=wolf2009::DecodeEntities4(entsBytes);
    // The decoder qualifies all folded ENTS names as unique, the relay's own
    // key/target and the exact OnUsed -> type19 -> type59 chain.
    out->plan=wolf2009::DecodeTrainDoorSignalH1023(ReadExact(prefix+"brai/"+GreeterBrain),GreeterBrain,RelayName,document);
    auto& plan=out->plan;
    Need(plan.ownerEntityName=="train_greeter"&&plan.ownerClassname=="trainyard_greeter"&&plan.useKey=="train_stop"&&plan.onUsedNodeId==3&&plan.voiceNodeId==29&&plan.actorStartNodeId==35&&
        plan.voiceDefinition=="scripted_voice_kc1_trn"&&plan.actorAnimation=="srp_tyd_nml_train_door_open"&&plan.targetEntityName=="train_door_1"&&plan.targetClassname=="env_train_door",
        "Greeter route decoded a different original relay/OnUsed/type19/actor/door chain");
    const auto greeterIndex=Unique(document,plan.ownerEntityName),doorIndex=Unique(document,plan.targetEntityName);Unique(document,RelayName);
    const auto& greeter=document.entities[greeterIndex];const auto& doorSource=document.entities[doorIndex];
    Need(Instance(greeter,"triggerStartMoveEnt")==plan.targetEntityName&&Instance(greeter,"anim")==plan.actorAnimation,"Greeter route greeter target/animation fields differ");
    out->wagonName=Instance(doorSource,"bind");Unique(document,out->wagonName);Need(out->wagonName=="starting_boxcar_model","Greeter route door is not bound to the measured source wagon");
    std::size_t worlds=0;std::string mapDefinition;for(const auto& entity:document.entities){const auto* name=entity.Find("classname");
        if(name&&name->kind==wolf2009::EntityProperty::Text&&name->text=="worldspawn"){++worlds;mapDefinition=Instance(entity,"mapSpecificDef");}}
    Need(worlds==1&&mapDefinition==MapModelDefinition,"Greeter route worldspawn map model context differs");
    wolf2009::SourceDefinitionContext context;context.mapModelDefinition=mapDefinition;
    const auto entityIdentity=Identity(entsBytes);
    auto& a=out->actorRequest;a.policy=WolfSourceScriptedActorPolicy::NativeFullPoseNoExtractionNoFrameCommandsV1;a.exactEntitiesPath=request.exactEntities;a.materialArchive=request.materialArchive;
    a.entityIndex=unsigned(greeterIndex);a.expectedJoints=52;a.expectedFrames=182;a.initialSourceFrame=0;a.expectedEntitiesBytes=entityIdentity.bytes;a.expectedEntitiesFingerprint=entityIdentity.fingerprint;
    a.signal=plan;a.definitionContext=context;
    const auto entity=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,plan.ownerClassname,context);
    const auto model=set.Definitions().ResolveModelAnimations(Literal(entity,"model"),context);
    auto& resources=a.resources;resources.modelResource=request.greeterModel;resources.meshSkeletonResource=request.greeterRig;resources.meshSkeletonName=Literal(model.model,"skeleton");
    Need(render::ParseResourcePath(request.greeterModel).type=="modl"&&render::ParseResourcePath(request.greeterRig).type=="skel","Greeter route explicit greeter MODL/SKEL resources required");
    resources.geometryProfile=actor::GeometryProfile::Weighted64;resources.lodPolicy=actor::LodPolicy::Authored;resources.levelOfDetail=0;
    resources.expected[0]=Identity(ReadExact(request.greeterModel));resources.expected[1]=Identity(ReadExact(request.greeterRig));
    const auto animationPath=prefix+"hkxa/"+request.sourceMapName;const auto animationIdentity=Identity(ReadExact(animationPath));
    actor::ClipSelection clip;clip.logicalName=plan.actorAnimation;clip.animationResource=animationPath;clip.animationSkeletonResource=request.greeterRig;clip.clipName=ActorClip;
    clip.animationSkeletonName=ActorClipSkeleton;clip.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;clip.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;
    clip.expected[0]=animationIdentity;clip.expected[1]=resources.expected[1];resources.clips.push_back(clip);
    auto& d=out->doorRequest;d.policy=WolfSourceAnimatedDoor_NativeControlledBoundSequenceV1;d.exactEntitiesPath=request.exactEntities;d.entityIndex=doorIndex;d.context=context;d.signal=plan;
    d.expectedEntitiesIdentityKnown=true;d.expectedEntitiesBytes=entityIdentity.bytes;d.expectedEntitiesFingerprint=entityIdentity.fingerprint;
    auto& doorResources=d.resources;doorResources.modelResource=prefix+"modl/"+DoorModel;doorResources.meshSkeletonResource=prefix+"skel/"+request.sourceMapName;doorResources.meshSkeletonName=DoorSkeleton;
    doorResources.geometryProfile=actor::GeometryProfile::WeightedOrRigid64;doorResources.lodPolicy=actor::LodPolicy::SingleResourceWithoutLodRecords;doorResources.levelOfDetail=0;
    doorResources.expected[0]=Identity(ReadExact(doorResources.modelResource));doorResources.expected[1]=Identity(ReadExact(doorResources.meshSkeletonResource));
    for(unsigned i=0;i<3;++i){actor::ClipSelection value;value.logicalName=DoorLogical[i];value.animationResource=animationPath;value.animationSkeletonResource=doorResources.meshSkeletonResource;
        value.clipName=DoorClips[i];value.animationSkeletonName=DoorSkeleton;value.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;
        value.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;value.expected[0]=animationIdentity;value.expected[1]=doorResources.expected[1];doorResources.clips.push_back(value);}
    std::string why;out->voice=Wolf2009_CreateScriptedVoiceReadiness(request.voiceReadinessProfile,why);Need(bool(out->voice),why.c_str());
    auto& state=out->state;state.relayName=plan.signalEntityName;state.greeterName=plan.ownerEntityName;state.doorName=plan.targetEntityName;state.forwardedUseKey=plan.useKey;
    state.onUsedNode=plan.onUsedNodeId;state.voiceNode=plan.voiceNodeId;state.actorStartNode=plan.actorStartNodeId;state.voiceDefinition=plan.voiceDefinition;
    state.actorAnimation=plan.actorAnimation;state.voiceProfile=out->voice->Profile();
    return std::unique_ptr<WolfSourceGreeterRoute>(new WolfSourceGreeterRoute(std::move(out)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceGreeterRoute>();}}

bool WolfSourceGreeterRoute::Spawn(std::shared_ptr<WolfNativeSourceRegistry> registry,idEntity* wagon,idStr& reason){reason.Clear();auto& p=*impl;try{
    Need(p.state.phase==WolfSourceGreeterRoutePhase::Prepared&&!p.actor&&!p.door&&registry&&registry->Generation(),"Greeter route assembly state/registry");
    Need(!gameLocal.isMultiplayer&&!gameLocal.isClient&&gameLocal.GameState()==GAMESTATE_ACTIVE,"Greeter route requires an active local single-player game");
    Need(wagon&&registry->NativeEntity(p.wagonName)==wagon&&p.wagonName==wagon->GetName()&&Wolf2009_IsSourceMover(wagon),"Greeter route requires the registered source wagon");
    WolfSourceMoverState mover;NeedReason(Wolf2009_QuerySourceMover(wagon,mover,reason),reason,"Greeter route wagon query failed");
    Need(mover.spawnDeliveries==0&&mover.arrivals==0,"Greeter route assembly must precede wagon OnSpawn");
    const auto generation=registry->Generation();p.actorRequest.levelGeneration=generation;p.doorRequest.levelGeneration=generation;
    p.signal.reset(new wolf2009::TrainDoorSignalRuntime(p.plan,generation));
    p.actor=Wolf2009_PrepareSourceScriptedActor(*p.set,p.actorRequest,reason);NeedReason(bool(p.actor),reason,"Greeter route actor preparation failed");
    NeedReason(p.actor->SpawnInactive(reason),reason,"Greeter route inactive actor Spawn failed");
    NeedReason(p.actor->StartBinding(p.doorRequest.actorStart,reason),reason,"Greeter route actor start authority unavailable");
    p.door=Wolf2009_PrepareSourceAnimatedDoor(p.doorRequest,*p.set,reason);NeedReason(bool(p.door),reason,"Greeter route door preparation failed");
    NeedReason(p.door->Spawn(wagon,reason),reason,"Greeter route bound door Spawn failed");
    p.registry=std::move(registry);p.wagon=wagon;p.state.levelGeneration=generation;p.state.wagonSpawnId=unsigned(p.wagon.GetSpawnId());
    p.state.greeterSpawnId=unsigned(gameLocal.GetSpawnId(p.actor->NativeEntity()));p.state.doorSpawnId=unsigned(gameLocal.GetSpawnId(p.door->Entity()));
    p.state.actorSpawned=p.state.doorBound=true;p.Owners();
    gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROUTE_ASSEMBLED generation=%llu greeter=%s greeter_entity=%d door=%s door_entity=%d bind=%s actor_held=1 door_idle=1 readiness_profile=%s\n",
        static_cast<unsigned long long>(generation),p.plan.ownerEntityName.c_str(),p.actor->NativeEntity()->entityNumber,p.plan.targetEntityName.c_str(),p.door->Entity()->entityNumber,
        p.wagonName.c_str(),p.state.voiceProfile.c_str());
    return true;
}catch(const std::exception& error){reason=error.what();p.door.reset();p.actor.reset();p.signal.reset();p.state.actorSpawned=p.state.doorBound=false;return false;}}

bool WolfSourceGreeterRoute::Start(WolfSourceDirector* director,idStr& reason){reason.Clear();auto& p=*impl;try{
    Need(p.state.phase==WolfSourceGreeterRoutePhase::Prepared&&p.actor&&p.door&&director&&!p.nativeThink.GetEntity(),"Greeter route start is not assembled");
    WolfSourceDirectorState current;NeedReason(director->Query(current,reason),reason,"Greeter route director is unavailable");
    Need(current.phase==WolfSourceDirectorPhase::WaitingForArrival&&current.levelGeneration==p.state.levelGeneration&&current.authoredRelayUseKey==p.plan.useKey,
        "Greeter route director generation/relay key differs");
    p.state.incomingUseKey=current.rawIncomingUseKey;p.Owners();
    const std::string name="wolf2009_source_greeter_route_"+std::to_string(p.state.levelGeneration);Need(!gameLocal.FindEntity(name.c_str()),"Greeter route Think name is occupied");
    idDict args;args.Set("classname","wolf2009_source_greeter_route");args.Set("name",name.c_str());args.Set("noclipmodel","1");args.Set("networkSync","0");
    const std::shared_ptr<RouteThinkHost> host=impl;ThinkContext context(host);
    std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourceGreeterRouteThink::Type,&args));
    Need(entity&&entity->GetType()==&idWolfSourceGreeterRouteThink::Type,"Greeter route Think allocation failed");
    p.director=director;p.nativeThink=entity.release();p.state.phase=WolfSourceGreeterRoutePhase::WaitingForRelay;return true;
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceGreeterRoute::Query(WolfSourceGreeterRouteState& out,idStr& reason)const{reason.Clear();try{
    const auto& p=*impl;
    if(p.state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay||p.state.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness||p.state.phase==WolfSourceGreeterRoutePhase::DoorMoving)
        Need(p.nativeThink.GetEntity()!=NULL,"Greeter route Think lifetime expired");
    if(p.state.actorSpawned&&p.state.phase!=WolfSourceGreeterRoutePhase::Failed&&p.state.phase!=WolfSourceGreeterRoutePhase::Refused)p.Owners();
    out=p.state;return true;
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceGreeterRoute::Inspect(const wolf2009::TrainDoorUseEvent& event,idEntity* originalActor,idStr& reason)const{return impl->Admission(event,originalActor,reason);}

bool WolfSourceGreeterRoute::Cancel(idStr& reason){reason.Clear();auto& p=*impl;
    if(p.busy)return Refuse(reason,"Greeter route Cancel during Think");
    if(p.state.phase==WolfSourceGreeterRoutePhase::Cancelled)return true;
    if(p.state.phase!=WolfSourceGreeterRoutePhase::WaitingForRelay&&p.state.phase!=WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness)
        return Refuse(reason,"Greeter route can only be cancelled before door activation");
    p.ReleaseTicket(true);++p.state.cancellations;p.state.phase=WolfSourceGreeterRoutePhase::Cancelled;p.state.reason="Cancelled by its map owner";
    if(p.state.voiceRequests)p.state.voice=WolfScriptedVoiceReadinessStatus::Cancelled;
    if(auto* think=p.nativeThink.GetEntity())think->BecomeInactive(TH_THINK);return true;}

int Wolf2009_LiveSourceGreeterRoutes(){return liveRoutes;}

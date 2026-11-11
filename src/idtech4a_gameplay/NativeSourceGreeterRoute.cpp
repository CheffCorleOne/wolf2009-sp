// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
// Standard headers used by the source owners must precede idlib's printf guards.
#include <algorithm>
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
#include "TrainyardExit.h"
#include "BrainFlow.h"
#include "BrainTask.h"
#include "SourceAIScriptedMove.h"
#include "NativeSourceScriptedMove.h"
#include "NativeSourceAIShell.h"
#include "SourceRootMotion.h"
#include "SplineClipData.h"

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
// One numeric field of a resolved compiled entityDef: signed integer, float32
// bits or decimal text; absent returns the fallback, other encodings refuse.
double Number(const wolf2009::ResolvedSourceDefinition& d,const char* key,double fallback){
    const auto* f=d.Find(key);if(!f)return fallback;
    if(f->encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&f->numericWords.size()==1)return double(std::int32_t(f->numericWords[0]));
    if(f->encoding==wolf2009::SourceLiteralEncoding::CompiledFloat32&&f->numericWords.size()==1){std::uint32_t w=f->numericWords[0];float v;std::memcpy(&v,&w,4);
        if(!std::isfinite(v))throw std::runtime_error("Greeter route numeric field is not finite");return v;}
    if((f->encoding==wolf2009::SourceLiteralEncoding::CompiledString||f->encoding==wolf2009::SourceLiteralEncoding::PlainText)&&f->value.size()==1){
        char* end=NULL;const double v=std::strtod(f->value[0].text.c_str(),&end);if(!end||*end||!std::isfinite(v))throw std::runtime_error("Greeter route numeric text field differs");return v;}
    throw std::runtime_error("Greeter route numeric field has an unqualified encoding");}
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
    case WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness:return "waiting-for-type19-readiness";case WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode:return "held-at-unsupported-node";
    case WolfSourceGreeterRoutePhase::Refused:return "refused";case WolfSourceGreeterRoutePhase::ScriptedMoveActive:return "scripted-move-active";
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
    std::unique_ptr<WolfScriptedVoiceReadinessRequest> voice;
    WolfSourceDirector* director=NULL;
    idEntityPtr<idEntity> wagon,nativeThink;
    wolf2009::TrainDoorCommand voiceCommand;
    // Greeter OnUsed roots for the idAI receiver's CanUseKey gate.
    std::vector<wolf2009::SourceOnUsedRoot> onUsedRoots;
    bool busy=false;
    WolfSourceGreeterRouteState state;
    Impl(){++liveRoutes;}
    ~Impl(){ReleaseTicket(false);if(auto* goal=goalTarget.GetEntity())delete goal;goalTarget=NULL;door.reset();actor.reset();--liveRoutes;}
    // Ends this route's type19 interest; Cancel first so a provider never sees
    // an abandoned live request become Ready.
    void ReleaseTicket(bool counted){if(voice){voice->Cancel("greeter route released its type19 request");voice.reset();if(counted)++state.voiceReleases;}}
    bool Active()const override{return state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay||state.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness||
        state.phase==WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode||state.phase==WolfSourceGreeterRoutePhase::ScriptedMoveActive;}
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
        OpenVoice(event);}
    void OpenVoice(const wolf2009::TrainDoorUseEvent& event){
        const auto result=signal->OnUsed(event);state.signal=signal->Status();
        Need(result.status==wolf2009::TrainDoorSignalStatus::VoicePreparationRequested&&result.command.kind==wolf2009::TrainDoorCommand::PrepareVoiceResources&&
            result.command.sourceNodeId==plan.voiceNodeId&&result.command.resourceName==plan.voiceDefinition&&result.command.eventId==event.eventId,
            "Greeter OnUsed did not enter its exact type19 node");
        voiceCommand=result.command;++state.onUsedDeliveries;state.onUsedTimeMs=gameLocal.time;state.onUsedKey=event.useKey;
        WolfScriptedVoiceReadinessSource ask;ask.definition=plan.voiceDefinition;ask.brainResource=plan.brainResource;ask.locale=request.voiceLocale;
        ask.levelGeneration=event.levelGeneration;ask.eventId=event.eventId;ask.nodeId=plan.voiceNodeId;ask.nodeType=19;
        std::string reason;voice=Wolf2009_RequestScriptedVoiceReadiness(request.voiceReadinessProfile,ask,actor->NativeEntity(),reason);++state.voiceRequests;state.voiceRequestTimeMs=gameLocal.time;
        const auto status=voice?voice->Status():WolfScriptedVoiceReadinessStatus::Failed;state.voice=status;state.voiceReason=reason;
        state.voiceBacked=voice&&voice->BankBacked();
        state.phase=WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROUTE_ONUSED generation=%llu event=%llu relay=%s target=%s incoming_key=%s forwarded_key=%s onused_node=%u type19_node=%u definition=%s readiness=%s bank_backed=%d reason=\"%s\" actor_start_ack=0 door_activation=0\n",
            static_cast<unsigned long long>(event.levelGeneration),static_cast<unsigned long long>(event.eventId),event.sourceEntityName.c_str(),event.ownerEntityName.c_str(),
            state.incomingUseKey.c_str(),event.useKey.c_str(),plan.onUsedNodeId,plan.voiceNodeId,plan.voiceDefinition.c_str(),Wolf2009_ScriptedVoiceReadinessText(status),int(state.voiceBacked),reason.c_str());
        if(status==WolfScriptedVoiceReadinessStatus::Failed||status==WolfScriptedVoiceReadinessStatus::Lost||status==WolfScriptedVoiceReadinessStatus::Cancelled)
            Fail(WolfSourceGreeterRoutePhase::Failed,"Type19 readiness request failed: "+reason);
    }
    // Node 29 condition passed: its provider's Ready was consumed exactly once.
    // Same original Tick (static, TRAINYARD-CHAIN 1/3.1): node 35 Enter only
    // stores mAnim as the AI's move-start animation; its probes 6/33/72 fire;
    // its unconditioned normal output enters node 10 GoToAndFaceTarget. The
    // door's triggerStartMoveEnt is consumed by that move start (inferred; the
    // consumer was not traced). Neither native locomotion nor that move start
    // exists, so the route holds node 10 with an explicit refusal. It does not
    // play the actor clip or activate the door: the actor owner's PlayOnce is
    // not shown to be the original start contract.
    void EnterNode35(const wolf2009::TrainDoorCommand& node35){
        ++state.readyConsumptions;state.readyTimeMs=gameLocal.time;state.node35Entered=true;state.storedMoveStartAnimation=node35.resourceName;
        state.bankGeneration=voice->BankGeneration();state.bankMembers=voice->BankMembers();
        for(const auto& output:plan.unsupportedOutputs){
            if(output.outputToken==32){++state.node35ProbeRefusals;continue;}
            state.stopNode=output.destinationNodeId;state.stopNodeType=output.destinationNodeType;}
        state.stopReason="node 10 GoToAndFaceTarget needs native AI locomotion/AAS and its move start, which drives triggerStartMoveEnt; not implemented";
        state.phase=WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROUTE_TYPE19_READY generation=%llu event=%llu node29_ready_consumed=1 bank_generation=%llu bank_members=%u node35_entered=1 stored_move_start_anim=%s node35_probe_refusals=%u stop_node=%u stop_type=%u actor_playonce=0 door_activation=0 reason=\"%s\"\n",
            static_cast<unsigned long long>(state.levelGeneration),static_cast<unsigned long long>(state.relayEventId),static_cast<unsigned long long>(state.bankGeneration),state.bankMembers,
            state.storedMoveStartAnimation.c_str(),state.node35ProbeRefusals,state.stopNode,state.stopNodeType,state.stopReason.c_str());
    }
    // After Ready the request stays retained while its owner lives, as the
    // original manager keeps its record. Loss or owner end releases it; the
    // graph does not move back from node 10.
    void ObserveRetainedReadiness(){
        if(!voice)return;
        const auto status=voice->Update();++state.voiceQueries;state.voice=status;state.voiceReason=voice->Reason();
        if(status==WolfScriptedVoiceReadinessStatus::Ready)return;
        voice.reset();++state.voiceReleases;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROUTE_READINESS_ENDED state=%s reason=\"%s\" node=%u progress_kept=1\n",Wolf2009_ScriptedVoiceReadinessText(status),state.voiceReason.c_str(),state.stopNode);
    }
    struct Readiness : wolf2009::TrainDoorVoiceReadiness {
        WolfScriptedVoiceReadinessRequest& request;explicit Readiness(WolfScriptedVoiceReadinessRequest& value):request(value){}
        bool ConsumeReady()override{return request.ConsumeReady();}
    };
    // Original Tick: a latched key makes its OnUsed root eligible; the root's
    // normal output then enters its destination (node 4 -> node 10, aux 3,
    // re-activating the GoToAndFaceTarget node). Neither native locomotion
    // nor the re-entry contract exists, so the transition is refused once and
    // the latch stays pending (it is consumed only by a real transition).
    void EvaluateLatch(){
        if(!state.latchPending||state.latchTransitionRefusals)return;
        Owners();
        ++state.latchTransitionRefusals;
        state.latchTransitionReason="OnUsed root "+std::to_string(state.latchedRoot)+" -> node "+std::to_string(state.latchedDestination)+
            " GoToAndFaceTarget: native AI locomotion/AAS and the re-entry of an active GoTo are not implemented";
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_USE_LATCH_EVALUATED key=%s root=%u destination=%u route_phase=%s transition=refused latch_consumed=0 reason=\"%s\"\n",
            state.latchedKey.c_str(),state.latchedRoot,state.latchedDestination,Wolf2009_SourceGreeterRoutePhaseText(state.phase),state.latchTransitionReason.c_str());
    }
    void Tick()override{
        if(!Active())return;
        try{Need(!busy,"Greeter route Think reentry");struct Scope{bool& b;explicit Scope(bool& v):b(v){b=true;}~Scope(){b=false;}}scope(busy);
            ++state.thinkFrames;EvaluateLatch();
            if(state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay){DeliverRelay();return;}
            if(state.phase==WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode){ObserveRetainedReadiness();Owners();return;}
            Owners();
            Need(bool(voice),"Greeter route type19 request is absent");const auto status=voice->Update();++state.voiceQueries;
            state.voice=status;state.voiceReason=voice->Reason();
            if(status==WolfScriptedVoiceReadinessStatus::Pending||status==WolfScriptedVoiceReadinessStatus::Unsupported)return;
            if(status!=WolfScriptedVoiceReadinessStatus::Ready){Fail(WolfSourceGreeterRoutePhase::Failed,std::string("Type19 readiness ended without Ready: ")+state.voiceReason);return;}
            Readiness provider(*voice);const auto node35=signal->ConsumeVoiceReadiness(voiceCommand,provider);state.signal=signal->Status();
            Need(node35.status==wolf2009::TrainDoorSignalStatus::ActorStartRequested&&node35.command.kind==wolf2009::TrainDoorCommand::StartActorAnimation&&
                node35.command.sourceNodeId==plan.actorStartNodeId&&node35.command.resourceName==plan.actorAnimation,"Type19 Ready was not consumable or did not enter the exact node 35");
            EnterNode35(node35.command);
        }catch(const std::exception& error){Fail(WolfSourceGreeterRoutePhase::Failed,error.what());}
    }
    // ---------------------------------------------------------------------
    // SourceAIShellGreeterRouteV1: the greeter network hosted by the shell.
    // Roots dispatch in descriptor priority (OnSpawn 10, then OnUsed 30 in
    // graph order); each Think runs the normal pass and the ProbeLeafPassV1
    // probe pass/flush with the shared BrainFlow primitives. A normal
    // transition to a node without an admitted handler is held without any
    // effect; a probe to an unadmitted leaf is recorded as a refused leaf.
    // ---------------------------------------------------------------------
    bool hosted=false;
    wolf2009::BrainGraph graph;
    wolf2009::BrainFlowState flow;
    std::vector<std::uint32_t> taskFlags;
    std::vector<std::size_t> rootOrder;
    std::size_t activeIndex=wolf2009::BrainRecord::None();
    wolf2009::sourceai::CombatState combat;
    std::string moveStartOverride;
    idEntityPtr<idEntity> triggerStartMoveEnt,goalTarget;
    float goalOrigin[3]={0,0,0},goalAngle=0;
    WolfSourceScriptedMove move;
    wolf2009::TrainDoorCommand node35Command;
    wolf2009::TrainDoorUseEvent hostedEvent;idEntityPtr<idEntity> hostedActivator;bool hostedEventValid=false;
    std::set<std::pair<std::uint32_t,std::uint32_t>> loggedHolds;
    bool moveStartRefusalLogged=false;
    // DoorOpenRootMotionV1 (SourceAIMoveV1 M1).
    wolf2009::rootmotion::ReferenceFrame moveStartFrame;
    bool rootMotionActive=false;double rootMotionClipSec=0,rootMotionYaw=0;
    // SourceAIMoveV1 M2.
    enum class Run{None,Pending,Running,Stopping,Arrived,Refused,Facing,Faced};
    Run run=Run::None;unsigned runPendingFrames=0;
    std::string runClip,stopClip,locomotionHkxa;wolf2009::rootmotion::TurnRates turnRates;
    wolf2009::rootmotion::ReferenceFrame runFrame,stopFrame;
    double runPrevSec=0,yawCurrent=0;int runLastTimeMs=-1;std::set<int> runAreas;
    struct Adapter : WolfSourceAIBrainHost {
        Impl* owner;explicit Adapter(Impl* value):owner(value){}
        idAnimatedEntity* HostedBody()const override{return owner->actor?owner->actor->NativeEntity():NULL;}
        void HostedRelay(const wolf2009::TrainDoorUseEvent& event,idEntity* activator)override{owner->HostedRelay(event,activator);}
        void HostedThink(wolf2009::BrainTaskUse& use)override{owner->HostedThink(use);}
    } adapter{this};
    struct OneDestination : wolf2009::BrainFlowRandom {
        std::uint32_t Next15()override{throw std::runtime_error("Greeter hosted network would need an unadmitted random destination choice");}
    };
    std::string Arg(std::size_t node,const char* key,const char* fallback)const{std::string value;
        return wolf2009::sourceai::NodeArgument(graph,node,key,value)?value:std::string(fallback);}
    std::size_t Index(std::uint32_t id)const{for(std::size_t i=0;i<graph.nodes.size();++i)if(graph.nodes[i].id==id)return i;throw std::runtime_error("Greeter hosted node id is absent");}
    std::uint32_t Id(std::size_t index)const{return index<graph.nodes.size()?graph.nodes[index].id:0;}
    std::uint32_t Type(std::size_t index)const{return index<graph.nodes.size()?graph.nodes[index].type:0;}
    void PrepareNetwork(){
        Need(graph.format==wolf2009::BrainGraph::H1023&&!graph.nodes.empty(),"Greeter hosted network is not decoded");
        flow=wolf2009::MakeBrainFlowState(graph);taskFlags.assign(graph.nodes.size(),0);
        for(std::size_t i=0;i<graph.nodes.size();++i){std::uint32_t flags=0;Need(wolf2009::ReadBrainTaskFlags(graph,i,flags)&&!(flags&~(1u|0x10u|0x20u)),"Greeter hosted task flags differ");
            taskFlags[i]=flags|0x2000u;}
        // Descriptor priorities (AI-ACTOR 3.3): OnSpawn 10 before OnUsed 30.
        for(std::size_t i=0;i<graph.nodes.size();++i)if(graph.nodes[i].type==1)rootOrder.push_back(i);
        for(std::size_t i=0;i<graph.nodes.size();++i)if(graph.nodes[i].type==3)rootOrder.push_back(i);
        Need(rootOrder.size()==4&&Id(rootOrder[0])==1&&Id(rootOrder[1])==3&&Id(rootOrder[2])==4&&Id(rootOrder[3])==5,"Greeter hosted root order differs");}
    void HostedRelay(const wolf2009::TrainDoorUseEvent& event,idEntity* activator){
        if(!hosted||!Active())return;++state.relayTakes;state.relayTimeMs=gameLocal.time;state.relayEventId=event.eventId;
        hostedEvent=event;hostedActivator=activator;hostedEventValid=true;}
    // Condition context of one Think.
    struct Conditions : wolf2009::BrainFlowConditions {
        Impl& p;wolf2009::BrainTaskUse& use;Conditions(Impl& owner,wolf2009::BrainTaskUse& latch):p(owner),use(latch){}
        wolf2009::BrainConditionValue Primary(const wolf2009::BrainGraph&,std::size_t node)override{return p.Primary(node,use);}
        wolf2009::BrainConditionValue Explicit(const wolf2009::BrainGraph& g,std::size_t record)override{
            // No explicit condition has a native contract in this profile; the
            // first one met is named for the hold receipt.
            std::string text;if(record<g.records.size())for(std::size_t i=0;i<g.records[record].tokens.size()&&i<8;++i){if(i)text+=' ';text+=g.records[record].tokens[i];}
            p.unsupportedCondition=text;return wolf2009::BrainConditionValue::Unsupported;}
    };
    std::string unsupportedCondition;
    // Node 31 Wait Timer (static 0x20210150 arming; strict now > start + duration).
    int waitStartMs=-1,waitMs=-1;
    static wolf2009::BrainConditionValue Value(bool value){return value?wolf2009::BrainConditionValue::True:wolf2009::BrainConditionValue::False;}
    wolf2009::BrainConditionValue Primary(std::size_t node,wolf2009::BrainTaskUse& use){
        switch(Type(node)){
        case 1:return Value(actor&&actor->NativeEntity()!=NULL);
        case 3:return Value(use.Matches(Arg(node,"mUseKey","")));
        case 19:{if(Id(node)!=plan.voiceNodeId||!voice)return wolf2009::BrainConditionValue::Unsupported;
            const auto status=voice->Update();++state.voiceQueries;state.voice=status;state.voiceReason=voice->Reason();
            if(status==WolfScriptedVoiceReadinessStatus::Pending||status==WolfScriptedVoiceReadinessStatus::Unsupported)return wolf2009::BrainConditionValue::False;
            if(status!=WolfScriptedVoiceReadinessStatus::Ready)throw std::runtime_error(std::string("Type19 readiness ended without Ready: ")+state.voiceReason);
            return wolf2009::BrainConditionValue::True;}
        case 243:return ReachedAndFacing(node);
        case 30:{if(node!=activeIndex||waitStartMs<0||waitMs<0)return wolf2009::BrainConditionValue::Unsupported;
            return wolf2009::BrainTimerElapsed(gameLocal.time,waitStartMs,waitMs);}
        }
        return wolf2009::BrainConditionValue::Unsupported;}
    // Condition 242 on node 10, from the actor's real position and state.
    wolf2009::BrainConditionValue ReachedAndFacing(std::size_t node){
        wolf2009::sourceai::ReachedAndFacingFacts f;auto* body=actor->NativeEntity();
        f.actorIsAI=body!=NULL;f.actorReferenceText=Arg(node,"mActor","Me");
        Need(f.actorReferenceText=="Me","Greeter ReachedAndFacing actor reference is outside the admitted Me profile");
        wolf2009::sourceai::Vec3 internal;Need(wolf2009::sourceai::VectorText(Arg(node,"mInternalPos","0 0 0"),internal),"Greeter ReachedAndFacing mInternalPos is unqualified");f.internalPos=internal;
        if(auto* goal=gameLocal.FindEntity(Arg(node,"mLocation","").c_str())){const idVec3 at=goal->GetPhysics()->GetOrigin();f.locationResolved=true;f.locationOrigin.x=at.x;f.locationOrigin.y=at.y;f.locationOrigin.z=at.z;}
        Need(wolf2009::sourceai::DistanceText(Arg(node,"mRange","20.0 0"),f.range),"Greeter ReachedAndFacing mRange is unqualified");
        const idVec3 origin=body->GetPhysics()->GetOrigin();f.origin.x=origin.x;f.origin.y=origin.y;f.origin.z=origin.z;
        f.moveStatus=move.State().moveStatus;auto* face=gameLocal.FindEntity(Arg(node,"mFaceTarget","Tgt").c_str());f.faceResolved=face!=NULL;
        // IsFacing from the native move state: approach toward this face or a
        // MOVE_FACE_ENTITY turn on it is pending; after StopMove it is not.
        f.facePending=move.IsFacing(face)?wolf2009::sourceai::ReachedAndFacingFacts::FacePending::No:wolf2009::sourceai::ReachedAndFacingFacts::FacePending::Yes;
        f.startActionPlaying=false;
        std::string condition;Need(wolf2009::sourceai::NodeArgument(graph,node,"^242",condition)&&(condition=="1024"||condition=="1026"),"Greeter ReachedAndFacing condition flags differ");
        f.inverted=condition=="1026";++state.reachedAndFacingEvaluations;
        const auto value=wolf2009::sourceai::EvaluateReachedAndFacing(f);
        if(value==wolf2009::sourceai::ConditionValue::Unsupported)return wolf2009::BrainConditionValue::Unsupported;
        if(value==wolf2009::sourceai::ConditionValue::True){++state.reachedAndFacingTrue;if(state.reachedAndFacingTimeMs<0)state.reachedAndFacingTimeMs=gameLocal.time;}
        return Value(value==wolf2009::sourceai::ConditionValue::True);}
    // Exact admitted normal transitions of this profile.
    bool Admit(std::size_t source,std::size_t target,std::string& why){
        const auto from=Id(source),to=Id(target),type=Type(target);
        if(from==1&&to==2&&type==49)return true;
        if(from==2&&to==24&&type==83)return true;
        if(from==plan.onUsedNodeId&&to==plan.voiceNodeId&&type==19){
            if(!hostedEventValid){why="OnUsed train_stop arrived without the qualified arrival relay event";return false;}
            idStr reason;if(!Admission(hostedEvent,hostedActivator.GetEntity(),reason)){why=reason.c_str();return false;}return true;}
        if(from==plan.voiceNodeId&&to==plan.actorStartNodeId&&type==59)return true;
        if(from==plan.actorStartNodeId&&to==10&&type==243)return true;
        // Node 10 ReachedAndFacing -> node 31 Wait (TRAPDOOR T3).
        if(from==10&&to==31&&type==30){
            if(!move.IsFacing(gameLocal.FindEntity(state.faceName.c_str()))||state.faceStopMoves==0){why="Node 10 -> 31 without a completed FaceEntity StopMove";return false;}
            return true;}
        if(type==243){why="Re-entry of an active GoToAndFaceTarget from its OnUsed root (aux 3) is not established";return false;}
        if(type==52){why="WatchTarget (type 52) head-only look is not implemented (AI stage 2)";return false;}
        why="Node type "+std::to_string(type)+" has no admitted native handler in SourceAIShellGreeterRouteV1";return false;}
    void Hold(std::size_t source,std::size_t target,const std::string& why){
        state.holdSourceNode=Id(source);state.holdNode=Id(target);state.holdNodeType=Type(target);state.holdReason=why;
        if(!loggedHolds.insert(std::make_pair(Id(source),Id(target))).second)return;++state.holdRefusals;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_HOLD from=%u to=%u type=%u active=%u time=%d reason=\"%s\"\n",Id(source),Id(target),Type(target),Id(activeIndex),gameLocal.time,why.c_str());}
    // A Wait whose output needs a condition without a native contract holds
    // in place; no timer advances it (TRAPDOOR 3, T3 step 3).
    void HoldWait(std::size_t node,const std::string& flowReason){
        std::string why;
        // A condition record with nested arguments refuses before Explicit is
        // asked; name the first explicit record of this node's outputs then.
        if(unsupportedCondition.empty())for(auto o:graph.nodes[node].outputs){for(auto r:graph.outputs[o].conditions){const auto& t=graph.records[r].tokens;
            if(t==std::vector<std::string>({"-1"}))continue;for(std::size_t i=0;i<t.size()&&i<8;++i){if(i)unsupportedCondition+=' ';unsupportedCondition+=t[i];}break;}
            if(!unsupportedCondition.empty())break;}
        if(unsupportedCondition.compare(0,4,"100 ")==0||unsupportedCondition.compare(0,5,"^100 ")==0)
            why="ReachedLocation (100) "+unsupportedCondition+": the player's location record (+0x154) is Unknown; node 31 output 1 (Timer and ReachedLocation trap_door_room/Player) is not evaluable";
        else why="Node "+std::to_string(Id(node))+" output condition '"+unsupportedCondition+"' has no native contract ("+flowReason+")";
        if(state.node31HoldReason.empty()){state.node31HoldReason=why;state.phase=WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode;}
        Hold(node,node,why);}
    void Log(std::string& log,const std::string& entry){if(log.size()<4096){if(!log.empty())log+=';';log+=entry;}}
    void EnterFlags(std::size_t node,bool probe){auto& flags=taskFlags[node];
        if(probe)flags=(flags&~0x50u)|0x20u;else flags=(flags&~0x60u)|0x10u;flags|=0x8000u;if(!(flags&1u))flags&=~0x80000u;
        for(auto output:graph.nodes[node].outputs){auto& out=flow.outputs[output].flags;if(!(out&1u))out&=~0x80000u;}}
    void ExitTask(std::size_t node){
        // GoToAndFaceTarget Exit 0x201f15d0: +0x45c then +0x460(1). The other
        // admitted types use the shared no-op Exit 0x201fd4c0.
        if(Type(node)==243&&move.Active()){move.Stop();++state.exitMoveStops;gameLocal.Printf("WOLF2009_SOURCE_GREETER_MOVE_STOP node=%u time=%d\n",Id(node),gameLocal.time);}}
    void EnterTask(std::size_t node){
        EnterFlags(node,false);activeIndex=node;state.activeNode=Id(node);state.activeNodeType=Type(node);
        switch(Type(node)){
        case 49:{// IgnoreEnemiesSet Enter 0x201f2c60.
            const auto who=Arg(node,"mActor","Me");Need(who=="Me"||who==plan.ownerEntityName,"Greeter IgnoreEnemiesSet actor is outside the admitted profile");
            std::string why;const auto result=wolf2009::sourceai::IgnoreEnemiesSetEnter(&combat,wolf2009::sourceai::BoolText(Arg(node,"mIgnore","true")),why);
            Need(result==wolf2009::sourceai::IgnoreEnemiesResult::Applied,why.empty()?"Greeter IgnoreEnemiesSet refused":why.c_str());
            ++state.ignoreEnemiesApplied;state.ignoreEnemies=combat.ignoreEnemies;state.spawnRootTimeMs=gameLocal.time;
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_NODE node=%u type=49 actor=%s ignore_enemies=%d enemy=0 tactical_goal=0 time=%d\n",Id(node),who.c_str(),int(combat.ignoreEnemies),gameLocal.time);break;}
        case 83:// SetIgnorePanic: Enter is the shared no-op 0x201fd4c0.
            ++state.ignorePanicNoops;gameLocal.Printf("WOLF2009_SOURCE_GREETER_NODE node=%u type=83 civilian=%s enter=noop time=%d\n",Id(node),Arg(node,"mCivilian","").c_str(),gameLocal.time);break;
        case 19:OpenVoice(hostedEvent);hostedEventValid=false;break;
        case 59:{// AnimOverrideMoveStart Enter 0x201f5d40 stores mAnim (ai+0x3820).
            Need(voice!=NULL,"Greeter node 35 entered without its node 29 request");Readiness provider(*voice);
            const auto node35=signal->ConsumeVoiceReadiness(voiceCommand,provider);state.signal=signal->Status();
            Need(node35.status==wolf2009::TrainDoorSignalStatus::ActorStartRequested&&node35.command.kind==wolf2009::TrainDoorCommand::StartActorAnimation&&
                node35.command.sourceNodeId==plan.actorStartNodeId&&node35.command.resourceName==plan.actorAnimation&&Arg(node,"mAnim","")==plan.actorAnimation,
                "Type19 Ready was not consumable or did not enter the exact node 35");
            node35Command=node35.command;moveStartOverride=node35.command.resourceName;
            ++state.readyConsumptions;state.readyTimeMs=gameLocal.time;state.node35Entered=true;state.storedMoveStartAnimation=moveStartOverride;
            state.bankGeneration=voice->BankGeneration();state.bankMembers=voice->BankMembers();
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_NODE node=%u type=59 stored_move_start_anim=%s bank_generation=%llu bank_members=%u time=%d\n",Id(node),moveStartOverride.c_str(),
                static_cast<unsigned long long>(state.bankGeneration),state.bankMembers,gameLocal.time);break;}
        case 243:EnterGoTo(node);break;
        case 30:{// Wait Enter arms the Timer from mDuration (literal ms profile).
            const auto duration=wolf2009::ParseSourceWaitDurationLiteralV1(Arg(node,"mDuration",""));
            const auto maximum=wolf2009::ParseSourceWaitDurationLiteralV1(Arg(node,"mDurationMax","0 0"));
            Need(maximum.milliseconds==0,"Greeter Wait random maximum is not admitted");
            waitStartMs=gameLocal.time;waitMs=duration.milliseconds;state.node31Entered=Id(node)==31;state.node31TimeMs=gameLocal.time;state.node31WaitMs=waitMs;
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_NODE node=%u type=30 wait=%s wait_ms=%d move_active=%d time=%d\n",Id(node),duration.raw.c_str(),waitMs,int(move.Active()),gameLocal.time);break;}
        default:throw std::runtime_error("Greeter hosted network entered an unadmitted node");
        }}
    // GoToAndFaceTarget Enter 0x201f1420: resolve actor, goal and face (a
    // missing one is reported), ScriptedMove(goal, face, range), style bits.
    void EnterGoTo(std::size_t node){
        state.node10Entered=true;state.node10TimeMs=gameLocal.time;auto* body=actor->NativeEntity();
        Need(Arg(node,"mActor","Me")=="Me","Greeter GoToAndFaceTarget actor is outside the admitted Me profile");
        state.goalName=Arg(node,"mLocation","Tgt");state.faceName=Arg(node,"mFaceTarget","Tgt");state.movement=Arg(node,"mMovement","Walk");
        WolfSourceScriptedMoveRequest request;request.actor=body;request.goal=gameLocal.FindEntity(state.goalName.c_str());request.face=gameLocal.FindEntity(state.faceName.c_str());
        state.goalResolved=request.goal!=NULL;state.faceResolved=request.face!=NULL;
        if(!request.goal)gameLocal.Warning("GoToAndFaceTarget task unable to find entity '%s'",state.goalName.c_str());
        if(!request.face)gameLocal.Warning("GoToAndFaceTarget task unable to find entity '%s'",state.faceName.c_str());
        Need(request.goal==NULL||request.goal==goalTarget.GetEntity(),"Greeter node 10 goal resolves to an entity other than its source target_null");
        Need(wolf2009::sourceai::DistanceText(Arg(node,"mRange","20.0 0"),request.range),"Greeter GoToAndFaceTarget mRange is unqualified");state.range=request.range;
        Need(wolf2009::sourceai::MovementText(state.movement,request.movement),"Greeter GoToAndFaceTarget mMovement is outside its enum");
        request.ignoreClusters=wolf2009::sourceai::BoolText(Arg(node,"mIgnoreClusters","false"));request.endInCrouch=wolf2009::sourceai::BoolText(Arg(node,"mEndInCrouch","false"));
        request.travel=WolfSourceGoToTravelPolicy::WalkAirV1;state.travelPolicy="WalkAirV1";
        const idVec3 at=body->GetPhysics()->GetOrigin();for(int i=0;i<3;++i)state.node10Origin[i]=at[i];
        idStr why;const bool ok=move.Begin(request,why);const auto& m=move.State();
        state.styleBits=m.styleBits;state.startArea=m.startArea;state.goalArea=m.goalArea;state.routeHops=m.routeHops;state.routeTravelTime=m.routeTravelTime;state.travelFlags=m.travelFlags;
        for(int i=0;i<3;++i)state.goal[i]=m.goal[i];
        if(!ok)state.moveFailure=why.c_str();
        state.phase=WolfSourceGreeterRoutePhase::ScriptedMoveActive;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_GOTO node=%u goal=%s goal_resolved=%d face=%s face_resolved=%d range=%.2f movement=%s style_bits=0x%02x travel=%s travel_flags=%d start_area=%d goal_area=%d hops=%u first_time=%d scripted_move=%d time=%d reason=\"%s\"\n",
            Id(node),state.goalName.c_str(),int(state.goalResolved),state.faceName.c_str(),int(state.faceResolved),state.range,state.movement.c_str(),state.styleBits,state.travelPolicy.c_str(),
            state.travelFlags,state.startArea,state.goalArea,state.routeHops,state.routeTravelTime,int(ok),gameLocal.time,why.c_str());}
    // Probe leaves: no outputs, no Exit. Effects are performed only by an
    // admitted handler; every other leaf is an explicit refused leaf.
    void ProbeLeaf(std::size_t source,std::size_t node){
        EnterFlags(node,true);++state.probeEntries;std::string why;
        switch(Type(node)){
        case 351:why="HideWeapon (351) contract is not recovered";break;
        case 75:why="SetUsable (75) writes entity +0x159 bit 7; no native player-use eligibility flag exists";break;
        case 33:why="SetScriptedManual (33) belongs to AI stage 2";break;
        case 19:why="Probe-entered ScriptedVOStreamIn has no established request lifetime (no Exit)";break;
        case 12:why="Say (12) is AI stage 3 (speech)";break;
        case 68:{const auto target=Arg(node,"mUseTarget","Entity");auto* receiver=gameLocal.FindEntity(target.c_str());
            why=receiver?"UseTarget receiver '"+target+"' is not an admitted native Use receiver":"UseTarget receiver '"+target+"' is not a native entity in this session";break;}
        case 306:why="ShowTip (306) writes the HUD/mission singleton tip slot; the native side has no equivalent slot";break;
        default:why="Probe leaf type "+std::to_string(Type(node))+" has no admitted handler";break;}
        ++state.probeRefusals;if(Id(source)==plan.actorStartNodeId)++state.node35ProbeRefusals;
        Log(state.probeLog,std::to_string(Id(source))+">"+std::to_string(Id(node))+":"+std::to_string(Type(node))+":refused");
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_PROBE from=%u leaf=%u type=%u effect=refused time=%d reason=\"%s\"\n",Id(source),Id(node),Type(node),gameLocal.time,why.c_str());}
    void ProbePass(wolf2009::BrainTaskUse& use){
        if(activeIndex==wolf2009::BrainRecord::None())return;Conditions conditions(*this,use);std::size_t marked=0;
        const auto decision=wolf2009::MarkBrainProbeOutputs(graph,activeIndex,flow,conditions,marked);
        Need(decision.status==wolf2009::BrainFlowDecision::NoTransition,decision.reason.empty()?"Greeter probe pass refused":decision.reason.c_str());
        OneDestination random;const auto source=activeIndex;
        for(auto output:graph.nodes[source].outputs){if(!(flow.outputs[output].flags&0x40000u))continue;
            const auto fire=wolf2009::FireBrainMarkedProbeOutput(graph,output,flow,random);
            Need(fire.status==wolf2009::BrainFlowDecision::Transition,fire.reason.empty()?"Greeter probe flush refused":fire.reason.c_str());
            Need(graph.nodes[fire.targetNode].outputs.empty(),"Greeter probe destination is not a leaf");ProbeLeaf(source,fire.targetNode);}}
    void NetworkTick(wolf2009::BrainTaskUse& use){
        Conditions conditions(*this,use);OneDestination random;bool checkRoots=true;unsigned transitions=0;
        for(;;){
            wolf2009::BrainFlowDecision decision;wolf2009::BrainFlowState next;std::size_t source=wolf2009::BrainRecord::None();
            if(checkRoots)for(auto root:rootOrder){
                if((taskFlags[root]&0x80000u)&&(taskFlags[root]&1u))continue;
                if(Primary(root,use)!=wolf2009::BrainConditionValue::True)continue;
                next=flow;decision=wolf2009::SelectBrainTransition(graph,root,next,conditions,random);
                Need(decision.status!=wolf2009::BrainFlowDecision::Unsupported,decision.reason.empty()?"Greeter root selection refused":decision.reason.c_str());
                if(decision.status==wolf2009::BrainFlowDecision::Transition){source=root;break;}}
            if(decision.status!=wolf2009::BrainFlowDecision::Transition&&activeIndex!=wolf2009::BrainRecord::None()){
                unsupportedCondition.clear();
                next=flow;decision=wolf2009::SelectBrainTransition(graph,activeIndex,next,conditions,random);source=activeIndex;
                if(decision.status==wolf2009::BrainFlowDecision::Unsupported&&Type(activeIndex)==30){HoldWait(activeIndex,decision.reason);return;}
                Need(decision.status!=wolf2009::BrainFlowDecision::Unsupported,decision.reason.empty()?"Greeter normal pass refused":decision.reason.c_str());}
            bool transitioned=false;
            if(decision.status==wolf2009::BrainFlowDecision::Transition){
                std::string why;if(!Admit(source,decision.targetNode,why)){Hold(source,decision.targetNode,why);return;}
                flow=std::move(next);if(activeIndex!=wolf2009::BrainRecord::None())ExitTask(activeIndex);
                const bool root=Type(source)==1||Type(source)==3;const auto from=Id(source);
                EnterTask(decision.targetNode);taskFlags[source]|=0x80000u;if(!Active())return;
                // OnUsed Exit 0x201e6ee0 clears the matching pending key after its transition.
                if(Type(source)==3){Need(use.Consume(Arg(source,"mUseKey","")),"Greeter OnUsed root fired without its pending key");}
                ++transitions;++state.networkTransitions;checkRoots=false;transitioned=true;(void)root;
                Log(state.transitionLog,std::to_string(from)+">"+std::to_string(Id(decision.targetNode)));
                Need(transitions<=50,"Greeter hosted network transition budget exceeded");}
            ProbePass(use);
            if(!transitioned)break;}}
    // Leg state machine (0x20166d00) and Action_Move_Start (0x201c75c0) after
    // the brain tick of the same Think, as in idAI Think (Static order).
    void MoveFrame(){
        if(!move.Active())return;
        if(move.UpdateLegs()){++state.legTransitions;gameLocal.Printf("WOLF2009_SOURCE_GREETER_LEGS idle_to_move=1 pending_move_start=1 time=%d\n",gameLocal.time);}
        if(move.State().moveStartPending){
            wolf2009::sourceai::MoveStartFacts facts;facts.pending=true;facts.moving=move.State().moving;facts.overrideAnimation=moveStartOverride;
            WolfSourceScriptedActorState held;idStr why;NeedReason(actor->Query(held,why),why,"Greeter actor owner query failed");
            facts.clipResolved=!moveStartOverride.empty()&&held.logicalClip==moveStartOverride;
            std::string animation,refusal;const auto decision=wolf2009::sourceai::TryMoveStart(facts,animation,refusal);
            if(decision==wolf2009::sourceai::MoveStartDecision::Start)ActionMoveStart(animation);
            else if(!moveStartRefusalLogged){moveStartRefusalLogged=true;++state.moveStartRefusals;
                gameLocal.Printf("WOLF2009_SOURCE_GREETER_MOVE_START_REFUSED time=%d reason=\"%s\"\n",gameLocal.time,refusal.c_str());}}
        if(rootMotionActive){RootMotionFrame();if(rootMotionActive)return;if(run==Run::None)run=Run::Pending;}
        if(run==Run::Pending)BeginRun();
        if(run==Run::Running||run==Run::Stopping){RunFrame();return;}
        if(run==Run::Facing){FaceFrame();return;}
        if(run==Run::Arrived||run==Run::Pending||run==Run::Faced)return;
        const bool first=move.State().translationRefusals==0;
        move.RefuseTranslation(!state.rootMotionRefusal.empty()?state.rootMotionRefusal:!state.runRefusal.empty()?state.runRefusal:
            "Run phase: the scripted actor owner holds only the move-start clip; cycling std_nil_nml_run along the aas28 route "
            "needs the multi-clip AI body (SourceAIMoveV1 M2); the actor keeps its move-start end origin");
        if(first&&move.State().translationRefusals){state.translationRefusal=move.State().translationReason;
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_TRANSLATION_REFUSED node=10 time=%d reason=\"%s\"\n",gameLocal.time,state.translationRefusal.c_str());}}
    static std::vector<unsigned char> ReadSource(const std::string& path){
        idFile* file=fileSystem->OpenFileRead(path.c_str(),false);Need(file!=NULL,"Move-start HKXA source is absent");
        const int length=file->Length();std::vector<unsigned char> bytes(length>0?std::size_t(length):0u);
        const int got=length>0&&length<=(64<<20)?file->Read(bytes.data(),length):-1;fileSystem->CloseFile(file);
        Need(length>0&&got==length,"Move-start HKXA source read is short or over budget");return bytes;}
    // The committed move-start clip's own reference frame drives the body from
    // its first frame with motion (frame 148 of the door-open clip).
    void BeginRootMotion(){
        state.rootMotionProfile="DoorOpenRootMotionV1";rootMotionActive=false;rootMotionClipSec=0;
        try{WolfSourceScriptedActorState held;idStr why;NeedReason(actor->Query(held,why),why,"Greeter actor owner query failed");
            Need(!held.animationResource.empty()&&!held.clipName.empty(),"Greeter actor owner has no exact move-start HKXA identity");
            moveStartFrame=wolf2009::rootmotion::ReferenceFrame::Admit(wolf2009::SplineClipData::Load(ReadSource(held.animationResource),held.clipName));
            Need(moveStartFrame.State()==wolf2009::rootmotion::FrameState::Admitted,
                 moveStartFrame.State()==wolf2009::rootmotion::FrameState::Absent?"Move-start clip has no root motion":moveStartFrame.Reason().c_str());
            Need(moveStartFrame.Frames()==held.frames,"Move-start reference frame and native clip frame counts differ");
            auto* body=actor->NativeEntity();const idVec3 at=body->GetPhysics()->GetOrigin();
            rootMotionYaw=DEG2RAD(body->GetPhysics()->GetAxis().ToAngles().yaw);
            const auto end=wolf2009::rootmotion::Rotate(moveStartFrame.Sample(moveStartFrame.Duration()),rootMotionYaw);
            for(int i=0;i<3;++i){state.rootMotionStart[i]=at[i];}
            state.rootMotionExpectedEnd[0]=at.x+float(end.x);state.rootMotionExpectedEnd[1]=at.y+float(end.y);state.rootMotionExpectedEnd[2]=at.z+float(end.z);
            state.rootMotionClipDuration=moveStartFrame.Duration();rootMotionActive=true;
        }catch(const std::exception& error){state.rootMotionRefusal=std::string("DoorOpenRootMotionV1 refused: ")+error.what();}
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROOT_MOTION_BEGIN profile=%s active=%d duration=%.4f start=(%.2f %.2f %.2f) expected_end=(%.2f %.2f %.2f) time=%d reason=\"%s\"\n",
            state.rootMotionProfile.c_str(),int(rootMotionActive),state.rootMotionClipDuration,state.rootMotionStart[0],state.rootMotionStart[1],state.rootMotionStart[2],
            state.rootMotionExpectedEnd[0],state.rootMotionExpectedEnd[1],state.rootMotionExpectedEnd[2],gameLocal.time,state.rootMotionRefusal.c_str());}
    void RootMotionFrame(){
        WolfSourceScriptedActorState held;idStr why;NeedReason(actor->Query(held,why),why,"Greeter actor owner query failed");
        Need(held.nativeStartTimeMs>=0,"Greeter move-start clip has no native start time");
        const double t=std::min(double(gameLocal.time-held.nativeStartTimeMs)*0.001,moveStartFrame.Duration());++state.rootMotionFrames;
        if(t>rootMotionClipSec){
            const auto d=moveStartFrame.ModelDelta(rootMotionClipSec,t);const auto w=wolf2009::rootmotion::Rotate(d,rootMotionYaw);
            const double step=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
            if(step>0||d.yaw!=0){auto* body=actor->NativeEntity();const idVec3 next=body->GetPhysics()->GetOrigin()+idVec3(float(w.x),float(w.y),float(w.z));
                const double yaw=RAD2DEG(rootMotionYaw+moveStartFrame.Sample(t).yaw);yawCurrent=wolf2009::rootmotion::Normalize180(yaw);
                NeedReason(actor->Relocate(next.x,next.y,next.z,float(yawCurrent),why),why,"Greeter actor owner refused the root-motion placement");}
            if(step>0){const idVec3 next=actor->NativeEntity()->GetPhysics()->GetOrigin();
                if(!state.rootMotionMovingFrames){state.rootMotionFirstMoveTimeMs=gameLocal.time;state.rootMotionFirstMoveClipMs=int(std::floor(t*1000.0+.5));}
                ++state.rootMotionMovingFrames;state.rootMotionDistance+=step;state.rootMotionMaxStep=std::max(state.rootMotionMaxStep,step);
                const auto s=wolf2009::rootmotion::Rotate(moveStartFrame.Sample(t),rootMotionYaw);
                const idVec3 expected(state.rootMotionStart[0]+float(s.x),state.rootMotionStart[1]+float(s.y),state.rootMotionStart[2]+float(s.z));
                state.rootMotionMaxPathError=std::max(state.rootMotionMaxPathError,double((next-expected).Length()));}
            rootMotionClipSec=t;}
        if(t>=moveStartFrame.Duration()){rootMotionActive=false;state.rootMotionEndTimeMs=gameLocal.time;
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROOT_MOTION_END profile=%s frames=%u moving_frames=%u first_move_clip_ms=%d distance=%.3f max_step=%.4f max_path_error=%.5f time=%d\n",
                state.rootMotionProfile.c_str(),state.rootMotionFrames,state.rootMotionMovingFrames,state.rootMotionFirstMoveClipMs,state.rootMotionDistance,state.rootMotionMaxStep,
                state.rootMotionMaxPathError,gameLocal.time);}}
    void RefuseRun(const std::string& why){run=Run::Refused;state.runPhase="refused";state.runRefusal="Run phase: "+why;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_RUN_REFUSED time=%d reason=\"%s\"\n",gameLocal.time,state.runRefusal.c_str());}
    void BeginRun(){
        try{WolfSourceScriptedActorState held;idStr why;NeedReason(actor->Query(held,why),why,"Greeter actor owner query failed");
            if(held.phase!=WolfSourceScriptedActorPhase::Completed){Need(++runPendingFrames<=30,"move-start clip completion was not observed");state.runPhase="pending";return;}
            Need(!runClip.empty()&&!stopClip.empty(),"no admitted locomotion prefix for the greeter model (CivilianUnarmedPrefixV1)");
            const auto style=wolf2009::rootmotion::ResolveMoveStyleV1(std::uint8_t(state.styleBits),false,false,false,false);
            Need(wolf2009::rootmotion::MoveClipNameV1(style,"std_nil_nml_","","")==runClip,"node 10 style bits resolve to a clip other than the prepared run clip");
            const auto bytes=ReadSource(locomotionHkxa);const std::string dir="models/characters/_humanoid_male/";
            runFrame=wolf2009::rootmotion::ReferenceFrame::Admit(wolf2009::SplineClipData::Load(bytes,dir+runClip+".hkx"));
            stopFrame=wolf2009::rootmotion::ReferenceFrame::Admit(wolf2009::SplineClipData::Load(bytes,dir+stopClip+".hkx"));
            Need(runFrame.State()==wolf2009::rootmotion::FrameState::Admitted&&stopFrame.State()==wolf2009::rootmotion::FrameState::Admitted,"run/stop reference frames were refused");
            state.stopForward=stopFrame.Frame(stopFrame.Frames()-1).x;Need(state.stopForward>0,"stop clip has no forward translation");
            NeedReason(actor->PlayLocomotion(runClip,true,why),why,"run clip cycle refused");
            run=Run::Running;runPrevSec=0;runLastTimeMs=gameLocal.time;state.runPhase="running";state.runStartTimeMs=gameLocal.time;state.runClip=runClip;state.stopClip=stopClip;
            state.turnRateRunning=turnRates.running;
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_RUN_BEGIN clip=%s stop=%s stop_forward=%.3f yaw=%.3f max_turn_running=%.1f time=%d\n",runClip.c_str(),stopClip.c_str(),state.stopForward,yawCurrent,turnRates.running,gameLocal.time);
        }catch(const std::exception& error){RefuseRun(error.what());}}
    void RunFrame(){
        try{WolfSourceScriptedActorState held;idStr why;NeedReason(actor->Query(held,why),why,"Greeter actor owner query failed");
            Need(held.locomotionStartTimeMs>=0,"locomotion clip has no native start time");
            const double now=double(gameLocal.time-held.locomotionStartTimeMs)*0.001;const double elapsedMs=double(gameLocal.time-runLastTimeMs);runLastTimeMs=gameLocal.time;
            Need(gameLocal.time-state.runStartTimeMs<=30000,"no arrival within 30 s of running");
            wolf2009::rootmotion::Motion d;
            if(run==Run::Running){d=runFrame.LoopModelDelta(std::fmod(runPrevSec,runFrame.Duration()),std::max(0.0,now-runPrevSec));}
            else{const double t=std::min(now,stopFrame.Duration());d=stopFrame.ModelDelta(std::min(runPrevSec,stopFrame.Duration()),t);}
            runPrevSec=now;
            auto* body=actor->NativeEntity();const idVec3 origin=body->GetPhysics()->GetOrigin();
            const idAAS* aas=gameLocal.GetAAS("aas28");Need(aas&&aas->GetSettings()&&aas->GetSettings()->numBoundingBoxes==1,"native aas28 is unavailable");
            const idBounds& hull=aas->GetSettings()->boundingBoxes[0];const auto& m=move.State();
            const idVec3 goal(m.goal[0],m.goal[1],m.goal[2]);
            const int area=aas->PointReachableAreaNum(origin+idVec3(0,0,1),hull,AREA_REACHABLE_WALK);Need(area>0,"GplWalkPathSeekV1: the greeter left the reachable aas28 areas");
            if(runAreas.insert(area).second){++state.runAreasVisited;if(std::find(m.routeAreas.begin(),m.routeAreas.end(),area)==m.routeAreas.end())++state.runOffRouteAreas;}
            idVec3 seek=goal;
            if(area!=m.goalArea){aasPath_t path;Need(aas->WalkPathToGoal(path,area,origin,m.goalArea,goal,m.travelFlags),"GplWalkPathSeekV1: no walk path to the goal area");seek=path.moveGoal;}
            // §3.3 step 6: the delta is rotated by the axis of the previous frame.
            const auto w=wolf2009::rootmotion::Rotate(d,DEG2RAD(yawCurrent));
            idVec3 next=origin+idVec3(float(w.x),float(w.y),0.f);
            trace_t floor;gameLocal.clip.TracePoint(floor,next+idVec3(0,0,24),next-idVec3(0,0,48),MASK_MONSTERSOLID,body);
            Need(floor.fraction<1.f,"TraceFloorSnapV1: no floor under the next step");next.z=floor.endpos.z;++state.floorSnaps;
            // §3.4 ordinary turn toward the seek point.
            const idVec3 to=(run==Run::Running?seek:goal)-next;state.yawIdeal=to.ToYaw();
            wolf2009::rootmotion::TurnInput turn;turn.yawIdeal=state.yawIdeal;turn.yawCurrent=yawCurrent;turn.elapsedMs=elapsedMs;turn.running=run==Run::Running;turn.moving=true;
            const double before=yawCurrent;yawCurrent=wolf2009::rootmotion::TurnStepV1(turnRates,turn);
            state.runMaxTurnStep=std::max(state.runMaxTurnStep,std::fabs(wolf2009::rootmotion::Normalize180(yawCurrent-before)));state.yawCurrent=yawCurrent;
            NeedReason(actor->Relocate(next.x,next.y,next.z,float(yawCurrent),why),why,"Greeter actor owner refused the run placement");
            const double step=std::sqrt(w.x*w.x+w.y*w.y);++state.runFrames;state.runDistance+=step;state.runMaxStep=std::max(state.runMaxStep,step);
            const double remaining=(goal-next).ToVec2().Length();
            if(run==Run::Running&&(seek-goal).LengthFast()<1.f&&remaining<=state.stopForward){
                // ApproachGoalStopV1: start the straight stop one stop length out.
                NeedReason(actor->PlayLocomotion(stopClip,false,why),why,"stop clip refused");run=Run::Stopping;runPrevSec=0;
                ++state.stopStarts;state.stopStartTimeMs=gameLocal.time;state.stopStartDistance=remaining;state.runPhase="stopping";
                gameLocal.Printf("WOLF2009_SOURCE_GREETER_STOP_BEGIN clip=%s distance=%.3f area=%d time=%d\n",stopClip.c_str(),remaining,area,gameLocal.time);}
            else if(run==Run::Stopping&&now>=stopFrame.Duration()){
                run=Run::Arrived;state.runPhase="arrived";state.arrivalTimeMs=gameLocal.time;state.arrivalDistance2D=remaining;
                // §3.5: ReachedPos within range posts "reached" on the goal; then
                // FaceEntity on the approach-face entity.
                const bool reached=remaining<state.range&&std::fabs(goal.z-next.z)<70.f;
                if(reached)++state.reachedPosts;
                gameLocal.Printf("WOLF2009_SOURCE_GREETER_ARRIVED distance2d=%.3f z=%.2f reached_posts=%u run_frames=%u distance=%.3f areas=%u off_route=%u yaw=%.3f time=%d\n",
                    remaining,next.z,state.reachedPosts,state.runFrames,state.runDistance,state.runAreasVisited,state.runOffRouteAreas,yawCurrent,gameLocal.time);
                if(reached)BeginFace();
                else state.faceRefusal="Arrival outside ReachedPos range: the original keeps moving; no FaceEntity";}
        }catch(const std::exception& error){RefuseRun(error.what());}}
    // AI-MOVE 3.5 approachFaceEntity: FaceEntity on move.approachFaceEntity
    // (tolerance 0, not an idActor). yaw_ideal toward its physics origin.
    void BeginFace(){
        state.faceProfile="FaceTurnInPlaceV1";auto* face=move.Face();auto* body=actor->NativeEntity();
        idStr why;
        if(!move.FaceEntity(face,why)){state.faceRefusal=std::string("FaceEntity: ")+why.c_str();++state.faceEntityCalls;
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_FACE_REFUSED time=%d reason=\"%s\"\n",gameLocal.time,state.faceRefusal.c_str());return;}
        ++state.faceEntityCalls;state.faceMoveCommand=move.State().moveCommand;
        const idVec3 target=face->GetPhysics()->GetOrigin(),at=body->GetPhysics()->GetOrigin();
        for(int i=0;i<3;++i){state.faceTarget[i]=target[i];state.faceOrigin[i]=at[i];}
        state.faceYawIdeal=wolf2009::rootmotion::Normalize180((target-at).ToYaw());state.yawIdeal=state.faceYawIdeal;
        state.faceStartYaw=yawCurrent;state.faceStartTimeMs=gameLocal.time;state.turnRateIdle=turnRates.idle;
        run=Run::Facing;state.runPhase="facing";runLastTimeMs=gameLocal.time;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_FACE_BEGIN face=%s target=(%.2f %.2f %.2f) origin=(%.2f %.2f %.2f) yaw=%.3f yaw_ideal=%.3f command=%u max_turn_idle=%.1f time=%d\n",
            face->GetName(),target.x,target.y,target.z,at.x,at.y,at.z,yawCurrent,state.faceYawIdeal,state.faceMoveCommand,turnRates.idle,gameLocal.time);}
    void FaceFrame(){
        try{auto* face=move.GoalEntity();Need(face&&move.State().moveCommand==WolfSourceMoveCommandFaceEntity,"FaceEntity lost its MOVE_FACE_ENTITY goal");
            auto* body=actor->NativeEntity();const idVec3 at=body->GetPhysics()->GetOrigin();
            const double elapsedMs=double(gameLocal.time-runLastTimeMs);runLastTimeMs=gameLocal.time;
            state.yawIdeal=wolf2009::rootmotion::Normalize180((face->GetPhysics()->GetOrigin()-at).ToYaw());
            wolf2009::rootmotion::TurnInput turn;turn.yawIdeal=state.yawIdeal;turn.yawCurrent=yawCurrent;turn.elapsedMs=elapsedMs;turn.running=false;turn.moving=false;
            const double before=yawCurrent;yawCurrent=wolf2009::rootmotion::TurnStepV1(turnRates,turn);
            state.faceMaxTurnStep=std::max(state.faceMaxTurnStep,std::fabs(wolf2009::rootmotion::Normalize180(yawCurrent-before)));
            const bool facing=wolf2009::rootmotion::FacingIdealV1(state.yawIdeal,yawCurrent);
            state.yawCurrent=yawCurrent;++state.faceFrames;idStr why;
            NeedReason(actor->Relocate(at.x,at.y,at.z,float(yawCurrent),why),why,"Greeter actor owner refused the facing placement");
            if(facing){move.StopMove();++state.faceStopMoves;state.facingTimeMs=gameLocal.time;state.faceFinalYaw=yawCurrent;
                state.facingAtStop=move.IsFacing(face);run=Run::Faced;state.runPhase="faced";
                gameLocal.Printf("WOLF2009_SOURCE_GREETER_FACE_STOP yaw=%.4f yaw_ideal=%.4f frames=%u max_step=%.3f is_facing=%d time=%d\n",
                    yawCurrent,state.yawIdeal,state.faceFrames,state.faceMaxTurnStep,int(state.facingAtStop),gameLocal.time);}
            else Need(gameLocal.time-state.faceStartTimeMs<=10000,"FaceEntity did not reach FacingIdeal within 10 s");
        }catch(const std::exception& error){state.faceRefusal=std::string("FaceTurnInPlaceV1: ")+error.what();run=Run::Refused;state.runPhase="refused";
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_FACE_REFUSED time=%d reason=\"%s\"\n",gameLocal.time,state.faceRefusal.c_str());}}
    void ActionMoveStart(const std::string& animation){
        // Stage 0: play the stored move-start clip once at rate 1 on the
        // actor; then, if triggerStartMoveEnt is live, process `activate` on
        // it synchronously with this AI as activator and clear the handle.
        WolfSourceScriptedActorStartAck ack;idStr why;
        NeedReason(actor->Start(node35Command,ack,why)&&ack.committed&&!ack.duplicate&&ack.actualPoseEvaluated,why,"Greeter move-start PlayOnce was refused");
        ++state.actorStartCommits;state.moveStartTimeMs=gameLocal.time;state.moveStartAnimation=animation;++state.moveStarts;BeginRootMotion();
        moveStartOverride.clear();state.moveStartOverrideCleared=true;move.ClearMoveStartPending();
        if(auto* trigger=triggerStartMoveEnt.GetEntity()){
            Need(trigger==door->Entity(),"Greeter triggerStartMoveEnt no longer resolves to its captured door");
            const auto result=signal->ActorStartCommitted(node35Command,true);state.signal=signal->Status();
            Need(result.status==wolf2009::TrainDoorSignalStatus::TargetActivationRequested&&result.command.kind==wolf2009::TrainDoorCommand::ActivateTarget&&
                result.command.targetEntityName==plan.targetEntityName,"Greeter move start did not produce the door activation");
            NeedReason(door->Activate(result.command,why),why,"Bound door refused the move-start activation");
            ++state.doorActivations;state.doorActivationTimeMs=gameLocal.time;}
        triggerStartMoveEnt=NULL;state.triggerStartMoveEntConsumed=true;
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_MOVE_START anim=%s actor_commit=%u door_activation=%u override_cleared=1 trigger_consumed=1 time=%d\n",
            animation.c_str(),state.actorStartCommits,state.doorActivations,gameLocal.time);}
    void DoorFrame(){
        if(!state.doorActivations)return;idStr why;NeedReason(door->Tick(why),why,"Bound door Tick failed");++state.doorTicks;
        wolf2009::TrainDoorUseEvent unused;(void)unused;WolfSourceAnimatedDoorCompletion completion;bool pending=false;
        NeedReason(door->TakeCompletion(pending,completion,why),why,"Bound door completion query failed");
        if(pending){++state.doorCompletions;state.doorCompletionTimeMs=gameLocal.time;WolfSourceAnimatedDoorState current;NeedReason(door->Query(current,why),why,"Bound door query failed");
            state.doorEndpointError=current.endpointPaletteError;
            gameLocal.Printf("WOLF2009_SOURCE_GREETER_DOOR_COMPLETE end_clip=%s native_end=%d observed=%d endpoint_error=%.6f time=%d\n",completion.endClip.c_str(),completion.nativeEndTimeMs,completion.observedTimeMs,current.endpointPaletteError,gameLocal.time);}}
    void HostedThink(wolf2009::BrainTaskUse& use){
        if(!hosted||!Active())return;
        try{Need(!busy,"Greeter hosted Think reentry");struct Scope{bool& b;explicit Scope(bool& v):b(v){b=true;}~Scope(){b=false;}}scope(busy);
            ++state.thinkFrames;++state.hostedThinks;Owners();
            if(taskFlags.empty())PrepareNetwork();
            if(state.node35Entered)ObserveRetainedReadiness();
            NetworkTick(use);
            if(Active()){MoveFrame();DoorFrame();}
            const idVec3 at=actor->NativeEntity()->GetPhysics()->GetOrigin();for(int i=0;i<3;++i)state.origin[i]=at[i];
        }catch(const std::exception& error){Fail(WolfSourceGreeterRoutePhase::Failed,error.what());if(move.Active())move.Stop();}}
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
bool WolfSourceGreeterRoute::HostInShell(idStr& reason){reason.Clear();if(impl->state.phase!=WolfSourceGreeterRoutePhase::Prepared||impl->actor||impl->hosted)return Refuse(reason,"Greeter route can be hosted only once, before Spawn");
    impl->hosted=impl->state.hosted=true;return true;}
WolfSourceAIBrainHost* WolfSourceGreeterRoute::ShellHost()const{return impl->hosted?&impl->adapter:NULL;}

std::unique_ptr<WolfSourceGreeterRoute> Wolf2009_PrepareSourceGreeterRoute(const WolfSourceDeclarationSet& set,const WolfSourceGreeterRouteRequest& request,idStr& reason){reason.Clear();try{
    Need(request.profile==Profile&&request.sourceMapName=="maps/game/trainyard/trainyard"&&!request.voiceLocale.empty()&&request.voiceLocale.size()<=64,"Explicit measured greeter route profile required");
    std::shared_ptr<WolfSourceGreeterRoute::Impl> out(new WolfSourceGreeterRoute::Impl);out->set=&set;out->request=request;
    const auto entities=render::ParseResourcePath(request.exactEntities);Need(entities.type=="ents"&&entities.name==request.sourceMapName,"Greeter route ENTS path differs from the source map");
    const auto prefix=entities.Prefix();const auto entsBytes=ReadExact(request.exactEntities);const auto document=wolf2009::DecodeEntities4(entsBytes);
    // The decoder qualifies all folded ENTS names as unique, the relay's own
    // key/target and the exact OnUsed -> type19 -> type59 chain.
    const auto greeterBrain=ReadExact(prefix+"brai/"+GreeterBrain);
    out->plan=wolf2009::DecodeTrainDoorSignalH1023(greeterBrain,GreeterBrain,RelayName,document);
    auto& plan=out->plan;
    // OnUsed roots in file order: 3 train_stop, 4 player_leaves_train, 5 player_safe.
    out->onUsedRoots=wolf2009::DecodeSourceOnUsedRootsH1023(greeterBrain);
    {const auto& roots=out->onUsedRoots;const char* keys[3]={"train_stop","player_leaves_train","player_safe"};const std::uint32_t ids[3]={3,4,5};
        Need(roots.size()==3,"Greeter OnUsed root profile differs");
        for(unsigned i=0;i<3;++i)Need(roots[i].node==ids[i]&&roots[i].key==keys[i]&&roots[i].outputFlags.size()==1&&roots[i].outputFlags[0]==16&&roots[i].outputPrimaryCondition[0],
            "Greeter OnUsed root profile differs");
        Need(roots[1].outputTargets[0]==10&&roots[1].outputAux[0]==3,"Greeter player_leaves_train root no longer enters node 10");}
    // Hosted network data: the same exact BRAI bytes, decoded once.
    out->graph=wolf2009::DecodeBrainGraph(greeterBrain);Need(out->graph.format==wolf2009::BrainGraph::H1023,"Greeter hosted network requires the measured H1023 envelope");
    Need(plan.ownerEntityName=="train_greeter"&&plan.ownerClassname=="trainyard_greeter"&&plan.useKey=="train_stop"&&plan.onUsedNodeId==3&&plan.voiceNodeId==29&&plan.actorStartNodeId==35&&
        plan.voiceDefinition=="scripted_voice_kc1_trn"&&plan.actorAnimation=="srp_tyd_nml_train_door_open"&&plan.targetEntityName=="train_door_1"&&plan.targetClassname=="env_train_door",
        "Greeter route decoded a different original relay/OnUsed/type19/actor/door chain");
    // Node 35 outputs in file order: probes (32) to 6/Say, 33/UseTarget, 72/UseTarget
    // and one unconditioned normal output (16) to node 10 GoToAndFaceTarget.
    {const std::uint32_t nodes[4]={6,33,10,72},types[4]={12,68,243,68},ports[4]={32,32,16,32};Need(plan.unsupportedOutputs.size()==4,"Greeter node 35 output profile differs");
        for(unsigned i=0;i<4;++i){const auto& output=plan.unsupportedOutputs[i];Need(output.destinationNodeId==nodes[i]&&output.destinationNodeType==types[i]&&output.outputToken==ports[i],"Greeter node 35 output profile differs");}}
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
    // idAI Use reads noUnhideOnUse with default "0"; only the absent case is admitted.
    Need(!entity.Find("noUnhideOnUse")&&!greeter.Find("noUnhideOnUse"),"Greeter noUnhideOnUse is set; that idAI Use branch is not admitted");
    const auto model=set.Definitions().ResolveModelAnimations(Literal(entity,"model"),context);
    auto& resources=a.resources;resources.modelResource=request.greeterModel;resources.meshSkeletonResource=request.greeterRig;resources.meshSkeletonName=Literal(model.model,"skeleton");
    Need(render::ParseResourcePath(request.greeterModel).type=="modl"&&render::ParseResourcePath(request.greeterRig).type=="skel","Greeter route explicit greeter MODL/SKEL resources required");
    resources.geometryProfile=actor::GeometryProfile::Weighted64;resources.lodPolicy=actor::LodPolicy::Authored;resources.levelOfDetail=0;
    resources.expected[0]=Identity(ReadExact(request.greeterModel));resources.expected[1]=Identity(ReadExact(request.greeterRig));
    const auto animationPath=prefix+"hkxa/"+request.sourceMapName;const auto animationIdentity=Identity(ReadExact(animationPath));
    actor::ClipSelection clip;clip.logicalName=plan.actorAnimation;clip.animationResource=animationPath;clip.animationSkeletonResource=request.greeterRig;clip.clipName=ActorClip;
    clip.animationSkeletonName=ActorClipSkeleton;clip.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;clip.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;
    clip.expected[0]=animationIdentity;clip.expected[1]=resources.expected[1];resources.clips.push_back(clip);
    {const auto prefix0=wolf2009::rootmotion::CivilianUnarmedPrefixV1(Literal(entity,"model"));
        if(!prefix0.empty()){out->runClip=wolf2009::rootmotion::MoveClipNameV1(wolf2009::rootmotion::ResolveMoveStyleV1(0x11,false,false,false,false),prefix0,"","");
            out->stopClip=out->runClip+"_to_st1";out->locomotionHkxa="wolf2009/sp_global/hkxa/maps/game/global/global";const auto global=Identity(ReadExact(out->locomotionHkxa));
            for(const std::string name:{out->runClip,out->stopClip}){actor::ClipSelection s;s.logicalName=name;s.animationResource=out->locomotionHkxa;s.animationSkeletonResource=request.greeterRig;
                s.clipName="models/characters/_humanoid_male/"+name+".hkx";s.animationSkeletonName=ActorClipSkeleton;s.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;
                s.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;s.expected[0]=global;s.expected[1]=resources.expected[1];a.locomotionClips.push_back(s);}}
        const auto key=[&](const char* k,double fallback){return Number(entity,k,fallback);};
        out->turnRates.idle=key("maxTurnIdle",5.0);out->turnRates.alert=key("maxTurnAlert",4.5);out->turnRates.walking=key("maxTurnWalking",5.0);out->turnRates.running=key("maxTurnRunning",5.0);}
    auto& d=out->doorRequest;d.policy=WolfSourceAnimatedDoor_NativeControlledBoundSequenceV1;d.exactEntitiesPath=request.exactEntities;d.entityIndex=doorIndex;d.context=context;d.signal=plan;
    d.expectedEntitiesIdentityKnown=true;d.expectedEntitiesBytes=entityIdentity.bytes;d.expectedEntitiesFingerprint=entityIdentity.fingerprint;
    auto& doorResources=d.resources;doorResources.modelResource=prefix+"modl/"+DoorModel;doorResources.meshSkeletonResource=prefix+"skel/"+request.sourceMapName;doorResources.meshSkeletonName=DoorSkeleton;
    doorResources.geometryProfile=actor::GeometryProfile::WeightedOrRigid64;doorResources.lodPolicy=actor::LodPolicy::SingleResourceWithoutLodRecords;doorResources.levelOfDetail=0;
    doorResources.expected[0]=Identity(ReadExact(doorResources.modelResource));doorResources.expected[1]=Identity(ReadExact(doorResources.meshSkeletonResource));
    for(unsigned i=0;i<3;++i){actor::ClipSelection value;value.logicalName=DoorLogical[i];value.animationResource=animationPath;value.animationSkeletonResource=doorResources.meshSkeletonResource;
        value.clipName=DoorClips[i];value.animationSkeletonName=DoorSkeleton;value.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;
        value.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;value.expected[0]=animationIdentity;value.expected[1]=doorResources.expected[1];doorResources.clips.push_back(value);}
    Need(request.voiceReadinessProfile=="ScriptedVOStreamInWholeBankV1","Greeter route requires the explicit whole-bank type19 readiness profile");
    auto& state=out->state;state.relayName=plan.signalEntityName;state.greeterName=plan.ownerEntityName;state.doorName=plan.targetEntityName;state.forwardedUseKey=plan.useKey;
    state.onUsedNode=plan.onUsedNodeId;state.voiceNode=plan.voiceNodeId;state.actorStartNode=plan.actorStartNodeId;state.voiceDefinition=plan.voiceDefinition;
    state.actorAnimation=plan.actorAnimation;state.voiceProfile=request.voiceReadinessProfile;
    {// GoToAndFaceTarget goal of node 10: exact target_null placement.
        const auto& goal=document.entities[Unique(document,"run_to_ladder_point")];std::set<std::string> keys;for(const auto& field:goal.properties)keys.insert(field.key);
        Need(keys==std::set<std::string>({"classname","name","origin","angle"})&&Instance(goal,"classname")=="target_null","Greeter node 10 goal is outside the measured target_null profile");
        const auto place=wolf2009::ReadEntityPlacement(goal);Need(place.hasOrigin&&place.hasAngle,"Greeter node 10 goal placement is absent");
        for(int i=0;i<3;++i)out->goalOrigin[i]=place.origin[i];out->goalAngle=place.angle;}
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
    if(p.hosted){// idAI PostSpawn resolves triggerStartMoveEnt once (handle ai+0x1534).
        Need(gameLocal.FindEntity(p.plan.targetEntityName.c_str())==p.door->Entity(),"Hosted greeter triggerStartMoveEnt does not resolve to the bound door");p.triggerStartMoveEnt=p.door->Entity();
        Need(!gameLocal.FindEntity("run_to_ladder_point"),"Hosted greeter goal name is occupied");idDict args;args.Set("name","run_to_ladder_point");
        args.Set("origin",va("%g %g %g",p.goalOrigin[0],p.goalOrigin[1],p.goalOrigin[2]));args.SetFloat("angle",p.goalAngle);args.Set("noclipmodel","1");args.Set("networkSync","0");
        std::unique_ptr<idEntity> goal(gameLocal.SpawnEntityType(idTarget::Type,&args));Need(goal&&goal->GetPhysics()->GetOrigin().Compare(idVec3(p.goalOrigin[0],p.goalOrigin[1],p.goalOrigin[2]),.0002f),"Hosted greeter goal target_null spawn failed");
        p.goalTarget=goal.release();}
    gameLocal.Printf("WOLF2009_SOURCE_GREETER_ROUTE_ASSEMBLED generation=%llu greeter=%s greeter_entity=%d door=%s door_entity=%d bind=%s actor_held=1 door_idle=1 readiness_profile=%s\n",
        static_cast<unsigned long long>(generation),p.plan.ownerEntityName.c_str(),p.actor->NativeEntity()->entityNumber,p.plan.targetEntityName.c_str(),p.door->Entity()->entityNumber,
        p.wagonName.c_str(),p.state.voiceProfile.c_str());
    return true;
}catch(const std::exception& error){reason=error.what();p.door.reset();p.actor.reset();p.signal.reset();p.state.actorSpawned=p.state.doorBound=false;return false;}}

bool WolfSourceGreeterRoute::Start(WolfSourceDirector* director,idStr& reason){reason.Clear();auto& p=*impl;try{
    Need(p.state.phase==WolfSourceGreeterRoutePhase::Prepared&&p.actor&&p.door&&director&&!p.nativeThink.GetEntity(),"Greeter route start is not assembled");
    if(p.hosted){WolfSourceDirectorState current;NeedReason(director->Query(current,reason),reason,"Greeter route director is unavailable");
        Need(current.phase==WolfSourceDirectorPhase::WaitingForArrival&&current.levelGeneration==p.state.levelGeneration&&current.authoredRelayUseKey==p.plan.useKey,"Greeter route director generation/relay key differs");
        p.state.incomingUseKey=current.rawIncomingUseKey;p.Owners();p.director=director;p.state.phase=WolfSourceGreeterRoutePhase::WaitingForRelay;return true;}
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
    if(!p.hosted&&(p.state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay||p.state.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness||p.state.phase==WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode))
        Need(p.nativeThink.GetEntity()!=NULL,"Greeter route Think lifetime expired");
    if(p.state.actorSpawned&&p.state.phase!=WolfSourceGreeterRoutePhase::Failed&&p.state.phase!=WolfSourceGreeterRoutePhase::Refused)p.Owners();
    out=p.state;return true;
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceGreeterRoute::Inspect(const wolf2009::TrainDoorUseEvent& event,idEntity* originalActor,idStr& reason)const{return impl->Admission(event,originalActor,reason);}

bool WolfSourceGreeterRoute::Cancel(idStr& reason){reason.Clear();auto& p=*impl;
    if(p.busy)return Refuse(reason,"Greeter route Cancel during Think");
    if(p.state.phase==WolfSourceGreeterRoutePhase::Cancelled)return true;
    if(!p.Active())return Refuse(reason,"Greeter route is already terminal");
    if(p.move.Active())p.move.Stop();
    const bool requested=bool(p.voice);p.ReleaseTicket(true);++p.state.cancellations;p.state.phase=WolfSourceGreeterRoutePhase::Cancelled;p.state.reason="Cancelled by its map owner";
    if(requested)p.state.voice=WolfScriptedVoiceReadinessStatus::Cancelled;
    if(auto* think=p.nativeThink.GetEntity())think->BecomeInactive(TH_THINK);return true;}

bool WolfSourceGreeterRoute::ReceiveKeyedUse(const std::string& key,idEntity* actor,WolfSourceAIUseReceipt& out,idStr& reason){
    out=WolfSourceAIUseReceipt();out.key=key;reason.Clear();auto& p=*impl;
    auto Refused=[&](const std::string& why){++p.state.keyedUseRefusals;out.reason=why;reason=why.c_str();
        gameLocal.Printf("WOLF2009_SOURCE_GREETER_USE_REFUSED key=%s reason=\"%s\"\n",key.c_str(),why.c_str());return false;};
    if(p.busy)return Refused("Greeter keyed Use during route Think");
    if(p.hosted)return Refused("SourceAIShellV1 owns the hosted greeter's Use; the route is not a receiver");
    if(!p.Active())return Refused(std::string("Greeter route is not running (")+Wolf2009_SourceGreeterRoutePhaseText(p.state.phase)+")");
    try{p.Owners();}catch(const std::exception& error){return Refused(std::string("Greeter receiver generation expired: ")+error.what());}
    if(!actor||actor->entityNumber<0||actor->entityNumber>=MAX_GENTITIES||gameLocal.entities[actor->entityNumber]!=actor)return Refused("Greeter keyed Use actor is not a live native entity");
    if(key.empty()||key.size()>256)return Refused("Greeter keyed Use key budget");
    // Notify (static 0x201ac7d0): a local-player user first updates an AI
    // member through a virtual whose meaning is not traced; it is omitted and
    // reported. The network then applies CanUseKey before the single latch.
    if(actor==gameLocal.GetLocalPlayer()){out.playerBookkeepingOmitted=true;++p.state.playerBookkeepingOmissions;}
    const int root=wolf2009::SourceCanUseKey(p.onUsedRoots,key);
    if(root<0)return Refused("Greeter CanUseKey refused: no OnUsed root accepts the key");
    const auto& accepted=p.onUsedRoots[std::size_t(root)];
    out.canUseKey=out.latched=true;out.root=accepted.node;out.destination=accepted.outputTargets[0];
    // A newer notification replaces the pending key (single latch).
    p.state.latchPending=true;p.state.latchConsumed=false;p.state.latchedKey=key;p.state.latchedRoot=accepted.node;p.state.latchedDestination=out.destination;
    p.state.latchTimeMs=gameLocal.time;p.state.latchActorName=actor->GetName();p.state.latchTransitionRefusals=0;++p.state.keyedUses;
    // Base Use then processes the activate event; idAI's activate handler
    // (unhide and other effects) has not been inspected.
    out.activateRefused=true;++p.state.activateRefusals;p.state.activateReason="idAI activate/unhide handler is not established; not performed";
    out.reason=p.state.activateReason;
    gameLocal.Printf("WOLF2009_SOURCE_GREETER_USE_LATCHED key=%s actor=%s root=%u destination=%u time=%d can_use_key=1 latched=1 activate=refused player_bookkeeping=%s\n",
        key.c_str(),actor->GetName(),out.root,out.destination,gameLocal.time,out.playerBookkeepingOmitted?"omitted":"n/a");
    return true;
}

int Wolf2009_LiveSourceGreeterRoutes(){return liveRoutes;}

// SPDX-License-Identifier: GPL-3.0-or-later
// QA observer for the production greeter route inside the ordinary trainyard
// source session. It drives only gameLocal.RunFrame; the route consumes the
// director's relay request from its own Think. No readiness is injected.
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
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
#include "NativeSourceMap.h"
#include "NativeSourceDirector.h"
#include "NativeSourceGreeterRoute.h"
#include "NativeSourceScriptedVoiceReadiness.h"
#include "NativeSourceScriptedActor.h"
#include "NativeSourceAnimatedDoor.h"
#include "NativeSourceMover.h"
#include "NativeSourcePlayer.h"
#include "SourceControlledEntity.h"
#include "SourceSession.h"

namespace {
unsigned checks=0,frames=0;
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){Need(ok,reason.IsEmpty()?fallback:reason.c_str());}
void Frame(){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);++frames;}
WolfSourceGreeterRouteState Route(const WolfSourceGreeterRoute& route){WolfSourceGreeterRouteState out;idStr why;NeedReason(route.Query(out,why),why,"Greeter route query failed");return out;}
WolfSourceDirectorState Director(const WolfSourceDirector& director){WolfSourceDirectorState out;idStr why;NeedReason(director.Query(out,why),why,"Source director query failed");return out;}
WolfSourceAnimatedDoorState Door(const WolfSourceAnimatedDoor& door){WolfSourceAnimatedDoorState out;idStr why;NeedReason(door.Query(out,why),why,"Bound door query failed");return out;}
WolfSourceScriptedActorState Actor(WolfSourceScriptedActor& actor){WolfSourceScriptedActorState out;idStr why;NeedReason(actor.Query(out,why),why,"Greeter actor query failed");return out;}
bool Flag(const char* path){idFile* file=fileSystem->OpenFileRead(path,false);if(file)fileSystem->CloseFile(file);return file!=NULL;}
// Same authored local frame check as the isolated door QA, now against the
// wagon of the running trainyard session.
void BoundPose(const WolfSourceAnimatedDoor& door,idEntity* parent){const auto a=wolf2009::ReadEntityPlacement(door.Facts().instance),b=wolf2009::ReadEntityPlacement(door.Facts().parentInstance);
    idVec3 authored(a.origin[0],a.origin[1],a.origin[2]),parentOrigin(b.origin[0],b.origin[1],b.origin[2]);idMat3 axis,parentAxis;
    for(int i=0;i<9;++i){axis[i/3][i%3]=a.rotation[i];parentAxis[i/3][i%3]=b.rotation[i];}const idVec3 local=(authored-parentOrigin)*parentAxis.Transpose();const idMat3 localAxis=axis*parentAxis.Transpose();
    const auto* render=parent->GetRenderEntity();const idVec3 expected=render->origin+local*render->axis;const idMat3 expectedAxis=localAxis*render->axis;const auto* physics=door.Entity()->GetPhysics();
    Need(door.Entity()->GetBindMaster()==parent,"Trainyard door lost its native wagon Bind");
    for(int i=0;i<3;++i)Need(std::abs(physics->GetOrigin()[i]-expected[i])<=.002f,"Trainyard door anchor differs from its authored wagon-local frame");
    for(int i=0;i<9;++i)Need(std::abs(physics->GetAxis()[i/3][i%3]-expectedAxis[i/3][i%3])<=.00002f,"Trainyard door orientation differs from its authored wagon-local frame");
    Need(physics->GetNumClipModels()==0&&physics->GetContents()==0,"Trainyard door invented source collision");}
void IdleDoor(const WolfSourceAnimatedDoor& door,idEntity* wagon){const auto state=Door(door);
    Need(state.phase==WolfSourceAnimatedDoor_Idle&&!state.activations&&!state.completions&&state.nativeBound,"Trainyard door left idle without an actor-issued activation");
    WolfSourceControlledState native;idStr why;NeedReason(Wolf2009_SourceControlledState(door.Entity(),native,why),why,"Trainyard door native state absent");
    Need(native.phase==WolfSourceControlled_Held&&native.clip=="idle"&&native.sourceFrame==0,"Trainyard door native animator left its initial idle pose");BoundPose(door,wagon);}
void HeldActor(WolfSourceScriptedActor& actor,unsigned spawnId){const auto state=Actor(actor);
    Need(state.phase==WolfSourceScriptedActorPhase::Held&&!state.startCommits&&state.nativeStartTimeMs==-1&&state.nativeSpawnId==spawnId,"Greeter actor started without the type19 transition");}
std::string Text(WolfScriptedVoiceReadinessStatus value){return Wolf2009_ScriptedVoiceReadinessText(value);}
struct Baseline {idVec3 wagon,door;};

void RouteCase(WolfSourceMapRuntime& map,WolfSourceGreeterRoute& route,WolfSourceDirector& director,idEntity* wagon,const Baseline& start){
    auto* door=route.Door();auto* actor=route.Actor();const auto initial=Route(route);
    unsigned guard=0;WolfSourceDirectorState arrival=Director(director);
    while(arrival.phase==WolfSourceDirectorPhase::WaitingForArrival){Need(++guard<=1600,"Source wagon did not arrive");Frame();arrival=Director(director);
        const auto state=Route(route);
        if(arrival.phase==WolfSourceDirectorPhase::WaitingForArrival)Need(state.phase==WolfSourceGreeterRoutePhase::WaitingForRelay&&!state.relayTakes&&!state.onUsedDeliveries,"Greeter OnUsed preceded the committed arrival");
        if(guard%64==0){IdleDoor(*door,wagon);HeldActor(*actor,initial.greeterSpawnId);}}
    Need(arrival.phase==WolfSourceDirectorPhase::PartialArrival&&arrival.arrivals==1&&arrival.relay==WolfSourceDelivery::Applied&&arrival.relayEvents==1&&
        arrival.rawIncomingUseKey=="train_Stop"&&arrival.authoredRelayUseKey=="train_stop"&&arrival.forwardedRelayUseKey=="train_stop"&&arrival.arrivalTimeMs==gameLocal.time,
        "Director did not deliver the qualified own-key relay on its arrival frame");
    const auto used=Route(route);
    Need(used.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness&&used.relayTakes==1&&used.onUsedDeliveries==1&&!used.refusals&&used.relayEventId==arrival.arrivalEventId&&
        used.levelGeneration==arrival.levelGeneration&&used.relayTimeMs==arrival.arrivalTimeMs&&used.onUsedTimeMs==used.relayTimeMs&&used.voiceRequestTimeMs==used.onUsedTimeMs,
        "Greeter route did not deliver OnUsed from the director's relay in the same arrival tick/generation/event");
    Need(used.incomingUseKey=="train_Stop"&&used.forwardedUseKey=="train_stop"&&used.onUsedKey=="train_stop"&&used.onUsedNode==3&&used.voiceNode==29&&used.actorStartNode==35,
        "Greeter route lost the raw incoming key, the relay's own key or the exact node chain");
    Need(used.signal==wolf2009::TrainDoorSignalStatus::VoicePreparationRequested&&used.voiceRequests==1&&used.voice==WolfScriptedVoiceReadinessStatus::Unsupported&&
        !used.voiceReason.empty()&&used.voiceDefinition=="scripted_voice_kc1_trn"&&used.voiceProfile=="ScriptedVOStreamInWholeBankV1"&&Wolf2009_LiveScriptedVoiceReadinessRequests()==1,
        "Greeter OnUsed did not hold at type19 node 29 with one explicit unsupported readiness request");
    Need(!used.actorStarts&&!used.doorActivations&&!used.doorCompletions&&!used.postReadinessExecuted,"Greeter route advanced past type19 without readiness");
    wolf2009::TrainDoorUseEvent relay;idEntity* original=NULL;bool pending=true;idStr why;
    Need(director.TakeRelayRequest(relay,original,pending,why)&&!pending&&!original,"Director relay request was not consumed exactly once by the route");
    // Effect-free admission predicate: the accepted event, and altered events,
    // are all refused now. None changes route, actor, door or provider state.
    wolf2009::TrainDoorUseEvent accepted;accepted.sourceEntityName=used.relayName;accepted.ownerEntityName=used.greeterName;accepted.useKey="train_stop";
    accepted.levelGeneration=used.levelGeneration;accepted.eventId=used.relayEventId;
    for(unsigned n=0;n<7;++n){auto event=accepted;idEntity* activator=wagon;
        if(n==1)event.useKey="train_Stop";if(n==2)++event.levelGeneration;if(n==3)event.ownerEntityName="train_greeter_missing";if(n==4)event.sourceEntityName="train_greeter_brainuse_2";
        if(n==5)event.eventId=0;if(n==6)activator=NULL;
        Need(!route.Inspect(event,activator,why)&&!why.IsEmpty(),"Greeter route admitted a replayed/mismatched/missing-target relay event");}
    const auto inspected=Route(route);Need(inspected.thinkFrames==used.thinkFrames&&inspected.voiceQueries==used.voiceQueries&&inspected.onUsedDeliveries==1&&inspected.signal==used.signal,
        "Greeter route Inspect/Query changed state");
    unsigned queries=inspected.voiceQueries;
    for(unsigned i=0;i<120;++i){Frame();const auto state=Route(route);
        Need(state.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness&&state.voice==WolfScriptedVoiceReadinessStatus::Unsupported&&state.voiceRequests==1&&state.voiceQueries==queries+1&&
            state.onUsedDeliveries==1&&!state.actorStarts&&!state.doorActivations,"Greeter route did not keep evaluating type19 without advancing");queries=state.voiceQueries;
        if(i%20==0){IdleDoor(*door,wagon);HeldActor(*actor,used.greeterSpawnId);}}
    IdleDoor(*door,wagon);HeldActor(*actor,used.greeterSpawnId);
    const idVec3 wagonDelta=wagon->GetPhysics()->GetOrigin()-start.wagon,doorDelta=door->Entity()->GetPhysics()->GetOrigin()-start.door;
    Need(wagonDelta.y>300.f,"Source wagon did not carry the bound door to train_stop");
    for(int i=0;i<3;++i)Need(std::abs(wagonDelta[i]-doorDelta[i])<=.002f,"Bound door displacement differs from the actual wagon displacement");
    // Cancellation releases the readiness request exactly once and stops Think.
    const auto beforeCancel=Route(route);Need(route.Cancel(why),why.IsEmpty()?"Greeter route Cancel failed":why.c_str());const auto cancelled=Route(route);
    Need(cancelled.phase==WolfSourceGreeterRoutePhase::Cancelled&&cancelled.cancellations==1&&cancelled.voiceReleases==1&&cancelled.voice==WolfScriptedVoiceReadinessStatus::Cancelled&&
        Wolf2009_LiveScriptedVoiceReadinessRequests()==0,"Greeter route cancellation did not release its type19 request once");
    Need(route.Cancel(why)&&Route(route).cancellations==1&&Route(route).voiceReleases==1,"Repeated Cancel released twice");
    for(unsigned i=0;i<30;++i)Frame();const auto after=Route(route);
    Need(after.thinkFrames==cancelled.thinkFrames&&after.voiceQueries==beforeCancel.voiceQueries&&after.phase==WolfSourceGreeterRoutePhase::Cancelled,"Cancelled greeter route kept running");
    IdleDoor(*door,wagon);HeldActor(*actor,used.greeterSpawnId);
    WolfSourceMapState mapState;NeedReason(map.Query(mapState,why),why,"Source map ownership changed after cancellation");
    common->Printf("WOLF2009_TRAINYARD_GREETER_ROUTE_STATE generation=%llu event=%llu arrival_time=%d relay_time=%d onused_time=%d type19_request_time=%d raw_key=%s relay_key=%s onused_key=%s nodes=%u>%u>%u readiness=%s queries=%u releases=%u wagon_dy=%.6f door_dy=%.6f\n",
        static_cast<unsigned long long>(used.levelGeneration),static_cast<unsigned long long>(used.relayEventId),arrival.arrivalTimeMs,used.relayTimeMs,used.onUsedTimeMs,used.voiceRequestTimeMs,
        used.incomingUseKey.c_str(),used.forwardedUseKey.c_str(),used.onUsedKey.c_str(),used.onUsedNode,used.voiceNode,used.actorStartNode,Text(beforeCancel.voice).c_str(),beforeCancel.voiceQueries,after.voiceReleases,wagonDelta.y,doorDelta.y);
    common->Printf("WOLF2009_TRAINYARD_GREETER_ROUTE_QA_OK checks=%u frames=%u real_session=1 relay_once=1 own_key=1 onused_node=3 type19_node=29 type19_readiness=unsupported pending_frames=120 actor_start_ack=0 door_activation=0 door_bound_ride=1 cancel_release_once=1 post_type19_executed=0 full_campaign=0\n",checks,frames);
}

void StaleCase(WolfSourceMapRuntime& map,WolfSourceGreeterRoute& route,WolfSourceDirector& director,idEntity* wagon){
    auto* door=route.Door();auto* greeter=route.Actor()->NativeEntity();const auto initial=Route(route);
    const int slot=greeter->entityNumber,oldId=gameLocal.GetSpawnId(greeter);const idStr name=greeter->GetName();delete greeter;
    idDict args;args.Set("classname","wolf2009_greeter_route_stale_qa");args.Set("name",name.c_str());args.Set("noclipmodel","1");args.Set("networkSync","0");args.SetInt("spawn_entnum",slot);
    std::unique_ptr<idEntity> replacement(gameLocal.SpawnEntityType(idTarget::Type,&args));
    Need(replacement&&gameLocal.GetSpawnId(replacement.get())!=oldId&&gameLocal.FindEntity(name.c_str())==replacement.get(),"Stale QA did not replace the greeter under its exact name");
    WolfSourceGreeterRouteState unused;idStr why;Need(!route.Query(unused,why)&&!why.IsEmpty(),"Greeter route accepted a same-name replacement as its captured greeter");
    unsigned guard=0;WolfSourceDirectorState arrival=Director(director);
    while(arrival.phase==WolfSourceDirectorPhase::WaitingForArrival){Need(++guard<=1600,"Source wagon did not arrive");Frame();arrival=Director(director);}
    Need(arrival.phase==WolfSourceDirectorPhase::PartialArrival&&arrival.relay==WolfSourceDelivery::Applied&&arrival.forwardedRelayUseKey=="train_stop","Director relay did not arrive in the stale case");
    const auto refused=Route(route);
    Need(refused.phase==WolfSourceGreeterRoutePhase::Refused&&refused.relayTakes==1&&refused.refusals==1&&!refused.onUsedDeliveries&&!refused.voiceRequests&&
        refused.signal==wolf2009::TrainDoorSignalStatus::AwaitingUse&&refused.relayTimeMs==arrival.arrivalTimeMs&&Wolf2009_LiveScriptedVoiceReadinessRequests()==0&&
        refused.reason.find("greeter")!=std::string::npos,"Greeter route did not refuse OnUsed for its stale greeter generation");
    wolf2009::TrainDoorUseEvent relay;idEntity* original=NULL;bool pending=true;Need(director.TakeRelayRequest(relay,original,pending,why)&&!pending,"Stale relay request was replayed");
    Need(gameLocal.FindEntity(name.c_str())==replacement.get()&&gameLocal.GetSpawnId(replacement.get())!=oldId,"Refusal touched the replacement greeter");
    WolfSourceControlledState native;Need(Wolf2009_SourceControlledState(door->Entity(),native,why)&&native.phase==WolfSourceControlled_Held&&native.clip=="idle","Stale case changed the door animator");
    // Missing target after refusal: externally removed door. The terminal
    // route neither rebinds nor reacts, and teardown remains ordinary.
    const int doorSlot=door->Entity()->entityNumber;delete door->Entity();Need(!door->Entity()&&!gameLocal.entities[doorSlot]&&!gameLocal.FindEntity(refused.doorName.c_str()),"Door removal failed");
    for(unsigned i=0;i<10;++i)Frame();const auto after=Route(route);
    Need(after.phase==WolfSourceGreeterRoutePhase::Refused&&after.thinkFrames==refused.thinkFrames&&!after.voiceRequests&&!after.actorStarts&&!after.doorActivations,"Terminal refused route kept running");
    WolfSourceAnimatedDoorState doorState;Need(!door->Query(doorState,why),"Door owner reported a deleted native door as alive");
    WolfSourceMapState mapState;NeedReason(map.Query(mapState,why),why,"Source map query failed after the terminal refusal");(void)wagon;
    replacement.reset();
    common->Printf("WOLF2009_TRAINYARD_GREETER_STALE_QA_OK checks=%u frames=%u stale_greeter_refused=1 relay_once=1 onused=0 type19_request=0 missing_door_detected=1 rebind=0 full_campaign=0\n",checks,frames);
}
}

extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardGreeterRouteQA(){checks=frames=0;try{
    auto* map=Wolf2009_SourceSessionMapRuntime();Need(map&&Wolf2009_HasSourceSession(),"Ordinary source trainyard session is absent");
    idStr why;WolfSourceMapState source;NeedReason(map->Query(source,why),why,"Source map state is absent");
    Need(source.spawned&&source.bodies==535&&source.entities==2238&&source.sourceMovers==1&&source.sourceStatics==3&&source.sourceRelays==1&&source.sourceDirectors==1&&source.sourceGreeterRoutes==1,
        "Measured source arrival/greeter assembly differs");
    idPlayer* player=gameLocal.GetLocalPlayer();Need(player&&Wolf2009_IsSourcePlayer(player)&&player->health==100,"Original source passenger is absent");
    auto* route=map->GreeterRoute();auto* director=map->Director();auto* wagon=map->PrimaryMover();
    Need(route&&director&&wagon&&route->Actor()&&route->Door()&&Wolf2009_LiveSourceGreeterRoutes()==1&&Wolf2009_LiveSourceScriptedActors()==1,"Greeter route owners are absent");
    const auto initial=Route(*route);
    Need(initial.phase==WolfSourceGreeterRoutePhase::WaitingForRelay&&initial.actorSpawned&&initial.doorBound&&!initial.relayTakes&&!initial.onUsedDeliveries&&!initial.voiceRequests&&
        initial.incomingUseKey=="train_Stop"&&initial.forwardedUseKey=="train_stop"&&initial.relayName=="train_greeter_brainuse_1"&&initial.greeterName=="train_greeter"&&initial.doorName=="train_door_1",
        "Greeter route did not start waiting for its exact relay");
    for(unsigned i=0;i<3;++i)Need(Route(*route).thinkFrames==initial.thinkFrames,"Greeter route Query advanced Think");
    Need(Wolf2009_LiveScriptedVoiceReadinessRequests()==0,"Readiness was requested before OnUsed");
    auto* actor=route->Actor();auto* door=route->Door();
    Need(gameLocal.FindEntity("train_greeter")==actor->NativeEntity()&&gameLocal.FindEntity("train_door_1")==door->Entity()&&door->Entity()->GetBindMaster()==wagon,"Greeter/door exact native names differ");
    const auto authored=wolf2009::ReadEntityPlacement(actor->SourceInstance());const idVec3 origin=actor->NativeEntity()->GetPhysics()->GetOrigin();
    for(int i=0;i<3;++i)Need(std::abs(origin[i]-authored.origin[i])<=.0002f,"Greeter native anchor differs from its authored placement");
    HeldActor(*actor,initial.greeterSpawnId);IdleDoor(*door,wagon);
    WolfSourceMoverState mover;NeedReason(Wolf2009_QuerySourceMover(wagon,mover,why),why,"Source wagon query failed");
    Need(mover.spawnDeliveries==1&&mover.phase==WolfSourceMover_Moving,"Source wagon is not moving after assembly");
    common->Printf("WOLF2009_TRAINYARD_GREETER_ROUTE_ASSEMBLY greeter_entity=%d greeter_spawn=%u door_entity=%d door_spawn=%u wagon_entity=%d generation=%llu actor_held=1 door_idle=1 door_bound=1\n",
        actor->NativeEntity()->entityNumber,initial.greeterSpawnId,door->Entity()->entityNumber,initial.doorSpawnId,wagon->entityNumber,static_cast<unsigned long long>(initial.levelGeneration));
    Baseline start;start.wagon=wagon->GetPhysics()->GetOrigin();start.door=door->Entity()->GetPhysics()->GetOrigin();
    if(Flag("fixtures/trainyard-greeter-stale.enabled"))StaleCase(*map,*route,*director,wagon);
    else RouteCase(*map,*route,*director,wagon,start);
    return 1;
}catch(const std::exception& error){common->Warning("WOLF2009_TRAINYARD_GREETER_ROUTE_QA_FAIL checks=%u frames=%u reason=%s",checks,frames,error.what());return 0;}}

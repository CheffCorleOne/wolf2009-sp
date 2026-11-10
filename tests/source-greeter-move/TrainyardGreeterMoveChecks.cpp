// SPDX-License-Identifier: GPL-3.0-or-later
// QA observer for SourceAIShellGreeterRouteV1 inside the ordinary trainyard
// session. It drives only gameLocal.RunFrame. The shell's greeter Think runs
// the hosted greeter network; readiness comes only from the native provider,
// the move start only from the leg transition and the door only from it.
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
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
#include "NativeSourceAIShell.h"
#include "SourceControlledEntity.h"
#include "SourceSession.h"

namespace {
unsigned checks=0,frames=0;
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){Need(ok,reason.IsEmpty()?fallback:reason.c_str());}
void Frame(){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);++frames;}
bool Flag(const char* path){idFile* file=fileSystem->OpenFileRead(path,false);if(file)fileSystem->CloseFile(file);return file!=NULL;}
WolfSourceGreeterRouteState Route(const WolfSourceGreeterRoute& route){WolfSourceGreeterRouteState out;idStr why;NeedReason(route.Query(out,why),why,"Hosted greeter route query failed");return out;}
WolfSourceDirectorState Director(const WolfSourceDirector& director){WolfSourceDirectorState out;idStr why;NeedReason(director.Query(out,why),why,"Source director query failed");return out;}
WolfSourceAnimatedDoorState Door(const WolfSourceAnimatedDoor& door){WolfSourceAnimatedDoorState out;idStr why;NeedReason(door.Query(out,why),why,"Bound door query failed");return out;}
WolfSourceScriptedActorState Actor(WolfSourceScriptedActor& actor){WolfSourceScriptedActorState out;idStr why;NeedReason(actor.Query(out,why),why,"Greeter actor query failed");return out;}
WolfSourceAIActorState Shell(const WolfSourceAIShell& shell){WolfSourceAIActorState out;idStr why;NeedReason(shell.Query("train_greeter",out,why),why,"Shell greeter query failed");return out;}
bool Has(const std::string& log,const char* entry){const std::string text=";"+log+";";return text.find(std::string(";")+entry+";")!=std::string::npos;}
void IdleDoor(const WolfSourceAnimatedDoor& door){const auto state=Door(door);
    Need(state.phase==WolfSourceAnimatedDoor_Idle&&!state.activations&&!state.completions,"Door left idle without the greeter move start");}
void HeldActor(WolfSourceScriptedActor& actor){const auto state=Actor(actor);
    Need(state.phase==WolfSourceScriptedActorPhase::Held&&!state.startCommits,"Greeter clip started without the move start");}

// First greeter Think: OnSpawn root 1 -> node 2 (IgnoreEnemiesSet), probes
// 7/61/68/21 refused as leaves, 2 -> 24 (SetIgnorePanic no-op), hold at 73.
void SpawnBranch(const WolfSourceGreeterRoute& route,const WolfSourceAIShell& shell){
    const auto s=Route(route);const auto g=Shell(shell);
    Need(s.hosted&&s.hostedThinks>=1&&g.hosted&&g.brainTicks==s.hostedThinks,"Shell greeter Think did not run the hosted network");
    Need(Has(s.transitionLog,"1>2")&&Has(s.transitionLog,"2>24")&&s.networkTransitions==2,"OnSpawn branch did not run 1>2>24");
    Need(s.ignoreEnemiesApplied==1&&s.ignoreEnemies&&s.ignorePanicNoops==1,"Node 2/24 handlers did not apply once");
    Need(Has(s.probeLog,"2>7:351:refused")&&Has(s.probeLog,"2>61:75:refused")&&Has(s.probeLog,"2>68:33:refused")&&Has(s.probeLog,"2>21:19:refused")&&s.probeRefusals==4,
        "Node 2 probe leaves were not each refused once");
    Need(s.activeNode==24&&s.holdSourceNode==24&&s.holdNode==73&&s.holdNodeType==52&&s.holdRefusals==1&&!s.holdReason.empty(),"OnSpawn branch did not hold at WatchTarget node 73");
    Need(s.phase==WolfSourceGreeterRoutePhase::WaitingForRelay&&!s.voiceRequests&&Wolf2009_LiveScriptedVoiceReadinessRequests()==0,"OnSpawn branch opened a readiness request");}

struct Arrived {WolfSourceDirectorState director;WolfSourceGreeterRouteState route;};
Arrived Arrive(WolfSourceGreeterRoute& route,WolfSourceDirector& director,WolfSourceAIShell& shell){
    unsigned guard=0;auto arrival=Director(director);
    while(arrival.phase==WolfSourceDirectorPhase::WaitingForArrival){Need(++guard<=1600,"Source wagon did not arrive");Frame();arrival=Director(director);
        if(arrival.phase==WolfSourceDirectorPhase::WaitingForArrival&&guard%64==0){IdleDoor(*route.Door());HeldActor(*route.Actor());
            const auto s=Route(route);Need(s.holdRefusals==1&&s.activeNode==24&&!s.relayTakes,"Held OnSpawn branch changed before arrival");}}
    Need(arrival.phase==WolfSourceDirectorPhase::PartialArrival&&arrival.relayEvents==1&&arrival.forwardedRelayUseKey=="train_stop","Director did not deliver its relay");
    // The shell's greeter Think precedes the director in entity order, so it
    // takes the relay request on its next Think (as SourceAIShellV1 does).
    if(!Route(route).relayTakes)Frame();
    const auto s=Route(route);const auto g=Shell(shell);
    Need(s.relayTakes==1&&s.relayEventId==arrival.arrivalEventId&&s.relayTimeMs>=arrival.arrivalTimeMs&&s.relayTimeMs<=arrival.arrivalTimeMs+gameLocal.msec&&
        g.relayDeliveries==1&&g.useNotifications==1,"Shell did not take and deliver the relay on its first Think after arrival");
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_ARRIVAL arrival=%d relay=%d onused=%d request=%d active=%u transitions=\"%s\" pending=%d key=%s\n",arrival.arrivalTimeMs,s.relayTimeMs,
        s.onUsedTimeMs,s.voiceRequestTimeMs,s.activeNode,s.transitionLog.c_str(),int(g.pendingUse),g.pendingKey.c_str());
    // The whole-bank provider may report Ready on its first Update, which the
    // same network Tick evaluates right after entering node 29.
    Need(Has(s.transitionLog,"3>29")&&(s.activeNode==29||(s.activeNode==10&&s.readyTimeMs==s.relayTimeMs)),"OnUsed root 3 did not enter node 29");
    Need(s.onUsedDeliveries==1&&s.onUsedTimeMs==s.relayTimeMs&&s.voiceRequests==1&&s.voiceRequestTimeMs==s.relayTimeMs,"Node 29 request was not opened in the relay Think");
    Need(s.onUsedKey=="train_stop"&&s.incomingUseKey=="train_Stop","Relay keys were not retained separately");
    Need(!g.pendingUse,"OnUsed root 3 did not consume the pending latch");
    Arrived out;out.director=arrival;out.route=s;return out;}

void MoveCase(WolfSourceMapRuntime& map,WolfSourceGreeterRoute& route,WolfSourceDirector& director,WolfSourceAIShell& shell,bool cancelAtEnd){
    auto* door=route.Door();auto* actor=route.Actor();const auto a=Arrive(route,director,shell);
    Need(a.route.voiceBacked&&(a.route.voice==WolfScriptedVoiceReadinessStatus::Pending||a.route.voice==WolfScriptedVoiceReadinessStatus::Ready),"Node 29 request is not the whole-bank request");
    unsigned guard=0;auto s=Route(route);
    while(s.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness){Need(++guard<=600,"Node 29 did not become Ready");Frame();s=Route(route);
        if(s.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness){Need(!s.readyConsumptions&&!s.node35Entered,"Node 29 advanced without Ready");IdleDoor(*door);HeldActor(*actor);}}
    // Same Think: 29 -> 35 (probes 6/33/72), 35 -> 10 (probe 58), scripted move,
    // leg transition, move start, actor clip commit and door activation.
    Need(s.phase==WolfSourceGreeterRoutePhase::ScriptedMoveActive&&s.readyConsumptions==1&&s.bankMembers==19&&Has(s.transitionLog,"29>35")&&Has(s.transitionLog,"35>10"),
        "Ready did not enter 35 and 10 in one Think");
    Need(Has(s.probeLog,"35>6:12:refused")&&Has(s.probeLog,"35>33:68:refused")&&Has(s.probeLog,"35>72:68:refused")&&Has(s.probeLog,"10>58:306:refused")&&
        s.node35ProbeRefusals==3&&s.probeRefusals==8,"Node 35/10 probe leaves were not each refused once");
    Need(s.node10Entered&&s.goalName=="run_to_ladder_point"&&s.goalResolved&&s.faceName=="sewer_trapdoor"&&!s.faceResolved&&s.movement=="CombatRun"&&s.styleBits==0x11&&
        s.range==20.f&&s.travelPolicy=="WalkAirV1"&&s.moveFailure.empty(),"Node 10 scripted move command differs");
    Need(s.startArea==6199&&s.goalArea==8040&&s.routeHops>0&&s.routeTravelTime>0,"Node 10 did not plan the native aas28 route 6199 -> 8040");
    const int t=s.readyTimeMs;
    Need(s.node10TimeMs==t&&s.legTransitions==1&&s.moveStarts==1&&s.moveStartTimeMs==t&&s.moveStartAnimation=="srp_tyd_nml_train_door_open"&&s.moveStartOverrideCleared&&
        s.actorStartCommits==1&&s.triggerStartMoveEntConsumed&&s.doorActivations==1&&s.doorActivationTimeMs==t&&!s.moveStartRefusals,"Move start did not play the clip and activate the door once in the node 10 Think");
    Need(!s.translationRefusal.empty()&&s.reachedAndFacingTrue==0,"Translation refusal or ReachedAndFacing differs");
    const auto started=Actor(*actor);Need(started.phase==WolfSourceScriptedActorPhase::Playing&&started.startCommits==1&&started.nativeStartTimeMs==t&&started.actualStartPoseEvaluated,
        "Greeter actor did not commit the move-start clip");
    const auto moving=Door(*door);Need(moving.phase==WolfSourceAnimatedDoor_Moving&&moving.activations==1&&moving.activationTimeMs==t,"Door did not start moving from the move start");
    // Door endpoint and end idle, observed once; nothing replays.
    guard=0;while(Route(route).doorCompletions==0){Need(++guard<=900,"Door did not reach its endpoint");Frame();
        const auto r=Route(route);Need(r.doorActivations==1&&r.moveStarts==1&&r.actorStartCommits==1,"Move start or door activation repeated");}
    s=Route(route);const auto done=Door(*door);
    Need(s.doorCompletions==1&&done.phase==WolfSourceAnimatedDoor_Completed&&done.completions==1&&done.endpointPaletteError<=.0002&&done.completionTimeMs>=done.nativeEndTimeMs,"Door endpoint/end-idle not observed once");
    WolfSourceControlledState native;idStr why;NeedReason(Wolf2009_SourceControlledState(door->Entity(),native,why),why,"Door native state absent");
    Need(native.phase==WolfSourceControlled_Held&&native.clip=="end_idle","Door native animator is not holding end_idle");
    guard=0;while(Actor(*actor).phase!=WolfSourceScriptedActorPhase::Completed){Need(++guard<=600,"Greeter move-start clip did not finish");Frame();}
    for(unsigned i=0;i<60;++i){Frame();const auto r=Route(route);
        Need(r.phase==WolfSourceGreeterRoutePhase::ScriptedMoveActive&&r.activeNode==10&&r.doorCompletions==1&&r.doorActivations==1&&r.moveStarts==1&&r.reachedAndFacingTrue==0&&
            r.voice==WolfScriptedVoiceReadinessStatus::Ready&&Wolf2009_LiveScriptedVoiceReadinessRequests()==1,"Node 10 state changed after the door completed");}
    s=Route(route);
    for(int i=0;i<3;++i)Need(std::abs(s.origin[i]-s.node10Origin[i])<=.0002f,"Greeter moved without an established translation");
    Need(s.reachedAndFacingEvaluations>=60,"ReachedAndFacing was not evaluated each Think");
    const char* label="move";
    if(cancelAtEnd){Need(route.Cancel(why),why.IsEmpty()?"Cancel at node 10 failed":why.c_str());const auto c=Route(route);
        Need(c.phase==WolfSourceGreeterRoutePhase::Cancelled&&c.voiceReleases==1&&Wolf2009_LiveScriptedVoiceReadinessRequests()==0,"Cancel at node 10 did not release the retained request once");
        const auto thinks=c.hostedThinks;for(unsigned i=0;i<10;++i)Frame();Need(Route(route).hostedThinks==thinks,"Cancelled hosted network kept running");}
    else label="teardown";
    WolfSourceMapState mapState;NeedReason(map.Query(mapState,why),why,"Source map ownership changed");
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_STATE generation=%llu event=%llu arrival=%d ready=%d move_start=%d door_complete=%d hops=%u first_time=%d travel_flags=%d transitions=\"%s\" probes=\"%s\" hold=%u reached_and_facing=%u/%u endpoint_error=%.6f\n",
        static_cast<unsigned long long>(s.levelGeneration),static_cast<unsigned long long>(s.relayEventId),a.director.arrivalTimeMs,s.readyTimeMs,s.moveStartTimeMs,s.doorCompletionTimeMs,
        s.routeHops,s.routeTravelTime,s.travelFlags,s.transitionLog.c_str(),s.probeLog.c_str(),s.holdNode,s.reachedAndFacingTrue,s.reachedAndFacingEvaluations,done.endpointPaletteError);
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_QA_OK checks=%u frames=%u case=%s real_session=1 hosted=1 node2=1 node29_ready_once=1 node35=1 node10=1 move_start_once=1 door_activation_once=1 door_end_idle_once=1 translation=refused full_campaign=0\n",checks,frames,label);}

// Cancel after the committed arrival, before the shell's greeter Think takes
// the relay: the shell still latches the key, the cancelled route opens no
// request, and nothing moves.
void CancelCase(WolfSourceGreeterRoute& route,WolfSourceDirector& director,WolfSourceAIShell& shell){
    unsigned guard=0;auto arrival=Director(director);
    while(arrival.phase==WolfSourceDirectorPhase::WaitingForArrival){Need(++guard<=1600,"Source wagon did not arrive");Frame();arrival=Director(director);}
    idStr why;const auto before=Route(route);
    if(before.relayTakes){Need(before.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness||before.phase==WolfSourceGreeterRoutePhase::ScriptedMoveActive,"Relay Think state differs");}
    Need(!before.relayTakes,"Shell took the relay in the arrival frame; the pre-OnUsed cancel window is absent");
    Need(route.Cancel(why),why.IsEmpty()?"Cancel before OnUsed failed":why.c_str());const auto c=Route(route);
    Need(c.phase==WolfSourceGreeterRoutePhase::Cancelled&&c.cancellations==1&&!c.voiceRequests&&Wolf2009_LiveScriptedVoiceReadinessRequests()==0,"Cancel before OnUsed differs");
    for(unsigned i=0;i<240;++i){Frame();const auto r=Route(route);
        Need(r.hostedThinks==c.hostedThinks&&!r.voiceRequests&&!r.node35Entered&&!r.moveStarts&&!r.doorActivations,"Cancelled hosted route advanced");
        if(i%40==0){IdleDoor(*route.Door());HeldActor(*route.Actor());}}
    const auto g=Shell(shell);Need(g.relayDeliveries==1&&g.pendingUse&&g.pendingKey=="train_stop","Shell did not keep the delivered latch after the route was cancelled");
    Need(route.Cancel(why)&&Route(route).cancellations==1,"Repeated Cancel counted twice");
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_QA_OK checks=%u frames=%u case=cancel real_session=1 hosted=1 cancelled_before_onused=1 type19_request=0 move_start=0 door_activation=0 latch_retained=1 full_campaign=0
",checks,frames);}

void StaleCase(WolfSourceGreeterRoute& route,WolfSourceDirector& director,WolfSourceAIShell& shell){
    auto* greeter=route.Actor()->NativeEntity();const int slot=greeter->entityNumber,oldId=gameLocal.GetSpawnId(greeter);const idStr name=greeter->GetName();delete greeter;
    idDict args;args.Set("classname","wolf2009_greeter_move_stale_qa");args.Set("name",name.c_str());args.Set("noclipmodel","1");args.Set("networkSync","0");args.SetInt("spawn_entnum",slot);
    std::unique_ptr<idEntity> replacement(gameLocal.SpawnEntityType(idTarget::Type,&args));
    Need(replacement&&gameLocal.GetSpawnId(replacement.get())!=oldId&&gameLocal.FindEntity(name.c_str())==replacement.get(),"Stale QA did not replace the greeter");
    WolfSourceAIActorState unused;idStr why;Need(!shell.Query("train_greeter",unused,why),"Shell accepted the replacement greeter");
    unsigned guard=0;auto arrival=Director(director);
    while(arrival.phase==WolfSourceDirectorPhase::WaitingForArrival){Need(++guard<=1600,"Source wagon did not arrive");Frame();arrival=Director(director);}
    for(unsigned i=0;i<30;++i)Frame();
    WolfSourceGreeterRouteState s;Need(!route.Query(s,why),"Route accepted a stale greeter");
    Need(Wolf2009_LiveScriptedVoiceReadinessRequests()==0&&gameLocal.FindEntity(name.c_str())==replacement.get(),"Stale greeter opened readiness or was rebound");
    IdleDoor(*route.Door());
    replacement.reset();
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_QA_OK checks=%u frames=%u case=stale real_session=1 hosted=1 stale_refused=1 onused=0 type19_request=0 door_activation=0 rebind=0 full_campaign=0\n",checks,frames);}
}

extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardGreeterMoveQA(){checks=frames=0;try{
    auto* map=Wolf2009_SourceSessionMapRuntime();Need(map&&Wolf2009_HasSourceSession(),"Ordinary source trainyard session is absent");
    idStr why;WolfSourceMapState source;NeedReason(map->Query(source,why),why,"Source map state is absent");
    Need(source.spawned&&source.sourceAIActors==5&&source.sourceGreeterRoutes==1&&source.sourceDirectors==1,"SourceAIShellGreeterRouteV1 assembly differs");
    auto* route=map->GreeterRoute();auto* director=map->Director();auto* shell=map->AIShell();
    Need(route&&director&&shell&&route->ShellHost()&&Wolf2009_LiveSourceGreeterRoutes()==1&&Wolf2009_LiveSourceScriptedActors()==1&&Wolf2009_LiveSourceAIActors()==5,"Hosted greeter owners are absent");
    auto* body=route->Actor()->NativeEntity();
    Need(shell->Body("train_greeter")==body&&gameLocal.FindEntity("train_greeter")==body&&shell->Head("train_greeter")&&shell->Head("train_greeter")->GetBindMaster()==body&&
        shell->AccessoryCount("train_greeter")>=1,"Shell head/accessory are not bound to the route's single greeter body");
    Need(gameLocal.FindEntity("run_to_ladder_point")&&!gameLocal.FindEntity("sewer_trapdoor"),"Node 10 goal/face native identities differ from the profile");
    const auto initial=Route(*route);Need(initial.hosted&&initial.greeterName=="train_greeter","Route is not hosted");
    // The greeter is visible from spawn; its first Think runs OnSpawn.
    unsigned guard=0;while(Route(*route).hostedThinks==0){Need(++guard<=4,"Hosted greeter network did not tick");Frame();}
    SpawnBranch(*route,*shell);
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_SPAWN active=%u hold=%u hold_type=%u probes=\"%s\" transitions=\"%s\" ignore_enemies=%d spawn_time=%d\n",
        Route(*route).activeNode,Route(*route).holdNode,Route(*route).holdNodeType,Route(*route).probeLog.c_str(),Route(*route).transitionLog.c_str(),int(Route(*route).ignoreEnemies),Route(*route).spawnRootTimeMs);
    if(Flag("fixtures/trainyard-greeter-move-stale.enabled"))StaleCase(*route,*director,*shell);
    else if(Flag("fixtures/trainyard-greeter-move-cancel.enabled"))CancelCase(*route,*director,*shell);
    else MoveCase(*map,*route,*director,*shell,!Flag("fixtures/trainyard-greeter-move-teardown.enabled"));
    return 1;
}catch(const std::exception& error){common->Warning("WOLF2009_TRAINYARD_GREETER_MOVE_QA_FAIL checks=%u frames=%u reason=%s",checks,frames,error.what());return 0;}}

// SPDX-License-Identifier: GPL-3.0-or-later
// QA observer for SourceAIShellGreeterRouteV1 inside the ordinary trainyard
// session. It drives only gameLocal.RunFrame. The shell's greeter Think runs
// the hosted greeter network; readiness comes only from the native provider,
// the move start only from the leg transition and the door only from it.
#include <cmath>
#include <cstdint>
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
#include "NativeSourceRotatingDoor.h"
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
WolfSourceRotatingDoorState Trapdoor(const WolfSourceRotatingDoor& door){WolfSourceRotatingDoorState out;idStr why;NeedReason(door.Query(out,why),why,"Trapdoor query failed");return out;}
bool Near(float a,float b,float e){return std::fabs(a-b)<=e;}
double Wrap180(double d){d=std::fmod(d,360.0);if(d>180.0)d-=360.0;else if(d<=-180.0)d+=360.0;return d;}
// SourceRotatingDoorOldV1 T1: assembly, one post-spawn self activate, the
// 1000 ms -100 deg pitch opening from the physics, open pose and hole trace.
void TrapdoorOpen(WolfSourceRotatingDoor& door){
    auto d=Trapdoor(door);auto* entity=door.Entity();
    Need(entity&&gameLocal.FindEntity("sewer_trapdoor")==entity&&d.entityNumber==entity->entityNumber&&Wolf2009_LiveSourceRotatingDoors()==1,"Native sewer_trapdoor identity differs");
    Need(d.profile=="SourceRotatingDoorOldV1"&&d.sourceClass=="func_door_rotating_old"&&d.entityIndex==2136&&d.renderTriangles==12&&
         d.nativeModel.size()>15&&d.nativeModel.compare(d.nativeModel.size()-15,15,"#sewer_trapdoor")==0,"Trapdoor class, ENTS index or PROC submodel differs");
    Need(Near(d.origin[0],-288,.001f)&&Near(d.origin[1],576,.001f)&&Near(d.origin[2],310,.001f),"Trapdoor physics origin is not the hinge");
    const float closed[6]={-288,544,308,-224,608,312},model[6]={0,-32,-2,64,32,2};
    for(int i=0;i<6;++i)Need(Near(d.closedBounds[i],closed[i],.01f)&&Near(d.modelBounds[i],model[i],.01f),"Trapdoor closed clip bounds or PROC bounds differ");
    Need(d.closedTraceFraction<1&&d.closedTraceEntity==d.entityNumber,"Hole trace was not blocked by the closed trapdoor at assembly");
    Need(d.contents==(CONTENTS_SOLID|CONTENTS_OPAQUE)&&d.pusher&&d.clipIsTraceModel&&d.contentsProfile=="HingedSolidContentsV1"&&d.pushProfile=="ParametricPushStandInV1"&&
         d.portalReceipt=="AreaPortalAbsentV1"&&d.keyReceipts=="noAutoClose:unconsumed-by-class;wait:unused-with-toggle","Trapdoor collision/portal/key receipts differ");
    Need(d.postSpawnDeliveries==1&&d.moveStarts==1&&d.activations==1&&d.durationMs==1000&&d.delta==-100&&d.target==-100&&d.speed==-100&&
         d.moveEndTimeMs==d.moveStartTimeMs+1000&&d.moveStartTimeMs==d.spawnTimeMs,"Post-spawn self activate did not start one -100 deg / 1000 ms opening");
    Need(d.soundReceipts=="snd_open=door_default_open:SourceSoundBindingAbsentV1","Trapdoor snd_open receipt differs");
    unsigned guard=0;while(Trapdoor(door).openCompletions==0){Need(++guard<=200,"Trapdoor opening did not complete");Frame();
        const auto r=Trapdoor(door);Need(r.moveStarts==1&&r.failure.empty(),"Trapdoor restarted or failed while opening");
        if(!r.openCompletions)Need(r.localAngles[0]<=0.001f&&r.localAngles[0]>=-100.001f&&r.doorState=="opening","Trapdoor pitch left its opening sweep");}
    d=Trapdoor(door);
    Need(d.openCompletionTimeMs>=d.moveEndTimeMs&&d.openCompletionTimeMs<d.moveEndTimeMs+gameLocal.msec&&d.physicsEndTimeMs==d.moveEndTimeMs&&d.physicsReachedAngEvents==1,"Trapdoor open completion is not the first Think at the physics end");
    Need(d.doorState=="open"&&d.openCompletions==1&&!d.closeCompletions&&d.physicsAtRest&&d.thinking&&Near(d.localAngles[0],-100,.001f)&&Near(d.localAngles[1],0,.001f)&&Near(d.localAngles[2],0,.001f),
        "Trapdoor did not end open at pitch -100 and keep thinking");
    for(int i=0;i<6;++i)Need(Near(d.bounds[i],d.expectedOpenBounds[i],.01f),"Trapdoor open clip bounds differ from the rotated HingedSolidBoxV1");
    Need(d.traceEntity!=d.entityNumber,"Hole trace is still blocked by the trapdoor after opening");
    idStr why;Need(!door.DeliverPostSpawn(why)&&Trapdoor(door).duplicatePostSpawns==1&&Trapdoor(door).moveStarts==1,"Repeated post-spawn delivery was not refused without effect");
    const auto thinks=d.thinkFrames;for(unsigned i=0;i<10;++i)Frame();d=Trapdoor(door);
    Need(d.thinkFrames>=thinks+10&&d.openThinkFrames>=10&&Near(d.localAngles[0],-100,.001f)&&d.doorState=="open"&&d.moveStarts==1,"Open trapdoor stopped thinking or moved");
    common->Printf("WOLF2009_TRAINYARD_TRAPDOOR_QA entity=%d model=%s triangles=%u start=%d end=%d complete=%d angle=%.4f open=(%.2f %.2f %.2f)-(%.2f %.2f %.2f) hole_closed=%.3f/%d hole_open=%.3f/%d sound=\"%s\" portal=%s\n",
        d.entityNumber,d.nativeModel.c_str(),d.renderTriangles,d.moveStartTimeMs,d.moveEndTimeMs,d.openCompletionTimeMs,d.localAngles[0],d.bounds[0],d.bounds[1],d.bounds[2],d.bounds[3],d.bounds[4],d.bounds[5],
        d.closedTraceFraction,d.closedTraceEntity,d.traceFraction,d.traceEntity,d.soundReceipts.c_str(),d.portalReceipt.c_str());}
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
    Need(s.node10Entered&&s.goalName=="run_to_ladder_point"&&s.goalResolved&&s.faceName=="sewer_trapdoor"&&s.faceResolved&&s.movement=="CombatRun"&&s.styleBits==0x11&&
        s.range==20.f&&s.travelPolicy=="WalkAirV1"&&s.moveFailure.empty(),"Node 10 scripted move command differs");
    Need(s.startArea==6199&&s.goalArea==8040&&s.routeHops>0&&s.routeTravelTime>0,"Node 10 did not plan the native aas28 route 6199 -> 8040");
    const int t=s.readyTimeMs;
    Need(s.node10TimeMs==t&&s.legTransitions==1&&s.moveStarts==1&&s.moveStartTimeMs==t&&s.moveStartAnimation=="srp_tyd_nml_train_door_open"&&s.moveStartOverrideCleared&&
        s.actorStartCommits==1&&s.triggerStartMoveEntConsumed&&s.doorActivations==1&&s.doorActivationTimeMs==t&&!s.moveStartRefusals,"Move start did not play the clip and activate the door once in the node 10 Think");
    Need(s.translationRefusal.empty()&&s.rootMotionProfile=="DoorOpenRootMotionV1"&&s.rootMotionRefusal.empty()&&s.reachedAndFacingTrue==0,
        "Move start did not admit the door-open reference frame");
    for(int i=0;i<3;++i)Need(s.rootMotionStart[i]==s.node10Origin[i],"Root motion did not start from the node 10 origin");
    Need(std::abs(s.rootMotionExpectedEnd[0]-s.node10Origin[0]+76.45f)<=.01f&&std::abs(s.rootMotionExpectedEnd[1]-s.node10Origin[1]-82.03f)<=.01f&&
         std::abs(s.rootMotionExpectedEnd[2]-s.node10Origin[2])<=.001f,"Door-open reference frame end differs from AI-MOVE 2.4");
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
    // Frames 0..147 carry no motion: the body keeps its origin through clip time 6125 ms
    // (frame 147); linear sampling toward frame 148 moves it after that.
    guard=0;while(Actor(*actor).phase!=WolfSourceScriptedActorPhase::Completed){Need(++guard<=600,"Greeter move-start clip did not finish");Frame();
        const auto r=Route(route);const int clipMs=gameLocal.time-t;
        if(clipMs<=6125)for(int i=0;i<3;++i)Need(r.origin[i]==r.node10Origin[i],"Greeter moved before door-open frame 148");}
    guard=0;while(Route(route).rootMotionEndTimeMs<0){Need(++guard<=60,"Door-open root motion did not end");Frame();}
    s=Route(route);
    Need(s.rootMotionFirstMoveClipMs>6125&&s.rootMotionFirstMoveClipMs<=6125+50&&s.rootMotionMovingFrames>=20&&s.rootMotionMaxPathError<=.01&&
         s.rootMotionMaxStep<=12.0&&s.rootMotionDistance>=112.0,"Door-open root motion did not follow its reference frame from frame 148");
    for(int i=0;i<2;++i)Need(std::abs(s.origin[i]-s.rootMotionExpectedEnd[i])<=.05f,"Greeter did not end at the door-open reference-frame end");
    // SourceAIMoveV1 M2: run on aas28 from the door to run_to_ladder_point. The stop
    // starts on the first frame whose walk path seeks the goal directly within one
    // stop length (113.4 of 126.2 on original trainyard).
    guard=0;while(Route(route).arrivalTimeMs<0){Need(++guard<=2400,"Greeter run did not arrive");Frame();const auto r=Route(route);
        Need(r.runRefusal.empty()&&r.runPhase!="refused",("Greeter run refused: "+r.runRefusal).c_str());
        Need(r.doorActivations==1&&r.moveStarts==1&&r.reachedAndFacingTrue==0,"Move start, door or ReachedAndFacing changed while running");}
    s=Route(route);
    Need(s.runClip=="std_nil_nml_run"&&s.stopClip=="std_nil_nml_run_to_st1"&&s.turnRateRunning==420.0&&s.runFrames>=60&&s.floorSnaps==s.runFrames&&
         s.runMaxStep<=12.0&&s.runMaxTurnStep<=420.0*0.1,"Run clip, turn rate or per-frame steps differ");
    Need(s.stopStarts==1&&s.stopForward>126.0&&s.stopForward<126.5&&s.stopStartDistance<=s.stopForward&&s.stopStartDistance>=s.stopForward-24.0,
         "ApproachGoalStopV1 did not start one stop length from the goal");
    Need(s.arrivalDistance2D>=0&&s.arrivalDistance2D<20.0&&s.reachedPosts==1&&s.faceRefusal.empty()&&s.reachedAndFacingTrue==0,
         "Arrival, reached or the FaceEntity start differs");
    common->Printf("WOLF2009_TRAINYARD_GREETER_RUN_QA run_frames=%u distance=%.3f max_step=%.3f areas=%u off_route=%u stop_distance=%.3f arrival_2d=%.3f origin=(%.2f %.2f %.2f) yaw=%.2f\n",
        s.runFrames,s.runDistance,s.runMaxStep,s.runAreasVisited,s.runOffRouteAreas,s.stopStartDistance,s.arrivalDistance2D,s.origin[0],s.origin[1],s.origin[2],s.yawCurrent);
    float arrived[3];for(int i=0;i<3;++i)arrived[i]=s.origin[i];
    // TRAPDOOR T3 / AI-MOVE 5.3: FaceEntity on the native trapdoor's physics
    // origin (the hinge), MOVE_FACE_ENTITY, idle-rate turn, 0.01 deg snap, StopMove.
    Need(s.faceProfile=="FaceTurnInPlaceV1"&&s.faceEntityCalls==1&&s.faceMoveCommand==2&&s.faceStartTimeMs==s.arrivalTimeMs&&s.turnRateIdle==180.0&&
         s.faceTarget[0]==-288&&s.faceTarget[1]==576&&s.faceTarget[2]==310,"FaceEntity did not start on the trapdoor hinge with MOVE_FACE_ENTITY");
    const double expectedYaw=RAD2DEG(std::atan2(double(s.faceTarget[1]-s.faceOrigin[1]),double(s.faceTarget[0]-s.faceOrigin[0])));
    Need(std::fabs(s.faceYawIdeal-expectedYaw)<.01&&std::fabs(s.faceYawIdeal+97.7)<6.0,"FaceEntity yaw_ideal is not toward the trapdoor physics origin");
    guard=0;while(Route(route).facingTimeMs<0){Need(++guard<=600,"FaceEntity did not reach FacingIdeal");Frame();const auto r=Route(route);
        Need(r.faceRefusal.empty()&&r.runPhase!="refused",("FaceEntity refused: "+r.faceRefusal).c_str());
        if(r.facingTimeMs<0)Need(r.reachedAndFacingTrue==0&&r.activeNode==10,"ReachedAndFacing became true while the face turn was pending");
        for(int i=0;i<3;++i)Need(r.origin[i]==arrived[i],"Greeter moved while turning in place");}
    s=Route(route);
    const double turn=std::fabs(Wrap180(s.faceYawIdeal-s.faceStartYaw));
    Need(s.faceStopMoves==1&&s.facingAtStop&&s.faceFinalYaw==s.faceYawIdeal&&s.yawCurrent==s.faceYawIdeal&&s.faceMaxTurnStep<=180.0*gameLocal.msec*0.001+1e-6,
         "FaceEntity did not snap at FacingIdeal and StopMove within the idle turn rate");
    Need(double(s.facingTimeMs-s.faceStartTimeMs)>=turn/180.0*1000.0-1.0&&double(s.facingTimeMs-s.faceStartTimeMs)<=turn/180.0*1000.0+2.0*gameLocal.msec,
         "FaceEntity turn time differs from the 180 deg/s idle rate");
    // Next greeter Think: ReachedAndFacing true from real origin/yaw, 10 -> 31 once.
    guard=0;while(!Route(route).node31Entered){Need(++guard<=4,"Node 10 -> 31 did not run after StopMove");Frame();}
    s=Route(route);
    Need(s.reachedAndFacingTrue==1&&s.reachedAndFacingTimeMs>s.facingTimeMs&&s.reachedAndFacingTimeMs<=s.facingTimeMs+2*gameLocal.msec&&s.node31TimeMs==s.reachedAndFacingTimeMs&&
         Has(s.transitionLog,"10>31")&&s.exitMoveStops==1&&s.node31WaitMs==100,"ReachedAndFacing or the 10 -> 31 transition differs");
    common->Printf("WOLF2009_TRAINYARD_GREETER_FACE_QA arrival=%d face_start=%d facing=%d reached_and_facing=%d node31=%d start_yaw=%.3f yaw_ideal=%.4f final_yaw=%.4f turn=%.3f max_step=%.3f frames=%u\n",
        s.arrivalTimeMs,s.faceStartTimeMs,s.facingTimeMs,s.reachedAndFacingTimeMs,s.node31TimeMs,s.faceStartYaw,s.faceYawIdeal,s.faceFinalYaw,turn,s.faceMaxTurnStep,s.faceFrames);
    // Node 31 holds on its first condition without a native contract; no timer advances it.
    const auto transitions=s.networkTransitions;
    for(unsigned i=0;i<60;++i){Frame();const auto r=Route(route);
        Need(r.activeNode==31&&r.networkTransitions==transitions&&r.reachedAndFacingTrue==1&&r.doorCompletions==1&&r.doorActivations==1&&r.moveStarts==1&&
            r.voice==WolfScriptedVoiceReadinessStatus::Ready&&Wolf2009_LiveScriptedVoiceReadinessRequests()==1,"Node 31 advanced or node 10 state changed");}
    s=Route(route);
    Need(s.phase==WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode&&s.holdSourceNode==31&&s.holdNode==31&&s.holdNodeType==30&&s.node31HoldReason.compare(0,21,"ReachedLocation (100)")==0&&
         s.holdReason==s.node31HoldReason,"Node 31 did not hold with an explicit refusal");
    for(int i=0;i<3;++i)Need(s.origin[i]==arrived[i],"Greeter moved after arriving");
    Need(s.yawCurrent==s.faceFinalYaw,"Greeter turned after StopMove");
    const auto trap=Trapdoor(*map.RotatingDoor());Need(trap.doorState=="open"&&std::fabs(trap.localAngles[0]+100.f)<=.001f&&trap.moveStarts==1,"Trapdoor left its open pose");
    common->Printf("WOLF2009_TRAINYARD_GREETER_NODE31_HOLD node=%u wait_ms=%d time=%d reason=\"%s\"\n",s.holdNode,s.node31WaitMs,gameLocal.time,s.node31HoldReason.c_str());
    const char* label="move";
    if(cancelAtEnd){Need(route.Cancel(why),why.IsEmpty()?"Cancel at node 31 failed":why.c_str());const auto c=Route(route);
        Need(c.phase==WolfSourceGreeterRoutePhase::Cancelled&&c.voiceReleases==1&&Wolf2009_LiveScriptedVoiceReadinessRequests()==0,"Cancel at node 31 did not release the retained request once");
        const auto thinks=c.hostedThinks;for(unsigned i=0;i<10;++i)Frame();Need(Route(route).hostedThinks==thinks,"Cancelled hosted network kept running");}
    else label="teardown";
    WolfSourceMapState mapState;NeedReason(map.Query(mapState,why),why,"Source map ownership changed");
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_STATE generation=%llu event=%llu arrival=%d ready=%d move_start=%d door_complete=%d hops=%u first_time=%d travel_flags=%d transitions=\"%s\" probes=\"%s\" hold=%u reached_and_facing=%u/%u endpoint_error=%.6f\n",
        static_cast<unsigned long long>(s.levelGeneration),static_cast<unsigned long long>(s.relayEventId),a.director.arrivalTimeMs,s.readyTimeMs,s.moveStartTimeMs,s.doorCompletionTimeMs,
        s.routeHops,s.routeTravelTime,s.travelFlags,s.transitionLog.c_str(),s.probeLog.c_str(),s.holdNode,s.reachedAndFacingTrue,s.reachedAndFacingEvaluations,done.endpointPaletteError);
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_QA_OK checks=%u frames=%u case=%s real_session=1 hosted=1 node2=1 node29_ready_once=1 node35=1 node10=1 move_start_once=1 door_activation_once=1 door_end_idle_once=1 move_start_root_motion=1 run=arrived trapdoor_open=1 face=stopmove reached_and_facing=1 node31=held full_campaign=0\n",checks,frames,label);}

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
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_QA_OK checks=%u frames=%u case=cancel real_session=1 hosted=1 cancelled_before_onused=1 type19_request=0 move_start=0 door_activation=0 latch_retained=1 full_campaign=0\n",checks,frames);}

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
    Need(gameLocal.FindEntity("run_to_ladder_point")&&map->RotatingDoor()&&gameLocal.FindEntity("sewer_trapdoor")==map->RotatingDoor()->Entity()&&source.sourceRotatingDoors==1,
        "Node 10 goal/face native identities differ from the profile");
    const auto initial=Route(*route);Need(initial.hosted&&initial.greeterName=="train_greeter","Route is not hosted");
    // The greeter is visible from spawn; its first Think runs OnSpawn.
    unsigned guard=0;while(Route(*route).hostedThinks==0){Need(++guard<=4,"Hosted greeter network did not tick");Frame();}
    SpawnBranch(*route,*shell);
    common->Printf("WOLF2009_TRAINYARD_GREETER_MOVE_SPAWN active=%u hold=%u hold_type=%u probes=\"%s\" transitions=\"%s\" ignore_enemies=%d spawn_time=%d\n",
        Route(*route).activeNode,Route(*route).holdNode,Route(*route).holdNodeType,Route(*route).probeLog.c_str(),Route(*route).transitionLog.c_str(),int(Route(*route).ignoreEnemies),Route(*route).spawnRootTimeMs);
    TrapdoorOpen(*map->RotatingDoor());
    if(Flag("fixtures/trainyard-greeter-move-stale.enabled"))StaleCase(*route,*director,*shell);
    else if(Flag("fixtures/trainyard-greeter-move-cancel.enabled"))CancelCase(*route,*director,*shell);
    else MoveCase(*map,*route,*director,*shell,!Flag("fixtures/trainyard-greeter-move-teardown.enabled"));
    return 1;
}catch(const std::exception& error){common->Warning("WOLF2009_TRAINYARD_GREETER_MOVE_QA_FAIL checks=%u frames=%u reason=%s",checks,frames,error.what());return 0;}}

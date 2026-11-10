// SPDX-License-Identifier: GPL-3.0-or-later
// QA observer for the production trainyard exit slice inside the ordinary
// trainyard source session. It drives gameLocal.RunFrame with ordinary player
// movement commands only; arming, blocker removal, trigger firing and relay
// delivery come from the owners' own Think/touch paths.
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
#include "NativeSourceTrainyardExit.h"
#include "NativeSourceRegistry.h"
#include "NativeSourceScriptedActor.h"
#include "NativeSourceAnimatedDoor.h"
#include "NativeSourcePlayer.h"
#include "SourceSession.h"

namespace {
unsigned checks=0,frames=0;
// Clip bounds at the origin; absBounds carry the clip-link epsilon.
idBounds SourceBox(idEntity* e){const auto* physics=e->GetPhysics();return physics->GetBounds().Translate(physics->GetOrigin());}
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){Need(ok,reason.IsEmpty()?fallback:reason.c_str());}
bool Flag(const char* path){idFile* file=fileSystem->OpenFileRead(path,false);if(file)fileSystem->CloseFile(file);return file!=NULL;}
WolfSourceTrainyardExitState Exit(const WolfSourceTrainyardExit& exit){WolfSourceTrainyardExitState out;idStr why;NeedReason(exit.Query(out,why),why,"Trainyard exit query failed");return out;}
WolfSourceDirectorState Director(const WolfSourceDirector& d){WolfSourceDirectorState out;idStr why;NeedReason(d.Query(out,why),why,"Director query failed");return out;}
bool RouteState(const WolfSourceGreeterRoute& r,WolfSourceGreeterRouteState& out){idStr why;return r.Query(out,why);}
// Ordinary movement: forward/right commands steer the player towards goal in
// its current view frame. The view itself is not turned.
void Frame(idPlayer* player,const idVec2* goal){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));
    if(goal&&player){const idVec3 o=player->GetPhysics()->GetOrigin();idVec2 d((*goal).x-o.x,(*goal).y-o.y);const float len=d.Length();
        if(len>0.5f){d/=len;const float yaw=DEG2RAD(player->viewAngles.yaw);const idVec2 f(idMath::Cos(yaw),idMath::Sin(yaw)),r(idMath::Sin(yaw),-idMath::Cos(yaw));
            auto& c=commands[player->entityNumber];c.forwardmove=(signed char)idMath::ClampInt(-127,127,int(127.f*(d*f)));c.rightmove=(signed char)idMath::ClampInt(-127,127,int(127.f*(d*r)));}}
    gameLocal.RunFrame(commands);++frames;}
const WolfSourceTargetReceipt* Receipt(const WolfSourceTrainyardExitState& s,const char* source,const char* target,const char* op){
    const WolfSourceTargetReceipt* found=NULL;unsigned n=0;for(const auto& r:s.receipts)if(r.source==source&&r.target==target&&r.operation==op){found=&r;++n;}
    Need(n<=1,"A receipt was produced twice");return found;}
void Assembly(WolfSourceTrainyardExit& exit,idEntity* wagon){
    const auto s=Exit(exit);
    Need(s.shake==WolfSourceExitStage::Waiting&&s.tail==WolfSourceExitStage::Waiting&&s.shakeWaitRaw=="1 1"&&s.shakeWaitMs==1000&&s.waitRaw=="6000 0"&&s.waitMs==6000,"Exit owner did not start with the measured Wait durations");
    Need(s.exitTrigger.phase==WolfSourceTriggerPhase::Live&&!s.exitTrigger.triggerFirst&&s.exitTrigger.alive&&s.armedTrigger.phase==WolfSourceTriggerPhase::Dormant&&s.armedTrigger.triggerFirst&&
        s.dormantTrigger.phase==WolfSourceTriggerPhase::Dormant&&s.dormantTrigger.triggerFirst,"Trigger volumes did not assemble as live/dormant");
    Need(s.blockerAlive&&s.blockerBound&&exit.Blocker()&&exit.Blocker()->GetBindMaster()==wagon&&exit.Blocker()->GetPhysics()->GetContents()==CONTENTS_SOLID,"Doorway blocker is not solid and bound to the wagon");
    for(const char* name:{"exit_train_trigger","trigger_once_1","layer3_trigger_once_2"}){auto* t=exit.Trigger(name);
        Need(t&&gameLocal.FindEntity(name)==t&&t->GetPhysics()->GetContents()==CONTENTS_TRIGGER&&t->GetPhysics()->GetAxis()==mat3_identity,"Trigger volume native identity/contents/axis differs");}
    // exit_train_trigger: HKXS box half (4,48,56) at (196,232,352), no translation.
    const auto b=SourceBox(exit.Trigger("exit_train_trigger"));
    Need(std::fabs(b[0].x-192)<.01f&&std::fabs(b[1].x-200)<.01f&&std::fabs(b[0].y-184)<.01f&&std::fabs(b[1].y-280)<.01f&&std::fabs(b[0].z-296)<.01f&&std::fabs(b[1].z-408)<.01f,"exit_train_trigger box differs from its decoded HKXS body");
    Need(exit.Relay()&&gameLocal.FindEntity("train_greeter_brainuse_2")==exit.Relay()&&gameLocal.FindEntity("train_shaker")&&s.relayKey=="player_leaves_train"&&!s.relayActivations&&s.receipts.empty(),"Exit relay/scriptrunner did not assemble idle");
    common->Printf("WOLF2009_TRAINYARD_EXIT_ASSEMBLY blocker_abs=(%.3f %.3f %.3f)-(%.3f %.3f %.3f) exit_abs=(%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n",s.blockerMins[0],s.blockerMins[1],s.blockerMins[2],s.blockerMaxs[0],s.blockerMaxs[1],s.blockerMaxs[2],
        b[0].x,b[0].y,b[0].z,b[1].x,b[1].y,b[1].z);
}
struct Run {WolfSourceDirectorState arrival;WolfSourceTrainyardExitState armed,removed,fired;idVec3 blockerAtArrival;float closestGap=1e9f;unsigned blockedFrames=0,walkFrames=0;};
enum Case { MainCase,StaleCase,MissingCase };
// Arrival with the shake_train arming observed on the way.
void Ride(WolfSourceTrainyardExit& exit,WolfSourceDirector& director,idEntity* wagon,idPlayer* player,Run& run){
    const idVec3 offset=exit.Blocker()->GetPhysics()->GetOrigin()-wagon->GetPhysics()->GetOrigin();unsigned guard=0;bool armed=false;
    WolfSourceDirectorState d=Director(director);
    while(d.phase==WolfSourceDirectorPhase::WaitingForArrival){Need(++guard<=1600,"Source wagon did not arrive");Frame(player,NULL);d=Director(director);const auto s=Exit(exit);
        Need(!s.exitTrigger.fires&&!s.removals&&s.blockerAlive,"Exit fired or the blocker was removed during the ride");
        Need(!s.dormantTrigger.arms&&!s.dormantTrigger.fires,"Unarmed layer3_trigger_once_2 changed");
        const idVec3 now=exit.Blocker()->GetPhysics()->GetOrigin()-wagon->GetPhysics()->GetOrigin();Need((now-offset).LengthFast()<.01f,"Doorway blocker left its wagon-bound offset");
        if(!armed&&s.shake==WolfSourceExitStage::Held){armed=true;run.armed=s;}
        else if(!armed)Need(s.armedTrigger.phase==WolfSourceTriggerPhase::Dormant&&!s.shakeUses,"trigger_once_1 armed before the shake_train Timer");}
    Need(armed,"shake_train did not arm trigger_once_1 before the arrival");
    const auto& a=run.armed;
    Need(a.shakeUses==1&&a.shakeUseOutcome=="applied"&&a.shakeUseMs>a.shakeStartMs+1000&&a.shakeUseMs<=a.shakeStartMs+1000+40&&a.shakeStopNode==34&&a.shakeStopType==253&&a.shakeEnded&&!a.shakeStopReason.empty(),
        "shake_train node 19 did not Use trigger_once_1 once on the first tick after its 1000 ms Timer");
    // SourceBrainActionsV1 tail in the node 19 tick: 31 EnablePlayerWeapons,
    // 32 NoTargetSet(Player,"fasle"), 33 EnableCredits(false), 34 SetUndying(Player,false).
    {const unsigned nodes[4]={31,32,33,34};Need(a.shakeActions.size()==4,"shake_train tail did not enter nodes 31-34 once each");
        for(unsigned i=0;i<4;++i){const auto& r=a.shakeActions[i];Need(r.node==nodes[i]&&r.outcome=="applied"&&r.timeMs==a.shakeUseMs&&!r.probe,"shake_train tail action receipt differs");}
        Need(a.shakePlayer.lastWeaponEvent=="weapons-enabled"&&a.shakePlayer.weaponEventHandled&&!a.shakePlayer.weaponsLocked&&
             !player->fl.notarget&&a.shakePlayer.lastCreditsEvent=="disableCreditsScreen"&&a.shakePlayer.creditsEvents==1&&!a.shakePlayer.creditsHandled&&
             !a.shakePlayer.undying,"shake_train tail native effects differ");}
    Need(a.armedTrigger.arms==1&&a.armedTrigger.armTimeMs==a.shakeUseMs&&a.armedTrigger.armActivator=="train_shaker"&&!a.armedTrigger.triggerFirst,"trigger_once_1 was not armed by the shake_train Use");
    Need(d.phase==WolfSourceDirectorPhase::PartialArrival&&d.relay==WolfSourceDelivery::Applied&&d.arrivalTimeMs==gameLocal.time,"Director did not arrive and deliver its relay");
    run.arrival=d;Frame(player,NULL);const auto s=Exit(exit);
    Need(s.waitStartMs==d.arrivalTimeMs&&s.tail==WolfSourceExitStage::Waiting&&!s.removals,"movetrain node 7 Wait did not start at the relay tick");
    run.blockerAtArrival=exit.Blocker()->GetPhysics()->GetOrigin();
}
idVec2 Outside(const WolfSourceTrainyardExitState& s){return idVec2(s.blockerMins[0]-48.f,(s.blockerMins[1]+s.blockerMaxs[1])*.5f);}
// Inside the wagon first line up with the doorway, then walk out along -X.
idVec2 Steer(idPlayer* player,const idVec2& outside){const idVec3 o=player->GetPhysics()->GetOrigin();return std::fabs(o.y-outside.y)>3.f&&o.x>outside.x+40.f?idVec2(o.x,outside.y):outside;}
void PreRemoval(WolfSourceTrainyardExit& exit,idPlayer* player,Run& run){
    const auto initial=Exit(exit);const idVec2 goal=Outside(initial);
    {const idVec3 o=player->GetPhysics()->GetOrigin();const auto pb=SourceBox(player);
        common->Printf("WOLF2009_TRAINYARD_EXIT_WALK_START player=(%.3f %.3f %.3f) bounds=(%.3f %.3f %.3f)-(%.3f %.3f %.3f) yaw=%.3f goal=(%.3f %.3f) blocker=(%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n",
            o.x,o.y,o.z,pb[0].x,pb[0].y,pb[0].z,pb[1].x,pb[1].y,pb[1].z,player->viewAngles.yaw,goal.x,goal.y,initial.blockerMins[0],initial.blockerMins[1],initial.blockerMins[2],initial.blockerMaxs[0],initial.blockerMaxs[1],initial.blockerMaxs[2]);}
    // Walk into the doorway until 500 ms before the Timer can expire.
    while(gameLocal.time+16<initial.waitStartMs+initial.waitMs-500){const idVec2 step=Steer(player,goal);Frame(player,&step);++run.walkFrames;const auto s=Exit(exit);
        if(run.walkFrames%40==0){const idVec3 o=player->GetPhysics()->GetOrigin();common->Printf("WOLF2009_TRAINYARD_EXIT_WALK frame=%u player=(%.3f %.3f %.3f) gap=%.3f\n",run.walkFrames,o.x,o.y,o.z,SourceBox(player)[0].x-s.blockerMaxs[0]);}
        Need(s.blockerAlive&&!s.removals&&!s.exitTrigger.fires,"Blocker removed or exit fired before the measured Wait");
        const auto p=SourceBox(player);const float gap=p[0].x-s.blockerMaxs[0];if(gap<run.closestGap)run.closestGap=gap;
        Need(gap>=-.25f,"Player passed into the solid doorway blocker before its removal");if(gap<2.f)++run.blockedFrames;}
    Need(run.closestGap<2.f&&run.blockedFrames>=30,"Player never reached the doorway blocker with normal movement");
    common->Printf("WOLF2009_TRAINYARD_EXIT_BLOCKED walk_frames=%u blocked_frames=%u closest_gap=%.3f player_x=%.3f blocker_max_x=%.3f\n",run.walkFrames,run.blockedFrames,run.closestGap,
        player->GetPhysics()->GetOrigin().x,initial.blockerMaxs[0]);
}
void Removal(WolfSourceTrainyardExit& exit,WolfSourceGreeterRoute& route,idEntity* wagon,idPlayer* player,Run& run){
    unsigned guard=0;auto s=Exit(exit);
    while(!s.removals){Need(++guard<=120,"movetrain node 8 did not remove the blocker after its Wait");Frame(player,NULL);s=Exit(exit);}
    Need(s.removals==1&&s.removalOutcome=="applied"&&s.removalTimeMs>s.waitStartMs+6000&&s.removalTimeMs<=s.waitStartMs+6000+40&&s.removalTimeMs==gameLocal.time,
        "Blocker removal did not happen once on the first tick after the 6000 ms Timer");
    Need(!s.blockerAlive&&!exit.Blocker()&&!gameLocal.FindEntity("layer3_func_static_3")&&s.tail==WolfSourceExitStage::Held&&s.consoleNode==9&&s.consoleType==456&&s.consoleCommand=="CheckOneOfMany"&&!s.consoleReason.empty(),
        "RemoveTarget did not destroy exactly the blocker or node 9 was not held as a refusal");
    Need(wagon->GetPhysics()->GetClipModel()&&route.Door()&&route.Door()->Entity()&&route.Door()->Entity()->GetBindMaster()==wagon,"Blocker removal broke the wagon or its bound door");
    run.removed=s;
}
void WalkOut(WolfSourceTrainyardExit& exit,idPlayer* player,Run& run,const WolfSourceTrainyardExitState& geometry){
    const idVec2 goal=Outside(geometry);unsigned guard=0;auto s=Exit(exit);
    while(!s.exitTrigger.fires){Need(++guard<=400,"Player did not reach exit_train_trigger with normal movement");const idVec2 step=Steer(player,goal);Frame(player,&step);s=Exit(exit);}
    Need(s.exitTrigger.fires==1&&s.exitTrigger.fireTimeMs==gameLocal.time&&s.exitTrigger.fireActivator==player->GetName()&&s.exitTrigger.phase==WolfSourceTriggerPhase::Fired,"exit_train_trigger did not fire once for the player");
    const auto p=SourceBox(player);Need(p[0].x<=200.01f&&p[1].x>=192.f,"exit_train_trigger fired without the player overlapping its volume");
    run.fired=s;
    common->Printf("WOLF2009_TRAINYARD_EXIT_FIRED frames=%u time=%d player=(%.3f %.3f %.3f)\n",guard,s.exitTrigger.fireTimeMs,player->GetPhysics()->GetOrigin().x,player->GetPhysics()->GetOrigin().y,player->GetPhysics()->GetOrigin().z);
}
void Replay(WolfSourceTrainyardExit& exit,idPlayer* player,const Run& run,const WolfSourceTrainyardExitState& geometry){
    // Walk back in and out across the volume: wait -1 fires once, then the
    // trigger removes itself; nothing is delivered again.
    const idVec2 in(geometry.blockerMaxs[0]+40.f,(geometry.blockerMins[1]+geometry.blockerMaxs[1])*.5f),out=Outside(geometry);
    for(unsigned i=0;i<120;++i){const idVec2& g=(i/30)%2?out:in;Frame(player,&g);}
    const auto s=Exit(exit);
    Need(s.exitTrigger.fires==1&&!s.exitTrigger.alive&&!exit.Trigger("exit_train_trigger")&&!gameLocal.FindEntity("exit_train_trigger")&&s.relayActivations==run.fired.relayActivations&&
        s.greeterUses==run.fired.greeterUses&&s.receipts.size()==run.fired.receipts.size(),"exit_train_trigger fired or delivered again after its once-only activation");
    // Registry replays of the shake Use and the blocker removal are refused.
    WolfSourceEvent use;use.kind=WolfSourceEvent::UseTarget;use.sourceName="train_shaker";use.targetName="trigger_once_1";use.levelGeneration=run.armed.levelGeneration;use.eventId=1;use.sourceNodeId=19;
    idStr why;Need(exit.Inspect(use,why)==int(WolfSourceDelivery::AlreadyDelivered),"Replayed shake_train Use was not refused as already delivered");
    WolfSourceEvent removal;removal.kind=WolfSourceEvent::RemoveTarget;removal.sourceName="starting_boxcar_model";removal.targetName="layer3_func_static_3";removal.levelGeneration=run.armed.levelGeneration;
    removal.eventId=run.arrival.arrivalEventId;removal.sourceNodeId=8;Need(exit.Inspect(removal,why)==int(WolfSourceDelivery::AlreadyDelivered),"Replayed blocker removal was not refused as already delivered");
    auto variant=removal;variant.eventId+=7;Need(exit.Inspect(variant,why)==int(WolfSourceDelivery::ExpiredNativeTarget),"Removal of the deleted blocker was not refused as expired");
}
void Report(const WolfSourceTrainyardExitState& s,const char* label){
    for(const auto& r:s.receipts)common->Printf("WOLF2009_TRAINYARD_EXIT_RECEIPT case=%s source=%s target=%s op=%s outcome=%s time=%d\n",label,r.source.c_str(),r.target.c_str(),r.operation.c_str(),r.outcome.c_str(),r.timeMs);
    common->Printf("WOLF2009_TRAINYARD_EXIT_STATE case=%s shake_start=%d shake_use=%d arm_time=%d wait_start=%d removal_time=%d exit_fire=%d armed_trigger_fires=%u armed_trigger_dormant_touches=%u dormant_trigger_touches=%u dormant_trigger_arms=%u relay_activations=%u greeter_uses=%u receipts=%u\n",
        label,s.shakeStartMs,s.shakeUseMs,s.armedTrigger.armTimeMs,s.waitStartMs,s.removalTimeMs,s.exitTrigger.fireTimeMs,s.armedTrigger.fires,s.armedTrigger.dormantTouches,s.dormantTrigger.dormantTouches,s.dormantTrigger.arms,
        s.relayActivations,s.greeterUses,unsigned(s.receipts.size()));
}
// trigger_once_1 (armed) covers the wagon interior at the stop; whether and
// when it fires depends on the passenger overlapping it. Its four targets
// have no native implementation here and must appear as refusals.
void ArmedTrigger(const WolfSourceTrainyardExitState& s){
    Need(s.armedTrigger.fires<=1&&!s.dormantTrigger.arms&&!s.dormantTrigger.fires,"Armable triggers fired more than once or the unarmed one fired");
    if(!s.armedTrigger.fires)return;
    Need(s.armedTrigger.fireTimeMs>=s.armedTrigger.armTimeMs,"trigger_once_1 fired before arming");
    const char* names[4]={"nazi_interragator_1","nazi_interragator_2","train_greeter","layer38_target_lightswitch_1"};const char* outcomes[4]={"implementation-absent","implementation-absent","refused","implementation-absent"};
    for(unsigned i=0;i<4;++i){const auto* r=Receipt(s,"trigger_once_1",names[i],"activate");Need(r&&r->outcome==outcomes[i]&&r->timeMs==s.armedTrigger.fireTimeMs,"trigger_once_1 target receipt differs");}
}
void Teardown(WolfSourceMapRuntime& map){WolfSourceMapState state;idStr why;NeedReason(map.Query(state,why),why,"Source map ownership changed");
    Need(state.sourceExits==1&&state.sourceTriggers==3,"Source map exit ownership differs");}
}

extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardExitQA(){checks=frames=0;try{
    auto* map=Wolf2009_SourceSessionMapRuntime();Need(map&&Wolf2009_HasSourceSession(),"Ordinary source trainyard session is absent");
    idStr why;WolfSourceMapState source;NeedReason(map->Query(source,why),why,"Source map state is absent");
    Need(source.spawned&&source.entities==2238&&source.sourceGreeterRoutes==1&&source.sourceExits==1&&source.sourceTriggers==3,"Measured exit assembly differs");
    idPlayer* player=gameLocal.GetLocalPlayer();Need(player&&Wolf2009_IsSourcePlayer(player)&&player->health>0,"Original source passenger is absent");
    auto* exit=map->TrainyardExit();auto* route=map->GreeterRoute();auto* director=map->Director();auto* wagon=map->PrimaryMover();
    Need(exit&&route&&director&&wagon&&Wolf2009_LiveSourceTrainyardExits()==1&&Wolf2009_LiveSourceTriggers()==3,"Exit owners are absent");
    Assembly(*exit,wagon);
    const Case selected=Flag("fixtures/trainyard-exit-stale.enabled")?StaleCase:Flag("fixtures/trainyard-exit-missing.enabled")?MissingCase:MainCase;
    const char* label=selected==StaleCase?"stale-greeter":selected==MissingCase?"missing-relay":"exit";
    std::unique_ptr<idEntity> replacement;
    if(selected==StaleCase){auto* greeter=route->Actor()->NativeEntity();const int slot=greeter->entityNumber,oldId=gameLocal.GetSpawnId(greeter);const idStr name=greeter->GetName();delete greeter;
        idDict args;args.Set("classname","wolf2009_exit_stale_greeter_qa");args.Set("name",name.c_str());args.Set("noclipmodel","1");args.Set("networkSync","0");args.SetInt("spawn_entnum",slot);
        replacement.reset(gameLocal.SpawnEntityType(idTarget::Type,&args));Need(replacement&&gameLocal.GetSpawnId(replacement.get())!=oldId,"Stale QA did not replace the greeter");}
    Run run;Ride(*exit,*director,wagon,player,run);
    WolfSourceGreeterRouteState routeState;const bool routeAlive=RouteState(*route,routeState);
    if(selected==StaleCase)Need(routeAlive&&routeState.phase==WolfSourceGreeterRoutePhase::Refused,"Stale greeter route did not refuse its relay");
    else Need(routeAlive&&(routeState.phase==WolfSourceGreeterRoutePhase::WaitingForVoiceReadiness||routeState.phase==WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode),"Greeter route is not running after arrival");
    const bool bankBacked=routeState.voiceBacked;
    PreRemoval(*exit,player,run);
    // Bank-backed builds reach node 10 before the exit; the route then holds.
    if(selected==MainCase&&bankBacked){Need(RouteState(*route,routeState)&&routeState.phase==WolfSourceGreeterRoutePhase::HeldAtUnsupportedNode&&routeState.stopNode==10,"Bank-backed greeter did not hold at node 10 before the exit");}
    const auto geometry=Exit(*exit);
    Removal(*exit,*route,wagon,player,run);
    if(selected==MissingCase){auto* relay=exit->Relay();Need(relay!=NULL,"Relay absent before the missing case");delete relay;Need(!exit->Relay()&&!gameLocal.FindEntity("train_greeter_brainuse_2"),"Relay deletion failed");}
    WalkOut(*exit,player,run,geometry);
    const auto& f=run.fired;
    if(selected==MainCase){
        Need(f.relayActivations==1&&f.greeterUses==1,"exit_train_trigger did not activate the relay and deliver the greeter Use once");
        const auto* a=Receipt(f,"exit_train_trigger","train_greeter_brainuse_2","activate");const auto* g=Receipt(f,"train_greeter_brainuse_2","train_greeter","use");
        const auto* n=Receipt(f,"train_greeter_brainuse_2","nazi_interragator_1","use");
        Need(a&&a->outcome=="delivered"&&g&&g->outcome=="delivered"&&g->key=="player_leaves_train"&&g->actor==player->GetName()&&n&&n->outcome=="implementation-absent"&&n->key=="player_leaves_train",
            "Relay receipts differ: greeter Use with the relay's own key and the player, interrogator refused");
        Need(RouteState(*route,routeState)&&routeState.keyedUses==1&&routeState.latchPending&&routeState.latchedKey=="player_leaves_train"&&routeState.latchedRoot==4&&routeState.latchedDestination==10&&
            routeState.activateRefusals==1&&routeState.playerBookkeepingOmissions==1&&routeState.latchTimeMs==f.exitTrigger.fireTimeMs,"Greeter idAI Use did not latch root 4 once and refuse activate");
        Frame(player,NULL);Need(RouteState(*route,routeState)&&routeState.latchTransitionRefusals==1&&!routeState.latchConsumed&&routeState.latchPending,"Greeter Think did not evaluate the latched root once");
    }else if(selected==StaleCase){
        const auto* g=Receipt(f,"train_greeter_brainuse_2","train_greeter","use");const auto* n=Receipt(f,"train_greeter_brainuse_2","nazi_interragator_1","use");
        Need(f.relayActivations==1&&!f.greeterUses&&g&&g->outcome=="stale-skipped"&&n&&n->outcome=="implementation-absent","Stale greeter was not skipped while the relay continued");
        Need(gameLocal.FindEntity("train_greeter")==replacement.get(),"Stale case touched the replacement greeter");
    }else{
        const auto* a=Receipt(f,"exit_train_trigger","train_greeter_brainuse_2","activate");
        Need(a&&a->outcome=="target-expired"&&!f.relayActivations&&!f.greeterUses&&RouteState(*route,routeState)&&!routeState.keyedUses,"Missing relay was not reported as an expired target");
    }
    Replay(*exit,player,run,geometry);
    const auto last=Exit(*exit);ArmedTrigger(last);Teardown(*map);Report(last,label);
    common->Printf("WOLF2009_TRAINYARD_EXIT_TIMING arrival=%d shake_start=%d shake_use=%d wait_start=%d removal=%d removal_elapsed=%d exit_fire=%d bank_backed=%d route_phase=%s\n",
        run.arrival.arrivalTimeMs,last.shakeStartMs,last.shakeUseMs,last.waitStartMs,last.removalTimeMs,last.removalTimeMs-last.waitStartMs,last.exitTrigger.fireTimeMs,int(bankBacked),
        RouteState(*route,routeState)?Wolf2009_SourceGreeterRoutePhaseText(routeState.phase):"unavailable");
    replacement.reset();
    common->Printf("WOLF2009_TRAINYARD_EXIT_QA_OK checks=%u frames=%u case=%s real_session=1 shake_arm_once=1 blocker_blocked=1 blocker_removed_once=1 wait_unit_ms=1 exit_fire_once=1 relay_multi_target=1 greeter_use=%s interrogator=refused node9=refused node10=refused idai_activate=refused full_campaign=0\n",
        checks,frames,label,selected==MainCase?"latched":selected==StaleCase?"stale-skipped":"not-delivered");
    return 1;
}catch(const std::exception& error){common->Warning("WOLF2009_TRAINYARD_EXIT_QA_FAIL checks=%u frames=%u reason=%s",checks,frames,error.what());return 0;}}

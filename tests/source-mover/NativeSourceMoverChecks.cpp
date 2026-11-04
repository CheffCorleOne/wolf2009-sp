// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceMoverChecks.h"
#include "NativeSourceMover.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include "idlib/precompiled.h"
#include "game/Game_local.h"

namespace {
unsigned checks=0,frames=0;
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct CollisionOwner {
    std::string alias;cmHandle_t handle=0;mutable unsigned refs=0;bool valid=true;
};
void Retain(const void* raw){++static_cast<const CollisionOwner*>(raw)->refs;}
void Release(const void* raw){auto* value=static_cast<const CollisionOwner*>(raw);Need(value->refs>0,"Source mover QA collision ref underflow");--value->refs;}
bool Preflight(const void* raw,idStr& reason){const auto& owner=*static_cast<const CollisionOwner*>(raw);
    if(!owner.valid||idClipModel::CheckModel(owner.alias.c_str())!=owner.handle){reason="Own source-mover fixture collision identity expired";return false;}reason.Clear();return true;
}
void Frames(idPlayer* player,unsigned count){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));
    for(unsigned i=0;i<count;++i){gameLocal.RunFrame(commands);++frames;}}
WolfSourceMoverState State(const idEntity* entity){WolfSourceMoverState state;idStr why;
    Need(Wolf2009_QuerySourceMover(entity,state,why),why.IsEmpty()?"Native source mover query failed":why.c_str());return state;}
void PrintState(const char* stage,const WolfSourceMoverState& state){
    common->Printf("WOLF2009_SOURCE_MOVER_STATE stage=%s name=%s phase=%d origin=(%.9g,%.9g,%.9g) destination=(%.9g,%.9g,%.9g) velocity=(%.9g,%.9g,%.9g) spawn=%u duplicate=%u arrivals=%u consumed=%u blocked=%u begin_ms=%d end_ms=%d game_ms=%d reason=%s\n",
        stage,state.sourceName?state.sourceName:"",int(state.phase),state.origin[0],state.origin[1],state.origin[2],state.destination[0],state.destination[1],state.destination[2],
        state.velocity[0],state.velocity[1],state.velocity[2],state.spawnDeliveries,state.duplicateSpawnDeliveries,state.arrivals,state.consumedArrivals,state.blockedFrames,state.startTime,state.nativeEndTime,gameLocal.time,state.failure?state.failure:"");
}
struct Cleanup {
    idEntityPtr<idEntity> a,b;idEntityPtr<idPlayer> player;idVec3 origin,velocity;idAngles view,delta;
    explicit Cleanup(idPlayer* value):origin(value->GetPhysics()->GetOrigin()),velocity(value->GetPhysics()->GetLinearVelocity()),view(value->viewAngles),delta(value->GetDeltaViewAngles()){player=value;}
    ~Cleanup(){if(auto* value=a.GetEntity())delete value;if(auto* value=b.GetEntity())delete value;
        if(auto* value=player.GetEntity()){value->SetOrigin(origin);value->GetPlayerPhysics()->SetLinearVelocity(velocity);value->SetViewAngles(view);value->SetDeltaViewAngles(delta);}}
};
WolfSourceMoverRequest Request(CollisionOwner& owner,unsigned index){WolfSourceMoverRequest request;
    request.policy=WolfSourceMover_NativeDirectLinearPusherV1;request.placementPolicy=WolfSourceMoverPlacement_AuthoredUnboundWorldV1;
    request.visualPolicy=WolfSourceMoverVisual_NoneDiagnosticV1;request.dataPolicy=WolfSourceMoverData_OwnLooseFixtureV1;
    request.exactEntitiesPath="fixtures/source-mover/own.ents";request.exactBrainPath=index?"fixtures/source-mover/b.brai":"fixtures/source-mover/a.brai";
    request.entityIndex=index;request.worldOrigin[0]=index?2048.0f:1024.0f;
    request.collision.nativeModel=owner.alias.c_str();request.collision.context=&owner;request.collision.retain=Retain;request.collision.release=Release;request.collision.preflight=Preflight;
    request.collision.contents=CONTENTS_SOLID|CONTENTS_OPAQUE;request.collision.clipMask=MASK_SOLID;return request;
}
}
bool Wolf2009_CheckNativeSourceMoverQA(idPlayer* player,const char* alias){checks=frames=0;try{
    Need(player&&gameLocal.GameState()==GAMESTATE_ACTIVE&&!gameLocal.isClient&&!gameLocal.isMultiplayer,"Native source mover QA needs an active local SP player");
    CollisionOwner owner;owner.alias=alias?alias:"";owner.handle=idClipModel::CheckModel(owner.alias.c_str());Need(owner.handle>0,"Own source-mover native collision fixture was not preloaded");
    const int before=Wolf2009_LiveSourceMovers();Cleanup cleanup(player);idStr why;idEntity* native=NULL;
    auto aRequest=Request(owner,0),bRequest=Request(owner,1);
    auto invalid=aRequest;invalid.expectedEntitiesFingerprint=1;
    std::unique_ptr<WolfSourceMoverPrepared> refused(Wolf2009_PrepareSourceMover(invalid,why));
    Need(!refused&&!why.IsEmpty()&&owner.refs==0&&Wolf2009_LiveSourceMovers()==before,"Source mover rejected identity allocated native state");
    std::unique_ptr<WolfSourceMoverPrepared> a(Wolf2009_PrepareSourceMover(aRequest,why));Need(bool(a),why.IsEmpty()?"Own mover A preparation failed":why.c_str());
    std::unique_ptr<WolfSourceMoverPrepared> b(Wolf2009_PrepareSourceMover(bRequest,why));Need(bool(b),why.IsEmpty()?"Own mover B preparation failed":why.c_str());
    Need(owner.refs==2&&Wolf2009_LiveSourceMovers()==before,"Source mover preparation executed native Spawn or lost its collision owner");
    Need(Wolf2009_SpawnPreparedSourceMover(*a,native,why),why.IsEmpty()?"Own mover A Spawn failed":why.c_str());cleanup.a=native;
    Need(Wolf2009_SpawnPreparedSourceMover(*b,native,why),why.IsEmpty()?"Own mover B Spawn failed":why.c_str());cleanup.b=native;
    Need(Wolf2009_LiveSourceMovers()==before+2,"Native source mover count differs after independent Spawn");
    auto as=State(cleanup.a.GetEntity()),bs=State(cleanup.b.GetEntity());
    Need(as.phase==WolfSourceMover_Prepared&&bs.phase==WolfSourceMover_Prepared&&as.spawnDeliveries==0&&bs.spawnDeliveries==0,"Source mover construction synthesized OnSpawn");
    Need(as.nativePusher&&bs.nativePusher&&!as.collisionIsTraceModel&&!bs.collisionIsTraceModel&&as.visualDiagnostic&&!as.typedSourceResources,"Own compound-CM native pusher or diagnostic scope changed");
    Need(as.onSpawnNode==20&&as.moveNode==9&&as.unexecutedNodes==1&&as.moveOutputRecords==1&&as.sourceSpeed==25&&bs.sourceSpeed==40,"Native source mover lost retained startup/continuation facts");
    player->GetPlayerPhysics()->SetLinearVelocity(vec3_origin);player->SetOrigin(idVec3(1024,0,4));Frames(player,30);
    as=State(cleanup.a.GetEntity());bs=State(cleanup.b.GetEntity());
    Need(as.origin[1]==0&&bs.origin[1]==0&&as.spawnDeliveries==0,"Native source mover advanced before level event delivery");
    const idVec3 rider0=player->GetPhysics()->GetOrigin();Need(std::fabs(rider0.x-1024)<.5f&&std::fabs(rider0.y)<.5f&&rider0.z>=0&&rider0.z<1,"Native source mover QA rider did not settle on the actual own CM floor");
    Need(Wolf2009_DeliverSourceMoverOnSpawn(cleanup.a.GetEntity(),why),why.c_str());const int begin=gameLocal.time;
    Need(Wolf2009_DeliverSourceMoverOnSpawn(cleanup.a.GetEntity(),why),why.c_str());
    Frames(player,60);as=State(cleanup.a.GetEntity());bs=State(cleanup.b.GetEntity());const idVec3 rider1=player->GetPhysics()->GetOrigin();
    PrintState("moving-a",as);PrintState("inactive-b",bs);
    const float expected=25.0f*float(gameLocal.time-begin)*.001f;
    Need(as.phase==WolfSourceMover_Moving&&std::fabs(as.origin[1]-expected)<.03f&&as.spawnDeliveries==1&&as.duplicateSpawnDeliveries==1,"Native source event replayed its trajectory or lost source speed");
    Need(bs.phase==WolfSourceMover_Prepared&&bs.origin[1]==0&&bs.spawnDeliveries==0,"Native source movers share activation state");
    Need(std::fabs((rider1.y-rider0.y)-as.origin[1])<.75f&&std::fabs(rider1.x-rider0.x)<.5f&&std::fabs(rider1.z-rider0.z)<.5f,"Native compound source-mover pusher did not carry the actual player");
    Need(Wolf2009_DeliverSourceMoverOnSpawn(cleanup.b.GetEntity(),why),why.c_str());
    const int deadline=gameLocal.time+5000;while((State(cleanup.a.GetEntity()).phase!=WolfSourceMover_Arrived||State(cleanup.b.GetEntity()).phase!=WolfSourceMover_Arrived)&&gameLocal.time<deadline)Frames(player,1);
    as=State(cleanup.a.GetEntity());bs=State(cleanup.b.GetEntity());
    PrintState("endpoint-a",as);PrintState("endpoint-b",bs);
    Need(as.phase==WolfSourceMover_Arrived&&bs.phase==WolfSourceMover_Arrived&&as.origin[1]==64&&bs.origin[1]==64&&as.arrivals==1&&bs.arrivals==1,"Native source mover did not reach each original endpoint exactly once");
    bool pending=false;unsigned moveNode=0;Need(Wolf2009_TakeSourceMoverArrival(cleanup.a.GetEntity(),pending,moveNode,why)&&pending&&moveNode==9,"Native source mover arrival was not delivered with its source node");
    Need(Wolf2009_TakeSourceMoverArrival(cleanup.a.GetEntity(),pending,moveNode,why)&&!pending&&moveNode==0,"Native source mover arrival handoff replayed");
    Frames(player,8);Need(State(cleanup.a.GetEntity()).arrivals==1&&State(cleanup.b.GetEntity()).origin[1]==64,"Native source mover continued after arrival");
    delete cleanup.a.GetEntity();cleanup.a=NULL;delete cleanup.b.GetEntity();cleanup.b=NULL;
    Need(Wolf2009_LiveSourceMovers()==before&&owner.refs==2,"Native source mover destruction lost retained preparation ownership");
    // Reuse immutable preparation after the previous entity generation died.
    Need(Wolf2009_SpawnPreparedSourceMover(*a,native,why),why.c_str());cleanup.a=native;
    player->SetOrigin(cleanup.origin);player->GetPlayerPhysics()->SetLinearVelocity(vec3_origin);
    Need(Wolf2009_DeliverSourceMoverOnSpawn(cleanup.a.GetEntity(),why),why.c_str());owner.valid=false;Frames(player,2);
    as=State(cleanup.a.GetEntity());Need(as.phase==WolfSourceMover_Unsupported&&as.failure&&*as.failure&&as.arrivals==0,"Expired source collision owner did not halt the native mover");
    PrintState("stale-owner-stop",as);
    const float halted=as.origin[1];Frames(player,4);Need(State(cleanup.a.GetEntity()).origin[1]==halted,"Refused source mover kept running native physics");
    delete cleanup.a.GetEntity();cleanup.a=NULL;a.reset();b.reset();Need(owner.refs==0&&Wolf2009_LiveSourceMovers()==before,"Native source mover cleanup leaked entity/preparation collision owners");
    common->Printf("WOLF2009_NATIVE_SOURCE_MOVER_QA_OK checks=%u frames=%u native_idMover=1 compound_cm_pusher=1 passenger=1 explicit_spawn=1 duplicate_guard=1 independent=2 source_speed=1 exact_endpoint=1 arrival_once=1 stale_owner_stop=1 no_per_frame_warp=1 own_fixture=1 retail_mover_world_sweep=0 retail_clock=0\n",checks,frames);return true;
}catch(const std::exception& error){common->Warning("WOLF2009_NATIVE_SOURCE_MOVER_QA_FAIL checks=%u frames=%u reason=%s",checks,frames,error.what());return false;}}

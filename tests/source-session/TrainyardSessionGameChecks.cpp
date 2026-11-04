// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <stdexcept>
#include "NativeSourceMap.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceMover.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceWeapon.h"
#include "SourceSession.h"
namespace {
unsigned checks=0,frames=0;
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void Frame(){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);++frames;}
WolfSourceMoverState Mover(idEntity* mover){WolfSourceMoverState state;idStr why;Need(Wolf2009_QuerySourceMover(mover,state,why),why.IsEmpty()?"Source wagon state absent":why.c_str());Need(state.phase!=WolfSourceMover_Unsupported,state.failure?state.failure:"Source wagon refused simulation");return state;}
}
extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardSessionQA(){checks=frames=0;try{
    auto* map=Wolf2009_SourceSessionMapRuntime();Need(Wolf2009_HasSourceSession()&&map,"Ordinary trainyard did not install its source map runtime");
    idStr why;WolfSourceMapState source;Need(map->Query(source,why),why.IsEmpty()?"Source map state absent":why.c_str());
    Need(source.spawned&&source.bodies==535&&source.entities==2238&&source.instanceBodies==227&&source.resourceBodies==307&&source.sourceMovers==1&&source.collisionShapes==86&&source.sourceSkin,"Trainyard body/entity ownership or wagon geometry/skin differs");
    Need(source.omittedModulationSurfaces==3&&source.omittedModulationTriangles==100,"Source wagon visual compatibility omitted an unexpected surface profile");
    idPlayer* player=gameLocal.GetLocalPlayer();Need(player&&Wolf2009_IsSourcePlayer(player)&&player->health==100,"Trainyard ordinary reserved-slot source player missing");
    WolfSourcePlayerState profile;Need(Wolf2009_QuerySourcePlayer(player,profile,why)&&profile.standVertices==50&&profile.standEdges==144&&profile.standPolygons==96,"Trainyard original player hull missing");
    WolfSourceWeaponState weapon;Need(Wolf2009_QuerySourceWeapon(player,weapon,why)&&weapon.sourceWeapon&&idStr::Cmp(weapon.sourceWeapon,"weapon_none")==0&&!weapon.hasViewModel,"Trainyard source startup invented an equipped weapon");
    auto* wagon=map->PrimaryMover();Need(wagon&&gameLocal.FindEntity("starting_boxcar_model")==wagon,"Trainyard wagon missing from the native entity registry");
    auto state=Mover(wagon);Need(state.nativePusher&&!state.collisionIsTraceModel&&state.typedSourceResources&&state.callerEstablishedWorld&&!state.visualDiagnostic&&state.sourceSpeed==25&&state.spawnDeliveries==1,"Trainyard wagon did not use source direct/native compound-pusher profile");
    Need(state.authoredSkin&&idStr::Cmp(state.authoredSkin,"models/vehicles/box_car_full_in")==0&&state.nativeSkin&&state.nativeSkin[0],"Trainyard authored skin was not bound");
    common->Printf("WOLF2009_TRAINYARD_START player=(%.5f,%.5f,%.5f) wagon=(%.5f,%.5f,%.5f) native_end=%d time=%d\n",player->GetPhysics()->GetOrigin().x,player->GetPhysics()->GetOrigin().y,player->GetPhysics()->GetOrigin().z,state.origin[0],state.origin[1],state.origin[2],state.nativeEndTime,gameLocal.time);
    for(int i=0;i<24;++i)Frame();
    const idVec3 rider0=player->GetPhysics()->GetOrigin(),wagon0=wagon->GetPhysics()->GetOrigin();
    Need(rider0.z>294&&rider0.z<300,"Source player did not settle on the measured original wagon floor");
    for(int i=0;i<60;++i)Frame();
    const idVec3 rider1=player->GetPhysics()->GetOrigin(),wagon1=wagon->GetPhysics()->GetOrigin();
    common->Printf("WOLF2009_TRAINYARD_CARRY wagon_delta=(%.6f,%.6f,%.6f) rider_delta=(%.6f,%.6f,%.6f) player_z=%.6f\n",wagon1.x-wagon0.x,wagon1.y-wagon0.y,wagon1.z-wagon0.z,rider1.x-rider0.x,rider1.y-rider0.y,rider1.z-rider0.z,rider1.z);
    Need(wagon1.y-wagon0.y>10&&std::fabs((rider1.y-rider0.y)-(wagon1.y-wagon0.y))<.75f&&std::fabs(rider1.z-rider0.z)<.5f,"Native source wagon did not carry the player through actual contacts");
    const int started=state.startTime;Need(Wolf2009_DeliverSourceMoverOnSpawn(wagon,why),"Repeated source OnSpawn was not acknowledged");state=Mover(wagon);Need(state.startTime==started&&state.spawnDeliveries==1&&state.duplicateSpawnDeliveries==1,"Repeated source OnSpawn replayed movement");
    const int deadline=state.nativeEndTime+5000;
    while(Mover(wagon).phase==WolfSourceMover_Moving&&gameLocal.time<deadline&&frames<1400)Frame();state=Mover(wagon);
    Need(state.phase==WolfSourceMover_Arrived&&state.arrivals==1&&std::fabs(state.origin[1]-239.98f)<.01f&&std::fabs(state.origin[0]-256.38f)<.01f&&state.origin[2]==256,"Source train did not arrive at its authored target");
    bool pending=false;unsigned node=0;Need(Wolf2009_TakeSourceMoverArrival(wagon,pending,node,why)&&pending&&node==state.moveNode,"Native arrival did not expose the retained source continuation node");
    Need(Wolf2009_TakeSourceMoverArrival(wagon,pending,node,why)&&!pending,"Source arrival was delivered twice");
    Need(player->health==100&&player->GetPhysics()->GetOrigin().z>294&&player->GetPhysics()->GetOrigin().z<300,"Source passenger failed to remain on the arriving train");
    common->Printf("WOLF2009_TRAINYARD_SESSION_QA_OK checks=%u frames=%u normal_devmap=1 world_loaded=1 original_player=1 original_wagon_geometry=1 original_skin=1 native_pusher=1 carried=1 arrived=1 arrival_once=1 unimplemented_ents=%u later_brain_nodes=%u full_campaign=0\n",checks,frames,source.unimplementedEntities,state.unexecutedNodes);return 1;
}catch(const std::exception& error){common->Warning("WOLF2009_TRAINYARD_SESSION_QA_FAIL checks=%u frames=%u reason=%s",checks,frames,error.what());return 0;}}

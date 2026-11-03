// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceWeapon.h"
namespace {
unsigned checks=0,frames=0;
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void Frames(unsigned count,int forward=0,int right=0,int up=0){usercmd_t commands[MAX_CLIENTS];std::memset(commands,0,sizeof(commands));commands[0].forwardmove=forward;commands[0].rightmove=right;commands[0].upmove=up;commands[0].buttons=BUTTON_RUN;for(unsigned i=0;i<count;++i){gameLocal.RunFrame(commands);++frames;}}
WolfSourcePlayerState State(idPlayer* player){WolfSourcePlayerState s;idStr reason;Need(Wolf2009_QuerySourcePlayer(player,s,reason),reason.IsEmpty()?"Source player query failed":reason.c_str());return s;}
const idTraceModel& Hull(idPlayer* player){const auto* model=player->GetPlayerPhysics()->GetClipModel();Need(model!=NULL,"Source player lost native clip");const auto* trm=model->GetTraceModel();Need(trm!=NULL,"Source player clip is not a trace model");return *trm;}
std::unique_ptr<idEntity> Ceiling(const idVec3& origin){idDict args;args.Set("name","wolf_source_player_qa_ceiling");args.Set("noclipmodel","1");std::unique_ptr<idEntity> e(gameLocal.SpawnEntityType(idSpawnableEntity::Type,&args));Need(bool(e),"Own player QA ceiling allocation failed");
    std::unique_ptr<idClipModel> clip(new idClipModel(idTraceModel(idBounds(idVec3(-32,-32,0),idVec3(32,32,8)))));clip->Translate(origin+idVec3(0,0,48));e->GetPhysics()->SetClipModel(clip.get(),1.0f);clip.release();e->GetPhysics()->SetContents(CONTENTS_SOLID);return e;
}
}
bool Wolf2009_CheckNativeSourcePlayerQA(idPlayer* player){try{
    checks=frames=0;Need(player&&Wolf2009_IsSourcePlayer(player)&&player->entityNumber==gameLocal.localClientNum,"Normal native map startup did not allocate the reserved source player");
    const auto initial=State(player);Need(initial.exactLocalHull&&initial.bodyPlaceholder&&!initial.retailCameraImplemented,"Source player compatibility/fidelity flags differ");
    Need(initial.health==100&&initial.maximumHealth==100&&initial.mass==200,"Actual source player health/mass failed");
    Need(initial.selectedSourceWeapon==0&&initial.sourceOwnedWeapons==((1u<<0)|(1u<<1)|(1u<<11))&&initial.nativeInventoryWeapons==initial.sourceOwnedWeapons,"Trainyard startup incorrectly used default armed slots");
    Need(initial.standVertices==50&&initial.crouchVertices==50&&initial.standEdges==144&&initial.crouchEdges==144&&initial.standPolygons==96&&initial.crouchPolygons==96,"Actual source stand/crouch topology did not reach native prepared player");
    Need(player->inventory.armor==0&&player->inventory.maxarmor==0,"Source first-person player inherited stock Doom armor");
    player->SetViewAngles(ang_zero);player->SetDeltaViewAngles(ang_zero);Frames(30);const idVec3 start=player->GetPhysics()->GetOrigin();
    Need(std::abs(start.x)<.5f&&std::abs(start.y)<.5f&&start.z>-.5f&&start.z<1,"Actual source player did not settle on the original empty floor");
    Need(Hull(player).numVerts==50&&Hull(player).numEdges==144&&Hull(player).numPolys==96&&Hull(player).bounds[1][2]>70&&Hull(player).bounds[1][2]<71,"Standing source hull was replaced by a stock bounds box");
    Frames(150,127);const idVec3 wall=player->GetPhysics()->GetOrigin();Need(wall.x>170&&wall.x<172&&std::abs(wall.y)<.5f,"Actual50-support source player did not stop at the original empty X184 wall");
    Frames(24,0,127);const idVec3 slide=player->GetPhysics()->GetOrigin();Need(std::abs(slide.x-wall.x)<.5f&&std::abs(slide.y-wall.y)>15,"Source native player failed lateral sliding at the wall");
    player->GetPlayerPhysics()->SetLinearVelocity(vec3_origin);player->SetOrigin(start);Frames(8);
    Frames(24,0,0,-127);auto s=State(player);Need(s.ducked&&static_cast<idPhysics_Player*>(player->GetPlayerPhysics())->IsCrouching(),"Actual usercmd duck did not update source player/controller flags");
    Need(Hull(player).numVerts==50&&Hull(player).bounds[1][2]>39.9f&&Hull(player).bounds[1][2]<40.1f,"Source crouch used a resized standing box instead of the original separate hull");
    const float crouchEye=player->GetEyePosition().z-player->GetPhysics()->GetOrigin().z;Need(crouchEye>=29&&crouchEye<40,"Native eyeheight did not approach the explicit source30 target");
    {auto ceiling=Ceiling(player->GetPhysics()->GetOrigin());Frames(16);Need(State(player).ducked&&Hull(player).bounds[1][2]<41,"Player stood through an own native low ceiling");}
    Frames(24);Need(!State(player).ducked&&!static_cast<idPhysics_Player*>(player->GetPlayerPhysics())->IsCrouching()&&Hull(player).bounds[1][2]>70,"Source player did not restore exact standing hull after ceiling removal");
    const float standingEye=player->GetEyePosition().z-player->GetPhysics()->GetOrigin().z;Need(standingEye>60&&standingEye<=65,"Native source standing eyeheight did not approach64");
    const float floor=player->GetPhysics()->GetOrigin().z;Frames(8,0,0,127);const float jump=player->GetPhysics()->GetOrigin().z;Need(jump>floor+10,"Source native usercmd jump did not leave the floor");Frames(90);Need(std::abs(player->GetPhysics()->GetOrigin().z-floor)<.5f,"Source native player did not land back on the original floor");
    // Exercise native death events directly, avoiding an absent stock suicide
    // damage definition. This proves native lifecycle, not source damage rules.
    Frames(8,0,0,-127);player->health=0;player->Killed(player,player,100,vec3_origin,INVALID_JOINT);Frames(1);
    Need(player->health<=0&&bool(player->AI_DEAD)&&Hull(player).bounds[1][2]<41,"Native death did not retain the explicitly qualified source crouch hull");
    player->SpawnToPoint(start,ang_zero);Frames(12);s=State(player);Need(player->health==100&&!bool(player->AI_DEAD)&&!s.ducked&&s.nativeInventoryWeapons==initial.sourceOwnedWeapons&&Hull(player).bounds[1][2]>70,"Source player native respawn did not restore health/maploadout/exactstand");
    WolfSourceWeaponState weapon;idStr reason;Need(Wolf2009_QuerySourceWeapon(player,weapon,reason)&&weapon.sourceWeapon&&idStr::Cmp(weapon.sourceWeapon,"weapon_none")==0,"Native source respawn did not reset the source controller to none");
    player->GetPlayerPhysics()->SetLinearVelocity(vec3_origin);player->SetOrigin(start);player->SetViewAngles(ang_zero);player->SetDeltaViewAngles(ang_zero);Frames(4);
    printf("WOLF2009_NATIVE_SOURCE_PLAYER_QA_OK checks=%u frames=%u native_reserved_slot=1 source_health=100 source_mass=200 source_owned_slots=0,1,11 selected=none stand_vertices=50 crouch_vertices=50 edges=144 polygons=96 walk=1 wall_slide=1 duck=1 blocked_stand=1 jump=1 native_death_respawn=1 local_hull=1 body_placeholder=1 retail_keepDistance=0 floor_origin_z=%.7g wall_x=%.7g crouch_eye=%.7g standing_eye=%.7g\n",checks,frames,start.z,wall.x,crouchEye,standingEye);return true;
}catch(const std::exception& e){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_PLAYER_QA_FAILED reason=%s checks=%u frames=%u\n",e.what(),checks,frames);return false;}}

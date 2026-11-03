// SPDX-License-Identifier: GPL-3.0-or-later
#include "idlib/precompiled.h"
#include "GameplayRuntimeProbe.h"
#include "EngineGameplay.h"
#include "cm/CollisionModel.h"
#include <stdio.h>

namespace {
cmHandle_t nativeCollisionFixtureHandle=-1;
class NativeCollisionFixtureResource {
  public:
    NativeCollisionFixtureResource() {Wolf2009_EnableNativeCollisionFixture(true);}
    ~NativeCollisionFixtureResource() {Wolf2009_EnableNativeCollisionFixture(false);}
};
bool Origin(const char *name,idVec3 &out) {
    idEntity *ent=gameEdit->FindEntity(name);
    if(!ent) return false;
    gameEdit->EntityGetOrigin(ent,out);
    return true;
}
void Frames(int count,int forward=0,int right=0,int up=0) {
    usercmd_t commands[MAX_ASYNC_CLIENTS];
    memset(commands,0,sizeof(commands));
    commands[0].forwardmove=forward;
    commands[0].rightmove=right;
    commands[0].upmove=up;
    commands[0].buttons=BUTTON_RUN;
    for(int i=0;i<count;++i) game->RunFrame(commands);
}
bool Near(float a,float b,float tolerance) { return idMath::Fabs(a-b)<=tolerance; }
bool NativeTrace(const char *label,cmHandle_t model,const idVec3 &start,const idVec3 &end,
                 const idTraceModel *shape,int mask,float expectedFraction,
                 const idVec3 &expectedNormal,int expectedContents) {
    trace_t result;
    collisionModelManager->Translation(&result,start,end,shape,mat3_default,mask,
        model,vec3_origin,mat3_default);
    const bool hit=expectedFraction<1;
    const bool valid=Near(result.fraction,expectedFraction,.00002f)&&
        result.endpos.Compare(start+(end-start)*expectedFraction,.002f)&&
        (!hit||(result.c.contents==expectedContents&&result.c.material&&
                result.c.normal.Compare(expectedNormal,.0001f)));
    // contactInfo_t is defined only for a hit, even when a previous trace left
    // a plausible normal in the manager's scratch storage.
    const idVec3 normal=result.fraction<1?result.c.normal:vec3_origin;
    const int contents=result.fraction<1?result.c.contents:0;
    common->Printf("WOLF2009_NATIVE_COLLISION_TRACE case=%s fraction=%.7f end=(%.4f,%.4f,%.4f) "
        "normal=(%.5f,%.5f,%.5f) contents=%d valid=%d\n",label,result.fraction,
        result.endpos.x,result.endpos.y,result.endpos.z,normal.x,normal.y,normal.z,contents,valid);
    return valid;
}
}
bool Wolf2009_RunNativeCollisionFixtureProbe() {
    if(!collisionModelManager)return false;
    const char *name="wolf2009/_qa/native-collision";
    cmHandle_t handle;
    {
        NativeCollisionFixtureResource resource;
        handle=collisionModelManager->LoadModel(name,true);
    }
    // BASE handle0 is the world or failure, never this new named model.
    if(handle<=0||idStr::Icmp(collisionModelManager->GetModelName(handle),name))return false;
    nativeCollisionFixtureHandle=handle;
    idBounds bounds;int contents=0;
    const bool owned=collisionModelManager->LoadModel(name,true)==handle&&
        collisionModelManager->GetModelBounds(handle,bounds)&&
        bounds[0].Compare(idVec3(-64,-64,0),.0001f)&&bounds[1].Compare(idVec3(448,224,96),.0001f)&&
        collisionModelManager->GetModelContents(handle,contents)&&contents==(1|2|8|16);
    common->Printf("WOLF2009_NATIVE_COLLISION_MODEL handle=%d cached_after_resource_close=%d contents=%d\n",
        int(handle),owned,contents);
    if(!owned)return false;
    const idVec3 up(0,0,1),left(-1,0,0);
    const float clip=CM_CLIP_EPSILON;
    bool valid=true;
    valid=NativeTrace("floor-front",handle,idVec3(0,16,32),idVec3(0,16,-32),NULL,1,
        (32-clip)/64,up,3)&&valid;
    valid=NativeTrace("floor-back",handle,idVec3(0,16,-32),idVec3(0,16,32),NULL,1,
        1,vec3_origin,0)&&valid;
    valid=NativeTrace("finite-inside",handle,idVec3(144,16,32),idVec3(144,16,0),NULL,16,
        (16-clip)/32,up,16)&&valid;
    // Inside this triangle's bounds, outside the actual finite triangle.
    valid=NativeTrace("finite-bounds-only",handle,idVec3(184,56,32),idVec3(184,56,0),NULL,16,
        1,vec3_origin,0)&&valid;
    valid=NativeTrace("wall-player-mask",handle,idVec3(96,0,48),idVec3(224,0,48),NULL,8,
        (64-clip)/128,left,8)&&valid;
    valid=NativeTrace("wall-solid-mask",handle,idVec3(96,0,48),idVec3(224,0,48),NULL,1,
        1,vec3_origin,0)&&valid;
    const idTraceModel box(idBounds(idVec3(-16,-16,-16),idVec3(16,16,16)));
    const idTraceModel cylinder(idBounds(idVec3(-16,-16,-16),idVec3(16,16,16)),8);
    valid=NativeTrace("wall-box",handle,idVec3(96,0,48),idVec3(224,0,48),&box,8,
        (48-clip)/128,left,8)&&valid;
    valid=NativeTrace("wall-cylinder",handle,idVec3(96,0,48),idVec3(224,0,48),&cylinder,8,
        (48-clip)/128,left,8)&&valid;
    valid=NativeTrace("floor-seam-down",handle,idVec3(-16,-16,32),idVec3(-16,-16,-32),&box,1,
        (16-clip)/64,up,3)&&valid;
    valid=NativeTrace("floor-seam-slide",handle,idVec3(-32,-16,16+clip),idVec3(32,-16,16+clip),&box,1,
        1,vec3_origin,0)&&valid;
    idVec3 slope(-.25f,0,1);slope.Normalize();
    valid=NativeTrace("slope-point",handle,idVec3(0,192,64),idVec3(0,192,-32),NULL,1,
        (48-clip/slope.z)/96,slope,1)&&valid;
    const idTraceModel smallBox(idBounds(idVec3(-4,-4,-4),idVec3(4,4,4)));
    valid=NativeTrace("slope-box",handle,idVec3(0,192,64),idVec3(0,192,-32),&smallBox,1,
        (43-clip/slope.z)/96,slope,1)&&valid;
    valid=NativeTrace("both-front",handle,idVec3(384,16,32),idVec3(384,16,-32),NULL,1,
        (32-clip)/64,up,1)&&valid;
    valid=NativeTrace("both-back",handle,idVec3(384,16,-32),idVec3(384,16,32),NULL,1,
        (32-clip)/64,-up,1)&&valid;
    valid=NativeTrace("both-back-box",handle,idVec3(384,16,-32),idVec3(384,16,32),&box,1,
        (16-clip)/64,-up,1)&&valid;
    valid=NativeTrace("both-back-seam-slide",handle,idVec3(352,0,-16-clip),idVec3(416,0,-16-clip),&box,1,
        1,vec3_origin,0)&&valid;
    const int overlap=collisionModelManager->Contents(vec3_origin,&box,mat3_default,1,
        handle,vec3_origin,mat3_default);
    const int excluded=collisionModelManager->Contents(vec3_origin,&box,mat3_default,16,
        handle,vec3_origin,mat3_default);
    // An open triangle surface has no fabricated point-volume underneath it.
    const int pointVolume=collisionModelManager->Contents(idVec3(0,0,-1),NULL,mat3_default,1,
        handle,vec3_origin,mat3_default);
    contactInfo_t contacts[16];idVec6 direction;direction.Zero();direction[2]=-1;
    const int count=collisionModelManager->Contacts(contacts,16,idVec3(0,0,16+clip),direction,1,
        &box,mat3_default,1,handle,vec3_origin,mat3_default);
    bool floorContact=false;
    for(int i=0;i<count;++i)if(contacts[i].normal.Compare(up,.0001f)&&contacts[i].contents==3)floorContact=true;
    const bool queries=overlap==3&&excluded==0&&pointVolume==0&&count>0&&floorContact;
    common->Printf("WOLF2009_NATIVE_COLLISION_CONTENTS overlap=%d excluded=%d point_volume=%d contacts=%d valid=%d\n",
        overlap,excluded,pointVolume,count,queries);
    if(!valid||!queries)return false;
    common->Printf("WOLF2009_ACTUAL_NATIVE_COLLISION_MESH_QA_OK\n");
    return true;
}
bool Wolf2009_CheckNativeCollisionFixtureReleased() {
    if(nativeCollisionFixtureHandle<0)return true;
    idBounds bounds;
    const bool released=collisionModelManager&&
        !collisionModelManager->GetModelBounds(nativeCollisionFixtureHandle,bounds);
    // common has already shut down; its logging path need not remain active.
    fprintf(stdout,"WOLF2009_NATIVE_COLLISION_LIFETIME released=%d\n",released);
    fflush(stdout);
    nativeCollisionFixtureHandle=-1;
    return released;
}
namespace {
bool NativeCollisionPlayerProbe() {
    idEntity *player=gameEdit->FindEntity("player1");
    if(!player||nativeCollisionFixtureHandle<=0)return false;
    idVec3 original;
    gameEdit->EntityGetOrigin(player,original);
    idDict args;
    args.Set("classname","wolf2009_static");
    args.Set("name","native_collision_player_floor");
    args.Set("origin","1000 0 0");
    args.Set("clipmodel","wolf2009/_qa/native-collision");
    idEntity *surface=NULL;
    gameEdit->SpawnEntityDef(args,&surface);
    if(!surface)return false;
    // A real native idPhysics_Player moves on an independently loaded CM
    // surface outside the brush fixture. No per-frame position correction.
    gameEdit->EntitySetOrigin(player,idVec3(968,0,4));
    Frames(30);
    idVec3 settled,moved;
    gameEdit->EntityGetOrigin(player,settled);
    // The preceding slide intentionally leaves lateral velocity. Let native
    // friction settle it, then centre this independent setup across y=x.
    gameEdit->EntitySetOrigin(player,idVec3(settled.x,0,settled.z));
    Frames(1);
    gameEdit->EntityGetOrigin(player,settled);
    Frames(30,127);
    gameEdit->EntityGetOrigin(player,moved);
    const bool walked=Near(settled.x,968,.1f)&&settled.z>0&&settled.z<.5f&&
        moved.x>1008&&moved.x<1048&&Near(moved.y,settled.y,.5f)&&Near(moved.z,settled.z,.05f)&&
        settled.x-1000<settled.y-1&&moved.x-1000>moved.y+1;
    common->Printf("WOLF2009_NATIVE_COLLISION_PLAYER start=(%.4f,%.4f,%.4f) end=(%.4f,%.4f,%.4f) seam_crossed=%d\n",
        settled.x,settled.y,settled.z,moved.x,moved.y,moved.z,walked);
    gameEdit->EntitySetOrigin(player,original);
    Frames(2);
    gameEdit->EntityDelete(surface);
    return walked;
}
}
bool Wolf2009_RunGameplayFixtureProbe() {
    if(!game || !gameEdit) return false;
    if(!Wolf2009_RunNativeCollisionFixtureProbe())return false;
    idEntity *player=gameEdit->FindEntity("player1");
    idVec3 platform0,platform1,player0,player1;
    if(!player || !Origin("runtime_platform",platform0) ||
       !gameEdit->FindEntity("runtime_touch") || !gameEdit->FindEntity("runtime_remove_me") ||
       !gameEdit->FindEntity("runtime_remove_targets")) {
        common->Warning("Wolf2009 gameplay fixture entities were not spawned");
        return false;
    }
    const idDict *args=gameEdit->EntityGetSpawnArgs(player);
    if(!args || idStr::Icmp(args->GetString("spawnclass"),"idPlayer")) {
        common->Warning("Wolf2009 gameplay fixture did not use native idPlayer");
        return false;
    }

    // Settle on the moving platform while its native parametric physics is
    // still active. Pusher order/contact handling belongs to the game module.
    gameEdit->EntitySetOrigin(player,platform0+idVec3(0,0,2));
    Frames(12);
    if(!Origin("runtime_platform",platform0) || !Origin("player1",player0)) return false;
    Frames(48);
    if(!Origin("runtime_platform",platform1) || !Origin("player1",player1)) return false;
    const idVec3 moverDelta=platform1-platform0, riderDelta=player1-player0;
    const bool carried=moverDelta.y>10 && Near(moverDelta.y,riderDelta.y,.75f) &&
        Near(platform1.x,player1.x,.5f) && Near(platform1.z,player1.z,1);
    common->Printf("WOLF2009_NATIVE_MOVER delta_y=%.4f rider_y=%.4f player_z=%.4f platform_z=%.4f carried=%d\n",
        moverDelta.y,riderDelta.y,player1.z,platform1.z,carried);
    if(!carried) return false;

    // Native gravity, forward/side input, wall collision and trigger touches.
    // Warp for independent test setup only; no position correction runs during
    // movement. The wall's inner face is X160 and actor width is32.
    gameEdit->EntitySetOrigin(player,idVec3(0,0,4));
    Frames(30);
    idVec3 settled;
    if(!Origin("player1",settled)) return false;
    Frames(180,127);
    if(!Origin("player1",player0)) return false;
    const bool wall=player0.x>143 && player0.x<145 && Near(player0.y,settled.y,.5f) &&
        player0.z>-.5f && player0.z<1;
    const bool touched=!gameEdit->FindEntity("runtime_touch") &&
        !gameEdit->FindEntity("runtime_remove_targets") && !gameEdit->FindEntity("runtime_remove_me");
    common->Printf("WOLF2009_NATIVE_PLAYER wall_x=%.4f y=%.4f z=%.4f stopped=%d trigger_removed_targets=%d\n",
        player0.x,player0.y,player0.z,wall,touched);
    if(!wall || !touched) return false;

    Frames(60,127,127);
    if(!Origin("player1",player1)) return false;
    const bool slid=Near(player1.x,player0.x,.5f) && player1.y<player0.y-40 &&
        player1.z>-.5f && player1.z<1;
    common->Printf("WOLF2009_NATIVE_PLAYER_SLIDE x=%.4f y=%.4f z=%.4f slid=%d\n",
        player1.x,player1.y,player1.z,slid);
    if(!slid) return false;
    if(!NativeCollisionPlayerProbe())return false;
    common->Printf("WOLF2009_ACTUAL_GAMEPLAY_FIXTURE_QA_OK\n");
    return true;
}

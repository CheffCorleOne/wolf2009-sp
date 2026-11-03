// SPDX-License-Identifier: GPL-3.0-or-later
#include "idlib/precompiled.h"
#include "GameplayRuntimeProbe.h"

namespace {
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
}
bool Wolf2009_RunGameplayFixtureProbe() {
    if(!game || !gameEdit) return false;
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
    common->Printf("WOLF2009_ACTUAL_GAMEPLAY_FIXTURE_QA_OK\n");
    return true;
}

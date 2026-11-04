// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <stdexcept>
#include "NativeSourceMap.h"
#include "NativeSourceDirector.h"
#include "NativeSourcePlayer.h"
#include "SourceSession.h"
#include "../source-director/NativeSourceDirectorChecks.h"
#include "../source-scripted-actor/TrainyardActorFixture.h"
#include "../source-animated-door/TrainyardDoorFixture.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"
namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void NeedReason(bool value,const idStr& reason,const char* fallback){Need(value,reason.IsEmpty()?fallback:reason.c_str());}
}
extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardArrivalQA(){try{
    auto* map=Wolf2009_SourceSessionMapRuntime();Need(map&&Wolf2009_HasSourceSession(),"Ordinary source trainyard session is absent");
    idStr why;WolfSourceMapState source;NeedReason(map->Query(source,why),why,"Source map state is absent");
    Need(source.spawned&&source.bodies==535&&source.entities==2238&&source.sourceMovers==1&&source.collisionShapes==86&&source.sourceStatics==3&&source.sourceRelays==1&&source.sourceDirectors==1,
        "Measured source arrival assembly differs");
    idPlayer* player=gameLocal.GetLocalPlayer();Need(player&&Wolf2009_IsSourcePlayer(player)&&player->health==100,"Original source passenger is absent");
    NeedReason(Wolf2009_RunSourceRegistryChecks(why),why,"Source registry native checks failed");
    NeedReason(Wolf2009_RunTrainyardDirectorChecks(*map,why),why,"Source director native checks failed");
    // Keep simulating after hidden source collision becomes solid. An accepted
    // show/event alone does not establish that ordinary player physics survives.
    for(unsigned i=0;i<60;++i){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);
        Need(player==gameLocal.GetLocalPlayer()&&player->health==100,"Passenger changed or died after source static activation");
        const idVec3& origin=player->GetPhysics()->GetOrigin();Need(std::isfinite(origin.x)&&std::isfinite(origin.y)&&std::isfinite(origin.z)&&origin.z>294&&origin.z<300,
            "Source passenger did not remain on the arriving train");}
    NeedReason(map->Query(source,why),why,"Source arrival ownership changed after simulation");
    common->Printf("WOLF2009_TRAINYARD_ARRIVAL_QA_OK normal_devmap=1 source_statics=3 own_key_relay=1 actual_passenger=1 post_show_frames=60 full_campaign=0\n");return 1;
}catch(const std::exception& error){common->Warning("WOLF2009_TRAINYARD_ARRIVAL_QA_FAIL reason=%s",error.what());return 0;}}
extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardScriptedActorQA(){
    idStr reason;if(Wolf2009_RunTrainyardScriptedActorFixtureQA(reason))return 1;
    common->Warning("WOLF2009_TRAINYARD_SCRIPTED_ACTOR_FIXTURE_QA_FAIL reason=%s",reason.c_str());return 0;
}
extern "C" __attribute__((visibility("default"))) int Wolf2009_RunTrainyardAnimatedDoorQA(){
    idStr reason;if(Wolf2009_RunTrainyardAnimatedDoorFixtureQA(reason))return 1;
    common->Warning("WOLF2009_TRAINYARD_ANIMATED_DOOR_FIXTURE_QA_FAIL reason=%s",reason.c_str());return 0;
}

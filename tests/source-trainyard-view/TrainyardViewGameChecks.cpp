// SPDX-License-Identifier: GPL-3.0-or-later
// Probe the ordinary live trainyard player/wagon. Core owns GPU capture.
#include <cmath>
#include <cstdio>
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
#include "NativeSourceMap.h"
#include "NativeSourceMover.h"
#include "NativeSourcePlayer.h"
#include "SourceSession.h"
#include "NativeSourceGreeterRoute.h"
#include "NativeSourceAnimatedDoor.h"
#include "TrainyardViewCoreChecks.h"

namespace {
unsigned checks=0;
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct State {
    WolfSourceMapRuntime* runtime=NULL;
    idEntityPtr<idPlayer> player;
    idEntityPtr<idEntity> wagon;
    // Optional bound door of a greeter-route session; never created here.
    idEntityPtr<idEntity> door;bool hasDoor=false;idVec3 doorOrigin;idMat3 doorAxis;const idRenderModel* doorModel=NULL;
    const idRenderModel* model=NULL;
    const idDeclSkin* skin=NULL;
    int time=0,frame=0,contents=0,mask=0,stage=0;
    bool originallyHidden=false;
    idVec3 playerOrigin,playerVelocity,cameraOrigin,wagonOrigin,wagonVelocity;
    idAngles playerAngles;
    idMat3 cameraAxis,wagonAxis;
    ~State(){if(auto* child=door.GetEntity()){child->idEntity::Show();child->UpdateVisuals();child->Present();}
        if(auto* entity=wagon.GetEntity()){
        // idMover::Hide/Show also change physics contents. Qualified idEntity
        // calls alter visibility only, retaining the admitted source pusher.
        if(originallyHidden)entity->idEntity::Hide();else entity->idEntity::Show();
        entity->UpdateVisuals();entity->Present();}}
};
std::unique_ptr<State> state;
WolfSourceMoverState Mover(idEntity* entity){WolfSourceMoverState result;idStr why;
    Need(Wolf2009_QuerySourceMover(entity,result,why),why.IsEmpty()?"trainyard view mover state missing":why.c_str());
    Need(result.phase!=WolfSourceMover_Unsupported,result.failure?result.failure:"trainyard view source mover refused");return result;
}
void Finite(const idVec3& p){for(unsigned i=0;i<3;++i)Need(std::isfinite(p[i]),"trainyard view snapshot has nonfinite coordinates");}
void Unchanged(){
    Need(bool(state),"trainyard view probe has not begun");auto* player=state->player.GetEntity();auto* wagon=state->wagon.GetEntity();
    Need(player&&wagon&&gameLocal.GetLocalPlayer()==player&&Wolf2009_SourceSessionMapRuntime()==state->runtime&&state->runtime->PrimaryMover()==wagon,
        "trainyard game entity/session generation changed during framebuffer capture");
    Need(gameLocal.time==state->time&&gameLocal.framenum==state->frame,"trainyard A/B advanced simulation time/frame");
    Need(player->health>0&&Wolf2009_IsSourcePlayer(player)&&player->GetPhysics()->GetOrigin().Compare(state->playerOrigin,0)&&
        player->GetPhysics()->GetLinearVelocity().Compare(state->playerVelocity,0)&&player->viewAngles.Compare(state->playerAngles,0)&&
        player->firstPersonViewOrigin.Compare(state->cameraOrigin,0)&&player->firstPersonViewAxis.Compare(state->cameraAxis,0),
        "trainyard A/B changed the original player pose/view/velocity");
    Need(wagon->GetPhysics()->GetOrigin().Compare(state->wagonOrigin,0)&&wagon->GetPhysics()->GetAxis().Compare(state->wagonAxis,0)&&
        wagon->GetPhysics()->GetLinearVelocity().Compare(state->wagonVelocity,0)&&wagon->GetPhysics()->GetContents()==state->contents&&wagon->GetPhysics()->GetClipMask()==state->mask,
        "trainyard A/B changed the source wagon physics/placement");
    Need(wagon->GetRenderEntity()->hModel==state->model&&wagon->GetRenderEntity()->customSkin==state->skin,
        "trainyard A/B replaced the original wagon model/skin");
    const auto source=Mover(wagon);Need(source.typedSourceResources&&!source.visualDiagnostic&&source.nativePusher&&!source.collisionIsTraceModel,
        "trainyard A/B lost its qualified original source mover");
    Need(wagon->IsHidden()==(state->stage==1),"trainyard A/B wagon visibility differs from its requested stage");
    if(state->hasDoor){auto* child=state->door.GetEntity();Need(child&&child->GetBindMaster()==wagon&&gameLocal.FindEntity("train_door_1")==child&&
            child->GetPhysics()->GetOrigin().Compare(state->doorOrigin,0)&&child->GetPhysics()->GetAxis().Compare(state->doorAxis,0)&&child->GetRenderEntity()->hModel==state->doorModel,
            "trainyard door A/B changed the bound source door placement/model");
        Need(child->IsHidden()==(state->stage==3),"trainyard door visibility differs from its requested stage");}
}
void Begin(){
    Need(!state,"trainyard view probe cannot begin twice");auto* runtime=Wolf2009_SourceSessionMapRuntime();
    Need(Wolf2009_HasSourceSession()&&runtime,"trainyard view has no ordinary source session/map runtime");
    idStr why;WolfSourceMapState map;Need(runtime->Query(map,why),why.IsEmpty()?"trainyard view map state missing":why.c_str());
    Need(map.spawned&&map.bodies==535&&map.entities==2238&&map.sourceMovers==1&&map.collisionShapes==86&&map.sourceSkin,
        "trainyard view map does not own the original wagon/source body plan");
    auto* player=gameLocal.GetLocalPlayer();auto* wagon=runtime->PrimaryMover();
    Need(player&&Wolf2009_IsSourcePlayer(player)&&player->health>0&&player->entityNumber==gameLocal.localClientNum,
        "trainyard view has no live reserved-slot source player");
    Need(wagon&&gameLocal.FindEntity("starting_boxcar_model")==wagon&&!wagon->IsHidden(),"trainyard original wagon is missing/hidden before capture");
    Need(!cvarSystem->GetCVarBool("pm_thirdPerson"),"trainyard proof requires the existing ordinary first-person camera");
    const auto source=Mover(wagon);
    Need(source.entityIndex==2129&&source.sourceName&&idStr::Cmp(source.sourceName,"starting_boxcar_model")==0&&source.sourceClass&&idStr::Cmp(source.sourceClass,"func_mover")==0&&
        source.sourceModel&&idStr::Cmp(source.sourceModel,"models/vehicles/box_car_a.md5r")==0&&source.authoredSkin&&idStr::Cmp(source.authoredSkin,"models/vehicles/box_car_full_in")==0,
        "trainyard wagon original entity/model/skin selection differs");
    Need(source.typedSourceResources&&source.entitiesBytes&&source.entitiesFingerprint&&source.brainBytes&&source.brainFingerprint&&source.nativeModel&&source.nativeSkin&&source.nativeSkin[0],
        "trainyard wagon lost exact source/native data identities");
    const auto* render=wagon->GetRenderEntity();
    Need(render&&render->hModel&&render->hModel->IsLoaded()&&!render->hModel->IsDefaultModel()&&render->hModel->IsDynamicModel()==DM_STATIC&&render->hModel->NumSurfaces()>0&&
        render->hModel==renderModelManager->CheckModel(source.nativeModel)&&idStr::Cmp(render->hModel->Name(),source.nativeModel)==0,
        "trainyard actual wagon render entity does not use the resident original static model");
    Need(render->customSkin&&render->customSkin->GetState()!=DS_DEFAULTED&&idStr::Cmp(render->customSkin->GetName(),source.nativeSkin)==0&&
        render->customSkin==declManager->FindSkin(source.nativeSkin,false),"trainyard actual wagon render entity lost its qualified source skin");
    std::unique_ptr<State> pending(new State);pending->runtime=runtime;pending->player=player;pending->wagon=wagon;pending->model=render->hModel;pending->skin=render->customSkin;
    pending->time=gameLocal.time;pending->frame=gameLocal.framenum;pending->contents=wagon->GetPhysics()->GetContents();pending->mask=wagon->GetPhysics()->GetClipMask();
    pending->originallyHidden=wagon->IsHidden();pending->playerOrigin=player->GetPhysics()->GetOrigin();pending->playerVelocity=player->GetPhysics()->GetLinearVelocity();pending->playerAngles=player->viewAngles;
    pending->cameraOrigin=player->firstPersonViewOrigin;pending->cameraAxis=player->firstPersonViewAxis;
    pending->wagonOrigin=wagon->GetPhysics()->GetOrigin();pending->wagonVelocity=wagon->GetPhysics()->GetLinearVelocity();pending->wagonAxis=wagon->GetPhysics()->GetAxis();
    Finite(pending->playerOrigin);Finite(pending->playerVelocity);Finite(pending->wagonOrigin);Finite(pending->wagonVelocity);
    if(auto* route=runtime->GreeterRoute()){auto* door=route->Door()?route->Door()->Entity():NULL;
        Need(door&&door->GetBindMaster()==wagon&&!door->IsHidden()&&door->GetRenderEntity()->hModel,"trainyard greeter route door is absent/hidden before capture");
        pending->door=door;pending->hasDoor=true;pending->doorOrigin=door->GetPhysics()->GetOrigin();pending->doorAxis=door->GetPhysics()->GetAxis();pending->doorModel=door->GetRenderEntity()->hModel;}
    state=std::move(pending);Unchanged();
    printf("WOLF2009_TRAINYARD_VIEW_READY source_entity=2129 native_entity=%d source_model=%s native_model=%s authored_skin=%s native_skin=%s entities_bytes=%llu entities_fingerprint=%llu brain_bytes=%llu brain_fingerprint=%llu source_surfaces=%d game_time=%d actual_game_camera=1 camera_warp=0\n",
        wagon->entityNumber,source.sourceModel,source.nativeModel,source.authoredSkin,source.nativeSkin,source.entitiesBytes,source.entitiesFingerprint,source.brainBytes,source.brainFingerprint,render->hModel->NumSurfaces(),gameLocal.time);
}
}
extern "C" __attribute__((visibility("default"))) int Wolf2009_TrainyardViewQA(int action){
    try{
        switch(action){
        case WolfTrainyardViewQa_Begin:checks=0;Begin();break;
        case WolfTrainyardViewQa_WagonEntityNumber:Unchanged();return state->wagon.GetEntity()->entityNumber;
        case WolfTrainyardViewQa_CheckUnchanged:Unchanged();break;
        case WolfTrainyardViewQa_HideWagon:{Unchanged();Need(state->stage==0,"trainyard hide stage repeated/out of order");
            auto* entity=state->wagon.GetEntity();entity->idEntity::Hide();entity->UpdateVisuals();entity->Present();state->stage=1;Unchanged();break;}
        case WolfTrainyardViewQa_RestoreWagon:{Unchanged();Need(state->stage==1,"trainyard restore stage requires the hidden control");
            auto* entity=state->wagon.GetEntity();entity->idEntity::Show();entity->UpdateVisuals();entity->Present();state->stage=2;Unchanged();break;}
        case WolfTrainyardViewQa_DoorEntityNumber:Unchanged();return state->hasDoor?state->door.GetEntity()->entityNumber:-1;
        case WolfTrainyardViewQa_HideDoor:{Unchanged();Need(state->hasDoor&&state->stage==2,"trainyard door hide requires the restored wagon stage");
            auto* entity=state->door.GetEntity();entity->idEntity::Hide();entity->UpdateVisuals();entity->Present();state->stage=3;Unchanged();break;}
        case WolfTrainyardViewQa_RestoreDoor:{Unchanged();Need(state->stage==3,"trainyard door restore requires the hidden door control");
            auto* entity=state->door.GetEntity();entity->idEntity::Show();entity->UpdateVisuals();entity->Present();state->stage=4;Unchanged();break;}
        case WolfTrainyardViewQa_End:{const bool complete=state&&(state->stage==4||(state->stage==2&&!state->hasDoor));if(complete)Unchanged();state.reset();
            if(complete)printf("WOLF2009_TRAINYARD_VIEW_GAME_QA_OK checks=%u original_model_skin_data=1 same_time_camera_physics=1 visibility_restored=1 camera_warp=0\n",checks);
            else printf("WOLF2009_TRAINYARD_VIEW_GAME_CLEANUP completed=0 visibility_restored=1\n");break;}
        default:Need(false,"unknown trainyard view QA action");}
        return 1;
    }catch(const std::exception& error){common->Warning("WOLF2009_TRAINYARD_VIEW_GAME_QA_FAIL action=%d checks=%u reason=%s",action,checks,error.what());state.reset();return 0;}
}

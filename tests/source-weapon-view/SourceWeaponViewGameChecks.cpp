// SPDX-License-Identifier: GPL-3.0-or-later
// Actual source-player/weapon state for the ordinary game->Draw camera. The
// core owns EGL/readback; this game-private probe never builds a preview world.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceWeapon.h"
#include "NativeSourceWeaponView.h"
#include "NativeSourceWeaponPresentation.h"
#include "SourceSession.h"
#include "SourceWeaponViewCoreChecks.h"

namespace {
unsigned checks=0,frames=0;
void Need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct ReleaseView {void operator()(WolfSourceWeaponView* view)const{if(view)Wolf2009_ReleaseSourceWeaponView(view);}};
typedef std::unique_ptr<WolfSourceWeaponView,ReleaseView> OwnedView;
struct State {
    idEntityPtr<idPlayer> player;
    idEntityPtr<idAnimatedEntity> entity;
    OwnedView view;
    WolfSourceWeaponViewState metadata;
    std::vector<float> idlePose;
    bool equipped=false,flat=false,cameraChanged=false,thirdPersonBefore=false;
    int viewsBefore=Wolf2009_LiveSourceWeaponViews();
    float fireJointDelta=0,reloadJointDelta=0;
    ~State(){if(equipped&&player.GetEntity())Wolf2009_ResetSourceWeaponForPlayer(player.GetEntity());
        if(cameraChanged&&cvarSystem)cvarSystem->SetCVarBool("pm_thirdPerson",thirdPersonBefore);}
};
std::unique_ptr<State> state;
idPlayer* Player(){Need(bool(state)&&state->player.GetEntity(),"source gun GPU player lifetime expired");return state->player.GetEntity();}
idAnimatedEntity* Weapon(){
    idPlayer* player=Player();idAnimatedEntity* entity=Wolf2009_SourceWeaponEntityForQA(player);
    Need(entity&&entity==state->entity.GetEntity(),"source gun GPU entity generation changed");return entity;
}
WolfSourceWeaponState WeaponState(){WolfSourceWeaponState value;idStr why;
    Need(Wolf2009_QuerySourceWeapon(Player(),value,why),why.IsEmpty()?"source gun GPU state missing":why.c_str());
    Need(!value.unsupported,why.IsEmpty()?"source gun GPU controller refused input":why.c_str());return value;
}
void Frame(bool fire){
    idPlayer* player=Player();usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));
    commands[player->entityNumber].buttons=fire?BUTTON_ATTACK:0;
    gameLocal.RunFrame(commands);++frames;
}
std::vector<float> Pose(){
    auto* entity=Weapon();auto* animator=entity->GetAnimator();Need(animator!=NULL,"source gun has no native animator");
    animator->CreateFrame(gameLocal.time,true);int count=0;idJointMat* joints=NULL;animator->GetJoints(&count,&joints);
    Need(count==49&&joints,"source gun pose does not contain the original49 joints");std::vector<float> result(std::size_t(count)*12);
    for(int j=0;j<count;++j)for(int c=0;c<12;++c){const float v=joints[j].ToFloatPtr()[c];Need(std::isfinite(v),"source gun joint pose contains nonfinite values");result[std::size_t(j)*12+c]=v;}
    return result;
}
float Difference(const std::vector<float>& a,const std::vector<float>& b){
    Need(a.size()==b.size()&&!a.empty(),"source gun pose shape changed");float difference=0;
    for(std::size_t i=0;i<a.size();++i)difference=(std::max)(difference,std::fabs(a[i]-b[i]));return difference;
}
void Skin(bool flat){
    auto* entity=Weapon();const char* name=flat?state->metadata.flatSkin:state->metadata.skin;
    Need(name&&*name,"source gun GPU skin alias is absent");const auto* skin=declManager->FindSkin(name,false);
    Need(skin&&skin->GetState()!=DS_DEFAULTED,"source gun GPU skin is defaulted");
    entity->SetSkin(skin);entity->UpdateVisuals();entity->Present();state->flat=flat;
}
void Begin(){
    Need(!state,"source gun GPU check cannot begin twice");Need(Wolf2009_HasSourceSession(),"source gun GPU check needs the actual source session");
    const auto* declarations=Wolf2009_SourceSessionDeclarations();Need(declarations!=NULL,"source gun GPU namespace is missing");
    auto* player=gameLocal.GetLocalPlayer();Need(player&&Wolf2009_IsSourcePlayer(player)&&player->health>0,"source gun GPU reserved-slot source player is missing/dead");
    std::unique_ptr<State> pending(new State);pending->player=player;
    WolfSourceWeaponViewRequest request;Wolf2009_SourceMP40FixtureViewRequest(request);idStr why;
    pending->view.reset(Wolf2009_CreateSourceWeaponView(*declarations,request,why));Need(bool(pending->view),why.IsEmpty()?"source gun GPU view preparation failed":why.c_str());
    Need(Wolf2009_QuerySourceWeaponView(pending->view.get(),pending->metadata,why),why.IsEmpty()?"source gun GPU metadata unavailable":why.c_str());
    const auto& m=pending->metadata;
    Need(m.modelDefinition&&m.nativeModel&&m.capability&&m.skin&&m.flatSkin,"source gun GPU identities are incomplete");
    Need(m.joints==49&&m.surfaces==7&&m.visibleSurfaces==3&&m.hiddenSurfaces==4&&m.vertices==4808&&m.triangles==6488,"source gun GPU does not use the admitted original MP40 geometry");
    Need(m.textures>=3&&m.baseDiffuseCompatibility&&!m.retailShaderParity,"source gun GPU appearance policy is not explicit");
    Need(m.frames[0]==80&&m.frames[1]==5&&m.frames[2]==48,"source gun GPU animation selection changed");
    for(unsigned clip=0;clip<3;++clip){Need(std::isfinite(m.durationSeconds[clip])&&m.durationSeconds[clip]>0,"source gun GPU source duration is invalid");
        for(unsigned role=0;role<4;++role)Need(m.sourceEpochs[clip][role]&&m.sourceBytes[clip][role],"source gun GPU lost an exact source role identity");}
    WolfSourceWeaponVisual visual;Need(Wolf2009_SourceWeaponViewDescriptor(pending->view.get(),visual,why),why.IsEmpty()?"source gun GPU descriptor is unavailable":why.c_str());
    Need(Wolf2009_EquipSourceWeapon(player,*declarations,"weapon_mp40",&visual,32,64,why),why.IsEmpty()?"source gun GPU equip failed":why.c_str());pending->equipped=true;
    auto* entity=Wolf2009_SourceWeaponEntityForQA(player);Need(entity!=NULL,"source gun GPU controller has no actual animated entity");pending->entity=entity;
    Need(entity->GetAnimator()&&entity->GetAnimator()->ModelDef()&&std::string(entity->GetAnimator()->ModelDef()->GetName())==m.modelDefinition,"source gun GPU native animator/modelDef differs from the prepared view");
    Need(entity->GetRenderEntity()->allowSurfaceInViewID==player->entityNumber+1&&entity->GetRenderEntity()->weaponDepthHack,"source gun is not bound to the ordinary first-person camera");
    state=std::move(pending);player->SetViewAngles(ang_zero);player->SetDeltaViewAngles(ang_zero);
    player->GetPlayerPhysics()->SetLinearVelocity(vec3_origin);state->thirdPersonBefore=cvarSystem->GetCVarBool("pm_thirdPerson");
    state->cameraChanged=true;cvarSystem->SetCVarBool("pm_thirdPerson",false);
    for(int i=0;i<8;++i)Frame(false);Skin(false);state->idlePose=Pose();const auto current=WeaponState();
    Need(current.hasViewModel&&current.clip==32&&current.shots==0,"source gun GPU idle startup state changed");
    printf("WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_READY joints=49 surfaces=7 visible=3 hidden=4 vertices=4808 triangles=6488 actual_game_camera=1 base_diffuse=1 retail_shader_parity=0\n");
}
void FirePose(){
    Need(bool(state)&&!state->flat,"source gun fire pose requires the original appearance");const auto before=WeaponState();
    Need(before.clip==32&&before.shots==0,"source gun GPU fire stage requires the unchanged idle state");
    Frame(true);for(int i=0;i<3;++i)Frame(false);const auto current=WeaponState();
    Need(current.shots==1&&current.clip==31&&current.frameCommands==1,"source gun GPU input did not dispatch exactly one authored weapon_attack");
    state->fireJointDelta=Difference(state->idlePose,Pose());Need(state->fireJointDelta>0.0001f,"source gun fire input did not change the actual native joint pose");
    Weapon()->UpdateVisuals();Weapon()->Present();
    printf("WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_FIRE shots=1 clip=31 authored_attack_once=1 joint_delta=%.9g\n",state->fireJointDelta);
}
void ReloadPose(){
    Need(bool(state)&&!state->flat,"source gun reload pose requires the original appearance");
    Need(WeaponState().shots==1&&WeaponState().clip==31,"source gun GPU reload stage requires one actual shot");
    Need(Wolf2009_ReloadSourceWeapon(Player()),"source gun GPU reload input was not consumed");Frame(false);
    const int begin=gameLocal.time;const double sampleSeconds=state->metadata.durationSeconds[2]*19.0/double(state->metadata.frames[2]-1);
    const int sample=begin+int(std::ceil(sampleSeconds*1000.0));unsigned ticks=0;
    while(gameLocal.time<sample&&ticks++<256)Frame(false);Need(gameLocal.time>=sample,"source gun GPU reload source-time sample exceeded the bounded frame budget");
    const auto current=WeaponState();Need(current.shots==1&&current.clip==31&&current.reserve==64,"source gun reload moved ammo or fired before its source endpoint");
    state->reloadJointDelta=Difference(state->idlePose,Pose());Need(state->reloadJointDelta>0.0001f,"source gun reload input did not change the native joint pose");
    WolfSourceWeaponPresentationState presentation;idStr why;
    Need(Wolf2009_QuerySourceWeaponViewPresentation(state->view.get(),presentation,why),why.IsEmpty()?"source gun GPU presentation state missing":why.c_str());
    Need(presentation.pointCommands>=3&&presentation.eyeCommands>=1&&presentation.soundCommands==1&&presentation.soundsStarted==1,"source gun GPU reload commands did not reach their single native presentation owner");
    Need(presentation.pointActive&&presentation.pointPositionValid&&presentation.pointJointName=="l_wrist","source reload POI did not track the actual animated wrist");
    for(int i=0;i<3;++i)Need(std::isfinite(presentation.pointWorld[i])&&std::isfinite(presentation.eyeApplied[i]),"source reload presentation contains nonfinite state");
    Need(std::fabs(presentation.eyeApplied[0])+std::fabs(presentation.eyeApplied[1])+std::fabs(presentation.eyeApplied[2])>0.001f,"source reload eye_angles did not affect the explicit weapon-local presentation");
    Need(presentation.soundEmitterPlaying&&presentation.soundLengthMs>0,"source reload sound was not active on its native emitter");
    Need(state->metadata.reloadSound&&state->metadata.reloadSample,"source reload sound lost its qualified original identity");
    Weapon()->UpdateVisuals();Weapon()->Present();
    printf("WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_RELOAD source_sample_frame=20 joint_delta=%.9g point_commands=%u eye_commands=%u sound_commands=1 original_sample=1 emitter_playing=1 audible_output_claimed=0\n",state->reloadJointDelta,presentation.pointCommands,presentation.eyeCommands);
}
}
extern "C" __attribute__((visibility("default"))) int Wolf2009_SourceWeaponViewGpuQA(int action){
    try{
        switch(action){
        case WolfSourceWeaponViewQa_Begin: checks=frames=0;Begin();break;
        case WolfSourceWeaponViewQa_Flat: Skin(true);break;
        case WolfSourceWeaponViewQa_Original: Skin(false);break;
        case WolfSourceWeaponViewQa_FirePose: FirePose();break;
        case WolfSourceWeaponViewQa_ReloadPose: ReloadPose();break;
        case WolfSourceWeaponViewQa_EntityNumber: return Weapon()->entityNumber;
        case WolfSourceWeaponViewQa_End:{
            const bool complete=state&&state->fireJointDelta>0.0001f&&state->reloadJointDelta>0.0001f;
            const int before=state?state->viewsBefore:Wolf2009_LiveSourceWeaponViews();state.reset();
            Need(Wolf2009_LiveSourceWeaponViews()==before,"source gun GPU view owner leaked during cleanup");
            if(complete)printf("WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GAME_QA_OK checks=%u frames=%u actual_game_view=1 view_owner_released=1\n",checks,frames);
            else printf("WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GAME_CLEANUP completed=0 view_owner_released=1\n");break;}
        default: Need(false,"unknown source gun GPU action");}
        return 1;
    }catch(const std::exception& e){common->Warning("WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GAME_QA_FAIL action=%d checks=%u frames=%u reason=%s",action,checks,frames,e.what());state.reset();return 0;}
}

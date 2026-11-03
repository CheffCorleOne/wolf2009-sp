// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceWeaponPresentation.h"
#include "SourceWeaponProfile.h"
#include "CompiledDeclarationNamespace.h"
#include "SourceSoundBinding.h"
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"
#include "EngineAnimationSet.h"

namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool SameEntry(const wolf2009::CompiledCatalogEntry& a,const wolf2009::CompiledCatalogEntry& b){
    return a.IsUnmodified()&&b.IsUnmodified()&&a.document==b.document&&a.source==b.source&&
        a.loadOrdinal==b.loadOrdinal&&a.documentOrdinal==b.documentOrdinal&&a.catalogOrdinal==b.catalogOrdinal&&
        a.section==b.section&&a.group==b.group&&a.record==b.record&&a.type==b.type&&a.name==b.name&&
        a.typeSpan.begin==b.typeSpan.begin&&a.typeSpan.end==b.typeSpan.end&&a.nameSpan.begin==b.nameSpan.begin&&
        a.nameSpan.end==b.nameSpan.end&&a.bodySpan.begin==b.bodySpan.begin&&a.bodySpan.end==b.bodySpan.end&&
        a.envelopeSpan.begin==b.envelopeSpan.begin&&a.envelopeSpan.end==b.envelopeSpan.end;
}
void Selected(const wolf2009::ResolvedSourceDefinition& definition,const wolf2009::CompiledDeclarationNamespace& names){
    Need(!definition.declarations.empty(),"Weapon presentation requires owning selected declarations");
    for(const auto& origin:definition.declarations){const auto entry=origin.member.compiledRecord;
        Need(bool(entry)&&entry->IsUnmodified(),"Weapon presentation declaration provenance is absent/modified");
        const auto found=names.Lookup(origin.declarationKind,origin.declarationName);
        Need(found.status==wolf2009::CompiledCatalogLookupStatus::Unique&&found.matches.size()==1&&
            SameEntry(found.matches[0],*entry),"Weapon presentation definition is not selected by this namespace");}
}
std::string SourceString(const wolf2009::ResolvedSourceDefinition& definition,const char* key){
    const auto* value=definition.Find(key);
    Need(value&&value->encoding==wolf2009::SourceLiteralEncoding::CompiledString&&value->value.size()==1,
        "Weapon presentation source sound key has no typed string");return value->value[0].text;
}
bool Matches(const wolf2009::SourceWeaponCommand& source,const WolfSourceWeaponFrameCommand& command){
    if(unsigned(source.kind)!=unsigned(command.kind)||source.frame!=command.frame||!command.argument||
        source.argument!=command.argument||source.values.size()!=command.valueCount||command.valueCount>5)return false;
    for(unsigned i=0;i<command.valueCount;++i)if(std::memcmp(&source.values[i],&command.values[i],sizeof(float))!=0)return false;
    return true;
}
void Finite(const idVec3& value,const char* reason){for(int i=0;i<3;++i)Need(std::isfinite(value[i]),reason);}
}
struct WolfSourceWeaponPresentation::Impl {
    WolfSourceWeaponPresentationRequest request;
    std::vector<wolf2009::SourceWeaponCommand> commands;
    std::map<std::string,jointHandle_t> points;
    std::shared_ptr<const WolfSourceSoundBinding> reloadSound;
    const idDeclModelDef* definition=NULL;
    const idRenderModel* model=NULL;
    idEntityPtr<idAnimatedEntity> bound;
    bool boundOnce=false;
    jointHandle_t pointJoint=INVALID_JOINT;
    WolfSourceWeaponPresentationState state;
    bool Preflight(idStr& reason)const{
        reason.Clear();
        try{
            Need(request.policy==WolfSourceWeaponPresentationPolicy::NativeWeaponLocalAimAndPointTrackingV1,
                "Weapon presentation native policy changed");
            Need(definition&&Wolf2009_FindRegisteredSourceAnimationSet(request.nativeModelDefinition.c_str())==definition&&
                definition->ModelHandle()==model&&model&&model->IsLoaded()&&!model->IsDefaultModel(),
                "Weapon presentation registered source model changed");
            Need(reloadSound&&reloadSound->MatchesNative(reason),reason.IsEmpty()?"Weapon presentation reload sound changed":reason.c_str());
            for(const auto& point:points)Need(model->GetJointHandle(point.first.c_str())==point.second,
                "Weapon presentation joint identity changed");
            reason.Clear();return true;
        }catch(const std::exception& error){reason=error.what();return false;}
    }
    void Bind(idAnimatedEntity* entity){
        Need(entity&&entity->entityNumber>=MAX_CLIENTS&&entity->entityNumber<MAX_GENTITIES&&
            gameLocal.entities[entity->entityNumber]==entity,"Weapon presentation requires a live native weapon entity");
        Need(entity->GetAnimator()&&entity->GetAnimator()->ModelDef()==definition,
            "Weapon presentation entity uses a different native model");
        if(boundOnce)Need(bound.GetEntity()==entity,"Weapon presentation cannot be reused by another entity generation");
        else {bound=entity;boundOnce=true;state.boundEntity=entity->entityNumber;}
    }
};
WolfSourceWeaponPresentation::WolfSourceWeaponPresentation(std::unique_ptr<Impl> p):impl_(std::move(p)){}
WolfSourceWeaponPresentation::~WolfSourceWeaponPresentation(){}
bool WolfSourceWeaponPresentation::Preflight(idStr& reason)const{return impl_->Preflight(reason);}
const char* WolfSourceWeaponPresentation::NativeReloadSound()const{return impl_->reloadSound->NativeAlias();}
const char* WolfSourceWeaponPresentation::ReloadSampleCapability()const{return impl_->reloadSound->SampleCapability();}

std::unique_ptr<WolfSourceWeaponPresentation> Wolf2009_PrepareSourceWeaponPresentation(
    const WolfSourceDeclarationSet& set,const wolf2009::SourceWeaponProfile& profile,
    const WolfSourceWeaponPresentationRequest& request,idStr& reason){
    reason.Clear();try{
        Need(request.policy==WolfSourceWeaponPresentationPolicy::NativeWeaponLocalAimAndPointTrackingV1,
            "Explicit native weapon presentation policy required");
        Need(profile.Policy()==wolf2009::SourceWeaponPolicy::MP40PrimaryNativeV1,
            "Weapon presentation supports the qualified MP40 primary profile only");
        Need(!request.nativeModelDefinition.empty()&&request.nativeModelDefinition.size()<=4096&&
            !request.exactReloadSampleResource.empty()&&request.exactReloadSampleResource.size()<=4096,
            "Weapon presentation explicit resource names are absent/excessive");
        Selected(profile.Weapon(),set.Namespace());Selected(profile.ViewModel().model,set.Namespace());
        std::unique_ptr<WolfSourceWeaponPresentation::Impl> out(new WolfSourceWeaponPresentation::Impl);
        out->request=request;out->definition=Wolf2009_FindRegisteredSourceAnimationSet(request.nativeModelDefinition.c_str());
        Need(out->definition!=NULL,"Weapon presentation requires an unchanged registered source animation set");
        out->model=out->definition->ModelHandle();Need(out->model&&out->model->IsLoaded()&&!out->model->IsDefaultModel(),
            "Weapon presentation source model is not resident");
        const int reloadNumber=out->definition->GetSpecificAnim("reload");
        const idAnim* reload=reloadNumber>0?out->definition->GetAnim(reloadNumber):NULL;
        Need(reload&&!reload->HasFrameCommands()&&reload->MD5Anim(0)&&reload->MD5Anim(0)->IsSourceAnimationV2(),
            "Weapon presentation requires one external owner for source reload commands");
        out->commands=profile.ReloadCommands();Need(!out->commands.empty()&&out->commands.size()<=64,
            "Weapon presentation source command count is absent/excessive");
        unsigned soundCount=0;
        for(const auto& command:out->commands){
            Need(command.frame>0&&command.frame<=unsigned(reload->NumFrames())&&!command.origins.empty(),
                "Weapon presentation source command frame/provenance is invalid");
            if(command.kind==wolf2009::SourceWeaponCommandKind::PointOfInterest){
                Need(command.values.size()==1&&std::isfinite(command.values[0])&&command.values[0]>=0&&command.values[0]<=1,
                    "Native point tracking requires one unit-interval parameter");
                if(command.argument!="none"){
                    const jointHandle_t joint=out->model->GetJointHandle(command.argument.c_str());
                    Need(joint!=INVALID_JOINT,"Authored source point_of_interest joint is absent from the native model");
                    out->points[command.argument]=joint;
                }
            }else if(command.kind==wolf2009::SourceWeaponCommandKind::EyeAngles){
                Need(command.values.size()==5,"Native weapon-local eye_angles requires five authored values");
                for(float value:command.values)Need(std::isfinite(value),"Nonfinite weapon-local eye_angles value");
                Need(std::fabs(command.values[0])<=89&&std::fabs(command.values[1])<=89&&std::fabs(command.values[2])<=89&&
                    command.values[3]>=0&&command.values[3]<=10000&&command.values[4]>=0&&command.values[4]<=1,
                    "Weapon-local eye_angles exceeds the explicit native profile");
            }else if(command.kind==wolf2009::SourceWeaponCommandKind::SoundWeapon){
                Need(command.argument=="snd_reload"&&command.values.empty(),"Weapon presentation has an unqualified sound command");++soundCount;
            }else Need(false,"Weapon presentation cannot execute attack or unknown reload commands");
        }
        Need(soundCount==1,"MP40 presentation requires exactly one source reload sound command");
        WolfSourceSoundRequest sound;sound.sourceSound=SourceString(profile.Weapon(),"snd_reload");
        sound.sampleResource=request.exactReloadSampleResource;sound.mixPolicy=WolfSourceSoundMix_NativePlayerUnitGainV1;
        out->reloadSound=Wolf2009_BindSourceSound(set.Namespace(),sound,reason);
        Need(bool(out->reloadSound),reason.IsEmpty()?"Source reload sound binding failed":reason.c_str());
        Need(out->Preflight(reason),reason.IsEmpty()?"Source weapon presentation preflight failed":reason.c_str());
        return std::unique_ptr<WolfSourceWeaponPresentation>(new WolfSourceWeaponPresentation(std::move(out)));
    }catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceWeaponPresentation>();}
}
bool WolfSourceWeaponPresentation::Dispatch(idAnimatedEntity* entity,const WolfSourceWeaponFrameCommand& command,
    int now,idStr& reason){
    reason.Clear();try{
        Need(impl_->Preflight(reason),reason.IsEmpty()?"Weapon presentation capability changed":reason.c_str());
        Need(now>=0&&now>=impl_->state.lastCommandTime,"Weapon presentation command clock moved backwards");
        bool found=false;for(const auto& source:impl_->commands)if(Matches(source,command)){found=true;break;}
        Need(found,"Weapon presentation command differs from the retained authored reload command");
        impl_->Bind(entity);
        if(command.kind==WolfSourceWeapon_PointOfInterest){
            impl_->state.pointJointName=command.argument;impl_->state.pointWeight=command.values[0];
            impl_->state.pointActive=impl_->state.pointJointName!="none";impl_->state.pointPositionValid=false;
            impl_->pointJoint=impl_->state.pointActive?impl_->points.at(impl_->state.pointJointName):INVALID_JOINT;
            ++impl_->state.pointCommands;
        }else if(command.kind==WolfSourceWeapon_EyeAngles){
            for(unsigned i=0;i<3;++i)impl_->state.eyeTarget[i]=command.values[i]*command.values[4];
            impl_->state.eyeRate=command.values[3];impl_->state.eyeGain=command.values[4];++impl_->state.eyeCommands;
        }else if(command.kind==WolfSourceWeapon_SoundWeapon){
            Need(gameLocal.isNewFrame,"Source reload audio requires a new local simulation frame");
            const idSoundShader* shader=declManager->FindSound(impl_->reloadSound->NativeAlias(),false);
            Need(shader&&shader->GetState()!=DS_DEFAULTED&&!shader->HasDefaultSound(),"Source reload sound lost its original sample");
            int length=0;Need(entity->StartSoundShader(shader,SND_CHANNEL_WEAPON,0,false,&length),"Native source reload emitter refused playback");
            Need(entity->GetSoundEmitter()!=NULL&&length>0,"Native source reload sound did not allocate/play a sample");
            impl_->state.soundLengthMs=length;++impl_->state.soundCommands;++impl_->state.soundsStarted;
        }else Need(false,"Native presentation does not own weapon_attack");
        ++impl_->state.commands;impl_->state.lastCommandTime=now;return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool WolfSourceWeaponPresentation::Place(idAnimatedEntity* entity,idPlayer* player,int now,idStr& reason){
    reason.Clear();try{
        Need(impl_->Preflight(reason),reason.IsEmpty()?"Weapon presentation capability changed":reason.c_str());
        Need(player&&now>=0&&now>=impl_->state.lastPlaceTime,"Weapon presentation player/placement clock is invalid");
        impl_->Bind(entity);
        idVec3 origin;idMat3 axis;player->GetViewPos(origin,axis);Finite(origin,"Nonfinite source weapon view origin");
        for(int i=0;i<3;++i)Finite(axis[i],"Nonfinite source weapon view axis");
        const idAnimBlend* channel=entity->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL);
        const bool reload=channel&&channel->Anim()&&idStr::Cmp(channel->Anim()->FullName(),"reload")==0;
        if(!reload){impl_->state.pointActive=false;impl_->state.pointPositionValid=false;impl_->pointJoint=INVALID_JOINT;
            for(float& value:impl_->state.eyeTarget)value=0;}
        const int elapsed=impl_->state.lastPlaceTime<0?0:now-impl_->state.lastPlaceTime;
        const double alpha=impl_->state.eyeRate==0?1.0:-std::expm1(-double(impl_->state.eyeRate)*elapsed*.001);
        for(unsigned i=0;i<3;++i)impl_->state.eyeApplied[i]=float(double(impl_->state.eyeApplied[i])+
            (double(impl_->state.eyeTarget[i])-impl_->state.eyeApplied[i])*alpha);
        const idAngles local(impl_->state.eyeApplied[0],impl_->state.eyeApplied[1],impl_->state.eyeApplied[2]);
        const idMat3 applied=local.ToMat3()*axis;entity->SetOrigin(origin);entity->SetAxis(applied);
        if(impl_->state.pointActive){
            idVec3 point;idMat3 jointAxis;
            Need(entity->GetAnimator()->GetJointTransform(impl_->pointJoint,now,point,jointAxis),"Source point tracking joint pose is unavailable");
            const idVec3 world=origin+point*applied;Finite(world,"Source point tracking produced a nonfinite position");
            for(unsigned i=0;i<3;++i)impl_->state.pointWorld[i]=world[i];impl_->state.pointPositionValid=true;
        }
        impl_->state.lastPlaceTime=now;return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool WolfSourceWeaponPresentation::Query(WolfSourceWeaponPresentationState& out,idStr& reason)const{
    if(!impl_->Preflight(reason))return false;
    out=impl_->state;const auto* entity=impl_->bound.GetEntity();
    out.soundEmitterPlaying=entity&&entity->GetSoundEmitter()&&entity->GetSoundEmitter()->CurrentlyPlaying();
    return true;
}

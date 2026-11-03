// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "game/physics/Physics_AF.h"
#include "SourceEntityRuntime.h"
#include "EngineSourceAF.h"
#include "SourceDeclarationSetRuntime.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "SourceActorBinding.h"
#include "NativeSourceAFChecks.h"

namespace {
unsigned afChecks=0,afFrames=0;
void Need(bool value,const char* why){++afChecks;if(!value)throw std::runtime_error(why);}
void Close(idFile* file){if(file)fileSystem->CloseFile(file);}
typedef std::unique_ptr<idFile,void(*)(idFile*)> File;
struct Owned {
    idEntityPtr<idEntity> entity;
    ~Owned(){idEntity* value=entity.GetEntity();if(value)delete value;}
};
std::string PersistenceKey(const char* name){std::string map=gameLocal.GetMapName();const auto slash=map.find_last_of("/\\");if(slash!=std::string::npos)map.erase(0,slash+1);const auto dot=map.find_last_of('.');if(dot!=std::string::npos)map.resize(dot);return "wolf2009.source-container:"+map+"_"+name;}
struct RestorePersistence {
    std::string key,value;bool existed=false;
    explicit RestorePersistence(const std::string& name):key(name){const auto* old=gameLocal.persistentLevelInfo.FindKey(key.c_str());if(old){existed=true;value=old->GetValue().c_str();}gameLocal.persistentLevelInfo.Delete(key.c_str());}
    ~RestorePersistence(){if(existed)gameLocal.persistentLevelInfo.Set(key.c_str(),value.c_str());else gameLocal.persistentLevelInfo.Delete(key.c_str());}
};
void Frame(){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);++afFrames;}
WolfSourceEntityState EntityState(idEntity* entity){WolfSourceEntityState state;idStr reason;Need(Wolf2009_QuerySourceEntity(entity,state,reason),"Native AF entity state unavailable");return state;}
WolfSourceAFState AFState(idEntity* entity){WolfSourceAFState state;idStr reason;Need(Wolf2009_QuerySourceEntityAF(entity,state,reason),reason.IsEmpty()?"Native source AF state unavailable":reason.c_str());return state;}
idVec3 Vector(const float* v){return idVec3(v[0],v[1],v[2]);}
idJointMat RawJoint(idEntity* entity,int joint){int count=0;idJointMat* palette=NULL;static_cast<idAnimatedEntity*>(entity)->GetAnimator()->GetJoints(&count,&palette);Need(palette&&joint>=0&&joint<count,"Native AF normal Think palette unavailable");return palette[joint];}
float Difference(const idJointMat& a,const idJointMat& b){float difference=0;for(int i=0;i<12;++i)difference=std::max(difference,std::abs(a.ToFloatPtr()[i]-b.ToFloatPtr()[i]));return difference;}
}
bool Wolf2009_CheckNativeSourceAFQA(const WolfSourceDeclarationSet& set,std::shared_ptr<const WolfSourceActorBinding> binding,const char* entitiesPath,const WolfSourceAFRequest& request){
    afChecks=afFrames=0;try{
        Need(binding&&entitiesPath&&gameLocal.GameState()==GAMESTATE_ACTIVE&&Wolf2009_LiveSourceEntities()==0,"Native AF QA requires an isolated active source class phase");
        Need(request.profile==WolfSourceAF_FixedRootWorldHingeNativeConservativeV1&&request.exactResourcePath&&request.authoredRagdollReference&&request.exactRootVariant&&request.restAnimation,
            "Native AF QA requires the caller's complete explicit source selection");
        Need(request.expectedBytes==6688&&request.expectedFingerprint==UINT64_C(0x69f1372d4a8d149c),"Native AF QA requires the qualified selected original HKXR identity");
        File file(fileSystem->OpenFileRead(entitiesPath,false),Close);Need(bool(file)&&file->Length()>0&&file->Length()<=64*1024*1024,"Native AF QA ENTS unavailable/excessive");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(file->Length()));Need(file->Read(bytes.data(),int(bytes.size()))==int(bytes.size()),"Native AF QA ENTS truncated");file.reset();
        const auto plan=wolf2009::PrepareSourceEntity(bytes,entitiesPath,917,set.Definitions(),wolf2009::SourceEntityPolicy::AnimatedContainerV1,binding->SourcePlan().Context());
        Need(plan->Classname()=="container_locker_foot_01"&&plan->Name()=="layer39_container_locker_foot_01_2","Native AF QA selected a substitute entity");
        const auto rejected=[&](std::size_t index,const WolfSourceAFRequest& invalid){idEntity* result=reinterpret_cast<idEntity*>(1);idStr reason;
            Need(!Wolf2009_SpawnSourceEntityWithAF(entitiesPath,index,set,binding,invalid,result,reason)&&result==NULL&&!reason.IsEmpty()&&Wolf2009_LiveSourceEntities()==0,"Invalid AF preparation published a native entity");};
        auto invalid=request;invalid.profile=WolfSourceAF_Unspecified;rejected(917,invalid);invalid=request;invalid.expectedFingerprint^=1;rejected(917,invalid);
        invalid=request;invalid.restAnimation="open";rejected(917,invalid);rejected(921,request); // Affine approximation is not AF admission.
        RestorePersistence persistent(PersistenceKey(plan->Name().c_str()));Owned owned;idEntity* entity=NULL;idStr reason;
        Need(Wolf2009_SpawnSourceEntityWithAF(entitiesPath,917,set,binding,request,entity,reason)&&entity,reason.IsEmpty()?"Actual source AF entity Spawn failed":reason.c_str());owned.entity=entity;
        idPhysics* originalPhysics=entity->GetPhysics();for(int i=0;i<3;++i)Frame();Need(!AFState(entity).active&&entity->GetPhysics()==originalPhysics,"Prepared AF changed native physics before animation completion");
        const int activated=gameLocal.time;Need(Wolf2009_ActivateSourceEntity(entity,NULL,reason),reason.IsEmpty()?"Source AF Activate failed":reason.c_str());
        const int animationEnd=EntityState(entity).animationEndMs;Need(animationEnd>activated&&animationEnd<activated+1200,"Source AF completion/give deadline order differs");
        while(gameLocal.time<animationEnd){Frame();if(gameLocal.time<animationEnd)Need(!AFState(entity).active,"Source AF handoff fired before the native animation deadline");}
        for(int i=0;i<3&&!AFState(entity).active;++i)Frame();const auto initial=AFState(entity);auto sourceState=EntityState(entity);
        Need(initial.active&&entity->GetPhysics()!=originalPhysics&&entity->GetPhysics()->IsType(idPhysics_AF::Type)&&entity->GetPhysics()->GetNumClipModels()==1,"Source completion did not install a real native idPhysics_AF body");
        Need(initial.sourceBodies==2&&initial.nativeBodies==1&&initial.hinges==1&&initial.jointMods==2&&initial.rootCollisionExcluded&&!initial.collisionCoverageComplete,
            "Native AF explicit fixed-root/partial collision policy was mislabeled");
        Need(!sourceState.afHandoffUnsupported&&sourceState.phase==WolfSourceEntity_Completed&&sourceState.pendingGiveEvents==1,"Source AF handoff canceled giving or retained an unsupported phase");
        gameLocal.Printf("WOLF2009_SOURCE_AF_HANDOFF_DIAGNOSTIC source_bodies=%d native_bodies=%d hinges=%d joint_mods=%d mass=%.9g friction_torque=%.9g rounding_excess=%.9g anchor_residual=%.9g initial_angle=%.9g current_angle=%.9g min_angle=%.9g max_angle=%.9g\n",
            initial.sourceBodies,initial.nativeBodies,initial.hinges,initial.jointMods,initial.mass,initial.frictionTorqueGameUnits,initial.maximumRoundingExcess,initial.handoffAnchorResidual,
            initial.initialHingeAngleDegrees,initial.hingeAngleDegrees,initial.minimumHingeAngleDegrees,initial.maximumHingeAngleDegrees);
        Need(std::isfinite(initial.maximumRoundingExcess)&&initial.maximumRoundingExcess>0&&initial.maximumRoundingExcess<1,"Native AF conservative rounding excess is invalid");
        Need(std::isfinite(initial.mass)&&initial.mass>0,"Native AF source mass is invalid");
        Need(std::isfinite(initial.handoffAnchorResidual)&&initial.handoffAnchorResidual<=0.5f,"Native source-to-native rest-delta projection exceeds its explicit0.5gu bound");
        const auto beforeRefusal=EntityState(entity);Need(!Wolf2009_ActivateSourceEntity(entity,NULL,reason)&&!reason.IsEmpty(),"Unqualified reverse AF-to-animation handoff was accepted");
        const auto afterRefusal=EntityState(entity);Need(beforeRefusal.activations==afterRefusal.activations&&beforeRefusal.pendingGiveEvents==afterRefusal.pendingGiveEvents&&AFState(entity).active,"Reverse handoff refusal changed animation/timers/AF");
        idVec3 axis=Vector(initial.hingeAxisWorld);Need(std::abs(axis.Length()-1)<0.001f,"Native AF world hinge axis is not unit length");
        const idVec3 assetOrigin=Vector(initial.assetOriginWorld),anchor=Vector(initial.hingeAnchorWorld);const idJointMat initialRoot=RawJoint(entity,0),initialLid=RawJoint(entity,1);
        Need(entity->GetPhysics()->GetGravity().LengthSqr()>0,"Native AF gravity profile is not active");
        for(int i=0;i<8;++i)Frame();const float gravityAngleChange=AFState(entity).hingeAngleDegrees-initial.hingeAngleDegrees;
        float maximumAngleChange=0,maximumAnchorDrift=0,maximumLidChange=0,maximumRootChange=0;
        // Real physics control, not a fabricated pose/animation tick. Drive
        // toward the interior if a source limit already constrains gravity.
        const float middle=(initial.minimumHingeAngleDegrees+initial.maximumHingeAngleDegrees)*0.5f;
        const float direction=AFState(entity).hingeAngleDegrees<=middle?1.0f:-1.0f;
        entity->GetPhysics()->SetAngularVelocity(axis*(direction*2.0f),0);entity->GetPhysics()->Activate();entity->BecomeActive(TH_PHYSICS|TH_ANIMATE);
        for(int i=0;i<60;++i){Frame();const auto current=AFState(entity);Need(current.active&&std::isfinite(current.hingeAngleDegrees),"Native source AF lost its active finite state");
            Need(current.hingeAngleDegrees>=current.minimumHingeAngleDegrees-3&&current.hingeAngleDegrees<=current.maximumHingeAngleDegrees+3,"Native world hinge escaped its source angular limits");
            maximumAngleChange=std::max(maximumAngleChange,std::abs(current.hingeAngleDegrees-initial.hingeAngleDegrees));maximumAnchorDrift=std::max(maximumAnchorDrift,(Vector(current.hingeAnchorWorld)-anchor).Length());
            Need((Vector(current.assetOriginWorld)-assetOrigin).Length()<0.001f&&(entity->GetRenderEntity()->origin-assetOrigin).Length()<0.002f,"Native body COM movement displaced the visual asset frame");
            maximumLidChange=std::max(maximumLidChange,Difference(RawJoint(entity,1),initialLid));maximumRootChange=std::max(maximumRootChange,Difference(RawJoint(entity,0),initialRoot));
        }
        Need(maximumAngleChange>0.01f&&maximumLidChange>0.0001f&&maximumRootChange<0.001f&&maximumAnchorDrift<0.25f,"Native AF did not move its source lid about a stable world hinge/root");
        while(gameLocal.time<=activated+1240)Frame();sourceState=EntityState(entity);Need(sourceState.giveEvents==1&&sourceState.pendingGiveEvents==0&&sourceState.activatedItems==0&&sourceState.expiredOpeners==0,
            "Independent source giving failed after AF or conflated literal NULL with a deleted opener");
        const idBounds lastBounds=entity->GetPhysics()->GetAbsBounds();const idClipModel* retiredClip=entity->GetPhysics()->GetClipModel(0);const idEntity* retiredOwner=entity;
        idEntityPtr<idEntity> old;old=entity;delete entity;Need(!old.GetEntity()&&Wolf2009_LiveSourceEntities()==0,"Active AF owner was not deleted");
        idClipModel* clips[64];const int count=gameLocal.clip.ClipModelsTouchingBounds(lastBounds.Expand(0.01f),request.nativeContents,clips,64);
        for(int i=0;i<count;++i)Need(clips[i]!=retiredClip&&clips[i]->GetEntity()!=retiredOwner,"Active AF deletion retained its clip model/owner in the native broadphase");
        for(int i=0;i<3;++i)Frame();
        gameLocal.Printf("WOLF2009_SOURCE_AF_NATIVE_QA_OK checks=%u frames=%u source_index=917 source_bodies=2 native_bodies=1 world_hinge=1 joint_mods=2 root_collision_excluded=1 collision_coverage_complete=0 gravity_angle_change=%.6g controlled_angle_change=%.6g lid_change=%.6g root_change=%.6g anchor_drift=%.6g rounding_excess=%.6g give_after_AF=1 reverse_refused=1 destroy_active=1\n",afChecks,afFrames,gravityAngleChange,maximumAngleChange,maximumLidChange,maximumRootChange,maximumAnchorDrift,initial.maximumRoundingExcess);
        return true;
    }catch(const std::exception& error){gameLocal.Printf("WOLF2009_SOURCE_AF_NATIVE_QA_FAIL reason=%s\n",error.what());return false;}
}

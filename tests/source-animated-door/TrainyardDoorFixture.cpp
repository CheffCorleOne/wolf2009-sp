// SPDX-License-Identifier: GPL-3.0-or-later
// Source-only QA assembly; all native playback, ACK authority, Bind and
// lifetime behavior belongs to the existing production owners and door QA.
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "TrainyardDoorFixture.h"
#include "NativeSourceAnimatedDoorChecks.h"
#include "../source-scripted-actor/TrainyardActorFixture.h"
#include "NativeSourceAnimatedDoor.h"
#include "NativeSourceMap.h"
#include "NativeSourceMover.h"
#include "NativeSourceScriptedActor.h"
#include "SourceControlledEntity.h"
#include "SourceDeclarationSetRuntime.h"
#include "SourceSession.h"
#include "EngineAnimationSet.h"
#include "AnimationWireV2.h"
#include "RenderData.h"
#include "SkinnedModelData.h"
#include "SourceRigAnimation.h"

namespace {
namespace actor=wolf2009::actor;
namespace render=wolf2009::idtech4a;
using render::Bytes;
const char* const Entities="wolf2009/sp_map/ENTS/maps/game/trainyard/trainyard";
const char* const Collision="wolf2009/sp_map/HKXS/maps/game/trainyard/trainyard";
const char* const Model="wolf2009/sp_map/modl/models/props/train_door/train_door_anim.md5r";
const char* const Rig="wolf2009/sp_map/skel/maps/game/trainyard/trainyard";
const char* const Animation="wolf2009/sp_map/hkxa/maps/game/trainyard/trainyard";
const char* const Skeleton="models/props/train_door/train_door_anim_skel.hkx";
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void NeedReason(bool value,const idStr& reason,const char* fallback){Need(value,reason.IsEmpty()?fallback:reason.c_str());}
struct Close {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
struct Sources {
    std::map<std::string,Bytes> bytes;std::size_t stored=0;
    const Bytes& Read(const std::string& path){const auto previous=bytes.find(path);if(previous!=bytes.end())return previous->second;
        std::unique_ptr<idFile,Close> file(fileSystem->OpenFileRead(path.c_str(),false));
        if(!file)throw std::runtime_error("Trainyard door fixture exact source is absent: "+path);
        const int length=file->Length();Need(file->Tell()==0&&length>0&&length<=64*1024*1024&&std::size_t(length)<=(128u<<20)-stored,"Trainyard door fixture source byte/position budget");
        Bytes data(static_cast<std::size_t>(length));std::size_t at=0;while(at<data.size()){const int n=file->Read(data.data()+at,int(data.size()-at));Need(n>0&&std::size_t(n)<=data.size()-at,"Trainyard door fixture short source read");at+=std::size_t(n);}
        unsigned char extra=0;Need(file->Read(&extra,1)==0&&file->Tell()==length&&file->Length()==length,"Trainyard door fixture source EOF/length changed");
        stored+=data.size();return bytes.emplace(path,std::move(data)).first->second;}
};
actor::ExpectedSource Identity(const Bytes& bytes){actor::ExpectedSource out;out.byteIdentityKnown=true;out.bytes=bytes.size();
    out.fingerprint=wolf2009::animation2::Fingerprint(bytes.data(),bytes.size());return out;}
std::string Instance(const wolf2009::EntityRecord& source,const char* key){const auto* value=source.Find(key);
    Need(value&&value->kind==wolf2009::EntityProperty::Text&&!value->text.empty(),"Trainyard door fixture selected instance text is absent/unqualified");return value->text;}
WolfSourceAnimatedDoorRequest DoorRequest(const WolfSourceDeclarationSet& set,const WolfSourceScriptedActorRequest& source,Sources& sources){
    WolfSourceAnimatedDoorRequest out;out.policy=WolfSourceAnimatedDoor_NativeControlledBoundSequenceV1;out.exactEntitiesPath=Entities;out.entityIndex=2193;
    out.levelGeneration=source.levelGeneration;out.context=source.definitionContext;out.signal=source.signal;out.expectedEntitiesIdentityKnown=true;
    const auto& entities=sources.Read(Entities);const auto identity=Identity(entities);
    Need(identity.bytes==source.expectedEntitiesBytes&&identity.fingerprint==source.expectedEntitiesFingerprint,"Trainyard door/actor fixture selected different original ENTS bytes");
    out.expectedEntitiesBytes=identity.bytes;out.expectedEntitiesFingerprint=identity.fingerprint;
    const auto document=wolf2009::DecodeEntities4(entities);Need(document.entities.size()==2238,"Trainyard door fixture original entity count differs");
    const auto& door=document.entities[out.entityIndex];Need(Instance(door,"name")=="train_door_1"&&Instance(door,"classname")=="env_train_door"&&
        Instance(door,"bind")=="starting_boxcar_model"&&Instance(document.entities[2129],"name")=="starting_boxcar_model","Trainyard door fixture selected a different animated door/parent");
    const auto entity=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"env_train_door",source.definitionContext);
    const auto* declaredModel=entity.Find("model");
    Need(!door.Find("model")&&declaredModel&&declaredModel->encoding==wolf2009::SourceLiteralEncoding::CompiledString&&
        declaredModel->value.size()==1&&declaredModel->value[0].text=="train_door","Trainyard door fixture effective compiled model differs");
    auto& resources=out.resources;resources.modelResource=Model;resources.meshSkeletonResource=Rig;resources.meshSkeletonName=Skeleton;
    resources.geometryProfile=actor::GeometryProfile::WeightedOrRigid64;resources.lodPolicy=actor::LodPolicy::SingleResourceWithoutLodRecords;resources.levelOfDetail=0;
    resources.expected[0]=Identity(sources.Read(Model));resources.expected[1]=Identity(sources.Read(Rig));
    const char* const logical[]={"idle","door_move","end_idle"};
    const char* const clips[]={"models/props/train_door/train_door_pose.hkx","models/props/train_door/srp_tyd_nml_train_door_anim.hkx","models/props/train_door/train_door_end.hkx"};
    const unsigned frames[]={2,182,2};const auto animationIdentity=Identity(sources.Read(Animation));
    for(unsigned i=0;i<3;++i){actor::ClipSelection clip;clip.logicalName=logical[i];clip.animationResource=Animation;clip.animationSkeletonResource=Rig;clip.clipName=clips[i];clip.animationSkeletonName=Skeleton;
        clip.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;clip.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;
        clip.expected[0]=animationIdentity;clip.expected[1]=resources.expected[1];resources.clips.push_back(clip);
        wolf2009::SourceRigRequest selected;selected.clipName=clips[i];selected.skeletonName=Skeleton;selected.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;
        const auto animation=wolf2009::SourceRigAnimation::Load(sources.Read(Animation),sources.Read(Rig),selected);
        Need(animation.FrameCount()==frames[i]&&animation.Skeleton().joints.size()==2,"Trainyard door fixture original clip/rig counts differ");}
    const auto request=render::MakeSkinnedMeshRequestV2(Model,Rig,Skeleton,0,wolf2009::SkinnedVertexProfile::WeightedOrRigid64,wolf2009::SkinnedLodPolicy::SingleResourceWithoutLodRecords);
    const auto mesh=render::LoadSkinnedMeshV2(request,[&](const std::string& path){return sources.Read(path);});
    Need(mesh.joints.size()==2&&mesh.joints[0].name=="origin"&&mesh.joints[1].name=="door_joint"&&!mesh.surfaces.empty()&&!mesh.sourceHasLodRecords&&mesh.selectionUsesCompatibilitySlot&&
        mesh.levelOfDetail==0&&mesh.lodPolicy==wolf2009::SkinnedLodPolicy::SingleResourceWithoutLodRecords,
        "Trainyard door fixture original two-joint/single-resource mesh qualification failed");
    common->Printf("WOLF2009_TRAINYARD_DOOR_FIXTURE_DATA entity=2193 parent=2129 source_mount=sp_map model=%s skeleton=%s joints=2 idle_frames=2 move_frames=182 end_frames=2 source_lod_records=0 compatibility_slot_used=1 explicit_slot=0 no_collision_policy=compiled_guard actor_start=actual_owner vo_ready_ack=0 trainyard_world=0 full_campaign=0\n",Model,Skeleton);
    return out;
}
bool StartActor(const void* raw,const wolf2009::TrainDoorCommand& command,idStr& reason){
    if(!raw){reason="Trainyard door fixture actual actor owner is absent";return false;}
    WolfSourceScriptedActorStartAck ack;return const_cast<WolfSourceScriptedActor*>(static_cast<const WolfSourceScriptedActor*>(raw))->Start(command,ack,reason);
}
void NativeDoorModel(const WolfSourceDeclarationSet& set,const WolfSourceAnimatedDoorRequest& request){
    // MD5 templates store meshes, whereas their posed dynamic snapshots store
    // render surfaces. Inspect both actual native representations explicitly.
    const auto plan=actor::Prepare(set.Definitions(),set.Namespace(),"env_train_door","train_door",request.context,request.resources);idStr reason;
    const auto binding=Wolf2009_BindSourceActor(plan,std::vector<WolfSourceActorSoundBinding>(),reason);NeedReason(bool(binding),reason,"Trainyard door fixture native model binding failed");
    const auto* definition=Wolf2009_FindRegisteredSourceAnimationSet(binding->NativeModelDefinition());auto* model=definition?definition->ModelHandle():NULL;
    Need(model&&model->NumJoints()==2&&definition->NumJoints()==2&&model->GetJoints()&&model->GetDefaultPose(),"Trainyard door fixture original native hierarchy absent");
    idJointMat* joints=static_cast<idJointMat*>(Mem_Alloc16(2*sizeof(idJointMat)));Need(joints,"Trainyard door fixture native joint allocation failed");idRenderModel* snapshot=NULL;
    try{const auto* hierarchy=model->GetJoints();const auto* local=model->GetDefaultPose();
        Need(!hierarchy[0].parent&&hierarchy[1].parent==hierarchy,"Trainyard door fixture native joint hierarchy differs");
        for(int i=0;i<2;++i){joints[i].SetRotation(local[i].q.ToMat3());joints[i].SetTranslation(local[i].t);if(i)joints[i]*=joints[0];}
        renderEntity_t entity;std::memset(&entity,0,sizeof(entity));entity.hModel=model;entity.numJoints=2;entity.joints=joints;entity.axis.Identity();for(int i=0;i<4;++i)entity.shaderParms[i]=1;
        snapshot=model->InstantiateDynamicModel(&entity,NULL,NULL);
        common->Printf("WOLF2009_TRAINYARD_DOOR_NATIVE_MODEL model=%s joints=%d definition_joints=%d names=(%s,%s) template_base_surfaces=%d dynamic=%d snapshot_surfaces=%d snapshot_base_surfaces=%d\n",
            model->Name(),model->NumJoints(),definition->NumJoints(),definition->GetJointName(0),definition->GetJointName(1),model->NumBaseSurfaces(),int(model->IsDynamicModel()),snapshot?snapshot->NumSurfaces():-1,snapshot?snapshot->NumBaseSurfaces():-1);
        Need(model->IsDynamicModel()==DM_CACHED&&std::string(definition->GetJointName(0))=="origin"&&std::string(definition->GetJointName(1))=="door_joint"&&snapshot&&snapshot->NumBaseSurfaces()>0,
            "Trainyard door fixture original native dynamic geometry differs");
        for(int i=0;i<snapshot->NumBaseSurfaces();++i){const auto* surface=snapshot->Surface(i);Need(surface&&surface->geometry&&surface->geometry->numVerts>0&&surface->geometry->numIndexes>0&&surface->shader&&surface->shader->IsDrawn(),
            "Trainyard door fixture native dynamic surface absent");}
    }catch(...){if(snapshot)renderModelManager->FreeModel(snapshot);Mem_Free16(joints);throw;}
    renderModelManager->FreeModel(snapshot);Mem_Free16(joints);
}
void FreeNames(){for(const auto* name:{"starting_boxcar_model","train_greeter","train_door_1"})Need(!gameLocal.FindEntity(name),"Trainyard door fixture source name is already occupied");}
void RunCase(const WolfSourceDeclarationSet& set,unsigned selected){
    FreeNames();const int controlled=Wolf2009_LiveSourceControlledEntities(),actors=Wolf2009_LiveSourceScriptedActors(),movers=Wolf2009_LiveSourceMovers();idStr reason;
    WolfSourceScriptedActorRequest actorRequest;NeedReason(Wolf2009_PrepareTrainyardScriptedActorFixtureRequest(set,"sp_map",actorRequest,reason),reason,"Trainyard door fixture actor request preparation failed");
    Sources sources;auto request=DoorRequest(set,actorRequest,sources);
    WolfSourceMapRequest mapRequest;mapRequest.sourceMapName="maps/game/trainyard/trainyard";mapRequest.exactEntities=Entities;mapRequest.exactCollision=Collision;
    mapRequest.materialArchive=actorRequest.materialArchive;mapRequest.profile="MeasuredTrainyardIntroV1";
    // Declaration order and explicit resets keep the real child (in the QA
    // callee), then actor, then map/mover dead before their source owners.
    auto map=Wolf2009_PrepareSourceMapRuntime(set,mapRequest,reason);NeedReason(bool(map),reason,"Trainyard door fixture source map preparation failed");
    auto actor=Wolf2009_PrepareSourceScriptedActor(set,actorRequest,reason);NeedReason(bool(actor),reason,"Trainyard door fixture actual actor preparation failed");
    NeedReason(actor->SpawnInactive(reason),reason,"Trainyard door fixture actual actor Spawn failed");
    NeedReason(actor->StartBinding(request.actorStart,reason),reason,"Trainyard door fixture actual actor authority unavailable");
    const int assemblyTime=gameLocal.time;NeedReason(map->Spawn(reason),reason,"Trainyard door fixture actual source wagon Spawn failed");
    Need(gameLocal.time==assemblyTime,"Trainyard door fixture advanced a frame during native assembly");
    WolfSourceMapState source;NeedReason(map->Query(source,reason),reason,"Trainyard door fixture map state is unavailable");
    Need(source.spawned&&source.sourceMovers==1&&!source.sourceStatics&&!source.sourceRelays&&!source.sourceDirectors&&source.bodies==535&&source.collisionShapes==86,
        "Trainyard door fixture did not use the isolated original intro-only map owner");
    auto* parent=map->PrimaryMover();WolfSourceMoverState mover;NeedReason(Wolf2009_QuerySourceMover(parent,mover,reason),reason,"Trainyard door fixture actual wagon state unavailable");
    Need(parent&&gameLocal.FindEntity("starting_boxcar_model")==parent&&mover.entityIndex==2129&&mover.phase==WolfSourceMover_Moving&&mover.nativePusher&&mover.typedSourceResources&&
        !mover.visualDiagnostic&&mover.spawnDeliveries==1&&!mover.arrivals&&mover.startTime==assemblyTime&&mover.entitiesBytes==request.expectedEntitiesBytes&&mover.entitiesFingerprint==request.expectedEntitiesFingerprint,
        "Trainyard door fixture requires the real unadvanced source wagon after actual OnSpawn");
    NativeDoorModel(set,request);
    WolfSourceAnimatedDoorActorStartDriver driver;driver.context=actor.get();driver.start=StartActor;
    bool ok=false;if(selected==0)ok=Wolf2009_CheckNativeSourceAnimatedDoorQA(set,request,parent,driver);
    else if(selected==1)ok=Wolf2009_CheckNativeSourceAnimatedDoorStaleStartQA(set,request,parent,driver);
    else if(selected==2)ok=Wolf2009_CheckNativeSourceAnimatedDoorReplacementQA(set,request,parent,driver);
    Need(ok,"Actual animated-door QA failed; see WOLF2009_SOURCE_ANIMATED_DOOR*_QA FAIL");
    actor.reset();map.reset();FreeNames();
    Need(Wolf2009_LiveSourceControlledEntities()==controlled&&Wolf2009_LiveSourceScriptedActors()==actors&&Wolf2009_LiveSourceMovers()==movers,"Trainyard door fixture owned native teardown leaked");
    common->Printf("WOLF2009_TRAINYARD_DOOR_FIXTURE_CASE_OK case=%u generation=%llu actual_wagon=1 actual_actor_owner=1 native_names_released=1\n",selected,static_cast<unsigned long long>(request.levelGeneration));
}
}

bool Wolf2009_RunTrainyardAnimatedDoorFixtureQA(idStr& reason){reason.Clear();try{
    Need(gameLocal.GameState()==GAMESTATE_ACTIVE&&Wolf2009_HasSourceSession()&&!Wolf2009_SourceSessionMapRuntime(),"Trainyard door fixture requires an active isolated empty source session without a campaign map owner");
    auto map=render::ParseResourcePath(gameLocal.GetMapName());
    // Generated map filenames retain .map in GetMapName; use the same final
    // extension removal as the source-session map identity check.
    if(map.name.size()>4&&map.name.compare(map.name.size()-4,4,".map")==0)map.name.resize(map.name.size()-4);
    if(map.mount!="main"||map.type!="proc"||map.name!="maps/game/empty/empty")
        throw std::runtime_error(std::string("Trainyard door fixture requires the original explicit empty map: ")+gameLocal.GetMapName()+" ["+map.mount+","+map.type+","+map.name+"]");
    const auto* set=Wolf2009_SourceSessionDeclarations();Need(set,"Trainyard door fixture selected compiled namespace is absent");
    for(unsigned selected=0;selected<3;++selected)RunCase(*set,selected);
    common->Printf("WOLF2009_TRAINYARD_ANIMATED_DOOR_FIXTURE_QA_OK fresh_cases=3 actual_wagon=1 actual_actor_start=1 native_door_qa=1 stale_start_qa=1 replacement_qa=1 teardown=1 trainyard_world=0 vo_ready_ack=0 full_campaign=0\n");return true;
}catch(const std::exception& error){reason=error.what();return false;}}

// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit source QA selection only; native behavior belongs to the existing
// scripted-actor owner and its actual game-frame/authority/lifetime checks.
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "TrainyardActorFixture.h"
#include "NativeSourceScriptedActor.h"
#include "SourceDeclarationSetRuntime.h"
#include "SourceSession.h"
#include "AnimationWireV2.h"
#include "Dds.h"
#include "Material.h"
#include "RenderData.h"
#include "SkinnedModelData.h"
#include "SkinCatalog.h"
#include "SourceRigAnimation.h"
#include "ZipArchive.h"

// Existing native QA entry. This glue does not duplicate its factory or ACK.
bool Wolf2009_CheckNativeSourceScriptedActorQA(const WolfSourceDeclarationSet&,
    const WolfSourceScriptedActorRequest&);

namespace {
namespace actor=wolf2009::actor;
namespace render=wolf2009::idtech4a;
using render::Bytes;
const char* const Body="wolf2009/sp_greeter/modl/models/characters/civilians/civ_male_04/anims/body_cm04.md5r";
const char* const GlobalRig="wolf2009/sp_global/skel/maps/game/global/global";
const char* const Animation="wolf2009/sp_map/hkxa/maps/game/trainyard/trainyard";
const char* const EmptyMale="models/characters/_humanoid_male/empty_male_skel.hkx";
const char* const HumanoidMale="models/characters/_humanoid_male/humanoid_male.hkx";
const char* const Clip="models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx";
const char* const Materials="fixtures/wolf-materials.bin";
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void NeedReason(bool value,const idStr& reason,const char* fallback){Need(value,reason.IsEmpty()?fallback:reason.c_str());}
std::string SourcePrefix(const char* exactMount){Need(exactMount&&*exactMount,"Trainyard actor fixture requires an explicit source mount");
    const std::string mount(exactMount);const auto path=render::ParseResourcePath("wolf2009/"+mount+"/ents/maps/game/trainyard/trainyard");
    Need(path.mount==mount&&path.type=="ents"&&path.name=="maps/game/trainyard/trainyard","Trainyard actor fixture source mount is not canonical");return path.Prefix();}
struct Close {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
struct Sources {
    std::map<std::string,Bytes> bytes;std::size_t stored=0;
    const Bytes& Read(const std::string& path){const auto prior=bytes.find(path);if(prior!=bytes.end())return prior->second;
        std::unique_ptr<idFile,Close> file(fileSystem->OpenFileRead(path.c_str(),false));
        if(!file)throw std::runtime_error("Trainyard actor fixture exact source is absent: "+path);
        const int length=file->Length();Need(file->Tell()==0&&length>0&&length<=64*1024*1024&&std::size_t(length)<=(128u<<20)-stored,"Trainyard actor fixture source byte/position budget");
        Bytes data(static_cast<std::size_t>(length));std::size_t at=0;while(at<data.size()){const int n=file->Read(data.data()+at,int(data.size()-at));Need(n>0&&std::size_t(n)<=data.size()-at,"Trainyard actor fixture short source read");at+=std::size_t(n);}
        unsigned char extra=0;Need(file->Read(&extra,1)==0&&file->Tell()==length&&file->Length()==length,"Trainyard actor fixture source EOF/length changed");
        stored+=data.size();return bytes.emplace(path,std::move(data)).first->second;}
};
actor::ExpectedSource Identity(const Bytes& bytes){actor::ExpectedSource out;out.byteIdentityKnown=true;out.bytes=bytes.size();
    out.fingerprint=wolf2009::animation2::Fingerprint(bytes.data(),bytes.size());return out;}
std::string Literal(const wolf2009::ResolvedSourceDefinition& source,const char* key){const auto* value=source.Find(key);
    Need(value&&(value->encoding==wolf2009::SourceLiteralEncoding::CompiledString||value->encoding==wolf2009::SourceLiteralEncoding::PlainText)&&value->value.size()==1&&!value->value[0].text.empty(),
        "Trainyard actor fixture selected source field is not one literal");return value->value[0].text;}
std::string Instance(const wolf2009::EntityRecord& source,const char* key){const auto* value=source.Find(key);
    Need(value&&value->kind==wolf2009::EntityProperty::Text&&!value->text.empty(),"Trainyard actor fixture selected instance text is absent/unqualified");return value->text;}

WolfSourceScriptedActorRequest Request(const WolfSourceDeclarationSet& set,const char* exactSourceMount,Sources& sources){
    WolfSourceScriptedActorRequest out;out.policy=WolfSourceScriptedActorPolicy::NativeFullPoseNoExtractionNoFrameCommandsV1;
    const auto prefix=SourcePrefix(exactSourceMount);out.exactEntitiesPath=prefix+"ents/maps/game/trainyard/trainyard";out.materialArchive=Materials;out.entityIndex=2034;out.expectedJoints=52;out.expectedFrames=182;out.initialSourceFrame=0;
    const auto& entities=sources.Read(out.exactEntitiesPath);const auto document=wolf2009::DecodeEntities4(entities);Need(document.entities.size()>out.entityIndex,"Trainyard actor fixture original ENTS2034 absent");
    const auto& instance=document.entities[out.entityIndex];Need(Instance(instance,"name")=="train_greeter"&&Instance(instance,"classname")=="trainyard_greeter"&&
        Instance(instance,"brain")=="trainyard/train_greeter.brain"&&Instance(instance,"anim")=="srp_tyd_nml_train_door_open","Trainyard actor fixture selected a different original actor");
    out.expectedEntitiesBytes=entities.size();out.expectedEntitiesFingerprint=wolf2009::animation2::Fingerprint(entities.data(),entities.size());
    out.signal=wolf2009::DecodeTrainDoorSignalH1023(sources.Read(prefix+"brai/brains/trainyard/train_greeter"),"brains/trainyard/train_greeter","train_greeter_brainuse_1",document);
    Need(out.signal.ownerEntityName=="train_greeter"&&out.signal.ownerClassname=="trainyard_greeter"&&out.signal.useKey=="train_stop"&&out.signal.actorAnimation=="srp_tyd_nml_train_door_open"&&
        out.signal.onUsedNodeId==3&&out.signal.voiceNodeId==29&&out.signal.actorStartNodeId==35&&out.signal.targetEntityName=="train_door_1"&&out.signal.targetClassname=="env_train_door",
        "Trainyard actor fixture decoded a different original signal chain");
    out.definitionContext.mapModelDefinition="model_trainyard_base";
    const auto entity=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"trainyard_greeter",out.definitionContext);
    const auto model=set.Definitions().ResolveModelAnimations(Literal(entity,"model"),out.definitionContext);
    Need(Literal(model.model,"mesh")=="models/characters/civilians/civ_male_04/anims/body_cm04.md5r"&&Literal(model.model,"skeleton")==EmptyMale,
        "Trainyard actor fixture source model differs from its explicit declared EmptyMale mesh selection");
    auto& resources=out.resources;resources.modelResource=Body;resources.meshSkeletonResource=GlobalRig;resources.meshSkeletonName=EmptyMale;
    resources.geometryProfile=actor::GeometryProfile::Weighted64;resources.lodPolicy=actor::LodPolicy::Authored;resources.levelOfDetail=0;
    resources.expected[0]=Identity(sources.Read(Body));resources.expected[1]=Identity(sources.Read(GlobalRig));
    actor::ClipSelection clip;clip.logicalName=out.signal.actorAnimation;clip.animationResource=Animation;clip.animationSkeletonResource=GlobalRig;
    clip.clipName=Clip;clip.animationSkeletonName=HumanoidMale;clip.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;clip.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;
    clip.expected[0]=Identity(sources.Read(Animation));clip.expected[1]=resources.expected[1];resources.clips.push_back(clip);
    auto meshRequest=render::MakeSkinnedMeshRequestV2(resources.modelResource,resources.meshSkeletonResource,resources.meshSkeletonName,0,wolf2009::SkinnedVertexProfile::Weighted64,wolf2009::SkinnedLodPolicy::Authored);
    const auto mesh=render::LoadSkinnedMeshV2(meshRequest,[&](const std::string& path){return sources.Read(path);});bool authoredZero=false;
    for(const auto& fact:mesh.sourceMeshLods)authoredZero|=fact.hasLevelOfDetail&&fact.levelOfDetail==0;
    Need(mesh.levelOfDetail==0&&mesh.lodPolicy==wolf2009::SkinnedLodPolicy::Authored&&mesh.sourceHasLodRecords&&!mesh.selectionUsesCompatibilitySlot&&authoredZero&&mesh.joints.size()==52&&!mesh.surfaces.empty(),
        "Trainyard actor fixture original authored LOD0/52-joint mesh qualification failed");
    wolf2009::SourceRigRequest animation;animation.clipName=Clip;animation.skeletonName=HumanoidMale;animation.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;
    const auto rig=wolf2009::SourceRigAnimation::Load(sources.Read(Animation),sources.Read(GlobalRig),animation);
    Need(rig.FrameCount()==182&&rig.Skeleton().joints.size()==52,"Trainyard actor fixture full-pose Humanoid clip counts differ");
    // Resolve the actual skin before checking selected diffuse images. The raw
    // generic body material is remapped; no alternate texture mount is tried.
    const auto& archive=sources.Read(Materials);const auto catalog=render::ReadMaterialCatalog(archive);
    std::istringstream input(std::string(archive.begin(),archive.end()),std::ios::in|std::ios::binary);const auto zip=wolf2009::ZipArchive::Scan(input);const auto skins=wolf2009::SkinCatalog::Read(input,zip);
    const std::string skin=instance.Find("skin")?Instance(instance,"skin"):Literal(entity,"skin");wolf2009::GeometryScene selected;
    for(const auto& surface:mesh.surfaces){wolf2009::GeometrySurface value;value.model=surface.geometry.model;value.material=surface.geometry.material;selected.surfaces.push_back(std::move(value));}
    const auto applied=skins.Apply(skin,selected);std::set<std::string> images;const auto modelSource=render::ParseResourcePath(resources.modelResource);
    for(std::size_t i=0;i<mesh.surfaces.size();++i){auto draw=mesh.surfaces[i].geometry;draw.material=selected.surfaces[i].material;const auto diffuse=render::BuildDiffuseMaterial(modelSource,draw,catalog.get());
        Need(diffuse.supported&&!diffuse.image.empty()&&render::ParseResourcePath(diffuse.image).mount=="sp_greeter","Trainyard actor fixture selected skin has no qualified diffuse in its explicit detail mount");
        if(images.insert(diffuse.image).second){const auto image=wolf2009::DecodeDdsMip0(sources.Read(diffuse.image));Need(image.width&&image.height&&!image.rgba.empty(),"Trainyard actor fixture selected source DDS is empty");}}
    Need(!images.empty(),"Trainyard actor fixture selected no original diffuse textures");
    // A separate explicit QA generation, not a guessed live director event.
    static std::uint64_t nextGeneration=UINT64_C(0x5459414354000001);Need(nextGeneration!=(std::numeric_limits<std::uint64_t>::max)(),"Trainyard actor fixture QA generation exhausted");out.levelGeneration=nextGeneration++;
    common->Printf("WOLF2009_TRAINYARD_ACTOR_FIXTURE_DATA entity=2034 entities_mount=%s model=%s mesh_skeleton=%s animation_skeleton=%s authored_lod=0 lod_records=%u mesh_lod_records=%u surfaces=%u selected_images=%u skin=%s skin_changes=%u joints=52 frames=182 initial_source_frame=0 automatic_rig_selection=0 body_only=1 vo_ready_ack=0 full_campaign=0\n",
        exactSourceMount,resources.modelResource.c_str(),resources.meshSkeletonName.c_str(),clip.animationSkeletonName.c_str(),unsigned(mesh.sourceLodDistances.size()),unsigned(mesh.sourceMeshLods.size()),unsigned(mesh.surfaces.size()),unsigned(images.size()),skin.c_str(),unsigned(applied.changedSurfaces));
    return out;
}
}

bool Wolf2009_PrepareTrainyardScriptedActorFixtureRequest(const WolfSourceDeclarationSet& set,const char* exactSourceMount,WolfSourceScriptedActorRequest& out,idStr& reason){reason.Clear();try{
    Sources sources;auto prepared=Request(set,exactSourceMount,sources);out=std::move(prepared);return true;
}catch(const std::exception& error){reason=error.what();return false;}}

bool Wolf2009_RunTrainyardScriptedActorFixtureQA(idStr& reason){reason.Clear();try{
    const auto* set=Wolf2009_SourceSessionDeclarations();Need(Wolf2009_HasSourceSession()&&set,"Trainyard actor fixture requires the ordinary source-session namespace");
    WolfSourceScriptedActorRequest request;NeedReason(Wolf2009_PrepareTrainyardScriptedActorFixtureRequest(*set,"main",request,reason),reason,"Trainyard actor fixture request preparation failed");
    Need(Wolf2009_CheckNativeSourceScriptedActorQA(*set,request),"Actual scripted-actor owner QA failed; see WOLF2009_SOURCE_SCRIPTED_ACTOR_QA_FAIL");
    common->Printf("WOLF2009_TRAINYARD_SCRIPTED_ACTOR_FIXTURE_QA_OK original_namespace=1 exact_signal=1 explicit_detail_mount=1 authored_lod=0 actual_owner_qa=1 vo_ready_ack=0 later_brain=0 full_campaign=0\n");return true;
}catch(const std::exception& error){reason=error.what();return false;}}

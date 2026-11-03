// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in core QA. Only two alias strings cross the existing game-probe ABI.
// All package bytes are read through the initialized ordinary filesystem.
#include "AnimationWireV2.h"
#include "EngineSkinnedModelData.h"
#include "SourceRigAnimation.h"
#include "ZipArchive.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "EngineAnimationBinding.h"
#include "EngineAnimationBindingV2.h"
#include "EngineAnimationQA.h"

namespace {
namespace wire=wolf2009::animation2;
namespace render=wolf2009::idtech4a;
const char* const TrainPackage="maps/game/trainyard/trainyard.mpk";
const char* const GlobalPackage="maps/game/global/global.spk";
const char* const CharacterPackage="streampacks/allies_kreisau_agt02.spk";
const char* const BodyModel="models/characters/civilians/civ_male_04/anims/body_cm04.md5r";
const char* const EmptyRig="models/characters/_humanoid_male/empty_male_skel.hkx";
const char* const HumanoidRig="models/characters/_humanoid_male/humanoid_male.hkx";
const char* const GreeterClip="models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx";
const char* const IdleClip="models/characters/_humanoid_male/st1_hg1_nml_idle.hkx";
const char* const PickupClip="models/characters/_humanoid_male/srp_tyd_mg1_order_pickup.hkx";
const char* const HeadRig="models/characters/civilians/head_male_03/anims/head_civilian_male_03_mesh_skel.hkx";
const char* const LockerModel="models/props/locker_foot_01_anim.md5r";
const char* const LockerRig="models/props/locker_foot_01_anim_skel.hkx";
const char* const LockerClip="models/props/locker_foot_01_anim_open.hkx";
const char* const LockerClose="models/props/locker_foot_01_anim_close.hkx";

struct Stats {
    std::size_t checks=0,gameProbes=0,setProbes=0,declarationProbes=0,refusals=0,descriptors=0,epochCases=0;
};
void Need(Stats& stats,bool value,const char* why) {
    ++stats.checks;
    if(!value)throw std::runtime_error(why);
}
struct FileCloser { void operator()(idFile* file)const {if(file)fileSystem->CloseFile(file);} };
typedef std::unique_ptr<idFile,FileCloser> File;
struct Mounted { std::string label,package;bool active=true; };
struct Resources {
    std::vector<Mounted> mounts;
    std::vector<idRenderModel*> models;
    std::vector<std::string> bindings;
    bool mayDeleteModels=true;
    bool cached=r_useCachedDynamicModels.GetBool();
    int show=r_showSkel.GetInteger();
    Resources() {
        mounts.reserve(16);models.reserve(32);bindings.reserve(32);
        r_useCachedDynamicModels.SetBool(true);r_showSkel.SetInteger(0);
    }
    void Mount(Stats& stats,const std::string& label,const char* package) {
        Need(stats,label!="main","v2 QA must never mount the active world label");
        for(const auto& item:mounts)Need(stats,item.label!=label||!item.active,"duplicate active v2 QA mount");
        for(auto& item:mounts)if(item.label==label){
            item.package=package;
            Need(stats,Wolf2009_Mount(label.c_str(),package),"v2 QA original package remount failed");item.active=true;return;
        }
        // Allocate ownership metadata before acquiring a backend mount.
        Mounted item;item.label=label;item.package=package;item.active=false;mounts.push_back(std::move(item));
        Need(stats,Wolf2009_Mount(label.c_str(),package),"v2 QA original package mount failed");mounts.back().active=true;
    }
    Mounted& Find(const std::string& label) {
        for(auto& item:mounts)if(item.label==label&&item.active)return item;
        throw std::runtime_error("v2 QA does not own the active mount");
    }
    void Unmount(Stats& stats,const std::string& label) {
        auto& item=Find(label);
        Need(stats,Wolf2009_Unmount(label.c_str()),"v2 QA source mount still has an unexpected owner");item.active=false;
    }
    void Pinned(Stats& stats,const std::string& label) {
        auto& item=Find(label);const bool removed=Wolf2009_Unmount(label.c_str());
        if(removed)item.active=false;
        Need(stats,!removed,"v2 descriptor/model failed to retain a selected source lease");
    }
    void Remount(Stats& stats,const std::string& label) {
        const std::string package=Find(label).package;
        Unmount(stats,label);Mount(stats,label,package.c_str());
    }
    void Remember(idRenderModel* model) {
        if(model&&model!=renderModelManager->DefaultModel()&&std::find(models.begin(),models.end(),model)==models.end())models.push_back(model);
    }
    void Binding(const idStr& alias) {
        const std::string name=alias.c_str();
        if(std::find(bindings.begin(),bindings.end(),name)==bindings.end())bindings.push_back(name);
    }
    void DeleteModels() {
        if(!mayDeleteModels)throw std::runtime_error("v2 QA cannot delete models while a failed game probe still owns source descriptors");
        for(auto* model:models){renderModelManager->RemoveModel(model);renderModelManager->FreeModel(model);}
        models.clear();
    }
    ~Resources() {
        // A failed/throwing game probe must release its own defs/anims first.
        // If it did not, retain manager ownership until normal game shutdown
        // rather than free models out from under active game references.
        if(mayDeleteModels)DeleteModels();
        else fprintf(stderr,"WOLF2009_ANIMATION_V2_CLEANUP_DEFERRED game_descriptor_owner=1\n");
        for(const auto& alias:bindings)Wolf2009_RemoveAnimationBindingV2(alias.c_str());
        for(auto& item:mounts)if(item.active&&!Wolf2009_Unmount(item.label.c_str()))
            fprintf(stderr,"WOLF2009_ANIMATION_V2_CLEANUP_PINNED mount=%s\n",item.label.c_str());
        r_useCachedDynamicModels.SetBool(cached);r_showSkel.SetInteger(show);
    }
};
struct GlobalDeclarationFixture {
    std::string path="fixtures/wolf2009-source-global-qa.decls";
    bool owned=false;
    ~GlobalDeclarationFixture(){if(owned)fileSystem->RemoveFile(path.c_str());}
};
std::string Path(const std::string& label,const char* type,const char* resource) {
    return "wolf2009/"+label+"/"+type+"/"+resource;
}
struct Sources {
    std::array<std::string,4> paths;
    std::string meshName,animationName;
    int geometryProfile=1,lodPolicy=1;
};
Sources Body(const std::array<std::string,4>& labels) {
    Sources out;
    out.paths={{Path(labels[0],"modl",BodyModel),Path(labels[1],"skel","maps/game/global/global"),
                Path(labels[2],"hkxa","maps/game/trainyard/trainyard"),Path(labels[3],"skel","maps/game/global/global")}};
    out.meshName=EmptyRig;out.animationName=HumanoidRig;return out;
}
std::array<std::string,4> Labels(const Sources& sources) {
    std::array<std::string,4> out;
    for(unsigned i=0;i<4;++i)out[i]=render::ParseResourcePath(sources.paths[i]).mount;
    return out;
}
std::uint64_t Epoch(Stats& stats,const std::string& label) {
    unsigned long long epoch=0;
    Need(stats,Wolf2009_MountEpoch(label.c_str(),epoch)&&epoch,"v2 QA selected source epoch absent");return epoch;
}
wire::Bytes ReadSource(const std::string& path) {
    File file(fileSystem->OpenFileRead(path.c_str(),false));
    if(!file)throw std::runtime_error("v2 QA original source is absent from its exact mount");
    const int count=file->Length();
    if(count<=0||std::size_t(count)>wire::MaximumSourceBytes)throw std::runtime_error("v2 QA original source byte budget");
    wire::Bytes out(static_cast<std::size_t>(count));
    if(file->Read(out.data(),count)!=count)throw std::runtime_error("v2 QA original source short read");return out;
}
void PrepareGlobalDeclarations(Stats& stats,GlobalDeclarationFixture& fixture) {
    // The runtime fixture keeps the owner's PK4 under a .bin name so the GPL
    // decl manager cannot automatically parse incompatible retail definitions.
    // Extract only this independently identified member to our own QA directory.
    const auto bytes=ReadSource("fixtures/wolf-materials.bin");
    std::istringstream input(std::string(bytes.begin(),bytes.end()),std::ios::in|std::ios::binary);
    const auto archive=wolf2009::ZipArchive::Scan(input);
    const std::string wanted="maps/game/global/global.decls";std::size_t matches=0,index=0;
    for(std::size_t i=0;i<archive.Entries().size();++i){std::string name=archive.Entries()[i].name;
        for(char& c:name){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
        if(name==wanted){++matches;index=i;}
    }
    Need(stats,matches==1&&archive.Entries()[index].size==2175123,"selected global declaration ZIP member is missing/ambiguous/changed");
    const auto payload=archive.ReadEntry(input,index);
    File prior(fileSystem->OpenFileRead(fixture.path.c_str(),false));
    Need(stats,!prior,"source declaration QA fixture namespace is already occupied");
    fixture.owned=true;
    Need(stats,fileSystem->WriteFile(fixture.path.c_str(),payload.data(),int(payload.size()))==int(payload.size()),
         "source global declaration QA fixture write failed");
}
struct Mesh {
    idStr alias;
    idRenderModelMD5* model=NULL;
    render::SkinnedMeshRequestV2 request;
};
Mesh Load(Stats& stats,Resources& resources,const Sources& sources) {
    Mesh out;
    Need(stats,Wolf2009_RegisterSkinnedMeshV2(sources.paths[0].c_str(),sources.paths[1].c_str(),sources.meshName.c_str(),
         0,sources.geometryProfile,sources.lodPolicy,out.alias),"v2 QA mesh registration failed");
    Need(stats,Wolf2009_GetSkinnedMeshRequestV2(out.alias,out.request),"v2 QA mesh request missing");
    auto* model=renderModelManager->FindModel(out.alias);
    resources.Remember(model);out.model=dynamic_cast<idRenderModelMD5*>(model);
    Need(stats,out.model&&model->IsLoaded()&&!model->IsDefaultModel()&&model->IsDynamicModel()==DM_CACHED&&
         idStr::Icmp(model->Name(),out.alias.c_str())==0,"v2 QA normal manager did not load the selected native MD5");
    render::SkinnedMeshSourceInfoV2 info;
    Need(stats,Wolf2009_GetSkinnedMeshSourcesV2(out.model,info)&&render::SameSkinnedMeshRequestV2(info.request,out.request),
         "v2 QA normal MD5 lacks its complete qualified source identity");return out;
}
int ParentIndex(const idMD5Joint* joints,int count,const idMD5Joint* parent) {
    if(!parent)return -1;
    for(int i=0;i<count;++i)if(parent==&joints[i])return i;
    return -2;
}
void SameHierarchy(Stats& stats,const Mesh& a,const Mesh& b) {
    Need(stats,a.alias!=b.alias&&a.model!=b.model&&a.model->NumJoints()==b.model->NumJoints(),"v2 QA wrong-identity control is not independent");
    const auto* x=a.model->GetJoints();const auto* y=b.model->GetJoints();const int count=a.model->NumJoints();
    Need(stats,x&&y&&count>0,"v2 QA wrong-identity control has no hierarchy");
    for(int i=0;i<count;++i){
        const int pa=ParentIndex(x,count,x[i].parent),pb=ParentIndex(y,count,y[i].parent);
        Need(stats,pa>=-1&&pb>=-1&&x[i].name==y[i].name&&pa==pb,"v2 QA identity control does not have the same named hierarchy");
    }
}
idStr Bind(Stats& stats,Resources& resources,const Mesh& mesh,const Sources& sources,const char* clip) {
    idStr alias="must_clear";
    Need(stats,Wolf2009_RegisterSourceAnimationV2(mesh.alias,sources.paths[2].c_str(),sources.paths[3].c_str(),clip,
         sources.animationName.c_str(),1,alias)&&!alias.IsEmpty()&&Wolf2009_IsSourceAnimationV2Alias(alias),"v2 QA explicit clip/rig registration failed");
    resources.Binding(alias);idStr repeated;
    Need(stats,Wolf2009_RegisterSourceAnimationV2(mesh.alias,sources.paths[2].c_str(),sources.paths[3].c_str(),clip,
         sources.animationName.c_str(),1,repeated)&&repeated==alias,"v2 QA complete-key registration was not idempotent");return alias;
}
wire::Envelope ReadDescriptor(Stats& stats,idFile* file,const char* alias) {
    Need(stats,file&&file->Tell()==0,"ordinary filesystem did not open/reset the v2 descriptor");
    const int length=file->Length();
    Need(stats,length>0&&std::size_t(length)<=wire::MaximumEnvelopeBytes&&file->Timestamp()==0&&
         idStr::Icmp(file->GetName(),alias)==0&&idStr::Icmp(file->GetFullPath(),alias)==0,"v2 descriptor file metadata differs");
    wire::Bytes bytes(static_cast<std::size_t>(length));
    Need(stats,file->Read(bytes.data(),length)==length&&file->Tell()==length,"short owned v2 descriptor read");
    unsigned char extra=0;
    Need(stats,file->Read(&extra,1)==0&&file->Seek(1,FS_SEEK_END)==-1&&file->Tell()==length,"v2 descriptor EOF/end seek changed range/cursor");
    Need(stats,file->Seek(-1,FS_SEEK_END)==0&&file->Tell()==length-1&&file->Read(&extra,1)==1&&extra==bytes.back(),"v2 descriptor bounded end seek/read failed");
    Need(stats,file->Seek(0,FS_SEEK_SET)==0&&file->Seek(-1,FS_SEEK_SET)==-1&&file->Tell()==0&&
         file->Seek(LONG_MIN,FS_SEEK_SET)==-1&&file->Seek(LONG_MAX,FS_SEEK_SET)==-1&&file->Tell()==0&&
         file->Seek(0,static_cast<fsOrigin_t>(99))==-1&&file->Tell()==0,"v2 descriptor rejected seek did not preserve cursor");
    auto envelope=wire::Parse(bytes);
    Need(stats,wire::Alias(envelope.binding)==alias,"v2 descriptor alias differs from its complete metadata");++stats.descriptors;return envelope;
}
void CheckEnvelope(Stats& stats,const wire::Envelope& envelope,const Mesh& mesh,const Sources& sources,const char* clip,unsigned frames) {
    const auto& binding=envelope.binding;
    Need(stats,binding.modelAlias==mesh.alias.c_str()&&binding.paths==sources.paths&&binding.meshSkeletonName==sources.meshName&&
         binding.clipName==clip&&binding.animationSkeletonName==sources.animationName&&binding.levelOfDetail==0&&
         unsigned(binding.geometryProfile)==unsigned(sources.geometryProfile)&&unsigned(binding.lodPolicy)==unsigned(sources.lodPolicy),
         "v2 descriptor conflated geometry and animation roles/named selections");
    for(unsigned role=0;role<4;++role){
        const auto bytes=ReadSource(sources.paths[role]);const auto& identity=binding.sources[role];
        Need(stats,identity.epoch==Epoch(stats,render::ParseResourcePath(sources.paths[role]).mount)&&identity.bytes==bytes.size()&&
             identity.fingerprint==wire::Fingerprint(bytes.data(),bytes.size()),"v2 descriptor role byte identity/epoch differs from original source");
        if(role==2)Need(stats,bytes==envelope.animation,"v2 HKXA payload is not the original selected packet");
        if(role==3)Need(stats,bytes==envelope.skeleton,"v2 animation SKEL payload is not the original selected packet");
    }
    wolf2009::SourceRigRequest request;request.clipName=clip;request.skeletonName=sources.animationName;
    request.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;
    const auto oracle=wolf2009::SourceRigAnimation::Load(envelope.animation,envelope.skeleton,request);
    const auto& clock=oracle.Curves().Source();
    Need(stats,binding.bones==unsigned(mesh.model->NumJoints())&&binding.bones==oracle.Skeleton().joints.size()&&binding.frames==frames&&
         binding.frames==oracle.FrameCount()&&binding.blocks==oracle.Curves().BlockCount()&&binding.maxFramesPerBlock==clock.maxFramesPerBlock&&
         binding.duration==oracle.Duration()&&binding.frameDuration==oracle.FrameDuration()&&binding.blockDuration==clock.blockDuration&&
         binding.blockInverseDuration==clock.blockInverseDuration,"v2 native wire lost variable source counts/timing");
}
void Game(Stats& stats,Resources& resources,Wolf2009AnimatorQaEntry probe,const idStr& alias,const Mesh& wrong) {
    const unsigned before=Wolf2009_OpenAnimationDescriptors();resources.mayDeleteModels=false;
    const int result=probe(alias.c_str(),wrong.alias.c_str());
    resources.mayDeleteModels=Wolf2009_OpenAnimationDescriptors()==before;
    Need(stats,resources.mayDeleteModels,"v2 game probe did not release its own defs/anims/descriptors");
    Need(stats,result==1,"v2 actual native game animator probe failed");++stats.gameProbes;
}
void ControlledGame(Stats& stats,Resources& resources,Wolf2009AnimationSetQaEntry probe,
                    const idStr& first,const idStr& second,const idStr& wrong) {
    const unsigned before=Wolf2009_OpenAnimationDescriptors();resources.mayDeleteModels=false;
    const int result=probe(first.c_str(),second.c_str(),wrong.c_str());
    resources.mayDeleteModels=Wolf2009_OpenAnimationDescriptors()==before;
    Need(stats,resources.mayDeleteModels,"controlled game probe retained defs/anims/descriptors");
    Need(stats,result==1,"actual native controlled animation probe failed");++stats.setProbes;
}
void Refusal(Stats& stats,idFile* preserved,const idStr& preservedAlias,const wire::Envelope& expected,
             const std::function<bool(idStr&)>& attempt) {
    const unsigned descriptors=Wolf2009_OpenAnimationDescriptors();const int definitions=declManager->GetNumDecls(DECL_MODELDEF);
    idStr rejected="must_clear";
    Need(stats,!attempt(rejected)&&rejected.IsEmpty(),"v2 invalid registration returned success/nonempty output");
    Need(stats,descriptors==Wolf2009_OpenAnimationDescriptors()&&definitions==declManager->GetNumDecls(DECL_MODELDEF),"v2 registration refusal changed descriptors/modelDef declarations");
    const auto after=ReadDescriptor(stats,preserved,preservedAlias);
    Need(stats,wire::Same(expected.binding,after.binding)&&expected.animation==after.animation&&expected.skeleton==after.skeleton,
         "v2 registration refusal damaged an already opened capability");++stats.refusals;
}
void Refusals(Stats& stats,Resources& resources,const Mesh& mesh,const Sources& sources,const idStr& valid,idFile* file,const wire::Envelope& original) {
    const auto attempt=[&](const char* model,const char* animation,const char* rig,const char* clip,const char* name,int policy,idStr& out){
        return Wolf2009_RegisterSourceAnimationV2(model,animation,rig,clip,name,policy,out);
    };
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(mesh.alias,sources.paths[2].c_str(),sources.paths[3].c_str(),GreeterClip,sources.animationName.c_str(),0,out);});
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(mesh.alias,sources.paths[0].c_str(),sources.paths[3].c_str(),GreeterClip,sources.animationName.c_str(),1,out);});
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(mesh.alias,sources.paths[2].c_str(),sources.paths[3].c_str(),"models/own/__missing_clip__.hkx",sources.animationName.c_str(),1,out);});
    std::string wrongCase=GreeterClip;wrongCase[0]='M';
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(mesh.alias,sources.paths[2].c_str(),sources.paths[3].c_str(),wrongCase.c_str(),sources.animationName.c_str(),1,out);});
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(mesh.alias,sources.paths[2].c_str(),sources.paths[3].c_str(),GreeterClip,"models/own/__missing_rig__.hkx",1,out);});
    const std::string headPacket=Path(render::ParseResourcePath(sources.paths[2]).mount,"skel","maps/game/trainyard/trainyard");
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(mesh.alias,sources.paths[2].c_str(),headPacket.c_str(),GreeterClip,HeadRig,1,out);});
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(mesh.alias,sources.paths[2].c_str(),headPacket.c_str(),LockerClip,LockerRig,1,out);});
    idStr legacyAlias;
    Need(stats,Wolf2009_RegisterSkinnedModel(sources.paths[0].c_str(),sources.paths[2].c_str(),sources.paths[1].c_str(),0,legacyAlias),"v2 QA legacy-control registration failed");
    auto* legacy=renderModelManager->FindModel(legacyAlias);resources.Remember(legacy);
    Need(stats,legacy&&legacy->IsLoaded()&&!legacy->IsDefaultModel()&&legacy->NumJoints()==52,"v2 QA legacy-control model is not qualified");
    Refusal(stats,file,valid,original,[&](idStr& out){return attempt(legacyAlias,sources.paths[2].c_str(),sources.paths[3].c_str(),GreeterClip,sources.animationName.c_str(),1,out);});
}
void Absent(Stats& stats,const idStr& alias) {
    const unsigned count=Wolf2009_OpenAnimationDescriptors();File file(fileSystem->OpenFileRead(alias,false));
    Need(stats,!file&&Wolf2009_OpenAnimationDescriptors()==count,"v2 stale/removed binding was still openable");
}
wire::Envelope OpenAndCheck(Stats& stats,const Mesh& mesh,const Sources& sources,const idStr& alias,const char* clip,unsigned frames) {
    const unsigned before=Wolf2009_OpenAnimationDescriptors();wire::Envelope envelope;
    {
        File file(fileSystem->OpenFileRead(alias,false));envelope=ReadDescriptor(stats,file.get(),alias);
        Need(stats,Wolf2009_OpenAnimationDescriptors()==before+1,"v2 core did not own/count its descriptor");CheckEnvelope(stats,envelope,mesh,sources,clip,frames);
    }
    Need(stats,Wolf2009_OpenAnimationDescriptors()==before,"closing a v2 idFile leaked a descriptor");return envelope;
}
void OwnedAfterRemoval(Stats& stats,Resources& resources,const idStr& alias,const std::vector<std::string>& labels) {
    const unsigned before=Wolf2009_OpenAnimationDescriptors();
    {
        File owned(fileSystem->OpenFileRead(alias,false));const auto snapshot=ReadDescriptor(stats,owned.get(),alias);
        resources.DeleteModels();Need(stats,Wolf2009_RemoveAnimationBindingV2(alias),"scoped v2 registration removal failed");Absent(stats,alias);
        const auto after=ReadDescriptor(stats,owned.get(),alias);
        Need(stats,wire::Same(snapshot.binding,after.binding)&&snapshot.animation==after.animation&&snapshot.skeleton==after.skeleton,
             "v2 open descriptor borrowed the removed registry/model state");
        for(const auto& label:labels)resources.Pinned(stats,label);
        Need(stats,Wolf2009_OpenAnimationDescriptors()==before+1,"v2 descriptor owner count changed across removal/model purge");
    }
    Need(stats,Wolf2009_OpenAnimationDescriptors()==before,"v2 descriptor close leaked its owner count");
    for(const auto& label:labels)resources.Unmount(stats,label);
}
void IndependentEpochs(Stats& stats,Resources& resources,const Sources& sources,const idStr& oldMesh,const idStr& oldClip) {
    const auto labels=Labels(sources);
    resources.Mount(stats,labels[0],TrainPackage);resources.Mount(stats,labels[1],GlobalPackage);
    resources.Mount(stats,labels[2],TrainPackage);resources.Mount(stats,labels[3],GlobalPackage);Absent(stats,oldClip);
    Mesh mesh=Load(stats,resources,sources);idStr alias=Bind(stats,resources,mesh,sources,GreeterClip);
    Need(stats,mesh.alias!=oldMesh&&alias!=oldClip,"v2 remount reused old mesh/animation aliases");
    auto previous=OpenAndCheck(stats,mesh,sources,alias,GreeterClip,182);
    // Animation sources can change epoch while geometry remains resident.
    // Geometry epoch changes require releasing the model's own source leases.
    for(unsigned role:{2u,3u,0u,1u}){
        const auto oldAlias=alias;const auto oldModel=mesh.alias;
        if(role<2)resources.DeleteModels();
        resources.Remount(stats,labels[role]);Absent(stats,oldAlias);
        if(role<2){mesh=Load(stats,resources,sources);Need(stats,mesh.alias!=oldModel,"geometry-only epoch change reused the old mesh alias");}
        else Need(stats,mesh.model->IsLoaded()&&!mesh.model->IsDefaultModel(),"animation-only remount invalidated unrelated resident geometry");
        alias=Bind(stats,resources,mesh,sources,GreeterClip);Need(stats,alias!=oldAlias,"one source-role epoch change reused the animation alias");
        const auto next=OpenAndCheck(stats,mesh,sources,alias,GreeterClip,182);
        for(unsigned i=0;i<4;++i)Need(stats,(i==role?next.binding.sources[i].epoch!=previous.binding.sources[i].epoch:next.binding.sources[i].epoch==previous.binding.sources[i].epoch)&&
             next.binding.sources[i].bytes==previous.binding.sources[i].bytes&&next.binding.sources[i].fingerprint==previous.binding.sources[i].fingerprint,
             "v2 independent source-role epoch test changed unrelated identities");
        if(role>=2)Need(stats,next.binding.modelAlias==previous.binding.modelAlias,"animation-only epoch change changed the mesh identity");
        previous=next;++stats.epochCases;
    }
    resources.DeleteModels();for(const auto& label:labels)resources.Unmount(stats,label);
}
} // namespace

bool Wolf2009_RunAnimationV2Checks(Wolf2009AnimatorQaEntry probe,Wolf2009AnimationSetQaEntry setProbe,Wolf2009DeclarationsQaEntry declProbe) {
    Stats stats;
    try {
        Need(stats,probe!=NULL&&setProbe!=NULL&&declProbe!=NULL,"v2 GPL game animator/controller/declaration QA probe is absent");
        Resources resources;const unsigned baseline=Wolf2009_OpenAnimationDescriptors();
        const std::array<std::string,4> distinct{{"qa2_model","qa2_mesh","qa2_clip","qa2_rig"}};
        resources.Mount(stats,distinct[0],TrainPackage);resources.Mount(stats,distinct[1],GlobalPackage);
        resources.Mount(stats,distinct[2],TrainPackage);resources.Mount(stats,distinct[3],GlobalPackage);
        resources.Mount(stats,"qa2_other",CharacterPackage);
        GlobalDeclarationFixture globalDeclarations;PrepareGlobalDeclarations(stats,globalDeclarations);
        const unsigned declarationsBefore=Wolf2009_OpenAnimationDescriptors();
        Need(stats,declProbe(Path(distinct[0],"decl","maps/game/trainyard/trainyard").c_str(),globalDeclarations.path.c_str())==1,
             "actual game-module source declaration loading failed");
        Need(stats,Wolf2009_OpenAnimationDescriptors()==declarationsBefore,"declaration data import changed animation descriptor ownership");
        ++stats.declarationProbes;
        const Sources sources=Body(distinct);const Mesh mesh=Load(stats,resources,sources);
        Sources otherSources=sources;otherSources.paths[0]=Path("qa2_other","modl",BodyModel);
        const Mesh wrong=Load(stats,resources,otherSources);SameHierarchy(stats,mesh,wrong);
        const std::array<const char*,3> names{{GreeterClip,IdleClip,PickupClip}};
        const std::array<unsigned,3> frames{{182,41,335}};std::array<idStr,3> aliases;
        for(unsigned i=0;i<3;++i){
            aliases[i]=Bind(stats,resources,mesh,sources,names[i]);const auto envelope=OpenAndCheck(stats,mesh,sources,aliases[i],names[i],frames[i]);
            Need(stats,envelope.binding.meshSkeletonName!=envelope.binding.animationSkeletonName,"v2 body test accidentally equated declaration and animation rigs");
            if(i==0){File preserved(fileSystem->OpenFileRead(aliases[i],false));const auto before=ReadDescriptor(stats,preserved.get(),aliases[i]);Refusals(stats,resources,mesh,sources,aliases[i],preserved.get(),before);}
            Game(stats,resources,probe,aliases[i],wrong);
        }
        Need(stats,aliases[0]!=aliases[1]&&aliases[1]!=aliases[2]&&aliases[0]!=aliases[2],"different named v2 clips collapsed to one alias");
        const idStr wrongBodyClip=Bind(stats,resources,wrong,otherSources,GreeterClip);
        ControlledGame(stats,resources,setProbe,aliases[2],aliases[1],wrongBodyClip);
        OwnedAfterRemoval(stats,resources,aliases[2],std::vector<std::string>(distinct.begin(),distinct.end()));
        resources.Unmount(stats,"qa2_other");
        IndependentEpochs(stats,resources,sources,mesh.alias,aliases[0]);

        // The same body resources now share two mounts, retaining independent
        // EmptyMale/Humanoid selections within the identical global SKEL path.
        resources.Mount(stats,"qa2_shared_map",TrainPackage);resources.Mount(stats,"qa2_shared_rig",GlobalPackage);
        const Sources shared=Body({{"qa2_shared_map","qa2_shared_rig","qa2_shared_map","qa2_shared_rig"}});
        const Mesh sharedMesh=Load(stats,resources,shared);Sources otherRig=shared;otherRig.meshName=HumanoidRig;
        const Mesh sharedWrong=Load(stats,resources,otherRig);SameHierarchy(stats,sharedMesh,sharedWrong);
        const idStr sharedAlias=Bind(stats,resources,sharedMesh,shared,IdleClip);
        const auto sharedEnvelope=OpenAndCheck(stats,sharedMesh,shared,sharedAlias,IdleClip,41);
        Need(stats,sharedEnvelope.binding.sources[0].epoch==sharedEnvelope.binding.sources[2].epoch&&
             sharedEnvelope.binding.paths[1]==sharedEnvelope.binding.paths[3]&&sharedEnvelope.binding.sources[1].epoch==sharedEnvelope.binding.sources[3].epoch&&
             sharedEnvelope.binding.meshSkeletonName!=sharedEnvelope.binding.animationSkeletonName,"v2 shared-label body did not retain independent named roles");
        Game(stats,resources,probe,sharedAlias,sharedWrong);
        OwnedAfterRemoval(stats,resources,sharedAlias,{"qa2_shared_map","qa2_shared_rig"});

        // All four locker roles live in one original MPK. Slot zero is the
        // explicit no-record compatibility slot, never an authored LOD fact.
        resources.Mount(stats,"qa2_prop",TrainPackage);resources.Mount(stats,"qa2_prop_other",TrainPackage);
        Sources prop;prop.paths={{Path("qa2_prop","modl",LockerModel),Path("qa2_prop","skel","maps/game/trainyard/trainyard"),
            Path("qa2_prop","hkxa","maps/game/trainyard/trainyard"),Path("qa2_prop","skel","maps/game/trainyard/trainyard")}};
        prop.meshName=prop.animationName=LockerRig;prop.geometryProfile=2;prop.lodPolicy=2;
        const Mesh propMesh=Load(stats,resources,prop);Sources otherProp=prop;otherProp.paths[1]=Path("qa2_prop_other","skel","maps/game/trainyard/trainyard");
        const Mesh propWrong=Load(stats,resources,otherProp);SameHierarchy(stats,propMesh,propWrong);
        const idStr propAlias=Bind(stats,resources,propMesh,prop,LockerClip);
        const auto propEnvelope=OpenAndCheck(stats,propMesh,prop,propAlias,LockerClip,24);
        Need(stats,propEnvelope.binding.bones==2&&propEnvelope.binding.lodPolicy==wire::LodPolicy::SingleResourceWithoutLodRecords&&
             propEnvelope.binding.levelOfDetail==0,"v2 locker compatibility source policy changed");
        for(unsigned i=1;i<4;++i)Need(stats,propEnvelope.binding.sources[i].epoch==propEnvelope.binding.sources[0].epoch,"v2 all-shared locker used inconsistent role epochs");
        Game(stats,resources,probe,propAlias,propWrong);
        const idStr propClose=Bind(stats,resources,propMesh,prop,LockerClose);
        OpenAndCheck(stats,propMesh,prop,propClose,LockerClose,21);
        const idStr propWrongClip=Bind(stats,resources,propWrong,otherProp,LockerClip);
        ControlledGame(stats,resources,setProbe,propAlias,propClose,propWrongClip);
        OwnedAfterRemoval(stats,resources,propAlias,{"qa2_prop"});resources.Unmount(stats,"qa2_prop_other");
        Need(stats,Wolf2009_OpenAnimationDescriptors()==baseline,"v2 complete core/game QA leaked descriptors relative to entry");
        printf("WOLF2009_ACTUAL_ENGINE_ANIMATION_V2_QA_OK checks=%zu game_probes=%zu set_probes=%zu declaration_probes=%zu descriptor_reads=%zu refusals=%zu independent_epoch_cases=%zu "
               "body_clips=182,41,335 prop_bones=2 prop_frames=24 roles_distinct=4 body_shared_labels=2 prop_shared_labels=1 "
               "payloads_original=1 scoped_remove_survived=1 source_pins_released=1 v1_model_refused=1 api8_extension=0 retail_clock=0 gpu=0\n",
               stats.checks,stats.gameProbes,stats.setProbes,stats.declarationProbes,stats.descriptors,stats.refusals,stats.epochCases);return true;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_ENGINE_ANIMATION_V2_FAIL checks=%zu probes=%zu message=%s\n",stats.checks,stats.gameProbes,error.what());}
    catch(...){fprintf(stderr,"WOLF2009_ENGINE_ANIMATION_V2_FAIL checks=%zu probes=%zu message=native-exception\n",stats.checks,stats.gameProbes);}
    return false;
}

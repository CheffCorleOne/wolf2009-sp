// SPDX-License-Identifier: GPL-3.0-or-later
// Core-owned v2 capability. Only readonly idFile operations cross API8.
#include "AnimationWireV2.h"
#include "EngineSkinnedModelData.h"
#include "SourceRigAnimation.h"
#include <algorithm>
#include <atomic>
#include <climits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "EngineAnimationBindingV2.h"
namespace {
namespace wire=wolf2009::animation2;
namespace render=wolf2009::idtech4a;
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Registration {wire::Binding binding;render::SkinnedMeshRequestV2 mesh;wire::Bytes metadata;};
std::map<std::string,Registration> registry;
std::mutex registryMutex;
std::atomic<unsigned> descriptors(0);
bool commandRegistered=false;
std::string SourcePath(const char* text,const char* type) {
    Need(text&&*text,"missing v2 source path");
    const auto parsed=render::ParseResourcePath(text);
    Need(parsed.type==type,"v2 animation source has the wrong resource type");
    const auto path=parsed.Path(parsed.type,parsed.name);
    Need(path.size()<=wire::MaximumStringBytes,"v2 source path budget");return path;
}
std::string CanonicalAlias(const char* text) {
    Need(text&&Wolf2009_IsSourceAnimationV2Alias(text),"not a v2 source animation alias");
    const auto parsed=render::ParseResourcePath(text);return parsed.Path(parsed.type,parsed.name);
}
class Leases {
    std::map<std::string,unsigned long long> held;
  public:
    ~Leases(){for(const auto& lease:held)Wolf2009_UnpinMount(lease.first.c_str(),lease.second);}
    void Pin(const wire::Binding& binding) {
        for(unsigned i=0;i<4;++i) {
            const auto label=render::ParseResourcePath(binding.paths[i]).mount;
            const auto previous=held.find(label);
            if(previous!=held.end()){Need(previous->second==binding.sources[i].epoch,"v2 shared source mount epoch mismatch");continue;}
            unsigned long long epoch=0;
            Need(binding.sources[i].epoch&&Wolf2009_PinMount(label.c_str(),epoch),"v2 animation mount could not be pinned");
            if(epoch!=binding.sources[i].epoch){Wolf2009_UnpinMount(label.c_str(),epoch);throw std::runtime_error("stale v2 animation source epoch");}
            try{held.insert(std::make_pair(label,epoch));}catch(...){Wolf2009_UnpinMount(label.c_str(),epoch);throw;}
        }
    }
    void Check() const {
        for(const auto& lease:held){unsigned long long epoch=0;
            Need(Wolf2009_MountEpoch(lease.first.c_str(),epoch)&&epoch==lease.second,"v2 animation source changed while leased");}
    }
};
wire::Bytes Read(const std::string& path) {
    struct Close{void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
    std::unique_ptr<idFile,Close> file(fileSystem->OpenFileRead(path.c_str(),false));
    Need(file.get()!=NULL,"v2 animation source absent from exact mounted package");
    const int count=file->Length();Need(count>0&&std::size_t(count)<=wire::MaximumSourceBytes,"v2 animation source byte budget");
    wire::Bytes bytes(static_cast<std::size_t>(count));
    Need(file->Read(bytes.data(),count)==count,"short v2 animation source read");return bytes;
}
wire::Bytes ReadChecked(const wire::Binding& binding,unsigned role) {
    wire::Bytes bytes=Read(binding.paths[role]);const auto& identity=binding.sources[role];
    Need(bytes.size()==identity.bytes&&wire::Fingerprint(bytes.data(),bytes.size())==identity.fingerprint,"v2 source byte identity changed");return bytes;
}
wolf2009::SourceRigRequest RigRequest(const wire::Binding& binding) {
    wolf2009::SourceRigRequest request;request.clipName=binding.clipName;request.skeletonName=binding.animationSkeletonName;
    request.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;return request;
}
void CheckModel(const Registration& registration,const wolf2009::SkeletonData* rig=NULL) {
    const auto& binding=registration.binding;const auto& mesh=registration.mesh;
    Need(binding.modelAlias==render::SkinnedMeshCacheNameV2(mesh)&&binding.meshSkeletonName==mesh.skeletonName&&
         binding.levelOfDetail==mesh.levelOfDetail&&unsigned(binding.geometryProfile)==unsigned(mesh.geometryProfile)&&
         unsigned(binding.lodPolicy)==unsigned(mesh.lodPolicy),"v2 animation differs from its complete mesh request");
    idRenderModel* model=renderModelManager->CheckModel(binding.modelAlias.c_str());
    Need(model&&model->IsLoaded()&&!model->IsDefaultModel()&&model->IsDynamicModel()==DM_CACHED&&
         model->NumJoints()==int(binding.bones)&&idStr::Icmp(model->Name(),binding.modelAlias.c_str())==0,"v2 animation model identity is not resident");
    auto* md5=dynamic_cast<idRenderModelMD5*>(model);render::SkinnedMeshSourceInfoV2 source;
    Need(md5&&Wolf2009_GetSkinnedMeshSourcesV2(md5,source)&&render::SameSkinnedMeshRequestV2(source.request,mesh),"v2 animation resident mesh source request mismatch");
    const std::array<render::SkinnedSourceIdentity,2> identities{{source.model,source.skeleton}};
    for(unsigned i=0;i<2;++i)Need(identities[i].path==binding.paths[i]&&identities[i].bytes==binding.sources[i].bytes&&
        identities[i].fingerprint==binding.sources[i].fingerprint&&mesh.sourceEpochs[i]==binding.sources[i].epoch,"v2 animation mesh byte/epoch identity mismatch");
    if(rig) {
        Need(rig->joints.size()==binding.bones&&model->GetJoints(),"v2 animation model/rig joint count mismatch");
        for(std::size_t i=0;i<rig->joints.size();++i){const auto& joint=rig->joints[i];const auto& actual=model->GetJoints()[i];
            Need(actual.name==joint.name.c_str()&&(joint.parent<0?actual.parent==NULL:actual.parent==&model->GetJoints()[joint.parent]),"v2 animation complete model/rig named hierarchy mismatch");}
    }
}
void CheckClip(const wire::Binding& binding,const wolf2009::SourceRigAnimation& clip) {
    const auto& source=clip.Curves().Source();
    Need(clip.Skeleton().name==binding.animationSkeletonName&&clip.Skeleton().joints.size()==binding.bones&&clip.FrameCount()==binding.frames&&
         clip.Duration()==binding.duration&&clip.FrameDuration()==binding.frameDuration&&clip.Curves().BlockCount()==binding.blocks&&
         source.maxFramesPerBlock==binding.maxFramesPerBlock&&source.blockDuration==binding.blockDuration&&source.blockInverseDuration==binding.blockInverseDuration,
         "v2 animation clip/profile differs from its immutable metadata");
}
class BindingFile : public idFile {
    const std::string name;
    const wire::Binding binding;
    const wire::Bytes bytes;
    const std::unique_ptr<Leases> leases;
    std::size_t position=0;
  public:
    BindingFile(std::string alias,wire::Binding request,wire::Bytes payload,std::unique_ptr<Leases> pins)
        :name(std::move(alias)),binding(std::move(request)),bytes(std::move(payload)),leases(std::move(pins)){++descriptors;}
    ~BindingFile() override {--descriptors;}
    const char* GetName() override{return name.c_str();}
    const char* GetFullPath() override{return name.c_str();}
    int Length() override{return int(bytes.size());}
    int Tell() override{return int(position);}
    ID_TIME_T Timestamp() override{return 0;}
    int Read(void* destination,int count) override {
        if(!destination||count<=0)return 0;
        const auto take=(std::min)(std::size_t(count),bytes.size()-position);
        if(take)memcpy(destination,bytes.data()+position,take);position+=take;return int(take);
    }
    int Seek(long offset,fsOrigin_t origin) override {
        const long long base=origin==FS_SEEK_SET?0:origin==FS_SEEK_CUR?static_cast<long long>(position):origin==FS_SEEK_END?static_cast<long long>(bytes.size()):-1;
        if(base<0||offset<-base||offset>static_cast<long long>(bytes.size())-base)return -1;
        position=std::size_t(base+offset);return 0;
    }
};
void RegisterCommand(const idCmdArgs& args) {
    if(args.Argc()!=7){common->Printf("usage: wolf2009SourceAnimationV2 <native mesh alias> <HKXA> <animation SKEL> <exact clip> <exact animation skeleton> <rig profile 1>\n");return;}
    idStr alias;const bool valid=args.Argv(6)[0]=='1'&&args.Argv(6)[1]=='\0';
    const bool ok=valid&&Wolf2009_RegisterSourceAnimationV2(args.Argv(1),args.Argv(2),args.Argv(3),args.Argv(4),args.Argv(5),1,alias);
    common->Printf("WOLF2009_SOURCE_ANIMATION_V2 registered=%d alias=%s\n",int(ok),alias.c_str());
}
}
bool Wolf2009_RegisterSourceAnimationV2(const char* modelAlias,const char* hkxa,const char* skeleton,
    const char* clipName,const char* skeletonName,int profile,idStr& out) {
    out.Clear();
    try {
        Need(profile==1&&modelAlias&&clipName&&skeletonName,"v2 animation requires explicit mesh/clip/rig/profile");
        Registration value;Need(Wolf2009_GetSkinnedMeshRequestV2(modelAlias,value.mesh),"v2 animation requires a registered mesh-v2 model");
        idRenderModel* model=renderModelManager->CheckModel(modelAlias);auto* md5=dynamic_cast<idRenderModelMD5*>(model);render::SkinnedMeshSourceInfoV2 source;
        Need(md5&&md5->IsLoaded()&&!md5->IsDefaultModel()&&Wolf2009_GetSkinnedMeshSourcesV2(md5,source)&&render::SameSkinnedMeshRequestV2(source.request,value.mesh),
             "v2 animation requires qualified resident mesh-v2 source geometry");
        auto& binding=value.binding;binding.modelAlias=render::SkinnedMeshCacheNameV2(value.mesh);
        binding.meshSkeletonName=value.mesh.skeletonName;binding.animationSkeletonName=skeletonName;binding.clipName=clipName;
        Need(binding.clipName.size()<=wire::MaximumStringBytes&&binding.animationSkeletonName.size()<=wire::MaximumStringBytes,"v2 exact object-name budget");
        binding.paths={{value.mesh.model,value.mesh.skeletonContainer,SourcePath(hkxa,"hkxa"),SourcePath(skeleton,"skel")}};
        binding.geometryProfile=static_cast<wire::GeometryProfile>(unsigned(value.mesh.geometryProfile));binding.levelOfDetail=std::uint32_t(value.mesh.levelOfDetail);
        binding.lodPolicy=static_cast<wire::LodPolicy>(unsigned(value.mesh.lodPolicy));
        const std::array<render::SkinnedSourceIdentity,2> identities{{source.model,source.skeleton}};
        for(unsigned i=0;i<4;++i){auto& identity=binding.sources[i];
            if(i<2){identity.epoch=value.mesh.sourceEpochs[i];identity.bytes=identities[i].bytes;identity.fingerprint=identities[i].fingerprint;}
            else{unsigned long long epoch=0;Need(Wolf2009_MountEpoch(render::ParseResourcePath(binding.paths[i]).mount.c_str(),epoch)&&epoch,"v2 animation source mount absent");identity.epoch=epoch;}}
        Leases pins;pins.Pin(binding);ReadChecked(binding,0);ReadChecked(binding,1);
        const auto animationBytes=Read(binding.paths[2]),skeletonBytes=Read(binding.paths[3]);
        for(unsigned i=2;i<4;++i){const auto& bytes=i==2?animationBytes:skeletonBytes;binding.sources[i].bytes=bytes.size();binding.sources[i].fingerprint=wire::Fingerprint(bytes.data(),bytes.size());}
        const auto clip=wolf2009::SourceRigAnimation::Load(animationBytes,skeletonBytes,RigRequest(binding));const auto& clock=clip.Curves().Source();
        binding.bones=std::uint32_t(clip.Skeleton().joints.size());binding.frames=std::uint32_t(clip.FrameCount());binding.blocks=std::uint32_t(clip.Curves().BlockCount());
        binding.maxFramesPerBlock=clock.maxFramesPerBlock;binding.duration=clip.Duration();binding.frameDuration=clip.FrameDuration();binding.blockDuration=clock.blockDuration;binding.blockInverseDuration=clock.blockInverseDuration;
        value.metadata=wire::Metadata(binding);const auto alias=wire::Alias(binding);CheckModel(value,&clip.Skeleton());CheckClip(binding,clip);pins.Check();
        std::lock_guard<std::mutex> lock(registryMutex);const auto prior=registry.find(alias);
        Need(prior==registry.end()||prior->second.metadata==value.metadata,"v2 animation alias collision with a different complete key");
        Need(prior!=registry.end()||registry.size()<4096,"v2 animation registration budget");registry.insert(std::make_pair(alias,std::move(value)));
        out=alias.c_str();return true;
    }catch(const std::exception& error){common->Warning("Wolf2009 animation v2 registration: %s",error.what());return false;}
}
idFile* Wolf2009_OpenSourceAnimationV2(const char* name) {
    try {
        const auto canonical=CanonicalAlias(name);Registration value;
        {std::lock_guard<std::mutex> lock(registryMutex);const auto found=registry.find(canonical);Need(found!=registry.end(),"unknown or stale v2 animation binding");value=found->second;}
        std::unique_ptr<Leases> pins(new Leases);pins->Pin(value.binding);ReadChecked(value.binding,0);ReadChecked(value.binding,1);
        wire::Envelope envelope;envelope.binding=value.binding;envelope.animation=ReadChecked(value.binding,2);envelope.skeleton=ReadChecked(value.binding,3);
        const auto clip=wolf2009::SourceRigAnimation::Load(envelope.animation,envelope.skeleton,RigRequest(value.binding));
        CheckClip(value.binding,clip);CheckModel(value,&clip.Skeleton());pins->Check();
        wire::Bytes bytes=wire::Serialize(envelope);Need(bytes.size()<=INT_MAX,"v2 descriptor exceeds native idFile size domain");
        return new BindingFile(canonical,value.binding,std::move(bytes),std::move(pins));
    }catch(const std::exception& error){common->Warning("Wolf2009 animation v2 descriptor: %s",error.what());return NULL;}
}
bool Wolf2009_RemoveAnimationBindingV2(const char* alias) {
    try{const auto canonical=CanonicalAlias(alias);std::lock_guard<std::mutex> lock(registryMutex);return registry.erase(canonical)!=0;}
    catch(const std::exception&){return false;}
}
void Wolf2009_InitAnimationBindingsV2(){std::lock_guard<std::mutex> lock(registryMutex);if(!commandRegistered){cmdSystem->AddCommand("wolf2009SourceAnimationV2",RegisterCommand,CMD_FL_RENDERER,"register an explicit four-source full-pose animation capability");commandRegistered=true;}}
void Wolf2009_ShutdownAnimationBindingsV2(){std::lock_guard<std::mutex> lock(registryMutex);if(commandRegistered){cmdSystem->RemoveCommand("wolf2009SourceAnimationV2");commandRegistered=false;}registry.clear();}
unsigned Wolf2009_OpenAnimationDescriptorsV2(){return descriptors.load();}

// SPDX-License-Identifier: GPL-3.0-or-later
// Game-module source provider; owns its API8 idFile capability and sampler.
#include "AnimationWire.h"
#include "AnimationWireV2.h"
#include "EngineAnimationBindingV2.h"
#include "GreeterAnimation.h"
#include "SourceRigAnimation.h"
#include "SourceActorBinding.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineAnimationGame.h"
#include "EngineAnimationSet.h"
namespace {
namespace wire = wolf2009::animation;
namespace wire2 = wolf2009::animation2;
void Need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct FileCloser { void operator()(idFile* file) const { if (file) fileSystem->CloseFile(file); } };
typedef std::unique_ptr<idFile,FileCloser> OwnedFile;
wire::Envelope ReadEnvelope(idFile* file) {
    Need(file != NULL, "source animation descriptor is absent/stale");
    const int length = file->Length();
    Need(length > 0 && std::size_t(length) <= wire::MaximumEnvelopeBytes, "source animation descriptor byte budget");
    wire::Bytes bytes(static_cast<std::size_t>(length));
    Need(file->Read(bytes.data(),length) == length, "short leased animation descriptor read");
    return wire::Parse(bytes);
}
wire2::Envelope ReadEnvelopeV2(idFile* file) {
    Need(file != NULL,"source animation v2 descriptor is absent/stale");
    const int length=file->Length();
    Need(length>0&&std::size_t(length)<=wire2::MaximumEnvelopeBytes,"source animation v2 descriptor byte budget");
    wire2::Bytes bytes(static_cast<std::size_t>(length));
    Need(file->Read(bytes.data(),length)==length,"short leased animation v2 descriptor read");
    return wire2::Parse(bytes);
}
idJointQuat Local(const wolf2009::GreeterJointPose& pose) {
    idJointQuat out;
    out.q.Set(float(-pose.rotation[0]),float(-pose.rotation[1]),float(-pose.rotation[2]),float(pose.rotation[3]));
    out.t.Set(float(pose.translation[0]),float(pose.translation[1]),float(pose.translation[2]));
    return out;
}
template<class T> class Aligned {
    T* value;
  public:
    explicit Aligned(std::size_t count) : value(static_cast<T*>(Mem_Alloc16(count*sizeof(T)))) {}
    ~Aligned() { Mem_Free16(value); }
    T* Get() const { return value; }
  private:
    Aligned(const Aligned&); Aligned& operator=(const Aligned&);
};
std::map<std::string,std::string> modelDefinitions;
const idDecl* ModelDefinitionWithoutParsing(const char* name) {
    const idDecl* found=declManager->FindDeclWithoutParsing(DECL_MODELDEF,name,false);
    if(found)return found;
    // A catalogued name can have no native self yet. Check the canonical,
    // extensionless namespace without parsing a foreign declaration's text.
    for(int i=0;i<declManager->GetNumDecls(DECL_MODELDEF);++i){
        const idDecl* candidate=declManager->DeclByIndex(DECL_MODELDEF,i,false);
        if(candidate&&idStr::Icmp(candidate->GetName(),name)==0)return candidate;
    }
    return NULL;
}
bool ModelDefinitionTextMatches(const idDecl* definition,const std::string& expected) {
    if(!definition||definition->GetTextLength()!=int(expected.size()))return false;
    std::vector<char> text(expected.size()+1,0);definition->GetText(text.data());
    return std::string(text.data(),expected.size())==expected;
}
std::string Hex(std::uint64_t value) {
    std::string text(16,'0');
    for (int i=15;i>=0;--i) { text[std::size_t(i)]="0123456789abcdef"[value&15]; value>>=4; }
    return text;
}
}
class Wolf2009SourceAnimation {
  public:
    OwnedFile lease;
    wire::Binding binding;
    wire2::Binding binding2;
    bool version2=false;
    std::unique_ptr<wolf2009::GreeterAnimation> clip;
    std::unique_ptr<wolf2009::SourceRigAnimation> rig;
    std::string modelAlias;
    std::uint32_t joints=0,frames=0;
    double duration=0,frameDuration=0;
    std::vector<std::string> jointNames;
    std::vector<idJointQuat> base;
    std::vector<int> parents;
    Wolf2009SourceAnimation(OwnedFile file, wire::Envelope envelope)
        : lease(std::move(file)), binding(std::move(envelope.binding)),
          clip(new wolf2009::GreeterAnimation(wolf2009::GreeterAnimation::Load(envelope.animation,envelope.skeleton))) {
        Need(clip->Joints().size()==binding.joints && clip->FrameCount()==binding.frames && clip->Duration()==binding.duration &&
             clip->FrameDuration()==binding.frameDuration && clip->ResourceName()==binding.clipName && clip->SkeletonName()==binding.skeletonName,
             "source sampler differs from its leased binding profile");
        modelAlias=binding.modelAlias;joints=binding.joints;frames=binding.frames;duration=binding.duration;frameDuration=binding.frameDuration;
        const auto pose=clip->SamplePose(0);
        for (std::size_t i=0;i<pose.local.size();++i) { base.push_back(Local(pose.local[i])); parents.push_back(clip->Joints()[i].parent);jointNames.push_back(clip->Joints()[i].name); }
        idRenderModel* model=renderModelManager->CheckModel(modelAlias.c_str());
        Need(Matches(model), "source animation descriptor does not match its registered model alias/hierarchy");
    }
    Wolf2009SourceAnimation(OwnedFile file,wire2::Envelope envelope)
        :lease(std::move(file)),binding2(std::move(envelope.binding)),version2(true) {
        wolf2009::SourceRigRequest request;request.clipName=binding2.clipName;request.skeletonName=binding2.animationSkeletonName;
        request.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;
        rig.reset(new wolf2009::SourceRigAnimation(wolf2009::SourceRigAnimation::Load(envelope.animation,envelope.skeleton,request)));
        const auto& source=rig->Curves().Source();const auto& identity=rig->Identity();
        // Parse already checked every declared role, geometry/LOD policy, alias
        // namespace and payload identity. MODL/mesh-SKEL qualification belongs
        // to the core capability producer; no private core types cross API8.
        Need(identity.request.clipName==binding2.clipName&&identity.request.skeletonName==binding2.animationSkeletonName&&
             rig->Skeleton().name==binding2.animationSkeletonName&&rig->Skeleton().joints.size()==binding2.bones&&
             rig->FrameCount()==binding2.frames&&rig->Curves().BlockCount()==binding2.blocks&&source.maxFramesPerBlock==binding2.maxFramesPerBlock&&
             rig->Duration()==binding2.duration&&rig->FrameDuration()==binding2.frameDuration&&source.blockDuration==binding2.blockDuration&&
             source.blockInverseDuration==binding2.blockInverseDuration&&identity.animationBytes==binding2.sources[2].bytes&&
             identity.animationFingerprint==binding2.sources[2].fingerprint&&identity.skeletonBytes==binding2.sources[3].bytes&&
             identity.skeletonFingerprint==binding2.sources[3].fingerprint,"source v2 sampler differs from its complete leased profile");
        modelAlias=binding2.modelAlias;joints=binding2.bones;frames=binding2.frames;duration=binding2.duration;frameDuration=binding2.frameDuration;
        const auto pose=rig->SamplePose(0);
        for(std::size_t i=0;i<pose.local.size();++i){base.push_back(Local(pose.local[i]));parents.push_back(rig->Skeleton().joints[i].parent);jointNames.push_back(rig->Skeleton().joints[i].name);}
        Need(Matches(renderModelManager->CheckModel(modelAlias.c_str())),"source v2 descriptor does not match its qualified native model alias/hierarchy");
    }
    bool Same(const Wolf2009SourceAnimation& other) const {
        return version2==other.version2&&(version2?wire2::Same(binding2,other.binding2):wire::Same(binding,other.binding));
    }
    bool Matches(const idRenderModel* model) const {
        if (!model || !model->IsLoaded() || model->IsDefaultModel() || model->IsDynamicModel()!=DM_CACHED ||
            idStr::Icmp(model->Name(),modelAlias.c_str())!=0 || model->NumJoints()!=int(joints)) return false;
        const idMD5Joint* native=model->GetJoints(); if (!native) return false;
        for (std::size_t i=0;i<jointNames.size();++i) {
            if (native[i].name!=jointNames[i].c_str() || (parents[i]<0 ? native[i].parent!=NULL : native[i].parent!=&native[parents[i]])) return false;
        }
        return true;
    }
    bool Arguments(idJointQuat* out,const int* indices,int count) const {
        if(!out||!indices||count<=0||count>int(joints))return false;
        for(int i=0;i<count;++i)if(indices[i]<0||indices[i]>=int(joints))return false;return true;
    }
    bool Store(const wolf2009::GreeterPose& pose,idJointQuat* out,const int* indices,int count) const {
        Need(pose.local.size()==joints,"source sampler returned an incomplete local palette");
        for (std::size_t i=0;i<base.size();++i) out[i]=base[i];
        for (int i=0;i<count;++i) out[indices[i]]=Local(pose.local[std::size_t(indices[i])]);
        return true;
    }
    bool Sample(double seconds,idJointQuat* out,const int* indices,int count) const {
        if(!Arguments(out,indices,count)||!std::isfinite(seconds))return false;
        return Store(version2?rig->SamplePose(seconds):clip->SamplePose(seconds),out,indices,count);
    }
    bool Frame(double frame,idJointQuat* out,const int* indices,int count) const {
        if(!Arguments(out,indices,count)||!std::isfinite(frame)||frame<0||frame>double(frames-1))return false;
        if(version2)return Store(rig->SamplePoseFrame(frame),out,indices,count);
        const double seconds=frame==double(frames-1)?binding.duration:frame*binding.frameDuration;
        return Sample(seconds,out,indices,count);
    }
    double FrameCoordinate(int milliseconds) const {
        if(version2)return rig->FrameCoordinateAtTime(double(milliseconds)*0.001);
        // Keep the v1 ConvertTimeToFrame source formula exactly.
        double coordinate=milliseconds<=0?0.0:(double(milliseconds)*0.001)/frameDuration;
        if(double(milliseconds)*0.001>=duration)coordinate=frames-1;
        if(coordinate>frames-1)coordinate=frames-1;return coordinate;
    }
    bool BoundsAtSeconds(double seconds,idBounds& bounds) const {
        const int count=int(joints); Aligned<idJointQuat> local(count); Aligned<idJointMat> global(count);
        std::vector<int> all; for(int i=0;i<count;++i) all.push_back(i);
        if (!Sample(seconds,local.Get(),all.data(),count)) return false;
        SIMDProcessor->ConvertJointQuatsToJointMats(global.Get(),local.Get(),count);
        if(count>1)SIMDProcessor->TransformJoints(global.Get(),parents.data(),1,count-1);
        idRenderModel* model=renderModelManager->CheckModel(modelAlias.c_str()); if (!Matches(model)) return false;
        renderEntity_t entity; memset(&entity,0,sizeof(entity)); entity.hModel=model; entity.numJoints=count; entity.joints=global.Get(); entity.axis.Identity();
        bounds=model->Bounds(&entity); return !bounds.IsCleared();
    }
    bool Bounds(int milliseconds,int cycle,idBounds& bounds) const {
        return cycle==1&&BoundsAtSeconds(double(milliseconds)*0.001,bounds);
    }
};
namespace {
std::unique_ptr<Wolf2009SourceAnimation> Open(const char* alias) {
    Need(alias && Wolf2009_IsSourceAnimationAlias(alias), "not a source animation binding alias");
    OwnedFile file(fileSystem->OpenFileRead(alias,false));
    if(Wolf2009_IsSourceAnimationV2Alias(alias)){
        wire2::Envelope envelope=ReadEnvelopeV2(file.get());
        Need(wire2::Alias(envelope.binding)==alias,"animation v2 alias differs from its complete leased metadata");
        return std::unique_ptr<Wolf2009SourceAnimation>(new Wolf2009SourceAnimation(std::move(file),std::move(envelope)));
    }
    wire::Envelope envelope=ReadEnvelope(file.get());
    Need(wire::Alias(envelope.binding)==alias, "animation alias differs from its full leased metadata");
    return std::unique_ptr<Wolf2009SourceAnimation>(new Wolf2009SourceAnimation(std::move(file),std::move(envelope)));
}
}
void Wolf2009_DeleteSourceAnimation(Wolf2009SourceAnimation* source) { delete source; }
bool idMD5Anim::LoadSourceAnimation(const char* filename) {
    try {
        auto loaded=Open(filename);
        Free(); sourceAnimation=loaded.release();
        name=filename; numFrames=int(sourceAnimation->frames); numJoints=int(sourceAnimation->joints);
        frameRate=24; animLength=int(std::ceil(sourceAnimation->duration*1000)); numAnimatedComponents=0; totaldelta.Zero();
        jointInfo.SetNum(numJoints); baseFrame.SetNum(numJoints);
        for (int i=0;i<numJoints;++i) {
            jointInfo[i].nameIndex=animationLib.JointIndex(sourceAnimation->jointNames[std::size_t(i)].c_str()); jointInfo[i].parentNum=sourceAnimation->parents[std::size_t(i)];
            jointInfo[i].animBits=0; jointInfo[i].firstComponent=0; baseFrame[i]=sourceAnimation->base[std::size_t(i)];
        }
        return true;
    } catch (const std::exception& error) { gameLocal.Warning("Wolf2009 source animation '%s': %s",filename?filename:"",error.what()); return false; }
}
bool idMD5Anim::ReloadSourceAnimation() {
    try {
        Need(sourceAnimation!=NULL,"missing source animation reload state");
        auto replacement=Open(name.c_str());
        Need(replacement->Same(*sourceAnimation),"source animation reload changed its complete binding");
        Wolf2009SourceAnimation* old=sourceAnimation; sourceAnimation=replacement.release(); delete old; return true;
    } catch (const std::exception& error) { gameLocal.Warning("Wolf2009 source animation reload refused: %s",error.what()); return false; }
}
bool idMD5Anim::SourcePoseAtTime(int milliseconds,int cycles,idJointQuat* out,const int* index,int count) const {
    if (!sourceAnimation) return false;
    if (!sourceAnimation->version2) return cycles==1&&sourceAnimation->Sample(double(milliseconds)*0.001,out,index,count);
    if (cycles==0||cycles<-1) return false;
    try {
        // The complete checked pose is evaluated before Store writes any joints.
        const auto clock=wolf2009::actor::CycleAtMilliseconds(milliseconds,cycles,sourceAnimation->duration);
        return sourceAnimation->Sample(clock.seconds,out,index,count);
    } catch (const std::exception& error) {
        gameLocal.Error("Wolf2009 source animation v2 timed pose '%s': %s",name.c_str(),error.what());
        return false; // Native Error does not return; never silently skip bad data.
    }
}
bool idMD5Anim::SourceFramePose(double frame,idJointQuat* out,const int* index,int count) const {
    if (!sourceAnimation || !std::isfinite(frame) || frame<0 || frame>double(numFrames-1)) return false;
    if (!sourceAnimation->version2) return sourceAnimation->Frame(frame,out,index,count);
    try {
        return sourceAnimation->Frame(frame,out,index,count);
    } catch (const std::exception& error) {
        gameLocal.Error("Wolf2009 source animation v2 frame pose '%s': %s",name.c_str(),error.what());
        return false;
    }
}
bool idMD5Anim::IsSourceAnimationV2() const { return sourceAnimation && sourceAnimation->version2; }
bool idMD5Anim::SourceBounds(int time,int cycles,idBounds& bounds) const {
    if (!sourceAnimation) return false;
    if (!sourceAnimation->version2) return sourceAnimation->Bounds(time,cycles,bounds);
    if (cycles==0||cycles<-1) return false;
    try {
        // Bounds assigns its output only after the checked pose and model bounds.
        const auto clock=wolf2009::actor::CycleAtMilliseconds(time,cycles,sourceAnimation->duration);
        return sourceAnimation->BoundsAtSeconds(clock.seconds,bounds);
    } catch (const std::exception& error) {
        gameLocal.Error("Wolf2009 source animation v2 bounds '%s': %s",name.c_str(),error.what());
        return false;
    }
}
bool idMD5Anim::SourceMatchesModel(const idRenderModel* model) const { return !sourceAnimation || sourceAnimation->Matches(model); }
const char* idMD5Anim::SourceModelAlias() const { return sourceAnimation ? sourceAnimation->modelAlias.c_str() : ""; }
double idMD5Anim::SourceDuration() const { return sourceAnimation ? sourceAnimation->duration : 0; }
double idMD5Anim::SourceFrameDuration() const { return sourceAnimation ? sourceAnimation->frameDuration : 0; }
double idMD5Anim::SourceFrameCoordinate(int milliseconds) const {
    if (!sourceAnimation) return 0;
    if (!sourceAnimation->version2) return sourceAnimation->FrameCoordinate(milliseconds);
    try {
        return sourceAnimation->FrameCoordinate(milliseconds);
    } catch (const std::exception& error) {
        gameLocal.Error("Wolf2009 source animation v2 frame coordinate '%s': %s",name.c_str(),error.what());
        return 0;
    }
}
double idMD5Anim::SourceCycleFrameCoordinate(int milliseconds,int cycles,int& completedCycles) const {
    completedCycles=0;
    if(!sourceAnimation)return 0;
    if(!sourceAnimation->version2){
        if(cycles!=1){gameLocal.Error("Unsupported source-v1 cycle policy");return 0;}
        return sourceAnimation->FrameCoordinate(milliseconds);
    }
    try{
        const auto clock=wolf2009::actor::CycleAtMilliseconds(milliseconds,cycles,sourceAnimation->duration);
        const double coordinate=sourceAnimation->rig->FrameCoordinateAtTime(clock.seconds);
        completedCycles=clock.completedCycles;return coordinate;
    }catch(const std::exception& error){
        gameLocal.Error("Wolf2009 source animation v2 cycle coordinate '%s': %s",name.c_str(),error.what());return 0;
    }
}
bool Wolf2009_CreateSourceModelDef(const char* animationAlias,const char* appearanceAlias,idStr& out) {
    out.Clear();
    try {
        idMD5Anim* animation=animationLib.GetAnim(animationAlias);
        Need(animation && animation->IsSourceAnimation(),"source modelDef requires a loaded source clip");
        const char* modelAlias=animation->SourceModelAlias(); idRenderModel* model=renderModelManager->CheckModel(modelAlias);
        Need(animation->SourceMatchesModel(model),"source modelDef model identity/hierarchy mismatch");
        std::ostringstream text; text<<"{\n mesh \""<<modelAlias<<"\"\n";
        if (appearanceAlias && *appearanceAlias) {
            const std::string safe(appearanceAlias);
            Need(safe.size()<=4096,"source modelDef skin name budget");
            for(unsigned char c:safe) Need(c>=33 && c<127 && c!='"' && c!='{' && c!='}' && c!='\\',"unsafe source modelDef skin name");
            const idDeclSkin* skin=declManager->FindSkin(appearanceAlias,false);
            Need(skin && skin->GetState()!=DS_DEFAULTED,"source modelDef appearance is not a qualified native skin");
            text<<" skin \""<<appearanceAlias<<"\"\n";
        }
        text<<" anim source \""<<animationAlias<<"\"\n}\n";
        const std::string declaration=text.str();
        const std::string name="wolf2009/animation-model/"+Hex(wire::Fingerprint(reinterpret_cast<const unsigned char*>(declaration.data()),declaration.size()));
        const auto prior=modelDefinitions.find(name);
        Need(prior==modelDefinitions.end() || prior->second==declaration,"source modelDef alias collision with different full text");
        Need(prior!=modelDefinitions.end() || modelDefinitions.size()<4096,"source modelDef registration budget");
        if (prior==modelDefinitions.end()) {
            Need(!ModelDefinitionWithoutParsing(name.c_str()),"source modelDef namespace is already occupied");
            idDecl* def=declManager->CreateNewDecl(DECL_MODELDEF,name.c_str(),"<wolf2009-source-animation>");
            def->SetText(declaration.c_str()); def->Invalidate(); modelDefinitions[name]=declaration;
        }
        Need(ModelDefinitionTextMatches(ModelDefinitionWithoutParsing(name.c_str()),declaration),
             "source modelDef declaration changed before parsing");
        const auto* def=static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name.c_str(),false));
        Need(def && def->GetState()!=DS_DEFAULTED && def->ModelHandle()==model && def->UsesSourceAnimation(),"native source modelDef failed to parse");
        out=name.c_str(); return true;
    } catch (const std::exception& error) { gameLocal.Warning("Wolf2009 source modelDef: %s",error.what()); return false; }
}
void Wolf2009_ClearAnimationGameRegistry() { modelDefinitions.clear(); Wolf2009_ClearAnimationSetRegistry(); }
const idDeclModelDef* Wolf2009_FindRegisteredSourceModelDef(const char* name) {
    if (!name) return NULL;
    const auto found=modelDefinitions.find(name);
    if (found==modelDefinitions.end()) return NULL;
    // FindType reparses UNPARSED text and replaces its native animation data.
    // A live animator may share this declaration, so reject changed text first.
    if (!ModelDefinitionTextMatches(ModelDefinitionWithoutParsing(name),found->second)) return NULL;
    const auto* def=static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name,false));
    if (!def || def->GetState()==DS_DEFAULTED || !def->UsesSourceAnimation() ||
        !ModelDefinitionTextMatches(def,found->second)) return NULL;
    return def;
}

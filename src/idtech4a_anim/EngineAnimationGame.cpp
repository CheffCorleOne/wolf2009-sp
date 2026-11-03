// SPDX-License-Identifier: GPL-3.0-or-later
// Game-module source provider; owns its API8 idFile capability and sampler.
#include "AnimationWire.h"
#include "GreeterAnimation.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineAnimationGame.h"
namespace {
namespace wire = wolf2009::animation;
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
    wolf2009::GreeterAnimation clip;
    std::vector<idJointQuat> base;
    std::vector<int> parents;
    Wolf2009SourceAnimation(OwnedFile file, wire::Envelope envelope)
        : lease(std::move(file)), binding(std::move(envelope.binding)), clip(wolf2009::GreeterAnimation::Load(envelope.animation,envelope.skeleton)) {
        Need(clip.Joints().size()==binding.joints && clip.FrameCount()==binding.frames && clip.Duration()==binding.duration &&
             clip.FrameDuration()==binding.frameDuration && clip.ResourceName()==binding.clipName && clip.SkeletonName()==binding.skeletonName,
             "source sampler differs from its leased binding profile");
        const auto pose=clip.SamplePose(0);
        for (std::size_t i=0;i<pose.local.size();++i) { base.push_back(Local(pose.local[i])); parents.push_back(clip.Joints()[i].parent); }
        idRenderModel* model=renderModelManager->CheckModel(binding.modelAlias.c_str());
        Need(Matches(model), "source animation descriptor does not match its registered model alias/hierarchy");
    }
    bool Matches(const idRenderModel* model) const {
        if (!model || !model->IsLoaded() || model->IsDefaultModel() || model->IsDynamicModel()!=DM_CACHED ||
            idStr::Icmp(model->Name(),binding.modelAlias.c_str())!=0 || model->NumJoints()!=int(binding.joints)) return false;
        const idMD5Joint* joints=model->GetJoints(); if (!joints) return false;
        for (std::size_t i=0;i<clip.Joints().size();++i) {
            const auto& joint=clip.Joints()[i];
            if (joints[i].name!=joint.name.c_str() || (joint.parent<0 ? joints[i].parent!=NULL : joints[i].parent!=&joints[joint.parent])) return false;
        }
        return true;
    }
    bool Sample(double seconds,idJointQuat* out,const int* indices,int count) const {
        if (!out || !indices || count<=0 || count>int(binding.joints) || !std::isfinite(seconds)) return false;
        for (int i=0;i<count;++i) if (indices[i]<0 || indices[i]>=int(binding.joints)) return false;
        const auto pose=clip.SamplePose(seconds);
        for (std::size_t i=0;i<base.size();++i) out[i]=base[i];
        for (int i=0;i<count;++i) out[indices[i]]=Local(pose.local[std::size_t(indices[i])]);
        return true;
    }
    bool Bounds(int milliseconds,int cycle,idBounds& bounds) const {
        if (cycle!=1) return false;
        const int count=int(binding.joints); Aligned<idJointQuat> local(count); Aligned<idJointMat> global(count);
        std::vector<int> all; for(int i=0;i<count;++i) all.push_back(i);
        if (!Sample(double(milliseconds)*0.001,local.Get(),all.data(),count)) return false;
        SIMDProcessor->ConvertJointQuatsToJointMats(global.Get(),local.Get(),count);
        SIMDProcessor->TransformJoints(global.Get(),parents.data(),1,count-1);
        idRenderModel* model=renderModelManager->CheckModel(binding.modelAlias.c_str()); if (!Matches(model)) return false;
        renderEntity_t entity; memset(&entity,0,sizeof(entity)); entity.hModel=model; entity.numJoints=count; entity.joints=global.Get(); entity.axis.Identity();
        bounds=model->Bounds(&entity); return !bounds.IsCleared();
    }
};
namespace {
std::unique_ptr<Wolf2009SourceAnimation> Open(const char* alias) {
    Need(alias && Wolf2009_IsSourceAnimationAlias(alias), "not a source animation binding alias");
    OwnedFile file(fileSystem->OpenFileRead(alias,false));
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
        name=filename; numFrames=int(sourceAnimation->binding.frames); numJoints=int(sourceAnimation->binding.joints);
        frameRate=24; animLength=int(std::ceil(sourceAnimation->binding.duration*1000)); numAnimatedComponents=0; totaldelta.Zero();
        jointInfo.SetNum(numJoints); baseFrame.SetNum(numJoints);
        for (int i=0;i<numJoints;++i) {
            const auto& joint=sourceAnimation->clip.Joints()[std::size_t(i)];
            jointInfo[i].nameIndex=animationLib.JointIndex(joint.name.c_str()); jointInfo[i].parentNum=joint.parent;
            jointInfo[i].animBits=0; jointInfo[i].firstComponent=0; baseFrame[i]=sourceAnimation->base[std::size_t(i)];
        }
        return true;
    } catch (const std::exception& error) { gameLocal.Warning("Wolf2009 source animation '%s': %s",filename?filename:"",error.what()); return false; }
}
bool idMD5Anim::ReloadSourceAnimation() {
    try {
        Need(sourceAnimation!=NULL,"missing source animation reload state");
        auto replacement=Open(name.c_str());
        Need(wire::Same(replacement->binding,sourceAnimation->binding),"source animation reload changed its complete binding");
        Wolf2009SourceAnimation* old=sourceAnimation; sourceAnimation=replacement.release(); delete old; return true;
    } catch (const std::exception& error) { gameLocal.Warning("Wolf2009 source animation reload refused: %s",error.what()); return false; }
}
bool idMD5Anim::SourcePoseAtTime(int milliseconds,int cycles,idJointQuat* out,const int* index,int count) const {
    if (!sourceAnimation || cycles!=1) return false;
    return sourceAnimation->Sample(double(milliseconds)*0.001,out,index,count);
}
bool idMD5Anim::SourceFramePose(double frame,idJointQuat* out,const int* index,int count) const {
    if (!sourceAnimation || !std::isfinite(frame) || frame<0 || frame>double(numFrames-1)) return false;
    const double seconds=frame==double(numFrames-1) ? sourceAnimation->binding.duration : frame*sourceAnimation->binding.frameDuration;
    return sourceAnimation->Sample(seconds,out,index,count);
}
bool idMD5Anim::SourceBounds(int time,int cycles,idBounds& bounds) const {
    return sourceAnimation && sourceAnimation->Bounds(time,cycles,bounds);
}
bool idMD5Anim::SourceMatchesModel(const idRenderModel* model) const { return !sourceAnimation || sourceAnimation->Matches(model); }
const char* idMD5Anim::SourceModelAlias() const { return sourceAnimation ? sourceAnimation->binding.modelAlias.c_str() : ""; }
double idMD5Anim::SourceDuration() const { return sourceAnimation ? sourceAnimation->binding.duration : 0; }
double idMD5Anim::SourceFrameDuration() const { return sourceAnimation ? sourceAnimation->binding.frameDuration : 0; }
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
            Need(!declManager->FindType(DECL_MODELDEF,name.c_str(),false),"source modelDef namespace is already occupied");
            idDecl* def=declManager->CreateNewDecl(DECL_MODELDEF,name.c_str(),"<wolf2009-source-animation>");
            def->SetText(declaration.c_str()); def->Invalidate(); modelDefinitions[name]=declaration;
        }
        const auto* def=static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name.c_str(),false));
        Need(def && def->GetState()!=DS_DEFAULTED && def->ModelHandle()==model && def->UsesSourceAnimation(),"native source modelDef failed to parse");
        out=name.c_str(); return true;
    } catch (const std::exception& error) { gameLocal.Warning("Wolf2009 source modelDef: %s",error.what()); return false; }
}
void Wolf2009_ClearAnimationGameRegistry() { modelDefinitions.clear(); }

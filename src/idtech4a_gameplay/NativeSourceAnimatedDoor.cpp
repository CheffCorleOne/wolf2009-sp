// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "NativeSourceAnimatedDoor.h"
#include "NativeSourceMover.h"
#include "EngineAnimationSet.h"
#include "SourceControlledEntity.h"
#include "SourceDeclarationSetRuntime.h"

namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void Require(bool value,const idStr& reason,const char* fallback){if(!value)throw std::runtime_error(reason.IsEmpty()?fallback:reason.c_str());}
bool Active(){return gameLocal.GameState()==GAMESTATE_ACTIVE&&!gameLocal.isClient&&!gameLocal.isMultiplayer&&gameLocal.time>=0;}
void Close(idFile* file){if(file)fileSystem->CloseFile(file);}
typedef std::unique_ptr<idFile,void(*)(idFile*)> SourceFile;
class ActorStartLease {
    WolfSourceActorStartBinding hooks;
  public:
    explicit ActorStartLease(const WolfSourceActorStartBinding& value):hooks(value){
        Need(hooks.context&&hooks.retain&&hooks.release&&hooks.preflight&&hooks.query,"Source animated door requires a complete retained actor-start authority");
        hooks.retain(hooks.context);
    }
    ~ActorStartLease(){hooks.release(hooks.context);}
    WolfSourceActorStartEvidence Read()const{
        idStr reason;Require(hooks.preflight(hooks.context,reason),reason,"Source animated door actor-start owner preflight failed");
        WolfSourceActorStartEvidence out;Require(hooks.query(hooks.context,out,reason),reason,"Source animated door actor-start owner query failed");return out;
    }
    ActorStartLease(const ActorStartLease&)=delete;ActorStartLease& operator=(const ActorStartLease&)=delete;
};
void Authority(const WolfSourceActorStartEvidence& proof,const WolfSourceAnimatedDoorRequest& request){
    const auto& signal=request.signal;const auto& command=proof.command;
    Need(proof.levelGeneration==request.levelGeneration&&proof.sourceName==signal.ownerEntityName&&!proof.sourceName.empty()&&proof.sourceName.size()<=127,
        "Source animated door actor-start authority source/generation differs");
    if(!proof.committed){Need(command.kind==wolf2009::TrainDoorCommand::None&&!command.levelGeneration&&!command.eventId&&!command.sourceNodeId&&
        command.ownerEntityName.empty()&&command.resourceName.empty()&&command.targetEntityName.empty()&&!proof.commandSerial&&proof.committedTimeMs==-1&&!proof.actualPoseEvaluated,
        "Source animated door actor-start authority published a command without an actual commit");return;}
    Need(proof.nativeActor&&proof.nativeSpawnId&&proof.commandSerial&&proof.committedTimeMs>=0&&proof.actualPoseEvaluated&&
        command.kind==wolf2009::TrainDoorCommand::StartActorAnimation&&command.levelGeneration==request.levelGeneration&&command.eventId&&command.sourceNodeId==signal.actorStartNodeId&&
        command.ownerEntityName==signal.ownerEntityName&&command.resourceName==signal.actorAnimation&&command.targetEntityName.empty(),
        "Source animated door actor-start authority did not issue the qualified actual Start command");
}
void NativeActor(const WolfSourceActorStartEvidence& proof){
    auto* actor=proof.nativeActor;Need(actor&&proof.nativeSpawnId&&Wolf2009_IsSourceControlledEntity(actor)&&proof.sourceName==actor->GetName()&&
        actor->entityNumber>=MAX_CLIENTS&&actor->entityNumber<ENTITYNUM_MAX_NORMAL&&gameLocal.entities[actor->entityNumber]==actor&&
        static_cast<unsigned>(gameLocal.GetSpawnId(actor))==proof.nativeSpawnId&&gameLocal.FindEntity(proof.sourceName.c_str())==actor,
        "Source animated door actor-start owner original native spawn is absent/changed");
}
std::string Fold(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c+='a'-'A';return text;}
std::uint64_t Hash(const std::vector<unsigned char>& bytes){std::uint64_t h=14695981039346656037ull;for(const auto c:bytes){h^=c;h*=1099511628211ull;}return h;}
void RequestBounds(const WolfSourceAnimatedDoorRequest& request){
    Need(request.resources.clips.size()==3&&request.signal.referencedSpeechLines.size()<=4096&&request.signal.unsupportedOutputs.size()<=4096,
        "Source animated door request vector budget");std::size_t stored=0;
    const auto text=[&](const std::string& value){Need(value.size()<=4096&&value.size()<=(1u<<20)-stored,"Source animated door request text budget");stored+=value.size();};
    const auto& signal=request.signal;const auto& resources=request.resources;
    for(const auto* value:{&request.exactEntitiesPath,&request.context.mapModelDefinition,&resources.modelResource,&resources.meshSkeletonResource,&resources.meshSkeletonName,
        &signal.brainResource,&signal.signalEntityName,&signal.ownerEntityName,&signal.ownerClassname,&signal.useKey,&signal.voiceDefinition,&signal.actorAnimation,&signal.targetEntityName,&signal.targetClassname})text(*value);
    for(const auto& line:signal.referencedSpeechLines)text(line);
    for(const auto& clip:resources.clips)for(const auto* value:{&clip.logicalName,&clip.animationResource,&clip.animationSkeletonResource,&clip.clipName,&clip.animationSkeletonName})text(*value);
}
std::string Mount(const std::string& path,const char* type){
    Need(!path.empty()&&path.size()<=4096,"Source animated door path length");std::string p=Fold(path);for(char& c:p)if(c=='\\')c='/';
    for(const unsigned char c:p)Need(c>=33&&c<127&&c!=':'&&c!='"'&&c!='{'&&c!='}',"Source animated door unsafe resource path");
    Need(p.compare(0,9,"wolf2009/")==0,"Source animated door requires a typed source path");
    const auto a=p.find('/',9),b=a==std::string::npos?a:p.find('/',a+1);
    Need(a!=std::string::npos&&a>9&&b!=std::string::npos&&b>a+1&&b+1<p.size()&&p.substr(a+1,b-a-1)==type,"Source animated door resource type/mount");
    for(std::size_t at=0;at<p.size();){const auto end=p.find('/',at);const auto part=p.substr(at,(end==std::string::npos?p.size():end)-at);
        Need(!part.empty()&&part!="."&&part!="..","Source animated door unsafe resource segment");if(end==std::string::npos)break;at=end+1;}
    return p.substr(9,a-9);
}
std::vector<unsigned char> Read(idFile* file){
    Need(file&&file->Tell()==0&&file->Length()>0&&file->Length()<=64*1024*1024,"Source animated door readonly ENTS length/position");
    const int length=file->Length();std::vector<unsigned char> bytes(static_cast<std::size_t>(length));std::size_t at=0;
    while(at<bytes.size()){const int n=file->Read(bytes.data()+at,int(bytes.size()-at));Need(n>0&&std::size_t(n)<=bytes.size()-at,"Source animated door short readonly ENTS read");at+=std::size_t(n);}
    unsigned char extra=0;Need(file->Read(&extra,1)==0&&file->Tell()==length&&file->Length()==length,"Source animated door ENTS EOF/length changed");return bytes;
}
wolf2009::SourceEntityValue Value(const WolfSourceAnimatedDoorFacts& facts,const char* key){
    wolf2009::SourceEntityValue out;out.instance=facts.instance.Find(key);out.declaration=facts.defaults.Find(key);return out;
}
std::string Text(const WolfSourceAnimatedDoorFacts& facts,const char* key){std::string result;
    Need(Value(facts,key).Text(result)&&!result.empty()&&result.size()<=4096,"Source animated door missing/unqualified text field");return result;}
int Integer(const WolfSourceAnimatedDoorFacts& facts,const char* key){std::int32_t result=0;
    Need(Value(facts,key).Integer(result),"Source animated door missing/unqualified integer field");return result;}
std::string InstanceText(const wolf2009::EntityRecord& entity,const char* key){const auto* value=entity.Find(key);
    Need(value&&value->kind==wolf2009::EntityProperty::Text&&!value->text.empty()&&value->text.size()<=4096,"Source animated door ENTS literal text required");return value->text;}
void Rigid(const idVec3& origin,const idMat3& axis){
    for(int i=0;i<3;++i){Need(std::isfinite(origin[i])&&std::abs(origin[i])<=10000000,"Source animated door origin domain");
        for(int j=0;j<3;++j)Need(std::isfinite(axis[i][j]),"Source animated door nonfinite axis");}
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)Need(std::abs(axis[i]*axis[j]-(i==j?1.f:0.f))<=.0001f,"Source animated door requires authored rigid axes");
    Need(std::abs(axis.Determinant()-1.f)<=.0001f,"Source animated door reflected/nonrigid axes");
}
void Placement(const wolf2009::EntityRecord& record,idVec3& origin,idMat3& axis){
    const auto placement=wolf2009::ReadEntityPlacement(record);Need(placement.hasOrigin&&placement.hasRotation&&!placement.hasAngle,"Source animated door requires explicit matrix world placement");
    for(int i=0;i<3;++i)origin[i]=placement.origin[i];for(int i=0;i<9;++i)axis[i/3][i%3]=placement.rotation[i];Rigid(origin,axis);
}
bool SamePose(const idVec3& a,const idMat3& r,const idVec3& b,const idMat3& s){
    for(int i=0;i<3;++i)if(std::abs(a[i]-b[i])>.0002f)return false;
    for(int i=0;i<9;++i)if(std::abs(r[i/3][i%3]-s[i/3][i%3])>.00002f)return false;return true;
}
const idAnim* NativeClip(const WolfSourceActorBinding& binding,const char* name){
    const auto* definition=Wolf2009_FindRegisteredSourceAnimationSet(binding.NativeModelDefinition());Need(definition!=NULL,"Source animated door registered set is unavailable");
    const auto* clip=binding.FindClip(name);const int number=definition->GetSpecificAnim(name);const auto* anim=number>0?definition->GetAnim(number):NULL;
    Need(clip&&anim&&anim->NumAnims()==1&&!anim->HasFrameCommands()&&anim->MD5Anim(0)&&anim->MD5Anim(0)->IsSourceAnimationV2()&&
        clip->animationAlias==anim->MD5Anim(0)->Name()&&clip->frames==anim->NumFrames()&&clip->lengthMs==anim->Length(),"Source animated door exact native clip differs");return anim;
}
template<class T>struct Aligned {T* data;explicit Aligned(std::size_t count):data(static_cast<T*>(Mem_Alloc16(count*sizeof(T)))){Need(data!=NULL,"Source animated door aligned pose allocation");}
    ~Aligned(){Mem_Free16(data);}Aligned(const Aligned&)=delete;Aligned& operator=(const Aligned&)=delete;};
void Pose(const idMD5Anim& clip,int frame,const idRenderModel& model,idJointMat* out){
    const int count=model.NumJoints();Need(count==2&&clip.NumJoints()==count&&frame>=0&&frame<clip.NumFrames(),"Source animated door two-joint frame domain");
    Aligned<idJointQuat> local(static_cast<std::size_t>(count));const int indices[2]={0,1};clip.GetSingleFrame(frame,local.data,indices,count);const auto* hierarchy=model.GetJoints();Need(hierarchy!=NULL,"Source animated door model hierarchy missing");
    for(int i=0;i<count;++i){out[i].SetRotation(local.data[i].q.ToMat3());out[i].SetTranslation(local.data[i].t);
        if(hierarchy[i].parent){const auto parent=hierarchy[i].parent-hierarchy;Need(parent>=0&&parent<i,"Source animated door invalid native hierarchy");out[i]*=out[parent];}
        for(int c=0;c<12;++c)Need(std::isfinite(out[i].ToFloatPtr()[c]),"Source animated door nonfinite source pose");}
}
struct NativeSnapshot {
    idRenderModel* value=NULL;
    ~NativeSnapshot(){if(value)renderModelManager->FreeModel(value);}
};
void NativeGeometry(idRenderModel& model){
    Need(model.IsLoaded()&&!model.IsDefaultModel()&&model.IsDynamicModel()==DM_CACHED&&model.NumJoints()==2&&model.GetDefaultPose()&&model.GetJoints(),
        "Source animated door original native dynamic model is unavailable");
    const auto* hierarchy=model.GetJoints();const auto* local=model.GetDefaultPose();
    Need(!hierarchy[0].parent&&hierarchy[1].parent==hierarchy,"Source animated door original native joint hierarchy differs");
    Aligned<idJointMat> joints(2);for(int i=0;i<2;++i){joints.data[i].SetRotation(local[i].q.ToMat3());joints.data[i].SetTranslation(local[i].t);if(i)joints.data[i]*=joints.data[0];
        for(int c=0;c<12;++c)Need(std::isfinite(joints.data[i].ToFloatPtr()[c]),"Source animated door nonfinite native default pose");}
    renderEntity_t entity;std::memset(&entity,0,sizeof(entity));entity.hModel=&model;entity.numJoints=2;entity.joints=joints.data;entity.axis.Identity();for(int i=0;i<4;++i)entity.shaderParms[i]=1;
    // MD5 templates own meshes, not idRenderModelStatic surfaces. Validate the
    // actual native posed geometry instead of the template's empty surfaces[].
    NativeSnapshot snapshot;snapshot.value=model.InstantiateDynamicModel(&entity,NULL,NULL);
    Need(snapshot.value&&snapshot.value->NumBaseSurfaces()>0&&snapshot.value->NumSurfaces()==snapshot.value->NumBaseSurfaces(),"Source animated door native dynamic geometry is absent");
    for(int i=0;i<snapshot.value->NumBaseSurfaces();++i){const auto* surface=snapshot.value->Surface(i);const auto* geometry=surface?surface->geometry:NULL;
        Need(surface&&surface->shader&&surface->shader->IsDrawn()&&geometry&&geometry->numVerts>0&&geometry->numIndexes>0&&geometry->numIndexes%3==0&&geometry->verts&&geometry->indexes,
            "Source animated door native dynamic draw is invalid");
        for(int index=0;index<geometry->numIndexes;++index)Need(static_cast<unsigned>(geometry->indexes[index])<static_cast<unsigned>(geometry->numVerts),"Source animated door native draw index outside vertices");
        for(int vertex=0;vertex<geometry->numVerts;++vertex)for(int axis=0;axis<3;++axis)Need(std::isfinite(geometry->verts[vertex].xyz[axis]),"Source animated door native draw position is nonfinite");}
}
double Difference(const idJointMat* a,const idJointMat* b){double error=0;for(int i=0;i<2;++i)for(int c=0;c<12;++c){
    const double d=std::abs(double(a[i].ToFloatPtr()[c])-b[i].ToFloatPtr()[c]);Need(std::isfinite(d),"Source animated door nonfinite native pose");error=std::max(error,d);}return error;}
void ConstantPose(const idAnim& animation,const idRenderModel& model){
    Need(animation.NumFrames()==2,"Source animated door idle/end clip must be the qualified two-frame source pose");
    Aligned<idJointMat> first(2),last(2);Pose(*animation.MD5Anim(0),0,model,first.data);Pose(*animation.MD5Anim(0),1,model,last.data);
    Need(Difference(first.data,last.data)==0,"Source animated door idle/end source pose is not exactly constant; native cycle support is required");
}
void Configure(WolfSourceAnimatedDoorFacts& facts,const wolf2009::TrainDoorSignalPlan& signal){
    Need(facts.instance.properties.size()<=512&&facts.defaults.literalFields.size()<=4096&&facts.defaults.opaqueFacts.empty(),"Source animated door effective field budget/opaque command");
    const std::set<std::string> fields={"classname","spawnclass","name","model","origin","rotation","anim","start_anim","anim1","anim2","num_anims","auto_advance","loop_last_anim",
        "frame","becomeusableonactivate","bind","noclipmodel","blend_in","cycle","wait","pose_frame","sound_bone","hide_finish"};
    for(const auto& field:facts.instance.properties)Need(fields.count(Fold(field.key))!=0,"Source animated door unimplemented instance field");
    for(const auto& field:facts.defaults.literalFields)Need(fields.count(field.first)!=0,"Source animated door unimplemented compiled default field");
    facts.name=Text(facts,"name");facts.classname=Text(facts,"classname");facts.model=Text(facts,"model");facts.bindName=Text(facts,"bind");
    Need(facts.classname=="env_train_door"&&Text(facts,"spawnclass")=="idAnimated"&&facts.model=="train_door","Source animated door requires the selected original func_animate class/model");
    bool parent=false;for(const auto& origin:facts.defaults.declarations)if(origin.declarationName=="func_animate")parent=true;
    Need(parent,"Source animated door compiled func_animate inheritance is absent");
    wolf2009::SourceEntityValue declaredNoClip;declaredNoClip.declaration=facts.defaults.Find("noclipmodel");std::int32_t noClip=0;
    Need(declaredNoClip.declaration&&declaredNoClip.declaration->encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&declaredNoClip.Integer(noClip)&&noClip==1&&Integer(facts,"noclipmodel")==1,
        "Source animated door compiled noclipmodel=1 qualification required");facts.sourceNoClipModel=true;
    Need(Integer(facts,"num_anims")==2&&Integer(facts,"auto_advance")==1&&Integer(facts,"loop_last_anim")==1&&Integer(facts,"becomeUsableOnActivate")==0,
        "Source animated door automatic two-stage sequence profile differs");facts.sourceLoopLastAnimation=true;
    Need(Integer(facts,"blend_in")==0&&Integer(facts,"cycle")==1&&Integer(facts,"wait")==-1&&Integer(facts,"pose_frame")==1&&Text(facts,"sound_bone")=="origin"&&Integer(facts,"hide_finish")==1,
        "Source animated door unsupported inherited animation behavior");
    facts.initialClip=Text(facts,"start_anim");facts.moveClip=Text(facts,"anim1");facts.endClip=Text(facts,"anim2");facts.rawEditorFrame=Integer(facts,"frame");
    Need(facts.rawEditorFrame>=0&&facts.initialClip=="idle"&&facts.moveClip=="door_move"&&facts.endClip=="end_idle"&&Text(facts,"anim")==facts.moveClip,"Source animated door exact authored clip sequence differs");
    Need(signal.targetEntityName==facts.name&&signal.targetClassname==facts.classname&&!signal.ownerEntityName.empty()&&signal.ownerEntityName.size()<=127&&
        !signal.actorAnimation.empty()&&signal.actorAnimation.size()<=127&&signal.actorStartNodeId!=0,"Source animated door actor-start signal target/owner is unqualified");
}
}

struct WolfSourceAnimatedDoor::Impl {
    SourceFile source;
    std::vector<unsigned char> sourceBytes;
    WolfSourceAnimatedDoorRequest request;
    WolfSourceAnimatedDoorFacts facts;
    std::shared_ptr<const WolfSourceActorBinding> binding;
    std::unique_ptr<ActorStartLease> actorStart;
    idEntityPtr<idEntity> parent;
    idEntityPtr<idAnimatedEntity> capturedActor;
    idEntityPtr<idAnimatedEntity> entity;
    WolfSourceAnimatedDoorState state;
    idVec3 origin,parentOrigin;
    idMat3 axis,parentAxis;
    bool busy=false;
    Impl():source(NULL,Close){}
    ~Impl(){idAnimatedEntity* child=entity.GetEntity();entity=NULL;if(child)delete child;actorStart.reset();binding.reset();source.reset();}
    WolfSourceActorStartEvidence ActorProof()const{Need(bool(actorStart),"Source animated door actor-start authority is absent");auto proof=actorStart->Read();Authority(proof,request);return proof;}
    void Parent(idEntity* master) const {
        Need(master&&Wolf2009_IsSourceMover(master),"Source animated door requires the actual native source mover as bind owner");
        WolfSourceMoverState current;idStr reason;Require(Wolf2009_QuerySourceMover(master,current,reason),reason,"Source animated door native bind owner query failed");
        Need(current.sourceName&&facts.bindName==current.sourceName&&current.entityIndex==facts.parentEntityIndex&&current.entitiesBytes==facts.identity.sourceBytes&&
            current.entitiesFingerprint==facts.identity.sourceFingerprint&&current.nativePusher&&!current.visualDiagnostic&&current.typedSourceResources,
            "Source animated door bind owner has different source ENTS/native pusher identity");
    }
    void Owner() const {
        Need(Active(),"Source animated door requires an active local single-player game");idStr reason;
        Need(bool(binding),"Source animated door source/native binding is absent");Require(binding->MatchesNative(reason),reason,"Source animated door source/native binding changed");
        idAnimatedEntity* child=entity.GetEntity();idEntity* master=parent.GetEntity();
        Need(child&&master&&gameLocal.FindEntity(facts.name.c_str())==child&&facts.name==child->GetName()&&facts.bindName==master->GetName()&&
            child->GetBindMaster()==master&&Wolf2009_IsSourceControlledEntity(child),"Source animated door native child/parent lifetime or identity changed");
        Parent(master);
        Need(child->GetPhysics()&&child->GetPhysics()->GetNumClipModels()==0&&child->GetPhysics()->GetContents()==0&&
            child->GetPhysics()->GetClipModel()==gameLocal.clip.DefaultClipModel(),"Source animated door no-clip source policy changed");
        const auto* animator=child->GetAnimator();Need(animator&&animator->ModelDef()==Wolf2009_FindRegisteredSourceAnimationSet(binding->NativeModelDefinition())&&!animator->RemoveOrigin(),"Source animated door actual animator/model changed");
    }
    WolfSourceControlledState Controlled() const {
        WolfSourceControlledState value;idStr reason;Require(Wolf2009_SourceControlledState(entity.GetEntity(),value,reason),reason,"Source animated door controlled state unavailable");
        Need(value.commandSerial==state.doorCommandSerial,"Source animated door was externally commanded outside its owner");return value;
    }
    void Phase(const WolfSourceControlledState& controlled) const {
        if(state.phase==WolfSourceAnimatedDoor_Idle)Need(controlled.phase==WolfSourceControlled_Held&&controlled.clip==facts.initialClip.c_str()&&controlled.sourceFrame==0,"Source animated door did not retain initial source pose");
        else if(state.phase==WolfSourceAnimatedDoor_Moving)Need(controlled.clip==facts.moveClip.c_str()&&controlled.commandTimeMs==state.activationTimeMs&&
            ((controlled.phase==WolfSourceControlled_Playing&&controlled.endTimeMs==state.nativeEndTimeMs)||(controlled.phase==WolfSourceControlled_Held&&controlled.sourceFrame==state.sourceFrames-1)),"Source animated door native move state changed");
        else Need(state.phase==WolfSourceAnimatedDoor_Completed&&controlled.phase==WolfSourceControlled_Held&&controlled.clip==facts.endClip.c_str()&&controlled.sourceFrame==0,"Source animated door final source pose changed");
    }
};
namespace {struct Busy {bool& value;explicit Busy(bool& v):value(v){Need(!v,"Source animated door reentry");v=true;}~Busy(){value=false;}};}
WolfSourceAnimatedDoor::WolfSourceAnimatedDoor(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
WolfSourceAnimatedDoor::~WolfSourceAnimatedDoor(){}
const WolfSourceAnimatedDoorFacts& WolfSourceAnimatedDoor::Facts() const{return impl_->facts;}
const WolfSourceActorBinding& WolfSourceAnimatedDoor::Binding() const{return *impl_->binding;}
idAnimatedEntity* WolfSourceAnimatedDoor::Entity() const{return impl_->entity.GetEntity();}

std::unique_ptr<WolfSourceAnimatedDoor> Wolf2009_PrepareSourceAnimatedDoor(const WolfSourceAnimatedDoorRequest& request,const WolfSourceDeclarationSet& declarations,idStr& reason){
    reason.Clear();try{
        Need(request.policy==WolfSourceAnimatedDoor_NativeControlledBoundSequenceV1&&request.levelGeneration!=0,"Source animated door explicit policy/generation required");
        RequestBounds(request); // Bound all owning copies before publication.
        if(request.expectedEntitiesIdentityKnown)Need(request.expectedEntitiesBytes>0&&request.expectedEntitiesBytes<=64u*1024u*1024u,"Source animated door expected ENTS length budget");
        else Need(!request.expectedEntitiesBytes&&!request.expectedEntitiesFingerprint,"Source animated door unknown ENTS identity carries metadata");
        const auto mount=Mount(request.exactEntitiesPath,"ents");std::unique_ptr<WolfSourceAnimatedDoor::Impl> out(new WolfSourceAnimatedDoor::Impl);out->request=request;
        out->actorStart.reset(new ActorStartLease(request.actorStart));out->ActorProof();
        out->source.reset(fileSystem->OpenFileRead(request.exactEntitiesPath.c_str(),false));out->sourceBytes=Read(out->source.get());
        auto document=wolf2009::DecodeEntities4(out->sourceBytes);Need(request.entityIndex<document.entities.size(),"Source animated door ENTS index out of range");
        auto& facts=out->facts;facts.instance=std::move(document.entities[request.entityIndex]);const auto classname=InstanceText(facts.instance,"classname");
        facts.defaults=declarations.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,classname,request.context);Configure(facts,request.signal);
        const auto actorPlan=wolf2009::actor::Prepare(declarations.Definitions(),declarations.Namespace(),classname,facts.model,request.context,request.resources);
        Need(actorPlan.ClipCount()==3&&actorPlan.ResourceRequest().resources.geometryProfile==wolf2009::actor::GeometryProfile::WeightedOrRigid64&&
            actorPlan.ResourceRequest().resources.lodPolicy==wolf2009::actor::LodPolicy::SingleResourceWithoutLodRecords,"Source animated door explicit original geometry/LOD profile required");
        // The actor capability pins this map mount. ENTS cannot silently come
        // from an unleased independently replaceable mount while it lives.
        const auto& resources=actorPlan.ResourceRequest().resources;Need(Mount(resources.modelResource,"modl")==mount&&Mount(resources.meshSkeletonResource,"skel")==mount,"Source animated door ENTS/model rig mount differs");
        for(std::size_t i=0;i<actorPlan.ClipCount();++i){const auto& clip=actorPlan.Clip(i).request;
            Need(clip.frameSounds.empty()&&Mount(clip.animationResource,"hkxa")==mount&&Mount(clip.animationSkeletonResource,"skel")==mount,"Source animated door unsupported clip command or independently unleased mount");}
        out->binding=Wolf2009_BindSourceActor(actorPlan,std::vector<WolfSourceActorSoundBinding>(),reason);Require(bool(out->binding),reason,"Source animated door original native actor binding failed");
        // Recheck after the owning capability acquired its mount lease.
        SourceFile pinned(fileSystem->OpenFileRead(request.exactEntitiesPath.c_str(),false),Close);Need(Read(pinned.get())==out->sourceBytes,"Source animated door ENTS changed while binding its selected source mount");out->source=std::move(pinned);
        facts.defaults=actorPlan.Entity();facts.identity.resourcePath=request.exactEntitiesPath;facts.identity.entityIndex=request.entityIndex;facts.identity.sourceBytes=out->sourceBytes.size();facts.identity.sourceFingerprint=Hash(out->sourceBytes);
        Need(!request.expectedEntitiesIdentityKnown||(request.expectedEntitiesBytes==facts.identity.sourceBytes&&request.expectedEntitiesFingerprint==facts.identity.sourceFingerprint),"Source animated door expected ENTS byte identity differs");
        std::size_t matches=0,parentMatches=0;for(std::size_t i=0;i<document.entities.size();++i){
            if(i==request.entityIndex)continue;
            const auto* name=document.entities[i].Find("name");if(!name||name->kind!=wolf2009::EntityProperty::Text)continue;
            if(name->text==facts.name)++matches;if(name->text==facts.bindName){facts.parentInstance=document.entities[i];facts.parentEntityIndex=i;++parentMatches;}}
        // The selected record was moved, so include its retained exact name.
        ++matches;Need(matches==1&&parentMatches==1&&facts.name!=facts.bindName,"Source animated door ambiguous exact ENTS name/bind owner");
        Placement(facts.instance,out->origin,out->axis);Placement(facts.parentInstance,out->parentOrigin,out->parentAxis);
        const auto* definition=Wolf2009_FindRegisteredSourceAnimationSet(out->binding->NativeModelDefinition());auto* model=definition?definition->ModelHandle():NULL;
        Need(model&&model->NumJoints()==2&&definition->NumJoints()==2&&std::string(definition->GetJointName(0))=="origin"&&std::string(definition->GetJointName(1))=="door_joint"&&
            std::string(model->Name())==out->binding->NativeModel(),"Source animated door original two-joint native model differs");NativeGeometry(*model);
        const auto* idle=NativeClip(*out->binding,facts.initialClip.c_str());const auto* move=NativeClip(*out->binding,facts.moveClip.c_str());const auto* end=NativeClip(*out->binding,facts.endClip.c_str());
        ConstantPose(*idle,*model);ConstantPose(*end,*model);Need(move->NumFrames()==182,"Source animated door selected move is not the measured 182-frame source clip");
        Aligned<idJointMat> endpoint(2),endPose(2);Pose(*move->MD5Anim(0),move->NumFrames()-1,*model,endpoint.data);Pose(*end->MD5Anim(0),0,*model,endPose.data);
        out->state.levelGeneration=request.levelGeneration;out->state.sourceFrames=move->NumFrames();out->state.sourceBones=model->NumJoints();out->state.sourceNoClipModel=true;
        out->state.qualifiedConstantEndPose=true;out->state.endPoseTransitionError=Difference(endpoint.data,endPose.data);
        return std::unique_ptr<WolfSourceAnimatedDoor>(new WolfSourceAnimatedDoor(std::move(out)));
    }catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceAnimatedDoor>();}
}

bool WolfSourceAnimatedDoor::Spawn(idEntity* parent,idStr& reason){reason.Clear();try{Busy guard(impl_->busy);auto& p=*impl_;
    Need(Active()&&p.state.phase==WolfSourceAnimatedDoor_Prepared&&!p.entity.GetEntity(),"Source animated door native Spawn state/clock");
    Need(parent&&p.facts.bindName==parent->GetName()&&gameLocal.FindEntity(p.facts.bindName.c_str())==parent&&parent!=gameLocal.world&&parent->GetPhysics(),"Source animated door exact live bind owner absent");
    p.Parent(parent);const auto* render=parent->GetRenderEntity();Need(render&&SamePose(p.parentOrigin,p.parentAxis,render->origin,render->axis),"Source animated door assembly must precede bind-owner movement");
    Require(p.binding->MatchesNative(reason),reason,"Source animated door native binding changed before Spawn");
    const auto actor=p.ActorProof();Need(!actor.committed,"Source animated door assembly must precede the actor owner's actual Start");NativeActor(actor);
    WolfSourceControlledState actorState;Require(Wolf2009_SourceControlledState(actor.nativeActor,actorState,reason),reason,"Source animated door inactive actor state missing");
    Need(actorState.phase==WolfSourceControlled_Held&&actor.nativeActor!=parent,"Source animated door actor owner must own a separate inactive held native actor");
    WolfSourceControlledRequest request;request.name=p.facts.name.c_str();request.modelDefinition=p.binding->NativeModelDefinition();request.initialClip=p.facts.initialClip.c_str();
    request.initialSourceFrame=0;request.origin=p.origin;request.axis=p.axis;request.policy=WolfSourceControlled_NativeMillisecondsHoldLastV2;
    idAnimatedEntity* entity=NULL;Require(Wolf2009_SpawnSourceControlled(request,entity,reason),reason,"Source animated door native controlled Spawn failed");Need(entity!=NULL,"Source animated door Spawn returned no native entity");
    std::unique_ptr<idAnimatedEntity> pending(entity);entity->Bind(parent,true);
    Need(entity->GetBindMaster()==parent&&SamePose(p.origin,p.axis,entity->GetPhysics()->GetOrigin(),entity->GetPhysics()->GetAxis()),"Source animated door native Bind did not preserve authored world pose");
    gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_NATIVE_CLIP models=%d contents=%d default_sentinel=%d\n",entity->GetPhysics()->GetNumClipModels(),entity->GetPhysics()->GetContents(),entity->GetPhysics()->GetClipModel()==gameLocal.clip.DefaultClipModel());
    // idPhysics_Static returns the engine's default sentinel for a missing
    // owned clip model. Its actual count and contents must both remain zero.
    Need(entity->GetPhysics()->GetNumClipModels()==0&&entity->GetPhysics()->GetContents()==0&&entity->GetPhysics()->GetClipModel()==gameLocal.clip.DefaultClipModel(),"Source animated door unexpectedly acquired native collision");
    WolfSourceControlledState initial;Need(Wolf2009_SourceControlledState(entity,initial,reason)&&initial.phase==WolfSourceControlled_Held&&initial.clip==p.facts.initialClip.c_str()&&initial.sourceFrame==0,"Source animated door native initial source frame differs");
    p.parent=parent;p.entity=entity;p.capturedActor=actor.nativeActor;p.state.actorNativeSpawnId=actor.nativeSpawnId;
    p.state.doorCommandSerial=initial.commandSerial;p.state.nativeBound=true;p.state.phase=WolfSourceAnimatedDoor_Idle;pending.release();return true;
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceAnimatedDoor::Activate(const wolf2009::TrainDoorCommand& command,idStr& reason){reason.Clear();try{Busy guard(impl_->busy);auto& p=*impl_;p.Owner();
    p.Phase(p.Controlled());const auto& signal=p.request.signal;
    Need(command.kind==wolf2009::TrainDoorCommand::ActivateTarget&&command.levelGeneration==p.request.levelGeneration&&command.eventId!=0&&command.sourceNodeId==signal.actorStartNodeId&&
        command.ownerEntityName==signal.ownerEntityName&&command.targetEntityName==p.facts.name&&command.resourceName.empty(),"Source animated door mismatched activation command");
    const auto proof=p.ActorProof();Need(proof.committed&&proof.command.eventId==command.eventId&&proof.command.levelGeneration==command.levelGeneration&&proof.command.sourceNodeId==command.sourceNodeId,
        "Source animated door activation does not match its actor owner's actual Start command");NativeActor(proof);
    auto* actor=p.capturedActor.GetEntity();const unsigned serial=proof.commandSerial;
    Need(actor&&actor==proof.nativeActor&&proof.nativeSpawnId==p.state.actorNativeSpawnId&&actor!=p.entity.GetEntity(),"Source animated door actor original captured native spawn was replaced");
    if(p.state.activations){Need(p.state.eventId==command.eventId&&p.state.sourceNodeId==command.sourceNodeId&&p.state.actorCommandSerial==serial&&p.state.activationTimeMs==proof.committedTimeMs,"Source animated door activation already committed by a different event/actor");
        Need(p.state.duplicateActivations!=(std::numeric_limits<unsigned>::max)(),"Source animated door duplicate counter overflow");++p.state.duplicateActivations;return true;}
    Need(p.state.phase==WolfSourceAnimatedDoor_Idle,"Source animated door is not awaiting actor start");WolfSourceControlledState start;
    Need(Wolf2009_SourceControlledState(actor,start,reason)&&start.phase==WolfSourceControlled_Playing&&start.clip==signal.actorAnimation.c_str()&&start.commandSerial==serial&&serial!=0&&
        start.commandTimeMs==gameLocal.time&&start.commandTimeMs==proof.committedTimeMs&&start.endTimeMs>gameLocal.time,"Source animated door requires the same-tick real actor owner PlayOnce commit");
    auto* animator=actor->GetAnimator();const auto* native=animator?animator->CurrentAnim(ANIMCHANNEL_ALL):NULL;const auto* anim=native?native->Anim():NULL;
    Need(native&&anim&&native->GetStartTime()==gameLocal.time&&native->GetEndTime()==start.endTimeMs&&signal.actorAnimation==anim->FullName()&&anim->NumAnims()==1&&anim->MD5Anim(0)&&anim->MD5Anim(0)->IsSourceAnimationV2(),"Source animated door real native actor channel did not commit the requested start");
    Require(Wolf2009_SourceControlledPlayOnce(p.entity.GetEntity(),p.facts.moveClip.c_str(),0,reason),reason,"Source animated door native PlayOnce failed");
    WolfSourceControlledState moving;Need(Wolf2009_SourceControlledState(p.entity.GetEntity(),moving,reason)&&moving.phase==WolfSourceControlled_Playing&&moving.clip==p.facts.moveClip.c_str()&&moving.commandTimeMs==gameLocal.time,"Source animated door native move commit differs");
    p.state.phase=WolfSourceAnimatedDoor_Moving;p.state.eventId=command.eventId;p.state.sourceNodeId=command.sourceNodeId;p.state.activations=1;
    p.state.activationTimeMs=gameLocal.time;p.state.nativeEndTimeMs=moving.endTimeMs;p.state.actorCommandSerial=serial;p.state.doorCommandSerial=moving.commandSerial;return true;
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceAnimatedDoor::Tick(idStr& reason){reason.Clear();try{Busy guard(impl_->busy);auto& p=*impl_;p.Owner();const auto controlled=p.Controlled();p.Phase(controlled);
    if(p.state.phase!=WolfSourceAnimatedDoor_Moving||controlled.phase!=WolfSourceControlled_Held)return true;
    Need(gameLocal.time>=p.state.nativeEndTimeMs&&p.state.completions==0,"Source animated door observed an early/repeated native endpoint");auto* entity=p.entity.GetEntity();auto* animator=entity->GetAnimator();
    animator->CreateFrame(gameLocal.time,true);int count=0;idJointMat* actual=NULL;animator->GetJoints(&count,&actual);Need(count==2&&actual,"Source animated door actual final palette missing");
    const auto* move=NativeClip(*p.binding,p.facts.moveClip.c_str());Aligned<idJointMat> expected(2);Pose(*move->MD5Anim(0),p.state.sourceFrames-1,*animator->ModelHandle(),expected.data);
    const double error=Difference(actual,expected.data);Need(error<=.0002,"Source animated door actual endpoint palette differs from exact source final frame/FK");
    Require(Wolf2009_SourceControlledHoldFrame(entity,p.facts.endClip.c_str(),0,0,reason),reason,"Source animated door native end-pose advance failed");
    WolfSourceControlledState end;Need(Wolf2009_SourceControlledState(entity,end,reason)&&end.phase==WolfSourceControlled_Held&&end.clip==p.facts.endClip.c_str()&&end.sourceFrame==0,"Source animated door native end-pose commit differs");
    p.state.endpointPaletteError=error;p.state.doorCommandSerial=end.commandSerial;p.state.phase=WolfSourceAnimatedDoor_Completed;p.state.completions=1;p.state.completionTimeMs=gameLocal.time;return true;
}catch(const std::exception& error){reason=error.what();return false;}}
bool WolfSourceAnimatedDoor::Query(WolfSourceAnimatedDoorState& out,idStr& reason)const{reason.Clear();try{
    if(impl_->state.phase!=WolfSourceAnimatedDoor_Prepared){impl_->Owner();impl_->Phase(impl_->Controlled());}out=impl_->state;return true;
}catch(const std::exception& error){reason=error.what();return false;}}
bool WolfSourceAnimatedDoor::TakeCompletion(bool& pending,WolfSourceAnimatedDoorCompletion& out,idStr& reason){pending=false;reason.Clear();try{Busy guard(impl_->busy);auto& p=*impl_;p.Owner();p.Phase(p.Controlled());
    if(p.state.completions==0||p.state.consumedCompletions)return true;WolfSourceAnimatedDoorCompletion value;value.levelGeneration=p.request.levelGeneration;value.eventId=p.state.eventId;
    value.sourceNodeId=p.state.sourceNodeId;value.entityName=p.facts.name;value.moveClip=p.facts.moveClip;value.endClip=p.facts.endClip;value.nativeEndTimeMs=p.state.nativeEndTimeMs;value.observedTimeMs=p.state.completionTimeMs;
    out=value;p.state.consumedCompletions=1;pending=true;return true;
}catch(const std::exception& error){reason=error.what();return false;}}

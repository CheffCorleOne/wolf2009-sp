// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in checks inside the initialized GPL game DSO. The source oracle owns
// the original HKXA/SKEL bytes obtained through the ordinary API8 filesystem.
#include "AnimationWire.h"
#include "GreeterAnimation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "renderer/tr_local.h"
#include "EngineAnimationGame.h"
#include "SourceVisualEntity.h"
namespace {
namespace wire = wolf2009::animation;
struct Stats {
    std::size_t checks=0, poses=0, snapshots=0, blends=0, legacy=0;
    double jointError=0, boundsError=0, subframeDifference=0;
};
void Need(Stats& s,bool condition,const char* message) { ++s.checks; if(!condition) throw std::runtime_error(message); }
void Near(Stats& s,double a,double b,double tolerance,double& maximum,const char* message) {
    const double error=std::abs(a-b); maximum=(std::max)(maximum,error);
    Need(s,std::isfinite(a)&&std::isfinite(b)&&error<=tolerance,message);
}
template<class T> class Aligned {
    T* data;
  public:
    explicit Aligned(std::size_t count):data(static_cast<T*>(Mem_Alloc16(count*sizeof(T)))) {}
    ~Aligned(){Mem_Free16(data);}
    T* Get() const{return data;}
  private:
    Aligned(const Aligned&); Aligned& operator=(const Aligned&);
};
idJointQuat Native(const wolf2009::GreeterJointPose& source) {
    idJointQuat out;
    out.q.Set(float(-source.rotation[0]),float(-source.rotation[1]),float(-source.rotation[2]),float(source.rotation[3]));
    out.t.Set(float(source.translation[0]),float(source.translation[1]),float(source.translation[2])); return out;
}
void Locals(const wolf2009::GreeterPose& source,idJointQuat* out) {
    for(std::size_t i=0;i<source.local.size();++i) out[i]=Native(source.local[i]);
}
struct Resources {
    idFile* descriptor=NULL;
    std::vector<idDeclModelDef*> definitions;
    idRenderModel* snapshotA=NULL;
    idRenderModel* snapshotB=NULL;
    const char* legacyPath="fixtures/source-animation-own-legacy.md5anim";
    const char* legacyMeshPath="fixtures/source-animation-own-legacy.md5mesh";
    bool legacyWritten=false,legacyMeshWritten=false;
    std::vector<idRenderModel*> ownModels;
    void CloseDescriptor(){if(descriptor){fileSystem->CloseFile(descriptor);descriptor=NULL;}}
    void ReleaseDefinitions(){
        delete snapshotA; snapshotA=NULL; delete snapshotB; snapshotB=NULL;
        for(auto* def:definitions){def->FreeData();def->Invalidate();}
        definitions.clear(); animationLib.FlushUnusedAnims();
    }
    ~Resources(){
        ReleaseDefinitions();CloseDescriptor();
        for(auto* model:ownModels){renderModelManager->RemoveModel(model);renderModelManager->FreeModel(model);}
        if(legacyWritten)fileSystem->RemoveFile(legacyPath);
        if(legacyMeshWritten)fileSystem->RemoveFile(legacyMeshPath);
    }
};
wire::Envelope Read(Stats& s,Resources& resources,const char* name) {
    resources.descriptor=fileSystem->OpenFileRead(name,false);
    Need(s,resources.descriptor!=NULL,"game API8 did not open the leased source descriptor");
    const int length=resources.descriptor->Length();
    Need(s,length>0&&std::size_t(length)<=wire::MaximumEnvelopeBytes,"game descriptor size budget");
    wire::Bytes bytes(static_cast<std::size_t>(length));
    Need(s,resources.descriptor->Read(bytes.data(),length)==length,"game descriptor short read");
    auto out=wire::Parse(bytes); Need(s,wire::Alias(out.binding)==name,"game descriptor alias/full metadata mismatch"); return out;
}
idDeclModelDef* Declaration(Stats& s,Resources& resources,const char* name,const std::string& text,bool valid) {
    idDecl* fresh=declManager->CreateNewDecl(DECL_MODELDEF,name,"<wolf2009-source-anim-qa>");
    Need(s,fresh!=NULL,"native modelDef allocation failed");
    fresh->SetText(text.c_str());fresh->Invalidate();
    auto* def=const_cast<idDeclModelDef*>(static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name,false)));
    Need(s,def!=NULL,"native modelDef disappeared");resources.definitions.push_back(def);
    Need(s,valid?def->GetState()!=DS_DEFAULTED:def->GetState()==DS_DEFAULTED,"native modelDef admission differs from expected model identity");
    return def;
}
void CompareLocal(Stats& s,const idJointQuat* actual,const wolf2009::GreeterPose& expected) {
    for(std::size_t i=0;i<expected.local.size();++i) {
        const idJointQuat wanted=Native(expected.local[i]);
        for(int axis=0;axis<3;++axis) Near(s,actual[i].t[axis],wanted.t[axis],0.00002,s.jointError,"timed source local translation differs from original curve");
        // Quaternion signs describe the same rotation; compare matrices.
        const idMat3 a=actual[i].q.ToMat3(),b=wanted.q.ToMat3();
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)
            Near(s,a[row][col],b[row][col],0.00002,s.jointError,"timed source local quaternion conversion differs");
    }
}
void CompareMatrices(Stats& s,const idJointMat* actual,const idJointMat* expected,int count,const char* message) {
    for(int i=0;i<count;++i)for(int c=0;c<12;++c)
        Near(s,actual[i].ToFloatPtr()[c],expected[i].ToFloatPtr()[c],c%4==3?0.0002:0.00002,s.jointError,message);
}
void CompareHierarchy(Stats& s,idAnimator& actor,const wolf2009::GreeterPose& source,const idVec3& offset) {
    int count=0; idJointMat* actual=NULL; actor.GetJoints(&count,&actual);
    Need(s,count==52&&actual&&source.model.size()==52,"native animator joint palette incomplete");
    for(int i=0;i<count;++i) {
        const idJointQuat wanted=Native(source.model[std::size_t(i)]); const idMat3 rotation=wanted.q.ToMat3();
        for(int axis=0;axis<3;++axis) Near(s,actual[i].ToVec3()[axis],wanted.t[axis]+offset[axis],0.0002,s.jointError,"native animator global hierarchy/root offset differs from original source pose");
        const idMat3 matrix=actual[i].ToMat3();
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)
            Near(s,matrix[row][col],rotation[row][col],0.00002,s.jointError,"native animator global rotation differs from original source pose");
    }
    ++s.poses;
}
void Snapshot(Stats& s,idAnimator& actor,int time,idRenderModel*& cached) {
    idBounds bounds;Need(s,actor.GetBounds(time,bounds)&&!bounds.IsCleared(),"native animator current/blended bounds unavailable");
    int count=0;idJointMat* joints=NULL;actor.GetJoints(&count,&joints);
    renderEntity_t entity;memset(&entity,0,sizeof(entity));entity.hModel=actor.ModelHandle();entity.axis.Identity();
    entity.joints=joints;entity.numJoints=count;entity.bounds=bounds;
    for(int i=0;i<4;++i)entity.shaderParms[i]=1;
    idRenderModel* previous=cached;cached=NULL;cached=entity.hModel->InstantiateDynamicModel(&entity,NULL,previous);
    Need(s,cached&&!cached->IsDefaultModel()&&cached->NumSurfaces()>0,"native animator did not produce an actual MD5 dynamic snapshot");
    Need(s,!previous||cached==previous,"native animator snapshot cache was not reused");
    const idBounds snapshot=cached->Bounds();
    for(int side=0;side<2;++side)for(int axis=0;axis<3;++axis)
        Near(s,bounds[side][axis],snapshot[side][axis],0.0002,s.boundsError,"native source current/blended bounds differ from actual dynamic mesh bounds");
    std::size_t vertices=0;
    for(int surface=0;surface<cached->NumSurfaces();++surface) {
        const auto* tri=cached->Surface(surface)->geometry;Need(s,tri&&tri->numVerts>0&&tri->numIndexes>0,"empty native animated surface");
        for(int i=0;i<tri->numVerts;++i)for(int axis=0;axis<3;++axis)
            Need(s,std::isfinite(tri->verts[i].xyz[axis])&&tri->verts[i].xyz[axis]>=bounds[0][axis]-0.0002&&tri->verts[i].xyz[axis]<=bounds[1][axis]+0.0002,
                 "native animator bounds do not enclose an actual skinned vertex");
        vertices+=std::size_t(tri->numVerts);
    }
    Need(s,vertices>0,"native animated mesh has no vertices");++s.snapshots;
}
struct OwnVisualEntities {
    std::vector<idEntityPtr<idEntity> > entities;
    void Add(idEntity* entity) { idEntityPtr<idEntity> ref;ref=entity;entities.push_back(ref); }
    ~OwnVisualEntities() { for(auto& ref:entities) if(ref.GetEntity()) delete ref.GetEntity(); }
};
int EntityFrames(int count) {
    usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));
    for(int i=0;i<count;++i) gameLocal.RunFrame(commands);
    return count;
}
void EntityPlayback(Stats& s,const char* modelDefinition,const wolf2009::GreeterAnimation& clip) {
    const std::size_t before=s.checks;
    const int live=Wolf2009_LiveSourceVisualEntities();
    const int nativeCount=gameLocal.spawnedEntities.Num();
    const int firstFree=gameLocal.firstFreeIndex;
    const auto spawnIds=wire::Fingerprint(reinterpret_cast<const unsigned char*>(gameLocal.spawnIds),sizeof(gameLocal.spawnIds));
    WolfSourceVisualRequest request;
    request.name="source_visual_qa_a";request.modelDefinition=modelDefinition;
    request.origin.Set(176.75f,271.75f,296.0f);request.axis=idAngles(0,37,0).ToMat3();
    request.startTimeMs=gameLocal.time;
    const auto rejected=[&](const WolfSourceVisualRequest& bad) {
        idAnimatedEntity* entity=NULL;idStr why;
        Need(s,!Wolf2009_SpawnSourceVisual(bad,entity,why)&&!entity&&why.Length()>0,"invalid source visual request was admitted");
        Need(s,gameLocal.spawnedEntities.Num()==nativeCount&&gameLocal.firstFreeIndex==firstFree&&
             wire::Fingerprint(reinterpret_cast<const unsigned char*>(gameLocal.spawnIds),sizeof(gameLocal.spawnIds))==spawnIds&&Wolf2009_LiveSourceVisualEntities()==live,
             "source visual refusal allocated/registered an entity before full preflight");
    };
    rejected(request); // No implicit clock/playback policy.
    request.policy=WolfSourceVisual_OneShotNativeMillisecondsV1;
    {auto bad=request;bad.origin.x=std::numeric_limits<float>::quiet_NaN();rejected(bad);}
    {auto bad=request;bad.axis[0]*=2;rejected(bad);}
    {auto bad=request;bad.axis[0]*=-1;rejected(bad);}
    {auto bad=request;bad.startTimeMs=std::numeric_limits<int>::max();rejected(bad);}
    {auto bad=request;bad.name="null_entity";rejected(bad);}
    {auto bad=request;bad.modelDefinition="wolf2009/animation-model/absent";rejected(bad);}
    auto* definition=const_cast<idDeclModelDef*>(Wolf2009_FindRegisteredSourceModelDef(modelDefinition));
    Need(s,definition!=NULL,"source visual registered definition unavailable");
    std::vector<char> text(std::size_t(definition->GetTextLength())+1,0);definition->GetText(text.data());
    definition->SetText((std::string(text.data())+"\n// modified external declaration\n").c_str());definition->Invalidate();
    rejected(request);
    definition->SetText(text.data());definition->Invalidate();
    Need(s,Wolf2009_FindRegisteredSourceModelDef(modelDefinition)==definition,"restored source declaration lost factory identity");
    int nativeFrames=0;
    {
        OwnVisualEntities own;
        idStr reason;idAnimatedEntity* first=NULL;
        Need(s,Wolf2009_SpawnSourceVisual(request,first,reason)&&first&&reason.IsEmpty(),"preflighted native source visual spawn failed");own.Add(first);
        Need(s,Wolf2009_IsSourceVisualEntity(first)&&first->GetPhysics()->GetNumClipModels()==0&&!first->GetAnimator()->RemoveOrigin(),
             "native source visual installed collision or root extraction");
        const int firstNumber=first->entityNumber;
        idEntityPtr<idEntity> stale;stale=first;const int oldSpawnId=stale.GetSpawnId();
        auto secondRequest=request;secondRequest.name="source_visual_qa_b";secondRequest.origin.Set(-31.25f,23.5f,9);
        secondRequest.axis=idAngles(0,-23,0).ToMat3();secondRequest.startTimeMs=gameLocal.time+512;
        idAnimatedEntity* second=NULL;
        Need(s,Wolf2009_SpawnSourceVisual(secondRequest,second,reason)&&second,"second independent native source visual spawn failed");own.Add(second);
        Need(s,first->GetAnimator()->ModelHandle()==second->GetAnimator()->ModelHandle(),"visual instances did not share the immutable source model");
        const int length=int(std::ceil(clip.Duration()*1000));
        const int end=secondRequest.startTimeMs+length;
        struct SnapshotScope { idRenderModel* mesh=NULL; ~SnapshotScope(){delete mesh;} } snapshot;
        nativeFrames+=EntityFrames(1);
        for(int sample=0;sample<5;++sample) {
            if(sample>0) nativeFrames+=EntityFrames(sample==4 ? (end-gameLocal.time)/gameLocal.msec+3 : 7);
            for(idAnimatedEntity* entity:{first,second}) {
                const auto& placement=entity==first?request:secondRequest;
                renderEntity_t* render=entity->GetRenderEntity();
                const double time=double(gameLocal.time-placement.startTimeMs)*0.001;
                Need(s,render->callback==idEntity::ModelCallback&&render->numJoints==52&&render->joints&&entity->GetModelDefHandle()>=0,
                     "native source visual Think/Present did not publish its MD5 palette and callback");
                // Read the palette produced by normal Think before any callback,
                // GetJointWorldTransform/GetBounds or forced CreateFrame can fix it.
                CompareHierarchy(s,*entity->GetAnimator(),clip.SamplePose(time),vec3_origin);
                if(sample==4) Need(s,!(entity->thinkFlags&TH_ANIMATE)&&!entity->GetAnimator()->IsAnimating(gameLocal.time),
                                   "native source visual did not stop normal animation thinking after its one-shot endpoint");
                entity->GetAnimator()->ForceUpdate();
                const int tick=gameLocal.time;
                Need(s,render->callback(render,NULL)&&gameLocal.time==tick,"native model callback failed or advanced the game clock");
                CompareHierarchy(s,*entity->GetAnimator(),clip.SamplePose(time),vec3_origin);
                Need(s,render->origin.Compare(placement.origin,0.0001f)&&render->axis.Compare(placement.axis,0.0001f)&&
                     entity->GetPhysics()->GetOrigin().Compare(placement.origin,0.0001f),"source visual placement was applied twice or moved by visual root animation");
                const auto pose=clip.SamplePose(time);
                idVec3 world;idMat3 rotation;
                Need(s,entity->GetJointWorldTransform(static_cast<jointHandle_t>(0),gameLocal.time,world,rotation),"native source visual world joint unavailable");
                const idVec3 local(float(pose.model[0].translation[0]),float(pose.model[0].translation[1]),float(pose.model[0].translation[2]));
                const idVec3 expected=placement.origin+local*placement.axis;
                for(int axis=0;axis<3;++axis) Near(s,world[axis],expected[axis],0.0003,s.jointError,"native source visual world joint placement differs");
                const renderEntity_t* published=gameRenderWorld->GetRenderEntity(entity->GetModelDefHandle());
                Need(s,published&&published->entityNum==entity->entityNumber&&published->hModel==render->hModel&&published->joints==render->joints,
                     "native render-world entry differs from animated game entity");
                Snapshot(s,*entity->GetAnimator(),gameLocal.time,snapshot.mesh);
                for(int side=0;side<2;++side) for(int axis=0;axis<3;++axis)
                    Near(s,render->bounds[side][axis],snapshot.mesh->Bounds()[side][axis],0.0002,s.boundsError,"Think bounds differ from actual entity skinned mesh");
            }
        }
        Need(s,gameLocal.time>end,"source visual native frames did not cross the one-shot endpoint");
        const auto hash=wire::Fingerprint(reinterpret_cast<const unsigned char*>(first->GetRenderEntity()->joints),52*sizeof(idJointMat));
        const int handle=first->GetModelDefHandle();
        first->SetModel("models/absent-unsupported.md5mesh");
        Need(s,first->GetModelDefHandle()==handle&&hash==wire::Fingerprint(reinterpret_cast<const unsigned char*>(first->GetRenderEntity()->joints),52*sizeof(idJointMat)),
             "source visual model-change refusal destroyed the live palette/render-world entry");
        delete first;first=NULL;
        Need(s,!stale.GetEntity()&&gameRenderWorld->GetRenderEntity(handle)==NULL,"source visual deletion left a live generation reference/render entry");
        auto replacement=request;replacement.startTimeMs=gameLocal.time;
        idAnimatedEntity* next=NULL;Need(s,Wolf2009_SpawnSourceVisual(replacement,next,reason)&&next,"native source visual respawn failed");own.Add(next);
        idEntityPtr<idEntity> fresh;fresh=next;
        Need(s,next->entityNumber==firstNumber&&fresh.GetSpawnId()!=oldSpawnId&&!stale.GetEntity(),"source visual generation reference rebound to a reused entity slot");
    }
    Need(s,Wolf2009_LiveSourceVisualEntities()==live&&gameLocal.spawnedEntities.Num()==nativeCount,"native source visual teardown leaked entities");
    printf("WOLF2009_NATIVE_SOURCE_VISUAL_QA_OK checks=%zu instances=2 native_frames=%d one_shot_endpoint=1 normal_think_present=1 model_callback=1 "
           "world_joint_placement=1 collision_models=0 root_extraction=0 generation_reuse=1 preflight_before_spawn=1 teardown=1 retail_actor_clock=0 gpu=0\n",s.checks-before,nativeFrames);
}
std::uint64_t PaletteHash(idAnimator& actor) {
    int count=0;idJointMat* joints=NULL;actor.GetJoints(&count,&joints);
    return wire::Fingerprint(reinterpret_cast<const unsigned char*>(joints),std::size_t(count)*sizeof(idJointMat));
}
std::uint64_t SnapshotHash(const idRenderModel* model) {
    std::uint64_t hash=UINT64_C(14695981039346656037);
    const auto add=[&](const void* p,std::size_t count){
        const auto* bytes=static_cast<const unsigned char*>(p);
        for(std::size_t i=0;i<count;++i)hash=(hash^bytes[i])*UINT64_C(1099511628211);
    };
    for(int surface=0;surface<model->NumSurfaces();++surface){
        const auto* tri=model->Surface(surface)->geometry;
        add(tri->indexes,std::size_t(tri->numIndexes)*sizeof(tri->indexes[0]));
        for(int v=0;v<tri->numVerts;++v){add(tri->verts[v].xyz.ToFloatPtr(),3*sizeof(float));add(tri->verts[v].st.ToFloatPtr(),2*sizeof(float));add(tri->verts[v].color,4);}
    }
    return hash;
}
int SubframeProof(Stats& s,const wolf2009::GreeterAnimation& clip,const idMD5Anim* animation,const std::vector<int>& all) {
    Aligned<idJointQuat> exact(52),control(52),next(52);
    int chosen=-1;
    for(int frame=0;frame<181;++frame) {
        const int time=int(std::floor((frame+0.37)*clip.FrameDuration()*1000));
        const double coordinate=(double(time)*0.001)/clip.FrameDuration();
        const int base=int(std::floor(coordinate));
        Locals(clip.SamplePose(base*clip.FrameDuration()),control.Get());
        Locals(clip.SamplePose((base+1)*clip.FrameDuration()),next.Get());
        SIMDProcessor->BlendJoints(control.Get(),next.Get(),float(coordinate-base),all.data(),52);
        Need(s,animation->GetPoseAtTime(time,1,exact.Get(),all.data(),52),"source subframe sampling failed");
        CompareLocal(s,exact.Get(),clip.SamplePose(double(time)*0.001));
        double difference=0;
        for(int joint=0;joint<52;++joint) {
            for(int axis=0;axis<3;++axis) difference=(std::max)(difference,double(std::abs(exact.Get()[joint].t[axis]-control.Get()[joint].t[axis])));
            const idMat3 a=exact.Get()[joint].q.ToMat3(),b=control.Get()[joint].q.ToMat3();
            for(int row=0;row<3;++row)for(int col=0;col<3;++col)difference=(std::max)(difference,double(std::abs(a[row][col]-b[row][col])));
        }
        if(difference>s.subframeDifference){s.subframeDifference=difference;chosen=time;}
    }
    Need(s,chosen>=0&&s.subframeDifference>0.00001,"source curve proof did not distinguish direct playback from a baked 24-Hz/SLERP clip");return chosen;
}
void Legacy(Stats& s,Resources& resources,const char* sourceDefinition) {
    // Own text fixture: no proprietary animation is rewritten or baked.
    const char* text="MD5Version 10\ncommandline \"\"\nnumFrames 3\nnumJoints 1\nframeRate 24\nnumAnimatedComponents 1\n"
        "hierarchy { \"root\" -1 1 0 }\nbounds { (-1 -1 -1)(3 1 1) (-1 -1 -1)(3 1 1) (-1 -1 -1)(3 1 1) }\n"
        "baseframe { (0 0 0)(0 0 0) }\nframe 0 { 0 }\nframe 1 { 1 }\nframe 2 { 2 }\n";
    Need(s,fileSystem->WriteFile(resources.legacyPath,text,int(strlen(text)))==int(strlen(text)),"own legacy MD5 fixture write failed");resources.legacyWritten=true;
    idMD5Anim legacy;Need(s,legacy.LoadAnim(resources.legacyPath)&&!legacy.IsSourceAnimation()&&legacy.NumJoints()==1&&legacy.NumFrames()==3,
                         "ordinary MD5 loader behavior changed");
    const int index=0;Aligned<idJointQuat> timed(1),stock(1);
    for(int cycles:{-1,1,2})for(int ms:{0,1,17,41,42,63,83,84,101}) {
        frameBlend_t frame;legacy.ConvertTimeToFrame(ms,cycles,frame);legacy.GetInterpolatedFrame(frame,stock.Get(),&index,1);
        Need(s,legacy.GetPoseAtTime(ms,cycles,timed.Get(),&index,1),"ordinary MD5 timed path refused");
        Need(s,memcmp(timed.Get()->q.ToFloatPtr(),stock.Get()->q.ToFloatPtr(),4*sizeof(float))==0&&
             memcmp(timed.Get()->t.ToFloatPtr(),stock.Get()->t.ToFloatPtr(),3*sizeof(float))==0,
             "ordinary MD5 timed path differs from stock frame interpolation");++s.legacy;
    }
    const char* mesh="MD5Version 10\ncommandline \"\"\nnumJoints 1\nnumMeshes 1\n"
        "joints { \"root\" -1 (0 0 0)(0 0 0) }\n"
        "mesh { shader \"_default\" numverts 3 vert 0 (0 0) 0 1 vert 1 (1 0) 1 1 vert 2 (0 1) 2 1 "
        "numtris 1 tri 0 0 1 2 numweights 3 weight 0 0 1 (0 0 0) weight 1 0 1 (4 0 0) weight 2 0 1 (0 4 0) }\n";
    Need(s,fileSystem->WriteFile(resources.legacyMeshPath,mesh,int(strlen(mesh)))==int(strlen(mesh)),"own legacy MD5 mesh fixture write failed");resources.legacyMeshWritten=true;
    const std::string name=std::string(sourceDefinition)+"/own-legacy";
    const std::string definition=std::string("{ mesh \"")+resources.legacyMeshPath+"\" anim control \""+resources.legacyPath+"\" }";
    idDeclModelDef* def=Declaration(s,resources,name.c_str(),definition,true);
    Need(s,def->ModelHandle()&&!def->ModelHandle()->IsDefaultModel()&&!def->UsesSourceAnimation(),"own legacy modelDef control failed");
    resources.ownModels.push_back(def->ModelHandle());
    idAnimator actor;Need(s,actor.SetModel(name.c_str())==def->ModelHandle(),"ordinary legacy animator control setup failed");
    actor.RemoveOriginOffset(true);actor.PlayAnim(ANIMCHANNEL_ALL,actor.GetAnim("control"),1000,0);
    actor.SetJointPos(static_cast<jointHandle_t>(0),JOINTMOD_LOCAL,idVec3(1,2,3));Need(s,actor.CreateFrame(1042,true),"ordinary legacy active palette unavailable");
    const auto hash=PaletteHash(actor);const int oldStart=actor.CurrentAnim(ANIMCHANNEL_ALL)->GetStartTime(),oldAnim=actor.CurrentAnim(ANIMCHANNEL_ALL)->AnimNum();
    Need(s,actor.SetModel(sourceDefinition)==NULL&&actor.ModelDef()==def&&actor.ModelHandle()==def->ModelHandle()&&actor.RemoveOrigin()&&
         actor.CurrentAnim(ANIMCHANNEL_ALL)->GetStartTime()==oldStart&&actor.CurrentAnim(ANIMCHANNEL_ALL)->AnimNum()==oldAnim&&PaletteHash(actor)==hash,
         "refused source SetModel root policy destroyed the active legacy animator/channels/palette");
    Need(s,actor.CreateFrame(1042,true)&&PaletteHash(actor)==hash,"source refusal removed the previous legacy joint modifications");
}
void Unsupported(Stats& s,idAnimator& actor,int animationNumber,int currentTime) {
    auto* blend=actor.CurrentAnim(ANIMCHANNEL_ALL);
    const int start=blend->GetStartTime(),end=blend->GetEndTime(),num=blend->AnimNum();
    const float weight=blend->GetWeight(currentTime); const auto hash=PaletteHash(actor);
    actor.CycleAnim(ANIMCHANNEL_ALL,animationNumber,currentTime,128);
    actor.SetFrame(ANIMCHANNEL_ALL,animationNumber,148,currentTime,128);
    blend->SetCycleCount(-1);blend->SetCycleCount(2);blend->SetPlaybackRate(currentTime,0.5f);actor.RemoveOriginOffset(true);
    Need(s,blend->GetStartTime()==start&&blend->GetEndTime()==end&&blend->AnimNum()==num&&blend->GetWeight(currentTime)==weight&&
         blend->GetCycleCount()==1&&blend->GetPlaybackRate()==1&&!actor.RemoveOrigin()&&PaletteHash(actor)==hash,
         "refused source loop/frame/rate/root request mutated native animator state");
    Need(s,actor.CurrentAnim(ANIMCHANNEL_ALL)[1].AnimNum()==0,"refused source request shifted native blend channels");
}
}
#if defined(_WIN32)
#define WOLF2009_ANIMATION_EXPORT __declspec(dllexport)
#else
#define WOLF2009_ANIMATION_EXPORT __attribute__((visibility("default")))
#endif
extern "C" WOLF2009_ANIMATION_EXPORT int Wolf2009_RunSourceAnimatorQA(const char* alias,const char* otherModelAlias) {
    Stats stats;
    try {
        Resources resources;const auto envelope=Read(stats,resources,alias);
        const auto clip=wolf2009::GreeterAnimation::Load(envelope.animation,envelope.skeleton);
        idStr definition;Need(stats,Wolf2009_CreateSourceModelDef(alias,NULL,definition),"ordinary source modelDef registration failed");
        auto* def=const_cast<idDeclModelDef*>(static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,definition,false)));
        Need(stats,def&&def->UsesSourceAnimation()&&def->GetState()!=DS_DEFAULTED,"registered source modelDef did not parse");resources.definitions.push_back(def);
        idMD5Anim* animation=animationLib.GetAnim(alias);
        Need(stats,animation&&animation->IsSourceAnimation()&&animation->NumJoints()==52&&animation->NumFrames()==182&&
             animation->Length()==int(std::ceil(clip.Duration()*1000))&&animation->SourceFrameDuration()==clip.FrameDuration(),"source MD5 animation metadata/tick policy differs");
        Need(stats,animation->SourceMatchesModel(def->ModelHandle()),"source animation rejected its exact registered native model");
        idRenderModel* other=renderModelManager->CheckModel(otherModelAlias);
        Need(stats,other&&other!=def->ModelHandle()&&other->NumJoints()==52&&!other->IsDefaultModel(),"independent wrong-identity source model unavailable");
        for(int i=0;i<52;++i)Need(stats,other->GetJoints()[i].name==def->ModelHandle()->GetJoints()[i].name,
                                  "wrong-identity control does not have the same source skeleton");
        Need(stats,!animation->SourceMatchesModel(other),"source animation matched a different model alias with identical 52 joint names");
        const std::string wrongName=std::string(definition.c_str())+"/wrong-model";
        const std::string wrongText=std::string("{ mesh \"")+otherModelAlias+"\" anim source \""+alias+"\" }";
        Declaration(stats,resources,wrongName.c_str(),wrongText,false);
        const std::string offsetName=std::string(definition.c_str())+"/offset";
        const std::string offsetText=std::string("{ mesh \"")+envelope.binding.modelAlias+"\" offset (3 5 7) anim source \""+alias+"\" }";
        Declaration(stats,resources,offsetName.c_str(),offsetText,true);
        std::vector<int> all;for(int i=0;i<52;++i)all.push_back(i);
        Aligned<idJointQuat> direct(52);
        Need(stats,!animation->GetPoseAtTime(17,-1,direct.Get(),all.data(),52)&&!animation->GetPoseAtTime(17,2,direct.Get(),all.data(),52),
             "source animation silently accepted unsupported loop policy");
        idVec3 root;idQuat rootRotation;animation->GetOrigin(root,1234,1);animation->GetOriginRotation(rootRotation,1234,1);
        Need(stats,root==vec3_origin&&rootRotation==idQuat(0,0,0,1)&&animation->TotalMovementDelta()==vec3_origin,"visual source root was extracted as actor movement");
        const int fractional=SubframeProof(stats,clip,animation,all);
        Legacy(stats,resources,definition.c_str());
        {
            idAnimator actorA,actorB,offsetActor,blendActor;
            Need(stats,actorA.SetModel(definition)==def->ModelHandle()&&actorB.SetModel(definition)==def->ModelHandle(),"ordinary idAnimator modelDef setup failed");
            Need(stats,offsetActor.SetModel(offsetName.c_str())==def->ModelHandle()&&blendActor.SetModel(definition)==def->ModelHandle(),"native offset/blend actor setup failed");
            const int anim=actorA.GetAnim("source");Need(stats,anim>0&&actorB.GetAnim("source")==anim,"source clip absent in native animator");
            actorA.PlayAnim(ANIMCHANNEL_ALL,anim,1000,0);actorB.PlayAnim(ANIMCHANNEL_ALL,anim,2600,0);
            offsetActor.PlayAnim(ANIMCHANNEL_ALL,anim,1000,0);
            Need(stats,actorA.CurrentAnim(ANIMCHANNEL_ALL)->GetEndTime()==1000+animation->Length()&&actorA.CurrentAnim(ANIMCHANNEL_ALL)->GetCycleCount()==1,
                 "native PlayAnim did not retain one-cycle ceil-end timing");
            actorB.CreateFrame(2600+1234,true);CompareHierarchy(stats,actorB,clip.SamplePose(1.234),vec3_origin);
            Snapshot(stats,actorB,2600+1234,resources.snapshotB);const auto bHash=PaletteHash(actorB),bMesh=SnapshotHash(resources.snapshotB);
            for(int elapsed:{0,1,17,21,42,43,91,1234,fractional,animation->Length()-1,animation->Length(),animation->Length()+17}) {
                Need(stats,animation->GetPoseAtTime(elapsed,1,direct.Get(),all.data(),52),"direct source sample failed");
                CompareLocal(stats,direct.Get(),clip.SamplePose(double(elapsed)*0.001));
                frameBlend_t frame;animation->ConvertTimeToFrame(elapsed,1,frame);
                animation->GetInterpolatedFrame(frame,direct.Get(),all.data(),52);
                CompareLocal(stats,direct.Get(),clip.SamplePose(double(elapsed)*0.001));
                actorA.CreateFrame(1000+elapsed,true);CompareHierarchy(stats,actorA,clip.SamplePose(double(elapsed)*0.001),vec3_origin);
                Snapshot(stats,actorA,1000+elapsed,resources.snapshotA);
                idBounds clipBounds;animation->GetBounds(clipBounds,elapsed,1);const idBounds meshBounds=resources.snapshotA->Bounds();
                for(int side=0;side<2;++side)for(int axis=0;axis<3;++axis)
                    Near(stats,clipBounds[side][axis],meshBounds[side][axis],0.0002,stats.boundsError,"source idMD5Anim bounds differ from current native animated mesh");
                Need(stats,resources.snapshotA!=resources.snapshotB&&PaletteHash(actorB)==bHash&&SnapshotHash(resources.snapshotB)==bMesh,
                     "advancing actor A mutated independent actor B palette/cached dynamic snapshot");
            }
            actorA.CreateFrame(1000+1234,true);int countA=0,countB=0;idJointMat* jointsA=NULL;idJointMat* jointsB=NULL;
            actorA.GetJoints(&countA,&jointsA);actorB.GetJoints(&countB,&jointsB);
            Need(stats,jointsA!=jointsB&&countA==countB,"independent animators shared mutable joint palette storage");
            CompareMatrices(stats,jointsA,jointsB,52,"shifted explicit actor starts do not reproduce the same elapsed source pose");
            animation->GetSingleFrame(0,direct.Get(),all.data(),52);CompareLocal(stats,direct.Get(),clip.SamplePose(0));
            animation->GetSingleFrame(181,direct.Get(),all.data(),52);CompareLocal(stats,direct.Get(),clip.SamplePose(clip.Duration()));
            Unsupported(stats,actorA,anim,1000+1234);
            offsetActor.CreateFrame(1000+fractional,true);CompareHierarchy(stats,offsetActor,clip.SamplePose(double(fractional)*0.001),idVec3(3,5,7));
            Snapshot(stats,offsetActor,1000+fractional,resources.snapshotA);
            actorA.ServiceAnims(1000+animation->Length()-1,1000+animation->Length()+17);
            Need(stats,actorA.FrameHasChanged(1000+animation->Length()+17),"tick crossing ceil-end did not schedule the native final pose");
            Need(stats,actorA.CreateFrame(1000+animation->Length()+17,false),"ceil-end dirty update was lost");
            CompareHierarchy(stats,actorA,clip.SamplePose(clip.Duration()),vec3_origin);
            Need(stats,!actorA.FrameHasChanged(1000+animation->Length()+17),"source final-frame dirty flag did not clear after native update");
            blendActor.PlayAnim(ANIMCHANNEL_ALL,anim,1000,0);blendActor.CreateFrame(2000,true);
            blendActor.PlayAnim(ANIMCHANNEL_ALL,anim,2000,128);const int blendTime=2064;
            auto* channels=blendActor.CurrentAnim(ANIMCHANNEL_ALL);const float w0=channels[0].GetWeight(blendTime),w1=channels[1].GetWeight(blendTime);
            Need(stats,w0>0&&w0<1&&w1>0&&w1<1,"native crossfade control has no two active source blend channels");
            Aligned<idJointQuat> expectedLocal(52),older(52);Aligned<idJointMat> expectedGlobal(52);
            Locals(clip.SamplePose(double(channels[0].AnimTime(blendTime))*0.001),expectedLocal.Get());
            Locals(clip.SamplePose(double(channels[1].AnimTime(blendTime))*0.001),older.Get());
            SIMDProcessor->BlendJoints(expectedLocal.Get(),older.Get(),w1/(w0+w1),all.data(),52);
            SIMDProcessor->ConvertJointQuatsToJointMats(expectedGlobal.Get(),expectedLocal.Get(),52);
            std::vector<int> parents;for(const auto& joint:clip.Joints())parents.push_back(joint.parent);
            SIMDProcessor->TransformJoints(expectedGlobal.Get(),parents.data(),1,51);
            Need(stats,blendActor.CreateFrame(blendTime,true),"native source crossfade did not produce a frame");
            int blendCount=0;idJointMat* blended=NULL;blendActor.GetJoints(&blendCount,&blended);Need(stats,blendCount==52,"native blended source palette incomplete");
            CompareMatrices(stats,blended,expectedGlobal.Get(),52,"native source crossfade differs from source samples blended by the existing native policy");
            Snapshot(stats,blendActor,blendTime,resources.snapshotA);++stats.blends;
            Need(stats,animation->Reload(),"same-epoch source animation reload failed");
            Need(stats,animation->GetPoseAtTime(fractional,1,direct.Get(),all.data(),52),"source sample unavailable after transactional reload");
            CompareLocal(stats,direct.Get(),clip.SamplePose(double(fractional)*0.001));
        }
        EntityPlayback(stats,definition.c_str(),clip);
        resources.ReleaseDefinitions();resources.CloseDescriptor();
        printf("WOLF2009_NATIVE_SOURCE_ANIMATOR_QA_OK checks=%zu poses=%zu snapshots=%zu blends=%zu legacy_controls=%zu joints=52 frames=182 "
               "duration_seconds=%.17g ceil_end_ms=%d fractional_ms=%d subframe_vs_baked_delta=%.9g max_joint_error=%.9g max_bounds_error=%.9g "
               "play_cycles=1 root_extraction=0 retail_actor_clock=0 model_identity=1 two_instances=1 same_epoch_reload=1 game_flush=1 gpu=0\n",
               stats.checks,stats.poses,stats.snapshots,stats.blends,stats.legacy,clip.Duration(),int(std::ceil(clip.Duration()*1000)),fractional,
               stats.subframeDifference,stats.jointError,stats.boundsError);
        return 1;
    } catch(const std::exception& error) {
        fprintf(stderr,"WOLF2009_NATIVE_SOURCE_ANIMATOR_FAIL checks=%zu poses=%zu message=%s\n",stats.checks,stats.poses,error.what());
    } catch(...) { fprintf(stderr,"WOLF2009_NATIVE_SOURCE_ANIMATOR_FAIL checks=%zu message=native-exception\n",stats.checks); }
    return 0;
}

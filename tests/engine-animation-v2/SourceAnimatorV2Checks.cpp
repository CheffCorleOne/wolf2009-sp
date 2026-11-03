// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in GPL game-module checks. Only POD alias strings cross the QA export.
#include "AnimationWireV2.h"
#include "SourceRigAnimation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "renderer/tr_local.h"
#include "EngineAnimationGame.h"
#include "SourceVisualEntity.h"
namespace {
namespace wire=wolf2009::animation2;
struct Stats{std::size_t checks=0,poses=0,frames=0,snapshots=0,nativeFrames=0;double jointError=0,boundsError=0;};
void Need(Stats& s,bool value,const char* why){++s.checks;if(!value)throw std::runtime_error(why);}
void Near(Stats& s,double a,double b,double tolerance,double& maximum,const char* why){const double difference=std::abs(a-b);maximum=(std::max)(maximum,difference);Need(s,std::isfinite(a)&&std::isfinite(b)&&difference<=tolerance,why);}
template<class T>class Aligned{T* value;public:explicit Aligned(std::size_t count):value(static_cast<T*>(Mem_Alloc16(count*sizeof(T)))){}~Aligned(){Mem_Free16(value);}T* Get()const{return value;}private:Aligned(const Aligned&);Aligned& operator=(const Aligned&);};
idJointQuat Native(const wolf2009::GreeterJointPose& pose){idJointQuat out;out.q.Set(float(-pose.rotation[0]),float(-pose.rotation[1]),float(-pose.rotation[2]),float(pose.rotation[3]));out.t.Set(float(pose.translation[0]),float(pose.translation[1]),float(pose.translation[2]));return out;}
std::uint64_t Hash(const void* bytes,std::size_t count){return wire::Fingerprint(static_cast<const unsigned char*>(bytes),count);}
std::uint64_t PaletteHash(idAnimator& actor){int count=0;idJointMat* joints=NULL;actor.GetJoints(&count,&joints);return Hash(joints,std::size_t(count)*sizeof(idJointMat));}
std::uint64_t MeshHash(const idRenderModel* model){std::uint64_t hash=UINT64_C(14695981039346656037);for(int surface=0;surface<model->NumSurfaces();++surface){const auto* tri=model->Surface(surface)->geometry;
    for(int v=0;v<tri->numVerts;++v)for(int axis=0;axis<3;++axis){const auto* data=reinterpret_cast<const unsigned char*>(&tri->verts[v].xyz[axis]);for(unsigned b=0;b<sizeof(float);++b)hash=(hash^data[b])*UINT64_C(1099511628211);}}return hash;}
struct Resources{
    idFile* descriptor=NULL;std::vector<idDeclModelDef*> definitions;std::vector<idEntityPtr<idEntity>> entities;idRenderModel* snapshotA=NULL;idRenderModel* snapshotB=NULL;
    Resources(){definitions.reserve(4);entities.reserve(2);}
    void Own(idEntity* entity){idEntityPtr<idEntity> ref;ref=entity;entities.push_back(ref);}
    ~Resources(){for(auto& entity:entities)if(entity.GetEntity())delete entity.GetEntity();delete snapshotA;delete snapshotB;
        for(auto* definition:definitions){definition->FreeData();definition->Invalidate();}animationLib.FlushUnusedAnims();if(descriptor)fileSystem->CloseFile(descriptor);}
};
wire::Envelope Read(Stats& s,Resources& resources,const char* alias){resources.descriptor=fileSystem->OpenFileRead(alias,false);Need(s,resources.descriptor!=NULL,"v2 game API8 descriptor open failed");
    const int count=resources.descriptor->Length();Need(s,count>0&&std::size_t(count)<=wire::MaximumEnvelopeBytes,"v2 game descriptor size budget");wire::Bytes bytes(static_cast<std::size_t>(count));
    Need(s,resources.descriptor->Read(bytes.data(),count)==count,"v2 game descriptor short read");auto envelope=wire::Parse(bytes);Need(s,wire::Alias(envelope.binding)==alias,"v2 game descriptor alias/full-key mismatch");return envelope;}
void Locals(const wolf2009::GreeterPose& pose,idJointQuat* out){for(std::size_t i=0;i<pose.local.size();++i)out[i]=Native(pose.local[i]);}
void CompareLocal(Stats& s,const idJointQuat* actual,const wolf2009::GreeterPose& expected){for(std::size_t i=0;i<expected.local.size();++i){const auto wanted=Native(expected.local[i]);
    for(unsigned axis=0;axis<3;++axis)Near(s,actual[i].t[axis],wanted.t[axis],.00002,s.jointError,"v2 native local translation differs from source sampler");
    const auto a=actual[i].q.ToMat3(),b=wanted.q.ToMat3();for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)Near(s,a[row][col],b[row][col],.00002,s.jointError,"v2 native local rotation differs from source sampler");}}
void CompareGlobal(Stats& s,idAnimator& actor,const wolf2009::GreeterPose& expected){int count=0;idJointMat* actual=NULL;actor.GetJoints(&count,&actual);Need(s,actual&&count==int(expected.model.size()),"v2 native full palette size mismatch");
    for(int i=0;i<count;++i){const auto wanted=Native(expected.model[std::size_t(i)]);const auto a=actual[i].ToMat3(),b=wanted.q.ToMat3();
        for(unsigned axis=0;axis<3;++axis)Near(s,actual[i].ToVec3()[axis],wanted.t[axis],.0002,s.jointError,"v2 native model-space hierarchy differs");
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)Near(s,a[row][col],b[row][col],.00002,s.jointError,"v2 native model-space rotation differs");}++s.poses;}
void Snapshot(Stats& s,idAnimator& actor,int time,idRenderModel*& cached){idBounds bounds;Need(s,actor.GetBounds(time,bounds)&&!bounds.IsCleared(),"v2 current animated bounds unavailable");
    int count=0;idJointMat* joints=NULL;actor.GetJoints(&count,&joints);renderEntity_t entity;memset(&entity,0,sizeof(entity));entity.hModel=actor.ModelHandle();entity.axis.Identity();entity.joints=joints;entity.numJoints=count;entity.bounds=bounds;
    for(int i=0;i<4;++i)entity.shaderParms[i]=1;idRenderModel* previous=cached;cached=NULL;cached=entity.hModel->InstantiateDynamicModel(&entity,NULL,previous);
    Need(s,cached&&!cached->IsDefaultModel()&&cached->NumSurfaces()>0&&(!previous||cached==previous),"v2 actual native dynamic snapshot/cache unavailable");
    const auto meshBounds=cached->Bounds();for(int side=0;side<2;++side)for(int axis=0;axis<3;++axis)Near(s,bounds[side][axis],meshBounds[side][axis],.0002,s.boundsError,"v2 current bounds differ from actual skinned mesh");
    for(int surface=0;surface<cached->NumSurfaces();++surface){const auto* tri=cached->Surface(surface)->geometry;Need(s,tri&&tri->numVerts>0,"v2 native animated surface empty");
        for(int v=0;v<tri->numVerts;++v)for(int axis=0;axis<3;++axis)Need(s,std::isfinite(tri->verts[v].xyz[axis])&&tri->verts[v].xyz[axis]>=bounds[0][axis]-.0002f&&tri->verts[v].xyz[axis]<=bounds[1][axis]+.0002f,"v2 bounds do not enclose a native animated vertex");}++s.snapshots;}
idDeclModelDef* OwnDefinition(Stats& s,Resources& resources,const char* name,const std::string& text,bool valid){Need(s,!declManager->FindType(DECL_MODELDEF,name,false),"v2 QA declaration namespace already occupied");
    idDecl* base=declManager->CreateNewDecl(DECL_MODELDEF,name,"<wolf2009-source-animation-v2-qa>");Need(s,base!=NULL,"v2 QA declaration creation failed");
    auto* definition=static_cast<idDeclModelDef*>(base);resources.definitions.push_back(definition);base->SetText(text.c_str());base->Invalidate();
    definition=const_cast<idDeclModelDef*>(static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name,false)));
    Need(s,definition&&(valid?definition->GetState()!=DS_DEFAULTED:definition->GetState()==DS_DEFAULTED),"v2 modelDef wrong-model admission differs");return definition;}
void Refusals(Stats& s,idAnimator& actor,int anim,int now){auto* blend=actor.CurrentAnim(ANIMCHANNEL_ALL);const int start=blend->GetStartTime(),end=blend->GetEndTime(),number=blend->AnimNum();const auto hash=PaletteHash(actor);
    actor.CycleAnim(ANIMCHANNEL_ALL,anim,now,128);actor.SetFrame(ANIMCHANNEL_ALL,anim,17,now,128);blend->SetCycleCount(-1);blend->SetCycleCount(2);blend->SetPlaybackRate(now,.5f);actor.RemoveOriginOffset(true);
    Need(s,blend->GetStartTime()==start&&blend->GetEndTime()==end&&blend->AnimNum()==number&&blend->GetCycleCount()==1&&blend->GetPlaybackRate()==1&&!actor.RemoveOrigin()&&PaletteHash(actor)==hash&&actor.CurrentAnim(ANIMCHANNEL_ALL)[1].AnimNum()==0,"v2 unsupported playback request mutated native state");}
void EntityPlayback(Stats& s,Resources& resources,const char* definition,const wolf2009::SourceRigAnimation& oracle,const std::string& tag){
    const int live=Wolf2009_LiveSourceVisualEntities(),before=gameLocal.spawnedEntities.Num();const std::string name="source_v2_visual_"+tag;
    WolfSourceVisualRequest request;request.name=name.c_str();request.modelDefinition=definition;
    request.origin.Set(133.125f,-79.5f,21.75f);request.axis=idAngles(0,31,0).ToMat3();request.startTimeMs=gameLocal.time;request.policy=WolfSourceVisual_OneShotNativeMillisecondsV1;
    idAnimatedEntity* entity=NULL;idStr reason;Need(s,Wolf2009_SpawnSourceVisual(request,entity,reason)&&entity,"v2 native SourceVisual spawn failed");resources.Own(entity);
    Need(s,entity->GetPhysics()->GetNumClipModels()==0&&!entity->GetAnimator()->RemoveOrigin(),"v2 visual acquired collision/root extraction");
    const int length=int(std::ceil(oracle.Duration()*1000));Need(s,length<=30000&&gameLocal.msec>0,"v2 visual QA frame budget");
    const int end=request.startTimeMs+length;
    for(unsigned sample=0;sample<4;++sample){const int frames=sample==3?(end-gameLocal.time)/gameLocal.msec+3:sample?7:1;
        usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));for(int i=0;i<frames;++i){gameLocal.RunFrame(commands);++s.nativeFrames;}
        const auto expected=oracle.SamplePose(double(gameLocal.time-request.startTimeMs)*.001);auto* render=entity->GetRenderEntity();
        Need(s,render->callback==idEntity::ModelCallback&&render->numJoints==int(oracle.Skeleton().joints.size())&&render->joints&&entity->GetModelDefHandle()>=0,"v2 normal Think/Present did not publish a native palette");
        CompareGlobal(s,*entity->GetAnimator(),expected); // Read BEFORE any helper can force a frame.
        Need(s,render->origin.Compare(request.origin,.0001f)&&render->axis.Compare(request.axis,.0001f)&&entity->GetPhysics()->GetOrigin().Compare(request.origin,.0001f),"v2 SourceVisual placement was applied twice/moved by the source root");
        if(sample==3)Need(s,gameLocal.time>end&&!(entity->thinkFlags&TH_ANIMATE)&&!entity->GetAnimator()->IsAnimating(gameLocal.time),"v2 SourceVisual one-shot did not stop normal animation thinking");
        const auto* published=gameRenderWorld->GetRenderEntity(entity->GetModelDefHandle());Need(s,published&&published->joints==render->joints&&published->hModel==render->hModel,"v2 render-world entity differs from normal game entity");
        idVec3 position;idMat3 axis;Need(s,entity->GetJointWorldTransform(jointHandle_t(0),gameLocal.time,position,axis),"v2 source visual world joint unavailable");
        const auto& root=expected.model[0].translation;const idVec3 wanted=request.origin+idVec3(float(root[0]),float(root[1]),float(root[2]))*request.axis;
        for(unsigned i=0;i<3;++i)Near(s,position[i],wanted[i],.0003,s.jointError,"v2 source visual world joint transform differs");
        Snapshot(s,*entity->GetAnimator(),gameLocal.time,resources.snapshotA);for(int side=0;side<2;++side)for(int i=0;i<3;++i)Near(s,render->bounds[side][i],resources.snapshotA->Bounds()[side][i],.0002,s.boundsError,"v2 SourceVisual Think bounds stale");
    }
    idEntityPtr<idEntity> stale;stale=entity;const int handle=entity->GetModelDefHandle();delete entity;
    Need(s,!stale.GetEntity()&&gameRenderWorld->GetRenderEntity(handle)==NULL&&Wolf2009_LiveSourceVisualEntities()==live&&gameLocal.spawnedEntities.Num()==before,"v2 SourceVisual destruction leaked generation/render entry");
}
}
#if defined(_WIN32)
#define WOLF2009_V2_QA_EXPORT __declspec(dllexport)
#else
#define WOLF2009_V2_QA_EXPORT __attribute__((visibility("default")))
#endif
extern "C" WOLF2009_V2_QA_EXPORT int Wolf2009_RunSourceAnimatorV2QA(const char* alias,const char* wrongModelAlias){Stats stats;
    try{Resources resources;const auto envelope=Read(stats,resources,alias);const auto& binding=envelope.binding;
        wolf2009::SourceRigRequest request;request.clipName=binding.clipName;request.skeletonName=binding.animationSkeletonName;request.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;
        const auto oracle=wolf2009::SourceRigAnimation::Load(envelope.animation,envelope.skeleton,request);const int bones=int(binding.bones);
        Need(stats,bones>=1&&bones<=256&&binding.frames<=4096&&binding.duration<=30,"v2 native QA count/time budget");
        const int priorDecls=declManager->GetNumDecls(DECL_MODELDEF);std::vector<declState_t> priorStates;
        for(int i=0;i<priorDecls;++i)priorStates.push_back(declManager->DeclByIndex(DECL_MODELDEF,i,false)->GetState());
        idStr definitionName;
        Need(stats,Wolf2009_CreateSourceModelDef(alias,NULL,definitionName),"v2 ordinary modelDef factory refused descriptor");
        auto* definition=const_cast<idDeclModelDef*>(Wolf2009_FindRegisteredSourceModelDef(definitionName));Need(stats,definition!=NULL,"v2 registered source modelDef unavailable");
        // Own only this call's newly parsed refs, including a repeat invocation
        // after our previous FreeData/Invalidate. Earlier parsed callers retain
        // their refs. The state snapshot does not force unrelated decls to parse.
        if(definition->Index()>=priorDecls||priorStates[definition->Index()]==DS_UNPARSED)resources.definitions.push_back(definition);
        idMD5Anim* animation=animationLib.GetAnim(alias);Need(stats,animation&&animation->IsSourceAnimation()&&animation->NumJoints()==bones&&animation->NumFrames()==int(binding.frames)&&animation->Length()==int(std::ceil(binding.duration*1000))&&animation->SourceDuration()==binding.duration&&animation->SourceFrameDuration()==binding.frameDuration,"v2 native source metadata mismatch");
        Need(stats,animation->SourceMatchesModel(definition->ModelHandle()),"v2 native source rejected its qualified mesh");
        idRenderModel* wrong=renderModelManager->CheckModel(wrongModelAlias);Need(stats,wrong&&wrong!=definition->ModelHandle()&&wrong->IsLoaded()&&!wrong->IsDefaultModel()&&!animation->SourceMatchesModel(wrong),"v2 wrong source model identity was accepted");
        static unsigned invocation=0;Need(stats,invocation<(std::numeric_limits<unsigned>::max)(),"v2 QA invocation namespace exhausted");
        const std::string tag=std::string(alias).substr(std::string(alias).find_last_of('/')+1,16)+"_"+std::to_string(++invocation),wrongName=std::string(definitionName.c_str())+"/v2-qa-wrong/"+tag;
        OwnDefinition(stats,resources,wrongName.c_str(),std::string("{ mesh \"")+wrongModelAlias+"\" anim source \""+alias+"\" }",false);
        std::vector<int> all;for(int i=0;i<bones;++i)all.push_back(i);Aligned<idJointQuat> direct(bones);
        memset(direct.Get(),0x5a,std::size_t(bones)*sizeof(idJointQuat));const auto untouched=Hash(direct.Get(),std::size_t(bones)*sizeof(idJointQuat));
        Need(stats,!animation->SourceFramePose(-1,direct.Get(),all.data(),bones)&&!animation->SourceFramePose(binding.frames,direct.Get(),all.data(),bones)&&
            !animation->SourceFramePose((std::numeric_limits<double>::quiet_NaN)(),direct.Get(),all.data(),bones)&&
            Hash(direct.Get(),std::size_t(bones)*sizeof(idJointQuat))==untouched,"v2 invalid frame mutated its caller's palette");
        Need(stats,!animation->GetPoseAtTime(17,-1,direct.Get(),all.data(),bones)&&!animation->GetPoseAtTime(17,2,direct.Get(),all.data(),bones),"v2 source accepted unsupported cycles");
        const int end=animation->Length();Need(stats,end>=8,"v2 native blend QA needs at least eight source milliseconds");
        std::vector<int> milliseconds{0,1,17,21,43,137,1234,end-1,end,end+17};
        if(binding.blocks>1){const int edge=int(binding.blockDuration*1000);milliseconds.push_back(edge-1);milliseconds.push_back(edge);milliseconds.push_back(edge+1);}
        {idAnimator first,second,blendActor;Need(stats,first.SetModel(definitionName)==definition->ModelHandle()&&second.SetModel(definitionName)==definition->ModelHandle()&&blendActor.SetModel(definitionName)==definition->ModelHandle(),"v2 ordinary animator model setup failed");
            const int anim=first.GetAnim("source");Need(stats,anim>0,"v2 ordinary modelDef has no source animation");first.PlayAnim(ANIMCHANNEL_ALL,anim,1000,0);second.PlayAnim(ANIMCHANNEL_ALL,anim,2600,0);
            second.CreateFrame(2737,true);Snapshot(stats,second,2737,resources.snapshotB);const auto secondPalette=PaletteHash(second),secondMesh=MeshHash(resources.snapshotB);
            for(int elapsed:milliseconds){Need(stats,animation->GetPoseAtTime(elapsed,1,direct.Get(),all.data(),bones),"v2 direct native timed pose failed");CompareLocal(stats,direct.Get(),oracle.SamplePose(double(elapsed)*.001));
                frameBlend_t frame;animation->ConvertTimeToFrame(elapsed,1,frame);const double coordinate=oracle.FrameCoordinateAtTime(double(elapsed)*.001);
                Need(stats,frame.frame1==int(coordinate)&&frame.frame2==(coordinate>=binding.frames-1?frame.frame1:frame.frame1+1)&&frame.cycleCount==0,"v2 native ConvertTimeToFrame ignored block-aware coordinate");
                animation->GetInterpolatedFrame(frame,direct.Get(),all.data(),bones);CompareLocal(stats,direct.Get(),oracle.SamplePoseFrame(double(frame.frame1)+(frame.frame1==frame.frame2?0.0:frame.backlerp)));
                first.CreateFrame(1000+elapsed,true);CompareGlobal(stats,first,oracle.SamplePose(double(elapsed)*.001));Snapshot(stats,first,1000+elapsed,resources.snapshotA);
                Need(stats,resources.snapshotA!=resources.snapshotB&&PaletteHash(second)==secondPalette&&MeshHash(resources.snapshotB)==secondMesh,"v2 advancing instance mutated another palette/cached mesh");}
            std::vector<double> coordinates{0,.37,double(binding.frames-1)};if(binding.blocks>1){const double split=binding.maxFramesPerBlock-1;coordinates.push_back(std::nextafter(split,0.0));coordinates.push_back(split);coordinates.push_back(split+.37);}
            for(double frame:coordinates){Need(stats,animation->SourceFramePose(frame,direct.Get(),all.data(),bones),"v2 explicit native frame failed");CompareLocal(stats,direct.Get(),oracle.SamplePoseFrame(frame));++stats.frames;}
            animation->GetSingleFrame(int(binding.frames-1),direct.Get(),all.data(),bones);CompareLocal(stats,direct.Get(),oracle.SamplePoseFrame(binding.frames-1));
            for(int i=0;i<32;++i){const double time=binding.duration*(i+.37)/32;const int ms=int(std::floor(time*1000));Need(stats,animation->GetPoseAtTime(ms,1,direct.Get(),all.data(),bones),"v2 fractional native sample failed");CompareLocal(stats,direct.Get(),oracle.SamplePose(ms*.001));}
            first.PlayAnim(ANIMCHANNEL_ALL,anim,1000,0);first.CreateFrame(1137,true);Refusals(stats,first,anim,1137);
            first.ServiceAnims(1000+end-1,1000+end+17);Need(stats,first.FrameHasChanged(1000+end+17)&&first.CreateFrame(1000+end+17,false),"v2 ceil-end tick failed to schedule native final palette");CompareGlobal(stats,first,oracle.SamplePose(binding.duration));
            Need(stats,!first.FrameHasChanged(1000+end+17),"v2 final dirty update did not clear");
            const int transition=1000+(std::min)(1000,end/3),blendTime=(std::min)(128,(std::max)(2,end/6)),blendSample=transition+blendTime/2;
            Need(stats,blendSample<1000+end,"v2 blend QA sample is outside the older one-shot");
            blendActor.PlayAnim(ANIMCHANNEL_ALL,anim,1000,0);blendActor.CreateFrame(transition,true);blendActor.PlayAnim(ANIMCHANNEL_ALL,anim,transition,blendTime);
            auto* channels=blendActor.CurrentAnim(ANIMCHANNEL_ALL);const float a=channels[0].GetWeight(blendSample),b=channels[1].GetWeight(blendSample);Need(stats,a>0&&b>0,"v2 native blend lacks two active channels");
            Aligned<idJointQuat> expected(bones),older(bones);Aligned<idJointMat> global(bones);Locals(oracle.SamplePose(channels[0].AnimTime(blendSample)*.001),expected.Get());Locals(oracle.SamplePose(channels[1].AnimTime(blendSample)*.001),older.Get());
            SIMDProcessor->BlendJoints(expected.Get(),older.Get(),b/(a+b),all.data(),bones);SIMDProcessor->ConvertJointQuatsToJointMats(global.Get(),expected.Get(),bones);std::vector<int> parents;for(const auto& joint:oracle.Skeleton().joints)parents.push_back(joint.parent);if(bones>1)SIMDProcessor->TransformJoints(global.Get(),parents.data(),1,bones-1);
            blendActor.CreateFrame(blendSample,true);int count=0;idJointMat* actual=NULL;blendActor.GetJoints(&count,&actual);Need(stats,count==bones,"v2 native blended palette size mismatch");for(int i=0;i<bones;++i)for(int c=0;c<12;++c)Near(stats,actual[i].ToFloatPtr()[c],global.Get()[i].ToFloatPtr()[c],c%4==3?.0002:.00002,stats.jointError,"v2 native crossfade differs from native blend of direct source curves");Snapshot(stats,blendActor,blendSample,resources.snapshotA);
            Need(stats,animation->Reload(),"v2 same-binding animation reload failed");Need(stats,animation->GetPoseAtTime(137,1,direct.Get(),all.data(),bones),"v2 timed sampler unavailable after reload");CompareLocal(stats,direct.Get(),oracle.SamplePose(.137));
        }
        EntityPlayback(stats,resources,definitionName,oracle,tag);
        printf("WOLF2009_NATIVE_SOURCE_ANIMATOR_V2_QA_OK checks=%zu poses=%zu frame_samples=%zu snapshots=%zu native_frames=%zu bones=%d frames=%u blocks=%u ceil_end_ms=%d max_joint_error=%.9g max_bounds_error=%.9g source_time_direct=1 source_frame_direct=1 one_cycle=1 normal_sourcevisual=1 full_key_reload=1 api8_extension=0 retail_actor_clock=0 gpu=0\n",stats.checks,stats.poses,stats.frames,stats.snapshots,stats.nativeFrames,bones,binding.frames,binding.blocks,end,stats.jointError,stats.boundsError);
        return 1;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_ANIMATOR_V2_FAIL checks=%zu message=%s\n",stats.checks,error.what());}catch(...){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_ANIMATOR_V2_FAIL checks=%zu message=native-exception\n",stats.checks);}return 0;
}

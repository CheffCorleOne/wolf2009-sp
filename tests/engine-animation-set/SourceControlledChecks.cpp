// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in game-module checks. Original data arrives only through leased API8
// descriptors; no scene/controller assumptions are inferred from clip names.
#include "AnimationWireV2.h"
#include "SourceRigAnimation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "renderer/tr_local.h"
#include "EngineAnimationSet.h"
#include "SourceControlledEntity.h"
namespace {
namespace wire=wolf2009::animation2;
struct Stats { std::size_t checks=0,poses=0,snapshots=0,nativeFrames=0,refusals=0,legacy=0;double jointError=0,boundsError=0; };
void Need(Stats& s,bool value,const char* why) { ++s.checks;if(!value)throw std::runtime_error(why); }
void Near(Stats& s,double a,double b,double tolerance,double& maximum,const char* why) {
    const double error=std::abs(a-b);maximum=(std::max)(maximum,error);
    Need(s,std::isfinite(a)&&std::isfinite(b)&&error<=tolerance,why);
}
template<class T>class Aligned {
    T* value;
  public:explicit Aligned(std::size_t count):value(static_cast<T*>(Mem_Alloc16(count*sizeof(T)))){}~Aligned(){Mem_Free16(value);}T* Get()const{return value;}
  private:Aligned(const Aligned&);Aligned& operator=(const Aligned&);
};
idJointQuat Native(const wolf2009::GreeterJointPose& pose) {
    idJointQuat out;out.q.Set(float(-pose.rotation[0]),float(-pose.rotation[1]),float(-pose.rotation[2]),float(pose.rotation[3]));
    out.t.Set(float(pose.translation[0]),float(pose.translation[1]),float(pose.translation[2]));return out;
}
struct Resources {
    std::vector<idEntityPtr<idEntity>> entities;
    std::vector<idDeclModelDef*> definitions;
    std::vector<idRenderModel*> ownModels;
    std::vector<std::string> ownFiles;
    std::string set;
    idRenderModel* firstSnapshot=NULL;
    idRenderModel* secondSnapshot=NULL;
    Resources(){entities.reserve(2);definitions.reserve(2);ownModels.reserve(1);ownFiles.reserve(2);}
    void Own(idEntity* entity){idEntityPtr<idEntity> pointer;pointer=entity;entities.push_back(pointer);}
    ~Resources(){
        for(auto& entity:entities)if(entity.GetEntity())delete entity.GetEntity();
        delete firstSnapshot;delete secondSnapshot;
        if(!set.empty())Wolf2009_ForgetSourceAnimationSet(set.c_str());
        for(auto* definition:definitions){definition->FreeData();definition->Invalidate();}
        animationLib.FlushUnusedAnims();
        for(auto* model:ownModels){renderModelManager->RemoveModel(model);renderModelManager->FreeModel(model);}
        for(const auto& path:ownFiles)fileSystem->RemoveFile(path.c_str());
    }
};
struct FileCloser { void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);} };
wire::Envelope Read(Stats& s,const char* alias) {
    std::unique_ptr<idFile,FileCloser> file(fileSystem->OpenFileRead(alias,false));Need(s,file.get()!=NULL,"controlled QA cannot open source-v2 descriptor");
    const int length=file->Length();Need(s,length>0&&std::size_t(length)<=wire::MaximumEnvelopeBytes,"controlled QA descriptor size budget");
    wire::Bytes bytes(static_cast<std::size_t>(length));Need(s,file->Read(bytes.data(),length)==length,"controlled QA short descriptor read");
    auto envelope=wire::Parse(bytes);Need(s,wire::Alias(envelope.binding)==alias,"controlled QA descriptor full-key alias differs");return envelope;
}
wolf2009::SourceRigAnimation Oracle(const wire::Envelope& envelope) {
    wolf2009::SourceRigRequest request;request.clipName=envelope.binding.clipName;request.skeletonName=envelope.binding.animationSkeletonName;
    request.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;
    return wolf2009::SourceRigAnimation::Load(envelope.animation,envelope.skeleton,request);
}
WolfSourceControlledState State(Stats& s,idAnimatedEntity* entity) {
    WolfSourceControlledState out;idStr reason;Need(s,Wolf2009_SourceControlledState(entity,out,reason)&&reason.IsEmpty(),"controlled state unavailable");return out;
}
std::uint64_t Palette(const idAnimatedEntity* entity) {
    const auto* render=const_cast<idAnimatedEntity*>(entity)->GetRenderEntity();
    return wire::Fingerprint(reinterpret_cast<const unsigned char*>(render->joints),std::size_t(render->numJoints)*sizeof(idJointMat));
}
void Frames(Stats& s,int count) {
    Need(s,count>=1&&count<=4096&&gameLocal.msec>0,"controlled QA native-frame budget");
    usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));
    for(int i=0;i<count;++i){gameLocal.RunFrame(commands);++s.nativeFrames;}
}
void CompareMatrices(Stats& s,idAnimatedEntity* entity,const idJointMat* expected,int count) {
    // This deliberately reads the palette published by ordinary Think before
    // callback/GetBounds/GetJointTransform can repair a scheduling failure.
    const renderEntity_t* render=entity->GetRenderEntity();
    Need(s,render->joints&&render->numJoints==count&&render->callback==idEntity::ModelCallback&&entity->GetModelDefHandle()>=0,
         "controlled normal Think did not publish the native MD5 palette");
    for(int i=0;i<count;++i)for(int component=0;component<12;++component)
        Near(s,render->joints[i].ToFloatPtr()[component],expected[i].ToFloatPtr()[component],component%4==3?.0002:.00002,
             s.jointError,"controlled published palette differs from direct source/native blend");
    const auto* published=gameRenderWorld->GetRenderEntity(entity->GetModelDefHandle());
    Need(s,published&&published->joints==render->joints&&published->hModel==render->hModel,"controlled Present did not retain its native render-world entry");
    ++s.poses;
}
void ComparePose(Stats& s,idAnimatedEntity* entity,const wolf2009::GreeterPose& pose) {
    Aligned<idJointMat> expected(pose.model.size());
    for(std::size_t i=0;i<pose.model.size();++i){const auto joint=Native(pose.model[i]);expected.Get()[i].SetRotation(joint.q.ToMat3());expected.Get()[i].SetTranslation(joint.t);}
    CompareMatrices(s,entity,expected.Get(),int(pose.model.size()));
}
void Snapshot(Stats& s,idAnimatedEntity* entity,idRenderModel*& cached) {
    auto* render=entity->GetRenderEntity();const idBounds publishedBounds=render->bounds;
    idBounds actualBounds;Need(s,entity->GetAnimator()->GetBounds(gameLocal.time,actualBounds)&&!actualBounds.IsCleared(),"controlled exact native bounds unavailable");
    for(int side=0;side<2;++side)for(int axis=0;axis<3;++axis)
        Near(s,publishedBounds[side][axis],actualBounds[side][axis],.0002,s.boundsError,"controlled Think bounds were stale before forced helpers");
    idRenderModel* previous=cached;cached=NULL;cached=render->hModel->InstantiateDynamicModel(render,NULL,previous);
    Need(s,cached&&!cached->IsDefaultModel()&&cached->NumSurfaces()>0&&(!previous||previous==cached),"controlled actual skinned snapshot/cache failed");
    const idBounds bounds=cached->Bounds();
    for(int side=0;side<2;++side)for(int axis=0;axis<3;++axis)
        Near(s,publishedBounds[side][axis],bounds[side][axis],.0002,s.boundsError,"controlled bounds differ from actual dynamic mesh");
    for(int surface=0;surface<cached->NumSurfaces();++surface){const auto* tri=cached->Surface(surface)->geometry;Need(s,tri&&tri->numVerts>0,"controlled skinned surface is empty");
        for(int v=0;v<tri->numVerts;++v)for(int axis=0;axis<3;++axis)
            Need(s,std::isfinite(tri->verts[v].xyz[axis])&&tri->verts[v].xyz[axis]>=bounds[0][axis]-.0002f&&tri->verts[v].xyz[axis]<=bounds[1][axis]+.0002f,
                 "controlled source bounds do not enclose an actual deformed vertex");}
    ++s.snapshots;
}
struct ChannelState {
    int animation,start,end,cycle,frame;float weight,rate;
    bool operator==(const ChannelState& other)const{return animation==other.animation&&start==other.start&&end==other.end&&cycle==other.cycle&&frame==other.frame&&weight==other.weight&&rate==other.rate;}
};
std::vector<ChannelState> Channels(idAnimatedEntity* entity) {
    auto* blend=entity->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL);std::vector<ChannelState> result;
    for(int i=0;i<ANIM_MaxAnimsPerChannel;++i)result.push_back({blend[i].AnimNum(),blend[i].GetStartTime(),blend[i].GetEndTime(),blend[i].GetCycleCount(),blend[i].GetFrameNumber(gameLocal.time),blend[i].GetWeight(gameLocal.time),blend[i].GetPlaybackRate()});
    return result;
}
bool SameState(const WolfSourceControlledState& a,const WolfSourceControlledState& b) {
    return a.phase==b.phase&&a.clip==b.clip&&a.sourceFrame==b.sourceFrame&&a.commandTimeMs==b.commandTimeMs&&a.endTimeMs==b.endTimeMs&&a.blendEndTimeMs==b.blendEndTimeMs&&a.commandSerial==b.commandSerial;
}
void Refusals(Stats& s,idAnimatedEntity* entity,int frames) {
    const auto before=State(s,entity);const auto channels=Channels(entity);const auto palette=Palette(entity);idStr reason;
    const auto unchanged=[&](){Need(s,SameState(before,State(s,entity))&&Channels(entity)==channels&&Palette(entity)==palette,
        "controlled refused command mutated native channels/state/palette");++s.refusals;};
    Need(s,!Wolf2009_SourceControlledHoldFrame(entity,"absent",0,0,reason)&&!reason.IsEmpty(),"controlled admitted an unknown clip");unchanged();
    for(int frame:{-1,frames,(std::numeric_limits<int>::max)()}){
        Need(s,!Wolf2009_SourceControlledHoldFrame(entity,"first",frame,0,reason)&&!reason.IsEmpty(),"controlled admitted an invalid frame");unchanged();}
    for(int blend:{-1,(std::numeric_limits<int>::max)()}){
        Need(s,!Wolf2009_SourceControlledHoldFrame(entity,"first",0,blend,reason)&&!reason.IsEmpty(),"controlled admitted an invalid hold blend/time");unchanged();
        Need(s,!Wolf2009_SourceControlledPlayOnce(entity,"first",blend,reason)&&!reason.IsEmpty(),"controlled admitted an invalid play blend/time");unchanged();}
    auto* animator=entity->GetAnimator();const int number=animator->ModelDef()->GetSpecificAnim("first");
    animator->SetFrame(ANIMCHANNEL_ALL,number,0,gameLocal.time,0);unchanged();
    animator->SetFrame(ANIMCHANNEL_ALL,number,frames+1,gameLocal.time,0);unchanged();
    animator->SetFrame(ANIMCHANNEL_TORSO,number,1,gameLocal.time,0);unchanged();
    animator->CycleAnim(ANIMCHANNEL_ALL,number,gameLocal.time,0);unchanged();
    animator->CurrentAnim(ANIMCHANNEL_ALL)->SetPlaybackRate(gameLocal.time,.5f);unchanged();
    animator->CurrentAnim(ANIMCHANNEL_ALL)->SetCycleCount(2);unchanged();
    animator->RemoveOriginOffset(true);unchanged();
    entity->SetModel("models/unsupported-replacement.md5mesh");unchanged();
}
void Crossfade(Stats& s,idAnimatedEntity* entity,const wolf2009::SourceRigAnimation& first,const wolf2009::SourceRigAnimation& second) {
    auto* animator=entity->GetAnimator();auto* blends=animator->CurrentAnim(ANIMCHANNEL_ALL);
    const int count=first.Skeleton().joints.size();Aligned<idJointQuat> pose(count),older(count);Aligned<idJointMat> global(count);
    std::vector<int> indices,parents;for(int i=0;i<count;++i){indices.push_back(i);parents.push_back(first.Skeleton().joints[std::size_t(i)].parent);}
    const float newerWeight=blends[0].GetWeight(gameLocal.time),olderWeight=blends[1].GetWeight(gameLocal.time);
    Need(s,newerWeight>0&&olderWeight>0&&blends[0].AnimNum()==animator->ModelDef()->GetSpecificAnim("second")&&blends[1].AnimNum()==animator->ModelDef()->GetSpecificAnim("first"),
         "controlled crossfade lacks the exact two requested source aliases");
    const auto a=second.SamplePose(blends[0].AnimTime(gameLocal.time)*.001),b=first.SamplePose(blends[1].AnimTime(gameLocal.time)*.001);
    for(int i=0;i<count;++i){pose.Get()[i]=Native(a.local[std::size_t(i)]);older.Get()[i]=Native(b.local[std::size_t(i)]);}
    SIMDProcessor->BlendJoints(pose.Get(),older.Get(),olderWeight/(newerWeight+olderWeight),indices.data(),count);
    SIMDProcessor->ConvertJointQuatsToJointMats(global.Get(),pose.Get(),count);
    if(count>1)SIMDProcessor->TransformJoints(global.Get(),parents.data(),1,count-1);
    CompareMatrices(s,entity,global.Get(),count);
}
void WriteOwn(Stats& s,Resources& resources,const std::string& path,const std::string& bytes) {
    resources.ownFiles.push_back(path);Need(s,fileSystem->WriteFile(path.c_str(),bytes.data(),int(bytes.size()))==int(bytes.size()),"controlled own legacy fixture write failed");
}
void LegacyWide(Stats& s,Resources& resources,const std::string& tag) {
    // A genuine native MD5 control, independent of source aliases, exercises a
    // representable frame above 32767 without forging a qualified core lease.
    const std::string clipPath="fixtures/controlled-own-wide-"+tag+".md5anim",meshPath="fixtures/controlled-own-wide-"+tag+".md5mesh";
    const int frames=40001;
    std::string text="MD5Version 10\ncommandline \"\"\nnumFrames 40001\nnumJoints 1\nframeRate 24\nnumAnimatedComponents 1\n"
        "hierarchy { \"root\" -1 1 0 }\nbounds {\n";
    text.reserve(3*1024*1024);for(int i=0;i<frames;++i)text+="(-1 -1 -1)(40005 5 1)\n";
    text+="}\nbaseframe { (0 0 0)(0 0 0) }\n";
    for(int i=0;i<frames;++i)text+="frame "+std::to_string(i)+" { "+std::to_string(i)+" }\n";
    WriteOwn(s,resources,clipPath,text);
    WriteOwn(s,resources,meshPath,"MD5Version 10\ncommandline \"\"\nnumJoints 1\nnumMeshes 1\njoints { \"root\" -1 (0 0 0)(0 0 0) }\n"
        "mesh { shader \"_default\" numverts 3 vert 0 (0 0) 0 1 vert 1 (1 0) 1 1 vert 2 (0 1) 2 1 numtris 1 tri 0 0 1 2 "
        "numweights 3 weight 0 0 1 (0 0 0) weight 1 0 1 (4 0 0) weight 2 0 1 (0 4 0) }\n");
    const std::string name="wolf2009/controlled-qa/legacy/"+tag;
    Need(s,!declManager->FindType(DECL_MODELDEF,name.c_str(),false),"controlled own legacy declaration occupied");
    auto* declaration=static_cast<idDeclModelDef*>(declManager->CreateNewDecl(DECL_MODELDEF,name.c_str(),"<wolf2009-controlled-own-qa>"));
    resources.definitions.push_back(declaration);
    declaration->SetText(("{ mesh \""+meshPath+"\" anim control \""+clipPath+"\" }").c_str());declaration->Invalidate();
    declaration=const_cast<idDeclModelDef*>(static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name.c_str(),false)));
    Need(s,declaration&&declaration->GetState()==DS_PARSED&&!declaration->UsesSourceAnimation(),"controlled own native MD5 failed to parse");
    resources.ownModels.push_back(declaration->ModelHandle());
    idAnimator actor;actor.RemoveOriginOffset(false);Need(s,actor.SetModel(name.c_str())==declaration->ModelHandle(),"controlled legacy animator setup failed");
    const int animation=actor.GetAnim("control");actor.SetFrame(ANIMCHANNEL_ALL,animation,frames,1000,0);actor.CreateFrame(1000,true);
    int count=0;idJointMat* joints=NULL;actor.GetJoints(&count,&joints);
    Need(s,actor.CurrentAnim(ANIMCHANNEL_ALL)->GetFrameNumber(1000)==frames&&count==1&&joints&&joints[0].ToVec3().x==40000,
         "wide native SetFrame truncated the requested frame or sampled a different legacy pose");++s.legacy;
    actor.SetFrame(ANIMCHANNEL_ALL,animation,32767,1001,0);actor.CreateFrame(1001,true);
    Need(s,gameSoundWorld!=NULL,"legacy save control requires the ordinary native sound world");
    const std::string savePath="fixtures/controlled-own-short-"+tag+".save";resources.ownFiles.push_back(savePath);int recordLength=0;
    {std::unique_ptr<idFile,FileCloser> file(fileSystem->OpenFileWrite(savePath.c_str()));Need(s,file.get()!=NULL,"legacy save-wire fixture open failed");
        idSaveGame save(file.get());actor.CurrentAnim(ANIMCHANNEL_ALL)->Save(&save);
        const int expected=32+4*ANIM_MaxSyncedAnims+6+2;
        Need(s,file->Tell()==expected,"widening runtime frame changed the legacy blend save record length");recordLength=file->Tell();}
    std::unique_ptr<idFile,FileCloser> file(fileSystem->OpenFileRead(savePath.c_str(),false));Need(s,file.get()!=NULL,"legacy save-wire fixture read failed");
    Need(s,file->Seek(32+4*ANIM_MaxSyncedAnims+2,FS_SEEK_SET)==0,"legacy short frame offset seek failed");
    short rawFrame=0;Need(s,file->ReadShort(rawFrame)==2&&rawFrame==32767,"legacy frame save is no longer the original short wire");file->Rewind();
    idRestoreGame restore(file.get());idAnimBlend restored;restored.Restore(&restore,declaration);
    Need(s,file->Tell()==recordLength&&restored.GetFrameNumber(1001)==32767&&restored.AnimNum()==animation&&restored.GetCycleCount()==-1,
         "legacy fixed frame short-wire roundtrip changed behavior");++s.legacy;
}
}
#if defined(_WIN32)
#define WOLF2009_CONTROLLED_QA_EXPORT __declspec(dllexport)
#else
#define WOLF2009_CONTROLLED_QA_EXPORT __attribute__((visibility("default")))
#endif
extern "C" WOLF2009_CONTROLLED_QA_EXPORT int Wolf2009_RunSourceControlledQA(const char* firstAlias,const char* secondAlias,const char* wrongAnimationAlias) {
    Stats s;
    try{
        Resources resources;const auto firstEnvelope=Read(s,firstAlias),secondEnvelope=Read(s,secondAlias);const auto firstOracle=Oracle(firstEnvelope),secondOracle=Oracle(secondEnvelope);
        Need(s,firstEnvelope.binding.modelAlias==secondEnvelope.binding.modelAlias&&firstOracle.Skeleton().joints.size()==secondOracle.Skeleton().joints.size()&&
             firstEnvelope.binding.frames<=1044481&&secondEnvelope.binding.frames<=1044481&&firstOracle.Duration()<=30&&secondOracle.Duration()<=30,
             "controlled QA clips do not fit the bounded same-model profile");
        static unsigned invocation=0;Need(s,invocation<(std::numeric_limits<unsigned>::max)(),"controlled QA namespace exhausted");
        const std::string tag=std::string(firstAlias).substr(std::string(firstAlias).find_last_of('/')+1,16)+"_"+std::to_string(++invocation);
        const std::vector<WolfSourceAnimationSetEntry> entries{{"first",firstAlias},{"second",secondAlias}};
        idStr definitionName,reason;
        const int declarations=declManager->GetNumDecls(DECL_MODELDEF);idStr rejected="must-clear";
        const auto rejectSet=[&](const std::vector<WolfSourceAnimationSetEntry>& bad){
            rejected="must-clear";
            Need(s,!Wolf2009_CreateSourceAnimationSet(bad,NULL,rejected,reason)&&rejected.IsEmpty()&&!reason.IsEmpty()&&
                 declManager->GetNumDecls(DECL_MODELDEF)==declarations,"invalid animation set changed native declarations");++s.refusals;
        };
        rejectSet({});
        rejectSet({{"first",firstAlias},{"first",secondAlias}});
        rejectSet({{"first\"\nmesh injected",firstAlias}});
        rejectSet({{"first","unsafe\"\nmesh injected"}});
        rejectSet(std::vector<WolfSourceAnimationSetEntry>(65,WolfSourceAnimationSetEntry{"first",firstAlias}));
        Need(s,!Wolf2009_CreateSourceAnimationSet({{"first",firstAlias},{"second",wrongAnimationAlias}},NULL,rejected,reason)&&rejected.IsEmpty()&&!reason.IsEmpty()&&
             declManager->GetNumDecls(DECL_MODELDEF)==declarations,"mixed actual-model animation set was admitted or created a declaration");
        Need(s,Wolf2009_CreateSourceAnimationSet(entries,NULL,definitionName,reason)&&reason.IsEmpty(),"qualified native source animation set factory failed");
        resources.set=definitionName.c_str();auto* definition=const_cast<idDeclModelDef*>(Wolf2009_FindRegisteredSourceAnimationSet(definitionName));
        Need(s,definition&&definition->NumAnims()==3,"native set did not contain both exact clip entries");resources.definitions.push_back(definition);
        idStr repeated;Need(s,Wolf2009_CreateSourceAnimationSet(entries,NULL,repeated,reason)&&repeated==definitionName,"identical set registration is not idempotent");
        auto* wrong=animationLib.GetAnim(wrongAnimationAlias);Need(s,wrong&&wrong->IsSourceAnimationV2()&&firstEnvelope.binding.modelAlias!=wrong->SourceModelAlias(),"mixed-model control alias is not qualified and distinct");
        WolfSourceControlledRequest request;request.name=("controlled_a_"+tag).c_str();request.modelDefinition=definitionName;request.initialClip="first";
        request.origin.Set(133.125f,-79.5f,21.75f);request.axis=idAngles(0,31,0).ToMat3();
        const int initialNative=gameLocal.spawnedEntities.Num(),initialLive=Wolf2009_LiveSourceControlledEntities(),initialFree=gameLocal.firstFreeIndex;
        const auto spawnIds=wire::Fingerprint(reinterpret_cast<const unsigned char*>(gameLocal.spawnIds),sizeof(gameLocal.spawnIds));
        const auto badSpawn=[&](const WolfSourceControlledRequest& bad){idAnimatedEntity* entity=NULL;Need(s,!Wolf2009_SpawnSourceControlled(bad,entity,reason)&&!entity&&!reason.IsEmpty(),"invalid controlled initial request was admitted");
            Need(s,gameLocal.spawnedEntities.Num()==initialNative&&Wolf2009_LiveSourceControlledEntities()==initialLive&&gameLocal.firstFreeIndex==initialFree&&
                 wire::Fingerprint(reinterpret_cast<const unsigned char*>(gameLocal.spawnIds),sizeof(gameLocal.spawnIds))==spawnIds,"controlled initial refusal registered a native entity");++s.refusals;};
        badSpawn(request);request.policy=WolfSourceControlled_NativeMillisecondsHoldLastV2;
        {auto bad=request;bad.initialSourceFrame=int(firstEnvelope.binding.frames);badSpawn(bad);}
        {auto bad=request;bad.initialClip="absent";badSpawn(bad);}
        {auto bad=request;bad.origin.x=(std::numeric_limits<float>::quiet_NaN)();badSpawn(bad);}
        {auto bad=request;bad.axis[0]*=2;badSpawn(bad);}
        idAnimatedEntity* first=NULL;Need(s,Wolf2009_SpawnSourceControlled(request,first,reason)&&first,"controlled native initial hold spawn failed");resources.Own(first);
        auto secondRequest=request;secondRequest.name=("controlled_b_"+tag).c_str();secondRequest.initialClip="second";secondRequest.initialSourceFrame=int(secondEnvelope.binding.frames)-1;
        secondRequest.origin.Set(-17.25f,42.5f,8);idAnimatedEntity* second=NULL;
        Need(s,Wolf2009_SpawnSourceControlled(secondRequest,second,reason)&&second,"controlled native second instance spawn failed");resources.Own(second);
        Need(s,State(s,first).commandSerial==1&&State(s,second).commandSerial==1&&first->GetAnimator()->ModelHandle()==second->GetAnimator()->ModelHandle()&&
             first->GetPhysics()->GetNumClipModels()==0&&second->GetPhysics()->GetNumClipModels()==0&&!first->GetAnimator()->RemoveOrigin(),"controlled ownership/initial serial/root policy differs");
        Frames(s,1);ComparePose(s,first,firstOracle.SamplePoseFrame(0));ComparePose(s,second,secondOracle.SamplePoseFrame(secondEnvelope.binding.frames-1));
        Snapshot(s,first,resources.firstSnapshot);Snapshot(s,second,resources.secondSnapshot);
        const auto secondPalette=Palette(second);const auto stateBefore=State(s,first);const int middle=int(firstEnvelope.binding.frames)/2;
        // All three accepted commands have exactly the same gameLocal.time.
        Need(s,Wolf2009_SourceControlledHoldFrame(first,"first",int(firstEnvelope.binding.frames)-1,0,reason)&&
             Wolf2009_SourceControlledPlayOnce(first,"second",0,reason)&&Wolf2009_SourceControlledHoldFrame(first,"first",middle,0,reason),"controlled same-ms command replacement failed");
        Need(s,State(s,first).commandSerial==stateBefore.commandSerial+3,"controlled command serial missed same-ms replacements");
        Frames(s,1);ComparePose(s,first,firstOracle.SamplePoseFrame(middle));Need(s,Palette(second)==secondPalette,"controlled commands changed another entity's palette");Snapshot(s,first,resources.firstSnapshot);
        if(firstEnvelope.binding.frames>255){Need(s,Wolf2009_SourceControlledHoldFrame(first,"first",255,0,reason),"controlled source block-end frame hold failed");Frames(s,1);ComparePose(s,first,firstOracle.SamplePoseFrame(255));}
        Refusals(s,first,int(firstEnvelope.binding.frames));
        Need(s,Wolf2009_SourceControlledPlayOnce(first,"first",0,reason),"controlled first one-shot failed");Frames(s,2);
        auto state=State(s,first);ComparePose(s,first,firstOracle.SamplePose((gameLocal.time-state.commandTimeMs)*.001));
        Need(s,gameLocal.msec>0&&secondEnvelope.binding.duration*1000>=6*gameLocal.msec,"controlled crossfade QA clip is too short for native ticks");
        const int blend=(std::min)(128,int(secondEnvelope.binding.duration*1000)/3);
        Need(s,blend>2*gameLocal.msec&&Wolf2009_SourceControlledPlayOnce(first,"second",blend,reason),"controlled transition to the second qualified clip failed");
        Frames(s,1);Crossfade(s,first,firstOracle,secondOracle);Snapshot(s,first,resources.firstSnapshot);
        Need(s,Palette(second)==secondPalette&&resources.firstSnapshot!=resources.secondSnapshot,"controlled crossfade reused another entity's cached snapshot");
        state=State(s,first);const unsigned playSerial=state.commandSerial;const int deadline=state.endTimeMs;
        Frames(s,(deadline-gameLocal.time)/gameLocal.msec+3);
        const auto final=State(s,first);Need(s,gameLocal.time>deadline&&final.phase==WolfSourceControlled_Held&&final.clip=="second"&&
             final.sourceFrame==int(secondEnvelope.binding.frames)-1&&final.commandSerial==playSerial&&
             first->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL)->GetFrameNumber(gameLocal.time)==int(secondEnvelope.binding.frames)&&
             first->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL)->GetCycleCount()==1,
             "controlled ceil-end did not become the exact last-frame native hold");
        ComparePose(s,first,secondOracle.SamplePoseFrame(secondEnvelope.binding.frames-1));Snapshot(s,first,resources.firstSnapshot);
        Need(s,(first->thinkFlags&TH_ANIMATE)&&first->GetAnimator()->IsAnimating(gameLocal.time),"controlled held pose no longer follows documented native fixed-frame activity");
        Need(s,first->GetRenderEntity()->origin.Compare(request.origin,.0001f)&&first->GetRenderEntity()->axis.Compare(request.axis,.0001f)&&
             first->GetPhysics()->GetOrigin().Compare(request.origin,.0001f),"controlled source visual root moved the actual entity or applied placement twice");
        // Detect changed declaration metadata before channel mutation, then
        // reparse the exact original text without retaining an unsafe idAnim*.
        std::vector<char> text(std::size_t(definition->GetTextLength())+1,0);definition->GetText(text.data());
        const auto savedState=State(s,first);const auto savedPalette=Palette(first);
        std::string changed(text.data());const auto nameAt=changed.find(" anim first ");
        Need(s,nameAt!=std::string::npos,"controlled QA could not find its exact logical-key declaration");
        changed.replace(nameAt+6,5,"third"); // Same length exercises full text, not just its length.
        definition->SetText(changed.c_str());definition->Invalidate();
        const auto savedChannels=Channels(first);
        Need(s,!Wolf2009_FindRegisteredSourceAnimationSet(definitionName)&&definition->GetState()==DS_UNPARSED,
             "changed-set discovery parsed foreign text before refusal");
        rejected="must-clear";
        Need(s,!Wolf2009_CreateSourceAnimationSet(entries,NULL,rejected,reason)&&rejected.IsEmpty()&&!reason.IsEmpty()&&
             definition->GetState()==DS_UNPARSED&&Channels(first)==savedChannels&&Palette(first)==savedPalette,
             "changed-set registration reparsed/mutated the live native declaration");
        auto changedRequest=request;changedRequest.name=("controlled_changed_"+tag).c_str();idAnimatedEntity* changedEntity=NULL;
        const int changedCount=gameLocal.spawnedEntities.Num();
        Need(s,!Wolf2009_SpawnSourceControlled(changedRequest,changedEntity,reason)&&!changedEntity&&
             definition->GetState()==DS_UNPARSED&&gameLocal.spawnedEntities.Num()==changedCount&&Channels(first)==savedChannels&&Palette(first)==savedPalette,
             "changed-set spawn parsed foreign text or affected the live actor");
        Need(s,!Wolf2009_SourceControlledPlayOnce(first,"first",0,reason)&&SameState(savedState,State(s,first))&&Channels(first)==savedChannels&&Palette(first)==savedPalette,
             "changed native set was accepted or mutated the controlled entity");
        definition->SetText(text.data());definition->Invalidate();Need(s,Wolf2009_FindRegisteredSourceAnimationSet(definitionName)==definition,"exact restored controlled set did not reparse");
        Need(s,Wolf2009_ForgetSourceAnimationSet(definitionName)&&!Wolf2009_FindRegisteredSourceAnimationSet(definitionName),"controlled set discovery removal failed");
        const int beforeForgetSpawn=gameLocal.spawnedEntities.Num();auto forgotten=request;forgotten.name=("controlled_missing_"+tag).c_str();idAnimatedEntity* rejectedEntity=NULL;
        Need(s,!Wolf2009_SpawnSourceControlled(forgotten,rejectedEntity,reason)&&!rejectedEntity&&gameLocal.spawnedEntities.Num()==beforeForgetSpawn,"forgotten set remained discoverable for new entities");
        Need(s,Wolf2009_SourceControlledHoldFrame(first,"first",0,0,reason),"forget invalidated an existing actor's owned set metadata/refs");
        Frames(s,1);ComparePose(s,first,firstOracle.SamplePoseFrame(0));ComparePose(s,second,secondOracle.SamplePoseFrame(secondEnvelope.binding.frames-1));
        Need(s,Wolf2009_SourceControlledPlayOnce(first,"second",0,reason),"forgotten retained controller cannot play its owned second clip");
        state=State(s,first);Frames(s,(state.endTimeMs-gameLocal.time)/gameLocal.msec+3);ComparePose(s,first,secondOracle.SamplePoseFrame(secondEnvelope.binding.frames-1));
        const int declarationsBeforeReactivation=declManager->GetNumDecls(DECL_MODELDEF);
        Need(s,Wolf2009_CreateSourceAnimationSet(entries,NULL,repeated,reason)&&repeated==definitionName&&
             Wolf2009_FindRegisteredSourceAnimationSet(repeated)==definition&&declManager->GetNumDecls(DECL_MODELDEF)==declarationsBeforeReactivation,
             "unchanged forgotten set could not be registered again on the same native declaration");
        Need(s,Wolf2009_ForgetSourceAnimationSet(definitionName)&&!Wolf2009_ForgetSourceAnimationSet(definitionName),
             "reactivated set discovery removal was not scoped/idempotent");
        LegacyWide(s,resources,tag);
        const int handle=first->GetModelDefHandle();idEntityPtr<idEntity> generation;generation=first;delete first;
        Need(s,!generation.GetEntity()&&!gameRenderWorld->GetRenderEntity(handle),"controlled deletion retained the generation/render-world entry");
        delete second;Need(s,Wolf2009_LiveSourceControlledEntities()==initialLive&&gameLocal.spawnedEntities.Num()==initialNative,"controlled entity/ref lifecycle leaked native actors");
        printf("WOLF2009_NATIVE_SOURCE_CONTROLLED_QA_OK checks=%zu poses=%zu snapshots=%zu native_frames=%zu refusals=%zu legacy=%zu bones=%u first_frames=%u second_frames=%u max_joint_error=%.9g max_bounds_error=%.9g source_frame_direct=1 hold_last=1 same_ms=1 immutable_set=1 discovery_independent=1 legacy_short_wire=1 wide_legacy_runtime=1 wide_source_runtime=0 source_save_guard_runtime=0 gpu=0\n",
            s.checks,s.poses,s.snapshots,s.nativeFrames,s.refusals,s.legacy,firstEnvelope.binding.bones,firstEnvelope.binding.frames,secondEnvelope.binding.frames,s.jointError,s.boundsError);
        return 1;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_CONTROLLED_FAIL checks=%zu message=%s\n",s.checks,error.what());}
    catch(...){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_CONTROLLED_FAIL checks=%zu message=native-exception\n",s.checks);}return 0;
}

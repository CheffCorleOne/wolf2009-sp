// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceActorBinding.h"
#include "SourceSoundBinding.h"
#include "AnimationWireV2.h"
#include "SourceRigAnimation.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineAnimationSet.h"
#include "SourceDeclarationSetRuntime.h"

namespace {
namespace actor=wolf2009::actor;
namespace wire=wolf2009::animation2;
struct Stats {std::size_t checks=0,refusals=0,poses=0;double error=0;};
void Need(Stats& s,bool value,const char* reason){++s.checks;if(!value)throw std::runtime_error(reason);}
struct Close {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
wire::Envelope Descriptor(Stats& s,const char* alias){
    std::unique_ptr<idFile,Close> file(fileSystem->OpenFileRead(alias,false));Need(s,file.get()!=NULL,"actor QA animation descriptor absent");const int size=file->Length();
    Need(s,size>0&&std::size_t(size)<=wire::MaximumEnvelopeBytes,"actor QA descriptor byte budget");wire::Bytes bytes(static_cast<std::size_t>(size));Need(s,file->Read(bytes.data(),size)==size,"actor QA descriptor short read");
    unsigned char extra=0;Need(s,file->Read(&extra,1)==0,"actor QA descriptor trailing bytes");const auto envelope=wire::Parse(bytes);Need(s,wire::Alias(envelope.binding)==alias,"actor QA descriptor full-key alias mismatch");return envelope;
}
std::string MapPrefix(Stats& s,const char* path){
    Need(s,path!=NULL,"actor QA selected map path absent");std::string value(path);for(char& c:value){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+='a'-'A';}
    const auto marker=value.find("/decl/");Need(s,value.size()<=4096&&value.compare(0,9,"wolf2009/")==0&&marker!=std::string::npos&&marker>9&&
        value.substr(marker+6)=="maps/game/trainyard/trainyard","actor QA requires exact selected trainyard DECL resource");return value.substr(0,marker+1);
}
actor::ResourceSelection Resources(const std::string& prefix){
    actor::ResourceSelection resource;resource.modelResource=prefix+"modl/models/props/locker_foot_01_anim.md5r";resource.meshSkeletonResource=prefix+"skel/maps/game/trainyard/trainyard";
    resource.meshSkeletonName="models/props/locker_foot_01_anim_skel.hkx";resource.geometryProfile=actor::GeometryProfile::WeightedOrRigid64;resource.lodPolicy=actor::LodPolicy::SingleResourceWithoutLodRecords;
    for(const auto* key:{"idle","open","idle_open","close"}){actor::ClipSelection clip;clip.logicalName=key;clip.animationResource=prefix+"hkxa/maps/game/trainyard/trainyard";
        clip.animationSkeletonResource=resource.meshSkeletonResource;clip.clipName=std::string("models/props/locker_foot_01_anim_")+key+".hkx";clip.animationSkeletonName=resource.meshSkeletonName;
        clip.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;resource.clips.push_back(std::move(clip));}return resource;
}
std::string Text(Stats& s,const idDecl* declaration){Need(s,declaration&&declaration->GetState()==DS_PARSED,"actor QA native set unparsed");const int count=declaration->GetTextLength();
    Need(s,count>0&&count<=64*1024,"actor QA native set text budget");std::vector<char> bytes(std::size_t(count)+1,0);declaration->GetText(bytes.data());return std::string(bytes.data(),std::size_t(count));}
struct Channel {
    int animation,start,end,cycle,frame;float rate,weight;
    bool operator==(const Channel& b)const{return animation==b.animation&&start==b.start&&end==b.end&&cycle==b.cycle&&frame==b.frame&&rate==b.rate&&weight==b.weight;}
};
std::vector<Channel> Channels(idAnimator& animator,int now){std::vector<Channel> result;auto* channel=animator.CurrentAnim(ANIMCHANNEL_ALL);
    for(int i=0;i<ANIM_MaxAnimsPerChannel;++i)result.push_back({channel[i].AnimNum(),channel[i].GetStartTime(),channel[i].GetEndTime(),channel[i].GetCycleCount(),channel[i].GetFrameNumber(now),channel[i].GetPlaybackRate(),channel[i].GetWeight(now)});return result;}
template<class T>struct Aligned {T* data;explicit Aligned(std::size_t count):data(static_cast<T*>(Mem_Alloc16(count*sizeof(T)))){}~Aligned(){Mem_Free16(data);}Aligned(const Aligned&)=delete;Aligned& operator=(const Aligned&)=delete;};
void Pose(Stats& s,idAnimator& animator,const wolf2009::SourceRigAnimation& source,int elapsed,int cycles,int now){
    const auto clock=actor::CycleAtMilliseconds(elapsed,cycles,source.Duration());const auto expected=source.SamplePose(clock.seconds);animator.CreateFrame(now+elapsed,true);
    int count=0;idJointMat* actual=NULL;animator.GetJoints(&count,&actual);Need(s,actual&&count==int(expected.model.size()),"actor QA native cycle palette count differs");
    for(int i=0;i<count;++i){const auto& joint=expected.model[std::size_t(i)];idQuat q(float(-joint.rotation[0]),float(-joint.rotation[1]),float(-joint.rotation[2]),float(joint.rotation[3]));
        idJointMat matrix;matrix.SetRotation(q.ToMat3());matrix.SetTranslation(idVec3(float(joint.translation[0]),float(joint.translation[1]),float(joint.translation[2])));
        for(int n=0;n<12;++n){const double error=std::abs(double(actual[i].ToFloatPtr()[n])-matrix.ToFloatPtr()[n]);if(error>s.error)s.error=error;
            Need(s,std::isfinite(error)&&error<=(n%4==3?.0002:.00002),"actor QA native source cycle differs from original-duration sampler");}}
    idBounds bounds;Need(s,animator.GetBounds(now+elapsed,bounds)&&!bounds.IsCleared(),"actor QA native exact cycle bounds absent");++s.poses;
}
struct TemporarySet {
    idStr name;
    ~TemporarySet(){if(name.IsEmpty())return;const auto* decl=declManager->FindDeclWithoutParsing(DECL_MODELDEF,name.c_str(),false);
        if(decl)const_cast<idDeclModelDef*>(static_cast<const idDeclModelDef*>(decl))->FreeData();Wolf2009_ForgetSourceAnimationSet(name.c_str());}
};
void NativeCycles(Stats& s,const WolfSourceActorBinding& binding){
    idStr reason;const auto* definition=Wolf2009_FindRegisteredSourceAnimationSet(binding.NativeModelDefinition());Need(s,definition&&binding.MatchesNative(reason),"actor QA immutable native set unavailable");
    const int idle=definition->GetSpecificAnim("idle"),open=definition->GetSpecificAnim("open");Need(s,idle>0&&open>0,"actor QA exact native clips absent");
    const auto* idleClip=definition->GetAnim(idle);const auto* openClip=definition->GetAnim(open);
    Need(s,!idleClip->HasFrameCommands()&&openClip->HasFrameCommands()&&Wolf2009_IsQualifiedSourceActorAnimation(definition,idleClip)&&
        Wolf2009_IsQualifiedSourceFrameSound(definition,openClip),"actor QA actor/frame-sound profile not qualified");
    const frameCommand_t* command=NULL;Need(s,openClip->FindFrameForFrameCommand(FC_SOUND,&command)==0&&command&&command->string&&*command->string=="snd_open","actor QA authored native frame1 sound was dropped");
    const auto descriptor=Descriptor(s,binding.FindClip("idle")->animationAlias.c_str());wolf2009::SourceRigRequest request;request.clipName=descriptor.binding.clipName;request.skeletonName=descriptor.binding.animationSkeletonName;
    request.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;const auto oracle=wolf2009::SourceRigAnimation::Load(descriptor.animation,descriptor.skeleton,request);
    Need(s,gameLocal.time>=0&&gameLocal.time<=INT_MAX-10000,"actor QA native clock horizon");const int now=gameLocal.time+17;
    idAnimator animator;animator.RemoveOriginOffset(false);Need(s,animator.SetModel(binding.NativeModelDefinition())==definition->ModelHandle(),"actor QA native animator model did not bind");
    animator.CycleAnim(ANIMCHANNEL_ALL,idle,now,0);animator.CurrentAnim(ANIMCHANNEL_ALL)->SetCycleCount(2);
    Need(s,animator.CurrentAnim(ANIMCHANNEL_ALL)->GetCycleCount()==2&&animator.CurrentAnim(ANIMCHANNEL_ALL)->GetEndTime()==now+int(std::ceil(oracle.Duration()*2000.0)),"actor QA finite source cycles use rounded/native24Hz duration");
    for(int time:{0,17,42,83,84})Pose(s,animator,oracle,time,2,now);
    animator.CycleAnim(ANIMCHANNEL_ALL,idle,now,0);animator.CurrentAnim(ANIMCHANNEL_ALL)->SetCycleCount(1073741824);
    Need(s,animator.CurrentAnim(ANIMCHANNEL_ALL)->GetCycleCount()==1073741824&&animator.CurrentAnim(ANIMCHANNEL_ALL)->GetEndTime()==-1,"actor QA source finite huge count overflowed or changed to infinite");
    for(int time:{1,42,137,1001})Pose(s,animator,oracle,time,1073741824,now);
    {
        const auto* animation=idleClip->MD5Anim(0);const int bones=animation->NumJoints();std::vector<int> indices;for(int i=0;i<bones;++i)indices.push_back(i);
        Aligned<idJointQuat> timed{std::size_t(bones)},interpolated{std::size_t(bones)};frameBlend_t frame;
        animation->ConvertTimeToFrame(137,1073741824,frame);Need(s,frame.cycleCount>=2,"actor QA native frame conversion lost completed source cycles");
        Need(s,animation->GetPoseAtTime(137,1073741824,timed.data,indices.data(),bones),"actor QA native timed cycle path refused");
        animation->GetInterpolatedFrame(frame,interpolated.data,indices.data(),bones);
        for(int i=0;i<bones;++i){for(int axis=0;axis<3;++axis)Need(s,std::abs(timed.data[i].t[axis]-interpolated.data[i].t[axis])<=.0002f,"actor QA native interpolated cycle translation differs");
            for(int component=0;component<4;++component)Need(s,std::abs(timed.data[i].q[component]-interpolated.data[i].q[component])<=.00002f,"actor QA native interpolated cycle quaternion differs");}
    }
    auto before=Channels(animator,now);animator.CycleAnim(ANIMCHANNEL_ALL,open,now,0);Need(s,Channels(animator,now)==before,"actor QA command-bearing Cycle mutated native channels");++s.refusals;
    animator.SetFrame(ANIMCHANNEL_ALL,open,1,now,0);Need(s,animator.CurrentAnim(ANIMCHANNEL_ALL)->AnimNum()==open&&animator.CurrentAnim(ANIMCHANNEL_ALL)->GetFrameNumber(now)==1,"actor QA qualified sound clip cannot be held without dispatch");
    animator.PlayAnim(ANIMCHANNEL_ALL,open,now,0);before=Channels(animator,now);animator.CurrentAnim(ANIMCHANNEL_ALL)->SetCycleCount(2);
    Need(s,Channels(animator,now)==before,"actor QA sound clip finite-cycle refusal mutated channels");++s.refusals;
    // The old factory deliberately keeps its NoCommands/cycle refusal policy.
    TemporarySet old;WolfSourceAnimationSetEntry entry;entry.logicalName="oldIdle";entry.animationAlias=binding.FindClip("idle")->animationAlias;
    Need(s,Wolf2009_CreateSourceAnimationSet({entry},NULL,old.name,reason),"actor QA ordinary NoCommands control creation failed");
    {idAnimator legacy;legacy.RemoveOriginOffset(false);Need(s,legacy.SetModel(old.name)!=NULL,"actor QA ordinary control model absent");const int number=legacy.ModelDef()->GetSpecificAnim("oldIdle");
        legacy.PlayAnim(ANIMCHANNEL_ALL,number,now,0);const auto snapshot=Channels(legacy,now);legacy.CycleAnim(ANIMCHANNEL_ALL,number,now,0);Need(s,Channels(legacy,now)==snapshot,"actor profile broadened the old NoCommands Cycle policy");++s.refusals;
        legacy.CurrentAnim(ANIMCHANNEL_ALL)->SetCycleCount(2);Need(s,Channels(legacy,now)==snapshot,"actor profile broadened old NoCommands cycle-count policy");++s.refusals;}
    entry.frameOneSoundKey="snd_open";const int count=declManager->GetNumDecls(DECL_MODELDEF);idStr refused="must-clear";
    Need(s,!Wolf2009_CreateSourceAnimationSet({entry},NULL,refused,reason)&&refused.IsEmpty()&&!reason.IsEmpty()&&declManager->GetNumDecls(DECL_MODELDEF)==count,"ordinary factory accepted authored commands or had decl effects");++s.refusals;
}
void Poison(Stats& s,const WolfSourceActorBinding& binding){
    const auto* current=Wolf2009_FindRegisteredSourceAnimationSet(binding.NativeModelDefinition());Need(s,current!=NULL,"actor QA poisoning target absent");
    const auto saved=Text(s,current);std::string changed=saved;const auto at=changed.find(" anim open ");Need(s,at!=std::string::npos,"actor QA source logical key not in native set");changed[at+9]='m';
    auto* declaration=const_cast<idDeclModelDef*>(current);declaration->SetText(changed.c_str());declaration->Invalidate();
    idStr reason;const bool refused=!binding.MatchesNative(reason)&&!reason.IsEmpty()&&declaration->GetState()==DS_UNPARSED&&
        !Wolf2009_FindRegisteredSourceAnimationSet(binding.NativeModelDefinition())&&declaration->GetState()==DS_UNPARSED;
    // Restore even when the refusal assertion fails: callers still own source
    // refs and may bind the unchanged original set after this helper returns.
    declaration->SetText(saved.c_str());declaration->Invalidate();const auto* restored=Wolf2009_FindRegisteredSourceAnimationSet(binding.NativeModelDefinition());
    Need(s,refused&&restored==current&&binding.MatchesNative(reason),"actor QA poison refusal reparsed foreign text or did not recover exact original");++s.refusals;
}
}

std::shared_ptr<const WolfSourceActorBinding> Wolf2009_CheckSourceActorBindingQA(const WolfSourceDeclarationSet& set,const char* exactMapDeclPath){
    Stats s;const int time=gameLocal.time,entities=gameLocal.num_entities;
    try{
        const auto prefix=MapPrefix(s,exactMapDeclPath);const auto plan=actor::Prepare(set.Definitions(),set.Namespace(),"container_locker_foot_01","",wolf2009::SourceDefinitionContext(),Resources(prefix));
        Need(s,plan.ClipCount()==4&&plan.Model().references.size()==6&&plan.SelectedInputs().size()==7&&plan.Entity().Find("def_afcore")&&plan.Entity().Find("giveDelay"),"actor QA lost selected source model/physical/loot facts");
        std::vector<WolfSourceActorSoundBinding> sounds;idStr reason;
        for(const auto* action:{"open","close"}){const auto& command=plan.FindClip(action)->request.frameSounds.at(0);WolfSourceSoundRequest request;request.sourceSound=command.sourceSound;
            request.sampleResource=prefix+"snds/sound/objects/footlocker/footlocker_"+action+".mp3";request.mixPolicy=WolfSourceSoundMix_NativeWorldUnitGainV1;
            auto owner=Wolf2009_BindSourceSound(set.Namespace(),request,reason);Need(s,owner&&reason.IsEmpty(),reason.IsEmpty()?"actor QA real source sound failed":reason.c_str());
            WolfSourceActorSoundBinding sound;sound.sourceKey=command.soundKey;sound.sourceSound=command.sourceSound;sound.nativeSoundAlias=owner->NativeAlias();sound.owner=owner;sounds.push_back(std::move(sound));}
        const int defs=declManager->GetNumDecls(DECL_MODELDEF);
        Need(s,!Wolf2009_BindSourceActor(plan,{},reason)&&!reason.IsEmpty()&&declManager->GetNumDecls(DECL_MODELDEF)==defs,"actor QA silently dropped authored sound commands");++s.refusals;
        auto invalid=sounds;invalid[0].owner.reset();Need(s,!Wolf2009_BindSourceActor(plan,invalid,reason)&&!reason.IsEmpty()&&declManager->GetNumDecls(DECL_MODELDEF)==defs,"actor QA unowned native sound accepted");++s.refusals;
        invalid=sounds;invalid[0].sourceSound="other";Need(s,!Wolf2009_BindSourceActor(plan,invalid,reason)&&!reason.IsEmpty()&&declManager->GetNumDecls(DECL_MODELDEF)==defs,"actor QA source sound substitution accepted");++s.refusals;
        {
            // Same SFX name/sample/native shader, but a distinct explicitly
            // selected load unit. A sound-name comparison alone cannot attest
            // that this owner belongs to the actor's selected declaration set.
            const auto& expected=plan.FindClip("open")->soundDeclarations.at(0);wolf2009::CompiledDeclarationCatalog otherCatalog;
            auto identity=*expected.source;identity.loadIdentity+=".other-sfx-owner";otherCatalog.AddDocument(*expected.document,identity);
            wolf2009::CompiledDeclarationNamespace otherNames(wolf2009::CompiledNamespacePolicy::OrdinaryTextOwnersV1);
            otherNames.ApplySection(otherCatalog,0,0,wolf2009::CompiledNamespaceReaderMode::Ordinary);otherNames.ApplySection(otherCatalog,0,1,wolf2009::CompiledNamespaceReaderMode::Ordinary);
            WolfSourceSoundRequest request;request.sourceSound=sounds[0].sourceSound;request.sampleResource=prefix+"snds/sound/objects/footlocker/footlocker_open.mp3";request.mixPolicy=WolfSourceSoundMix_NativeWorldUnitGainV1;
            auto owner=Wolf2009_BindSourceSound(otherNames,request,reason);Need(s,owner&&owner->MatchesNative(reason)&&owner->Source().name==sounds[0].sourceSound&&
                std::string(owner->NativeAlias())==sounds[0].nativeSoundAlias,"actor QA different-namespace sound control is not qualified/independent");
            invalid=sounds;invalid[0].owner=owner;Need(s,!Wolf2009_BindSourceActor(plan,invalid,reason)&&!reason.IsEmpty()&&declManager->GetNumDecls(DECL_MODELDEF)==defs,
                "actor QA accepted a same-name sound owner from another selected source namespace");++s.refusals;
        }
        auto binding=Wolf2009_BindSourceActor(plan,sounds,reason);Need(s,binding&&reason.IsEmpty(),reason.IsEmpty()?"actor QA real source binding failed":reason.c_str());
        auto repeated=Wolf2009_BindSourceActor(plan,sounds,reason);Need(s,repeated&&repeated->NativeModelDefinition()==std::string(binding->NativeModelDefinition())&&
            repeated->NativeModel()==std::string(binding->NativeModel())&&repeated->CapabilityName()==std::string(binding->CapabilityName()),"actor QA immutable request was not idempotent");repeated.reset();
        for(std::size_t i=0;i<binding->ClipCount();++i){const auto& clip=binding->Clip(i);Need(s,clip.frames>=2&&clip.lengthMs>=1&&binding->FindClip(clip.logicalName.c_str())==&clip,"actor QA exact clip metadata/index differs");}
        NativeCycles(s,*binding);Poison(s,*binding);
        Need(s,gameLocal.time==time&&gameLocal.num_entities==entities,"actor QA resource binding mutated world time/entities");
        printf("WOLF2009_NATIVE_SOURCE_ACTOR_BINDING_QA_OK checks=%zu refusals=%zu poses=%zu clips=4 commands=2 source_refs=6 exact_sources=4 native_model_manager=1 native_animator_cycles=1 finite_horizon_count=1073741824 sound_profile=frame1 native_entities=0 max_joint_error=%.9g gpu=0\n",s.checks,s.refusals,s.poses,s.error);
        return binding;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_ACTOR_BINDING_FAIL checks=%zu message=%s\n",s.checks,error.what());throw;}
}

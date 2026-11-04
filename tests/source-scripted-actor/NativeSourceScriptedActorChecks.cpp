// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in actual game-module checks; no private source assets or mocked ACK.
#include "NativeSourceScriptedActor.h"
#include "AnimationWireV2.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceControlledEntity.h"
#include "NativeSourceAnimatedDoor.h"
namespace {
unsigned checks=0,refusals=0,frames=0;
void Need(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void NeedReason(bool value,const idStr& reason,const char* fallback){Need(value,reason.IsEmpty()?fallback:reason.c_str());}
struct Lease {
    WolfSourceActorStartBinding binding;
    explicit Lease(const WolfSourceScriptedActor& owner){idStr reason;NeedReason(owner.StartBinding(binding,reason),reason,"Actor start binding failed");
        Need(binding.context&&binding.retain&&binding.release&&binding.preflight&&binding.query,"Scripted actor authority descriptor is incomplete");binding.retain(binding.context);}
    ~Lease(){if(binding.context)binding.release(binding.context);}
    void Release(){const auto copy=binding;binding=WolfSourceActorStartBinding();copy.release(copy.context);}
    WolfSourceActorStartEvidence Read()const{WolfSourceActorStartEvidence out;idStr reason;NeedReason(binding.preflight(binding.context,reason)&&binding.query(binding.context,out,reason),reason,"Actor authority query failed");return out;}
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
};
bool Same(const wolf2009::TrainDoorCommand& a,const wolf2009::TrainDoorCommand& b){return a.kind==b.kind&&a.ownerEntityName==b.ownerEntityName&&a.resourceName==b.resourceName&&a.targetEntityName==b.targetEntityName&&a.levelGeneration==b.levelGeneration&&a.eventId==b.eventId&&a.sourceNodeId==b.sourceNodeId;}
wolf2009::TrainDoorCommand StartCommand(const WolfSourceScriptedActorRequest& request){wolf2009::TrainDoorCommand value;
    value.kind=wolf2009::TrainDoorCommand::StartActorAnimation;value.ownerEntityName=request.signal.ownerEntityName;value.resourceName=request.signal.actorAnimation;
    value.levelGeneration=request.levelGeneration;value.eventId=117;value.sourceNodeId=request.signal.actorStartNodeId;return value;}
void NoAck(const WolfSourceActorStartEvidence& proof,bool spawned){
    Need(!proof.committed&&!proof.actualPoseEvaluated&&proof.commandSerial==0&&proof.committedTimeMs==-1&&Same(proof.command,wolf2009::TrainDoorCommand()),"Inactive actor fabricated or retained a start ACK");
    Need(spawned?(proof.nativeActor&&proof.nativeSpawnId!=0):(!proof.nativeActor&&!proof.nativeSpawnId),"Inactive authority has the wrong native identity phase");}
struct Channel {int number,start,end,cycle,frame;float weight,rate;
    bool operator==(const Channel& other)const{return number==other.number&&start==other.start&&end==other.end&&cycle==other.cycle&&frame==other.frame&&weight==other.weight&&rate==other.rate;}};
std::vector<Channel> Channels(idAnimatedEntity& entity){std::vector<Channel> out;
    for(int channel=0;channel<ANIM_NumAnimChannels;++channel){const auto* native=entity.GetAnimator()->CurrentAnim(channel);
        for(int blend=0;blend<ANIM_MaxAnimsPerChannel;++blend){const auto& a=native[blend];out.push_back(Channel{a.AnimNum(),a.GetStartTime(),a.GetEndTime(),a.GetCycleCount(),a.GetFrameNumber(gameLocal.time),a.GetWeight(gameLocal.time),a.GetPlaybackRate()});}}return out;}
WolfSourceControlledState Controlled(idAnimatedEntity* entity){WolfSourceControlledState out;idStr reason;NeedReason(Wolf2009_SourceControlledState(entity,out,reason),reason,"Actor native controller query failed");return out;}
bool Same(const WolfSourceControlledState& a,const WolfSourceControlledState& b){return a.phase==b.phase&&a.clip==b.clip&&a.sourceFrame==b.sourceFrame&&a.commandTimeMs==b.commandTimeMs&&a.endTimeMs==b.endTimeMs&&a.blendEndTimeMs==b.blendEndTimeMs&&a.commandSerial==b.commandSerial;}
std::uint64_t Palette(idAnimatedEntity& entity){int count=0;idJointMat* actual=NULL;entity.GetAnimator()->GetJoints(&count,&actual);Need(count>0&&actual,"Actual actor palette is absent");
    return wolf2009::animation2::Fingerprint(reinterpret_cast<const unsigned char*>(actual),std::size_t(count)*sizeof(idJointMat));}
void Refused(WolfSourceScriptedActor& owner,const wolf2009::TrainDoorCommand& bad){auto* native=owner.NativeEntity();const auto before=Controlled(native);const auto channels=Channels(*native);const auto palette=Palette(*native);
    WolfSourceScriptedActorStartAck ack;idStr reason;Need(!owner.Start(bad,ack,reason)&&!reason.IsEmpty()&&!ack.committed,"Scripted actor accepted an invalid start command");
    Need(Same(before,Controlled(native))&&channels==Channels(*native)&&palette==Palette(*native),"Rejected actor start mutated actual native channels/state/palette");++refusals;}
void Step(){Need(frames<4096&&gameLocal.msec>0,"Scripted actor native frame budget");usercmd_t commands[MAX_CLIENTS];std::memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);++frames;}
struct NativeOwner {idEntityPtr<idAnimatedEntity> entity;~NativeOwner(){if(entity.GetEntity())delete entity.GetEntity();}};
}

// Run only in a dedicated active local-SP QA game with the exact authored
// actor name unoccupied. Caller owns the selected namespace/mounts throughout.
bool Wolf2009_CheckNativeSourceScriptedActorQA(const WolfSourceDeclarationSet& set,const WolfSourceScriptedActorRequest& request){try{
    checks=refusals=frames=0;const int ownersBefore=Wolf2009_LiveSourceScriptedActors(),nativeBefore=Wolf2009_LiveSourceControlledEntities();idStr reason;
    auto owner=Wolf2009_PrepareSourceScriptedActor(set,request,reason);NeedReason(bool(owner),reason,"Actor preparation failed");Lease authority(*owner);const auto initial=authority.Read();NoAck(initial,false);
    Need(initial.sourceName==request.signal.ownerEntityName&&initial.levelGeneration==request.levelGeneration&&!owner->NativeEntity()&&Wolf2009_LiveSourceControlledEntities()==nativeBefore,"Actor preparation published native playback");
    WolfSourceScriptedActorState state;Need(owner->Query(state,reason)&&state.phase==WolfSourceScriptedActorPhase::Prepared&&state.startCommits==0,"Prepared actor phase differs");
    Need(state.declaredSkeleton==state.selectedMeshSkeleton&&state.authoredSkin.size()>0&&state.sharedGeometryUnchanged&&!state.retailAiImplemented&&!state.rootMotionExtractionImplemented&&!state.frameCommandsImplemented&&!state.headAttachmentsImplemented,"Actor raw appearance/profile facts were lost or overclaimed");
    const auto command=StartCommand(request);WolfSourceScriptedActorStartAck accepted;
    Need(!owner->Start(command,accepted,reason)&&!accepted.committed&&!owner->NativeEntity(),"Unspawned actor acknowledged a start");NoAck(authority.Read(),false);
    NeedReason(owner->SpawnInactive(reason),reason,"Actor inactive spawn failed");const auto inactive=authority.Read();NoAck(inactive,true);idEntityPtr<idAnimatedEntity> original;original=owner->NativeEntity();
    Need(unsigned(original.GetSpawnId())==inactive.nativeSpawnId&&Wolf2009_LiveSourceControlledEntities()==nativeBefore+1,"Actor inactive native identity/count differs");
    auto* animator=owner->NativeEntity()->GetAnimator();animator->CreateFrame(gameLocal.time,true);const auto held=Controlled(owner->NativeEntity());
    Need(held.phase==WolfSourceControlled_Held&&held.sourceFrame==request.initialSourceFrame&&owner->Query(state,reason)&&state.phase==WolfSourceScriptedActorPhase::Held,"Actor inactive source frame differs");
    {auto bad=command;bad.kind=wolf2009::TrainDoorCommand::ActivateTarget;Refused(*owner,bad);}
    {auto bad=command;bad.levelGeneration=0;Refused(*owner,bad);}
    {auto bad=command;bad.eventId=0;Refused(*owner,bad);}
    {auto bad=command;bad.sourceNodeId=0;Refused(*owner,bad);}
    {auto bad=command;bad.ownerEntityName+="_replacement";Refused(*owner,bad);}
    {auto bad=command;bad.resourceName+="_unknown";Refused(*owner,bad);}
    {auto bad=command;bad.targetEntityName=request.signal.targetEntityName;Refused(*owner,bad);}
    NeedReason(owner->Start(command,accepted,reason)&&accepted.committed&&!accepted.duplicate&&accepted.actualPoseEvaluated&&Same(accepted.command,command),reason,"Actor native start/ACK failed");
    const auto proof=authority.Read();const auto playing=Controlled(owner->NativeEntity());const auto* channel=animator->CurrentAnim(ANIMCHANNEL_ALL);
    Need(proof.committed&&proof.actualPoseEvaluated&&Same(proof.command,command)&&proof.nativeActor==original.GetEntity()&&proof.nativeSpawnId==unsigned(original.GetSpawnId())&&proof.commandSerial==playing.commandSerial&&proof.committedTimeMs==gameLocal.time&&
         playing.phase==WolfSourceControlled_Playing&&playing.commandSerial==held.commandSerial+1&&channel->GetStartTime()==gameLocal.time&&channel->GetEndTime()==playing.endTimeMs,"Actor ACK lacks a genuine same-tick native PlayOnce boundary");
    const auto channels=Channels(*owner->NativeEntity());const auto palette=Palette(*owner->NativeEntity());WolfSourceScriptedActorStartAck duplicate;
    Need(owner->Start(command,duplicate,reason)&&duplicate.committed&&duplicate.duplicate&&Same(duplicate.command,command)&&duplicate.commandSerial==accepted.commandSerial&&
         channels==Channels(*owner->NativeEntity())&&palette==Palette(*owner->NativeEntity()),"Duplicate start replayed or reset actual native animation");
    {auto bad=command;++bad.eventId;Refused(*owner,bad);}const auto unchanged=authority.Read();Need(Same(unchanged.command,proof.command)&&unchanged.commandSerial==proof.commandSerial&&unchanged.committedTimeMs==proof.committedTimeMs,"Conflicting start rewrote private owner ACK");
    while(Controlled(owner->NativeEntity()).phase==WolfSourceControlled_Playing){Need(owner->Query(state,reason)&&state.phase==WolfSourceScriptedActorPhase::Playing&&state.completions==0,"Actor completed without an actual native final hold");Step();}
    const auto final=Controlled(owner->NativeEntity());Need(final.phase==WolfSourceControlled_Held&&final.sourceFrame==int(request.expectedFrames)-1&&final.commandSerial==accepted.commandSerial&&final.commandTimeMs==accepted.committedTimeMs,
         "Actual native completion did not preserve the start serial/clock");
    Need(owner->Query(state,reason)&&state.phase==WolfSourceScriptedActorPhase::Completed&&state.completions==1&&state.startCommits==1&&state.duplicateStarts==1&&state.completionObservedTimeMs>=state.nativeEndTimeMs,"Actor completion was not correlated to its native endpoint");
    Need(owner->Query(state,reason)&&state.completions==1,"Actor repeated a completion");const auto finalProof=authority.Read();Need(Same(finalProof.command,proof.command)&&finalProof.commandSerial==proof.commandSerial&&finalProof.committedTimeMs==proof.committedTimeMs,"Completion rewrote immutable start authority");
    owner.reset();Need(Wolf2009_LiveSourceScriptedActors()==ownersBefore+1&&original.GetEntity()&&authority.Read().nativeActor==original.GetEntity(),"Retained door authority lost actor/resource lifetime");authority.Release();
    Need(!original.GetEntity()&&Wolf2009_LiveSourceScriptedActors()==ownersBefore&&Wolf2009_LiveSourceControlledEntities()==nativeBefore,"Last actor authority lease did not remove its original native entity");
    // A same-name/same-model/clip/serial/time clone is not this owner. Do not
    // manufacture an ACK: both the original and clone use real native starts.
    owner=Wolf2009_PrepareSourceScriptedActor(set,request,reason);NeedReason(bool(owner)&&owner->SpawnInactive(reason),reason,"Replacement QA original actor preparation/spawn failed");Lease replaced(*owner);
    NeedReason(owner->Start(command,accepted,reason),reason,"Replacement QA original actor start failed");const auto real=replaced.Read();auto* old=owner->NativeEntity();const auto origin=old->GetPhysics()->GetOrigin();const auto axis=old->GetPhysics()->GetAxis();
    NeedReason(owner->Query(state,reason),reason,"Replacement QA original actor query failed");delete old;Need(!owner->NativeEntity(),"Deleted actor original idEntityPtr rebound");
    WolfSourceControlledRequest clone;clone.name=state.sourceName.c_str();clone.modelDefinition=state.nativeModelDefinition.c_str();clone.initialClip=state.logicalClip.c_str();clone.initialSourceFrame=request.initialSourceFrame;
    clone.origin=origin;clone.axis=axis;clone.policy=WolfSourceControlled_NativeMillisecondsHoldLastV2;idAnimatedEntity* replacement=NULL;NativeOwner replacementOwner;
    NeedReason(Wolf2009_SpawnSourceControlled(clone,replacement,reason)&&replacement,reason,"Replacement QA native clone spawn failed");replacementOwner.entity=replacement;
    NeedReason(Wolf2009_SourceControlledPlayOnce(replacement,state.logicalClip.c_str(),0,reason),reason,"Replacement QA native clone start failed");replacement->GetAnimator()->CreateFrame(gameLocal.time,true);const auto cloned=Controlled(replacement);
    Need(cloned.commandSerial==real.commandSerial&&cloned.commandTimeMs==real.committedTimeMs&&unsigned(replacementOwner.entity.GetSpawnId())!=real.nativeSpawnId,"Replacement test did not reproduce clip/time/serial with a distinct spawn");
    WolfSourceActorStartEvidence rejected;Need(!owner->Preflight(reason)&&!reason.IsEmpty()&&!replaced.binding.query(replaced.binding.context,rejected,reason)&&!reason.IsEmpty()&&!rejected.committed,"Same-name replacement stole original actor authority");++refusals;
    owner.reset();replaced.Release();Need(replacementOwner.entity.GetEntity()==replacement,"Original actor owner deleted a foreign replacement");delete replacement;replacementOwner.entity=NULL;
    Need(Wolf2009_LiveSourceScriptedActors()==ownersBefore&&Wolf2009_LiveSourceControlledEntities()==nativeBefore,"Actor lifecycle QA leaked owned native objects");
    std::printf("WOLF2009_SOURCE_SCRIPTED_ACTOR_QA_OK checks=%u refusals=%u frames=%u actual_start=1 actual_pose=1 actual_final_hold=1 authority_retained=1 replacement_refused=1 ai=0 root_extraction=0 later_brain=0\n",checks,refusals,frames);return true;
}catch(const std::exception& error){std::fprintf(stderr,"WOLF2009_SOURCE_SCRIPTED_ACTOR_QA_FAIL checks=%u reason=%s\n",checks,error.what());return false;}}

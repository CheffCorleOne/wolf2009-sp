// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "NativeSourceAnimatedDoor.h"
#include "NativeSourceAnimatedDoorChecks.h"
#include "SourceControlledEntity.h"
#include "SourceDeclarationSetRuntime.h"

namespace {
struct Stats {unsigned checks=0,frames=0;};
void Need(Stats& stats,bool value,const char* text){++stats.checks;if(!value)throw std::runtime_error(text);}
void Frame(Stats& stats){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);++stats.frames;}
WolfSourceAnimatedDoorState State(Stats& stats,const WolfSourceAnimatedDoor& door){WolfSourceAnimatedDoorState out;idStr reason;
    const bool ok=door.Query(out,reason);Need(stats,ok,reason.IsEmpty()?"Door QA state query failed":reason.c_str());return out;}
struct Channel {int number,start,end,frame,cycle;unsigned serial;WolfSourceControlledPhase phase;std::string clip;};
Channel Snapshot(Stats& stats,idAnimatedEntity* entity){WolfSourceControlledState state;idStr reason;
    const bool ok=Wolf2009_SourceControlledState(entity,state,reason);Need(stats,ok,reason.IsEmpty()?"Door QA controlled state missing":reason.c_str());
    const auto* native=entity->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL);Channel out;out.number=native->AnimNum();out.start=native->GetStartTime();out.end=native->GetEndTime();
    out.frame=native->GetFrameNumber(gameLocal.time);out.cycle=native->GetCycleCount();out.serial=state.commandSerial;out.phase=state.phase;out.clip=state.clip.c_str();return out;}
bool Same(const Channel& a,const Channel& b){return a.number==b.number&&a.start==b.start&&a.end==b.end&&a.frame==b.frame&&a.cycle==b.cycle&&a.serial==b.serial&&a.phase==b.phase&&a.clip==b.clip;}
void Unchanged(Stats& stats,const WolfSourceAnimatedDoor& door,const WolfSourceAnimatedDoorState& before,const Channel& channel){const auto after=State(stats,door);
    Need(stats,before.phase==after.phase&&before.activations==after.activations&&before.completions==after.completions&&before.duplicateActivations==after.duplicateActivations&&
        before.consumedCompletions==after.consumedCompletions&&before.doorCommandSerial==after.doorCommandSerial&&before.actorCommandSerial==after.actorCommandSerial&&before.actorNativeSpawnId==after.actorNativeSpawnId&&
        before.eventId==after.eventId&&before.sourceNodeId==after.sourceNodeId&&before.activationTimeMs==after.activationTimeMs&&before.nativeEndTimeMs==after.nativeEndTimeMs&&
        before.completionTimeMs==after.completionTimeMs,"Door QA refusal changed owned runtime state");Need(stats,Same(channel,Snapshot(stats,door.Entity())),"Door QA refusal changed actual native animator channel");}
void BoundPose(Stats& stats,const WolfSourceAnimatedDoor& door,idEntity* parent){const auto a=wolf2009::ReadEntityPlacement(door.Facts().instance),b=wolf2009::ReadEntityPlacement(door.Facts().parentInstance);
    idVec3 authored(a.origin[0],a.origin[1],a.origin[2]),parentOrigin(b.origin[0],b.origin[1],b.origin[2]);idMat3 axis,parentAxis;
    for(int i=0;i<9;++i){axis[i/3][i%3]=a.rotation[i];parentAxis[i/3][i%3]=b.rotation[i];}const idVec3 local=(authored-parentOrigin)*parentAxis.Transpose();const idMat3 localAxis=axis*parentAxis.Transpose();
    const auto* render=parent->GetRenderEntity();const idVec3 expected=render->origin+local*render->axis;const idMat3 expectedAxis=localAxis*render->axis;const auto* physics=door.Entity()->GetPhysics();
    Need(stats,door.Entity()->GetBindMaster()==parent,"Door QA lost its actual native wagon Bind");
    for(int i=0;i<3;++i)Need(stats,std::abs(physics->GetOrigin()[i]-expected[i])<=.001f,"Door QA native bound anchor differs from authored local frame");
    for(int i=0;i<9;++i)Need(stats,std::abs(physics->GetAxis()[i/3][i%3]-expectedAxis[i/3][i%3])<=.00002f,"Door QA native bound orientation differs from authored local frame");
    Need(stats,physics->GetNumClipModels()==0&&physics->GetContents()==0&&physics->GetClipModel()==gameLocal.clip.DefaultClipModel(),"Door QA invented source collision");}
void Endpoint(Stats& stats,const WolfSourceAnimatedDoor& door,const std::string& logical,int frame){auto* animator=door.Entity()->GetAnimator();const int number=animator->ModelDef()->GetSpecificAnim(logical.c_str());
    const auto* anim=number>0?animator->GetAnim(number):NULL;Need(stats,anim&&anim->NumAnims()==1&&anim->MD5Anim(0)&&anim->MD5Anim(0)->IsSourceAnimationV2(),"Door QA actual original source animation unavailable");
    animator->CreateFrame(gameLocal.time,true);int count=0;idJointMat* actual=NULL;animator->GetJoints(&count,&actual);Need(stats,count==2&&actual,"Door QA actual two-joint palette absent");
    idJointQuat* local=static_cast<idJointQuat*>(Mem_Alloc16(2*sizeof(idJointQuat)));idJointMat* expected=static_cast<idJointMat*>(Mem_Alloc16(2*sizeof(idJointMat)));
    try{const int indices[2]={0,1};anim->MD5Anim(0)->GetSingleFrame(frame,local,indices,2);const auto* hierarchy=animator->ModelHandle()->GetJoints();
        for(int i=0;i<2;++i){expected[i].SetRotation(local[i].q.ToMat3());expected[i].SetTranslation(local[i].t);if(hierarchy[i].parent)expected[i]*=expected[hierarchy[i].parent-hierarchy];
            for(int c=0;c<12;++c)Need(stats,std::abs(actual[i].ToFloatPtr()[c]-expected[i].ToFloatPtr()[c])<=.0002f,"Door QA palette differs from direct source-frame/FK");}
    }catch(...){Mem_Free16(expected);Mem_Free16(local);throw;}Mem_Free16(expected);Mem_Free16(local);}
WolfSourceActorStartEvidence Evidence(Stats& stats,const WolfSourceAnimatedDoorRequest& request){idStr reason;WolfSourceActorStartEvidence out;
    Need(stats,request.actorStart.context&&request.actorStart.preflight&&request.actorStart.query,"Door QA actor owner authority missing");
    bool ok=request.actorStart.preflight(request.actorStart.context,reason);Need(stats,ok,reason.IsEmpty()?"Door QA actual actor owner preflight failed":reason.c_str());
    ok=request.actorStart.query(request.actorStart.context,out,reason);Need(stats,ok,reason.IsEmpty()?"Door QA actual actor owner evidence missing":reason.c_str());return out;}
wolf2009::TrainDoorCommand ActivateCommand(const WolfSourceAnimatedDoorRequest& request){wolf2009::TrainDoorCommand out;out.kind=wolf2009::TrainDoorCommand::ActivateTarget;
    out.levelGeneration=request.levelGeneration;out.eventId=47;out.sourceNodeId=request.signal.actorStartNodeId;out.ownerEntityName=request.signal.ownerEntityName;out.targetEntityName=request.signal.targetEntityName;return out;}
WolfSourceActorStartEvidence StartActor(Stats& stats,const WolfSourceAnimatedDoorRequest& request,const WolfSourceAnimatedDoorActorStartDriver& driver,const wolf2009::TrainDoorCommand& activation){
    Need(stats,driver.context&&driver.start,"Door QA requires an actual scripted actor owner Start adapter");auto command=activation;command.kind=wolf2009::TrainDoorCommand::StartActorAnimation;
    command.targetEntityName.clear();command.resourceName=request.signal.actorAnimation;idStr reason;const bool ok=driver.start(driver.context,command,reason);
    Need(stats,ok,reason.IsEmpty()?"Door QA actual owner Start failed":reason.c_str());const auto proof=Evidence(stats,request);
    Need(stats,proof.committed&&proof.actualPoseEvaluated&&proof.nativeActor&&proof.nativeSpawnId&&proof.commandSerial&&proof.committedTimeMs==gameLocal.time&&
        proof.command.kind==command.kind&&proof.command.levelGeneration==command.levelGeneration&&proof.command.eventId==command.eventId&&proof.command.sourceNodeId==command.sourceNodeId&&
        proof.command.ownerEntityName==command.ownerEntityName&&proof.command.resourceName==command.resourceName&&proof.command.targetEntityName.empty(),"Door QA owner did not publish this exact actual Start commit");return proof;}
}

bool Wolf2009_CheckNativeSourceAnimatedDoorQA(const WolfSourceDeclarationSet& declarations,const WolfSourceAnimatedDoorRequest& request,idEntity* parent,const WolfSourceAnimatedDoorActorStartDriver& driver){
    Stats stats;idStr reason;const int baseline=Wolf2009_LiveSourceControlledEntities();try{
        const auto initialActor=Evidence(stats,request);auto* actor=initialActor.nativeActor;
        Need(stats,parent&&actor&&!initialActor.committed&&gameLocal.GameState()==GAMESTATE_ACTIVE,"Door QA requires real native inactive actor owner/active game");
        Need(stats,!gameLocal.FindEntity(request.signal.targetEntityName.c_str()),"Door QA source door already exists");
        auto door=Wolf2009_PrepareSourceAnimatedDoor(request,declarations,reason);Need(stats,bool(door),reason.IsEmpty()?"Door QA original prepare failed":reason.c_str());
        Need(stats,door->Facts().identity.entityIndex==2193&&door->Facts().name=="train_door_1"&&door->Facts().classname=="env_train_door"&&door->Facts().bindName=="starting_boxcar_model", "Door QA selected the wrong trainyard door");
        Need(stats,door->Facts().rawEditorFrame==154&&door->Facts().sourceLoopLastAnimation&&door->Facts().sourceNoClipModel,"Door QA lost authored non-runtime facts");
        Need(stats,door->Facts().defaults.selected.member.compiledRecord&&door->Binding().SourcePlan().ResourceRequest().clips.size()==3,"Door QA source compiled/model closure missing");
        const auto identity=door->Facts().identity;const auto prepared=State(stats,*door);Need(stats,prepared.phase==WolfSourceAnimatedDoor_Prepared&&prepared.sourceFrames==182&&prepared.sourceBones==2&&
            prepared.qualifiedConstantEndPose&&!door->Entity(),"Door QA preparation allocated/activated a native entity or lost source counts");
        auto invalid=request;invalid.policy=WolfSourceAnimatedDoor_Unspecified;Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA unspecified policy accepted");
        invalid=request;invalid.levelGeneration=0;Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA zero generation accepted");
        invalid=request;invalid.exactEntitiesPath="wolf2009/a/ENTS/../map";Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA traversal resource accepted");
        invalid=request;invalid.expectedEntitiesIdentityKnown=true;invalid.expectedEntitiesBytes=identity.sourceBytes;invalid.expectedEntitiesFingerprint=identity.sourceFingerprint^1;
        Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA wrong ENTS identity accepted");
        invalid=request;invalid.signal.targetEntityName+="_wrong";Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA wrong source signal target accepted");
        invalid=request;invalid.resources.clips.resize(4);Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA excessive request clip count accepted");
        invalid=request;invalid.signal.brainResource.assign(4097,'x');Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA excessive request text accepted");
        invalid=request;invalid.actorStart.query=NULL;Need(stats,!Wolf2009_PrepareSourceAnimatedDoor(invalid,declarations,reason),"Door QA incomplete actor owner authority accepted");
        Need(stats,Wolf2009_LiveSourceControlledEntities()==baseline,"Door QA preparation/refusals leaked native entities");
        bool ok=door->Spawn(actor,reason);Need(stats,!ok&&!door->Entity(),"Door QA accepted actor as the real source wagon");ok=door->Spawn(parent,reason);Need(stats,ok,reason.IsEmpty()?"Door QA native Spawn failed":reason.c_str());
        Need(stats,Wolf2009_LiveSourceControlledEntities()==baseline+1,"Door QA source controlled lifetime differs");BoundPose(stats,*door,parent);
        const idVec3 parentStart=parent->GetPhysics()->GetOrigin(),doorStart=door->Entity()->GetPhysics()->GetOrigin();
        const auto activate=ActivateCommand(request);
        for(int i=0;i<8;++i){Frame(stats);ok=door->Tick(reason);Need(stats,ok,reason.IsEmpty()?"Door QA idle Tick failed":reason.c_str());const auto idle=State(stats,*door);
            Need(stats,idle.phase==WolfSourceAnimatedDoor_Idle&&!idle.activations&&!idle.completions,"Door QA auto-started without real external actor event");BoundPose(stats,*door,parent);Endpoint(stats,*door,"idle",0);}
        WolfSourceControlledState actorBefore;ok=Wolf2009_SourceControlledState(actor,actorBefore,reason);Need(stats,ok,"Door QA supplied actor is not source-controlled");
        const auto before=State(stats,*door);const auto channel=Snapshot(stats,door->Entity());
        Need(stats,!door->Activate(activate,reason),"Door QA timer/held actor fabricated a start ACK");Unchanged(stats,*door,before,channel);
        const auto started=StartActor(stats,request,driver,activate);const auto actorChannel=Snapshot(stats,actor);
        Need(stats,started.nativeSpawnId==before.actorNativeSpawnId&&started.nativeSpawnId==initialActor.nativeSpawnId,"Door QA actor authority rebound its native generation");
        for(int n=0;n<8;++n){auto wrong=activate;
            if(n==0)++wrong.levelGeneration;if(n==1)wrong.eventId=0;if(n==2)++wrong.sourceNodeId;if(n==3)wrong.ownerEntityName+="_wrong";
            if(n==4)wrong.targetEntityName+="_wrong";if(n==5)wrong.resourceName="unexpected";if(n==6)wrong.kind=wolf2009::TrainDoorCommand::StartActorAnimation;if(n==7)++wrong.eventId;
            Need(stats,!door->Activate(wrong,reason),"Door QA stale/mismatched actual owner event admitted");Unchanged(stats,*door,before,channel);
            Need(stats,Same(actorChannel,Snapshot(stats,actor)),"Door QA refusal changed the actual actor channel");}
        ok=door->Activate(activate,reason);Need(stats,ok,reason.IsEmpty()?"Door QA true source actor owner activation failed":reason.c_str());const auto moving=State(stats,*door);
        Need(stats,moving.phase==WolfSourceAnimatedDoor_Moving&&moving.activations==1&&moving.actorCommandSerial==started.commandSerial&&moving.activationTimeMs==gameLocal.time&&
            moving.nativeEndTimeMs-moving.activationTimeMs==7542,"Door QA source duration/native move boundary differs");
        const auto playing=Snapshot(stats,door->Entity());ok=door->Activate(activate,reason);Need(stats,ok,"Door QA duplicate event not acknowledged");
        Need(stats,Same(playing,Snapshot(stats,door->Entity()))&&State(stats,*door).duplicateActivations==1,"Door QA duplicate event restarted native animation");
        unsigned guard=0;while(State(stats,*door).phase==WolfSourceAnimatedDoor_Moving){Need(stats,++guard<=600,"Door QA native animation failed to reach endpoint");Frame(stats);
            WolfSourceControlledState native;ok=Wolf2009_SourceControlledState(door->Entity(),native,reason);Need(stats,ok,"Door QA native move state missing");
            if(native.phase==WolfSourceControlled_Held){Need(stats,native.sourceFrame==181&&native.clip=="door_move","Door QA native held the wrong endpoint");Endpoint(stats,*door,"door_move",181);}
            ok=door->Tick(reason);Need(stats,ok,reason.IsEmpty()?"Door QA native moving Tick failed":reason.c_str());BoundPose(stats,*door,parent);}
        const auto complete=State(stats,*door);Need(stats,complete.phase==WolfSourceAnimatedDoor_Completed&&complete.activations==1&&complete.completions==1&&complete.endpointPaletteError<=.0002&&complete.completionTimeMs>=complete.nativeEndTimeMs,
            "Door QA exact source endpoint/completion proof missing");Endpoint(stats,*door,"end_idle",0);
        WolfSourceAnimatedDoorCompletion event;bool pending=false;ok=door->TakeCompletion(pending,event,reason);Need(stats,ok&&pending&&event.levelGeneration==request.levelGeneration&&event.eventId==activate.eventId&&
            event.sourceNodeId==activate.sourceNodeId&&event.entityName=="train_door_1"&&event.moveClip=="door_move"&&event.endClip=="end_idle","Door QA original completion identity differs");
        ok=door->TakeCompletion(pending,event,reason);Need(stats,ok&&!pending,"Door QA completion emitted twice");
        const auto finalChannel=Snapshot(stats,door->Entity());for(int i=0;i<4;++i){Frame(stats);ok=door->Tick(reason);Need(stats,ok,"Door QA final pose Tick failed");Endpoint(stats,*door,"end_idle",0);}
        Need(stats,State(stats,*door).completions==1&&State(stats,*door).consumedCompletions==1&&Snapshot(stats,door->Entity()).serial==finalChannel.serial,"Door QA final loop replayed completion or external commands");
        const auto finalState=State(stats,*door);const auto finalSnapshot=Snapshot(stats,door->Entity());auto another=activate;++another.eventId;
        Need(stats,!door->Activate(another,reason),"Door QA a new event restarted an already completed door");Unchanged(stats,*door,finalState,finalSnapshot);
        Need(stats,door->Facts().identity.sourceBytes==identity.sourceBytes&&door->Facts().identity.sourceFingerprint==identity.sourceFingerprint&&door->Facts().identity.resourcePath==identity.resourcePath&&
            door->Facts().rawEditorFrame==154&&door->Facts().instance.Find("frame")->integer==154,"Door QA mutated immutable source metadata");
        const idVec3 parentDelta=parent->GetPhysics()->GetOrigin()-parentStart,doorDelta=door->Entity()->GetPhysics()->GetOrigin()-doorStart;
        Need(stats,parentDelta.y>1.f,"Door QA source pusher did not actually move during bound playback");
        for(int i=0;i<3;++i)Need(stats,std::abs(parentDelta[i]-doorDelta[i])<=.002f,"Door QA actual child displacement differs from the moving source pusher");
        const int number=door->Entity()->entityNumber;door.reset();Need(stats,!gameLocal.entities[number]&&!gameLocal.FindEntity("train_door_1")&&Wolf2009_LiveSourceControlledEntities()==baseline,"Door QA owned native teardown leaked entity/bind");
        gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_QA PASS checks=%u frames=%u source_frames=182 source_bones=2 endpoint_error=%.9g end_pose_transition_error=%.9g parent_delta_y=%.9g child_delta_y=%.9g native_clip=1 native_bind=1 no_collision=1 completion_once=1\n",stats.checks,stats.frames,complete.endpointPaletteError,complete.endPoseTransitionError,parentDelta.y,doorDelta.y);return true;
    }catch(const std::exception& error){gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_QA FAIL checks=%u frames=%u reason=%s\n",stats.checks,stats.frames,error.what());return false;}
}

bool Wolf2009_CheckNativeSourceAnimatedDoorStaleStartQA(const WolfSourceDeclarationSet& declarations,const WolfSourceAnimatedDoorRequest& request,idEntity* parent,const WolfSourceAnimatedDoorActorStartDriver& driver){
    Stats stats;idStr reason;try{
        auto door=Wolf2009_PrepareSourceAnimatedDoor(request,declarations,reason);Need(stats,bool(door),reason.IsEmpty()?"Door stale QA prepare failed":reason.c_str());
        bool ok=door->Spawn(parent,reason);Need(stats,ok,reason.IsEmpty()?"Door stale QA Spawn failed":reason.c_str());const auto activate=ActivateCommand(request);
        const auto proof=StartActor(stats,request,driver,activate);Frame(stats);const auto before=State(stats,*door);const auto channel=Snapshot(stats,door->Entity());const auto actor=Snapshot(stats,proof.nativeActor);
        Need(stats,gameLocal.time>proof.committedTimeMs&&before.activations==0,"Door stale QA did not create a prior-tick owner Start");
        Need(stats,!door->Activate(activate,reason),"Door stale QA accepted a prior-tick true owner ACK before first activation");Unchanged(stats,*door,before,channel);
        Need(stats,Same(actor,Snapshot(stats,proof.nativeActor)),"Door stale QA refusal changed the actual actor channel");
        gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_STALE_QA PASS checks=%u frames=%u actual_owner_start=1 prior_tick_ack_refused=1 first_activation=0\n",stats.checks,stats.frames);return true;
    }catch(const std::exception& error){gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_STALE_QA FAIL checks=%u reason=%s\n",stats.checks,error.what());return false;}
}

bool Wolf2009_CheckNativeSourceAnimatedDoorReplacementQA(const WolfSourceDeclarationSet& declarations,const WolfSourceAnimatedDoorRequest& request,idEntity* parent,const WolfSourceAnimatedDoorActorStartDriver& driver){
    Stats stats;idStr reason;try{
        auto door=Wolf2009_PrepareSourceAnimatedDoor(request,declarations,reason);Need(stats,bool(door),reason.IsEmpty()?"Door replacement QA prepare failed":reason.c_str());
        bool ok=door->Spawn(parent,reason);Need(stats,ok,reason.IsEmpty()?"Door replacement QA Spawn failed":reason.c_str());const auto activate=ActivateCommand(request);
        const auto proof=StartActor(stats,request,driver,activate);const auto before=State(stats,*door);const auto channel=Snapshot(stats,door->Entity());
        Need(stats,before.activations==0&&before.actorNativeSpawnId==proof.nativeSpawnId,"Door replacement QA is not before first accepted activation");
        const idStr model=proof.nativeActor->GetAnimator()->ModelDef()->GetName();const idStr name=proof.nativeActor->GetName();delete proof.nativeActor;
        WolfSourceControlledRequest clone;clone.name=name;clone.modelDefinition=model;clone.initialClip=request.signal.actorAnimation.c_str();clone.initialSourceFrame=0;
        clone.policy=WolfSourceControlled_NativeMillisecondsHoldLastV2;idAnimatedEntity* raw=NULL;ok=Wolf2009_SpawnSourceControlled(clone,raw,reason);
        std::unique_ptr<idAnimatedEntity> replacement(raw);Need(stats,ok&&bool(replacement),reason.IsEmpty()?"Door replacement QA actual clone Spawn failed":reason.c_str());
        Need(stats,static_cast<unsigned>(gameLocal.GetSpawnId(raw))!=proof.nativeSpawnId&&name==raw->GetName(),"Door replacement QA did not replace the native spawn under the same name");
        WolfSourceControlledState state;ok=Wolf2009_SourceControlledState(raw,state,reason);Need(stats,ok,"Door replacement QA clone state absent");unsigned guard=0;
        // Real native commands deliberately reproduce the old admission inputs;
        // they cannot fabricate the scripted owner's retained Start authority.
        while(state.commandSerial<proof.commandSerial){Need(stats,++guard<=64,"Door replacement QA source serial budget");
            ok=Wolf2009_SourceControlledPlayOnce(raw,request.signal.actorAnimation.c_str(),0,reason);Need(stats,ok,"Door replacement QA actual clone PlayOnce failed");
            ok=Wolf2009_SourceControlledState(raw,state,reason);Need(stats,ok,"Door replacement QA actual clone state absent");}
        Need(stats,state.phase==WolfSourceControlled_Playing&&state.clip==request.signal.actorAnimation.c_str()&&state.commandSerial==proof.commandSerial&&
            state.commandTimeMs==proof.committedTimeMs&&state.commandTimeMs==gameLocal.time,"Door replacement QA failed to reproduce same clip/time/serial observables");
        const auto cloneChannel=Snapshot(stats,raw);Need(stats,!door->Activate(activate,reason),"Door replacement QA accepted same-name replacement before first activation");Unchanged(stats,*door,before,channel);
        Need(stats,Same(cloneChannel,Snapshot(stats,raw)),"Door replacement QA refusal changed the replacement native actor");
        gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_REPLACEMENT_QA PASS checks=%u original_spawn=%u replacement_spawn=%u same_clip_time_serial=1 actual_clone=1 owner_rebind=0 first_activation=0\n",
            stats.checks,proof.nativeSpawnId,static_cast<unsigned>(gameLocal.GetSpawnId(raw)));return true;
    }catch(const std::exception& error){gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_REPLACEMENT_QA FAIL checks=%u reason=%s\n",stats.checks,error.what());return false;}
}

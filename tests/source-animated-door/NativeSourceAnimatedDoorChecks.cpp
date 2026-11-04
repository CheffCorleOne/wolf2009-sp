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
        before.consumedCompletions==after.consumedCompletions&&before.doorCommandSerial==after.doorCommandSerial&&before.actorCommandSerial==after.actorCommandSerial&&
        before.eventId==after.eventId&&before.sourceNodeId==after.sourceNodeId&&before.activationTimeMs==after.activationTimeMs&&before.nativeEndTimeMs==after.nativeEndTimeMs&&
        before.completionTimeMs==after.completionTimeMs,"Door QA refusal changed owned runtime state");Need(stats,Same(channel,Snapshot(stats,door.Entity())),"Door QA refusal changed actual native animator channel");}
void BoundPose(Stats& stats,const WolfSourceAnimatedDoor& door,idEntity* parent){const auto a=wolf2009::ReadEntityPlacement(door.Facts().instance),b=wolf2009::ReadEntityPlacement(door.Facts().parentInstance);
    idVec3 authored(a.origin[0],a.origin[1],a.origin[2]),parentOrigin(b.origin[0],b.origin[1],b.origin[2]);idMat3 axis,parentAxis;
    for(int i=0;i<9;++i){axis[i/3][i%3]=a.rotation[i];parentAxis[i/3][i%3]=b.rotation[i];}const idVec3 local=(authored-parentOrigin)*parentAxis.Transpose();const idMat3 localAxis=axis*parentAxis.Transpose();
    const auto* render=parent->GetRenderEntity();const idVec3 expected=render->origin+local*render->axis;const idMat3 expectedAxis=localAxis*render->axis;const auto* physics=door.Entity()->GetPhysics();
    Need(stats,door.Entity()->GetBindMaster()==parent,"Door QA lost its actual native wagon Bind");
    for(int i=0;i<3;++i)Need(stats,std::abs(physics->GetOrigin()[i]-expected[i])<=.001f,"Door QA native bound anchor differs from authored local frame");
    for(int i=0;i<9;++i)Need(stats,std::abs(physics->GetAxis()[i/3][i%3]-expectedAxis[i/3][i%3])<=.00002f,"Door QA native bound orientation differs from authored local frame");
    Need(stats,!physics->GetClipModel()&&physics->GetContents()==0,"Door QA invented source collision");}
void Endpoint(Stats& stats,const WolfSourceAnimatedDoor& door,const std::string& logical,int frame){auto* animator=door.Entity()->GetAnimator();const int number=animator->ModelDef()->GetSpecificAnim(logical.c_str());
    const auto* anim=number>0?animator->GetAnim(number):NULL;Need(stats,anim&&anim->NumAnims()==1&&anim->MD5Anim(0)&&anim->MD5Anim(0)->IsSourceAnimationV2(),"Door QA actual original source animation unavailable");
    animator->CreateFrame(gameLocal.time,true);int count=0;idJointMat* actual=NULL;animator->GetJoints(&count,&actual);Need(stats,count==2&&actual,"Door QA actual two-joint palette absent");
    idJointQuat* local=static_cast<idJointQuat*>(Mem_Alloc16(2*sizeof(idJointQuat)));idJointMat* expected=static_cast<idJointMat*>(Mem_Alloc16(2*sizeof(idJointMat)));
    try{const int indices[2]={0,1};anim->MD5Anim(0)->GetSingleFrame(frame,local,indices,2);const auto* hierarchy=animator->ModelHandle()->GetJoints();
        for(int i=0;i<2;++i){expected[i].SetRotation(local[i].q.ToMat3());expected[i].SetTranslation(local[i].t);if(hierarchy[i].parent)expected[i]*=expected[hierarchy[i].parent-hierarchy];
            for(int c=0;c<12;++c)Need(stats,std::abs(actual[i].ToFloatPtr()[c]-expected[i].ToFloatPtr()[c])<=.0002f,"Door QA palette differs from direct source-frame/FK");}
    }catch(...){Mem_Free16(expected);Mem_Free16(local);throw;}Mem_Free16(expected);Mem_Free16(local);}
}

bool Wolf2009_CheckNativeSourceAnimatedDoorQA(const WolfSourceDeclarationSet& declarations,const WolfSourceAnimatedDoorRequest& request,idEntity* parent,idAnimatedEntity* actor){
    Stats stats;idStr reason;const int baseline=Wolf2009_LiveSourceControlledEntities();try{
        Need(stats,parent&&actor&&gameLocal.GameState()==GAMESTATE_ACTIVE,"Door QA requires real native source receivers/active game");
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
        Need(stats,Wolf2009_LiveSourceControlledEntities()==baseline,"Door QA preparation/refusals leaked native entities");
        bool ok=door->Spawn(actor,reason);Need(stats,!ok&&!door->Entity(),"Door QA accepted actor as the real source wagon");ok=door->Spawn(parent,reason);Need(stats,ok,reason.IsEmpty()?"Door QA native Spawn failed":reason.c_str());
        Need(stats,Wolf2009_LiveSourceControlledEntities()==baseline+1,"Door QA source controlled lifetime differs");BoundPose(stats,*door,parent);
        wolf2009::TrainDoorCommand activate;activate.kind=wolf2009::TrainDoorCommand::ActivateTarget;activate.levelGeneration=request.levelGeneration;activate.eventId=47;activate.sourceNodeId=request.signal.actorStartNodeId;
        activate.ownerEntityName=request.signal.ownerEntityName;activate.targetEntityName=request.signal.targetEntityName;
        for(int i=0;i<8;++i){Frame(stats);ok=door->Tick(reason);Need(stats,ok,reason.IsEmpty()?"Door QA idle Tick failed":reason.c_str());const auto idle=State(stats,*door);
            Need(stats,idle.phase==WolfSourceAnimatedDoor_Idle&&!idle.activations&&!idle.completions,"Door QA auto-started without real external actor event");BoundPose(stats,*door,parent);Endpoint(stats,*door,"idle",0);}
        WolfSourceControlledState actorBefore;ok=Wolf2009_SourceControlledState(actor,actorBefore,reason);Need(stats,ok,"Door QA supplied actor is not source-controlled");
        const auto before=State(stats,*door);const auto channel=Snapshot(stats,door->Entity());
        Need(stats,!door->Activate(activate,actor,actorBefore.commandSerial,reason),"Door QA timer/held actor fabricated a start ACK");Unchanged(stats,*door,before,channel);
        ok=Wolf2009_SourceControlledPlayOnce(actor,request.signal.actorAnimation.c_str(),0,reason);Need(stats,ok,reason.IsEmpty()?"Door QA actual actor PlayOnce failed":reason.c_str());
        WolfSourceControlledState started;ok=Wolf2009_SourceControlledState(actor,started,reason);Need(stats,ok&&started.phase==WolfSourceControlled_Playing&&started.commandTimeMs==gameLocal.time,"Door QA actual actor start was not committed");
        Frame(stats);const auto staleActor=Snapshot(stats,actor);
        Need(stats,!door->Activate(activate,actor,started.commandSerial,reason),"Door QA accepted a previous-tick actor start ACK");Unchanged(stats,*door,before,channel);
        Need(stats,Same(staleActor,Snapshot(stats,actor)),"Door QA stale ACK refusal changed the actual actor channel");
        ok=Wolf2009_SourceControlledPlayOnce(actor,request.signal.actorAnimation.c_str(),0,reason);Need(stats,ok,"Door QA actual actor retry PlayOnce failed");
        ok=Wolf2009_SourceControlledState(actor,started,reason);Need(stats,ok&&started.commandTimeMs==gameLocal.time,"Door QA actual actor retry did not commit");const auto actorChannel=Snapshot(stats,actor);
        for(int n=0;n<8;++n){auto wrong=activate;unsigned serial=started.commandSerial;
            if(n==0)++wrong.levelGeneration;if(n==1)wrong.eventId=0;if(n==2)++wrong.sourceNodeId;if(n==3)wrong.ownerEntityName+="_wrong";
            if(n==4)wrong.targetEntityName+="_wrong";if(n==5)wrong.resourceName="unexpected";if(n==6)wrong.kind=wolf2009::TrainDoorCommand::StartActorAnimation;if(n==7)++serial;
            Need(stats,!door->Activate(wrong,actor,serial,reason),"Door QA stale/mismatched actor activation admitted");Unchanged(stats,*door,before,channel);
            Need(stats,Same(actorChannel,Snapshot(stats,actor)),"Door QA refusal changed the actual actor channel");}
        ok=door->Activate(activate,actor,started.commandSerial,reason);Need(stats,ok,reason.IsEmpty()?"Door QA true source actor-start activation failed":reason.c_str());const auto moving=State(stats,*door);
        Need(stats,moving.phase==WolfSourceAnimatedDoor_Moving&&moving.activations==1&&moving.actorCommandSerial==started.commandSerial&&moving.activationTimeMs==gameLocal.time&&
            moving.nativeEndTimeMs-moving.activationTimeMs==7542,"Door QA source duration/native move boundary differs");
        const auto playing=Snapshot(stats,door->Entity());ok=door->Activate(activate,actor,started.commandSerial,reason);Need(stats,ok,"Door QA duplicate event not acknowledged");
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
        Need(stats,!door->Activate(another,actor,started.commandSerial,reason),"Door QA a new event restarted an already completed door");Unchanged(stats,*door,finalState,finalSnapshot);
        Need(stats,door->Facts().identity.sourceBytes==identity.sourceBytes&&door->Facts().identity.sourceFingerprint==identity.sourceFingerprint&&door->Facts().identity.resourcePath==identity.resourcePath&&
            door->Facts().rawEditorFrame==154&&door->Facts().instance.Find("frame")->integer==154,"Door QA mutated immutable source metadata");
        const int number=door->Entity()->entityNumber;door.reset();Need(stats,!gameLocal.entities[number]&&!gameLocal.FindEntity("train_door_1")&&Wolf2009_LiveSourceControlledEntities()==baseline,"Door QA owned native teardown leaked entity/bind");
        gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_QA PASS checks=%u frames=%u source_frames=182 source_bones=2 endpoint_error=%.9g end_pose_transition_error=%.9g native_clip=1 native_bind=1 no_collision=1 completion_once=1\n",stats.checks,stats.frames,complete.endpointPaletteError,complete.endPoseTransitionError);return true;
    }catch(const std::exception& error){gameLocal.Printf("WOLF2009_SOURCE_ANIMATED_DOOR_QA FAIL checks=%u frames=%u reason=%s\n",stats.checks,stats.frames,error.what());return false;}
}

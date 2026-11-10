// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineAnimationSet.h"
#include "SourceControlledEntity.h"

namespace {
int liveControlled=0;
const int MaximumControlled=128;
bool Refuse(idStr& reason,const char* text) { reason=text;return false; }
void Need(bool condition,const char* text) { if(!condition)throw std::runtime_error(text); }
bool ActiveGame() { return gameLocal.GameState()==GAMESTATE_ACTIVE&&!gameLocal.isClient&&!gameLocal.isMultiplayer&&gameLocal.time>=0; }
bool SafeName(const idStr& name) {
    if(name.Length()<1||name.Length()>127||name=="NULL"||name=="null_entity")return false;
    for(int i=0;i<name.Length();++i){const unsigned char c=name[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i>0&&c>='0'&&c<='9')))return false;
    }return true;
}
bool Placement(const WolfSourceControlledRequest& request) {
    for(int i=0;i<3;++i){
        if(!std::isfinite(request.origin[i])||std::abs(request.origin[i])>10000000.0f)return false;
        for(int j=0;j<3;++j)if(!std::isfinite(request.axis[i][j]))return false;
    }
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)
        if(std::abs(request.axis[i]*request.axis[j]-(i==j?1.0f:0.0f))>0.0001f)return false;
    return std::abs(request.axis.Determinant()-1.0f)<=0.0001f;
}
std::string PlacementWords(const float* values,int count) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10);
    for(int i=0;i<count;++i){if(i)out<<' ';out<<values[i];}return out.str();
}
bool FlagsAbsent(const idAnim* clip) {
    const animFlags_t& flags=clip->GetAnimFlags();
    return !flags.prevent_idle_override&&!flags.random_cycle_start&&!flags.ai_no_turn&&!flags.anim_turn;
}
struct Clip {
    std::string name,alias;
    const idMD5Anim* animation;
    int number,frames,length;
};
std::string DeclarationText(const idDeclModelDef* definition) {
    Need(definition&&definition->GetState()==DS_PARSED,"controlled set is not a parsed native declaration");
    const int length=definition->GetTextLength();
    Need(length>0&&length<=64*1024,"controlled set declaration text budget");
    std::vector<char> bytes(std::size_t(length)+1,0);definition->GetText(bytes.data());
    return std::string(bytes.data(),std::size_t(length));
}
bool ClipMatches(const idDeclModelDef* definition,const Clip& expected) {
    const int number=definition->GetSpecificAnim(expected.name.c_str());
    const idAnim* clip=number>0?definition->GetAnim(number):NULL;
    if(number!=expected.number||!clip||clip->NumAnims()!=1||clip->HasFrameCommands()||!FlagsAbsent(clip)||
       expected.name!=clip->FullName())return false;
    const idMD5Anim* animation=clip->MD5Anim(0);
    return animation&&animation==expected.animation&&animation->IsSourceAnimationV2()&&expected.alias==animation->Name()&&
        animation->NumFrames()==expected.frames&&animation->Length()==expected.length&&
        animation->NumJoints()==definition->NumJoints()&&animation->SourceMatchesModel(definition->ModelHandle());
}
std::vector<Clip> ReadClips(const idDeclModelDef* definition) {
    Need(definition&&definition->GetState()==DS_PARSED,"controlled entity requires a parsed animation set");
    const idRenderModel* model=definition->ModelHandle();
    Need(model&&model->IsLoaded()&&!model->IsDefaultModel()&&model->NumJoints()>=1&&model->NumJoints()<=256&&
         definition->NumJoints()==model->NumJoints(),"controlled set has no qualified actual model");
    const int count=definition->NumAnims()-1;Need(count>=1&&count<=64,"controlled set clip count must be 1..64");
    std::vector<Clip> clips;clips.reserve(std::size_t(count));
    for(int i=1;i<=count;++i){
        const idAnim* clip=definition->GetAnim(i);Need(clip&&clip->NumAnims()==1,"controlled set needs one source per logical clip");
        const idMD5Anim* animation=clip->MD5Anim(0);
        Need(animation&&animation->IsSourceAnimationV2(),"controlled set requires source-v2 clips");
        Clip entry;entry.name=clip->FullName();entry.alias=animation->Name();entry.animation=animation;
        entry.number=i;entry.frames=animation->NumFrames();entry.length=animation->Length();
        Need(!entry.name.empty()&&entry.name.size()<=127&&entry.frames>=1&&entry.length>=1&&
             std::string(animation->SourceModelAlias())==model->Name()&&ClipMatches(definition,entry),
             "controlled set source identity, flags or hierarchy differs");
        clips.push_back(entry);
    }return clips;
}
int FindClip(const std::vector<Clip>& clips,const char* name) {
    if(!name)return -1;
    // The factory's exact logical names are at most 127 bytes. Do not construct
    // an unbounded string or invoke native random alias selection here.
    std::size_t length=0;while(length<=127&&name[length])++length;if(length>127)return -1;
    for(std::size_t i=0;i<clips.size();++i)if(clips[i].name.compare(0,std::string::npos,name,length)==0)return int(i);
    return -1;
}
struct Busy {
    bool& flag;explicit Busy(bool& value):flag(value){flag=true;}~Busy(){flag=false;}
};
}

class idWolfSourceControlled : public idAnimatedEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceControlled);
    idWolfSourceControlled() : definition(NULL),model(NULL),assigned(false),bound(false),busy(false),
        phase(WolfSourceControlled_Unbound),clipIndex(-1),heldFrame(-1),commandTime(-1),endTime(-1),blendEnd(-1),serial(0) {
        ++liveControlled;animator.RemoveOriginOffset(false);
    }
    ~idWolfSourceControlled() {
        // These extra native refs are independent of registry discovery. The
        // animator/base destructor still releases its ordinary level ownership.
        for(const auto& clip:clips)clip.animation->DecreaseRefs();
        --liveControlled;
    }
    void SetModel(const char* name) override {
        if(assigned||bound||!Wolf2009_FindRegisteredSourceAnimationSet(name)){
            gameLocal.Warning("Controlled source model replacement refused before mutation");return;
        }
        idAnimatedEntity::SetModel(name);assigned=true;
    }
    bool Bind(const idDeclModelDef* expected,std::vector<Clip>& entries,const std::string& text,int initial,int initialFrame,idStr& reason) {
        if(bound||!assigned||animator.ModelDef()!=expected||animator.RemoveOrigin())return Refuse(reason,"controlled native model initialization differs");
        definition=expected;model=expected->ModelHandle();modelName=model->Name();definitionText=text;
        clips.swap(entries);for(const auto& clip:clips)clip.animation->IncreaseRefs();
        bound=true;ApplyHold(initial,initialFrame,0,gameLocal.time,true);return true;
    }
    bool Command(const char* name,int frame,int blend,bool play,idStr& reason,bool cycle=false) {
        reason.Clear();
        if(busy)return Refuse(reason,"controlled source command reentry is unsupported");
        Busy guard(busy);
        if(!ActiveGame()||!Matches())return Refuse(reason,"controlled source owner/model/set identity is unavailable or changed");
        const int index=FindClip(clips,name),now=gameLocal.time;
        if(index<0)return Refuse(reason,"controlled source clip key is absent");
        if(now<commandTime||blend<0||now>(std::numeric_limits<int>::max)()-blend||serial==(std::numeric_limits<unsigned>::max)())
            return Refuse(reason,"controlled source command time/blend/serial range is invalid");
        const Clip& clip=clips[std::size_t(index)];
        if(cycle){
            animator.CycleAnim(ANIMCHANNEL_ALL,clip.number,now,blend);
            animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowMovement(false);
            animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowFrameCommands(false);
            animator.ForceUpdate();
            phase=WolfSourceControlled_Cycling;clipIndex=index;heldFrame=-1;commandTime=now;
            endTime=-1;blendEnd=now+blend;++serial;
            BecomeActive(TH_ANIMATE);UpdateVisuals();
        }else if(play){
            if(blend>clip.length||now>(std::numeric_limits<int>::max)()-clip.length)
                return Refuse(reason,"controlled one-shot range overflow or blend exceeds clip length");
            animator.PlayAnim(ANIMCHANNEL_ALL,clip.number,now,blend);
            animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowMovement(false);
            animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowFrameCommands(false);
            animator.ForceUpdate(); // Also required for zero-weight/same-ms replacement.
            phase=WolfSourceControlled_Playing;clipIndex=index;heldFrame=-1;commandTime=now;
            endTime=now+clip.length;blendEnd=now+blend;++serial;
            BecomeActive(TH_ANIMATE);UpdateVisuals();
        }else{
            if(frame<0||frame>=clip.frames)return Refuse(reason,"controlled hold source frame is outside the exact clip");
            ApplyHold(index,frame,blend,now,true);
        }return true;
    }
    bool State(WolfSourceControlledState& out,idStr& reason) const {
        reason.Clear();if(!bound||clipIndex<0)return Refuse(reason,"controlled source entity is unbound");
        WolfSourceControlledState value;value.phase=phase;value.clip=clips[std::size_t(clipIndex)].name.c_str();value.sourceFrame=heldFrame;
        value.commandTimeMs=commandTime;value.endTimeMs=endTime;value.blendEndTimeMs=blendEnd;value.commandSerial=serial;out=value;return true;
    }
    void Think() override {
        if(bound&&phase==WolfSourceControlled_Playing&&gameLocal.time>=endTime){
            // No callbacks or authored commands are enabled. At ceil(end), the
            // direct last frame is the source endpoint; install the native hold
            // before ordinary UpdateAnimation/Present publishes this tick.
            try{
                Need(!busy&&Matches(),"controlled source set changed before final hold");
                Busy guard(busy);ApplyHold(clipIndex,clips[std::size_t(clipIndex)].frames-1,0,gameLocal.time,false);
            }catch(const std::exception& error){gameLocal.Error("Controlled source final hold: %s",error.what());return;}
        }
        idAnimatedEntity::Think();
    }
    void Save(idSaveGame*) const { gameLocal.Error("Controlled source save-state policy is unsupported"); }
    void Restore(idRestoreGame*) { gameLocal.Error("Controlled source restore policy is unsupported"); }
  private:
    const idDeclModelDef* definition;
    idRenderModel* model;
    std::string modelName,definitionText;
    std::vector<Clip> clips;
    bool assigned,bound,busy;
    WolfSourceControlledPhase phase;
    int clipIndex,heldFrame,commandTime,endTime,blendEnd;
    unsigned serial;
    bool Matches() const {
        if(!bound||!definition||animator.ModelDef()!=definition||animator.ModelHandle()!=model||animator.RemoveOrigin()||
           !model||!model->IsLoaded()||model->IsDefaultModel()||modelName!=model->Name()||
           definition->GetState()!=DS_PARSED||definition->NumAnims()!=int(clips.size()+1)||DeclarationText(definition)!=definitionText)return false;
        for(const auto& clip:clips)if(!ClipMatches(definition,clip))return false;return true;
    }
    void ApplyHold(int index,int frame,int blend,int now,bool external) {
        const Clip& clip=clips[std::size_t(index)];
        animator.SetFrame(ANIMCHANNEL_ALL,clip.number,frame+1,now,blend);
        animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowFrameCommands(false);
        animator.CurrentAnim(ANIMCHANNEL_ALL)->AllowMovement(false);
        animator.ForceUpdate();
        phase=WolfSourceControlled_Held;clipIndex=index;heldFrame=frame;endTime=-1;blendEnd=now+blend;
        if(external){commandTime=now;++serial;}
        BecomeActive(TH_ANIMATE);UpdateVisuals();
    }
};
CLASS_DECLARATION(idAnimatedEntity,idWolfSourceControlled)
END_CLASS

bool Wolf2009_SpawnSourceControlled(const WolfSourceControlledRequest& request,idAnimatedEntity*& out,idStr& reason) {
    out=NULL;reason.Clear();
    try{
        if(!ActiveGame())return Refuse(reason,"controlled source requires an active local single-player game clock");
        if(request.policy!=WolfSourceControlled_NativeMillisecondsHoldLastV2)return Refuse(reason,"controlled source requires the explicit native-ms hold-last v2 policy");
        if(!SafeName(request.name)||gameLocal.FindEntity(request.name))return Refuse(reason,"controlled source entity name is invalid or occupied");
        if(!Placement(request))return Refuse(reason,"controlled source placement must be finite and rigid");
        if(liveControlled>=MaximumControlled)return Refuse(reason,"controlled source entity budget exceeded");
        bool free=false;for(int i=MAX_CLIENTS;i<ENTITYNUM_MAX_NORMAL;++i)if(!gameLocal.entities[i]){free=true;break;}
        if(!free)return Refuse(reason,"controlled source native entity slots exhausted");
        if(request.modelDefinition.Length()>4096)return Refuse(reason,"controlled source modelDef name budget");
        const idDeclModelDef* definition=Wolf2009_FindRegisteredSourceAnimationSet(request.modelDefinition);
        if(!definition)return Refuse(reason,"controlled source requires an unchanged registered animation set");
        auto clips=ReadClips(definition);const std::string text=DeclarationText(definition);
        const int initial=FindClip(clips,request.initialClip.c_str());
        if(initial<0||request.initialSourceFrame<0||request.initialSourceFrame>=clips[std::size_t(initial)].frames)
            return Refuse(reason,"controlled source initial clip/frame is invalid");
        idDict args;args.Set("classname","wolf2009_source_controlled");args.Set("name",request.name);args.Set("model",request.modelDefinition);
        args.Set("origin",PlacementWords(request.origin.ToFloatPtr(),3).c_str());args.Set("rotation",PlacementWords(request.axis.ToFloatPtr(),9).c_str());
        args.SetBool("noclipmodel",true);args.SetBool("neverDormant",true);args.SetBool("cinematic",true);args.SetBool("networkSync",false);
        std::unique_ptr<idWolfSourceControlled> entity(static_cast<idWolfSourceControlled*>(gameLocal.SpawnEntityType(idWolfSourceControlled::Type,&args)));
        if(!entity||!entity->Bind(definition,clips,text,initial,request.initialSourceFrame,reason))return Refuse(reason,"controlled source native spawn/binding failed");
        out=entity.release();return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool Wolf2009_IsSourceControlledEntity(const idEntity* entity) { return entity&&entity->GetType()==&idWolfSourceControlled::Type; }
int Wolf2009_LiveSourceControlledEntities() { return liveControlled; }
bool Wolf2009_SourceControlledHoldFrame(idAnimatedEntity* entity,const char* clip,int frame,int blend,idStr& reason) {
    try{if(!Wolf2009_IsSourceControlledEntity(entity))return Refuse(reason,"entity is not a controlled source visual");
        return static_cast<idWolfSourceControlled*>(entity)->Command(clip,frame,blend,false,reason);
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool Wolf2009_SourceControlledPlayOnce(idAnimatedEntity* entity,const char* clip,int blend,idStr& reason) {
    try{if(!Wolf2009_IsSourceControlledEntity(entity))return Refuse(reason,"entity is not a controlled source visual");
        return static_cast<idWolfSourceControlled*>(entity)->Command(clip,0,blend,true,reason);
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool Wolf2009_SourceControlledCycle(idAnimatedEntity* entity,const char* clip,int blend,idStr& reason) {
    try{if(!Wolf2009_IsSourceControlledEntity(entity))return Refuse(reason,"entity is not a controlled source visual");
        return static_cast<idWolfSourceControlled*>(entity)->Command(clip,0,blend,false,reason,true);
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool Wolf2009_SourceControlledState(const idAnimatedEntity* entity,WolfSourceControlledState& state,idStr& reason) {
    try{if(!Wolf2009_IsSourceControlledEntity(entity))return Refuse(reason,"entity is not a controlled source visual");
        return static_cast<const idWolfSourceControlled*>(entity)->State(state,reason);
    }catch(const std::exception& error){reason=error.what();return false;}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceScriptedVoiceReadiness.h"

namespace {
int liveRequests=0;
bool Name(const std::string& value){if(value.empty()||value.size()>1024)return false;
    for(unsigned char c:value)if(c<32||c>=127)return false;return true;}
const char* const WholeBankProfile="ScriptedVOStreamInWholeBankV1";
const char* const Unavailable="ScriptedVOStreamIn whole-bank SNDS registration is not implemented in this build; node readiness stays unsupported";
// Records one owner's request so its lifetime, owner generation and release
// are observable. It deliberately has no path that can report Ready.
class UnavailableWholeBank : public WolfScriptedVoiceReadinessRequest {
  public:
    UnavailableWholeBank(const WolfScriptedVoiceReadinessSource& value,idEntity* owner):source(value),entityNumber(owner->entityNumber),spawnId(gameLocal.GetSpawnId(owner)){
        ++liveRequests;
        if(!Name(source.brainResource)||!Name(source.definition)||!Name(source.locale)||source.nodeType!=19||!source.nodeId||!source.levelGeneration||!source.eventId)
            Stop(WolfScriptedVoiceReadinessStatus::Failed,"ScriptedVOStreamIn readiness source is incomplete or not a type19 node");
        else reason=Unavailable;
    }
    ~UnavailableWholeBank(){--liveRequests;}
    WolfScriptedVoiceReadinessStatus Update()override{
        if(status!=WolfScriptedVoiceReadinessStatus::Unsupported)return status;
        idEntity* owner=entityNumber>=0&&entityNumber<MAX_GENTITIES?gameLocal.entities[entityNumber]:NULL;
        if(!owner||gameLocal.GetSpawnId(owner)!=spawnId)Stop(WolfScriptedVoiceReadinessStatus::Lost,"ScriptedVOStreamIn readiness owner generation ended");
        return status;
    }
    bool ConsumeReady()override{return false;}
    void Cancel(const char* why)override{if(status==WolfScriptedVoiceReadinessStatus::Unsupported)Stop(WolfScriptedVoiceReadinessStatus::Cancelled,why&&*why?why:"cancelled by brain host");}
    WolfScriptedVoiceReadinessStatus Status()const override{return status;}
    const std::string& Reason()const override{return reason;}
    const char* Profile()const override{return WholeBankProfile;}
  private:
    void Stop(WolfScriptedVoiceReadinessStatus value,const char* why){status=value;reason=why;}
    WolfScriptedVoiceReadinessSource source;int entityNumber=-1,spawnId=0;
    WolfScriptedVoiceReadinessStatus status=WolfScriptedVoiceReadinessStatus::Unsupported;std::string reason;
};
}
const char* Wolf2009_ScriptedVoiceReadinessText(WolfScriptedVoiceReadinessStatus value){switch(value){
    case WolfScriptedVoiceReadinessStatus::Pending:return "pending";case WolfScriptedVoiceReadinessStatus::Ready:return "ready";
    case WolfScriptedVoiceReadinessStatus::Failed:return "failed";case WolfScriptedVoiceReadinessStatus::Cancelled:return "cancelled";
    case WolfScriptedVoiceReadinessStatus::Lost:return "lost";case WolfScriptedVoiceReadinessStatus::Unsupported:return "unsupported";}return "unknown";}
std::unique_ptr<WolfScriptedVoiceReadinessRequest> Wolf2009_RequestScriptedVoiceReadiness(const std::string& profile,const WolfScriptedVoiceReadinessSource& source,idEntity* owner,std::string& reason){
    reason.clear();
    if(profile!=WholeBankProfile){reason="Unknown scripted-voice readiness profile";return std::unique_ptr<WolfScriptedVoiceReadinessRequest>();}
    if(!owner||owner->entityNumber<0||owner->entityNumber>=MAX_GENTITIES||gameLocal.entities[owner->entityNumber]!=owner){
        reason="ScriptedVOStreamIn readiness owner is absent";return std::unique_ptr<WolfScriptedVoiceReadinessRequest>();}
    std::unique_ptr<WolfScriptedVoiceReadinessRequest> out(new UnavailableWholeBank(source,owner));reason=out->Reason();return out;
}
int Wolf2009_LiveScriptedVoiceReadinessRequests(){return liveRequests;}

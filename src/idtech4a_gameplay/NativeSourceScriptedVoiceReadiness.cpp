// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceScriptedVoiceReadiness.h"
#include <limits>
#include <map>
#include <utility>

namespace {
int liveRequests=0;
bool Name(const std::string& value){if(value.empty()||value.size()>1024)return false;
    for(unsigned char c:value)if(c<32||c>=127)return false;return true;}
const char* const WholeBankProfile="ScriptedVOStreamInWholeBankV1";
const char* const Unavailable="ScriptedVOStreamIn whole-bank SNDS registration is not implemented in this build; node readiness stays unsupported";
// Records each owner's request so its lifetime and Release are observable.
// It deliberately has no path that can report Ready.
class UnavailableWholeBank : public WolfScriptedVoiceReadiness {
  public:
    ~UnavailableWholeBank(){liveRequests-=int(requests.size());}
    const char* Profile()const override{return WholeBankProfile;}
    WolfScriptedVoiceReadinessStatus Request(const WolfScriptedVoiceReadinessRequest& request,std::uint64_t& ticket,std::string& reason)override{
        ticket=0;
        if(!Name(request.definition)||!Name(request.ownerName)||!Name(request.brainResource)||!Name(request.locale)||
           !request.levelGeneration||!request.eventId||!request.sourceNodeId||request.sourceNodeType!=19||!request.ownerSpawnId){
            reason="ScriptedVOStreamIn readiness request is incomplete or not a type19 node";return WolfScriptedVoiceReadinessStatus::Failed;}
        if(requests.size()>=64||next==(std::numeric_limits<std::uint64_t>::max)()){reason="ScriptedVOStreamIn readiness request budget";return WolfScriptedVoiceReadinessStatus::Failed;}
        ticket=next++;requests.emplace(ticket,request);++liveRequests;reason=Unavailable;return WolfScriptedVoiceReadinessStatus::Unsupported;
    }
    WolfScriptedVoiceReadinessStatus Query(std::uint64_t ticket,std::string& reason)const override{
        if(!requests.count(ticket)){reason="ScriptedVOStreamIn readiness ticket is unknown or released";return WolfScriptedVoiceReadinessStatus::Failed;}
        reason=Unavailable;return WolfScriptedVoiceReadinessStatus::Unsupported;
    }
    void Release(std::uint64_t ticket)noexcept override{if(requests.erase(ticket))--liveRequests;}
    unsigned LiveRequests()const override{return unsigned(requests.size());}
  private:
    std::map<std::uint64_t,WolfScriptedVoiceReadinessRequest> requests;std::uint64_t next=1;
};
}
const char* Wolf2009_ScriptedVoiceReadinessText(WolfScriptedVoiceReadinessStatus value){switch(value){
    case WolfScriptedVoiceReadinessStatus::Pending:return "pending";case WolfScriptedVoiceReadinessStatus::Ready:return "ready";
    case WolfScriptedVoiceReadinessStatus::Failed:return "failed";case WolfScriptedVoiceReadinessStatus::Cancelled:return "cancelled";
    case WolfScriptedVoiceReadinessStatus::Lost:return "lost";case WolfScriptedVoiceReadinessStatus::Unsupported:return "unsupported";}return "unknown";}
std::shared_ptr<WolfScriptedVoiceReadiness> Wolf2009_CreateScriptedVoiceReadiness(const std::string& profile,std::string& reason){
    reason.clear();if(profile!=WholeBankProfile){reason="Unknown scripted-voice readiness profile";return std::shared_ptr<WolfScriptedVoiceReadiness>();}
    return std::make_shared<UnavailableWholeBank>();
}
int Wolf2009_LiveScriptedVoiceReadinessRequests(){return liveRequests;}

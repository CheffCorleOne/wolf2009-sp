// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>
#ifdef WOLF2009_SOURCE_AUDIO_BANK
#include <vector>
#include "ScriptedVoiceBank.h"
#endif
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceScriptedVoiceReadiness.h"
#ifdef WOLF2009_SOURCE_AUDIO_BANK
#include "NativeScriptedVOReadiness.h"
#endif

namespace {
int liveRequests=0;
bool Name(const std::string& value){if(value.empty()||value.size()>1024)return false;
    for(unsigned char c:value)if(c<32||c>=127)return false;return true;}
const char* const WholeBankProfile="ScriptedVOStreamInWholeBankV1";
#ifndef WOLF2009_SOURCE_AUDIO_BANK
const char* const Unavailable="ScriptedVOStreamIn whole-bank SNDS registration is not built (WOLF2009_SOURCE_AUDIO_BANK OFF); node readiness stays unsupported";
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
    bool BankBacked()const override{return false;}
    std::uint64_t BankGeneration()const override{return 0;}
    unsigned BankMembers()const override{return 0;}
  private:
    void Stop(WolfScriptedVoiceReadinessStatus value,const char* why){status=value;reason=why;}
    WolfScriptedVoiceReadinessSource source;int entityNumber=-1,spawnId=0;
    WolfScriptedVoiceReadinessStatus status=WolfScriptedVoiceReadinessStatus::Unsupported;std::string reason;
};
#else
// Thin adapter over the native whole-bank provider. It forwards Update,
// ConsumeReady and Cancel unchanged; it never decides readiness itself.
WolfScriptedVoiceReadinessStatus Map(wolf2009::ScriptedVOReadinessState value){switch(value){
    case wolf2009::ScriptedVOReadinessState::Pending:return WolfScriptedVoiceReadinessStatus::Pending;
    case wolf2009::ScriptedVOReadinessState::Ready:return WolfScriptedVoiceReadinessStatus::Ready;
    case wolf2009::ScriptedVOReadinessState::Failed:return WolfScriptedVoiceReadinessStatus::Failed;
    case wolf2009::ScriptedVOReadinessState::Cancelled:return WolfScriptedVoiceReadinessStatus::Cancelled;
    case wolf2009::ScriptedVOReadinessState::Lost:return WolfScriptedVoiceReadinessStatus::Lost;}
    return WolfScriptedVoiceReadinessStatus::Failed;}
class WholeBankAdapter : public WolfScriptedVoiceReadinessRequest {
  public:
    explicit WholeBankAdapter(std::unique_ptr<WolfScriptedVOReadiness> value):provider(std::move(value)){++liveRequests;Refresh();}
    ~WholeBankAdapter(){provider.reset();--liveRequests;}
    WolfScriptedVoiceReadinessStatus Update()override{provider->Update();Refresh();return status;}
    bool ConsumeReady()override{const bool ready=provider->ConsumeReady();Refresh();return ready;}
    void Cancel(const char* why)override{provider->Cancel(why);Refresh();}
    WolfScriptedVoiceReadinessStatus Status()const override{return status;}
    const std::string& Reason()const override{return reason;}
    const char* Profile()const override{return WholeBankProfile;}
    bool BankBacked()const override{return true;}
    std::uint64_t BankGeneration()const override{return generation;}
    unsigned BankMembers()const override{return members;}
  private:
    void Refresh(){status=Map(provider->State());reason=provider->Reason()?provider->Reason():"";
        if(status==WolfScriptedVoiceReadinessStatus::Ready){const auto& bank=provider->Bank();generation=bank.generation;members=unsigned(bank.members.size());}}
    std::unique_ptr<WolfScriptedVOReadiness> provider;
    WolfScriptedVoiceReadinessStatus status=WolfScriptedVoiceReadinessStatus::Pending;std::string reason;
    std::uint64_t generation=0;unsigned members=0;
};
#endif
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
#ifdef WOLF2009_SOURCE_AUDIO_BANK
    if(!Name(source.brainResource)||!Name(source.definition)||!Name(source.locale)){reason="ScriptedVOStreamIn readiness source text is unqualified";return std::unique_ptr<WolfScriptedVoiceReadinessRequest>();}
    wolf2009::ScriptedVOStreamInSource raw;raw.brainResource=source.brainResource;raw.nodeId=source.nodeId;raw.nodeType=source.nodeType;
    raw.definition=source.definition;raw.locale=source.locale;raw.levelGeneration=source.levelGeneration;raw.eventId=source.eventId;
    idStr why;auto provider=WolfScriptedVOReadiness::Request(raw,owner,why);
    if(!provider){reason=why.IsEmpty()?"ScriptedVOStreamIn whole-bank provider refused the request":why.c_str();return std::unique_ptr<WolfScriptedVoiceReadinessRequest>();}
    std::unique_ptr<WolfScriptedVoiceReadinessRequest> out(new WholeBankAdapter(std::move(provider)));
#else
    std::unique_ptr<WolfScriptedVoiceReadinessRequest> out(new UnavailableWholeBank(source,owner));
#endif
    reason=out->Reason();return out;
}
int Wolf2009_LiveScriptedVoiceReadinessRequests(){return liveRequests;}

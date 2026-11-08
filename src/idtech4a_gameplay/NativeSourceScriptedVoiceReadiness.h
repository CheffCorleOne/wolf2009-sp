// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_SCRIPTED_VOICE_READINESS_H
#define WOLF2009_NATIVE_SOURCE_SCRIPTED_VOICE_READINESS_H
// Narrow consumer seam for brain type19 ScriptedVOStreamIn. The original
// condition is true only after a whole category-8 scripted-voice bank reaches
// its native loaded state. A provider reports that observed result; callers
// never supply it. Ready is not speech, playback, lip-sync or an actor ACK.
//
// The shape follows the per-request game-module provider being built for the
// whole-bank service (one object per type19 request and live owner, Update
// from Think, ConsumeReady at most once, distinct terminal states), so that
// provider can back this seam through a thin adapter.
#include <cstdint>
#include <memory>
#include <string>
class idEntity;

enum class WolfScriptedVoiceReadinessStatus {
    Pending,     // Live request; the bank owner has not observed completion yet.
    Ready,       // Whole-bank registration completed for this live request.
    Failed,      // Bank work or request qualification failed (terminal).
    Cancelled,   // Cancelled by its owner before readiness (terminal).
    Lost,        // Owner generation or backing bank lifetime ended (terminal).
    Unsupported  // No implementation can produce readiness (stays pending in the graph).
};
const char* Wolf2009_ScriptedVoiceReadinessText(WolfScriptedVoiceReadinessStatus);

// Raw type19 source facts, retained independently of any later bank mapping.
struct WolfScriptedVoiceReadinessSource {
    std::string brainResource,definition,locale;
    std::uint32_t nodeId=0,nodeType=19;
    std::uint64_t levelGeneration=0,eventId=0;
};

// One live request. Engine thread only. Update observes the provider and may
// change the status; it never reports Ready without completed bank work.
// ConsumeReady returns true at most once, only after Update reported Ready.
// Cancel and destruction release the request; neither can produce Ready.
class WolfScriptedVoiceReadinessRequest {
  public:
    virtual ~WolfScriptedVoiceReadinessRequest(){}
    virtual WolfScriptedVoiceReadinessStatus Update()=0;
    virtual bool ConsumeReady()=0;
    virtual void Cancel(const char* reason)=0;
    virtual WolfScriptedVoiceReadinessStatus Status()const=0;
    virtual const std::string& Reason()const=0;
    virtual const char* Profile()const=0;
};

// Explicit profile "ScriptedVOStreamInWholeBankV1". The current build has no
// whole-bank SNDS registration, so every request it returns stays Unsupported.
// There is no timer, test hook or caller boolean that reports Ready. Returns
// null (with reason) only for an unknown profile or an absent live owner.
std::unique_ptr<WolfScriptedVoiceReadinessRequest> Wolf2009_RequestScriptedVoiceReadiness(
    const std::string& profile,const WolfScriptedVoiceReadinessSource&,idEntity* owner,std::string& reason);
int Wolf2009_LiveScriptedVoiceReadinessRequests();
#endif

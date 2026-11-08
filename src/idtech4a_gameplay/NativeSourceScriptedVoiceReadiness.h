// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_SCRIPTED_VOICE_READINESS_H
#define WOLF2009_NATIVE_SOURCE_SCRIPTED_VOICE_READINESS_H
// Narrow consumer seam for brain type19 ScriptedVOStreamIn. The original
// condition is true only after a whole category-8 scripted-voice bank reaches
// its native loaded state. A provider reports that observed result; callers
// never supply it. Ready is not speech, playback, lip-sync or an actor ACK.
#include <cstdint>
#include <memory>
#include <string>

enum class WolfScriptedVoiceReadinessStatus {
    Pending,     // Request is live; the bank owner has not observed completion yet.
    Ready,       // Whole-bank registration completed for this live request.
    Failed,      // Bank work failed; the request will not become ready.
    Cancelled,   // Released by its owner before readiness.
    Lost,        // A ready bank lost its backing lifetime.
    Unsupported  // No implementation can produce readiness for this request.
};
const char* Wolf2009_ScriptedVoiceReadinessText(WolfScriptedVoiceReadinessStatus);

struct WolfScriptedVoiceReadinessRequest {
    // Raw source facts, retained independently of any later bank mapping.
    std::string definition,ownerName,brainResource,locale;
    std::uint64_t levelGeneration=0,eventId=0;
    std::uint32_t sourceNodeId=0,sourceNodeType=0;
    unsigned ownerSpawnId=0;
};

// Game-thread interface. Request registers one owner's interest and returns
// its current status; it may report Ready only from completed bank work.
// Query is effect-free. Release ends the caller's interest exactly once and
// must not throw. A ticket is never reused while the provider lives.
class WolfScriptedVoiceReadiness {
  public:
    virtual ~WolfScriptedVoiceReadiness(){}
    virtual const char* Profile()const=0;
    virtual WolfScriptedVoiceReadinessStatus Request(const WolfScriptedVoiceReadinessRequest&,
        std::uint64_t& ticket,std::string& reason)=0;
    virtual WolfScriptedVoiceReadinessStatus Query(std::uint64_t ticket,std::string& reason)const=0;
    virtual void Release(std::uint64_t ticket)noexcept=0;
    virtual unsigned LiveRequests()const=0;
};

// Explicit profile "ScriptedVOStreamInWholeBankV1". The current build has no
// whole-bank SNDS registration, so its provider keeps every live request
// Unsupported. It has no timer, test hook or caller boolean that reports Ready.
std::shared_ptr<WolfScriptedVoiceReadiness> Wolf2009_CreateScriptedVoiceReadiness(
    const std::string& profile,std::string& reason);
int Wolf2009_LiveScriptedVoiceReadinessRequests();
#endif

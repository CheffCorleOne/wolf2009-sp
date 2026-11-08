// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SCRIPTED_VO_READINESS_H
#define WOLF2009_NATIVE_SCRIPTED_VO_READINESS_H
#include "ScriptedVoiceBank.h"
#include <memory>
class idEntity;
class idStr;

// Game-module type19 ScriptedVOStreamIn readiness provider.
//
// One instance belongs to one brain host request: a type19 source node, its
// raw mDefName, the explicit locale and one live owner entity (entity number +
// native spawn generation). Ready is produced only by the core whole-bank
// service (Category8SndsMp3V1) through its private filesystem capability, and
// the bank stays retained while this object holds it.
//
// Host usage (no timer or caller boolean can advance node 29):
//   auto vo = WolfScriptedVOReadiness::Request(source, owner, reason);
//   each Think/Tick:  vo->Update();  if (vo->ConsumeReady()) advance node 29;
//   Failed / Cancelled / Lost are terminal and distinct; inspect Reason().
// ConsumeReady() is true at most once, after re-checking the owner generation
// and the backing bank. Destroying the object, Cancel(), owner deletion or
// replacement and bank loss release the bank lease. Engine thread only.
class WolfScriptedVOReadiness {
  public:
    // Null only when the owner is absent; an unqualified source yields an
    // object that is already Failed with a reason.
    static std::unique_ptr<WolfScriptedVOReadiness> Request(const wolf2009::ScriptedVOStreamInSource &source,
                                                            idEntity *owner, idStr &reason);
    ~WolfScriptedVOReadiness();
    WolfScriptedVOReadiness(const WolfScriptedVOReadiness &) = delete;
    WolfScriptedVOReadiness &operator=(const WolfScriptedVOReadiness &) = delete;

    wolf2009::ScriptedVOReadinessState Update();
    bool ConsumeReady();
    void Cancel(const char *reason = NULL);
    // Optional subscription; delivered once from Update instead of ConsumeReady.
    void SetListener(wolf2009::ScriptedVOReadinessListener *listener);

    wolf2009::ScriptedVOReadinessState State() const;
    const char *StateName() const;
    const char *Reason() const;
    // Bank identity behind Ready (generation, package, mount epoch, members).
    const wolf2009::ScriptedVoiceBankManifest &Bank() const;
    const wolf2009::ScriptedVOStreamInReadiness &Readiness() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfScriptedVOReadiness(std::unique_ptr<Impl>);
};

// Live provider objects / held bank leases in this game module (teardown QA).
unsigned Wolf2009_LiveScriptedVOReadiness();
unsigned Wolf2009_HeldScriptedVOBankLeases();
#endif

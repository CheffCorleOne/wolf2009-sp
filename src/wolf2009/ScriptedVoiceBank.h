// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SCRIPTED_VOICE_BANK_H
#define WOLF2009_SCRIPTED_VOICE_BANK_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace wolf2009 {

// Explicit profile for the category-8 scripted-voice request issued by brain
// type19 ScriptedVOStreamIn. The measured original Enter path expands its
// mDefName through common +0x118: an input without '/' becomes
// "streampacks/<definition>_vo_<vo_lang>.spk". This profile admits only that
// branch, a plain lowercase [a-z0-9_] definition and the qualified English locale.
// It does not read the entityDef detailPack, fall back to another locale or
// strip an extension: definitions containing '.', '/' or other bytes refuse.
enum class ScriptedVoiceBankProfile { Unspecified = 0, Category8SndsMp3V1 = 1 };
struct ScriptedVoicePack {
    ScriptedVoiceBankProfile profile = ScriptedVoiceBankProfile::Unspecified;
    std::string definition, locale, package;
};
// Throws std::runtime_error on an unqualified definition/locale.
ScriptedVoicePack ExpandScriptedVoicePack(const std::string &definition, const std::string &locale);
const char *ScriptedVoiceBankProfileName(ScriptedVoiceBankProfile);

// Text manifest returned by the native core bank service through its private
// filesystem capability. It describes an observed result; parsing it never
// creates readiness. Values are percent-encoded outside [A-Za-z0-9_./-].
struct ScriptedVoiceBankMember {
    std::size_t index = 0;
    std::string rawName, cacheName;
    std::uint64_t bytes = 0, fingerprint = 0, frames = 0, samples = 0;
};
struct ScriptedVoiceBankManifest {
    enum class State { Invalid, Ready, Failed, Lost } state = State::Invalid;
    ScriptedVoiceBankProfile profile = ScriptedVoiceBankProfile::Unspecified;
    std::string definition, locale, package, reason, mountLabel;
    std::uint64_t generation = 0, sourceBytes = 0, sourceFingerprint = 0, mountEpoch = 0;
    std::uint64_t leases = 0, encodedBytes = 0;
    std::vector<ScriptedVoiceBankMember> members;
};
std::string SerializeScriptedVoiceBankManifest(const ScriptedVoiceBankManifest &);
// Strict: unknown/missing/duplicate keys, a member count mismatch, a Ready
// manifest without members or a Failed manifest with ownership fields throw.
ScriptedVoiceBankManifest ParseScriptedVoiceBankManifest(const std::string &text, std::size_t maximumBytes = 1u << 20);
const char *ScriptedVoiceBankStateName(ScriptedVoiceBankManifest::State);

// ---------------------------------------------------------------------------
// Type19 readiness bound to a retained whole bank.
//
// The host supplies the actual bank service and owner liveness. Readiness can
// only come from an Acquire() that returned a live bank lease; no timer, frame
// count or caller boolean can advance it. Pending, Failed, Cancelled and Lost
// stay distinct. The ready event is delivered at most once, and only while the
// owner and the backing lease are still live.
enum class ScriptedVOReadinessState { Pending, Ready, Failed, Cancelled, Lost };
const char *ScriptedVOReadinessStateName(ScriptedVOReadinessState);

struct ScriptedVOStreamInSource {
    std::string brainResource;          // raw typed BRAI resource identity
    std::uint32_t nodeId = 0, nodeType = 19;
    std::string definition, locale;     // raw mDefName and explicit locale
    std::uint64_t levelGeneration = 0, eventId = 0;
};
struct ScriptedVOOwner {
    int entityNumber = -1, spawnId = 0; // host-native generation identity
    std::string name;
};

class ScriptedVOBankLease {
  public:
    virtual ~ScriptedVOBankLease() {}
    virtual const ScriptedVoiceBankManifest &Manifest() const = 0;
    // Re-observes the retained backing lifetime (mount, cache identities).
    virtual bool StillReady(std::string &reason) = 0;
};
class ScriptedVOReadinessHost {
  public:
    virtual ~ScriptedVOReadinessHost() {}
    virtual bool OwnerLive(const ScriptedVOOwner &) = 0;
    // Performs or joins the complete bank work. Returns a lease only for an
    // actually Ready bank; otherwise null with a reason (Failed).
    virtual std::unique_ptr<ScriptedVOBankLease> Acquire(const ScriptedVoicePack &, std::string &reason) = 0;
};
class ScriptedVOReadinessListener {
  public:
    virtual ~ScriptedVOReadinessListener() {}
    virtual void OnScriptedVOReady(const class ScriptedVOStreamInReadiness &) = 0;
};

class ScriptedVOStreamInReadiness {
  public:
    // Validates the explicit source profile; an unqualified request starts Failed.
    ScriptedVOStreamInReadiness(const ScriptedVOStreamInSource &, const ScriptedVOOwner &);
    ScriptedVOStreamInReadiness(const ScriptedVOStreamInReadiness &) = delete;
    ScriptedVOStreamInReadiness &operator=(const ScriptedVOStreamInReadiness &) = delete;
    // Advances only from observed host results. Must not be called recursively.
    ScriptedVOReadinessState Update(ScriptedVOReadinessHost &);
    // True exactly once, after a fresh owner/backing recheck.
    bool ConsumeReady(ScriptedVOReadinessHost &);
    // A pending or ready request is cancelled and its lease released.
    void Cancel(const char *reason = "cancelled by owner");
    // Optional: delivered once from Update instead of ConsumeReady.
    void SetListener(ScriptedVOReadinessListener *listener) { listener_ = listener; }

    ScriptedVOReadinessState State() const { return state_; }
    const std::string &Reason() const { return reason_; }
    const ScriptedVOStreamInSource &Source() const { return source_; }
    const ScriptedVOOwner &Owner() const { return owner_; }
    const ScriptedVoicePack &Pack() const { return pack_; }
    bool Delivered() const { return delivered_; }
    bool HoldsBank() const { return bool(lease_); }
    // Identity of the bank that produced Ready (retained after Lost/Cancelled).
    const ScriptedVoiceBankManifest &BankManifest() const { return manifest_; }

  private:
    void Terminal(ScriptedVOReadinessState, const std::string &);
    bool Recheck(ScriptedVOReadinessHost &);
    ScriptedVOStreamInSource source_;
    ScriptedVOOwner owner_;
    ScriptedVoicePack pack_;
    ScriptedVOReadinessState state_ = ScriptedVOReadinessState::Pending;
    std::string reason_;
    std::unique_ptr<ScriptedVOBankLease> lease_;
    ScriptedVoiceBankManifest manifest_;
    ScriptedVOReadinessListener *listener_ = nullptr;
    bool delivered_ = false, updating_ = false;
};

int RunScriptedVoiceBankTests();
} // namespace wolf2009
#endif

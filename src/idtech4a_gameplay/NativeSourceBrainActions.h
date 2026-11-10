// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_BRAIN_ACTIONS_H
#define WOLF2009_NATIVE_SOURCE_BRAIN_ACTIONS_H
// Game-private native handlers for single-procedure brain actions decoded by
// wolf2009::DecodeBrainActionV1. Each action is admitted under its explicit
// profile name (BrainActionSpec::profile) and produces one receipt. A null
// subject is the original's no-op, never an error. Hosts (the source AI shell,
// the shake_train script runner) supply subject resolution; actors expose the
// recovered idAI fields through WolfSourceAIScriptState. Engine thread only.
#include "BrainActions.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
class idEntity;
class idPlayer;
class WolfScriptedVoiceReadinessRequest;

// Recovered idAI/idActor script fields (Gamex86 7cb1cbed..., docs/AI-ACTOR.md).
struct WolfSourceAIScriptState {
    // SetScriptedManual 0x201ac600: ai+0x954 bit 0x40, +0x957 bit 0x20
    // (announcements off), +0x1c84/+0x1e14 bit 0x4 (ignore enemies), +0xf39
    // (idle look for enemy). Restored from spawn args on leave.
    bool scriptedManual = false, announcementsOff = false, ignoreEnemies = false, idleLookForEnemy = false;
    bool spawnDisableAnnouncements = false, spawnIdleLookForEnemy = false;
    // AnimOverrideIdle 0x201f5930: ai+0x37b4 string and ai+0x95b bit 0x80.
    std::string idleOverride;
    bool idleStale = false;
    // Consumed by the leg state machine 0x20166d00 (idle branch): clears the
    // stale bit, takes the name from +0x6a8 (0x20168550: the override when it
    // is non-empty) into ai+0x3790 and plays it unless already playing.
    std::string currentIdle;
    unsigned idleConsumptions = 0;
    std::string idlePlayback;      // native playback outcome of the last consumption
    bool idleClipDeclared = false; // name resolves in the actor's model animations
    // AnimOverrideDeath 0x201f5c60: ai+0x388c. Its consumer (death animation
    // choice +0x69c) is outside this profile.
    std::string deathOverride;
    // NoTargetSet 0x201f2d70: ent+0x158 bit 0. SetUndying 0x201f2fa0: actor+0x5f9.
    bool notarget = false, undying = false;
    unsigned manualEnters = 0, manualLeaves = 0, scriptShows = 0, manualNoops = 0;
    std::vector<std::string> postedStates; // Wait_ScriptedManual, initial state
    std::string movementStop;              // recorded outcome of the +0x4e0 stop
};

// A native source AI actor as seen by the action handlers.
class WolfSourceAIActionTarget {
  public:
    virtual ~WolfSourceAIActionTarget() {}
    virtual const std::string &SourceName() const = 0;
    virtual idEntity *NativeBody() const = 0;
    virtual bool Dead() const = 0;
    virtual bool Hidden() const = 0;
    // ai+0x955 bit 0x2: set by idAI Spawn (0x20158694) and cleared at the start
    // of idAI PostSpawn (0x2015d828). SetScriptedManual does not Show while set.
    virtual bool PostSpawnPending() const = 0;
    // Native Show (idAI Show 0x20157100): unhide body, head and attachments and
    // restore saved think flags.
    virtual void ScriptShow() = 0;
    virtual WolfSourceAIScriptState &Script() = 0;
    // SourceAIScriptedAnimV1 (stage 2). The defaults are an actor without an
    // admitted clip profile: Animate and KillTarget refuse on it.
    virtual bool ScriptedAnimAdmitted() const { return false; }
    // ScriptedAnim +0x468 including Begin (Show unless ai+0x955 bit 0x2).
    virtual bool ScriptedAnim(const std::string &, int, int, bool, const std::string &, std::string &effect) {
        effect = "actor has no SourceAIScriptedAnimV1 clip profile";
        return false;
    }
    // +0x33c on the actor's current clip.
    virtual bool AnimDoneByName(const std::string &, int, int &remainingMs, std::string &current) const {
        remainingMs = -1;
        current.clear();
        return true;
    }
    // <kill> (0x20184e50) and the death path.
    virtual bool Kill(bool, std::string &effect) {
        effect = "actor has no SourceAIScriptedAnimV1 death path";
        return false;
    }
};

// Host-side receipt of the player bytes the actions wrote. The authoritative
// state is the native player's WolfSourcePlayerFlags (NativeSourcePlayer.h).
struct WolfSourcePlayerScriptState {
    bool weaponsLocked = false;      // player+0x958 bit 3 = !mEnabled
    // "weapons-enabled"/"weapons-disabled": the native flag write. The GPL
    // enableWeapon/disableWeapon events are not sent: in this build the
    // original player events are no-ops (docs/MISSION-SYSTEMS.md §4.3).
    std::string lastWeaponEvent;
    bool weaponEventHandled = false; // the native source player flag was written
    bool undying = false;            // actor+0x5f9, consumed by native player Damage
    bool locked = false;             // player+0x957 bit 0x10
    unsigned creditsEvents = 0;
    std::string lastCreditsEvent;    // enableCreditsScreen / disableCreditsScreen
    bool creditsHandled = false;     // native player responded to the event
};

struct WolfSourceSubject {
    enum Kind { Null, AI, Player, Entity } kind = Null;
    WolfSourceAIActionTarget *ai = nullptr;
    idEntity *entity = nullptr;
};
class WolfSourceBrainActionResolver {
  public:
    virtual ~WolfSourceBrainActionResolver() {}
    virtual WolfSourceSubject Resolve(const wolf2009::BrainEntityReference &) = 0;
    // Player-side state owned by the host for this level generation.
    virtual WolfSourcePlayerScriptState &PlayerState() = 0;
};

struct WolfSourceBrainActionReceipt {
    std::string brain, profile, action, subject, outcome, effect;
    std::uint32_t node = 0, type = 0;
    bool probe = false;
    int timeMs = -1;
};

// Entity +0x159 bit 7 (SetUsable). Values are kept per native entity
// generation until the source session ends; -1 means never written by a
// source action (the per-class spawn defaults are not mapped).
void Wolf2009_SetSourceEntityUsable(idEntity*, bool usable);
int Wolf2009_SourceEntityUsable(const idEntity*);
void Wolf2009_ClearSourceEntityFlags();
int Wolf2009_LiveSourceEntityFlags();

// PlayerHasWeapon (448): raw value from the local player's source inventory.
// No player or no source inventory gives raw false, as in the original.
struct WolfSourceConditionReceipt {
    std::string brain, profile, condition, argument, outcome, reason;
    std::size_t record = 0;
    bool raw = false;
    int timeMs = -1;
};
bool Wolf2009_EvaluateSourceBrainCondition(const wolf2009::BrainConditionSpec &, WolfSourceConditionReceipt &);

// Applies one immediate action (every admitted kind except type 19). Returns
// false only for a refused/unsupported action; a null subject is applied as
// the original no-op and reported with outcome "null-subject".
bool Wolf2009_ApplySourceBrainAction(const wolf2009::BrainActionSpec &, WolfSourceBrainActionResolver &,
                                     WolfSourceBrainActionReceipt &);
// AI animation update: consumes a stale idle override exactly as the recovered
// idle branch does, then resolves the clip against the actor's declared model
// animations through `declared`. Playback itself is an explicit refusal in the
// reference-pose actor profile. Returns true when a consumption happened.
bool Wolf2009_UpdateSourceAIIdle(WolfSourceAIActionTarget &, bool (*declared)(const void *, const std::string &),
                                 const void *context);

// Animate 51 / AnimFinished 50 task state (SourceAIScriptedAnimV1). Enter
// 0x201f5290: mAnimSucceeded = false, mAnimFailTime = now + 1, then on an idAI
// mAnimSucceeded = ScriptedAnim(name, mBlendFrames, mNumLoops, mEndWithIdle).
struct WolfSourceAnimateTask {
    bool entered = false, succeeded = false, subjectAI = false;
    int enterTimeMs = -1, failTimeMs = -1;
    std::string subject;
    unsigned evaluations = 0;
    bool lastValue = false;
    int lastRemainingMs = -1, lastFalseRemainingMs = -1, firstTrueMs = -1, firstTrueRemainingMs = -1;
    std::string lastCurrent, lastDetail;
};
// Returns false for a refused Enter (non-AI entity subject, unadmitted actor).
bool Wolf2009_EnterSourceAnimate(const wolf2009::BrainActionSpec &, WolfSourceBrainActionResolver &,
                                 WolfSourceBrainActionReceipt &, WolfSourceAnimateTask &, const std::string &task);
// AnimFinished 50 Evaluate 0x201ebe70 against the resolved mActor.
bool Wolf2009_EvaluateSourceAnimFinished(const wolf2009::BrainActionSpec &, WolfSourceBrainActionResolver &,
                                         WolfSourceAnimateTask &);

// Type19 ScriptedVOStreamIn as issued at node entry. The original Enter
// requests the stream for (mDefName, owner) and returns; the network does not
// wait unless an output references the embedded condition. A stream owns the
// readiness request; Update observes Ready exactly once and qualifies the bank
// inventory against the measured pack profile when one is declared.
struct WolfSourceVoiceStreamReceipt {
    std::string brain, definition, owner, state, reason;
    std::uint32_t node = 0, expectedMembers = 0, members = 0;
    std::uint64_t bankGeneration = 0;
    bool bankBacked = false, readyConsumed = false, inventoryQualified = false;
    int requestTimeMs = -1, readyTimeMs = -1;
};
class WolfSourceVoiceStreams {
  public:
    WolfSourceVoiceStreams();
    ~WolfSourceVoiceStreams();
    // Owner null: the original's "Can't find ent" warning; recorded, no request.
    void Start(const wolf2009::BrainActionSpec &, const std::string &brainResource, idEntity *owner,
               std::uint64_t levelGeneration, std::uint64_t eventId);
    void Update();
    void CancelAll(const char *reason);
    std::vector<WolfSourceVoiceStreamReceipt> Receipts() const;
    unsigned Live() const;
    WolfSourceVoiceStreams(const WolfSourceVoiceStreams &) = delete;
    WolfSourceVoiceStreams &operator=(const WolfSourceVoiceStreams &) = delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Measured English trainyard interrogator packs (TRAINYARD-CHAIN §4): exact
// member counts that a Ready bank must present before its stream is qualified.
std::uint32_t Wolf2009_MeasuredScriptedVoiceMembers(const std::string &definition);
int Wolf2009_LiveSourceVoiceStreams();
#endif

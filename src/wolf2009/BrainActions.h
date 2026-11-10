// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_BRAIN_ACTIONS_H
#define WOLF2009_BRAIN_ACTIONS_H
// Portable argument decoding for single-procedure brain actions and a held
// stepping kernel. Nothing here touches an entity. Native hosts apply the
// decoded actions under their own explicit profiles (NativeSourceBrainActions).
//
// Static contracts (hash-identified Gamex86.dll 7cb1cbed...; docs/BRAIN-TASKS.md
// and docs/AI-ACTOR.md): property names, kinds and defaults come from each
// descriptor's property table; bool kind 0x18 is true only for an ASCII
// case-insensitive "true"; entity references follow the Focus grammar.
#include "BrainFlow.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wolf2009 {
// Raw node header and key/value arguments, verbatim and in source order.
struct BrainNodeArguments {
    std::uint32_t type = 0, id = 0, serializedFlags = 0, primaryCondition = 0;
    std::vector<std::pair<std::string, std::string> > values;
    const std::string *Find(const std::string &exactKey) const;
};
// Throws std::runtime_error for a malformed header, a second embedded
// condition or a duplicate key.
BrainNodeArguments ReadBrainNodeArguments(const BrainGraph &, std::size_t node);

// Entity reference subset (Focus grammar, parse 0x2020f920). Keywords match
// ASCII case-insensitively and exactly. This profile admits None, Me, Player
// and plain entity names; every other keyword, an indexed keyword and '@'
// resolution are refused instead of guessed.
enum class BrainEntityMode { None, Me, Player, Named };
struct BrainEntityReference {
    BrainEntityMode mode = BrainEntityMode::Me;
    std::string raw, name;
};
BrainEntityReference ParseBrainEntityReferenceV1(const std::string &raw);
// Kind 0x18 bool text (0x2020ff40) without '@' resolution.
bool ParseBrainBoolTextV1(const std::string &raw);

// SourceBrainActionsV1: the node types whose Enter is a single recovered
// procedure. Values hold the effective argument after the descriptor default.
enum class BrainActionKind : std::uint32_t {
    ScriptedVOStreamIn = 19,
    SetScriptedManual = 33,
    // SourceAIScriptedAnimV1 (docs/AI-PRESENTATION.md §2). Animate embeds
    // AnimFinished 50 as its primary condition; its saved runtime fields
    // mAnimSucceeded/mAnimFailTime are reset at Enter and only retained here.
    Animate = 51,
    AnimOverrideIdle = 54,
    AnimOverrideDeath = 57,
    KillTarget = 69,
    SetUsable = 75,
    NoTargetSet = 102,
    LockPlayer = 185,
    UnlockPlayer = 186,
    EnableDamage = 252,
    SetUndying = 253,
    EnablePlayerWeapons = 357,
    EnableCredits = 468
};
struct BrainActionSpec {
    std::uint32_t nodeId = 0, nodeType = 0;
    BrainActionKind kind = BrainActionKind::ScriptedVOStreamIn;
    // mActor/mOwner. EnablePlayerWeapons and EnableCredits always act on the
    // local player and carry mode Player here.
    BrainEntityReference subject;
    bool value = false, valueDefaulted = false;     // kind 0x18 argument
    std::string rawValue;                           // verbatim bool text ("fasle")
    std::string text;                               // mDefName/mAnimName/mDeathAnim
    bool textDefaulted = false;
    // Kind 0x15 integer text (Animate/AnimFinished mBlendFrames, mNumLoops).
    std::int32_t blendFrames = 0, numLoops = 0;
    bool blendDefaulted = false, loopsDefaulted = false;
    // Saved task state carried by the record (never used as behavior).
    std::vector<std::pair<std::string, std::string> > savedState;
    std::string profile;                            // native admission profile name
};
const char *BrainActionKindName(BrainActionKind);
bool IsBrainActionType(std::uint32_t type);
// Returns false with a reason for any other type, an unknown key, '@' text,
// an unadmitted reference mode or a primary condition outside the descriptor.
bool DecodeBrainActionV1(const BrainGraph &, std::size_t node, BrainActionSpec &, std::string &reason);

// SourceBrainConditionsV1: explicit condition records under an output. The
// record layout is "<type> ^<type> <flags> key value ...". Only flag word 0 is
// admitted: the bit that inverts a condition (NOT) is not mapped here, so any
// other flag word is an explicit refusal rather than a guessed inversion.
enum class BrainConditionKind : std::uint32_t {
    PlayerHasWeapon = 448
};
struct BrainConditionSpec {
    std::size_t record = 0;
    std::uint32_t type = 0, flags = 0;
    BrainConditionKind kind = BrainConditionKind::PlayerHasWeapon;
    std::string text;              // mWeaponName
    bool textDefaulted = false;
    std::string profile;
};
const char *BrainConditionKindName(BrainConditionKind);
bool DecodeBrainConditionV1(const BrainGraph &, std::size_t record, BrainConditionSpec &, std::string &reason);

// SourceAIHuntV1 argument decoding (docs/AI-COMBAT.md §3). These types are
// deliberately outside IsBrainActionType: the native single-procedure table
// keeps refusing Hunt 43 and IgnoreEnemiesSet 49 until the native layer
// (AI-COMBAT §7.2) admits them with its own handlers. NoTargetSet 102 stays in
// the action table above (SourceEntityNoTargetV1).
enum class BrainCombatTaskKind : std::uint32_t {
    Hunt = 43,             // table 0x205c8d68, embedded EnemyDead 42 as primary
    IgnoreEnemiesSet = 49  // table 0x205c9018
};
struct BrainCombatTaskSpec {
    std::uint32_t nodeId = 0, nodeType = 0;
    BrainCombatTaskKind kind = BrainCombatTaskKind::Hunt;
    // Hunt: mHunter of the embedded EnemyDead (task +0x6c, default Me).
    // IgnoreEnemiesSet: mActor (default Me).
    BrainEntityReference subject;
    BrainEntityReference prey;                       // Hunt mPrey (default MyEnemy, refused)
    bool locked = false, lockedDefaulted = true;     // Hunt mLocked (kind 0x18, "false")
    bool updatePosition = false, updateDefaulted = true; // Hunt mUpdatePosition (kind 0x18, "false")
    std::string rawLocked = "false", rawUpdatePosition = "false";
    bool value = false, valueDefaulted = false;      // IgnoreEnemiesSet mIgnore (kind 0x18, "true")
    std::string rawValue;
    std::uint32_t primaryCondition = 0;              // 42 for Hunt, 0 otherwise
    std::string profile;
};
const char *BrainCombatTaskKindName(BrainCombatTaskKind);
bool IsBrainCombatTaskType(std::uint32_t type);
// Refuses other types, unknown keys, '@' text and unadmitted references (the
// Hunt default prey "MyEnemy" is outside the reference profile and refuses).
bool DecodeBrainCombatTaskV1(const BrainGraph &, std::size_t node, BrainCombatTaskSpec &, std::string &reason);

// Combat conditions (AI-COMBAT §3.5). Property tables (Static): EnemyDead
// mHunter (Me); Aware, HasTakenPain, CombatJustStarted mActor (Me);
// HasTakenPain mMinThreshold/mMaxThreshold kind 0x15 (default "-1" each);
// CombatJustStarted saved fields mWasInCombat/mIsInCombat (kind 0x06 native
// bool) and mTimeChecked (kind 0x04 int). NOT is BRAINFLAGS bit 2
// (docs/BRAIN-TASKS.md §1.1); an embedded condition carries the fixed word
// 1024 and is never inverted.
enum class BrainCombatConditionKind : std::uint32_t {
    EnemyDead = 42,
    Aware = 103,
    HasTakenPain = 104,
    CombatJustStarted = 108
};
struct BrainCombatConditionSpec {
    BrainCombatConditionKind kind = BrainCombatConditionKind::EnemyDead;
    bool embedded = false;
    std::size_t node = BrainRecord::None(), record = BrainRecord::None();
    std::uint32_t flags = 0;
    bool invert = false;
    BrainEntityReference subject;                    // mHunter or mActor
    std::int32_t minThreshold = -1, maxThreshold = -1;
    bool minDefaulted = true, maxDefaulted = true;
    bool wasInCombat = false, isInCombat = false;    // saved state, kind 0x06
    std::int32_t timeChecked = 0;                    // saved state, kind 0x04
    std::string profile;
};
const char *BrainCombatConditionKindName(BrainCombatConditionKind);
// Embedded primary condition of a node: Hunt 43 (EnemyDead) and the roots
// OnCombat 7, OnAware 111 and OnPain 112 (their records carry only the
// embedded condition's keys). Other owners refuse.
bool DecodeBrainCombatPrimaryV1(const BrainGraph &, std::size_t node, BrainCombatConditionSpec &, std::string &reason);
// Explicit output condition record "<type> ^<type> <flags> key value ...";
// flag words 0 and 2 (NOT) are admitted, every other bit refuses.
bool DecodeBrainCombatConditionV1(const BrainGraph &, std::size_t record, BrainCombatConditionSpec &, std::string &reason);

// ---------------------------------------------------------------------------
// Held stepping kernel (profile SourceHeldStepV1). It follows the inspected
// Tick order of the BrainTask runtime (root scan by priority, first root
// transition disables further root scans in that Tick, normal pass on the
// active task, probe pass and flush of the active task, repeat while a
// normal transition happened). Before committing a normal transition it asks
// the host to admit the destination and every probe destination of that
// destination. A refusal holds the network at its current task: no exit,
// no flow-state change and no side effect.
class BrainHeldStepHost {
  public:
    virtual ~BrainHeldStepHost() {}
    // Root declaration: true with a priority for an admitted root type.
    virtual bool Root(const BrainGraph &, std::size_t node, std::uint32_t &priority) = 0;
    virtual BrainConditionValue RootReady(const BrainGraph &, std::size_t root) = 0;
    virtual BrainConditionValue Primary(const BrainGraph &, std::size_t node) = 0;
    virtual BrainConditionValue Explicit(const BrainGraph &, std::size_t record) = 0;
    virtual bool Admits(const BrainGraph &, std::size_t node, std::string &reason) = 0;
    // Effects. Throwing poisons the stepper.
    virtual void Enter(const BrainGraph &, std::size_t node, bool probe) = 0;
    virtual void ExitRoot(const BrainGraph &, std::size_t root) = 0;
};
struct BrainHeldStepResult {
    enum Status { Idle, Advanced, Held, Failed } status = Idle;
    std::size_t transitions = 0, probes = 0;
    std::size_t heldSource = BrainRecord::None(), heldNode = BrainRecord::None();
    std::string reason;
};
class BrainHeldStepper {
  public:
    BrainHeldStepper(const BrainGraph &, BrainHeldStepHost &);
    BrainHeldStepper(const BrainHeldStepper &) = delete;
    BrainHeldStepper &operator=(const BrainHeldStepper &) = delete;
    BrainHeldStepResult Tick(std::size_t transitionBudget = 50);
    std::size_t Active() const { return active_; }
    bool Failed() const { return failed_; }
    const std::string &Failure() const { return failure_; }
  private:
    struct Single;
    bool Prepare();
    bool Admit(std::size_t node, std::size_t &refused, std::string &reason);
    void Enter(std::size_t node, bool probe);
    const BrainGraph &graph_;
    BrainHeldStepHost &host_;
    BrainFlowState flow_;
    std::vector<std::uint32_t> nodeFlags_;
    std::vector<std::size_t> roots_;
    std::size_t active_ = BrainRecord::None();
    bool prepared_ = false, failed_ = false, busy_ = false;
    std::string failure_;
};
int RunBrainActionTests();
} // namespace wolf2009
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_AI_COMBAT_H
#define WOLF2009_SOURCE_AI_COMBAT_H
// SourceAIHuntV1 portable layer (docs/AI-COMBAT.md §7.1). No entity, clock or
// file access: hosts supply world facts and receive requests. Every rule the
// page leaves Unknown is an explicit input or a named refusal; a stand-in is
// only taken when the caller selects it by name, and the name is reported.
//
// 1. Brain handlers: Hunt 43, IgnoreEnemiesSet 49, NoTargetSet 102 (§3.2-3.4)
//    and the conditions EnemyDead 42, Aware 103, HasTakenPain 104 and
//    CombatJustStarted 108 (§3.5).
// 2. Tactical tables: the 20-value enum, mask keys and defaults, the closed
//    filter predicates and the selector priority (§5.3).
// 3. Burst model: the weapon controller 0x20178380 as a step function of
//    time, stats, a supplied random source and fire-predicate inputs (§5.6).
#include "BrainActions.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wolf2009 {
namespace aicombat {

// Three-valued input for facts the original computes from unread code.
enum class Tri { False, True, Unknown };

// ---------------------------------------------------------------------------
// 1. Brain handlers
// What the host's resolver found for an entity reference (dynamic cast
// classes used by the Enter/Evaluate procedures).
enum class SubjectClass { Missing, Entity, Actor, AI };

// Hunt procedure 0x201aa390(prey, locked, updatePosition, 1). Its body (§3.3)
// is native enemy state (SourceAIEnemyStateV1, §7.2); this layer only issues it.
struct HuntCall {
    std::uint32_t node = 0;
    BrainEntityReference hunter, prey;
    bool locked = false, updatePosition = false;
};
class CombatTaskHost {
  public:
    virtual ~CombatTaskHost() {}
    virtual SubjectClass Classify(const BrainEntityReference &) = 0;
    // Hunt Enter step 2: virtual +0x45c, then +0x460(1) on the hunter.
    virtual void StopScriptedMove(const BrainEntityReference &hunter) = 0;
    virtual void Hunt(const HuntCall &) = 0;
    // IgnoreEnemiesSet: ClearEnemy 0x201a4680 (only when mIgnore is true),
    // then senses.fl.ignoreEnemies (ai+0x1c84 bit 0x4) = mIgnore.
    virtual void ClearEnemy(const BrainEntityReference &ai) = 0;
    virtual void SetIgnoreEnemies(const BrainEntityReference &ai, bool value) = 0;
    // NoTargetSet: entity byte +0x158 bit 0 (notarget) = mNoTarget.
    virtual void SetNoTarget(const BrainEntityReference &entity, bool value) = 0;
};
struct CombatTaskReceipt {
    enum Outcome { Applied, NoOp, Refused } outcome = Refused;
    std::uint32_t node = 0, type = 0;
    std::string action, profile, effect;
    // All three tasks complete in the Tick of their Enter and have no Exit
    // (slot 3 is `ret`) and no per-frame Update.
    bool completesInEnterTick = true, hasExit = false;
};
CombatTaskReceipt EnterCombatTaskV1(const BrainCombatTaskSpec &, CombatTaskHost &);
CombatTaskReceipt EnterNoTargetSetV1(const BrainActionSpec &, CombatTaskHost &);

// Condition inputs. Each evaluator returns True/False, or Unsupported when an
// input it needs is Unknown and no named stand-in was selected.
struct EnemyDeadInputV1 {
    SubjectClass hunter = SubjectClass::Missing;
    bool hasEnemy = false;          // hunter virtual +0x384 non-null
    float enemyHealth = 0;          // enemy +0xd8
    bool enemyIsAI = false;
    std::int32_t now = 0, enemyDeathTime = 0; // enemy +0x1cac
    std::int32_t newEnemyDebounceTime = 2000; // hunter ai+0x1b2c (spawn default 2000)
};
BrainConditionValue EvaluateEnemyDeadV1(const BrainCombatConditionSpec &, const EnemyDeadInputV1 &);
// Aware 103 calls a body that returns false (0x202fce30): the result is the
// NOT bit for every subject. A documented constant, not a refusal.
BrainConditionValue EvaluateAwareV1(const BrainCombatConditionSpec &);
struct PainInputV1 {
    SubjectClass actor = SubjectClass::Missing;
    float health = 0;               // +0xd8; <= 0 is dead
    float lastFramePain = 0;        // actor+0x5ec (previous Think's +0x5e8)
};
BrainConditionValue EvaluateHasTakenPainV1(const BrainCombatConditionSpec &, const PainInputV1 &);
// Shifts once per game frame; the state starts from the record's saved fields.
struct CombatJustStartedStateV1 {
    bool wasInCombat = false, isInCombat = false;
    std::int32_t timeChecked = 0;
    static CombatJustStartedStateV1 FromSpec(const BrainCombatConditionSpec &);
};
enum class CombatTimerPolicy {
    Refuse,              // default: ai+0x1204 is Unknown (§8)
    InactiveStandInV1    // named stand-in: the timer never lies in the future
};
struct CombatJustStartedInputV1 {
    SubjectClass actor = SubjectClass::Missing;
    float health = 0;
    std::int32_t frameStamp = 0;    // global 0x20682ca8, compared with mTimeChecked
    bool hasEnemy = false;
    Tri combatTimerActive = Tri::Unknown; // 0x201a4740 ("now < ai+0x1204")
    CombatTimerPolicy policy = CombatTimerPolicy::Refuse;
};
struct ConditionResultV1 {
    BrainConditionValue value = BrainConditionValue::Unsupported;
    std::string standIn, reason;
};
ConditionResultV1 EvaluateCombatJustStartedV1(const BrainCombatConditionSpec &, CombatJustStartedStateV1 &,
                                              const CombatJustStartedInputV1 &);

// Hunt procedure step 8 timers (§3.3): timeEnemySet = now; unless ai+0x95e
// bit 0x1, timeUpdateEnemy = now + 5000 (perceived) or now + 500.
struct HuntTimersV1 {
    std::int32_t timeEnemySet = 0, timeUpdateEnemy = 0;
    bool updateWritten = false;
};
HuntTimersV1 HuntTimersFor(std::int32_t now, bool perceived, bool flag95e, std::int32_t previousUpdate);

// ---------------------------------------------------------------------------
// 2. Tactical tables (§5.3)
enum class Tactical : std::uint8_t {
    None = 0, Idle, MoveFollow, Cover, NoCover, Hide, Hunt, Relocate, PullAlarm, KickTable, CheckCorpse,
    GotoVehicle, GotoTurret, Wander, Charge, CheckObjectBounce, CheckInvisObject, DerivedBehavior,
    CounterDefend, MeleeReady
};
const std::size_t TacticalCount = 20;
struct TacticalInfo {
    Tactical value;
    const char *name;     // AITACTICAL_*
    const char *state;    // state-name table 0x205bf668
    std::uint32_t mask;   // 1 << value; 0 for NONE and NOCOVER
    const char *spawnKey; // nullptr: IDLE (always), NONE, NOCOVER (fallback)
    int spawnDefault;
};
const TacticalInfo &TacticalInfoFor(Tactical);
const std::vector<TacticalInfo> &TacticalTableV1();
// Selector priority 0x201d6d60, highest first; NOCOVER is the fallback.
const std::vector<Tactical> &TacticalPriorityV1();
// The idTech/C atoi used for tactical_* values (leading space, sign, digits).
std::int32_t SourceAtoi(const std::string &);
// combat.tacticalAvailable (0x201d68d0): 0x2 plus one bit per key whose
// atoi value is non-zero. lookup returns the merged spawn value or nullptr.
typedef std::function<const std::string *(const std::string &)> SpawnLookup;
std::uint32_t BuildTacticalAvailableMaskV1(const SpawnLookup &lookup);

// Filter 0x201d6ea0, closed parts only. Facts the original reads from unread
// code are Tri inputs; every predicate the page did not close reports an
// "UnknownPredicate:<NAME>" result instead of a value.
struct TacticalFilterInputV1 {
    bool hasEnemy = false;
    bool movementEnabled = false;
    bool coverOverride = false;     // combat.coverOverrideTime -1 or in the future
    // COVER predicate 0x201bb470
    bool cvarDisableCover = false;  // ai_disableCover
    bool flagDisableCover = false;  // combat.fl.disableCover (spawn disableCover)
    Tri coverFeature = Tri::Unknown; // ai+0xa40 virtual +0x64
    // HIDE: the ai+0x141c entity is alive
    Tri hideEntityAlive = Tri::Unknown;
    // HUNT: CanHunt 0x201aded0 plus the hunt-order gate
    bool cvarDisableHunt = false;   // ai_disableHunt
    bool enemySubRecord = false;    // enemy entry +0xc
    Tri huntDelayElapsed = Tri::Unknown;   // now - ai+0x970 >= hunt delay
    Tri enemySeenWithinDelay = Tri::Unknown;
    Tri huntOrderAdmits = Tri::Unknown;    // ai+0x1b3c <= 4 while not already hunting
    // RELOCATE / MELEE_READY
    bool relocate = false;          // combat.relocate ai+0x1e70
    bool meleeReadyFlag = false;    // ai+0x1e15 bit 0x10
};
struct TacticalVerdict {
    Tactical value;
    enum Kind { Kept, Removed, Unknown } kind;
    std::string reason;
};
struct TacticalFilterResultV1 {
    std::uint32_t available = 0, kept = 0, removed = 0, unknown = 0;
    bool hideImmediate = false;     // "HIDE wins at once"
    std::vector<TacticalVerdict> verdicts;
};
TacticalFilterResultV1 FilterTacticalV1(std::uint32_t available, const TacticalFilterInputV1 &);
// Hunt delay 0x201ade50 in ms: (order * add + min) * 1000 with the cvar values
// (team 0: ai_huntTimeAddAllies/MinAllies 5/15, others ai_huntTimeAdd/Min 5/10).
std::int32_t HuntDelayMsV1(float huntOrder, int team, float add = -1, float minimum = -1);

enum class UnknownTacticalPolicy {
    Undecided,     // portable default: an unknown predicate above the choice blocks it
    RefuseByNameV1 // native plan §7.2: each unknown predicate refuses its tactical, named
};
struct TacticalSelectionV1 {
    bool decided = false;
    Tactical value = Tactical::NoCover;
    std::string blockedBy;            // first unknown predicate when undecided
    std::vector<std::string> refused; // RefuseByNameV1: tacticals refused by name
};
TacticalSelectionV1 SelectTacticalV1(const TacticalFilterResultV1 &, UnknownTacticalPolicy);
// SetTactical 0x201d6ce0: equal value only refreshes ai+0x1e20; otherwise the
// new state is posted by name (value 0 posts the initial state State_Combat).
struct TacticalChangeV1 {
    bool changed = false;
    std::string postState;
};
TacticalChangeV1 ApplyTacticalV1(Tactical current, Tactical selected);
// Tactical update gate 0x201d76e0: refuses while an action runs, in the unread
// animation states, while ai+0x1ce4 is set, or inside the delay window.
struct TacticalGateInputV1 {
    bool actionRunning = false;            // ai+0x3b60
    Tri animationStateBlocks = Tri::Unknown;
    bool flag1ce4 = false;
    std::int32_t delay = 0, now = 0, lastTacticalTime = 0; // ai+0x1e20
};
Tri TacticalUpdateAllowedV1(const TacticalGateInputV1 &, std::string &reason);

// ---------------------------------------------------------------------------
// 3. Burst model (§5.6)
struct WeaponStatsV1 {
    std::int32_t minShots = 1, maxShots = 0, shotWaitTime = 150;
    std::int32_t minBursts = 0, maxBursts = 0, burstWaitTimeMin = 0, burstWaitTimeMax = 0, clipSize = 0;
    float attackRangeMin = 0, attackRangeMax = 0, minAimDot = 0, weaponSpread = 0;
};
// Reads a def_stats_ai dictionary. minShots (1) and shotWaitTime (150) have
// recovered defaults; every other key must be present or the read refuses.
bool ReadWeaponStatsV1(const SpawnLookup &lookup, WeaponStatsV1 &, std::string &reason);

class CombatRandomV1 {
  public:
    virtual ~CombatRandomV1() {}
    virtual std::int32_t Range(std::int32_t low, std::int32_t high) = 0; // inclusive
    virtual bool OneInTen() = 0;
};
// The game LCG at 0x2063376c: seed = seed * 0x343fd + 0x269ec3; a draw uses
// bits 17..31. Range(lo, hi) = lo + (((hi + 1 - lo) * draw) >> 15) in 32-bit
// signed arithmetic; OneInTen is draw * 10 < 0x8000. The initial seed is
// supplied by the caller (the game's seeding is not part of this layer).
class SourceGameLcgV1 : public CombatRandomV1 {
  public:
    explicit SourceGameLcgV1(std::uint32_t seed) : seed_(seed) {}
    std::uint32_t Next();
    std::int32_t Range(std::int32_t low, std::int32_t high) override;
    bool OneInTen() override;
    std::uint32_t Seed() const { return seed_; }
    std::size_t Draws() const { return draws_; }
  private:
    std::uint32_t seed_;
    std::size_t draws_ = 0;
};

// weapon.* runtime fields. Their spawn-time values were not read; zero is the
// value the lastShotTime test treats as "never fired".
struct BurstStateV1 {
    std::int32_t waitTime = 0;      // weapon.waitTime +0x2224
    std::int32_t shots = 0;         // weapon.shots +0x2228
    std::int32_t bursts = 0;        // weapon.bursts +0x222c (set by unread code)
    std::int32_t shotCounter = 0;   // +0x2230
    std::int32_t lastShotTime = 0;  // +0x223c
    std::int32_t postBurstTime = 0; // ai+0xa68
};
// Fire predicate 0x20176d30 minus its first clause ("no shots left"), which
// the model checks itself. Unread parts are Tri inputs.
struct FireChecksV1 {
    bool forcedFire = false;        // ai+0x1e15 bit 0x4 (beginForcedFire)
    float aimDot = 0;               // ai+0xf28
    bool facingWall = false, facingFriendly = false;
    Tri targetLockedGate = Tri::Unknown;   // targetLocked* (unread)
    Tri notInBlockedAction = Tri::Unknown; // the two specific actions (unread)
    bool hasEnemy = false;
    bool targetLOSOnly = false;
    Tri lineOfSight = Tri::Unknown;        // only read when targetLOSOnly
    Tri enemySeenRecently = Tri::Unknown;  // seen within ai+0x1e68 ms
    Tri enemyKnownLongEnough = Tri::Unknown; // known for ai+0x1e6c ms
    bool enemyDead = false;         // 0x201a64a0
    float enemyHealth = 0;
    float enemyDistance = 0;
    bool cvarDisableAttacks = false; // ai_disableAttacks
    Tri finalCheckClear = Tri::Unknown; // 0x201aea20(0, 0) returns false
};
// True/False, or Unknown when an unread input decides it.
Tri EvaluateFireChecksV1(const FireChecksV1 &, const WeaponStatsV1 &, std::string &reason);

class BurstHostV1 {
  public:
    virtual ~BurstHostV1() {}
    // idAI::AttackHitscan (virtual +0x628) with "base" or "second"; returns
    // whether the attack happened. Spread and muzzle are native (§7.2 item 5).
    virtual bool AttackHitscan(const char *attackName) = 0;
    // After a successful attack the controller draws the aim kick from the
    // same game random source (two draws, a third under unread conditions).
    // The host performs them here so a shared LCG keeps its order.
    virtual void AfterAttackDraws(CombatRandomV1 &) = 0;
    virtual void Chatter() = 0; // 0x20171f50(25, 0, 0)
};
struct BurstThinkInputV1 {
    std::int32_t now = 0;
    // The controller runs (alive, or dead with ai+0x2244) and ai+0x958 bits
    // 0x6 are clear. The host decides; false skips the controller.
    bool active = false;
    Tri hold64c = Tri::Unknown;     // virtual +0x64c (unread): true ends the Think without fire
    bool flag2108 = false;          // byte +0x2108 gate (unread): profile requires it clear
    bool projectileDef = false;     // def_projectile: AI projectile path (outside this profile)
    bool continuousDef = false;     // def_continuous (outside this profile)
    bool altMode = false;           // bytes +0x2198 and +0x20bc (outside this profile)
    bool secondAttack = false;      // running action byte +0x1 bit 0x1
    bool inAction1658 = false;      // ai+0x3b60 == ai+0x1658
    FireChecksV1 checks;
};
struct BurstThinkResultV1 {
    bool refused = false;
    std::string reason;
    bool fired = false, burstEnded = false, burstStarted = false, chatter = false, aimWaitPushed = false;
    std::string attack;
    std::int32_t burstShots = 0, burstWait = 0;
    Tri predicate = Tri::Unknown;
};
// One controller Think. Order (0x20178380): shot step, post-shot bookkeeping,
// fire predicate, then either the low-aim wait push or a burst start.
BurstThinkResultV1 ThinkBurstV1(BurstStateV1 &, const WeaponStatsV1 &, const BurstThinkInputV1 &, CombatRandomV1 &,
                                BurstHostV1 &);

int RunSourceAICombatTests();
} // namespace aicombat
} // namespace wolf2009
#endif

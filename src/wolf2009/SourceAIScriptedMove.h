// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_AI_SCRIPTED_MOVE_H
#define WOLF2009_SOURCE_AI_SCRIPTED_MOVE_H
// Portable C++11 contracts for the trainyard greeter's node 2, node 10 and
// their conditions, recovered from the identified retail Gamex86.dll (see
// docs/AI-ACTOR.md section 8, stage 4). Hosts own entity lookup and native
// state; these functions only apply the recorded rules to supplied facts.
// Anything outside the recorded rules is an explicit Unsupported result.
#include "BrainGraph.h"
#include <cstdint>
#include <string>

namespace wolf2009 {
namespace sourceai {

struct Vec3 { float x = 0, y = 0, z = 0; };

// Argument text of one node record ("type id x y n ^type flags ... key value").
// Returns false when the key is absent. Duplicate keys are refused (throws).
bool NodeArgument(const BrainGraph &, std::size_t node, const std::string &key, std::string &value);

// Bool kind 0x18 (0x2020ff40): only a case-insensitive "true" is true.
bool BoolText(const std::string &text);
// Distance kind (0x2020ff80): "<value> <unit>", unit 1 multiplies by 12.
// Returns false for text outside "%s %d" with a finite value.
bool DistanceText(const std::string &text, float &value);
// Vector kind: three finite components.
bool VectorText(const std::string &text, Vec3 &value);

// --- Type 49 IgnoreEnemiesSet (Enter 0x201f2c60) -------------------------
// The idAI-cast actor's acquisition state as the original reads it.
struct CombatState {
    bool ignoreEnemies = false;     // ai+0x1c84 bit 2
    bool enemyLock = false;         // ai+0x1c85 bit 0
    bool hasEnemy = false;          // ai+0x1b18 handle
    bool tacticalGoalArmed = false; // ai+0x1e15 bit 3 (path_corner tacticalGoal)
};
enum class IgnoreEnemiesResult { Applied, NoActor, Unsupported };
// mIgnore true first runs the clear-enemy procedure 0x201a4680, then writes
// bit 2. Clearing a live enemy (virtual +0x5a8) or re-issuing the tactical
// move (+0x520 command 9) is not recovered and is refused before any write.
IgnoreEnemiesResult IgnoreEnemiesSetEnter(CombatState *actor, bool ignore, std::string &reason);

// --- Type 83 SetIgnorePanic ----------------------------------------------
// Enter is the shared no-op 0x201fd4c0; the descriptor has no primary.

// --- Type 243 GoToAndFaceTarget (Enter 0x201f1420, Exit 0x201f15d0) -------
enum class Movement { Walk = 0, Run, CombatWalk, CombatRun, StealthWalk, StealthRun, Dash };
bool MovementText(const std::string &text, Movement &value);
// ai+0x11b5 style bits written after a successful scripted move.
std::uint8_t MovementStyleBits(Movement value, bool ignoreClusters, bool endInCrouch);

// --- ReachedPos 0x20199420 -----------------------------------------------
// range <= 0 uses 8; a nonzero move status (ai+0x11c4) is never reached;
// 2D distance squared must be below range squared; unless ignoreHeight the
// height difference must be below 70.
bool ReachedPosition(const Vec3 &origin, const Vec3 &target, float range, bool ignoreHeight, std::uint32_t moveStatus);

enum class ConditionValue { False, True, Unsupported };
inline ConditionValue Apply(bool raw, bool inverted) { return raw != inverted ? ConditionValue::True : ConditionValue::False; }

// --- Condition 242 ReachedAndFacing (Evaluate 0x20213e40) ----------------
struct ReachedAndFacingFacts {
    bool actorIsAI = false, actorDead = false;
    std::string actorReferenceText; // raw mActor text, for the named exception
    Vec3 internalPos;               // mInternalPos; all zero selects mLocation
    bool locationResolved = false;
    Vec3 locationOrigin;
    float range = 0;
    Vec3 origin;                    // actor physics origin
    std::uint32_t moveStatus = 0;
    bool faceResolved = false;
    // IsFacing 0x2019afb0 is false while a turn toward the same entity is
    // pending. Hosts without that native state report Unknown.
    enum class FacePending { No, Yes, Unknown } facePending = FacePending::Unknown;
    // 0x20198160: the start actions at ai+0x1538 / ai+0xe7c are playing.
    bool startActionPlaying = false;
    bool inverted = false;
};
ConditionValue EvaluateReachedAndFacing(const ReachedAndFacingFacts &);

// --- Condition 21 Reached (Evaluate 0x201e90d0) --------------------------
struct ReachedFacts {
    bool actorResolved = false, actorIsAI = false;
    Vec3 internalPos;   // nonzero needs 0x2013b5c0, not recovered
    bool locationResolved = false;
    Vec3 locationOrigin;
    float range = 0;
    Vec3 origin;
    std::uint32_t moveStatus = 0;
    bool startActionPlaying = false;
    bool inverted = false;
};
ConditionValue EvaluateReached(const ReachedFacts &);

// --- Move start (TryMoveStart 0x201c7330, Action_Move_Start 0x201c75c0) ---
struct MoveStartFacts {
    bool pending = false;           // ai+0x11b7 bit 0x10, set when legs leave idle
    bool dead = false, inVehicle = false, movementDisabled = false;
    bool moving = false;            // ai+0x11b0 bit 0x2 after StartMove
    std::string overrideAnimation;  // ai+0x3820
    bool moveOverrideActive = false; // walk/run override strings (ai+0x37e0/0x3804)
    bool clipResolved = false;      // LookupAnimByName succeeded for the name
};
enum class MoveStartDecision { NotPending, Refused, NoClip, Start };
// Only the override path is admitted: an empty override needs the default
// style name from virtual +0x6c4, which is not recovered (Refused).
MoveStartDecision TryMoveStart(const MoveStartFacts &, std::string &animation, std::string &reason);

int RunSourceAIScriptedMoveTests();
} // namespace sourceai
} // namespace wolf2009
#endif

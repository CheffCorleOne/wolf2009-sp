// SPDX-License-Identifier: GPL-3.0-or-later
// SourceAIHuntV1 portable layer; contracts in docs/AI-COMBAT.md.
#include "SourceAICombat.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace aicombat {
namespace {
BrainConditionValue Bool(bool value) { return value ? BrainConditionValue::True : BrainConditionValue::False; }
bool IsAI(SubjectClass c) { return c == SubjectClass::AI; }
bool IsActor(SubjectClass c) { return c == SubjectClass::Actor || c == SubjectClass::AI; }
std::string RefText(const BrainEntityReference &ref) {
    switch (ref.mode) {
    case BrainEntityMode::None: return "None";
    case BrainEntityMode::Me: return "Me";
    case BrainEntityMode::Player: return "Player";
    case BrainEntityMode::Named: return ref.name;
    }
    return "?";
}
} // namespace

// ---------------------------------------------------------------------------
// Brain handlers
CombatTaskReceipt EnterCombatTaskV1(const BrainCombatTaskSpec &spec, CombatTaskHost &host) {
    CombatTaskReceipt receipt;
    receipt.node = spec.nodeId;
    receipt.type = spec.nodeType;
    receipt.action = BrainCombatTaskKindName(spec.kind);
    receipt.profile = spec.profile;
    if (spec.kind == BrainCombatTaskKind::Hunt) {
        // Enter 0x201f2890: hunter must cast to idAI, otherwise return.
        if (!IsAI(host.Classify(spec.subject))) {
            receipt.outcome = CombatTaskReceipt::NoOp;
            receipt.effect = "hunter " + RefText(spec.subject) + " is not an idAI; Enter returns";
            return receipt;
        }
        host.StopScriptedMove(spec.subject);
        HuntCall call;
        call.node = spec.nodeId;
        call.hunter = spec.subject;
        call.prey = spec.prey;
        call.updatePosition = spec.updatePosition;
        call.locked = spec.locked;
        host.Hunt(call);
        receipt.outcome = CombatTaskReceipt::Applied;
        receipt.effect = "scripted move stopped; Hunt(prey=" + RefText(spec.prey) + ", locked=" +
                         (spec.locked ? "1" : "0") + ", updatePosition=" + (spec.updatePosition ? "1" : "0") + ")";
        return receipt;
    }
    // IgnoreEnemiesSet 0x201f2c60.
    if (!IsAI(host.Classify(spec.subject))) {
        receipt.outcome = CombatTaskReceipt::NoOp;
        receipt.effect = "actor " + RefText(spec.subject) + " is not an idAI; no-op";
        return receipt;
    }
    if (spec.value) host.ClearEnemy(spec.subject);
    host.SetIgnoreEnemies(spec.subject, spec.value);
    receipt.outcome = CombatTaskReceipt::Applied;
    receipt.effect = std::string(spec.value ? "ClearEnemy; " : "") + "senses.fl.ignoreEnemies=" + (spec.value ? "1" : "0");
    return receipt;
}

CombatTaskReceipt EnterNoTargetSetV1(const BrainActionSpec &spec, CombatTaskHost &host) {
    CombatTaskReceipt receipt;
    receipt.node = spec.nodeId;
    receipt.type = spec.nodeType;
    receipt.action = "NoTargetSet";
    receipt.profile = spec.profile;
    if (spec.kind != BrainActionKind::NoTargetSet) {
        receipt.effect = "not a NoTargetSet action";
        return receipt;
    }
    // 0x201f2d70 writes any entity class; nothing is written without one.
    if (host.Classify(spec.subject) == SubjectClass::Missing) {
        receipt.outcome = CombatTaskReceipt::NoOp;
        receipt.effect = "entity " + RefText(spec.subject) + " not resolved; nothing written";
        return receipt;
    }
    host.SetNoTarget(spec.subject, spec.value);
    receipt.outcome = CombatTaskReceipt::Applied;
    receipt.effect = std::string("notarget=") + (spec.value ? "1" : "0") + " (raw \"" + spec.rawValue + "\")";
    return receipt;
}

BrainConditionValue EvaluateEnemyDeadV1(const BrainCombatConditionSpec &spec, const EnemyDeadInputV1 &in) {
    if (spec.kind != BrainCombatConditionKind::EnemyDead) return BrainConditionValue::Unsupported;
    // 0x201ea9f0: a non-AI hunter gives false XOR NOT. 0x201a64a0: the enemy
    // exists with health <= 0; an idAI enemy must also have been dead for
    // longer than the hunter's newEnemyDebounceTime.
    bool dead = false;
    if (IsAI(in.hunter) && in.hasEnemy && in.enemyHealth <= 0) {
        dead = !in.enemyIsAI || (in.now - in.enemyDeathTime > in.newEnemyDebounceTime);
    }
    return Bool(dead != spec.invert);
}

BrainConditionValue EvaluateAwareV1(const BrainCombatConditionSpec &spec) {
    if (spec.kind != BrainCombatConditionKind::Aware) return BrainConditionValue::Unsupported;
    return Bool(spec.invert);
}

BrainConditionValue EvaluateHasTakenPainV1(const BrainCombatConditionSpec &spec, const PainInputV1 &in) {
    if (spec.kind != BrainCombatConditionKind::HasTakenPain) return BrainConditionValue::Unsupported;
    // 0x201eacc0: no actor or a dead actor returns plain false (NOT is not
    // applied). -1 thresholds open the range; both bounds are inclusive on
    // the truncated damage; the damage must be non-zero.
    if (!IsActor(in.actor) || !(in.health > 0)) return BrainConditionValue::False;
    const std::int64_t low = spec.minThreshold == -1 ? INT32_MIN : spec.minThreshold;
    const std::int64_t high = spec.maxThreshold == -1 ? INT32_MAX : spec.maxThreshold;
    bool result = false;
    if (in.lastFramePain != 0) {
        const std::int64_t damage = static_cast<std::int64_t>(in.lastFramePain); // truncation
        result = damage >= low && damage <= high;
    }
    return Bool(result != spec.invert);
}

CombatJustStartedStateV1 CombatJustStartedStateV1::FromSpec(const BrainCombatConditionSpec &spec) {
    CombatJustStartedStateV1 state;
    state.wasInCombat = spec.wasInCombat;
    state.isInCombat = spec.isInCombat;
    state.timeChecked = spec.timeChecked;
    return state;
}

ConditionResultV1 EvaluateCombatJustStartedV1(const BrainCombatConditionSpec &spec, CombatJustStartedStateV1 &state,
                                              const CombatJustStartedInputV1 &in) {
    ConditionResultV1 result;
    if (spec.kind != BrainCombatConditionKind::CombatJustStarted) {
        result.reason = "not a CombatJustStarted condition";
        return result;
    }
    // 0x201eae00: no idAI or a dead one returns plain false.
    if (!IsAI(in.actor) || !(in.health > 0)) {
        result.value = BrainConditionValue::False;
        return result;
    }
    if (state.timeChecked != in.frameStamp) {
        bool inCombat = in.hasEnemy;
        if (!inCombat) {
            if (in.combatTimerActive == Tri::Unknown) {
                if (in.policy != CombatTimerPolicy::InactiveStandInV1) {
                    // Nothing is shifted: a refused evaluation leaves the state as it was.
                    result.reason = "CombatJustStarted needs ai+0x1204 (Unknown) while there is no enemy";
                    return result;
                }
                result.standIn = "InactiveStandInV1";
            } else {
                inCombat = in.combatTimerActive == Tri::True;
            }
        }
        state.wasInCombat = state.isInCombat;
        state.isInCombat = inCombat;
        state.timeChecked = in.frameStamp;
    }
    result.value = Bool((state.isInCombat && !state.wasInCombat) != spec.invert);
    return result;
}

HuntTimersV1 HuntTimersFor(std::int32_t now, bool perceived, bool flag95e, std::int32_t previousUpdate) {
    HuntTimersV1 timers;
    timers.timeEnemySet = now;
    timers.timeUpdateEnemy = previousUpdate;
    if (!flag95e) {
        timers.timeUpdateEnemy = now + (perceived ? 5000 : 500);
        timers.updateWritten = true;
    }
    return timers;
}

// ---------------------------------------------------------------------------
// Tactical tables
const std::vector<TacticalInfo> &TacticalTableV1() {
    static const std::vector<TacticalInfo> table = {
        {Tactical::None, "AITACTICAL_NONE", "State_Combat", 0, nullptr, 0},
        {Tactical::Idle, "AITACTICAL_IDLE", "State_Idle", 0x2, nullptr, 1},
        {Tactical::MoveFollow, "AITACTICAL_MOVE_FOLLOW", "State_MoveFollowLeader", 0x4, "tactical_moveFollow", 1},
        {Tactical::Cover, "AITACTICAL_COVER", "State_CombatCover", 0x8, "tactical_cover", 0},
        {Tactical::NoCover, "AITACTICAL_NOCOVER", "State_CombatNoCover", 0, nullptr, 0},
        {Tactical::Hide, "AITACTICAL_HIDE", "State_CombatHide", 0x20, "tactical_hide", 0},
        {Tactical::Hunt, "AITACTICAL_HUNT", "State_CombatHunt", 0x40, "tactical_hunt", 1},
        {Tactical::Relocate, "AITACTICAL_RELOCATE", "State_CombatRelocate", 0x80, "tactical_relocate", 1},
        {Tactical::PullAlarm, "AITACTICAL_PULL_ALARM", "State_CombatPullAlarm", 0x100, "tactical_pull_alarm", 0},
        {Tactical::KickTable, "AITACTICAL_KICK_TABLE", "State_CombatKickTable", 0x200, "tactical_kick_table", 1},
        {Tactical::CheckCorpse, "AITACTICAL_CHECK_CORPSE", "State_CheckCorpse", 0x400, "tactical_checkCorpse", 1},
        {Tactical::GotoVehicle, "AITACTICAL_GOTO_VEHICLE", "State_GoToVehicle", 0x800, "tactical_gotoVehicle", 1},
        {Tactical::GotoTurret, "AITACTICAL_GOTO_TURRET", "State_GoToTurret", 0x1000, "tactical_gotoTurret", 1},
        {Tactical::Wander, "AITACTICAL_WANDER", "State_Wander", 0x2000, "tactical_wander", 1},
        {Tactical::Charge, "AITACTICAL_CHARGE", "State_CombatCharge", 0x4000, "tactical_charge", 1},
        {Tactical::CheckObjectBounce, "AITACTICAL_CHECK_OBJECT_BOUNCE", "State_CheckObjectBounce", 0x8000,
         "tactical_object_bounce", 0},
        {Tactical::CheckInvisObject, "AITACTICAL_CHECK_INVIS_OBJECT", "State_CheckInvisObject", 0x10000,
         "tactical_invis_object", 0},
        {Tactical::DerivedBehavior, "AITACTICAL_DERIVED_BEHAVIOR", "State_DerivedBehavior", 0x20000, "tactical_derived", 0},
        {Tactical::CounterDefend, "AITACTICAL_COUNTER_DEFEND", "State_CounterDefend", 0x40000, "tactical_counter", 1},
        {Tactical::MeleeReady, "AITACTICAL_MELEE_READY", "State_MeleeReady", 0x80000, "tactical_melee_ready", 1}};
    return table;
}
const TacticalInfo &TacticalInfoFor(Tactical value) {
    const auto index = static_cast<std::size_t>(value);
    if (index >= TacticalCount) throw std::out_of_range("Tactical value outside the 20-value enum");
    return TacticalTableV1()[index];
}
const std::vector<Tactical> &TacticalPriorityV1() {
    static const std::vector<Tactical> order = {Tactical::Hide, Tactical::Charge, Tactical::GotoVehicle,
        Tactical::GotoTurret, Tactical::Relocate, Tactical::KickTable, Tactical::PullAlarm, Tactical::CounterDefend,
        Tactical::MeleeReady, Tactical::CheckCorpse, Tactical::CheckObjectBounce, Tactical::CheckInvisObject,
        Tactical::Cover, Tactical::Hunt, Tactical::MoveFollow, Tactical::Wander, Tactical::Idle,
        Tactical::DerivedBehavior};
    return order;
}

std::int32_t SourceAtoi(const std::string &text) {
    std::size_t at = 0;
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    bool negative = false;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) negative = text[at++] == '-';
    std::int64_t value = 0;
    for (; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at) {
        value = value * 10 + (text[at] - '0');
        if (value > 0x80000000ll) value = 0x80000000ll; // saturate; out-of-range atoi is not a contract
    }
    if (negative) value = -value;
    if (value > INT32_MAX) value = INT32_MAX;
    return static_cast<std::int32_t>(value);
}

std::uint32_t BuildTacticalAvailableMaskV1(const SpawnLookup &lookup) {
    std::uint32_t mask = 0x2; // IDLE is always available
    for (const auto &info : TacticalTableV1()) {
        if (!info.spawnKey) continue;
        const std::string *value = lookup ? lookup(info.spawnKey) : nullptr;
        const std::int32_t number = value ? SourceAtoi(*value) : info.spawnDefault;
        if (number != 0) mask |= info.mask;
    }
    return mask;
}

std::int32_t HuntDelayMsV1(float huntOrder, int team, float add, float minimum) {
    const bool allies = team == 0;
    if (add < 0) add = 5.0f;
    if (minimum < 0) minimum = allies ? 15.0f : 10.0f;
    return static_cast<std::int32_t>((huntOrder * add + minimum) * 1000.0f);
}

TacticalFilterResultV1 FilterTacticalV1(std::uint32_t available, const TacticalFilterInputV1 &in) {
    TacticalFilterResultV1 out;
    out.available = available;
    const auto verdict = [&](Tactical t, TacticalVerdict::Kind kind, const std::string &reason) {
        const auto bit = TacticalInfoFor(t).mask;
        if (kind == TacticalVerdict::Kept) out.kept |= bit;
        else if (kind == TacticalVerdict::Removed) out.removed |= bit;
        else out.unknown |= bit;
        out.verdicts.push_back(TacticalVerdict{t, kind, reason});
    };
    // Combines closed gates (any false removes) with unread ones.
    const auto combine = [&](Tactical t, const std::vector<std::pair<bool, std::string> > &known,
                             const std::vector<std::pair<Tri, std::string> > &tri) {
        for (const auto &gate : known)
            if (!gate.first) { verdict(t, TacticalVerdict::Removed, gate.second); return; }
        std::string unknown;
        for (const auto &gate : tri) {
            if (gate.first == Tri::False) { verdict(t, TacticalVerdict::Removed, gate.second); return; }
            if (gate.first == Tri::Unknown && unknown.empty()) unknown = gate.second;
        }
        if (!unknown.empty()) verdict(t, TacticalVerdict::Unknown, "UnknownInput:" + unknown);
        else verdict(t, TacticalVerdict::Kept, "all closed gates pass");
    };
    for (const auto &info : TacticalTableV1()) {
        if (!info.mask || !(available & info.mask)) continue;
        switch (info.value) {
        case Tactical::Idle:
            if (in.hasEnemy) verdict(info.value, TacticalVerdict::Removed, "IDLE removed: has an enemy");
            else verdict(info.value, TacticalVerdict::Kept, "no enemy");
            break;
        case Tactical::Hide:
            if (!in.movementEnabled || in.hideEntityAlive == Tri::False)
                verdict(info.value, TacticalVerdict::Removed, "HIDE: ai+0x141c entity not alive or movement disabled");
            else if (in.hideEntityAlive == Tri::Unknown)
                verdict(info.value, TacticalVerdict::Unknown, "UnknownInput:ai+0x141c entity");
            else {
                verdict(info.value, TacticalVerdict::Kept, "HIDE wins at once");
                out.hideImmediate = true;
            }
            break;
        case Tactical::Cover:
            combine(info.value, {{!in.cvarDisableCover, "COVER: ai_disableCover"},
                                 {!in.flagDisableCover, "COVER: combat.fl.disableCover (disableCover)"},
                                 {in.movementEnabled, "COVER: movement disabled"},
                                 {!in.coverOverride, "COVER: cover override"}},
                    {{in.coverFeature, "COVER: cover feature ai+0xa40"}});
            break;
        case Tactical::Hunt:
            combine(info.value, {{!in.coverOverride, "HUNT: cover override"},
                                 {in.hasEnemy, "HUNT: no enemy"},
                                 {!in.cvarDisableHunt, "HUNT: ai_disableHunt"},
                                 {in.movementEnabled, "HUNT: movement disabled"},
                                 {in.enemySubRecord, "HUNT: no enemy entry sub-record"}},
                    {{in.huntDelayElapsed, "HUNT: now - ai+0x970 >= hunt delay"},
                     {in.enemySeenWithinDelay == Tri::Unknown ? Tri::Unknown
                          : (in.enemySeenWithinDelay == Tri::True ? Tri::False : Tri::True),
                      "HUNT: enemy seen within the hunt delay"},
                     {in.huntOrderAdmits, "HUNT: ai+0x1b3c order"}});
            break;
        case Tactical::Relocate:
            combine(info.value, {{in.movementEnabled, "RELOCATE: movement disabled"},
                                 {in.relocate, "RELOCATE: combat.relocate clear"}}, {});
            break;
        case Tactical::MeleeReady:
            combine(info.value, {{in.meleeReadyFlag, "MELEE_READY: ai+0x1e15 bit 0x10 clear"}}, {});
            break;
        case Tactical::CounterDefend:
        case Tactical::CheckObjectBounce:
        case Tactical::CheckInvisObject:
            verdict(info.value, TacticalVerdict::Removed, std::string(info.name) + ": constant-false predicate");
            break;
        default:
            verdict(info.value, TacticalVerdict::Unknown, std::string("UnknownPredicate:") + info.name);
            break;
        }
    }
    return out;
}

TacticalSelectionV1 SelectTacticalV1(const TacticalFilterResultV1 &filter, UnknownTacticalPolicy policy) {
    TacticalSelectionV1 out;
    if (filter.hideImmediate) {
        out.decided = true;
        out.value = Tactical::Hide;
        return out;
    }
    for (auto t : TacticalPriorityV1()) {
        const auto bit = TacticalInfoFor(t).mask;
        if (filter.kept & bit) {
            out.decided = true;
            out.value = t;
            return out;
        }
        if (filter.unknown & bit) {
            std::string reason;
            for (const auto &v : filter.verdicts) if (v.value == t) reason = v.reason;
            if (policy == UnknownTacticalPolicy::Undecided) {
                out.blockedBy = reason;
                return out;
            }
            out.refused.push_back(reason);
        }
    }
    out.decided = true;
    out.value = Tactical::NoCover;
    return out;
}

TacticalChangeV1 ApplyTacticalV1(Tactical current, Tactical selected) {
    TacticalChangeV1 out;
    if (current == selected) return out;
    out.changed = true;
    out.postState = selected == Tactical::None ? "State_Combat" : TacticalInfoFor(selected).state;
    return out;
}

Tri TacticalUpdateAllowedV1(const TacticalGateInputV1 &in, std::string &reason) {
    reason.clear();
    if (in.actionRunning) { reason = "an action runs (ai+0x3b60)"; return Tri::False; }
    if (in.flag1ce4) { reason = "ai+0x1ce4 is set"; return Tri::False; }
    if (in.delay != 0 && in.now - in.lastTacticalTime < in.delay) { reason = "inside the delay window"; return Tri::False; }
    if (in.animationStateBlocks == Tri::True) { reason = "animation state blocks"; return Tri::False; }
    if (in.animationStateBlocks == Tri::Unknown) { reason = "UnknownInput:animation states of 0x201d76e0"; return Tri::Unknown; }
    return Tri::True;
}

// ---------------------------------------------------------------------------
// Burst model
namespace {
bool Number(const std::string &text, double &value) {
    std::istringstream stream(text);
    stream >> value;
    if (!stream) return false;
    stream >> std::ws;
    return stream.eof();
}
} // namespace

bool ReadWeaponStatsV1(const SpawnLookup &lookup, WeaponStatsV1 &stats, std::string &reason) {
    reason.clear();
    WeaponStatsV1 out;
    const auto integer = [&](const char *key, std::int32_t &target, bool required) {
        const std::string *value = lookup ? lookup(key) : nullptr;
        if (!value) {
            if (required) reason = std::string("def_stats_ai key ") + key + " is absent and has no recovered default";
            return !required;
        }
        double number = 0;
        if (!Number(*value, number) || number != static_cast<std::int32_t>(number)) {
            reason = std::string("def_stats_ai key ") + key + " is not an integer";
            return false;
        }
        target = static_cast<std::int32_t>(number);
        return true;
    };
    const auto real = [&](const char *key, float &target) {
        const std::string *value = lookup ? lookup(key) : nullptr;
        double number = 0;
        if (!value || !Number(*value, number)) {
            reason = std::string("def_stats_ai key ") + key + " is absent or not a number";
            return false;
        }
        target = static_cast<float>(number);
        return true;
    };
    if (!integer("minShots", out.minShots, false) || !integer("maxShots", out.maxShots, true) ||
        !integer("shotWaitTime", out.shotWaitTime, false) || !integer("minBursts", out.minBursts, true) ||
        !integer("maxBursts", out.maxBursts, true) || !integer("burstWaitTimeMin", out.burstWaitTimeMin, true) ||
        !integer("burstWaitTimeMax", out.burstWaitTimeMax, true) || !integer("clipSize", out.clipSize, true) ||
        !real("minAimDot", out.minAimDot) || !real("weaponSpread", out.weaponSpread))
        return false;
    const std::string *range = lookup ? lookup("attackRange") : nullptr;
    if (!range) { reason = "def_stats_ai key attackRange is absent"; return false; }
    std::istringstream pair(*range);
    double low = 0, high = 0;
    pair >> low >> high;
    if (!pair || !(pair >> std::ws).eof()) { reason = "def_stats_ai attackRange is not two numbers"; return false; }
    out.attackRangeMin = static_cast<float>(low);
    out.attackRangeMax = static_cast<float>(high);
    if (out.minShots < 1 || out.maxShots < out.minShots || out.shotWaitTime < 0 || out.burstWaitTimeMin < 0 ||
        out.burstWaitTimeMax < out.burstWaitTimeMin || out.burstWaitTimeMax - out.burstWaitTimeMin > 0xffff ||
        out.maxShots - out.minShots > 0xffff) {
        reason = "def_stats_ai shot/burst ranges are outside SourceAIBurstV1";
        return false;
    }
    stats = out;
    return true;
}

std::uint32_t SourceGameLcgV1::Next() {
    seed_ = seed_ * 0x343fdu + 0x269ec3u;
    ++draws_;
    return seed_;
}
std::int32_t SourceGameLcgV1::Range(std::int32_t low, std::int32_t high) {
    const std::int32_t draw = static_cast<std::int32_t>(Next() >> 17);
    const std::uint32_t product = static_cast<std::uint32_t>(high + 1 - low) * static_cast<std::uint32_t>(draw);
    return low + (static_cast<std::int32_t>(product) >> 15);
}
bool SourceGameLcgV1::OneInTen() {
    const std::uint32_t draw = Next() >> 17;
    return ((draw * 10u) & 0xffff8000u) < 0x8000u;
}

Tri EvaluateFireChecksV1(const FireChecksV1 &c, const WeaponStatsV1 &stats, std::string &reason) {
    reason.clear();
    if (c.forcedFire) return Tri::True;
    const std::vector<std::pair<bool, const char *> > known = {
        {c.aimDot >= stats.minAimDot, "aim dot below minAimDot"},
        {!c.facingWall, "aim.facingWall"},
        {!c.facingFriendly, "aim.facingFriendly"},
        {c.hasEnemy, "no enemy"},
        {!c.enemyDead && c.enemyHealth > 0, "enemy dead"},
        {!c.cvarDisableAttacks, "ai_disableAttacks"}};
    for (const auto &gate : known)
        if (!gate.first) { reason = gate.second; return Tri::False; }
    // 0x201a8400 inclusivity was not read: a distance on a bound is Unknown.
    Tri range = Tri::True;
    if (c.enemyDistance < stats.attackRangeMin || c.enemyDistance > stats.attackRangeMax) range = Tri::False;
    else if (c.enemyDistance == stats.attackRangeMin || c.enemyDistance == stats.attackRangeMax) range = Tri::Unknown;
    const std::vector<std::pair<Tri, const char *> > unread = {
        {range, "attackRange (0x201a8400)"},
        {c.targetLockedGate, "targetLocked* gate"},
        {c.notInBlockedAction, "two specific actions"},
        {c.targetLOSOnly ? c.lineOfSight : Tri::True, "targetLOSOnly line of sight"},
        {c.enemySeenRecently, "enemy seen within ai+0x1e68"},
        {c.enemyKnownLongEnough, "enemy known for ai+0x1e6c"},
        {c.finalCheckClear, "final check 0x201aea20"}};
    std::string unknown;
    for (const auto &gate : unread) {
        if (gate.first == Tri::False) { reason = gate.second; return Tri::False; }
        if (gate.first == Tri::Unknown && unknown.empty()) unknown = gate.second;
    }
    if (!unknown.empty()) { reason = "UnknownInput:" + unknown; return Tri::Unknown; }
    return Tri::True;
}

BurstThinkResultV1 ThinkBurstV1(BurstStateV1 &s, const WeaponStatsV1 &stats, const BurstThinkInputV1 &in,
                                CombatRandomV1 &random, BurstHostV1 &host) {
    BurstThinkResultV1 out;
    if (!in.active) return out;
    const auto refuse = [&](const std::string &why) {
        out.refused = true;
        out.reason = why;
        return out;
    };
    if (in.continuousDef) return refuse("def_continuous weapon is outside SourceAIBurstV1");
    if (in.altMode) return refuse("alternate fire mode (+0x2198/+0x20bc) is outside SourceAIBurstV1");
    if (in.flag2108) return refuse("byte +0x2108 gate (unread) is outside SourceAIBurstV1");
    if (in.hold64c == Tri::Unknown) return refuse("UnknownInput:virtual +0x64c");
    if (in.hold64c == Tri::True) return out; // ends the Think without fire (hooks are native)
    const std::int32_t now = in.now;
    // The fire checks do not depend on this Think's shot. Decide before any
    // state changes whether an unread input could matter: the predicate is
    // only consulted when no shots are left after the shot step.
    std::string why;
    const Tri checks = EvaluateFireChecksV1(in.checks, stats, why);
    const bool mayEmpty = s.shots <= 0 || (s.shots == 1 && s.waitTime < now);
    if (checks == Tri::Unknown && mayEmpty) return refuse("fire predicate: " + why);
    // Shot step: shots > 0 and waitTime < now (0x201789d7 skips when
    // waitTime >= now).
    if (s.shots > 0 && s.waitTime < now) {
        if (in.projectileDef) return refuse("def_projectile AI path (virtual +0x62c) is outside SourceAIBurstV1");
        out.attack = in.secondAttack ? "second" : "base";
        if (host.AttackHitscan(out.attack.c_str())) {
            out.fired = true;
            host.AfterAttackDraws(random);
            s.waitTime = now + stats.shotWaitTime;
            ++s.shotCounter;
            --s.shots;
            s.lastShotTime = now;
            if (s.shots == 0) {
                s.bursts = std::max(s.bursts - 1, 0);
                s.postBurstTime = now + random.Range(100, 300);
                out.burstEnded = true;
            }
        }
    }
    // Fire predicate 0x20176d30: no shots left (the continuous clause is
    // outside this profile), then forced fire or the checks.
    out.predicate = s.shots > 0 ? Tri::False : checks;
    if (out.predicate == Tri::False) {
        if (stats.minAimDot > in.checks.aimDot && !in.checks.forcedFire && !in.inAction1658) {
            s.waitTime = std::max(s.waitTime, now + 100);
            out.aimWaitPushed = true;
        }
        return out;
    }
    out.burstStarted = true;
    std::int32_t wait = 0;
    if (stats.maxShots == 1) {
        s.shots = 1;
        wait = random.Range(0, 60);
    } else {
        s.shots = random.Range(stats.minShots, stats.maxShots);
        if (s.lastShotTime != 0 && now - s.lastShotTime <= stats.shotWaitTime + stats.burstWaitTimeMax)
            wait = random.Range(stats.burstWaitTimeMin, stats.burstWaitTimeMax);
        if (random.OneInTen()) {
            out.chatter = true;
            host.Chatter();
        }
    }
    s.waitTime = std::max(s.waitTime, now) + wait;
    out.burstShots = s.shots;
    out.burstWait = wait;
    return out;
}
} // namespace aicombat
} // namespace wolf2009

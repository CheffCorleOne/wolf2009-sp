// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceAIScriptedMove.h"
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace sourceai {
namespace {
bool EqualFold(const std::string &a, const char *b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x >= 'A' && x <= 'Z') x = static_cast<unsigned char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<unsigned char>(y + 32);
        if (x != y) return false;
    }
    return i == a.size() && !b[i];
}
bool Zero(const Vec3 &v) {
    // The original ORs the raw component bits and masks the sign bit.
    return v.x == 0.f && v.y == 0.f && v.z == 0.f;
}
} // namespace

bool NodeArgument(const BrainGraph &graph, std::size_t node, const std::string &key, std::string &value) {
    if (node >= graph.nodes.size()) throw std::runtime_error("Node argument index outside the graph");
    const auto &tokens = graph.records[graph.nodes[node].record].tokens;
    if (tokens.size() < 5 || (tokens.size() - 5) % 2) throw std::runtime_error("Node record has an unpaired argument");
    bool found = false;
    for (std::size_t i = 5; i + 1 < tokens.size(); i += 2) {
        if (tokens[i] != key) continue;
        if (found) throw std::runtime_error("Node record repeats an argument key");
        value = tokens[i + 1];
        found = true;
    }
    return found;
}

bool BoolText(const std::string &text) { return EqualFold(text, "true"); }

bool DistanceText(const std::string &text, float &value) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    std::string number;
    int unit = 0;
    if (!(input >> number >> unit)) return false;
    std::string extra;
    if (input >> extra) return false;
    std::istringstream parsed(number);
    parsed.imbue(std::locale::classic());
    double raw = 0;
    if (!(parsed >> raw) || !parsed.eof() || !std::isfinite(raw)) return false;
    float result = static_cast<float>(raw);
    // Units of 2 or more are reset to 0 by the kind parser (as durations).
    if (unit == 1) result *= 12.f;
    if (!std::isfinite(result)) return false;
    value = result;
    return true;
}

bool VectorText(const std::string &text, Vec3 &value) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double v[3];
    if (!(input >> v[0] >> v[1] >> v[2])) return false;
    std::string extra;
    if (input >> extra) return false;
    for (double x : v)
        if (!std::isfinite(x)) return false;
    value.x = float(v[0]); value.y = float(v[1]); value.z = float(v[2]);
    return true;
}

IgnoreEnemiesResult IgnoreEnemiesSetEnter(CombatState *actor, bool ignore, std::string &reason) {
    reason.clear();
    if (!actor) return IgnoreEnemiesResult::NoActor;
    if (ignore) {
        if (actor->hasEnemy) {
            reason = "IgnoreEnemiesSet would clear a live enemy through virtual +0x5a8, which is not recovered";
            return IgnoreEnemiesResult::Unsupported;
        }
        if (actor->tacticalGoalArmed) {
            reason = "IgnoreEnemiesSet would re-issue the tactical move +0x520 command 9, which is not recovered";
            return IgnoreEnemiesResult::Unsupported;
        }
        actor->enemyLock = false;
    }
    actor->ignoreEnemies = ignore;
    return IgnoreEnemiesResult::Applied;
}

bool MovementText(const std::string &text, Movement &value) {
    static const char *names[] = {"Walk", "Run", "CombatWalk", "CombatRun", "StealthWalk", "StealthRun", "Dash"};
    for (unsigned i = 0; i < 7; ++i)
        if (text == names[i]) { value = static_cast<Movement>(i); return true; }
    return false;
}

std::uint8_t MovementStyleBits(Movement value, bool ignoreClusters, bool endInCrouch) {
    static const std::uint8_t style[] = {0x2, 0x1, 0x12, 0x11, 0x22, 0x21, 0x5};
    std::uint8_t bits = style[static_cast<unsigned>(value)];
    if (ignoreClusters) bits |= 0x40;
    if (endInCrouch) bits |= 0x8;
    return bits;
}

bool ReachedPosition(const Vec3 &origin, const Vec3 &target, float range, bool ignoreHeight, std::uint32_t moveStatus) {
    if (!(range > 0.f)) range = 8.f;
    if (moveStatus) return false;
    const float dx = origin.x - target.x, dy = origin.y - target.y;
    if (!(range * range > dx * dx + dy * dy)) return false;
    if (ignoreHeight) return true;
    return 70.f > std::fabs(target.z - origin.z);
}

ConditionValue EvaluateReachedAndFacing(const ReachedAndFacingFacts &f) {
    const Vec3 target = Zero(f.internalPos) ? (f.locationResolved ? f.locationOrigin : Vec3()) : f.internalPos;
    if (!f.actorIsAI || f.actorDead) {
        if (!EqualFold(f.actorReferenceText, "layer52_enemies_ss_officer_2")) return Apply(true, f.inverted);
        if (!f.actorIsAI) return Apply(true, f.inverted);
    }
    if (!f.faceResolved) return Apply(false, f.inverted);
    const bool reached = ReachedPosition(f.origin, target, f.range, false, f.moveStatus);
    if (!reached || f.startActionPlaying) return Apply(false, f.inverted);
    if (f.facePending == ReachedAndFacingFacts::FacePending::Unknown) return ConditionValue::Unsupported;
    return Apply(f.facePending == ReachedAndFacingFacts::FacePending::No, f.inverted);
}

ConditionValue EvaluateReached(const ReachedFacts &f) {
    if (!Zero(f.internalPos)) return ConditionValue::Unsupported;
    const Vec3 target = f.locationResolved ? f.locationOrigin : Vec3();
    if (!f.actorResolved) return Apply(true, f.inverted);
    if (!f.actorIsAI) return ConditionValue::Unsupported;
    const bool reached = ReachedPosition(f.origin, target, f.range, false, f.moveStatus);
    return Apply(reached && !f.startActionPlaying, f.inverted);
}

MoveStartDecision TryMoveStart(const MoveStartFacts &f, std::string &animation, std::string &reason) {
    animation.clear();
    reason.clear();
    if (!f.pending) return MoveStartDecision::NotPending;
    if (f.dead || f.inVehicle) { reason = "Move start refused for a dead or vehicle-bound AI"; return MoveStartDecision::Refused; }
    if (!f.moving) { reason = "Move start requires an active move (ai+0x11b0 bit 0x2)"; return MoveStartDecision::Refused; }
    if (f.movementDisabled) { reason = "Move start refused while movement is disabled"; return MoveStartDecision::Refused; }
    if (f.moveOverrideActive) { reason = "Move start refused while a walk/run override is set"; return MoveStartDecision::Refused; }
    if (f.overrideAnimation.empty()) {
        reason = "Default move-start name (virtual +0x6c4 from the movement style) is not recovered";
        return MoveStartDecision::Refused;
    }
    if (!f.clipResolved) { reason = "Move-start animation does not resolve on the actor"; return MoveStartDecision::NoClip; }
    animation = f.overrideAnimation;
    return MoveStartDecision::Start;
}

int RunSourceAIScriptedMoveTests() {
    unsigned checks = 0;
    auto need = [&checks](bool ok, const char *what) {
        ++checks;
        if (!ok) throw std::runtime_error(what);
    };
    try {
        need(BoolText("TRUE") && BoolText("true") && !BoolText("1") && !BoolText("fasle") && !BoolText(""), "bool kind");
        float d = 0;
        need(DistanceText("20.00 0", d) && d == 20.f, "distance units");
        need(DistanceText("2 1", d) && d == 24.f, "distance unit 1");
        need(!DistanceText("20", d) && !DistanceText("x 0", d), "distance refusals");
        Vec3 v;
        need(VectorText("-271 702 313", v) && v.x == -271.f && v.z == 313.f && !VectorText("1 2", v), "vector kind");

        CombatState state;
        std::string why;
        need(IgnoreEnemiesSetEnter(nullptr, true, why) == IgnoreEnemiesResult::NoActor, "null actor");
        state.enemyLock = true;
        need(IgnoreEnemiesSetEnter(&state, true, why) == IgnoreEnemiesResult::Applied && state.ignoreEnemies && !state.enemyLock, "ignore true");
        state.enemyLock = true;
        need(IgnoreEnemiesSetEnter(&state, false, why) == IgnoreEnemiesResult::Applied && !state.ignoreEnemies && state.enemyLock, "ignore false keeps the lock");
        CombatState enemy; enemy.hasEnemy = true;
        need(IgnoreEnemiesSetEnter(&enemy, true, why) == IgnoreEnemiesResult::Unsupported && !enemy.ignoreEnemies, "enemy refusal before write");
        need(IgnoreEnemiesSetEnter(&enemy, false, why) == IgnoreEnemiesResult::Applied, "ignore false never clears");
        CombatState tactical; tactical.tacticalGoalArmed = true;
        need(IgnoreEnemiesSetEnter(&tactical, true, why) == IgnoreEnemiesResult::Unsupported, "tactical refusal");

        Movement m;
        need(MovementText("CombatRun", m) && MovementStyleBits(m, false, false) == 0x11, "combat run bits");
        need(MovementStyleBits(Movement::Dash, true, true) == 0x4d && !MovementText("combatrun", m), "style bits / exact enum text");

        Vec3 o, t;
        t.x = 19.9f;
        need(ReachedPosition(o, t, 20.f, false, 0) && !ReachedPosition(o, t, 20.f, false, 1), "reached / move status");
        t.x = 20.f;
        need(!ReachedPosition(o, t, 20.f, false, 0), "strict range");
        t.x = 7.9f;
        need(ReachedPosition(o, t, 0.f, false, 0), "default range 8");
        t.x = 0; t.z = 69.9f;
        need(ReachedPosition(o, t, 20.f, false, 0), "height below 70");
        t.z = 70.f;
        need(!ReachedPosition(o, t, 20.f, false, 0) && ReachedPosition(o, t, 20.f, true, 0), "height boundary / ignore");

        ReachedAndFacingFacts f;
        f.actorIsAI = false;
        f.actorReferenceText = "Me";
        need(EvaluateReachedAndFacing(f) == ConditionValue::True, "missing actor raw true");
        f.inverted = true;
        need(EvaluateReachedAndFacing(f) == ConditionValue::False, "missing actor inverted");
        f = ReachedAndFacingFacts();
        f.actorIsAI = true; f.range = 20.f; f.internalPos.x = -271; f.internalPos.y = 702; f.internalPos.z = 313;
        f.origin.x = 176.75f; f.origin.y = 271.75f; f.origin.z = 296.f;
        f.faceResolved = true; f.facePending = ReachedAndFacingFacts::FacePending::No;
        need(EvaluateReachedAndFacing(f) == ConditionValue::False, "far actor false");
        f.origin.x = -265; f.origin.y = 700; f.origin.z = 300;
        need(EvaluateReachedAndFacing(f) == ConditionValue::True, "reached and facing");
        f.faceResolved = false;
        need(EvaluateReachedAndFacing(f) == ConditionValue::False, "missing face false");
        f.faceResolved = true; f.startActionPlaying = true;
        need(EvaluateReachedAndFacing(f) == ConditionValue::False, "start action false");
        f.startActionPlaying = false; f.facePending = ReachedAndFacingFacts::FacePending::Unknown;
        need(EvaluateReachedAndFacing(f) == ConditionValue::Unsupported, "unknown turn state");
        f.origin.x = 0;
        need(EvaluateReachedAndFacing(f) == ConditionValue::False, "unknown turn state irrelevant when far");
        f = ReachedAndFacingFacts();
        f.actorIsAI = true; f.actorDead = true; f.actorReferenceText = "LAYER52_enemies_ss_officer_2";
        need(EvaluateReachedAndFacing(f) == ConditionValue::False, "named exception continues for a dead AI");

        ReachedFacts r;
        need(EvaluateReached(r) == ConditionValue::True, "reached without actor");
        r.actorResolved = true;
        need(EvaluateReached(r) == ConditionValue::Unsupported, "non-AI reached refused");
        r.actorIsAI = true; r.locationResolved = true; r.locationOrigin.x = 5;
        need(EvaluateReached(r) == ConditionValue::True, "reached default range");
        r.internalPos.z = 1;
        need(EvaluateReached(r) == ConditionValue::Unsupported, "internal pos refused");

        MoveStartFacts s;
        std::string anim;
        need(TryMoveStart(s, anim, why) == MoveStartDecision::NotPending, "not pending");
        s.pending = true; s.moving = true; s.overrideAnimation = "srp_tyd_nml_train_door_open"; s.clipResolved = true;
        need(TryMoveStart(s, anim, why) == MoveStartDecision::Start && anim == s.overrideAnimation, "override start");
        s.movementDisabled = true;
        need(TryMoveStart(s, anim, why) == MoveStartDecision::Refused, "movement disabled");
        s.movementDisabled = false; s.overrideAnimation.clear();
        need(TryMoveStart(s, anim, why) == MoveStartDecision::Refused, "default name refused");
        s.overrideAnimation = "x"; s.clipResolved = false;
        need(TryMoveStart(s, anim, why) == MoveStartDecision::NoClip, "missing clip");
        std::printf("WOLF2009_SOURCE_AI_SCRIPTED_MOVE_SELFTEST_OK checks=%u\n", checks);
        return 0;
    } catch (const std::exception &error) {
        std::printf("WOLF2009_SOURCE_AI_SCRIPTED_MOVE_SELFTEST_FAIL checks=%u reason=%s\n", checks, error.what());
        return 1;
    }
}
} // namespace sourceai
} // namespace wolf2009

#ifdef WOLF2009_SOURCE_AI_SCRIPTED_MOVE_TEST_MAIN
int main() { return wolf2009::sourceai::RunSourceAIScriptedMoveTests(); }
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#include "RotatingDoorOld.h"
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace wolf2009 {
namespace rotatingdoor {
namespace {
void Need(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
std::string Lower(std::string value) { for (char &c : value) if (c >= 'A' && c <= 'Z') c = char(c + 32); return value; }
bool Prefix(const std::string &key, const char *prefix) { return Lower(key).compare(0, std::string(prefix).size(), prefix) == 0; }
// Keys whose class branches are not implemented by this profile. Each refuses
// by name (TRAPDOOR 4, T0 step 1).
const char *const RefusedKeys[] = {"team", "locked", "synclock", "buddy", "globalexpression", "requires", "x_axis",
                                   "rotation", "clipmodel", "noclose", "nosmartopen", "twowayhinge",
                                   "noclient", "noai"};
const char *const RefusedPrefixes[] = {"target", "triggeropened"};
bool RefusedKey(const std::string &key) {
    const auto k = Lower(key);
    for (const char *name : RefusedKeys) if (k == name) return true;
    for (const char *prefix : RefusedPrefixes) if (Prefix(k, prefix)) return true;
    return false;
}
const EntityProperty &Field(const std::map<std::string, const EntityProperty *> &fields, const char *key) {
    const auto found = fields.find(key);
    Need(found != fields.end(), std::string("TrainyardSewerTrapdoorKeysV1: required key ") + key + " is absent");
    return *found->second;
}
std::string Text(const std::map<std::string, const EntityProperty *> &fields, const char *key, const char *expected) {
    const auto &field = Field(fields, key);
    Need(field.kind == EntityProperty::Text && field.text == expected,
         "TrainyardSewerTrapdoorKeysV1: " + field.key + " differs from the measured value " + expected);
    return field.text;
}
int Integer(const std::map<std::string, const EntityProperty *> &fields, const char *key, int expected) {
    const auto &field = Field(fields, key);
    Need(field.kind == EntityProperty::Integer && field.integer == expected,
         "TrainyardSewerTrapdoorKeysV1: " + field.key + " differs from the measured value " + std::to_string(expected));
    return field.integer;
}
double Radians(double degrees) { return degrees * 3.14159265358979323846 / 180.0; }
} // namespace

DoorKeys QualifyTrainyardSewerTrapdoorKeysV1(const EntityRecord &entity) {
    static const char *const admitted[] = {"classname", "name", "model", "origin", "start_open", "y_axis", "opendegrees",
                                           "wait", "toggle", "noautoclose", "triggerclosed", "usable", "snd_close"};
    std::map<std::string, const EntityProperty *> fields;
    for (const auto &property : entity.properties) {
        const auto key = Lower(property.key);
        Need(!RefusedKey(key), "TrainyardSewerTrapdoorKeysV1: key " + property.key + " selects an unimplemented rvRotatingDoor branch");
        bool known = false;
        for (const char *name : admitted) if (key == name) known = true;
        Need(known, "TrainyardSewerTrapdoorKeysV1: key " + property.key + " is outside the admitted key set");
        Need(fields.insert(std::make_pair(key, &property)).second, "TrainyardSewerTrapdoorKeysV1: duplicate key " + property.key);
    }
    Need(fields.size() == 13, "TrainyardSewerTrapdoorKeysV1: the 13 measured keys are not all present");
    DoorKeys out;
    out.classname = Text(fields, "classname", "func_door_rotating_old");
    out.name = Text(fields, "name", "sewer_trapdoor");
    out.model = Text(fields, "model", "sewer_trapdoor");
    const auto &origin = Field(fields, "origin");
    Need(origin.kind == EntityProperty::Floats && origin.sourceType == 1 && origin.numbers.size() == 3 && origin.numbers[0] == -288.f &&
             origin.numbers[1] == 576.f && origin.numbers[2] == 310.f,
         "TrainyardSewerTrapdoorKeysV1: origin differs from the measured hinge (-288 576 310)");
    for (int i = 0; i < 3; ++i) out.origin[i] = origin.numbers[std::size_t(i)];
    out.startOpen = Integer(fields, "start_open", 1) != 0;
    Integer(fields, "y_axis", 1);
    out.axis = Axis::Y;
    out.openDegrees = Integer(fields, "opendegrees", -100);
    out.wait = Integer(fields, "wait", 0);
    out.toggle = Integer(fields, "toggle", 1) != 0;
    out.noAutoClose = Integer(fields, "noautoclose", 1) != 0;
    out.triggerClosed = Text(fields, "triggerclosed", "door_default_closed");
    out.usable = Integer(fields, "usable", 0) != 0;
    out.sndClose = Text(fields, "snd_close", "trap_door_close");
    // Read by rvPhysicsDoor only (0x20037700); rvRotatingDoor never reads it.
    out.receipts.push_back("noAutoClose:unconsumed-by-class");
    // Used only by the auto-close and re-post paths, which toggle disables.
    out.receipts.push_back("wait:unused-with-toggle");
    return out;
}

void QualifyRotatingDoorOldClassV1(const ClassFacts &facts) {
    for (const auto &key : facts.keys)
        Need(!RefusedKey(key), "SourceRotatingDoorOldV1 class: resolved key " + key + " selects an unimplemented rvRotatingDoor branch");
    static const char *const consumed[] = {"spawnclass", "time", "wait", "opendegrees", "snd_open", "snd_close", "start_open",
                                           "toggle", "y_axis", "usable", "enabled", "no_touch"};
    for (const auto &key : facts.opaqueKeys) {
        Need(!RefusedKey(key), "SourceRotatingDoorOldV1 class: opaque key " + key + " selects an unimplemented branch");
        for (const char *name : consumed) Need(Lower(key) != name, "SourceRotatingDoorOldV1 class: consumed key " + key + " is opaque");
    }
    Need(facts.spawnclass == "rvRotatingDoor", "SourceRotatingDoorOldV1 class: spawnclass is not rvRotatingDoor");
    Need(facts.hasTime && facts.time == 1, "SourceRotatingDoorOldV1 class: time differs from 1");
    Need(facts.hasWait && facts.wait == 3, "SourceRotatingDoorOldV1 class: wait differs from 3");
    Need(facts.hasOpenDegrees && facts.openDegrees == 100, "SourceRotatingDoorOldV1 class: openDegrees differs from 100");
    Need(facts.sndOpen == "door_default_open", "SourceRotatingDoorOldV1 class: snd_open differs from door_default_open");
    Need(facts.sndClose == "door_default_close", "SourceRotatingDoorOldV1 class: snd_close differs from door_default_close");
    for (const auto &key : facts.keys)
        for (const char *name : {"start_open", "toggle", "x_axis", "y_axis", "usable", "enabled", "no_touch"})
            Need(Lower(key) != name, "SourceRotatingDoorOldV1 class: class default for " + key + " is outside the measured chain");
}

DoorProfile ResolveTrainyardSewerTrapdoorV1(const DoorKeys &keys, const ClassFacts &facts) {
    QualifyRotatingDoorOldClassV1(facts);
    DoorProfile out;
    out.keys = keys;
    out.sndOpen = facts.sndOpen;   // not overridden by the instance
    out.sndClose = keys.sndClose;  // instance override
    out.openDegrees = keys.openDegrees;
    out.time = facts.time;
    out.wait = keys.wait;
    Need(out.wait >= 0 && out.wait <= 3600, "SourceRotatingDoorOldV1: wait is outside its admitted range");
    out.waitMs = std::int32_t(std::floor(out.wait * 1000.0 + 0.5));
    return out;
}

LocalBox HingedBoxLocalBounds(const SourceBoxBody &body) {
    LocalBox out;
    for (int k = 0; k < 3; ++k) {
        out.min[k] = body.center[k] - body.halfExtents[k];
        out.max[k] = body.center[k] + body.halfExtents[k];
    }
    return out;
}

void RequireHingedBoxMatchesBounds(const SourceBoxBody &body, const LocalBox &model, double tolerance) {
    const auto box = HingedBoxLocalBounds(body);
    for (int k = 0; k < 3; ++k)
        Need(std::fabs(box.min[k] - model.min[k]) <= tolerance && std::fabs(box.max[k] - model.max[k]) <= tolerance,
             "HingedSolidBoxV1: HKXS box differs from the PROC model bounds");
}

LocalBox RotatedWorldBounds(const LocalBox &box, const double origin[3], double pitch, double yaw, double roll) {
    const double sp = std::sin(Radians(pitch)), cp = std::cos(Radians(pitch));
    const double sy = std::sin(Radians(yaw)), cy = std::cos(Radians(yaw));
    const double sr = std::sin(Radians(roll)), cr = std::cos(Radians(roll));
    // Doom 3 idAngles::ToMat3 rows; a point maps as p * M.
    const double m[3][3] = {{cp * cy, cp * sy, -sp},
                            {sr * sp * cy + cr * -sy, sr * sp * sy + cr * cy, sr * cp},
                            {cr * sp * cy + -sr * -sy, cr * sp * sy + -sr * cy, cr * cp}};
    LocalBox out;
    for (int k = 0; k < 3; ++k) { out.min[k] = 1e300; out.max[k] = -1e300; }
    for (int c = 0; c < 8; ++c) {
        const double p[3] = {(c & 1) ? box.max[0] : box.min[0], (c & 2) ? box.max[1] : box.min[1], (c & 4) ? box.max[2] : box.min[2]};
        for (int k = 0; k < 3; ++k) {
            const double w = origin[k] + p[0] * m[0][k] + p[1] * m[1][k] + p[2] * m[2][k];
            out.min[k] = std::min(out.min[k], w);
            out.max[k] = std::max(out.max[k], w);
        }
    }
    return out;
}

const char *DoorStateText(DoorState state) {
    switch (state) {
    case DoorState::Closed: return "closed";
    case DoorState::Opening: return "opening";
    case DoorState::Open: return "open";
    case DoorState::ClosingReversed: return "closing-reversed";
    case DoorState::Closing: return "closing";
    }
    return "unknown";
}

std::int32_t DurationMsV1(double delta, double openDegrees, double time, double &seconds) {
    Need(openDegrees != 0 && std::isfinite(openDegrees) && std::isfinite(delta) && time > 0 && std::isfinite(time),
         "RotatingDoorOldStateV1: duration inputs are outside the admitted range");
    seconds = std::fabs(delta / openDegrees) * time;
    Need(seconds <= 3600, "RotatingDoorOldStateV1: duration exceeds one hour");
    // fistp with the default round-to-nearest (ties to even).
    return std::int32_t(std::nearbyint(seconds * 1000.0));
}

namespace {
ToggleResult Ignore(const ToggleInput &in, const char *why) {
    ToggleResult out;
    out.effect = ToggleResult::Effect::Ignored;
    out.next = in.state;
    out.axis = in.axis;
    out.reason = why;
    return out;
}
ToggleResult Move(const ToggleInput &in, DoorState next, double target, double delta) {
    ToggleResult out;
    out.effect = ToggleResult::Effect::Move;
    out.next = next;
    out.axis = in.axis;
    out.target = target;
    out.delta = delta;
    out.durationMs = DurationMsV1(delta, in.openDegrees, in.durationOverride > 0 ? in.durationOverride : in.time, out.durationSeconds);
    out.speed = out.durationSeconds > 0 ? delta / out.durationSeconds : 0;
    out.startTime = in.now;
    Need(double(in.now) + out.durationMs <= 2147483647.0, "RotatingDoorOldStateV1: end time exceeds the integer clock");
    out.endTime = in.now + out.durationMs;
    return out;
}
ToggleResult Close(const ToggleInput &in) {
    if (in.noClose) return Ignore(in, "noClose");
    // Close: cancel pending activate, state 4, delta = -(last delta).
    auto out = Move(in, DoorState::Closing, in.currentAngle - in.lastDelta, -in.lastDelta);
    out.cancelPendingActivate = true;
    out.propagateToTeam = true;
    return out;
}
ToggleResult Repost(const ToggleInput &in, const char *why) {
    ToggleResult out;
    out.effect = ToggleResult::Effect::Repost;
    out.next = in.state;
    out.axis = in.axis;
    out.cancelPendingActivate = true;
    out.repostDelayMs = in.waitMs;
    out.reason = why;
    return out;
}
} // namespace

ToggleResult ActivateV1(const ToggleInput &in) {
    if (!in.enabled) return Ignore(in, "door is not enabled");
    if (in.activator == Activator::None) return Ignore(in, "activate without an activator is outside the recovered table");
    const bool self = in.activator == Activator::Self;
    switch (in.state) {
    case DoorState::Closed: {
        // Inferred: the move delta is target - current angle on the axis.
        double delta = in.openDegrees - in.currentAngle;
        if (!self && !in.noSmartOpen && !in.twoWayHinge && in.smartOpenNegate) delta = -delta;
        auto out = Move(in, DoorState::Opening, in.currentAngle + delta, delta);
        out.becomeActive = true;
        out.portalOpen = true;
        out.soundKey = "snd_open";
        out.propagateToTeam = true;
        return out;
    }
    case DoorState::Opening:
        if (!self) return Ignore(in, "opening door ignores an outside activator");
        if (in.noClose) return Ignore(in, "noClose");
        return Move(in, DoorState::ClosingReversed, 0, -in.currentAngle);
    case DoorState::Open:
        if (!self) {
            if (in.toggle || in.playerUsingThisDoor) return Close(in);
            return Repost(in, "open door without toggle re-posts an outside activate after wait");
        }
        if (in.doorwayOccupied) return Repost(in, "doorway occupied; activate re-posted after wait");
        return Close(in);
    case DoorState::ClosingReversed:
    case DoorState::Closing:
        if (!self) return Ignore(in, "closing door ignores an outside activator");
        return Move(in, DoorState::Opening, in.openDegrees, in.openDegrees - in.currentAngle);
    }
    return Ignore(in, "unknown door state");
}

ThinkResult ThinkV1(const ThinkInput &in) {
    ThinkResult out;
    out.next = in.state;
    if (in.now < in.endTime) return out;
    out.velocityReset = true;
    if (in.state == DoorState::Opening) {
        out.completed = true;
        out.next = DoorState::Open;
        if (!in.toggle) { out.postAutoClose = true; out.autoCloseDelayMs = in.waitMs; }
        out.activateNames = in.triggerOpened;
    } else if (in.state == DoorState::ClosingReversed || in.state == DoorState::Closing) {
        out.completed = true;
        out.next = DoorState::Closed;
        out.becomeInactive = true;
        out.portalClosed = !in.noPortal;
        out.soundKey = "snd_close";
        out.sound = in.sndClose;
        out.activateNames = in.triggerClosed;
    }
    return out;
}

} // namespace rotatingdoor
} // namespace wolf2009

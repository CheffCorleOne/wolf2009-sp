// SPDX-License-Identifier: GPL-3.0-or-later
// SourceRotatingDoorOldV1 T0 self-test. Synthetic tables always run; with an
// owner-supplied trainyard.mpk path the original ENTS 2136 keys and its HKXS
// body are qualified read-only as well.
#include "RotatingDoorOld.h"
#include "SourceMapBodies.h"
#include "StreamMounts.h"
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace wolf2009 {
namespace rotatingdoor {
namespace {
unsigned checks = 0;
void Need(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
void Refuses(const std::function<void()> &f, const char *containing, const char *why) {
    ++checks;
    try { f(); } catch (const std::exception &error) {
        if (std::string(error.what()).find(containing) == std::string::npos)
            throw std::runtime_error(std::string(why) + " (refused for another reason: " + error.what() + ")");
        return;
    }
    throw std::runtime_error(std::string(why) + " (accepted)");
}
EntityProperty Text(const char *key, const char *value) { EntityProperty p; p.key = key; p.kind = EntityProperty::Text; p.text = value; return p; }
EntityProperty Int(const char *key, int value) { EntityProperty p; p.key = key; p.kind = EntityProperty::Integer; p.integer = value; p.sourceType = 4; return p; }
EntityProperty Vec(const char *key, float x, float y, float z) {
    EntityProperty p; p.key = key; p.kind = EntityProperty::Floats; p.numbers = {x, y, z}; p.sourceType = 1; return p;
}
EntityRecord Trapdoor() {
    EntityRecord e;
    e.properties = {Text("classname", "func_door_rotating_old"), Text("name", "sewer_trapdoor"), Text("model", "sewer_trapdoor"),
                    Vec("origin", -288, 576, 310), Int("start_open", 1), Int("y_axis", 1), Int("openDegrees", -100), Int("wait", 0),
                    Int("toggle", 1), Int("noAutoClose", 1), Text("triggerClosed", "door_default_closed"), Int("usable", 0),
                    Text("snd_close", "trap_door_close")};
    return e;
}
ClassFacts Class() {
    ClassFacts c;
    c.spawnclass = "rvRotatingDoor"; c.sndOpen = "door_default_open"; c.sndClose = "door_default_close";
    c.time = 1; c.hasTime = true; c.wait = 3; c.hasWait = true; c.openDegrees = 100; c.hasOpenDegrees = true;
    c.keys = {"spawnclass", "time", "wait", "opendegrees", "dmg", "triggersize", "maxfocusdistance", "translucent", "chokepointradius",
              "snd_open", "snd_close", "snd_closed", "snd_locked", "snd_lock", "snd_unlock", "snd_kick", "nodefaultphysics",
              "def_knockback_player"};
    return c;
}
bool Near(double a, double b, double e) { return std::fabs(a - b) <= e; }

void KeyTests() {
    const auto keys = QualifyTrainyardSewerTrapdoorKeysV1(Trapdoor());
    Need(keys.name == "sewer_trapdoor" && keys.axis == Axis::Y && keys.openDegrees == -100 && keys.startOpen && keys.toggle && !keys.usable &&
             keys.sndClose == "trap_door_close" && keys.triggerClosed == "door_default_closed" && keys.origin[2] == 310,
         "original keys qualify");
    Need(keys.receipts.size() == 2 && keys.receipts[0] == "noAutoClose:unconsumed-by-class" && keys.receipts[1] == "wait:unused-with-toggle",
         "noAutoClose and wait receipts");
    auto changed = [](const char *key, EntityProperty value) {
        auto e = Trapdoor();
        for (auto &p : e.properties) if (p.key == key) p = value;
        return e;
    };
    Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(changed("openDegrees", Int("openDegrees", 100))); }, "openDegrees", "changed openDegrees refuses");
    Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(changed("toggle", Int("toggle", 0))); }, "toggle", "changed toggle refuses");
    Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(changed("origin", Vec("origin", -288, 576, 311))); }, "origin", "changed origin refuses");
    Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(changed("wait", Text("wait", "0"))); }, "wait", "text wait refuses");
    for (const char *key : {"team", "rotation", "target", "target1", "triggerOpened", "locked", "syncLock", "buddy", "x_axis", "clipmodel",
                            "noClose", "noSmartOpen", "twoWayHinge", "noClient", "noAI", "globalExpression", "requires"}) {
        auto e = Trapdoor();
        e.properties.push_back(Text(key, "x"));
        Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(e); }, key, "branch key refuses by name");
    }
    auto extra = Trapdoor();
    extra.properties.push_back(Int("hingeOffset", 1));
    Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(extra); }, "hingeOffset", "unknown key refuses by name");
    auto missing = Trapdoor();
    missing.properties.pop_back();
    Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(missing); }, "13", "missing key refuses");
    auto dup = Trapdoor();
    dup.properties.push_back(Int("WAIT", 0));
    Refuses([&] { QualifyTrainyardSewerTrapdoorKeysV1(dup); }, "duplicate", "duplicate key refuses");

    const auto profile = ResolveTrainyardSewerTrapdoorV1(keys, Class());
    Need(profile.openDegrees == -100 && profile.time == 1 && profile.wait == 0 && profile.waitMs == 0 && profile.sndOpen == "door_default_open" &&
             profile.sndClose == "trap_door_close",
         "instance overrides the class");
    auto c = Class();
    c.spawnclass = "rvPhysicsDoor";
    Refuses([&] { QualifyRotatingDoorOldClassV1(c); }, "spawnclass", "parent spawnclass refuses");
    c = Class(); c.keys.push_back("team");
    Refuses([&] { QualifyRotatingDoorOldClassV1(c); }, "team", "class team refuses");
    c = Class(); c.opaqueKeys.push_back("time");
    Refuses([&] { QualifyRotatingDoorOldClassV1(c); }, "time", "opaque consumed key refuses");
    c = Class(); c.time = 2;
    Refuses([&] { QualifyRotatingDoorOldClassV1(c); }, "time", "class time refuses");
}

void BoxTests() {
    SourceBoxBody body;
    body.center[0] = 32; body.halfExtents[0] = 32; body.halfExtents[1] = 32; body.halfExtents[2] = 2;
    LocalBox model;
    model.min[0] = 0; model.min[1] = -32; model.min[2] = -2; model.max[0] = 64; model.max[1] = 32; model.max[2] = 2;
    RequireHingedBoxMatchesBounds(body, model, 0.001);
    ++checks;
    auto wrong = model; wrong.max[0] = 63;
    Refuses([&] { RequireHingedBoxMatchesBounds(body, wrong, 0.001); }, "PROC model bounds", "box/model mismatch refuses");
    const double origin[3] = {-288, 576, 310};
    const auto closed = RotatedWorldBounds(model, origin, 0, 0, 0);
    Need(closed.min[0] == -288 && closed.max[0] == -224 && closed.min[1] == 544 && closed.max[1] == 608 && closed.min[2] == 308 && closed.max[2] == 312,
         "closed world box");
    const auto open = RotatedWorldBounds(model, origin, -100, 0, 0);
    Need(Near(open.min[0], -301.08, 0.01) && Near(open.max[0], -286.03, 0.01) && Near(open.min[1], 544, 1e-9) && Near(open.max[1], 608, 1e-9) &&
             Near(open.min[2], 309.65, 0.01) && Near(open.max[2], 373.37, 0.01),
         "open world box at pitch -100 (TRAPDOOR 1.3)");
}

void StateTests() {
    ToggleInput in;
    in.axis = Axis::Y; in.openDegrees = -100; in.time = 1; in.toggle = true; in.now = 5000; in.activator = Activator::Self;
    // Closed + self -> opening, -100 about Y over 1000 ms, snd_open.
    auto r = ActivateV1(in);
    Need(r.effect == ToggleResult::Effect::Move && r.next == DoorState::Opening && r.delta == -100 && r.axis == Axis::Y && r.durationMs == 1000 &&
             r.endTime == 6000 && r.speed == -100 && r.soundKey == "snd_open" && r.becomeActive && r.portalOpen,
         "closed + self opens");
    // Opening done -> open, no auto-close (toggle), no triggerOpened.
    ThinkInput t;
    t.state = DoorState::Opening; t.toggle = true; t.now = 5999; t.endTime = 6000; t.sndClose = "trap_door_close";
    t.triggerClosed = {"door_default_closed"};
    Need(!ThinkV1(t).completed && !ThinkV1(t).velocityReset, "not done before the end time");
    t.now = 6000;
    auto d = ThinkV1(t);
    Need(d.completed && d.next == DoorState::Open && !d.postAutoClose && !d.becomeInactive && d.activateNames.empty() && d.velocityReset, "opening done -> open");
    t.state = DoorState::Open; t.now = 7000;
    Need(!ThinkV1(t).completed && ThinkV1(t).velocityReset, "open keeps thinking after the end time");
    // Open + other with toggle -> closing, +100 over 1000 ms, no sound.
    in.state = DoorState::Open; in.activator = Activator::Other; in.currentAngle = -100; in.lastDelta = -100; in.now = 20000;
    r = ActivateV1(in);
    Need(r.effect == ToggleResult::Effect::Move && r.next == DoorState::Closing && r.delta == 100 && r.target == 0 && r.durationMs == 1000 &&
             r.endTime == 21000 && r.soundKey.empty() && r.cancelPendingActivate,
         "open + other with toggle closes");
    // Closing done -> closed, inactive, snd_close, triggerClosed names.
    t.state = DoorState::Closing; t.now = 21000; t.endTime = 21000;
    d = ThinkV1(t);
    Need(d.completed && d.next == DoorState::Closed && d.becomeInactive && d.portalClosed && d.soundKey == "snd_close" && d.sound == "trap_door_close" &&
             d.activateNames.size() == 1 && d.activateNames[0] == "door_default_closed",
         "closing done -> closed");
    // Opening + other ignored; opening + self reverses; closing + self reopens.
    in.state = DoorState::Opening; in.activator = Activator::Other; in.currentAngle = -40;
    Need(ActivateV1(in).effect == ToggleResult::Effect::Ignored && ActivateV1(in).next == DoorState::Opening, "opening + other ignored");
    in.activator = Activator::Self;
    r = ActivateV1(in);
    Need(r.effect == ToggleResult::Effect::Move && r.next == DoorState::ClosingReversed && r.delta == 40 && r.target == 0 && r.durationMs == 400,
         "opening + self reverses");
    in.noClose = true;
    Need(ActivateV1(in).effect == ToggleResult::Effect::Ignored, "noClose blocks the reversal");
    in.noClose = false;
    in.state = DoorState::ClosingReversed;
    r = ActivateV1(in);
    Need(r.effect == ToggleResult::Effect::Move && r.next == DoorState::Opening && r.delta == -60 && r.durationMs == 600, "closing + self reopens");
    in.state = DoorState::Closing; in.activator = Activator::Other;
    Need(ActivateV1(in).effect == ToggleResult::Effect::Ignored, "closing + other ignored");
    // Disabled ignored.
    in.state = DoorState::Closed; in.enabled = false; in.currentAngle = 0;
    Need(ActivateV1(in).effect == ToggleResult::Effect::Ignored, "disabled ignored");
    in.enabled = true;
    // Without toggle: the open state re-posts an outside activate after wait,
    // and opening completion posts the auto-close.
    in.state = DoorState::Open; in.toggle = false; in.waitMs = 3000; in.activator = Activator::Other; in.currentAngle = -100; in.lastDelta = -100;
    r = ActivateV1(in);
    Need(r.effect == ToggleResult::Effect::Repost && r.repostDelayMs == 3000 && r.cancelPendingActivate && r.next == DoorState::Open, "no toggle re-posts after wait");
    in.playerUsingThisDoor = true;
    Need(ActivateV1(in).next == DoorState::Closing, "player use target closes without toggle");
    in.playerUsingThisDoor = false; in.activator = Activator::Self; in.doorwayOccupied = true;
    Need(ActivateV1(in).effect == ToggleResult::Effect::Repost, "occupied doorway re-posts the self close");
    in.doorwayOccupied = false;
    Need(ActivateV1(in).next == DoorState::Closing, "clear doorway closes on self activate");
    t.state = DoorState::Opening; t.toggle = false; t.waitMs = 3000; t.now = 6000; t.endTime = 6000; t.triggerOpened = {"a", "b"};
    d = ThinkV1(t);
    Need(d.completed && d.postAutoClose && d.autoCloseDelayMs == 3000 && d.activateNames.size() == 2, "auto-close posted without toggle");
    // Smart open negates an outside activator's delta.
    in = ToggleInput(); in.openDegrees = 90; in.activator = Activator::Other; in.smartOpenNegate = true;
    Need(ActivateV1(in).delta == -90, "smart open negates the delta");
    in.noSmartOpen = true;
    Need(ActivateV1(in).delta == 90, "noSmartOpen keeps the delta");
    in.activator = Activator::None;
    Need(ActivateV1(in).effect == ToggleResult::Effect::Ignored, "no activator is outside the table");
    // Duration formula: time 1 and 2, and a half-open delta.
    double s = 0;
    Need(DurationMsV1(-100, -100, 1, s) == 1000 && s == 1, "time 1");
    Need(DurationMsV1(-100, -100, 2, s) == 2000 && s == 2, "time 2");
    Need(DurationMsV1(50, -100, 1, s) == 500 && s == 0.5, "half-open delta");
    Need(DurationMsV1(-100.0 / 3.0, -100, 1, s) == 333, "fractional ms rounds to nearest");
    Refuses([] { double unused = 0; DurationMsV1(10, 0, 1, unused); }, "duration", "zero openDegrees refuses");
}

void Original(const char *mpk) {
    StreamMounts mounts([&](const std::string &) { return std::unique_ptr<std::istream>(new std::ifstream(mpk, std::ios::binary)); });
    mounts.Mount("trainyard.mpk");
    const char *map = "maps/game/trainyard/trainyard";
    const auto plan = DecodeSourceMapBodies(mounts.ReadUnique("HKXS", map), mounts.ReadUnique("ENTS", map), map);
    std::size_t index = 0, matches = 0;
    for (std::size_t i = 0; i < plan.entities.entities.size(); ++i) {
        const auto *c = plan.entities.entities[i].Find("classname");
        if (c && c->kind == EntityProperty::Text && c->text == "func_door_rotating_old") { index = i; ++matches; }
    }
    Need(matches == 1 && index == 2136, "trainyard has one func_door_rotating_old at ENTS 2136");
    const auto keys = QualifyTrainyardSewerTrapdoorKeysV1(plan.entities.entities[index]);
    Need(keys.name == "sewer_trapdoor", "original keys qualify");
    const auto body = DecodeSourceEntityBoxBody(plan, index, SourceBoxBodyProfile::HingedSolidBoxV1);
    Need(body.rawFilter == 3 && body.rootType == "hkpConvexTranslateShape" && body.boundingType == "hkpConvexTranslateShape", "original hinged body profile");
    LocalBox model;
    model.min[0] = 0; model.min[1] = -32; model.min[2] = -2; model.max[0] = 64; model.max[1] = 32; model.max[2] = 2;
    RequireHingedBoxMatchesBounds(body, model, 0.01);
    ++checks;
    Refuses([&] { DecodeSourceEntityBoxBody(plan, index, SourceBoxBodyProfile::SolidBoxV1); }, "filter", "filter 3 is not SolidBoxV1");
    std::cout << "WOLF2009_ROTATING_DOOR_OLD_ORIGINAL entity=" << index << " filter=" << body.rawFilter << " box=(" << body.center[0] - body.halfExtents[0]
              << " " << body.center[1] - body.halfExtents[1] << " " << body.center[2] - body.halfExtents[2] << ")-(" << body.center[0] + body.halfExtents[0]
              << " " << body.center[1] + body.halfExtents[1] << " " << body.center[2] + body.halfExtents[2] << ")\n";
}
} // namespace

int RunRotatingDoorOldTests() {
    checks = 0;
    KeyTests();
    BoxTests();
    StateTests();
    return int(checks);
}
} // namespace rotatingdoor
} // namespace wolf2009

int main(int argc, char **argv) {
    try {
        const int synthetic = wolf2009::rotatingdoor::RunRotatingDoorOldTests();
        if (argc > 1) wolf2009::rotatingdoor::Original(argv[1]);
        std::cout << "WOLF2009_ROTATING_DOOR_OLD_OK synthetic_checks=" << synthetic << " total_checks=" << wolf2009::rotatingdoor::checks
                  << " original=" << (argc > 1 ? 1 : 0) << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "WOLF2009_ROTATING_DOOR_OLD_FAIL " << error.what() << "\n";
        return 1;
    }
}

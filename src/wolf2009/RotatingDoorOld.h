// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ROTATING_DOOR_OLD_H
#define WOLF2009_ROTATING_DOOR_OLD_H
// SourceRotatingDoorOldV1, portable stage T0 (docs/TRAPDOOR.md section 4).
// Strict admission of the trainyard sewer trapdoor (ENTS func_door_rotating_old
// -> native class rvRotatingDoor) and the pure open/close state machine of
// that class, recovered statically (TRAPDOOR sections 2.3-2.6). Nothing here
// moves an entity, plays a sound or posts an event; native owners act on the
// returned decisions under their own explicit profiles.
#include "Entities.h"
#include "TrainyardExit.h"
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
namespace rotatingdoor {

enum class Axis { X = 0, Y = 1, Z = 2 }; // roll (x_axis), pitch (y_axis), yaw

// --- TrainyardSewerTrapdoorKeysV1 -------------------------------------------
// Exactly the 13 ENTS keys of ENTS 2136 with their measured values. Any other
// key, a changed value or a refused branch key throws with the key's name.
struct DoorKeys {
    std::string classname, name, model, triggerClosed, sndClose;
    float origin[3] = {0, 0, 0};
    double openDegrees = 0, wait = 0;
    bool startOpen = false, toggle = false, usable = true, noAutoClose = false;
    Axis axis = Axis::Z;
    // Present keys without effect in this class/profile, kept by name:
    // "noAutoClose:unconsumed-by-class", "wait:unused-with-toggle".
    std::vector<std::string> receipts;
};
DoorKeys QualifyTrainyardSewerTrapdoorKeysV1(const EntityRecord &entity);

// Resolved compiled declaration chain func_door_rotating_old ->
// func_door_rotating, reduced by the native owner to the consumed literals.
// keys lists every resolved literal key (ASCII lower case); refusedKeys
// names opaque facts on consumed keys, which refuse.
struct ClassFacts {
    std::string spawnclass, sndOpen, sndClose;
    double time = -1, openDegrees = 0, wait = -1;
    bool hasTime = false, hasOpenDegrees = false, hasWait = false;
    std::vector<std::string> keys, opaqueKeys;
};
// Child spawnclass rvRotatingDoor, time 1, wait 3, openDegrees 100,
// snd_open door_default_open, snd_close door_default_close; branch keys of
// the class (team, locked, ...) refuse by name if a parent sets them.
void QualifyRotatingDoorOldClassV1(const ClassFacts &facts);

// Spawn values after the instance overrides the class (TRAPDOOR 1.2).
struct DoorProfile {
    DoorKeys keys;
    std::string sndOpen, sndClose;
    double openDegrees = 0, time = 0, wait = 0;
    std::int32_t waitMs = 0;
};
DoorProfile ResolveTrainyardSewerTrapdoorV1(const DoorKeys &, const ClassFacts &);

// --- HingedSolidBoxV1 ------------------------------------------------------
// The HKXS body (SourceBoxBodyProfile::HingedSolidBoxV1) must equal the PROC
// model bounds in the door's local frame (hinge at the origin).
struct LocalBox { double min[3] = {0, 0, 0}, max[3] = {0, 0, 0}; };
LocalBox HingedBoxLocalBounds(const SourceBoxBody &);
void RequireHingedBoxMatchesBounds(const SourceBoxBody &, const LocalBox &model, double tolerance);
// World AABB of the local box rotated by the Doom 3 idAngles convention
// (pitch about Y, yaw about Z, roll about X) and translated to origin.
// Inferred convention (TRAPDOOR 1.3); used to check native clip bounds.
LocalBox RotatedWorldBounds(const LocalBox &, const double origin[3], double pitch, double yaw, double roll);

// --- RotatingDoorOldStateV1 ------------------------------------------------
// State +0x30c: 0 closed, 1 opening, 2 open, 3/4 closing.
enum class DoorState { Closed = 0, Opening = 1, Open = 2, ClosingReversed = 3, Closing = 4 };
enum class Activator { None, Self, Other };
const char *DoorStateText(DoorState);

struct ToggleInput {
    DoorState state = DoorState::Closed;
    Activator activator = Activator::None;
    bool enabled = true, toggle = false, noClose = false, noSmartOpen = false, twoWayHinge = false;
    // State 2, other activator, no toggle: the activator is the local player
    // using this door (Inferred +0x15d0/+0x1a90). Not used by the trapdoor.
    bool playerUsingThisDoor = false;
    // State 2, self activator: result of the doorway occupancy test (0x20036a60).
    bool doorwayOccupied = false;
    // State 0, other activator, smart open: result of the side sign test.
    bool smartOpenNegate = false;
    Axis axis = Axis::Z;
    double openDegrees = 0, time = 1, durationOverride = 0; // override in seconds, 0 = none
    double currentAngle = 0, lastDelta = 0;                  // along axis
    std::int32_t waitMs = 0, now = 0;
};
struct ToggleResult {
    enum class Effect { Ignored, Move, Repost } effect = Effect::Ignored;
    DoorState next = DoorState::Closed;
    double target = 0, delta = 0, speed = 0, durationSeconds = 0;
    std::int32_t durationMs = 0, startTime = 0, endTime = 0, repostDelayMs = 0;
    Axis axis = Axis::Z;
    bool becomeActive = false, cancelPendingActivate = false, portalOpen = false, propagateToTeam = false;
    std::string soundKey; // "snd_open" at open start, empty otherwise
    std::string reason;   // why a request was ignored or re-posted
};
// activate gates (0x200363c0, without the refused lock/team/client/AI keys)
// followed by Toggle (+0x2f8, 0x20038d70).
ToggleResult ActivateV1(const ToggleInput &);
// Duration (s) = |delta / openDegrees| * time (or the override); ms rounded
// to nearest (fistp default rounding, ties to even).
std::int32_t DurationMsV1(double delta, double openDegrees, double time, double &seconds);

struct ThinkInput {
    DoorState state = DoorState::Opening;
    bool toggle = false, noPortal = false;
    std::int32_t now = 0, endTime = 0, waitMs = 0;
    std::vector<std::string> triggerOpened, triggerClosed; // key values in dictionary order
    std::string sndClose;
};
struct ThinkResult {
    bool completed = false;
    DoorState next = DoorState::Opening;
    bool postAutoClose = false, becomeInactive = false, portalClosed = false, velocityReset = false;
    std::int32_t autoCloseDelayMs = 0;
    std::string soundKey, sound;
    std::vector<std::string> activateNames; // triggerOpened*/triggerClosed* values
};
// Think completion (0x20038a20) at or after the end time.
ThinkResult ThinkV1(const ThinkInput &);

int RunRotatingDoorOldTests();
} // namespace rotatingdoor
} // namespace wolf2009
#endif

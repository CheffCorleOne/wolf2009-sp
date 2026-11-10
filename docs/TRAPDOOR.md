# Trainyard sewer trapdoor (`func_door_rotating_old`)

Research and plan, reviewed 2026-11-11. **Nothing here is implemented or
observed natively by this work.** It records the original contract of
ENTS 2136 `sewer_trapdoor` and of its native class `rvRotatingDoor`, the
greeter chain that follows node 10, and a staged native plan.

Why it matters: greeter node 10 `GoToAndFaceTarget` faces `sewer_trapdoor`
([AI-MOVE](AI-MOVE.md) §3.5, §5.2 step 7). Without a native entity of that
name FaceEntity refuses, ReachedAndFacing stays false and the chain cannot
reach node 31. The trapdoor is also the door that `shut_door` closes behind
the player in the sewer ([TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) X4).

Sources: the owner's trainyard MPK (SHA-256 `22a703b7…0832`; ENTS, HKXS,
PROC, DECL), the text `.def` members of `assets.pk4`, the compiled
declaration records, and a private copy of `Gamex86.dll` (`7cb1cbed…2d47`).
Disassembly was read from the retained `llvm-objdump` text; no new Ghidra
import, build, game process or emulator was used. Raw dumps and scripts stay
in ignored `evidence/trapdoor/`.

Labels: **Data** (read from original bytes), **Static** (closed by
disassembly and call sites, not executed), **Inferred**, **Unknown**.

## 1. Source data

### 1.1 Instance (Data)

ENTS 2136, the only `func_door_rotating_old` on trainyard (campaign-wide:
trainyard 1, church 2, caverns 2):

| Key | Value | Note |
|---|---|---|
| `classname` | `func_door_rotating_old` | |
| `name`, `model` | `sewer_trapdoor` | model is a PROC submodel and an HKXS body of the same name |
| `origin` | (−288, 576, 310) | hinge point; no `rotation` key (identity) |
| `start_open` | 1 | opens itself after spawn (§2.4) |
| `y_axis` | 1 | rotation about the local Y axis (pitch) |
| `openDegrees` | −100 | |
| `wait` | 0 | unused here, see §2.6 |
| `toggle` | 1 | no auto-close; any outside activation closes it |
| `noAutoClose` | 1 | **not read by this class** (§2.8) |
| `triggerClosed` | `door_default_closed` | a sound-shader name; no entity has this name |
| `usable` | 0 | player use flag cleared |
| `snd_close` | `trap_door_close` | |

### 1.2 Declaration chain (Data)

`func_door_rotating_old` → `func_door_rotating`. The compiled record
(ordinal 2377, `def/door.def`) holds only `spawnclass "rvRotatingDoor"` and
`inherit "func_door_rotating"`; the parent (ordinal 2378) has
`spawnclass rvPhysicsDoor` and these defaults, identical to the `.def` text:
`time 1`, `wait 3`, `openDegrees 100`, `dmg 0`, `triggersize 32`,
`maxFocusDistance 48`, `translucent 0`, `chokePointRadius 0`,
`snd_open door_default_open`, `snd_close door_default_close`,
`snd_closed door_default_closed`, `snd_locked door_default_locked`,
`snd_lock`/`snd_unlock door_default_unlock`, `snd_kick kick_door`,
`noDefaultPhysics 1`, `def_knockback_player damage_door_knockback_player`.
The child's `spawnclass` overrides the parent's.

Resolved values for the trapdoor: open angle −100°, `time` 1 s, `toggle`
true, `start_open` true, `snd_open door_default_open`,
`snd_close trap_door_close`.

### 1.3 Geometry, sound and references (Data)

- **HKXS body** `sewer_trapdoor`: raw filter word 3, origin (−288, 576, 310),
  root `hkpConvexTranslateShape` (translation (32, 0, 0)) around an
  `hkpBoxShape` with half extents (32, 32, 2). Local box (0, −32, −2)–(64, 32, 2).
- **PROC model** `sewer_trapdoor`: one mesh, material `textures/wood/wood_02`,
  `DrawIndexedTriList` 24 vertices / 12 triangles, bounds
  (0, −32, −2)–(64, 32, 2), equal to the HKXS box.
- **Sound shaders** (trainyard DECL): `trap_door_close` → one sample
  `sound/objects/doors/trap_door_close_loud.wav`, duration 1430, category
  `World - Louder`; `door_default_open` → 3 samples, `maxPlayback 4`.
- **Closed pose** (identity): world box x −288…−224, y 544…608, z 308…312.
- **Open pose** at pitch −100° (**Inferred**: Doom 3 `idAngles::ToMat3`
  convention): x ≈ −301.1…−286.0, y 544…608, z ≈ 309.7…373.4. The slab
  stands up past vertical, leaning toward −X.
- **Who refers to it on trainyard.** ENTS 2095 `shut_door` (`trigger_once`,
  HKXS phantom half (112, 64, 4) at (−240, 576, 132)) has `target trap_door`
  (no such entity), `target1 train_greeter_brainuse_4` (`player_safe`) and
  `target2 sewer_trapdoor`. Greeter node 10 uses it as `mFaceTarget`. No
  other ENTS key and none of the other 15 trainyard BRAI graphs mention it.
- A prior PC probe of an isolated copy listed entity 2447 as
  `rvRotatingDoor, sewer_trapdoor, sewer_trapdoor` (class, name, render
  model).

## 2. Native class `rvRotatingDoor` (Static unless marked)

### 2.1 Type and hierarchy

| Fact | Value |
|---|---|
| Type registration | `0x2003b500` → typeinfo ctor `0x202b1060`; superclass name `rvDoor` |
| Hierarchy | `rvRotatingDoor` → `rvDoor` → `idEntity` (`rvDoor` registered at `0x2003b5b0` with super `idEntity`). It is **not** an `idMover`. `rvPhysicsDoor` (`func_door_rotating`) is the other `rvDoor` child |
| CreateInstance | `0x2003a1f0`: 0x738 bytes, ctor `0x20038630` |
| Spawn | `0x20038760` (typeinfo `+0xc`); `rvDoor` Spawn `0x2003a290` runs first (base-first chain `0x202afbc0`) |
| Post-spawn callback | typeinfo `+0x18` = `rvDoor`'s `0x200399f0`, run base-first by `0x202afbf0` from a deferred game pass (`0x2006efe0`) over recorded entity handles |
| Save | typeinfo `+0x10` = `rvDoor::ReadWriteSaveGame` `0x20032470` (shared) |
| Own events | none: the event table at `0x20613f94` is zero-initialized (only its terminator) |
| Inherited `rvDoor` events (`0x205b7f00`) | `activate` → `0x200363c0`; `<spawnDoorTrigger>`; `enable`/`disable` (flag); `open` → `0x20037550`; `close` → `0x20037570`; `lock(d)`; `isOpen`; `isLocked` |
| Virtual overrides (vft `0x204ce444` vs `rvDoor` `0x204cdd84`) | `+0x00` GetType, `+0x04` destructor, `+0x3c` Think `0x20038a20`, `+0x2f8` Toggle `0x20038d70` (the `rvDoor` slot is a stub) |
| Physics member | `+0x4ec`, RTTI `idPhysics_Parametric` (vft `0x20546414`) |

`open` acts only in states 0/3/4 and `close` only in states 1/2; both call
the `activate` handler with the door itself as activator.

### 2.2 Spawn

`rvDoor` Spawn (base, `0x2003a290`) reads the shared door keys into
flag bytes. Bits used below:

| Byte.bit | Key (default) |
|---|---|
| `+0x308`.0 / .1 | `locked` (0) / `locked` > 1 |
| `+0x308`.2 | `enabled` (1) |
| `+0x308`.3 | `toggle` (0) |
| `+0x308`.4 / .5 / .6 / .7 | `noClient` / `noAI` / `no_touch` / `noSmartOpen` |
| `+0x309`.0 / .1 / .2 | `noLatch` / `noKnob` / `twoWayHinge` |
| `+0x309`.3 / .4 / .5 | `blowOffHinge` / `blowOpen` (1) / `ignoreBullets` |
| `+0x309`.6 / .7 | `noClose` / `noPortal` |
| `+0x159`.7 | `usable` (1), the entity use flag of BRAIN-TASKS SetUsable |

It also reads `team`, `syncLock`, `buddy`, `globalExpression`, `requires`,
`removeItem`, `triggersize` (32), `maxFocusDistance` (48),
`minFocusDistance`, `wait` (code default 10) into `+0x34c`, `openDegrees`
into `+0x450` and its negation into `+0x454`, `hingeOffset`, `playerWillOpen`
and `noFocus`. It looks up an area portal from the door bounds through the
render world (`+0xcc`, stored at `+0x4ac`; **Inferred** FindPortal) and,
unless `no_touch`, posts `<spawnDoorTrigger>`, which only computes the
use-focus geometry; no trigger entity is spawned.

`rvRotatingDoor` Spawn (`0x20038760`):

1. Sets the parametric physics self, origin (entity origin `+0x1e4`) and
   axis (`+0x1c0`).
2. Builds a clip model (0xc0 bytes) from the `clipmodel` key, else from
   `model` unless the entity flag `+0x15a` bit 2 is set, through
   `0x202e4f40` with friction-like constants 0.1/0.1. **Inferred:** this
   loads the HKXS body named by `model`.
3. Physics contents `0x10003` (id −1) and a one-argument physics call with
   `0x10003` (**Inferred** clip mask). Raw HKXS filter 3 is consistent with
   bits 0|1.
4. `SetLinearExtrapolation(NONE, now, 0, origin, 0, 0)` and
   `SetAngularExtrapolation(0x42, now, 0, axis angles, 0, 0)` (Doom 3
   `idPhysics_Parametric` argument shapes; enum values **Inferred** from the
   GPL numbering).
5. `openDegrees` → `+0x728` (code default 135), `time` → `+0x71c` (default
   1; ctor value 2). Clears the sound override string `+0x4c8` and the
   duration override `+0x4c4`, then `BecomeInactive(all)` (entity virtual
   `+0x48`).

The Think also prints `rvRotatingDoor %d '%s' has no hkEnt!` when the
physics has no Havok entity: the door expects a Havok body.

### 2.3 Activation gates (`activate`, `0x200363c0`)

In order: not `enabled` → ignore. With an activator: `noClient` and a player
→ `snd_locked` at most once per second, ignore; `noAI` and an AI → ignore.
`locked` while closed → buddy/unlock handling, and `locked` > 1 stops there.
A `syncLock` door that is not closed → ignore. Then the activator handle is
stored at `+0x350` (null without activator) and Toggle (`+0x2f8`) runs with
(activator, propagate = true).

### 2.4 Post-spawn

The post-spawn callback (`0x200399f0`) reads `start_open`. When true it
processes `activate` **synchronously with the door itself as activator**,
then links team members, sets the portal state (open while any team door is
not closed) and stores the first targeted light (`rvDoor::FindLinkedLight`,
`+0x4bc`). The trapdoor has no team and no
`target` key, so it has no team and no light. **The trapdoor therefore
spawns closed and opens itself through the ordinary Toggle path; it is not
placed open.** The frame of the deferred pass relative to the first Think is
Unknown.

### 2.5 Toggle (`+0x2f8`, `0x20038d70`)

State `+0x30c`: 0 closed, 1 opening, 2 open, 3/4 closing. The current
physics angles are read first.

| State | Activator | Effect |
|---|---|---|
| 0 | any | `BecomeActive(3)`; portal open; smart-open side test unless `noSmartOpen`/`twoWayHinge`; target delta `+0x72c` = `openDegrees`; state 1; sound override `+0x4c8` if set (then cleared), else `snd_open`; with propagate, Toggle(activator, false) on closed team members |
| 1 | self | unless `noClose`: state 3, delta 0 |
| 1 | other | ignored |
| 2 | other, `toggle` set | close (below) |
| 2 | other, no `toggle` | close only when the activator is the local player and its fields `+0x15d0`/`+0x1a90` point at this door (**Inferred**: its use target); otherwise cancel `activate` and re-post it on itself after `wait` |
| 2 | self | if `0x20036a60` finds the player (unless `noClient`) or an AI in the doorway, cancel and re-post after `wait`; else close |
| 3, 4 | self | reopen: state 1, delta `openDegrees` |
| 3, 4 | other | ignored |

Close: unless `noClose`, cancel pending `activate`, state 4, delta =
−(last delta `+0x724`); with propagate, Toggle(activator, false) on team
members that are not closed. No sound at close start.

Motion (all states that start a move): when state is 1, the activator is
another entity and `noSmartOpen` is clear, a sign test of the activator's
offset against the door's stored axes negates the delta (**Inferred**:
open away from the activator). `+0x724` = delta. The axis is X (roll) with `x_axis`, else Y
(pitch) with `y_axis`, else Z (yaw). Duration (s) = |delta / openDegrees| ×
`time` (or × `+0x4c4` once, when set). Angular speed = delta / duration.
`+0x730` = duration × 1000 rounded by `fistp`; `+0x720` = now + that;
`+0x734` = now; then `SetAngularExtrapolation(2, now, ms, current angles,
speed, 0)`. **Inferred:** type 2 is GPL `EXTRAPOLATION_LINEAR`, which stops
at the end of the duration.

### 2.6 Think completion (`0x20038a20`)

Each Think runs the base entity Think, the visual update and the linked
light update. When `now ≥ +0x720`:

- **State 1 → 2.** Unless the spawn arg `toggle` is true, post `activate`
  on itself after `wait` (the auto-close). Then for every key with prefix
  `triggerOpened`, find the entity by name and post `activate` with the
  stored activator. Missing names are skipped silently.
- **State 3/4 → 0.** `BecomeInactive(all)`; portal closed unless a team
  member is still open (`noPortal` forces open); sound key `snd_close`; for
  every `triggerClosed*` key, `activate` with the stored activator on the
  named entity, missing names skipped.
- Then, on every Think at or after the end time, the door and its team
  members pass their Havok entity to two Havok-world calls with zero
  vectors (**Inferred**: velocity reset); a door without a Havok entity
  prints the `has no hkEnt` warning instead.

The door stays active (thinking) while open; only closing completion makes
it inactive. The portal call passes blocking bits 0 (open) or `0x17`
(closed) with the portal handle `+0x4ac`.

### 2.7 What this means for the trapdoor (derived)

1. Post-spawn: self `activate` → opening, pitch delta −100 over exactly
   1000 ms at −100°/s, `snd_open` = `door_default_open`.
2. Open (state 2). `toggle` → no auto-close post. Thinking continues.
3. Greeter node 10 faces it: FaceEntity uses the **physics origin**, the
   hinge (−288, 576, 310), which rotation does not move. From the goal
   (−271, 702) the ideal yaw is about −97.7°.
4. `shut_door` activates it with the touching player as activator
   (**Inferred**: GPL trigger target activation passes the toucher). State 2
   + outside activator + `toggle` → close at once, without the doorway
   occupancy test: delta +100 over 1000 ms.
5. Closed (state 0) at the first Think ≥ 1000 ms later: inactive, portal
   closed, `snd_close` = `trap_door_close`, `triggerClosed` lookup of
   `door_default_closed` finds nothing.

### 2.8 Keys present but without effect here

- `noAutoClose`: the string is read only at `0x20037700`, in the
  `rvPhysicsDoor` code. `rvRotatingDoor` never reads it.
- `wait 0`: read, but used only by the auto-close and re-post paths, which
  `toggle` disables for this door.
- `triggerClosed door_default_closed`: no entity carries that name.
- `usable 0`: clears the use flag; the player cannot use the door. The
  consumer of that flag was not traced (BRAIN-TASKS §7).

## 3. Greeter chain after node 10 (Data + existing contracts)

Node 10 leaves through its normal output [prim ReachedAndFacing] to node 31.
Its probe 58 ShowTip (`#str_007236`, `mAutoHide false`) is already the first
player-facing event of node 10. Exit of 10 stops the move.

| Order | Node | Original data | Contract status |
|---|---|---|---|
| 1 | **31 Wait** (30) | `mDuration "100 0"` = 100 ms. Output 1 → 11 when [prim Timer] **and** ReachedLocation(`trap_door_room`, `Player`); output 2 → 32 when [prim Timer] | Wait/Timer: implemented and runtime-tested as literal milliseconds (movetrain, shake_train); the greeter route host must admit it. ReachedLocation 100: contract established, not implemented; it needs the player's location record (`+0x154`, Unknown). `trap_door_room` is ENTS 1612, AAS location 26, phantom box (−248…−64, 544…768, 312…432), the room above the trapdoor |
| 2 | **32 Animate** (51) | `st1_nil_nml_idle` (global HKXA, 41 frames, 1.667 s; clip lookup rules in AI-PRESENTATION §2) → [prim AnimFinished] → 31 | `SourceAIScriptedAnimV1` passed for the interrogator shell bodies; the greeter body has no clip profile yet (CONTINUE item 3), so this is a refusal today |
| 3 | **11 Say** (12) | `lipsync_trn_1105_ka1` ("Down here, quickly. Kreisau agents are waiting for you on the other side.") with probes 53 WatchTarget (52), 34 Animate `srp_tyd_point_floor` (53 frames), 69 HideTip (307); normal output [prim SayFinished] → 67 | Say/SayFinished: contract established (AI-ACTOR §5.1, stage 3 `SourceAISpeechV1`), not implemented. WatchTarget: unknown (identity only). HideTip: contract established, not implemented |
| then | 67 AnimOverrideIdle (54) `srp_tyd_nml_hide_idle` → 57 Animate `srp_tyd_nml_to_hide` (43 frames) → [AnimFinished] 60 Wait `"1 0"` → equal weighted {54 Wait 8 s, 55 Wait 10 s, 56 Wait 9 s} → nag Say 59/62/63 (probes 64/65/66 Animate `srp_tyd_nml_hide_talk`) → 60 | AnimOverrideIdle store: implemented; the rest as above |

Notes:

- The 31 ↔ 32 loop polls the player's location only between idle plays. A
  player who enters `trap_door_room` during the idle clip is noticed after the
  clip ends plus up to 100 ms.
- **Next player-facing event after node 10:** node 11's line and floor point
  when the player enters `trap_door_room`, plus HideTip of the node 10 tip.
- In parallel, the player's descent fires ENTS 1676 (objective 1 success)
  and 2095 `shut_door` (trapdoor close + `player_safe`). The greeter's root 5
  (OnUsed `player_safe`) then starts X5 in TRAINYARD-CHAIN.

## 4. Native plan: `SourceRotatingDoorOldV1`

Goal: one native `sewer_trapdoor` that behaves as §2 from original data,
reusing the mover, animated-door, collision and registry systems. No new
loader, no replacement geometry, no pose snap. Every unsupported key, branch
or target is a named refusal receipt.

### Stage T0 — portable (`src/wolf2009`)

1. **Qualification `TrainyardSewerTrapdoorKeysV1`.** Admit exactly the 13
   ENTS keys of §1.1 and the compiled chain of §1.2 (child spawnclass
   `rvRotatingDoor`). Refuse by name `team`, `locked`, `syncLock`, `buddy`,
   `globalExpression`, `requires`, `target*`, `triggerOpened*`, `x_axis`,
   `rotation`, `clipmodel`, `noClose`, `noSmartOpen`, `twoWayHinge`,
   `noClient`, `noAI` and any other key until its branch exists. Record
   `noAutoClose` as `unconsumed-by-class` and `wait` as `unused-with-toggle`.
2. **Body profile `HingedSolidBoxV1`.** Extend `DecodeSourceEntityBoxBody`
   (today `SolidBoxV1`/`PhantomTriggerBoxV1`) with root
   ConvexTranslate(Box), raw filter 3, identity rotation. Require the box to
   equal the PROC model bounds (0, −32, −2)–(64, 32, 2).
3. **State machine `RotatingDoorOldStateV1`.** A pure function of
   (state, activator kind self/other/none, flags, last delta, now) that
   returns the next state, delta, axis, duration ms, end time, sound key and
   trigger keys, exactly as §2.3–2.6. Include the branches the trapdoor does
   not use (auto-close re-post, self close with occupancy result as an input,
   reopen while closing) so the tables cover the class.

Portable acceptance:

- Original trainyard: the 13 keys, chain and body qualify; changing any
  value, adding `team` or a rotation, or a body with filter 1 refuses with
  that name.
- Table tests: closed + self → opening, −100, axis Y, 1000 ms; opening done
  → open, no auto-close; open + other with toggle → closing, +100,
  1000 ms; closing done → closed, `snd_close`, `triggerClosed` key list;
  opening + other → ignored; opening + self → closing; closing + self →
  reopen; disabled → ignored; without toggle the open state re-posts after
  `wait`.
- Duration formula with `time` 1 and 2 and a half-open delta.

### Stage T1 — native entity and post-spawn open (`src/idtech4a_gameplay`)

1. Add a second policy to the mover seam,
   `WolfSourceMover_NativeRotatingDoorOldV1`, with the same
   Prepare/Spawn/Query/Take split as `NativeSourceMover`. Its native class
   derives from GPL `idMover` like `idWolfSourceMover`, so it owns an
   `idPhysics_Parametric`: the same physics class the original uses. Motion
   is `SetAngularExtrapolation(EXTRAPOLATION_LINEAR, start, ms, base, speed,
   ang_zero)`; there is no pose interpolation outside the physics.
2. Visual: the PROC submodel already registered by `Wolf2009_LoadRenderWorld`
   as `<proc>#sewer_trapdoor`. This is the first gameplay consumer of a PROC
   submodel; refuse if the alias is absent or its bounds differ.
3. Collision: a clip model from the T0 box in the door's local frame (hinge
   at the origin), contents `CONTENTS_SOLID | CONTENTS_OPAQUE` as the named
   stand-in `HingedSolidContentsV1` for original physics contents `0x10003`.
   Pushing uses the GPL mover push, named `ParametricPushStandInV1`; the
   Havok kinematic behavior is Unknown and `dmg 0`.
4. Registration: `WolfNativeSourceRegistry::Bind("sewer_trapdoor",
   "func_door_rotating_old", …)` with a receiver that maps `activate` to the
   §2.3 gates and Toggle. Spawn at map assembly, before greeter OnSpawn.
5. Post-spawn: after map assembly, deliver one `DeferredPostSpawnActivateV1`
   (self `activate`) through the receiver. The open completion is the first
   Think with `gameLocal.time ≥ end`, observed from the physics state, as in
   the animated door's completion handoff (`TakeCompletion`). A timer alone
   never completes it.
6. Sounds via the native sound path for `snd_open`/`snd_close`; a missing
   shader or sample is a receipt, not a gate. The single-area native render
   world has no portals: record `AreaPortalAbsentV1` for both portal calls.
7. Teardown: the owner removes the entity before the registry; reload
   gives a new generation and one new post-spawn activation.

T1 acceptance (original trainyard, Android 15 x86_64):

- One native `sewer_trapdoor`; `FindEntity` returns it; physics origin
  (−288, 576, 310); render model `#sewer_trapdoor` with 12 triangles.
- At assembly its clip bounds are the closed box (−288, 544, 308)–
  (−224, 608, 312) ± 0.01.
- Exactly one post-spawn activation; opening lasts 1000 ms ± one frame;
  the physics angles end at pitch −100.000 and stay there; the state is open
  and the entity keeps thinking; one `snd_open` receipt.
- The open clip bounds match the rotated box of §1.3. A trace down through
  the hole at (−256, 576) from z 330 to 290 is blocked before the opening
  and clear after it.
- Reload and teardown leave zero live trapdoor owners and no pending event.

### Stage T2 — `shut_door` and closing

1. Add ENTS 2095 `shut_door` and 1676 `trigger_objective_success_1` as
   source trigger volumes with the existing `idWolfSourceTrigger` and
   `PhantomTriggerBoxV1` decode (reuse the trainyard exit volume code).
2. `shut_door` targets in spawn dictionary order: `trap_door` → receipt
   `source-absent`; `train_greeter_brainuse_4` → the existing multi-target
   relay pattern, which delivers `player_safe` to the greeter; and
   `sewer_trapdoor` → `activate` with the player as activator. 1676 → an
   explicit refusal until the mission system exists.

T2 acceptance:

- A real player touch fires `shut_door` once. In that frame the trapdoor
  enters closing (toggle path, no doorway test), +100 over 1000 ms, then
  closed: inactive, one `snd_close` (`trap_door_close`) receipt, one
  `triggerClosed door_default_closed` `target-absent` receipt, pitch 0.000.
- A second touch fires nothing (`trigger_once`). Touching before the door
  is open is impossible on the measured route; a replayed activation during
  opening is ignored (outside activator).
- The hole trace is blocked again after close. A player inside the sweep
  during closing produces a named `ParametricPushStandInV1` receipt.
- The greeter receives `player_safe` once through the relay; a stale or
  missing receiver is skipped as in the exit tests.
- Death and respawn do not repeat post-spawn activation or trigger
  delivery.

### Stage T3 — greeter node 10 to node 31

1. Node 10 resolves `sewer_trapdoor` at Enter; update the greeter move check
   that today asserts that the face entity is absent.
2. After `reached`, FaceEntity turns the greeter toward the physics origin
   (yaw about −97.7°, at most 180°/s); IsFacing becomes true on the 0.01°
   snap and StopMove; ReachedAndFacing becomes true from real origin and yaw.
3. 10 → 31 runs once. Node 31 then holds with a named refusal on
   ReachedLocation 100 (player location record) or on Animate 32 (greeter
   clip profile) until those stages land; no timer advances it.

T3 acceptance: as AI-MOVE §5.3 for facing and ReachedAndFacing, plus an
explicit hold receipt at node 31 naming the first unsupported condition or
task.

### Later

Church and caverns use further `rvRotatingDoor` keys (`team`, `locked`,
`playerWillOpen`, `noClose`, `ignoreBullets`, `noFocus`, `openAngle`,
`target`, `triggerCloseTorqueScale`). Each needs its own profile and
its branch from §2. `rvPhysicsDoor` (335 entities) is a different class.

## 5. Open questions

- The frame of the deferred post-spawn pass relative to the first Think
  and to arrival; whether anything can observe the trapdoor before it opens.
- The clip loader `0x202e4f40` and the Havok body it builds (the HKXS body by
  `model` is Inferred), the per-Think Havok velocity update, and the
  original contact response while closing on the player.
- Doom 3 angle convention and extrapolation enum values in Wolf's
  `idPhysics_Parametric` (Inferred from matching call shapes).
- `idTrigger_Multi` activation order and the activator it passes
  (TRAINYARD-CHAIN §7).
- Whether `rvRotatingDoor` saves its own members (`+0x71c…+0x734`): the type
  uses `rvDoor`'s save routine.
- The smart-open side test's members (`+0x418…+0x474`) and the player-use
  branch of state 2 without `toggle` (`+0x15d0`, `+0x1a90`): not needed for
  this door.

## Reproduce

Read-only inputs: the owner's `SP/base/maps/game/trainyard/trainyard.mpk`,
`assets.pk4` text members, the compiled declaration records and a private
copy of `Gamex86.dll`. Private scripts in `evidence/trapdoor/scripts` locate
strings and their references, read RTTI vftables, print disassembly ranges
from the retained `llvm-objdump` text, decode the HKXS body and print the
PROC model block. None of them writes outside `evidence/`.

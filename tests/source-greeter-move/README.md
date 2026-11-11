# Hosted greeter network and node 10 move start

`TrainyardGreeterMoveChecks.cpp` observes the explicit session profile
`SourceAIShellGreeterRouteV1` inside the ordinary trainyard session. In this
profile `SourceAIShellV1` owns the greeter's identity, wake, arrival relay
take, Use/Activate and pending-key latch, and the greeter route
(`MeasuredTrainyardGreeterRouteV1`) runs the greeter's brain network from the
shell's greeter Think. The route's actor owner supplies the one native
greeter body; the shell binds its head and accessory to that body.

Build with the native AAS, the audio bank and the session QA, then run:

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 -WithAas140 `
  -WithAnimationV2Checks -WithAudioBankChecks -Jobs 1 `
  -BuildDirectory build/engine-qa-move-x86_64
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -BuildDirectory build/engine-qa-move-x86_64 -RendererGameRoot 'C:\path\to\Wolfenstein' `
  -WithTrainyardGreeterMoveChecks -MoveCase Move|Teardown|Cancel|Stale
```

The switch implies the arrival director, stages the three character packs
and the English `kc1` scripted-voice pack, lists `aas28` in `aas_types`
and selects `wolf2009_aasProfile WolfAas140V1` before the map loads. It
cannot be combined with the separate greeter, exit, AI shell, actor or door
fixtures.

## Cases

Every case first checks the greeter's first Think: OnSpawn root 1 enters
node 2 (IgnoreEnemiesSet), the four probe leaves of node 2 are refused once
each, 2 → 24 runs the SetIgnorePanic no-op, and 24 → 73 (WatchTarget) is
held with an explicit refusal. No readiness request exists yet.

- **Move**: on the arrival tick the shell takes the relay, delivers its
  Use, and root 3 enters node 29 with one pending whole-bank request and
  consumes the latch. On the provider's Ready (in this pack, its first
  Update in that same Tick) the Think enters node
  35 (probes 6/33/72 refused), node 10 (probe 58 refused), issues the
  scripted move on the native aas28 route from area 6199 to area 8040,
  leaves the leg idle state, plays `srp_tyd_nml_train_door_open` once and
  activates the door once. The door endpoint and its end idle are
  observed once; nothing repeats for 60 more Thinks. `DoorOpenRootMotionV1` moves the
  greeter by the clip's own reference frame (model-space delta, actor yaw,
  owner `Relocate`, no collision): still through frame 147, then within
  0.01 of the sampled path to (-76.45, 82.03). The run phase then cycles
  `std_nil_nml_run` (model delta, yaw_current), seeks with GPL
  `WalkPathToGoal` on aas28, turns at maxTurnRunning 420 deg/s, snaps
  to the floor by trace and starts `std_nil_nml_run_to_st1` when the path
  seeks the goal within one stop length. It arrives within range and
  records `reached`. FaceEntity (`FaceTurnInPlaceV1`) then aims at the
  native `sewer_trapdoor`'s physics origin (the hinge), sets
  `MOVE_FACE_ENTITY`, turns `yaw_current` with TurnStepV1 at maxTurnIdle
  180 deg/s, snaps at FacingIdealV1 and calls StopMove. On the next greeter
  Think ReachedAndFacing is true from the real origin and yaw, 10 -> 31
  runs once (Exit stops the move) and node 31 Wait (100 ms Timer) holds
  with an explicit refusal on its first condition without a native
  contract (ReachedLocation, the player's location record); nothing
  advances it for 60 Thinks. Cancel then releases the retained request once.

Every case also checks `SourceRotatingDoorOldV1` (TRAPDOOR T1) before the
arrival: one native `sewer_trapdoor` (ENTS 2136) at the hinge
(-288 576 310) with the PROC submodel `#sewer_trapdoor` (12 triangles), the
closed `HingedSolidBoxV1` clip (-288 544 308)-(-224 608 312) and a hole
trace blocked by it at assembly; one `DeferredPostSpawnActivateV1` self
activate opens it -100 deg about Y over 1000 ms through GPL
idPhysics_Parametric angular extrapolation; the open completion is the
first door Think with the physics at rest at pitch -100; the open clip
bounds match the rotated box, the hole trace is clear, a repeated
post-spawn delivery is refused, the door keeps thinking, and `snd_open`
is a `SourceSoundBindingAbsentV1` receipt. Teardown requires zero live
rotating doors.
- **Teardown**: the same, ending with ordinary map shutdown instead of
  Cancel.
- **Cancel**: Cancel after the committed arrival, before the shell's next
  greeter Think takes the relay. The shell still latches the key; the
  route opens no request, and no move start or door activation follows
  in 240 frames.
- **Stale**: the greeter body is replaced under its name before arrival;
  the shell and route refuse it, no readiness request opens and nothing
  is rebound.

Each case requires zero live routes, readiness requests, AI shells, actors,
brains, pending keys and voice banks after `MapShutdown`.

## Portable contracts

`src/wolf2009/SourceAIScriptedMove.cpp` holds the recovered node 2/10
rules (IgnoreEnemiesSet, style bits, ReachedPos, ReachedAndFacing, Reached,
TryMoveStart). Its self-test (`wolf2009-source-ai-move` in the portable
CMake build) passed 36 checks on Android 15 x86_64.

`src/wolf2009/RotatingDoorOld.cpp` holds `SourceRotatingDoorOldV1` T0:
`TrainyardSewerTrapdoorKeysV1`, the class chain facts, `HingedSolidBoxV1` and
the `RotatingDoorOldStateV1` activate/Toggle/Think tables. Its self-test
(`wolf2009-rotating-door-old`, Windows zig C++11) passed 60 synthetic checks,
and 65 checks with the owner's `trainyard.mpk` (ENTS 2136, HKXS filter 3,
box (0 -32 -2)-(64 32 2)).

## Validated

SourceRotatingDoorOldV1 T1 + T3 (2026-11-11, Android 15 x86_64 emulator,
original trainyard): Move `5e71f182343c` (5968 checks / 1730 frames),
Teardown `b533454c9a13` (5963 / 1720), Cancel `13a91d1a651c` (2286 / 1030),
Stale `df7a364cd15f` (1779 / 820); all with `live_rotating_doors=0` after
MapShutdown. Trapdoor: entity 38, opening 0 -> 1000 ms (physics end 1000,
one `<reachedang>`), open completion at the first Think 1008, pitch
-100.0000, open clip (-301.08 544 309.65)-(-286.03 608 373.38), hole trace
0.444 (door) closed and 1.000 open. Greeter: arrival 9.237 from the goal at
26304 ms, yaw -23.672; FaceEntity yaw_ideal -95.2021 toward the hinge,
71.53 deg in 25 frames (max step 2.88), StopMove at 26704,
ReachedAndFacing true and 10 -> 31 at 26720; node 31 held on ReachedLocation
(100) `trap_door_room`/`Player` from 26832 for 60 Thinks. Not covered:
map reload (new generation), `shut_door` closing (T2), sound playback.

SourceAIMoveV1 M2 (2026-11-11): Move `1f6ffcf0ed92` (5593 / 1704; 824 units,
36 areas, 11 not on the planned route, stop at 113.4, arrival 9.24 from the
goal at z 312.25 vs retail 312.06), Teardown `f4381488f5d9`, Cancel
`34f803d24552`, Stale `cf03ebd8d1a1`.

SourceAIMoveV1 M1 (2026-11-11): Move `bd3e0a47aea8`, Teardown
`14741d263559`, Cancel `c4145db6655f`, Stale `b94b94ffa6cf`.

Earlier: 
Android 15 x86_64, original trainyard: Move `7476e6adc451` (3724 / 1333),
Teardown `a856e87e3765` (3719 / 1323), Cancel `7e451cbaf120` (2118 / 1030),
Stale `bfc120b16c37` (1611 / 820). Details are in
[TRAINYARD](../../docs/TRAINYARD.md).

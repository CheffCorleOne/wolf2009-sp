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
  observed once; nothing repeats for 60 more Thinks. The greeter keeps its
  origin (translation is refused) and ReachedAndFacing stays false.
  Cancel then releases the retained request once.
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

## Validated

Android 15 x86_64, original trainyard: Move `7476e6adc451` (3724 / 1333),
Teardown `a856e87e3765` (3719 / 1323), Cancel `7e451cbaf120` (2118 / 1030),
Stale `bfc120b16c37` (1611 / 820). Details are in
[TRAINYARD](../../docs/TRAINYARD.md).

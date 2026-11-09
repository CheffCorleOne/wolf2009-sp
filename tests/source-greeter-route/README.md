# Trainyard greeter route in the real session

`TrainyardGreeterRouteChecks.cpp` observes the production
`WolfSourceGreeterRoute` owner inside the ordinary trainyard source session.
It is compiled into the game module with `-WithAnimationV2Checks` and selected
by the runtime test switch:

```powershell
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -BuildDirectory build/engine-qa-x86_64 -RendererGameRoot 'C:\path\to\Wolfenstein' `
  -WithTrainyardGreeterChecks -GreeterCase Route [-WithOffscreenChecks]
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -BuildDirectory build/engine-qa-x86_64 -RendererGameRoot 'C:\path\to\Wolfenstein' `
  -WithTrainyardGreeterChecks -GreeterCase Stale
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -BuildDirectory build/engine-qa-audio-x86_64 -RendererGameRoot 'C:\path\to\Wolfenstein' `
  -WithTrainyardGreeterChecks -GreeterCase Ready|Lost|ReadyStale [-WithOffscreenChecks]
```

The switch implies the arrival director, stages the greeter detail pack
`streampacks/allies_kreisau_agt02.spk` and writes explicit session keys
`mapGreeterProfile`, `mapGreeterModel`, `mapGreeterRig` and
`mapGreeterVoiceLocale`. It cannot be combined with the separate actor or door
fixtures, because the route owns the exact `train_greeter` and `train_door_1`
names. Its QA replaces the arrival QA: the route itself takes the director's
relay request, so the arrival QA's direct take would race with it.

## What the cases check

**Route.** Assembly before wagon OnSpawn: inactive greeter at its authored
origin, idle door bound to the real wagon, no readiness request. Ordinary
frames until the committed arrival. On that arrival tick the route takes the
relay request once, keeps raw `train_Stop`, the relay's own `train_stop` and
the OnUsed key separately, enters node 3, and requests node 29 readiness from
the `ScriptedVOStreamInWholeBankV1` provider. The provider reports
`unsupported`. For 120 further frames the route re-evaluates node 29 once per
Think and does not advance: no actor start, no door activation. The door rides
with the wagon. The effect-free admission predicate refuses the replayed event
and variants with the incoming key, a wrong generation, a missing target, a
different relay, event 0 and no activator. Cancel releases the request once and
stops Think; a repeated Cancel releases nothing.

**Stale.** Before arrival the QA deletes the native greeter and spawns a
same-name replacement in its slot. The route refuses the relay's OnUsed (one
take, no readiness request) and never rebinds the replacement. The QA then
deletes the native door; the refused route does not react and the door owner
reports it missing.

**Ready, Lost, ReadyStale (bank-backed).** These need a build with
`-WithAudioBankChecks` (CMake `WOLF2009_SOURCE_AUDIO_BANK`), which backs
`WolfScriptedVoiceReadinessRequest` with the whole-bank `WolfScriptedVOReadiness`
provider through a thin adapter. The test stages the owner's
`streampacks/scripted_voice_kc1_trn_vo_english.spk`, the pack that node 29
expands to. The OnUsed tick opens a Pending request. On a later Think the
provider's own `Update` reports Ready for the whole 19-member bank. The route
passes it to `TrainDoorSignalRuntime::ConsumeVoiceReadiness`, which consumes it
exactly once. The route enters node 35, records the stored move-start animation
`srp_tyd_nml_train_door_open` and refuses node 35's three probe outputs (6 Say,
33 and 72 UseTarget). It then holds node 10 GoToAndFaceTarget with an explicit
refusal: there is no native AI locomotion and no move start, and the door's
`triggerStartMoveEnt` depends on that move start. The actor clip is not played
and the door is not activated. For 60 frames the retained request stays Ready
and is not consumed again. Then:

- *Ready*: Cancel releases the retained request once and stops Think.
- *Lost*: the core QA seam runs the ordinary native cache level-load purge.
  The next Think observes `lost` once and releases the request; the graph stays
  at node 10.
- *ReadyStale*: the greeter is replaced under its name. The provider reports
  `cancelled` (owner ended), the request is released and the route fails
  without rebinding.

Each case requires `WOLF2009_AUDIO_BANK_LIFETIME live_banks=0` at shutdown.
**Door appearance.** With `-WithOffscreenChecks` the existing trainyard camera
check captures the actual player camera in the route case after cancellation.
It hides and restores the bound door at the same simulation time and requires
door draw surfaces, a changed-pixel count above 64 and an exact RGB restore.
The door is in its idle pose; the opened pose is not rendered in this session.

Every case ends with ordinary `MapShutdown`: zero live routes, scripted actors,
directors and readiness requests.

## Validated

On Android 15 x86_64, original trainyard, commit `dd7fc53`. Route `728dfb4fd1a8` (3871 checks / 940 frames, with door GPU A/B); Stale `7484f2c4374f` (1635 / 800); Ready `b6dbf50e7f26` (3752 / 861, with door GPU A/B); Lost `85349fc393ed` (3752 / 862); ReadyStale `f8ce627e1cc4` (3732 / 862). The bank-backed runs ended with `live_banks=0`. Details are in [TRAINYARD](../../docs/TRAINYARD.md).
## Not covered

Node 35 probes (Say `lipsync_trn_0505_ka1`, HUD compass, mission give), node 10
locomotion and its move start, the door activation that depends on it, the
door's endpoint and end-idle in this session, idAI Use/Activate, speech and
playback, and the doorway blocker. The separate door/actor fixtures on `empty`
remain the only native evidence for door playback. Results are in
[TRAINYARD](../../docs/TRAINYARD.md).
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

**Door appearance.** With `-WithOffscreenChecks` the existing trainyard camera
check captures the actual player camera in the route case after cancellation.
It hides and restores the bound door at the same simulation time and requires
door draw surfaces, a changed-pixel count above 64 and an exact RGB restore.
The door is in its idle pose; the opened pose is not rendered in this session.

Both cases end with ordinary `MapShutdown`: zero live routes, scripted actors,
directors and readiness requests.

## Not covered

No readiness provider reports Ready, so the post-type19 continuation (actor
start ACK, same-tick door activation, endpoint and end-idle) is not executed in
this session. Its component behavior is the separate door/actor QA. Node 35
probes, node 10 locomotion, Say, idAI Activate and the doorway blocker remain
unimplemented. Results are recorded in [TRAINYARD](../../docs/TRAINYARD.md).

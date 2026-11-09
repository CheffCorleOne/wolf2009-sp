# Trainyard exit in the real session

`TrainyardExitChecks.cpp` observes the production `WolfSourceTrainyardExit`
owner (`src/idtech4a_gameplay/NativeSourceTrainyardExit.*`, profile
`MeasuredTrainyardExitV1`) inside the ordinary trainyard source session. It is
compiled into the game module with `-WithAnimationV2Checks` and selected by:

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 `
  -BuildDirectory build/engine-qa-audio-x86_64 -WithAnimationV2Checks -WithAudioBankChecks -Jobs 1
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -BuildDirectory build/engine-qa-audio-x86_64 -RendererGameRoot 'C:\path\to\Wolfenstein' `
  -WithTrainyardExitChecks -ExitCase Exit|Stale|Missing
```

The switch implies the greeter route (and so the arrival director), stages the
greeter detail pack and the owner's English `scripted_voice_kc1_trn` pack, and
writes the session key `mapExitProfile`. With an audio-bank build the greeter
consumes its node 29 Ready and holds node 10 before the player leaves; without
it the greeter holds node 29. The QA replaces the greeter route QA.

The QA only runs ordinary frames. Its user commands steer the player with
`forwardmove`/`rightmove` in the current view frame; the view is not turned and
the player is never moved directly.

## What the cases check

**Exit.** Assembly: three trigger volumes from their decoded HKXS boxes
(`exit_train_trigger` live, `trigger_once_1` and `layer3_trigger_once_2`
dormant with `triggerFirst`), `CONTENTS_TRIGGER`, identity axis; the solid
doorway blocker bound to the wagon; relay `train_greeter_brainuse_2` and
`train_shaker`. During the ride the blocker keeps its wagon-bound offset.
shake_train node 19 Uses `trigger_once_1` once on the first tick after its
`"1 1"` (1000 ms) Timer, which arms it without firing; node 31 is held as a
refusal; `layer3_trigger_once_2` is never armed. At the arrival tick movetrain
node 7 starts its `"6000 0"` (6000 ms) Wait. The player walks into the doorway
for about 5.5 s and must reach the blocker without passing it; the exit
trigger does not fire. On the first tick after 6000 ms node 8 removes exactly
the blocker (the wagon and its bound door stay), and node 9
`ExecConsoleCommand CheckOneOfMany` is held as a refusal. The player then walks
out and `exit_train_trigger` fires once for the player. The relay delivers
`player_leaves_train` with the player as actor to the greeter (idAI Use:
CanUseKey root 4, latched; activate refused; the next Think refuses the node 4
-> node 10 transition) and records `nazi_interragator_1` as
implementation-absent. Walking back and forth through the area delivers
nothing again; the trigger has removed itself. Registry replays of the shake
Use and the blocker removal are refused as already delivered.

**Stale.** The greeter is replaced under its name before arrival. The route
refuses its relay; the exit relay still activates once, skips the stale
greeter and records the interrogator as implementation-absent.

**Missing.** The native relay is deleted after the blocker removal. The exit
trigger fires once and reports its target as expired; nothing is delivered.

Every case requires ordinary `MapShutdown` with `live_exit_owners=0` and
`live_triggers=0`.

## Not covered

Native AI, `idAI` activate/unhide, the interrogators and hostages, node 10
locomotion, `ExecConsoleCommand`, `EnablePlayerWeapons` and the later shake
nodes, the blocker's visual model, Veil, save/restore and ARM64 devices.

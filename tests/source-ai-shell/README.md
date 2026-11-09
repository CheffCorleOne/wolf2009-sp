# Native trainyard source AI shell checks

This probe runs in the normal trainyard session through the GPL Doom 3 game
module. The fixture stages the owner's original map and character archives
and explicitly selects SourceAIShellV1.

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 -WithAnimationV2Checks -Jobs 2
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -RendererGameRoot 'C:\path\to\Wolfenstein' -WithTrainyardAIShellChecks
```

The probe checks five distinct bodies, five separate heads and six
accessories. It verifies original model identities and joint counts, head
binding to each body's chest, matching copied neck, shoulder and head
transforms, and each accessory's original local transform on its head or
body joint. The checks read actual native palettes and generate ten
deformed mesh snapshots after ordinary game frames.

The two interrogators stay hidden until the player overlaps the armed
`trigger_once_1` on the moving wagon. Their native activation events
unhide them once. The first interrogator's OnSpawn stops at type19 node 11
and the greeter's OnSpawn stops at type49 node 2. The train arrival relay
latches `train_stop` on the greeter. After the player walks out of the
wagon, the real exit trigger and relay deliver `player_leaves_train` to
the greeter and first interrogator. Each actor retains the key while held
at its first unsupported OnSpawn destination. A QA-only keyed activator
also sends `player_safe` through the greeter's native Activate callback.

The hostages remain hidden throughout those source events. The QA calls
the native Activate entry point for each hostage to inspect the head and
accessories in visible frames, then repeats activation to check that it
does not wake the actor a second time. The original execution graph owns
the later hostage Show events. The five authored actors leave
`noUnhideOnUse` unset. The probe checks that source configuration.
This headless probe covers native poses, mesh snapshots, event delivery and
teardown. GPU pixel output, scripted execution animation, speech and AI
movement are separate stages.

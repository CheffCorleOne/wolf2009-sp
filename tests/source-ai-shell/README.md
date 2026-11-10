# Native trainyard source AI shell checks

This probe runs in the normal trainyard session through the GPL Doom 3 game
module. The fixture stages the owner's original map, character archives and
the English `scripted_voice_wi1_trn` / `scripted_voice_wi2_trn` packs, and
explicitly selects SourceAIShellV1.

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 -WithAnimationV2Checks `
  -WithAudioBankChecks -Jobs 1
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -RendererGameRoot 'C:\path\to\Wolfenstein' -WithTrainyardAIShellChecks `
  -AIShellCase Chain   # or Stale, MissingMember
```

The probe checks five distinct bodies, five separate heads and six
accessories. It verifies original model identities and joint counts, head
binding to each body's chest, matching copied neck, shoulder and head
transforms, and each accessory's original local transform on its head or
body joint. The checks read actual native palettes and generate deformed
mesh snapshots after ordinary game frames.

The two interrogators stay hidden until the player overlaps the armed
`trigger_once_1` on the moving wagon. Their native activation events
unhide them once. By then shake_train has run nodes 31–34 in its arming
tick. The first interrogator's network runs `SourceInterrogatorChainV1`. In
its first Think it enters 11, 20, 2 and 38 with twelve probe entries. The
check reads every action receipt, the native SetScriptedManual, idle, death,
no-target and undying fields, and both type19 receipts. Each bank must be
Ready once with its measured member count; the network does not wait for
the banks. Probes 23 and 24 show both hostages through SetScriptedManual,
not through Activate. A later QA Activate on these visible hostages
neither wakes nor shows them again.

The greeter's OnSpawn stops at type49 node 2, and the train arrival relay
latches `train_stop` on it. After the player walks out of the wagon, the
real exit trigger and relay deliver `player_leaves_train` to the greeter and
the first interrogator. The greeter keeps the key. The interrogator's OnUsed
consumes it, enters 43, 21 and 22, and holds before type51 Animate node 25
without re-entering anything in later frames. A QA-only keyed activator also
sends `player_safe` through the greeter's native Activate callback.

Cases:

- `Stale`: before any source event the QA deletes the second interrogator's
  body. `trigger_once_1` skips it, node 20 records `owner-absent`, and probes
  3, 39 and 45 are null-subject no-ops.
- `MissingMember`: the staged wi2 copy lacks its final member
  (`tests/source-audio-bank/MakeInterrogatorFixtures.mjs missing`). The bank
  is Ready with 8 members, and the measured inventory refuses it.
- `ExecStale`: about 3 s into the execution the QA deletes the second
  interrogator's body. `execution_gunshot2` is never activated and the
  stale actor no longer answers queries.
- `ExecMissingTarget`: `blood_decal_2` is deleted before frame 206. The
  second hostage's `trigger` frame command records `missing-target`.

### Execution scene (`SourceAIScriptedAnimV1`)

From node 25 the QA follows the scene to its hold. Animate 51 starts
`srp_tyd_execute_nzi_one` through ScriptedAnim, probe 26 starts
`srp_tyd_execute_nzi_two`, and KillTarget 69 plays the hostages' override
death clips. The clips are the compiled `model_trainyard_base` blocks:
273, 281, 283 and 285 frames. The checks cover:

- AnimFinished on node 25 follows the native remaining-time rule. It is
  false while more than `blendFrames × 1000/24` ms remain and true after.
- Each actor's State_ScriptedAnim ends at its own 4-frame stop. The second
  interrogator stops about 333 ms after node 25 finishes.
- Every frame command is dispatched once at its zero-based frame. `speak` and
  `fx_offset` are named refusals, and so is `interactive_ragdoll`. `trigger`
  activates the four speaker/decal targets once, with the right activator.
- The network runs 25 → 17 (probes 16, 46, 47) and holds before Hunt node 6
  without re-entering a task.
- Synthetic cases: a missing clip finishes on the next tick, and an absent
  death override is recorded as the unchecked choice's failure.

Teardown must report zero AI owners, pending keys, stream owners, scripted
animators and execution targets, and `live_banks=0` when the run was
bank-backed. This headless probe covers native poses, mesh snapshots,
scripted clip playback and frame commands, event delivery, brain actions and
teardown. GPU pixel output, speech, ragdoll and AI movement are separate
stages.

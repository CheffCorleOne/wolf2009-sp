# Continue development

Read [AGENTS.md](../AGENTS.md), [CHECKPOINT](CHECKPOINT.md),
[DEVELOPMENT](DEVELOPMENT.md) and [NEXT-STEPS](NEXT-STEPS.md) before changing
code. The target is a playable, fully reverse-engineered SP campaign. No
mission is complete. Reviewed 2026-11-09.

## Priority

Work on the core game: native gameplay handlers, story/event connectivity and
campaign-wide reverse engineering. Controls, HUD, pause menu, settings, use
prompt, compass, ammo counter and weapon switching are deferred.

## Where the reverse-engineering results are

| Doc | Content |
|---|---|
| [CAMPAIGN-RE](CAMPAIGN-RE.md) + [JSON](CAMPAIGN-RE.json) | Mission order and transitions; all 154 brain node types, 51 conditions and 158 native classes with first map, method addresses, status and the DLL procedure still to read |
| [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) | Trainyard from the opened door to the first enemies, with a ranked handler list |
| [BRAIN-TASKS](BRAIN-TASKS.md) | Argument machinery, the expression evaluator, and static contracts for 30 node types and 13 conditions used by trainyard and mtw |
| [AI-ACTOR](AI-ACTOR.md) | idAI spawn/hide/activate/use, brain binding, scripted animation and move start, speech, scripted move and combat entry points, staged native plan |
| [AAS](AAS.md) | DewmAAS 1.40 layout, invariants across all 27 files, mapping to idAASFile, the original's query contracts |
| [BRAINS](BRAINS.md) | BrainGraph/BrainFlow/BrainTask kernels, including the `ProbeLeafPassV1` profile |

Private evidence for each lives in ignored `evidence/` folders
(`trainyard-chain`, `campaign`, `brain-contracts`, `ai-actor`, `aas`), each
indexed by a `FINDINGS.local.md` where present. Re-derive a narrow observation
from your own game copy if that evidence is unavailable.

## Validated on main

- Whole-bank SNDS registration and type19 readiness (run `9941b7d722f0`).
- Trainyard greeter route: relay → greeter OnUsed → node 29 Ready (with the
  `WOLF2009_SOURCE_AUDIO_BANK` opt-in) → node 35 → explicit hold at node 10
  (runs `728dfb4fd1a8`, `7484f2c4374f`, `b6dbf50e7f26`, `85349fc393ed`,
  `f8ce627e1cc4`).
- Measured trainyard exit, including the two-target relay, stale and missing
  receiver refusals and zero live trigger owners (runs `a750e88df993`,
  `4486dfa10d63`, `beb87523626f`). The greeter latches root 4 and holds
  node 10. See [TRAINYARD](TRAINYARD.md) for the greeter regression runs.
- `ProbeLeafPassV1` in the portable BrainTask kernel. No native host uses it
  yet, and native `EngineBrainTask.cpp` has not been rebuilt against it on main.
- Execution scene, `SourceAIScriptedAnimV1` (AI stage 2): Animate 51 /
  AnimFinished 50, KillTarget 69, frame commands, both interrogators'
  4-frame stops; the first interrogator holds before Hunt node 6
  (runs `bae191be3fa8`, `df3d9a74b535`, `66d598dd374b`, `518b03bc38bd`,
  `9f9bda679342`; pickups regression `d6f228e3af7a`, `aeedf88f5903`).
- `WolfAas140V1` loaded, routed and reloaded in run `016093956a80`.
  The GPL router traversed 31 WALK reaches from area 6199 to 8040 and
  the side table retained location 26. See [AAS](AAS.md).

APK `v0.11.0-engine-dev` (source `78cf2c9`) predates all of these.

## Source AI shell on the development branch

Branch `work/source-ai-shell-20261109` records the original base-first
PostSpawn Hide, OnSpawn/OnUsed arbitration and trainyard body, head and
attachment contracts. The shared kernel supports declared root priorities.
Its portable self-test passed 19 groups and the native literal host passed
run `5db43b860188` on the authored runtime fixture. The five native actors,
head and accessory binds, trigger wakes and arrival and exit relay keys
passed on original trainyard in run `a9fd9bd4a72b`. The greeter and first
interrogator stop at their first unsupported OnSpawn destinations.

## Next work, in order

Owner priority (2026-11-11): the engine, gameplay systems and the campaign
story chain. Graphics parity is deferred; [RENDERING](RENDERING.md) holds the
research, and branch `park/detail-texture-20261111` holds an unbuilt
`SourceDetailTextureV1` start. Keep docs short; spend effort on code.

State on main: the greeter opens the wagon (SourceAIMoveV1 M1), runs on aas28
and stops 9 units from `run_to_ladder_point` (M2, runs `1f6ffcf0ed92`,
`f4381488f5d9`). The interrogators' execution scene runs to the hold before
Hunt node 6. Stand-ins in use: GplWalkPathSeekV1, TraceFloorSnapV1 (no Havok
character), ApproachGoalStopV1, no blending between clips.

1. `SourceRotatingDoorOldV1` ([TRAPDOOR](TRAPDOOR.md) T0, T1, T3): the native
   `sewer_trapdoor` opens itself; the greeter's FaceEntity and
   ReachedAndFacing complete and node 10 -> 31 runs. Then node 31 Wait and 32
   Animate on the greeter (give the greeter body the `st1_nil_nml_idle`
   locomotion clip the same way as the run clips) and `ReachedLocation`.
2. `SourceAISpeechV1` (AI stage 3, [AI-ACTOR](AI-ACTOR.md)): Say 12 /
   SayFinished 10 on the whole-bank gate. Node 11 `lipsync_trn_1105_ka1` is
   the next player-facing event; the execution scene's `speak` frame
   commands are refused until then.
3. `SourceAIHuntV1` ([AI-COMBAT](AI-COMBAT.md) §7): portable layer first,
   then the native layer for the interrogators after node 17, AI hitscan and
   damage, then the first fight (TRAINYARD-CHAIN).
4. Trapdoor T2 (`shut_door` and the objective trigger), the remaining greeter
   chain, objectives and save entities; then continue CAMPAIGN-RE in campaign
   order (trainyard, then mtw).
5. Leftovers: greeter node 2 / interrogator node 11 via measured handlers;
   `ProbeLeafPassV1` in the native greeter route.

Subagent workflow that worked: research-only agents need no build slot;
give the single native-build slot and the emulator to one implementing agent
at a time; portable zig compiles count against the two-job budget.

## Rules that bit us

- Root motion: use `ModelDelta` (clip model space, rotated by the actor
  yaw). Turn clips end at an absolute `w` of ±π/4, not `w − w0`. Motion
  starts right after frame 147 (linear sampling toward 148).
- Generating C++ through shell heredocs or JS template literals loses the
  `\n` and `\"` escapes. Write edit scripts with the file tool, then grep for
  lines that are just `",`.

- `idDeclManager` canonicalizes decl names at the last `.`. A private decl
  name built from a model path (`x.md5r/...`) silently aliases others; strip
  the dots.
- Under Windows PowerShell 5.1, redirecting `*>` around the test scripts turns
  adb's stderr into terminating errors. Run them through
  `powershell.exe -File ... > log 2>&1` instead.
- The QA emulator is `evidence/avd/Wolf2009_QA35` (set `ANDROID_AVD_HOME`),
  started headless on port 5556.

- Prefer assembly and call sites over decompiler argument names. Several
  decompiler-suggested contracts were wrong, e.g. the relay key and the
  GoToAndFaceTarget method that calls `+0x45c`/`+0x460` (it is in Exit).
- Never advance a node with a timer, a placeholder Ready flag or a fabricated
  ACK. Stop at the first unsupported node with an explicit, observed refusal.
- Reuse of upstream Quake 4/Prey SDK code is allowed under its own license
  (see AGENTS.md); shared Raven ancestry is not proof of identical behavior.
- At most two compiler jobs across all agents and worktrees.

## Environment notes

- Windows PowerShell 5.1 is the documented shell. `prepare-engine.ps1` can
  fail on `fetch` in a fresh worktree. It populates the game and AAS source
  directories before checking the patch chain.
- `node tools/check-engine-patch.mjs` compares against the local `engine/`
  checkout, which must be prepared with the current patch chain.
- `tests/engine-patch-chain.mjs` fails on main before these changes (it cannot
  find `player-hulls.patch` in its temporary copy).
- Native tests run on an Android 15 x86_64 emulator in a unique
  `/data/local/tmp/wolf2009-runtime-<nonce>` directory. Do not reinstall or
  reset the user's app for core tests.

Use [ENGINE](ENGINE.md) for build/test commands and each `tests/*/README.md`
for selectors. Package another APK after a useful engine milestone.

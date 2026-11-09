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
- `WolfAas140V1` loaded, routed and reloaded in run `016093956a80`.
  The GPL router traversed 31 WALK reaches from area 6199 to 8040 and
  the side table retained location 26. See [AAS](AAS.md).

APK `v0.11.0-engine-dev` (source `78cf2c9`) predates all of these.

## Next work, in order

1. AI stage 1 `SourceAIShellV1` ([AI-ACTOR §8](AI-ACTOR.md)): interrogators,
   hostages and greeter as source actors; hide rule, think-gated brain tick,
   Activate/Use with `useKey`, `noUnhideOnUse`, brain binding, teardown.
2. Wire `ProbeLeafPassV1` into the native greeter route (node 35 probes 6/33/72
   and node 10 probe 58) with the handlers from BRAIN-TASKS.
3. AI stages 2–4: scripted animation and frame commands for the execution
   scene, speech on the whole bank, and scripted move for greeter node 10 on
   AAS. The real move start then plays the door-open clip and activates the
   door.
4. AI stage 5 and the first fight (TRAINYARD-CHAIN): Hunt, AI hitscan, pickups
   (`weapon_mp40`), objectives and save entities.
5. Continue down the ranked list in CAMPAIGN-RE in campaign order (trainyard,
   then mtw).

## Rules that bit us

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

# Development

Trainyard now reaches its original `train_stop` through native physics. The
next work follows the events after arrival. We favor shared C++ handlers that
can be qualified on other missions too.

The published `v0.10.0-engine-dev` APK is checkpoint `55fe192`; that build was
installed and tested in the emulator. Current reviewed engine work is on `main`.
Consult [CHECKPOINT](CHECKPOINT.md) and [CONTINUE](CONTINUE.md) before relying
on a newer source change.

## Where work belongs

- `src/wolf2009/`: portable archive, declaration, geometry, animation and source
  gameplay planning. It owns decoded facts without platform I/O.
- `src/idtech4a_*/`: native filesystem/render/game adapters and owning bindings.
- `patches/`: reproducible changes to the pinned idTech4A++ checkout in ignored
  `engine/`. [PROVENANCE.json](PROVENANCE.json) records upstream revisions.
- `tests/`: portable contracts and native runtime probes. Original inputs,
  private evidence and build outputs stay outside Git.

Keep the original installation and profile untouched. Use your own game copy
for assets; use a separate copy/profile for original-runtime experiments.
The repository and APK ship no original game archives or binaries.

The existing C++ engine is the reason for C++ adapters. Platform hosts supply
input, graphics/audio setup and filesystem access; campaign handlers stay
shared. Another operating system needs platform work and tests, not a rewrite
of every mission. Original maps and artwork come from the owner's archives.

## A working loop

1. Pick one missing behavior from [NEXT-STEPS](NEXT-STEPS.md). State its source
   input, current refusal and observable completion condition.
2. Check existing loaders and native bindings first. Collect only the original
   evidence needed to distinguish competing interpretations.
3. Implement the smallest shared handler under an explicit source profile.
   Preserve raw facts, ownership, generations and unsupported cases.
4. Test data/error contracts, then the actual native lifecycle. Keep a concrete
   failure case; check teardown and repeated use before widening the profile.
5. Record what ran and what remains unknown. Integrate reviewed source, then
   package a platform build when the engine change warrants it.

From the repository root, `npm test` runs the Node inventory tests;
`node tools/check-engine-patch.mjs` checks the prepared engine chain. Use the
relevant `tests/*/README.md` for source-input and native checks. Build/staging
instructions are in [ENGINE](ENGINE.md) and [ANDROID](ANDROID.md). Coordinate a
maximum of two compiler jobs across the team, including other worktrees.

## Lessons from trainyard

We favor assembly and call-site checks when decompiler output is ambiguous.
In the relay investigation, Ghidra inferred misleading arguments. The recovered
contract came from the handler and its caller: `target_brainuse` forwards its
own `useKey`. Incoming `train_Stop`, relay `train_stop` and greeter `train_stop`
remain separate raw facts. Avoid global case folding to hide the difference.

Rig names and parent lists also failed as an equivalence test. EmptyMale and
HumanoidMale have matching joint names/parents but different reference poses.
Use the declared source rig and its measured pose contract; avoid treating the
names as interchangeable or filling unsupported tracks from another rig.

Loading an archive was an early dead end for gameplay readiness. Type19 waits
for a whole streaming bank, while the old voice helper prepared one sample.
The bank needs a separate native decode/registration result for every member.
Avoid timers, placeholder Ready flags and fabricated actor ACKs to advance a
brain. The next handler should consume an observed result from its owner.

We favor ordinary native entities and frame progression. A successful syntax
check leaves event dispatch, pusher movement, animator endpoints and destruction
untested. Native tests must exercise those paths in the same runtime that the
player uses, including stale callbacks and repeated map teardown.

## What current evidence establishes

Native run `86c9d5c22a98` passed registry checks, the arrival director and the
scripted actor slice: 37 registry checks; 809 director checks over 790 frames;
1517 actor checks with 9 refusals over 472 frames. The actor uses the original
52-joint body, 182-frame clip and authored skin. AI, separate head binding,
root-motion extraction, VO and story execution remain outside that slice.
Door QA has its own lifecycle and result; check the current checkpoint.

Container tests establish readable assets. Screenshots establish visible
rendering. A playable mission needs its event chain, player interactions and
recovery paths to work together. No mission is currently claimed complete.

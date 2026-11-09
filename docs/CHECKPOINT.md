# Current checkpoint

Reviewed 2026-11-09. **No complete campaign mission is playable.** The current
C++ milestone covers trainyard arrival and the measured exit-trigger slice,
alongside isolated scripted components.
The actor/door implementation checkpoint is `580959c`; APK 0.11 adds manual
MP40 controls at `78cf2c9`.

## Released APK 0.11

[v0.11.0-engine-dev](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.11.0-engine-dev)
has version code 12 and source `78cf2c9`. The installed APK digest matches:

```text
df8b78db0ae07e2e2138df1e2bde97d60592183f03a61f3b2de3379666aad9e6
```

On developer-staged trainyard, explicit MP40 grant gives 32 rounds and 32 in
reserve. Actual Android 15 x86_64 / SwiftShader checks passed:

- Held fire consumed nine rounds; release left the counter stable and attack up.
- Reload entered the native Reloading phase, then produced clip 32 / reserve 23.
  Reloading a full magazine did not create ammo.
- Holding fire across Home/return kept the same process, released attack and
  stopped further shots. Empty magazine and repeat grant also worked.
- Normal Exit ended the process. A fresh process started unarmed before another
  explicit grant. Update-in-place preserved selection and nine staged-file hashes.

Both ABIs are packaged with QA disabled. The native sound cache is enabled;
audible output and complete weapon/VO audio remain unverified or unfinished.
The ordinary SAF import still prepares only `empty`, with weapon controls
disabled. No pickup, enemies or automatic campaign progression is added.
[Setup, controls and screenshot](ANDROID.md).

## Earlier APK 0.10

[v0.10.0-engine-dev](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.10.0-engine-dev)
was built from `55fe192` and installed on Android 15 x86_64. Its installed
digest matches the release:

```text
2b75bc10322aebdc913225471b0bbe744fe3e3f015a259d38420b0b32daec008
```

It includes ARM64 and x86_64, animation enabled and production QA disabled.
The native trainyard intro supports wagon travel, walking, looking and jumping
after developer staging. The normal folder import prepares only `empty`.
The player starts unarmed; the Android UI has no crouch button; audio is disabled.
Door, greeter and combat progression are absent from this APK.
[Setup and staging](ANDROID.md).

## Native C++ component results

Run `86c9d5c22a98`, Android 15 x86_64 / SwiftShader GLES3:

| Check | Result |
|---|---|
| Source registry | 37 checks passed |
| Trainyard arrival director | 809 checks / 790 frames passed |
| Scripted actor owner | 1517 checks, 9 refusal cases / 472 frames passed |
| Native trainyard camera / wagon GPU test | 127 checks passed; 173,682 pixels changed with wagon removal; exact RGB restoration |
| Shutdown | Normal game/filesystem/renderer teardown; zero open source descriptors |

The arrival director shows the three original static boxcars with real source
collision, then preserves the player through 60 normal frames. The relay
forwards its own `train_stop` key while retaining the incoming `train_Stop`.
The missing quake implementation remains an explicit refusal.

The actor uses the original 52-joint body, authored skin and 182-frame clip.
Start and completion come from actual native animator state. Duplicate starts,
conflicting commands and same-name replacements are checked. This slice has
no AI, separate head, root-motion extraction, animation frame-command dispatch
or VO. It does not establish the automatic greeting/door chain.

The shared diffuse-material qualifier now matches native DDS fallback behavior;
valid, absent and corrupt texture cases passed the two portable renderer tests.

Separate run `eb5902bfd353` passed three fresh animated-door cases on original
`empty`: 9141 checks / 484 frames for completion, 19 checks for stale start and
26 for actor replacement. The original two-joint door remained bound to the
real source wagon; both moved 193.600006 units in Y. The final source-frame
palette error was zero. The transition into the separately compressed end clip
has maximum component difference 0.0567755699; no identical-endpoint claim is
made. Native no-owned-collision policy, once-only completion and teardown passed.

That run also passed offscreen world and MP40 regressions. It did not separately
prove the door's GPU appearance, load the trainyard world or execute VO and the
automatic story chain. The production engine/game targets compiled for both
x86_64 and ARM64 with all seven QA options disabled. These component probes
do not make the automatic story chain available in APK 0.11.

Player/MP40 regression run `b1eef2682ddf` separately passed real player/mover,
fire/damage/reload and GPU checks on original `empty`. A reload sample emitter
was observed; audible output was not tested.

## Whole-bank SNDS and type19 readiness (component)

Branch `work/audio-ready-20261108`, opt-in `WOLF2009_SOURCE_AUDIO_BANK` (off
by default, not in an APK). Run `9941b7d722f0`, Android 15 x86_64 emulator,
headless, authored `wolf2009/runtime` map:

| Check | Result |
|---|---|
| Core bank service | 402 checks, 11 whole-bank refusal cases passed |
| Original English scripted-voice pack | 19 members, 19 strict native decode receipts, 19 exact cache registrations; 1622 frames / 1868544 PCM samples |
| Ownership | Shared join, pinned-unmount refusal, release, stale generation, purge loss, fresh generation, retry |
| Type19 provider (game module) | 49 checks: once-only Ready, frames alone stay Pending, cancelled, owner ended, slot reuse, failed, lost |
| Shutdown | Zero live banks, providers and leases; normal teardown |

This is a resource result. No sample was played or heard.
[Profile and limits](../tests/source-audio-bank/README.md).

## Trainyard greeter route (real session, stops at node 10)

Branch `work/greeter-chain-20261108`, commit `dd7fc53`, Android 15 x86_64
emulator, original trainyard map:

| Run | Case | Result |
|---|---|---|
| `728dfb4fd1a8` | No provider | 3871 checks / 940 frames; node 29 stays unsupported; door GPU A/B 7449 pixels, exact restore |
| `b6dbf50e7f26` | Bank-backed Ready, Cancel | 3752 / 861; one Ready (bank generation 1, 19 members) → node 35 → hold at node 10 |
| `85349fc393ed` | Ready, then Lost | 3752 / 862; loss observed once, progress kept |
| `f8ce627e1cc4` | Ready, then stale greeter | 3732 / 862; request released, no rebind |
| `7484f2c4374f` | Stale greeter before arrival | 1635 / 800; OnUsed refused |

The arrival tick delivers the relay's own key into greeter OnUsed. Node 29
advances only on the provider's consumed Ready. The actor clip is not played
and the door is not activated: node 10 locomotion and its move start do not
exist natively. Teardown left zero routes, actors, readiness requests and
banks. [Details](TRAINYARD.md).

## Trainyard exit trigger (native component)

`MeasuredTrainyardExitV1` uses native trigger contacts and the measured
Wait/arm and Wait/remove paths. Android 15 x86_64 runs on original trainyard
passed Exit `a750e88df993` (5964 checks / 1298 frames), Stale `4486dfa10d63`
(5961 / 1297), and Missing `beb87523626f` (5960 / 1297), each with normal
teardown and zero live exit owners and triggers.

The same source passed Route `17eba126525e`, Ready `67bb45f9d655`,
Lost `ba25fd250849`, ReadyStale `d93821b95faf`, and Stale `1d61ea769dd9`
in the headless greeter regression.

The player reaches the exit trigger with native user commands after the
blocker is removed. The exit relay latches the greeter's `player_leaves_train`
key and refuses the interrogator target. The greeter holds its node 10
transition and movetrain holds node 9. Automatic door opening, AI movement
and the first encounter remain open. [Profile and regression runs](TRAINYARD.md).

## Trainyard AAS route (native component)

Run `016093956a80`, Android 15 x86_64, original trainyard. The
`WolfAas140V1` adapter refused the unprofiled request and passed 200 checks
on each of two native loads. The Doom 3 router traversed 31 WALK reaches
from greeter area 6199 to the floor below `run_to_ladder_point` in area
8040. Its side table retained location 26, `trap_door_room`. Both files
were released during reload and shutdown. The greeter's actual movement
and actor-bound location evaluation remain open. [Details](AAS.md).

## Platform and gameplay limits

ARM64 has build validation; physical-device runtime remains untested. iOS and
WebAssembly have no working platform builds. Rendering is partial diffuse
compatibility, with explicitly omitted unsupported modulation layers. There
is no retail graphics, movement/filter or save-game compatibility claim.

AI/navigation, VO playback, the complete trainyard event chain, combat,
mission transitions and campaign save/reload remain work ahead. The bank
readiness above is not playback, and readiness save/restore is unsupported.

[Machine-readable results](CHECKPOINT-VALIDATION.json) ·
[Next acceptance gates](NEXT-STEPS.md) · [Handoff](CONTINUE.md)

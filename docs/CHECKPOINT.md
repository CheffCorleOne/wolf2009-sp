# Current checkpoint

Reviewed 2026-11-04. **No complete campaign mission is playable.** The current
C++ milestone covers trainyard arrival and isolated scripted components.
Its native implementation checkpoint is `580959c`. The installed APK is an
earlier checkpoint.

## Released APK

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

## Newer native C++ results

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
x86_64 and ARM64 with all seven QA options disabled; this source milestone
has no new APK.

Player/MP40 regression run `b1eef2682ddf` separately passed real player/mover,
fire/damage/reload and GPU checks on original `empty`. A reload sample emitter
was observed; audible output was not tested.

## Platform and gameplay limits

ARM64 has build validation; physical-device runtime remains untested. iOS and
WebAssembly have no working platform builds. Rendering is partial diffuse
compatibility, with explicitly omitted unsupported modulation layers. There
is no retail graphics, movement/filter or save-game compatibility claim.

AI/navigation, VO readiness, the complete trainyard event chain, combat,
mission transitions and campaign save/reload remain work ahead.
Frozen audio-bank and strict-decoder branches are next-stage prototypes;
they are not a native Ready bank or playback implementation.

[Machine-readable results](CHECKPOINT-VALIDATION.json) ·
[Next acceptance gates](NEXT-STEPS.md) · [Handoff](CONTINUE.md)

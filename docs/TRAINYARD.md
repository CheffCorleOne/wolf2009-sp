# Trainyard

The original map loads through the shared native engine/game path. Source startup creates the player and wagon; the wagon's measured OnSpawn movement uses native pusher physics to carry the player to `train_stop`, then delivers arrival once. This is the beginning of the mission.

## Published APK 0.10

The installed [v0.10.0-engine-dev](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.10.0-engine-dev) build is source checkpoint `55fe192`. It packages ARM64 and x86_64 libraries; actual device testing was on an Android 15 x86_64 emulator. [ANDROID](ANDROID.md) records the artifact hash and staging instructions.

SAF currently imports empty only. Trainyard requires explicit `Stage-EngineData.ps1 -WithTrainyard` with your own original data. In APK 0.10 the tested slice is the ride, limited walking/look/jump input and Home/return. The player starts with `weapon_none`, the touch UI has no crouch control, and sound is disabled. Door, greeter and combat progression are not available in this APK.

## Shared systems behind the ride

- `SourceMapBodies` retains HKXS/ENTS facts and exact world/instance/resource links. Scanning bodies on other maps establishes data coverage without activating their gameplay.
- `SourceCollisionBinding` supplies qualified native collision with owning source/mount leases. Compound geometry is preserved; render bounds or replacement brush boxes are not collision substitutes.
- `NativeSourceMover` uses actual `idMover`, `idPhysics_Parametric` and passenger contacts. The owner delivers source OnSpawn after assembly. The measured direct MoveToEnt profile retains source speed/target, exact endpoint and once-only arrival; later brain outputs remain explicit work.
- `NativeSourceStaticSkin` retains authored remaps and source geometry/UV/color identity. An explicit visual profile omits three measured modulation surfaces, totaling 100 triangles. Their original shader composition is unfinished; other unsupported materials can still show checker fallback.

The published intro uses the explicit `MeasuredTrainyardIntroV1` source-session profile. The selected wagon is ENTS2129 `starting_boxcar_model`, with the original `models/vehicles/box_car_a` compound: 66 boxes, 8 hulls and 12 cylinders. Its measured native response and established world placement qualify this instance only. They are not inferred defaults for every `func_mover`.

Native collision masks and cylinder envelopes are declared compatibility profiles. Retail Havok filter/clock/camera parity is unproven. The compound mover carries passengers but does not perform its own convex world sweep. The player's visible body also remains bootstrap support.

## Newer C++ checks

Current source includes native arrival, actor and door work newer than APK 0.10. The first three rows below are from native run `86c9d5c22a98`; door QA passed separately in `eb5902bfd353`.

| Slice | Observed checks |
|---|---|
| Exact-name source registry | 37 checks |
| Arrival director | 809 checks over 790 frames |
| Scripted source actor | 1517 checks, 9 refusals, 472 frames |
| Separate native door fixtures, run `eb5902bfd353` | 9141 checks over 484 frames |

The actor uses the original 52-joint body, 182-frame clip and authored skin in the actual native animator. This slice covers no AI, separate head binding, root-motion extraction, VO or story execution. Its factory/lifecycle tests do not establish the automatic mission chain.

Door run `eb5902bfd353` passed three fresh fixtures on the original empty map, including actor-start checks, parent/child movement, endpoints, replacement and cleanup. Its combined offscreen pass includes renderer/MP40 regression. Door appearance still needs its own GPU check. These fixtures execute no trainyard world, VO or automatic mission chain; a successful campaign greeter-to-door sequence is not established. See [CHECKPOINT](CHECKPOINT.md) for the detailed measurements.

## What must happen next

The whole scripted-voice SNDS bank needs actual decode/registration receipts and retained ownership before type19 can report readiness. Then the qualified relay/greeter request can advance through actual actor start, bound-door activation and its endpoint. Source keys, generations and actor-issued ACKs must survive that chain.

Combat, AI/navigation, Veil, objectives, original save/reload behavior and campaign transitions remain unfinished. The ordered acceptance gates are in [NEXT-STEPS](NEXT-STEPS.md). No complete mission or campaign is claimed playable.

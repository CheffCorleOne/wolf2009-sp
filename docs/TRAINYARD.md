# Trainyard

The original map loads through the shared native engine/game path. Source startup creates the player and wagon; the wagon's measured OnSpawn movement uses native pusher physics to carry the player to `train_stop`, then delivers arrival once. This is the beginning of the mission.

## APK v0.11

The [v0.11.0-engine-dev APK](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.11.0-engine-dev) is source `78cf2c9`, version code `12`, with ARM64 and x86_64 libraries. Manual equip/fire/reload passed on an Android 15 x86_64 emulator with SwiftShader GLES3. [ANDROID](ANDROID.md) records the verified artifact hash, native counter results and staging procedure.

SAF imports empty only. Trainyard requires explicit `Stage-EngineData.ps1 -WithTrainyard` with your own original data, kept read-only. After Start, the player still begins with `weapon_none`. Give MP40 equips a development weapon with clip/reserve `32/32`; hold Fire to shoot and use Reload to transfer ammunition. Repeating Give creates fresh `32/32` ammunition. Movement and action controls use two rows below the viewport; there is no crouch button. Empty lacks the weapon resources and keeps the weapon controls disabled.

`F8` logs actual native weapon state through `wolf2009WeaponStatus`. The host enables the native reload sample cache with `s_noSound=0`. Audible output is untested; full weapon and VO audio are unfinished. This manual slice has no normal pickup, enemy encounter or automatic greeter/door progression.

The APK test observed nine held-fire shots, clip consumption, the actual reload phase and ammunition transfer. Fire stopped after release and after Home/return in the same process. Empty-clip handling and repeat Give also passed. The two control rows restored the tested emulator viewport to 225 pixels high; [the capture](images/android-mp40-controls.png) shows that layout.

The earlier installed [v0.10.0-engine-dev](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.10.0-engine-dev), source `55fe192`, covered the wagon ride, limited walking/look/jump input and Home/return on the emulator. It had no touch weapon controls and disabled sound. Its artifact hash is retained in [ANDROID](ANDROID.md).

## Shared systems behind the ride

- `SourceMapBodies` retains HKXS/ENTS facts and exact world/instance/resource links. Scanning bodies on other maps establishes data coverage without activating their gameplay.
- `SourceCollisionBinding` supplies qualified native collision with owning source/mount leases. Compound geometry is preserved; render bounds or replacement brush boxes are not collision substitutes.
- `NativeSourceMover` uses actual `idMover`, `idPhysics_Parametric` and passenger contacts. The owner delivers source OnSpawn after assembly. The measured direct MoveToEnt profile retains source speed/target, exact endpoint and once-only arrival; later brain outputs remain explicit work.
- `NativeSourceStaticSkin` retains authored remaps and source geometry/UV/color identity. An explicit visual profile omits three measured modulation surfaces, totaling 100 triangles. Their original shader composition is unfinished; other unsupported materials can still show checker fallback.

The published intro uses the explicit `MeasuredTrainyardIntroV1` source-session profile. The selected wagon is ENTS2129 `starting_boxcar_model`, with the original `models/vehicles/box_car_a` compound: 66 boxes, 8 hulls and 12 cylinders. Its measured native response and established world placement qualify this instance only. They are not inferred defaults for every `func_mover`.

Native collision masks and cylinder envelopes are declared compatibility profiles. Retail Havok filter/clock/camera parity is unproven. The compound mover carries passengers but does not perform its own convex world sweep. The player's visible body also remains bootstrap support.

## Native component checks

The first three rows below are from native run `86c9d5c22a98`; door QA passed separately in `eb5902bfd353`. These component results do not establish a playable mission in the APK.

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

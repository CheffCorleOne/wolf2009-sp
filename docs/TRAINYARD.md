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

Door run `eb5902bfd353` passed three fresh fixtures on the original empty map, including actor-start checks, parent/child movement, endpoints, replacement and cleanup. Its combined offscreen pass includes renderer/MP40 regression. The door's idle-pose appearance in trainyard has since passed a separate GPU A/B check (run `686aad49c21f`, below); the moving and opened poses have not been rendered. These fixtures execute no trainyard world, VO or automatic mission chain; a successful campaign greeter-to-door sequence is not established. See [CHECKPOINT](CHECKPOINT.md) for the detailed measurements.

## Greeter route in the real session

The greeter route runs inside the ordinary trainyard session, after the arrival director. It is selected by the explicit session profile `MeasuredTrainyardGreeterRouteV1` and is absent from APK 0.11. It stops at type19 by design.

The original greeter BRAI fixes where type19 sits. Node 3 (OnUsed, key `train_stop`) has one output, to node 29 (type19 ScriptedVOStreamIn, `mDefName=scripted_voice_kc1_trn`). Node 29 has one output, to node 35 (type59 AnimOverrideMoveStart, `srp_tyd_nml_train_door_open`), and that edge carries node 29's own condition (ScriptedVOStreamedIn). Every path from the relay to the actor start and the door therefore passes through node 29. Startup node 2 also has a port-32 output to a separate type19 node 21 for the same bank; it is not on this route and is not executed.

The production owner `WolfSourceGreeterRoute` (`src/idtech4a_gameplay/NativeSourceGreeterRoute.*`) does the following:

1. The map runtime builds it. Before wagon OnSpawn it spawns the inactive greeter at its authored origin and the door bound to the real wagon at its authored pose. Data preparation checks that the relay, greeter and door names resolve uniquely and that the decoded chain is exactly 3 → 29 → 35 with target `train_door_1`.
2. Its own Think takes the director's relay request once. It checks the generation, event, relay source, exact greeter target, the relay's own key, the original activator (the wagon) and the captured spawn generations of the greeter and the door. Raw `train_Stop`, the forwarded `train_stop` and the OnUsed key are kept separately.
3. It delivers node 3 through `TrainDoorSignalRuntime` and opens a type19 request through `WolfScriptedVoiceReadinessRequest` (`NativeSourceScriptedVoiceReadiness.h`). That seam has the same shape as the whole-bank provider under development: one object per request and owner, `Update` from Think, `ConsumeReady` at most once, and separate Pending, Failed, Cancelled and Lost states. The only provider in this build, profile `ScriptedVOStreamInWholeBankV1`, reports `unsupported`. Nothing in the route can produce Ready: no timer, no flag and no caller boolean.
4. The continuation after Ready is implemented but has never run, because no provider reports Ready. It would take the actor owner's same-tick start ACK, activate the door and observe the door's endpoint and end-idle once. Static research (`TRAINYARD-CHAIN`) shows that node 35 only stores the animation name and moves on to node 10 (GoToAndFaceTarget) in the same Tick; the clip probably starts that move. The actor-start model, the node 35 probes and node 10 locomotion must be qualified before this continuation is enabled.

Results, Android 15 x86_64 emulator / SwiftShader, original trainyard, QA build of this branch:

| Run | Case | Result |
|---|---|---|
| `14cbfb9700fb` | Route | 3871 checks / 940 frames passed |
| `4df8b8c75482` | Stale | 1635 checks / 800 frames passed |
| `686aad49c21f` | Route + offscreen | same route checks; wagon and door GPU A/B passed |

In the route case the director arrived at game time 12800 ms. In that same Think tick and generation the route took the relay, delivered OnUsed and opened the node 29 request. It then re-evaluated node 29 once per Think for 120 frames without advancing: no actor start and no door activation. The door rode with the wagon: both moved +316.000000 units in Y from the QA baseline. The admission check refused the replayed event and variants with the incoming key, a wrong generation, a missing target, another relay, event 0 and no activator. Cancel released the request once and stopped Think; a second Cancel released nothing. In the stale case, a same-name greeter replacement placed before arrival led to one relay take and a refused OnUsed. No readiness request was opened and nothing was rebound. After the door was deleted, the refused route stayed inert. Both cases ended with ordinary teardown: zero live routes, actors, directors and readiness requests. In run `686aad49c21f`, after cancellation, the existing first-person camera check hid and restored the bound door at the same simulation time. In the actual player camera the door drew 1 surface with 504 indexes, changed 7449 pixels and restored to exactly the same RGB values; the wagon A/B in the same frame changed 172,107 pixels. This shows the closed idle pose in the GPU render; it does not prove texture parity or show the opened pose. That run did not pull the door BMPs, because the frame list was extended afterwards.

Not established here: readiness, speech or lip-sync; the actor start ACK, door activation, endpoint and end-idle *inside this session* (these are covered only by the separate component run `eb5902bfd353`); node 35 probes (Say, HUD, mission give); node 10 locomotion; idAI Use/Activate; and the solid doorway blocker that movetrain node 8 removes. Selectors are in [tests/source-greeter-route](../tests/source-greeter-route/README.md).

## What must happen next

The whole scripted-voice SNDS bank needs actual decode/registration receipts and retained ownership before type19 can report readiness. An adapter can then back `WolfScriptedVoiceReadinessRequest` with that provider, so the greeter route can continue through the actor start, bound-door activation and the endpoint in this same session. Source keys, generations and actor-issued ACKs must survive that chain.

Combat, AI/navigation, Veil, objectives, original save/reload behavior and campaign transitions remain unfinished. The ordered acceptance gates are in [NEXT-STEPS](NEXT-STEPS.md). No complete mission or campaign is claimed playable.

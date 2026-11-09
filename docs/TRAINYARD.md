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

Door run `eb5902bfd353` passed three fresh fixtures on the original empty map, including actor-start checks, parent/child movement, endpoints, replacement and cleanup. Its combined offscreen pass includes renderer/MP40 regression. The door's idle-pose appearance in trainyard has since passed a separate GPU A/B check (runs `728dfb4fd1a8` and `b6dbf50e7f26`, below); the moving and opened poses have not been rendered. These fixtures execute no trainyard world, VO or automatic mission chain; a successful campaign greeter-to-door sequence is not established. See [CHECKPOINT](CHECKPOINT.md) for the detailed measurements.

## Greeter route in the real session

The greeter route runs inside the ordinary trainyard session, after the arrival director. It is selected by the explicit session profile `MeasuredTrainyardGreeterRouteV1` and is absent from APK 0.11.

The original greeter BRAI fixes where type19 sits. Node 3 (OnUsed, key `train_stop`) has one output, to node 29 (type19 ScriptedVOStreamIn, `mDefName=scripted_voice_kc1_trn`). Node 29 has one output, to node 35 (type59 AnimOverrideMoveStart, `srp_tyd_nml_train_door_open`). That edge carries node 29's own condition (ScriptedVOStreamedIn), so every path from the relay to the actor and the door passes through node 29. Startup node 2 also has a port-32 output to a separate type19 node 21 for the same bank; it is not on this route and is not executed. In the original Tick, node 35 only stores the animation as the AI's move-start animation. Its probe outputs go to 6 (Say), 33 and 72 (UseTarget), and its unconditioned normal output enters node 10 GoToAndFaceTarget ([TRAINYARD-CHAIN](TRAINYARD-CHAIN.md), static).

The production owner `WolfSourceGreeterRoute` (`src/idtech4a_gameplay/NativeSourceGreeterRoute.*`) does the following:

1. The map runtime builds it. Before wagon OnSpawn it spawns the inactive greeter at its authored origin and the door bound to the real wagon at its authored pose. Data preparation checks that the relay, greeter and door names resolve uniquely, that the chain is exactly 3 → 29 → 35 with target `train_door_1`, and that node 35's outputs are exactly the probes 6/33/72 and the normal output to node 10.
2. Its own Think takes the director's relay request once. It checks the generation, event, relay source, exact greeter target, the relay's own key, the original activator (the wagon) and the captured spawn generations of the greeter and door. Raw `train_Stop`, the forwarded `train_stop` and the OnUsed key are kept separately.
3. It delivers node 3 and opens a node 29 request through `WolfScriptedVoiceReadinessRequest` (`NativeSourceScriptedVoiceReadiness.h`). With the opt-in `WOLF2009_SOURCE_AUDIO_BANK` this is a thin adapter over the whole-bank provider `WolfScriptedVOReadiness`. Without it the request stays an explicit `unsupported`. The route asks the provider for Ready through `TrainDoorSignalRuntime::ConsumeVoiceReadiness`; it never takes a caller boolean.
4. A consumed Ready enters node 35: the route records the stored move-start animation, refuses the three probe outputs and holds node 10 with an explicit refusal. Node 10 needs native AI locomotion on AAS and its move start. The door's `triggerStartMoveEnt` is inferred to depend on that move start. Neither exists natively, so the route does not play the actor clip and does not activate the door. The actor owner's own PlayOnce is not shown to be the original start contract. The request stays retained while the owner lives; loss or owner end releases it without moving the graph back.

Results, Android 15 x86_64 emulator / SwiftShader, original trainyard, QA builds of commit `dd7fc53` (Route and Stale without the audio-bank opt-in, the other three with `-WithAudioBankChecks`):

| Run | Case | Result |
|---|---|---|
| `728dfb4fd1a8` | Route, no provider, + offscreen | 3871 checks / 940 frames; node 29 stays `unsupported`; wagon and door GPU A/B passed |
| `7484f2c4374f` | Stale greeter before arrival | 1635 checks / 800 frames |
| `b6dbf50e7f26` | Ready, then Cancel, + offscreen | 3752 checks / 861 frames; door GPU A/B passed |
| `85349fc393ed` | Ready, then bank Lost | 3752 checks / 862 frames |
| `f8ce627e1cc4` | Ready, then stale greeter | 3732 checks / 862 frames |

Common to all cases: the director arrived at game time 12800 ms. In that same Think tick and generation the route took the relay once, delivered OnUsed and opened the node 29 request. The admission check refused the replayed event and variants with the incoming key, a wrong generation, a missing target, another relay, event 0 and no activator. The door rode with the wagon, with displacement identical to the wagon's (+316.0 Y from the QA baseline). Every run ended with ordinary teardown: zero live routes, actors, directors and readiness requests. The three bank-backed runs also reported `live_banks=0`.

- **Route (no provider).** Node 29 was re-evaluated once per Think for 120 frames as `unsupported`, without advancing. Cancel released the request once and stopped Think.
- **Ready.** The request was Pending at 12800 ms. At 12816 ms the provider's own `Update` reported Ready for bank generation 1 with 19 members, and the route consumed it once. It entered node 35 (stored `srp_tyd_nml_train_door_open`), refused 3 probes and held node 10. For 60 more frames the request stayed Ready, retained and not consumed again; the actor stayed held and the door idle. Cancel then released it once.
- **Lost.** After Ready, the ordinary native cache level-load purge (core QA seam) made the next Think observe `lost` ("audio bank native cache sample was purged or changed"). The request was released once and the route stayed at node 10.
- **ReadyStale.** After Ready, the greeter was replaced under its name. The provider reported `cancelled` (owner ended), the request was released and the route failed without rebinding the replacement.
- **Stale.** A same-name greeter replacement before arrival led to one relay take and a refused OnUsed, with no readiness request. After the door was deleted, the refused route stayed inert.
- **Door GPU.** After cancellation, the first-person camera check hid and restored the bound door at the same simulation time. The door drew 1 surface with 504 indexes, changed 7449 pixels and restored exactly the same RGB values (the wagon A/B in the same frame changed 172,107 pixels). This is the closed idle pose only, with no texture-parity claim.

**The chain now stops at node 10 (GoToAndFaceTarget, type 243).** Not established in this session: node 35 probe effects (Say `lipsync_trn_0505_ka1`, HUD compass, mission give); node 10 locomotion and its move start; the door activation that depends on it; the door's endpoint and end-idle (native door playback has only the separate `empty` component run `eb5902bfd353`); speech or playback; idAI Use/Activate; and the doorway blocker that movetrain node 8 removes. Selectors are in [tests/source-greeter-route](../tests/source-greeter-route/README.md).

## What must happen next

To get past node 10 the route needs, in order: the shared probe-output pass, so that node 35 probes fire as leaves; a native AI locomotion/AAS handler for GoToAndFaceTarget, whose move start plays the stored move-start animation; and a traced consumer of `triggerStartMoveEnt` that activates the bound door from that real move start. Source keys, generations and once-only events must survive that chain.
Combat, AI/navigation, Veil, objectives, original save/reload behavior and campaign transitions remain unfinished. The ordered acceptance gates are in [NEXT-STEPS](NEXT-STEPS.md). No complete mission or campaign is claimed playable.

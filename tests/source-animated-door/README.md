# Native source animated door

This bounded game-private owner targets the measured trainyard ENTS 2193
`train_door_1 / env_train_door`, not ENTS 2128's rigid mover or the older preview
door. It reuses the real source actor capability, model, source V2 animation
sets, `SourceControlledEntity`, `idAnimator`, and native entity Bind. It does
not contain original assets or proprietary implementation code.

`Wolf2009_PrepareSourceAnimatedDoor` takes the selected compiled declarations,
exact typed ENTS resource/index, explicit source model/rig/clip selections,
level generation and decoded `TrainDoorSignalPlan`. It requires the compiled
`env_train_door → func_animate` class, `idAnimated`, `noclipmodel=1`, and the
qualified two-stage automatic sequence. Unknown effective fields refuse.
Authored facts remain immutable, including editor frame 154 and `loop_last_anim=1`.
The native policy holds source frame 0 of the original two-frame idle/end clips
only after proving each clip's two complete source poses are exactly equal.
Nonconstant last clips require native cycle support and refuse this profile.

All selected geometry/rig/animation resources must use the ENTS map mount. The
existing actor capability pins that mount; ENTS is reread and byte-compared
after that lease is acquired. The owner retains the exact ENTS handle, full
source identity/record, compiled inheritance/model provenance, actor capability,
native model and animation owners. The caller keeps the map material/declaration
owner alive. A mandatory `actorStart` binding retains the scripted actor
owner's private implementation. Obtain its borrowed context from that actual
owner and keep the owner live until door preparation acquires its lease.
Native child deletion precedes release of actor/source/model owners.

`Spawn` must run during level assembly, before the wagon's OnSpawn movement. It
requires the real native source mover with the exact ENTS name/index and full
ENTS byte identity. The actual parent's initial render pose must match its raw
source rigid world pose. Native `Bind(parent,true)` must preserve the child's
authored world pose. No collision model is fabricated for this source class.
The scripted actor must already be spawned inactive, with no accepted Start.
Door Spawn captures its original `idEntityPtr` and native SpawnId before any
external actor-start delivery. An expired identity cannot be rebound by name.

`Activate(command,reason)` queries that retained owner as its sole authority.
There is no caller-supplied actor pointer, serial or freely assembled ACK.
The private actual Start record must carry the exact `StartActorAnimation`
command: generation, nonzero event, source node, owner and requested clip must
correlate with the director's `ActivateTarget`. It also requires first-pose
evaluation, the original captured native SpawnId, actual controlled serial,
and native channel start/end at the same gameLocal.time as the owner commit.
Preparation, held poses, timers, a matching clip from another event, stale
acknowledgements and same-name native replacements cannot open the door.
Repeated accepted event deliveries never replay it. Authority preflight/query
are effect-free; they cannot issue or acknowledge a new start.

The level owner calls `Tick` from ordinary map Think. SourceControlledEntity's
own native Think plays and holds the exact final source frame. Tick observes
that real hold, verifies the native palette against direct source-frame/FK,
then installs the original constant end clip in the actual animator. A single
completion carries generation, event, source node and original entity/clip
identities. End-pose transition error is reported independently; this does not
claim that separately compressed source clips have identical endpoints.

The opt-in native QA entry point takes the real source wagon, a request with
the original scripted greeter's retained authority, and a QA-only adapter that
calls that actual owner's Start API. The adapter's success alone proves nothing:
the test independently queries the private actual commit. It tests preparation
refusals and immutable source metadata, initial waiting, actual native Bind,
wrong nonzero event and other mismatched activation refusals without door/actor
channel mutation, true owner Start, duplicate idempotence, game-frame playback,
direct final source-frame palette, original end pose, one-shot completion and
owned teardown. It does not warp the player or create a detached renderWorld.
It deliberately starts the supplied greeter's actual clip; the caller must not
pass an unrelated live campaign actor.

Two additional QA entry points require separate fresh isolated fixtures.
`Wolf2009_CheckNativeSourceAnimatedDoorStaleStartQA` advances a normal game frame
after a real owner Start and verifies refusal before the first door acceptance.
`Wolf2009_CheckNativeSourceAnimatedDoorReplacementQA` deletes that original
native actor after its real owner commit, then spawns an actual source-controlled
replacement under the same name/model. Native PlayOnce commands reproduce the
same clip/time/serial, without any owner-issued proof for the replacement. The
door must refuse before its first acceptance and leave both channels unchanged.
This replacement case consumes the original native actor; discard that actor
owner with the fresh fixture. It does not forge an ACK or rebind the authority.

The implementation is integrated into the existing game-private C++11 target.
The fixture and QA code are opt-in under `WOLF2009_ANIMATION_V2_QA`.
`Test-EngineRuntime.ps1 -WithTrainyardDoorChecks` runs three fresh source-wagon,
actor and door fixtures on original `empty`, independently of trainyard world
execution. The fixture resolves the door's inherited compiled `model` default
and explicitly loads the material catalog before source model import.

Actual Android 15 x86_64 run `eb5902bfd353` passed with offscreen regression:
9141 checks / 484 frames for normal playback, 19 checks / 1 frame for stale
start, and 26 replacement checks. The real wagon and bound door both moved
193.600006 units in Y. Final source-frame palette error was zero; transition
to the separately compressed end clip had component difference 0.0567755699.
That difference is reported rather than hidden by a substituted pose.

Native validation also caught two incorrect API assumptions. An MD5 template
has no static base surfaces; preparation now validates a real posed dynamic
snapshot and frees it through the native model manager. An `idPhysics_Static`
without an owned clip returns the default sentinel; the no-collision guard
checks zero owned models, zero contents and that exact sentinel.

All three fixtures released native names and owners; normal game/filesystem/
renderer shutdown left zero source descriptors. The same run passed existing
world and MP40 GPU checks. It did not separately test door GPU appearance,
collision passage, save/restore, later BRAI outputs, AI, VO readiness or the
automatic trainyard chain. See the [runtime commands](../engine-runtime/README.md)
and [checkpoint](../../docs/CHECKPOINT.md).

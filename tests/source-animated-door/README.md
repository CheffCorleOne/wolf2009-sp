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
owner alive. Native child deletion precedes release of source/model owners.

`Spawn` must run during level assembly, before the wagon's OnSpawn movement. It
requires the real native source mover with the exact ENTS name/index and full
ENTS byte identity. The actual parent's initial render pose must match its raw
source rigid world pose. Native `Bind(parent,true)` must preserve the child's
authored world pose. No collision model is fabricated for this source class.

`Activate` consumes the director's exact `ActivateTarget` command, actual
source-controlled greeter pointer and accepted actor commandSerial. Generation,
event, source node, exact owner/target, current source clip and actual native
actor channel start/end must agree at the same gameLocal.time as PlayOnce.
Preparation, held poses, timers, stale acknowledgements and mismatched commands
cannot open the door. Repeated accepted event deliveries never replay it.

The level owner calls `Tick` from ordinary map Think. SourceControlledEntity's
own native Think plays and holds the exact final source frame. Tick observes
that real hold, verifies the native palette against direct source-frame/FK,
then installs the original constant end clip in the actual animator. A single
completion carries generation, event, source node and original entity/clip
identities. End-pose transition error is reported independently; this does not
claim that separately compressed source clips have identical endpoints.

The opt-in native QA entry point takes the real source wagon and a newly owned,
source-controlled original greeter in an isolated QA level. It tests preparation
refusals and immutable source metadata, initial waiting, actual native Bind,
stale/mismatched activation refusals without channel mutation, true actor
PlayOnce acknowledgement, duplicate idempotence, ordinary game-frame playback,
direct final source-frame palette, original end pose, one-shot completion and
owned teardown. It does not warp the player or create a detached renderWorld.
It deliberately starts the supplied greeter's actual clip; the caller must not
pass an unrelated live campaign actor.

This worktree does not change shared CMake, native hooks or SourceMap/director.
The root integrator must add the implementation and optional QA translation
unit to its existing game-private C++11 target and connect lifecycle/Think to
the actual map owner. Native execution, GPU appearance, collision passage,
save/restore, later BRAI outputs, ordinary actor AI and full campaign behavior
are not established by static inspection or a syntax-only compile.

Validation status: both implementation and native QA translation units pass
NDK x86_64 Android26 syntax with the prepared engine's actual game flags and
native headers, using this isolated worktree's private project includes.
Read-only checks against retained original evidence passed 31 assertions for
compiled payload SHA256 identities/typed class defaults, exact door/parent
ENTS facts (including negative-zero axis words), and source clip/rig counts.
Linking and runtime execution have not been run in this isolated worktree.

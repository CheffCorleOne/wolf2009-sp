# Native scripted source actor

`NativeSourceScriptedActor` is a game-private owning C++11 factory. It binds one
exact ENTS actor, selected compiled entity/model closure, original weighted
MODL, named SKEL, one full-pose HKXA clip and authored source skin to the actual
native `idWolfSourceControlled` / `idAnimator`. It changes no API8 layout and
contains no original game payload.

The caller must explicitly choose
`NativeFullPoseNoExtractionNoFrameCommandsV1`, resource mounts, authored LOD,
geometry/rig/tree policies, generation and decoded `TrainDoorSignalPlan`.
The first intended source actor is ENTS2034 `train_greeter`, 52 joints and the
182-frame `srp_tyd_nml_train_door_open` clip. These names/counts are supplied by
the caller; the factory does not select them from a scene-name table.

The exact selected model's declared skeleton is mandatory. For this actor it
is `models/characters/_humanoid_male/empty_male_skel.hkx`, present in the named
global SKEL. The same explicitly named rig can be selected for this full-pose
animation. HumanoidMale has the same52 names/parents but different reference
transforms; neither a reference-pose alias nor a declaration override is used.
The MODL supplies its own bind transforms. All clip tracks supply full local
poses; default/reference fill, masked/additive blending and retargeting remain
unsupported. `MAPDEF` resolution requires the caller's exact context; the first
actor uses the source worldspawn's `model_trainyard_base`.

The original instance record and resolved definitions are retained unchanged.
Placement preflight admits an explicit rigid matrix without an angle, or the
measured redundant **Integer angle0 plus exact identity matrix**. Actual greeter
ENTS2034 contains both fields. Both remain source facts; no general angle/matrix
precedence is invented. Nonzero/other-storage angles, nonidentity matrices with
an angle and duplicate keys refuse before native/model effects. The owning
state separately reports authored angle presence and this admitted profile.
The selected clip's source block is recognized only for the retained classes
`no_delta_blend_out`, `anim_turn`, `anim_turn_force_positive`,
`ai_pure_motion_extraction` and `frame` actions `sound`, `aiming_disable`,
`aiming_enable`, `ai_set_eyeFocus`. They are counted and preserved, never
installed as native options/frame commands. Unknown/nested commands refuse.
All other clips/channel facts and later BRAI nodes remain owned source facts.
This is an explicit full-pose visual slice, not their gameplay implementation.

`SkinCatalog::Read/Apply` resolves the exact nonempty authored skin from the
explicit material archive. Both source and replacement diffuse profiles must
preserve the model's existing UV/color encoding. Changed target DDS resources
are read through their explicit MODL mount; no other pack is searched. Private
immutable native skin/material aliases refuse foreign declarations. Actual
native snapshots before/after skin application must retain every surface,
deformed XYZ, UV, RGBA and index. The shared model/importer is unchanged.

Call ordering:

1. `Wolf2009_PrepareSourceScriptedActor(set, request, reason)` validates and
   retains source capabilities. It creates no entity and emits no start ACK.
2. Obtain `StartBinding(out, reason)` while the owner lives. This descriptor is
   borrowed; its consumer must call `retain` before releasing the owner and
   eventually `release`. Preparation queries contain source name/generation,
   null native identity and completely default command evidence.
3. In an **active local single-player game**, call `SpawnInactive`. This creates
   the original native actor once and holds the explicit `initialSourceFrame`.
   That frame is a caller selection, never an inferred interpretation of the
   preserved ENTS `frame`. The held actor still has no start ACK. The animated
   door can now capture its identity before either animation starts.
4. Pass the director's exact `StartActorAnimation` command to `Start`. Native
   `PlayOnce`, current channel/serial/start time and an actual evaluated animator
   palette compared with source frame0 must all succeed. Only then is the
   immutable owner-issued full-command ACK available. Activate the door
   synchronously on this accepted boundary; no timer acknowledges a start.
5. Poll `Query` from the ordinary level Think. Completion requires the real
   native controller to hold the exact source final frame, with preserved
   command serial/time and an evaluated endpoint palette. A clock threshold
   alone never completes this owner. Duplicate starts return the original ACK
   without replay; different generation/event/node/owner/clip requests refuse.

The captured `idEntityPtr`/SpawnId is never looked up again by name. External
deletion invalidates the authority even if a matching replacement exists. The
last owner/authority reference removes only its original live native actor,
then releases animation/source handles. Release these before source mounts,
native declaration registries or game shutdown.

This actor has no collision body, AI, head/accessory attachment, source root
extraction, frame-command/audio dispatch, save/restore or later BRAI behavior.
Native millisecond one-shot playback and the chosen first visual hold are
explicit compatibility policies; full retail animation-action parity is not
claimed.

`NativeSourceScriptedActorChecks.cpp` is an opt-in **actual native** QA driver.
It needs the selected namespace and the same owner-supplied request in a
dedicated active local-SP game with the source actor's name unoccupied. It
tests inactive construction, invalid-command nonmutation, genuine animator
ACK/pose, duplicate/conflicting commands, ordinary `RunFrame` completion,
retained authority lifetime, and replacement with the same native
name/model/clip/serial/time but a different SpawnId. It fabricates no ACK and
embeds no original asset. It must not run inside an ordinary game-frame callback
that would recursively enter `RunFrame`.

Link the factory with the existing animation/game adapters and portable
`SkinnedModelData.cpp`, `GreeterModel.cpp`, `SkeletonData.cpp`, source readers
and material/skin/DDS code. Its authority adapter uses the existing frozen
`NativeSourceAnimatedDoor.h`; it does not depend on that factory's `.cpp`.
The QA driver additionally uses normal native game-frame functions.

Validation for this isolated change: actual Android NDK x86_64 game-header and
production-flag syntax checks passed for factory and QA driver. The existing
original-data rig executable was rerun read-only: 343218 checks, 364 greeter
samples, both explicit reference-different rigs and partial-track refusal
passed (maximum greeter component error5.684e-14). A retained-artifact comparison
also confirmed all52 MODL names/parents against the declared rig. No native
link, actor lifecycle runtime, GPU run or campaign test has yet been run for
this new factory; those require integration by the level owner.

The small standalone placement target runs this same production data preflight
before any heavy factory/model initialization. Synthetic cases cover rigid
matrix placement, redundant source fields, raw-record preservation and
ambiguous/malformed refusals. An optional caller-supplied decoded original ENTS
also checks the real greeter's raw Integer/type4 angle0 and identity matrix,
then applies the same negative mutations. It embeds no source payload:

```sh
cmake -S tests/source-scripted-actor -B build/scripted-actor-placement \
  -DWOLF2009_SOURCE_ENTS=/owner/path/to/decoded-trainyard-ENTS
cmake --build build/scripted-actor-placement --parallel 1
ctest --test-dir build/scripted-actor-placement --output-on-failure
```

Placement synthetic/original CTest2/2 and102 checks passed against the retained
384353-byte original ENTS. Factory and native QA syntax were rechecked after
the placement change. Error propagation passes the live `idStr` object across
mutating API calls, avoiding a stale `c_str()` under C++11 argument evaluation.
These checks qualify source placement; they do not establish native actor or
campaign runtime success.

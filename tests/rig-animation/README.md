# Explicit full-pose source rig

`SourceRigAnimation` combines the checked named HKXA reader, shared spline
codec and explicitly selected `SkeletonData`. It owns curves and rig data and
returns local and model-space rigid poses through the existing neutral pose
value types. No OS, native game module or platform graphics code is needed.

Callers must select `CallerIdentityFullLocalPoseV1`, exact clip and exact
skeleton names. The admitted packet has no binding table and transform tracks
must equal the number of ordered bones, at most 256. Track i supplies bone i.
This is a caller-selected ordinary full-local-pose policy: no rig is chosen
from a prefix, matching count or matching bone names. Nonidentity curve scale
is refused by the shared codec. Reference/default skeleton poses and MODL
bind transforms retain separate roles; no reference data substitutes for a
missing track. Model-space poses are composed from the selected parent tree.

A bounded static audit of the retail ordinary keyframe path established
ordinal copying when the remap is NULL, and the matching track/bone-count
guard. The node's independent `BlendType` controls replacement versus layering;
the animation wrapper's constructor does not read `binding.blendHint` in that
path. These facts support this explicit subset. They do not establish automatic
SKEL association, partial-track initialization or additive transform math.
Those profiles remain unavailable here. Nonempty binding packets are outside
the first adapter profile, even if a particular binding could be identity.

Source seconds and multi-block curves are sampled directly. Finite times clamp
to endpoints. The adapter does not extract movement, apply entity/world
placement, evaluate floats or dispatch annotations. Float/annotation counts and
an extracted-motion presence fact are retained as opaque metadata. They do not
constitute support for those systems. Source identities retain the complete
explicit request and both original byte counts/FNV fingerprints; these are local
identities, not authentication. Only compressed curves are retained, without
keeping a second complete copy of the named reader's input document.

`SamplePoseFrame(globalFrame)` uses the codec's direct block/local-frame API
and the same rigid FK as source-time sampling. It does not turn a global frame
into seconds before selecting curves. Shared frame255 selects the next source
block and the final global frame selects the exact last local endpoint.
`FrameCoordinateAtTime` forwards the source-time block coordinate. The frame
pose's `seconds` field is only the codec's clamped `SourceTimeAtFrame` metadata;
float32 block-clock drift makes these coordinate helpers nonbijective. It must
not be used to resample the frame pose through the time API. Existing direct
source-time playback remains unchanged. Rig, reference-pose, scale and full
coverage admission are identical for both entry points.

Profile/FK failures use `SourceRigError`; delegated reader and codec failures
retain their typed exceptions, while skeleton failures use `runtime_error`.
All have the common `std::runtime_error` base. Loads never return an identity
fallback after an error.

Standalone CMake registers two tests; optional `WOLF2009_SP_BASE` selects the
owner's read-only resources and `WOLF2009_MINIZ_SOURCE_DIR` selects the pinned
miniz. Main core CMake also exposes these targets. Verification passed:
465 authored checks with independent analytic three-bone star/seven-bone chain
FK, ownership, distinct rigs, unknown reference poses and refusal cases;
351821 original-data checks, including 364 greeter times against the previous
narrow decoder, two explicitly selected reference-different rigs and actual
335-frame/two-block and 41-frame clips. Maximum greeter component difference
was `5.685e-14`. A two-track prop clip presented to a 52-bone rig was refused.
Frame checks add an analytical three-bone FK with a deliberately discontinuous
two-block source and float32 clock drift, plus eleven original-data frame FK
samples (seven around the real pickup's shared/final frames). The original
frame FK oracle uses independent rotation matrices and matrix multiplication.

These are portable component checks. Native wire/provider v1 retain their
qualified single-clip profile. The separate [native V2 integration](../../src/idtech4a_anim/V2.md)
now uses this sampler with explicit model/rig bindings and has its own actual
game/runtime proof. GPU, retail actor behavior and campaign completion are not
established by these component checks. Original bytes, private binary-analysis
excerpts and run outputs are excluded from publication.

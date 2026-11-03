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

Profile/FK failures use `SourceRigError`; delegated reader and codec failures
retain their typed exceptions, while skeleton failures use `runtime_error`.
All have the common `std::runtime_error` base. Loads never return an identity
fallback after an error.

Standalone CMake registers two tests; optional `WOLF2009_SP_BASE` selects the
owner's read-only resources and `WOLF2009_MINIZ_SOURCE_DIR` selects the pinned
miniz. Main core CMake also exposes these targets. Verification passed:
283 authored checks with independent analytic three-bone star/seven-bone chain
FK, ownership, distinct rigs, unknown reference poses and refusal cases;
343218 original-data checks, including 364 greeter times against the previous
narrow decoder, two explicitly selected reference-different rigs and actual
335-frame/two-block and 41-frame clips. Maximum greeter component difference
was `5.685e-14`. A two-track prop clip presented to a 52-bone rig was refused.

These are portable component checks. The existing native animation provider
and wire v1 still use their qualified single-clip profile. No generic native
actor playback, GPU, retail actor behavior or complete campaign is claimed by
this adapter. Original bytes, private binary-analysis excerpts and run outputs
are excluded from publication.

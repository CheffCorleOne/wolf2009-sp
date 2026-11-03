# Checked transform spline codec

Portable C++11 local TRS curves, adapted under GPL-3.0-or-later from the pinned
HavokLib codec. See [provenance](PROVENANCE.md). The codec has no OS, SDK,
Python, SSE, C++20, skeleton or native-engine dependency.

`SplineTransformInput` contains compressed data, source timing and explicit
absolute transform-end indices for every block. The caller supplies checked
block-relative source boundaries converted to absolute indices. The codec
does not guess the semantics of optional raw source metadata arrays. It owns
the compressed bytes and curves; float masks and suffixes remain opaque ranges
in `Source().data`. Unknown formats are typed `Unsupported`, never identity
fallbacks. Nonidentity scale is rejected by default; explicit
`RetainFiniteScale` preserves it and marks the result unqualified for the
native quaternion/translation-only palette.

The admitted frame domain has shared block endpoints, `maxFramesPerBlock`
in 2..256, degree 1..3 with clamped byte knots, 8/16-bit vector components,
Polar32/ThreeComp40/ThreeComp48/uncompressed rotations. Conflicting component
masks, discontinuous knot multiplicity, unsupported quaternion formats and
nonzero alignment padding are refused. Sampling uses source `frameDuration`
and `blockDuration`, clamps finite seconds and evaluates the actual curves.
It does not bake frames or use SLERP.

`SampleFrame`/`SampleTrackFrame` select a block and local frame directly from
the global frame coordinate. Shared frame 255 (for a 256-frame block) belongs
to the next block; the final global frame is the last block's exact endpoint.
Finite frame coordinates clamp to the declared range. No frame sampling path
uses `globalFrame * frameDuration` or converts through seconds. The existing
source-seconds sampler remains unchanged.

`FrameCoordinateAtTime` reports the ordinal coordinate represented by that
time sampler's source block and clamped local frame. `SourceTimeAtFrame` is
metadata only: next-block start at shared endpoints, exact source duration at
the final frame, and finite output clamped to the source duration. Source
float32 `blockDuration` can differ from `(maxFramesPerBlock-1)*frameDuration`.
Consequently the helpers are not bijections or pose-preserving inverses: the
time sampler can still be in the previous block at a shared endpoint, while
direct frame sampling must choose the next. Timed playback must keep using
the direct source-time API; timestamps are not frame-sampling intermediates.

Standalone configure `tests/spline-codec`, then build and run CTest. Optional
`WOLF2009_SPLINE_SOURCE_BASE` selects the owner's read-only `SP/base` and
`WOLF2009_MINIZ_SOURCE_DIR` selects the existing pinned miniz source instead
of a system zlib. Only these standalone targets/tests are registered.

The optional source check selects three clips from trainyard HKXA: the
previously verified greeter, a 335-frame two-block clip and a 41-frame idle.
Greeter integer/fractional/endpoint poses are compared with the existing
decoder. The multiblock check requires exact TRS consumption, validates source
block selection against isolated blocks and reports the boundary jump without
claiming an independent retail pose oracle. Original bytes stay local.
Transform track order is not a skeleton order or gameplay binding. Different
rigs, additive policy, float/face tracks, retail actor clocks and native
providers remain outside this codec checkpoint.

Verified standalone checkpoint: 262 synthetic checks in nine groups and three
selected original clips passed (2/2 CTest). On 373 greeter times the maximum
translation error against the existing decoder was `7.11e-15`, quaternion
component error `4.45e-16`. The two-block clip consumed exact supplied TRS
ranges `[216,16256)` and `[20008,25496)`. Its block boundary jump is reported,
not hidden or interpreted as a retail correctness certificate. Polar32,
ThreeComp48, uncompressed quaternion, quadratic and nonidentity-scale profiles
have authored synthetic coverage; the selected source gate does not claim
original-data coverage for every supported quantization or rig.

Frame verification also passed 364 greeter coordinates (maximum component
error `1.96e-14`) and nine pickup coordinates against separately selected
source-block curves, including adjacent representable frames around frame255.
The pickup frame comparison had zero component error. Its deliberately tested
naive global-frame-to-seconds path differed by `0.01451` in a transform/quaternion
component before the boundary. An authored analytical two-block jump separately
tests both signs of float32 block-clock drift, metadata end clamping, nonfinite
coordinates, scale qualification and single-frame clips.

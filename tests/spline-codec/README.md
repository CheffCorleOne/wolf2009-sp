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

Verified standalone checkpoint: 185 synthetic checks in eight groups and three
selected original clips passed (2/2 CTest). On 373 greeter times the maximum
translation error against the existing decoder was `7.11e-15`, quaternion
component error `4.45e-16`. The two-block clip consumed exact supplied TRS
ranges `[216,16256)` and `[20008,25496)`. Its block boundary jump is reported,
not hidden or interpreted as a retail correctness certificate. Polar32,
ThreeComp48, uncompressed quaternion, quadratic and nonidentity-scale profiles
have authored synthetic coverage; the selected source gate does not claim
original-data coverage for every supported quantization or rig.

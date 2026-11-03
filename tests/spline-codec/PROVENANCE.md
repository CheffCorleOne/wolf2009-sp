# Spline transform codec provenance

`src/wolf2009/SplineTransformCodec.{h,cpp}` is a checked scalar adaptation of
the independent Havok Format Library transform decompressor by Lukas Cone.
It is distributed under **GPL-3.0-or-later**, retaining that author's notice.
The full GPL version 3 license is the repository's [`LICENSE`](../../LICENSE).

Pinned upstream: [PredatorCZ/HavokLib
ef5d5c6f5ab8d7f32cb4e8983300b8db61e4dc6b](https://github.com/PredatorCZ/HavokLib/tree/ef5d5c6f5ab8d7f32cb4e8983300b8db61e4dc6b).

| Source reference | Git blob identity | License |
| --- | --- | --- |
| [`source/hka_spline_decompressor.cpp`](https://github.com/PredatorCZ/HavokLib/blob/ef5d5c6f5ab8d7f32cb4e8983300b8db61e4dc6b/source/hka_spline_decompressor.cpp) | `d330b3d1869aeea487e0271add90fa0e47f5fcde` | GPL-3.0-or-later |
| [`source/hka_spline_decompressor.hpp`](https://github.com/PredatorCZ/HavokLib/blob/ef5d5c6f5ab8d7f32cb4e8983300b8db61e4dc6b/source/hka_spline_decompressor.hpp) | `21ecb2e4eba25629516d39ac149a0b287222d705` | GPL-3.0-or-later |

Modified in 2026 for this project: bounded little-endian scalar reads replace
unchecked pointer casts and SSE; immutable owning curves replace borrowed
spans; allocations/work have explicit budgets; unsupported quantizations,
degrees and masks fail explicitly; source frame duration is retained instead
of inferred integer FPS; quaternion curves are normalized after direct
component spline evaluation; scale qualification is explicit. Block TRS ends
are supplied by the checked caller and must be consumed exactly. Float tracks
remain opaque byte ranges. No proprietary SDK source or runtime is required.
Spike, Python, the upstream packfile loader, class generator and toolset are
not vendored or linked. Tests are independently authored except where they
read the owner's original data locally; no original payload is distributed.

The upstream loader does not recognize this game's exact 5.6 version string.
This adaptation does not relabel it as 5.5: the existing checked `HavokPack`
reader supplies the selected resource's explicit counts and ranges.

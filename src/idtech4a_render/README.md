# Native renderer adapter

This bridge uses the pinned idTech4A++ model manager, render worlds, materials,
images and GL renderer. The standalone target in this directory only audits and
tests the portable adapter; it does not render a frame. The baseline for the
identity-white extension is source checkpoint `88ccdf41` and its nine-patch
engine chain. The tenth patch, `idtech4a-wolf2009-materials-command.patch`, adds
normal startup/shutdown registration for `wolf2009Materials <relative archive>`.
The preparation tests cover upgrading the unchanged nine-patch checkout to the
ten-patch result without rewriting the engine CMake file.

The identity-white profile has a deliberately narrow contract. For a supported
simple diffuse material that requests `VertexColor`, every vertex referenced by
the draw's indices must have raw `diffuseColor == 0xffffffff`. Unused vertices
do not contribute. A coloured referenced vertex keeps the explicit checker
fallback. A qualified draw uses a separate generated declaration namespace and
cache key, so another draw with the same authored material cannot inherit its
qualification. The native vertices retain constant white colour; no general
source colour decoding or channel swizzle is enabled.

The owner-supplied material guides request vertex colour, and a focused static
inspection of the corresponding retail standard/detail GBuffer vertex shaders
found colour-input modulation after an RGB permutation. Their pixel shader
multiplies sampled diffuse RGB by the interpolated colour. White is invariant
under that permutation and is the multiplicative identity. This supports the
profile without establishing the game's D3D vertex declaration type or the
channel order of nonwhite words. Both normalized packed byte types under
consideration map `0xffffffff` to white; Direct3D shader model 2 colour inputs
are also saturated. See Microsoft's [vertex declaration types](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3ddecltype)
and [Direct3D 9 shader inputs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-writing-shaders-9).
Retail bytecode, disassembly, material sources and extracted assets remain
private and are not included here.

The portable tests exercise unused coloured vertices, each nonwhite raw byte
position, referenced coloured vertices, empty/invalid draws, both material cache
orders, preserved alpha test and rejected unsupported composition. Read-only
world-corpus validation produced the following diffuse counts; these are narrow
unlit adapter coverage, not original material/lighting fidelity.

| World | Before: surfaces / DDS | Identity-white added | After: surfaces / DDS |
| --- | ---: | ---: | ---: |
| empty | 2 / 1 | 0 | 2 / 1 |
| trainyard | 53 / 8 | 522 | 575 / 88 |
| mtw_ware | 4 / 2 | 79 | 83 / 34 |

The actual native engine reproduced the world counts, kept coloured and
identity-white variants isolated for shared authored material names, and passed
the catalog-command success/failure/recovery check. The GLES3 offscreen test
retains the original opaque/depth/culling pipeline and compares real TXTR stages
with the same stages using `_white`. The empty view changed 254,576 pixels; a
warehouse view selected from an identity-white wood surface changed 224,495.
Normal common/game/filesystem/GL shutdown passed. GPU evidence is limited to
Android x86_64 SwiftShader ES3.0 at 512 by 512; it is not a hardware-device test.

Nonwhite colour remains unsupported. The corpus contains 749 such VertexColor
world surfaces on trainyard and 86 on warehouse, with no additional nonwhite
surface whose four raw bytes are all equal. Unsupported decal/complex shader
composition also remains a checker fallback. A separate source metadata audit
found `TwoSided` on 78 trainyard alpha-test surfaces; the adapter does not yet
carry that flag, so their culling fidelity remains limited.

`wolf2009-render-audit OWNER_SP_BASE` reads the owner's original archives and
prints JSONL counts and bounded raw-word histograms. It never modifies input
files. Counts use only compacted, referenced vertices of world draw surfaces.
The audit is a data inventory; it does not establish the source declaration,
original lighting, full material fidelity, BSP/PVS, or gameplay support.

Build and run the CTest targets using the optional owner-supplied
`WOLF2009_SP_BASE` and `WOLF2009_MINIZ_SOURCE_DIR` CMake settings. Native runtime
and offscreen GPU tests are under `tests/engine-render` and are run through the
normal isolated runtime test scripts. Generated runtime fixtures are tests
only; application input remains the owner's original PK4/SPK/MPK archives.
Pass startup arguments as separate argv entries. In an interactive console,
quote the archive argument, for example
`wolf2009Materials "relative/material-archive.bin"`, so punctuation in the path
stays within one argument.

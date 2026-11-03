# Native renderer adapter

This bridge uses the pinned idTech4A++ model manager, render worlds, materials,
images and GL renderer. The standalone target in this directory audits and
tests the portable adapter; it does not render a frame. Normal engine startup
registers `wolf2009Materials <relative archive>` through the existing
materials-command patch. The colour extension changes portable material
metadata and the adapter; it adds no engine patch.

The adapter supports a narrow unlit diffuse profile for the standard/detail
GBuffer material family, including constant alpha test and UV scale. Unknown
techniques, explicit blending, decals and complex composition keep an explicit
checker fallback. Material classes determine support; authored resource names
do not select exceptions.

Focused static inspection established the source colour contract for the
supported packed 40-byte and unpacked 64-byte declarations. The input was the
owner's PE32 `Wolf2.exe`, SHA-256
`e8604f6450d183254ccf4af7e59bcf7a2aea0332f69739955854d95f3b9140c9`.
REA bound that input's identity; selected original instruction ranges were
inspected with LLVM objdump, without launching the program or running a new
whole-program analysis. The relevant facts are:

- The `DiffuseColor` parser selects component/storage index 7. Vertex-format
  finishing records its offset at descriptor `+0x22` and storage at `+0x87`.
  The declaration builder at `0x10169306` reads those fields, emits colour
  usage 10, and obtains declaration type 4 from table row `0x106126e8`.
  The builder calls `IDirect3DDevice9::CreateVertexDeclaration` at
  `0x10169437`. These layouts place the four source bytes at offsets 28 and
  52 respectively; the common geometry reader continues to preserve a raw word.
- Type 4 is `D3DCOLOR`: its normalized shader input is ARGB converted to RGBA.
  The shipped standard and detail GBuffer vertex programs then permute colour
  `.zyxw`. The composition therefore gives native RGBA bytes
  `{word & 255, (word >> 8) & 255, (word >> 16) & 255, word >> 24}`.
  For example, raw `0x11223344` becomes `{0x44,0x33,0x22,0x11}`.
- The retail `VertexColor` parser branch at `0x10107b09` creates named
  `colorModulate` and `colorAdd` vectors. It fills the former with four copies
  of 1.0 and the latter with four zeros. The technique binds these material
  vectors to the vertex program. The standard/detail pixel programs multiply
  sampled diffuse RGB by interpolated colour RGB.
- The alpha-test variants additionally compare texture alpha multiplied by
  interpolated colour alpha with the authored reference. The native depth
  prepass tests texture alpha alone. Such draws are therefore supported only
  when every referenced vertex has source alpha 255. Other vertex-alpha
  cutouts receive a checker material.

Microsoft documents the [declaration types](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3ddecltype)
and [Direct3D 9 shader inputs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-writing-shaders-9).
The inspected static vertex programs were `GBufferMRT_STD.vso` (SHA-256
`31ac6602bd1cf4b64501564d7844ddeda0d8118cb966394413d492c0fed7caba`)
and `GBufferDNMRT_STD.vso`
(`bf2f533c1c23ae8aa053396df6fddf5bb9ab64f8ea50f42cf13787f0621eb5d3`).
Pixel-program inspection also covered both corresponding alpha-test variants.
Retail bytecode, disassembly, material sources and extracted assets remain
private and are not included here. This is static format/shader evidence,
not an observation of the original game's execution.

The material resolver retains the presence of explicit `Vector ColorModulate`
and `Vector ColorAdd` through regular and inline guide expansion, without
evaluating their expressions. Every simple diffuse profile rejects either
override, including an apparent constant identity and draws whose vertices are
all white. This prevents user constants from silently receiving default math.

Draws with `VertexColor` and only referenced `0xffffffff` words retain the
identity-white profile and constant native white vertices. Nonwhite supported
draws use `NativeDiffuseColor` during native `idDrawVert` construction and a
generated `vertexColor` stage. The profiles have distinct generated namespaces
and declaration/cache keys: `materials-identity-white` and
`materials-vertex-color`. Unused vertices do not affect qualification, and
different nonwhite colour values share the same modulation declaration.
Materials without `VertexColor` ignore the source colour words.

Portable tests cover distinct RGB/A bytes, D3DCOLOR plus shader permutation,
unused and referenced coloured vertices, both cache orders, default versus
explicit colour constants, nested guide metadata, empty/invalid draws and the
vertex-alpha cutout restriction. Read-only corpus validation produced these
counts, including actual DDS decoding:

| World | Total surfaces | Identity-white baseline: surfaces / DDS | With source colour: surfaces / DDS |
| --- | ---: | ---: | ---: |
| empty | 2 | 2 / 1 | 2 / 1 |
| trainyard | 1,789 | 575 / 88 | 1,318 / 119 |
| mtw_ware | 232 | 83 / 34 | 169 / 51 |

The trainyard result includes 522 identity-white and 743 nonwhite modulated
surfaces; warehouse includes 79 and 86 respectively. These are unlit adapter
coverage counts, not full material or lighting fidelity. Source normal maps,
detail normals, specular response and deferred GBuffer outputs are not
implemented by this ambient stage. The previously audited `TwoSided` flag on
78 trainyard alpha-test surfaces is also not yet carried to native materials.

The earlier actual engine/GLES3 proof covered the identity-white baseline,
with real TXTR versus `_white` comparisons: 254,576 changed pixels on empty and
224,495 on a warehouse wood view. Common/game/filesystem/GL shutdown passed.
That proof was Android x86_64 SwiftShader ES3.0 at 512 by 512. The new nonwhite
profile now also passed the actual native GL checks. The updated warehouse
view changed 262,144 pixels when source textures were replaced with white;
disabling only its vertex-colour modulation changed 262,144 pixels across
86 controlled stages. Both comparisons retained the same geometry, camera,
culling and opaque depth pipeline. All five readbacks and normal native
game/filesystem/renderer shutdown passed on SwiftShader ES3.0. This remains
an offscreen test rather than a hardware-device or full-level visual check.

The first coloured-stage run exposed an upstream cleanup bug: `colorModulate`
and `colorAdd` are scalar float uniforms, but stage cleanup sent `Uniform4fv`.
The draw completed and left `GL_INVALID_OPERATION` for framebuffer readback.
The appended `idtech4a-wolf2009-vertex-color-uniform.patch` changes only those
two cleanup calls to `Uniform1fv`, matching their existing setup calls and
the six affected default-program variants in both GLES100 and GLES300.
The otherwise identical native comparison then passed. The first eleven
engine patches remain unchanged. The earlier two startup stencil-mode
`GL_INVALID_ENUM` reports remain separate from the now-clean frame/readbacks.

`wolf2009-render-audit OWNER_SP_BASE` reads the owner's original archives and
prints JSONL counts, material metadata and bounded raw-word histograms. It
never modifies input files. Counts use only compacted, referenced vertices of
world draw surfaces. Static MD5R, a single render area without BSP/PVS, and the
material limitations above remain the bridge's current rendering scope.

Build and run the CTest targets using the optional owner-supplied
`WOLF2009_SP_BASE` and `WOLF2009_MINIZ_SOURCE_DIR` CMake settings. Native runtime
and offscreen GPU tests are under `tests/engine-render`. Generated runtime
fixtures are tests only; application input remains the owner's original
PK4/SPK/MPK archives. Pass startup arguments as separate argv entries. In an
interactive console, quote the archive argument, for example
`wolf2009Materials "relative/material-archive.bin"`.

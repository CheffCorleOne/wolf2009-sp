# Rendering: what trainyard really uses and how to get there

Reviewed 2026-11-11. The owner reports that Android graphics look far worse
than the PC game. This page establishes, from the original data, what the
trainyard materials, textures and lights actually are. It lists what the
current adapter drops, maps each feature to the pinned idTech4A++ renderer and
plans named, staged profiles with acceptance checks. It is research only: no
code, build or emulator run is part of this review.

Labels:

- **Data**: read from the owner's original archives (`assets.pk4`,
  `shaders.pk4`, `maps/game/trainyard/*.mpk|spk`, `streampacks/*.spk`)
  through the repository's package readers, read-only.
- **Static**: read from source text (technique files, engine and adapter
  source) or from the disassembly of a hash-identified retail shader program.
  Not executed.
- **Inferred**: consistent with the above, not closed.
- **Unknown**: no adequate evidence yet.

Counts use the original trainyard. "World surfaces" are the 1,789 compacted
`_area*` draw surfaces counted by `wolf2009-render-audit` (see
[src/idtech4a_render/README.md](../src/idtech4a_render/README.md)); MODL
props and characters are counted as materials only. Raw scans, dumps and the
small read-only Node scripts that produced them stay under the ignored
`evidence/rendering/`. No asset, shader bytecode or material text is
reproduced here.

## 1. Summary

The native renderer today draws an **unlit, single-stage diffuse** image of the
world with the **low-resolution stub textures** of the main map pack. The PC
game draws a **deferred, dynamically lit** scene with full-resolution
diffuse/normal/specular textures from 12 detail packs, more than 600 static
lights, a sun, hemispherical ambient, fog, decals and a colour grade.

Top five visual gaps, by screen impact:

| # | Gap | Size on trainyard | Status |
|---|---|---|---|
| 1 | Texture stubs: the main MPK holds 16–32 px stand-ins; full-resolution copies live in `trainyard_pak01…12.spk`, which Android never mounts | 245 of 369 diffuse maps are ≤ 32 px in the main pack, used by 978 of 1,348 diffuse-bearing world surfaces (73 %). Larger byte-identical copies exist for 240 of them (991 surfaces) | Data |
| 2 | No lighting: every generated material is one ambient `map` stage; normal, specular, lights, sun and ambient are dropped | 351 materials have a normal map, 316 a specular map; PROC carries 632 static lights; the atmosphere has a sun and two hemisphere maps | Data, Static |
| 3 | Checker fallback on unsupported materials | 471 of 1,789 world surfaces (26 %): 386 multiply decals, 23 invisible shadow casters, 62 glass/water/flare/env/parallax | Data, Static |
| 4 | Render-side props are not placed | PROC `RenderSideOnlyEntityDicts` holds 2,025 `func_static` props and 25 merged instances; only measured arrival statics are spawned today | Data, Static |
| 5 | No atmosphere or post-process: fog, sky dome, desaturate/scale/pow grade and bloom | One atmosphere state, one post-process chain | Data |

Recommended first stage: **`SourceDetailTextureV1`** (§8, stage 1), followed
immediately by **`SourceDecalModulateV1`** (stage 2). Both are verifiable with
offscreen GPU checks on SwiftShader, do not depend on lighting, and together
remove the stubs from about 1,000 surfaces and the checker from 409.

## 2. The original renderer

### 2.1 A deferred pipeline (Static)

`assets.pk4` contains 50 technique files (`techniques/*.tech`) and
`shaders.pk4` 1,044 Shader Model 2 programs (663 `.vso`, 381 `.pso`). The
technique names and pass states describe a deferred renderer:

| Phase | Techniques (examples) | What they do |
|---|---|---|
| Geometry buffer | `DSGBufferMRT_STD[_Depth][_AlphaTest][_DetailNormal]`, `DSAddGBuffer…` | Two render targets: diffuse and view normal. Samplers `DiffuseMap`, `SpecularMap` (default `_black`), `NormalMap` (default `_flat`), `DetailNormalMap` |
| Ambient | `DSAmbient_STD_2D_Outdoor`, `…_Indoor`, `…_DefaultIndoor` | Full-screen pass reading `_DiffuseBuffer`, `_NormalDepthBuffer` and a hemisphere map (`_outdoorHemiAmbient` / `_indoorHemiAmbient`), split by stencil reference 0 / 1, scaled by `OutdoorAmbientScale` |
| Sun | `DSDirectionalLightInteraction_STD_2D[_Spec]_Outdoor`, `DSShadowMapOrtho_…` | Full-screen directional light on outdoor pixels, orthographic shadow map |
| Local lights | `DSPointLightInteraction_*` (100 variants), `DSProjectedLightInteraction_*` (50) | Light-volume passes with stencil, additive blend, attenuation texture, optional cube map, specular, rectangular attenuation, luminance scaling, subtractive variants |
| Shadows | `StencilShadowVolume_*`, `ShadowProjVolume_*`, `ShadowBlur*`, `DrawWhiteInLitAreas*` | Stencil shadow volumes plus a screen-space shadow blur ("soft shadows") |
| Fog | `DSFog_STD_2D`, `DSFog_STD_3D_Stencil_*`, `ForwardVistaFog*` | Scene fog, vista fog |
| After lighting | `AmbientDecal*` (13), `DDecalUnlit*` | Decals that blend onto the lit result |
| Forward | `LitGlassFogged*`, `WaterEnvMapReflection*`, `EnvMap*`, `FlareDeformTextureAdd`, particle and sprite techniques | Glass, water, reflections, flares, effects |
| Post | `PP_*`, `DSDesatScalePow`, `PPDesaturateAndOverBloom` | Downsample, separable Gaussian bloom, desaturate/scale/pow grade, depth of field, distortion |

`techniques/lightmap.tech`, `shroud.tech`, `environmenteffects.tech` and
`dsgbufferpowers.tech` are 3-byte empty files. **Trainyard uses no lightmaps**
(Data): no lightmap technique, no lightmap texture role and no lightmap chunk
in any trainyard pack. All lighting is dynamic at run time.

Static inspection of the retail `GBufferMRT_STD.pso` (hash in the renderer
README) gives the geometry-buffer contract:

- target 0 RGB = diffuse texel × interpolated vertex colour;
- target 0 A = specular intensity: Rec. 709 luminance of the specular map,
  `saturate(lum × scale + bias)`, then combined with a power term from the
  material `Specular` vector. The specular map is therefore a **monochrome
  intensity**, not a colour. How the power is packed into the byte is
  **Unknown**;
- target 1 XYZ = normalized tangent-to-view transform of
  `2 × (texel.a, texel.g, texel.b) − 1`. **X comes from alpha, Y from green, Z
  from blue**; red is unused;
- target 1 W is copied from an interpolator (depth, **Inferred**).

The default material vectors in `renderer/global.bind` are
`Specular {16 0 1 0}` (power, intensity bias, intensity scale) and
`DetailNormalControls {4 4 1 0}` (Data).

### 2.2 Atmosphere (Data)

Worldspawn sets `TODSetting0…3` and `radiantAtmosphereSetting` to
`train_station`. `def/atmosphere.def` defines that state:

| Key | Value |
|---|---|
| Sun direction / colour × intensity | (0.368, 0.782, 0.503) / (1, 1, 0.965) × 1.2 |
| Sun specular colour × intensity | (1, 1, 1) × 1 |
| Scene fog range / colour | −100 … 3,500 / (0.373, 0.392, 0.282) |
| Vista fog range / colour | −300 … 4,300 / (0.784, 0.843, 0.957) |
| Outdoor / indoor hemisphere map | `gfx/env/hemiday2` / `gfx/env/hemihospitalindoor` (both DXT1 128×32, 2D) |
| Environment cube | `gfx/env/trainyard_cube_cm` (DXT1 128² cube) |
| Light scale | 1.5 |
| Sky models | `models/skydomes/train_sky.md5r`, `…/train_sky_disc.md5r` |
| Stream pack | `streampacks/train_station.spk`; post-process `PPSettings train_station` |

The named stream pack file does not exist. The hemisphere maps and cube are in
`streampacks/train_stationotherwhere.spk` and `…otherwheretrain.spk`. The
pack-name resolution rule is **Unknown**. The `train_station` post-process
values are not in any text source (`def/postprocess/postprocess.def` only
defines the step chain); they are presumably compiled, **Unknown**.
`renderer/global.vectors` carries the global grade defaults
`DesaturateScalePow {0.4 1.3 1.6}`, `BloomColor 1.5` and
`OutdoorAmbientLightScalePC 1` (Data).

## 3. Trainyard materials (Data)

### 3.1 Where material text comes from

The adapter reads the 80 `.mtr` and 5 `.guide` files of `assets.pk4`. The
trainyard map pack also has a compiled `DECL` chunk: 431 of the 432 materials
trainyard references are present there in expanded form (guides resolved).
Whether the retail renderer uses that copy, and whether it equals the `.mtr`
text, is **Unknown**. Comparing them through the existing compiled-declaration
catalog is a cheap check for stage 3.

### 3.2 Feature counts

432 distinct materials are referenced: 170 by PROC, 299 by MODL (37 by
both). All 432 resolve in the text sources; 274 are inline bodies and 158
use one of 25 guides (`diff_bump_spec` 53, `diff_multiply` 15, `diff_overlay`
14, `mix_diff_bump_spec_nooverlay` 14, …).

| Feature (declaration shape) | Materials | World surfaces |
|---|---:|---:|
| `Image DiffuseMap` | 369 | 1,348 |
| `Image NormalMap` (`_local`) | 351 | 1,244 |
| `Image SpecularMap` (`_s`) | 316 | 1,078 |
| `Vector Specular` (power, bias, scale) | 307 | 1,093 |
| `Image DetailNormalMap` (real map / `_flat`) | 8 / 30 | 24 / 4 |
| `Image ColorMap` (decals, effects) | 49 | 411 |
| `VertexColor` | 205 | 1,661 |
| `AlphaTest` | 61 | 254 |
| `TwoSided` | 21 | 78 |
| `PolygonOffset` | 63 | 587 |
| `NoShadows` | 121 | 708 |
| `NoLighting` | 50 | 434 |
| `ColorModulate` / `ColorAdd` vectors | 38 | 49 |
| Table, time or entity-parm expressions | 29 | 23 |
| Nested stage blocks | 13 | 28 |
| `Deform` | 3 | 8 |
| `AlwaysOutside` / `AlwaysInside` | 13 / 5 | 0 / 0 |
| `ShroudMaterial` (Veil render set) | 4 | 0 |
| Explicit `translucent`/`blend` keywords | 0 | 0 |

Specular power varies per material: 25 distinct `Specular` triples, power 16
on 202 materials (721 world surfaces), 32 on 30, 45 on 13, 64 on 17, 100 on
12, up to 350. Other image semantics are `EnvironmentMap` (15), `MaskMap` (8),
`DepthMap` (1, parallax) and two-layer `DiffuseMap0/1`, `NormalMap0/1`,
`SpecularMap0/1` (16, all MODL, none in the world).

### 3.3 Techniques on world surfaces

| Technique | World surfaces | Today |
|---|---:|---|
| (default deferred G-buffer) opaque / alpha-tested | 1,044 / 250 | Unlit diffuse |
| `DSAddGBuffer…_DetailNormal_Depth` | 24 | Unlit diffuse |
| Default technique with `NoDraw` + `ForceShadows` (`textures/common/shadowcaulk`, `…/shadow`) | 23 | **Checker** |
| `AmbientDecalModulate` (blend `DstColor Zero`) | 216 | **Checker** |
| `AmbientDecal2xModulate` (blend `DstColor SrcColor`) | 170 | **Checker** |
| `AmbientDecal2xModulatePlusEnvMapBumpReflectMaskedAddOpaque` | 8 | **Checker** |
| `WaterEnvMapReflectionBlend` | 16 | **Checker** |
| `LitGlassFogged…` (two techniques) | 16 | **Checker** |
| `StdDrawTextureAddAlphaWrite` (glowing windows) | 9 | **Checker** |
| `FlareDeformTextureAdd` | 8 | **Checker** |
| `DSAddGBuffer…_Parallax_AlphaTest_Depth_Clamp` | 3 | **Checker** |
| `EnvMapBumpReflectMaskedAddOpaque` | 1 | **Checker** |
| Alpha test with non-opaque vertex alpha | 1 | **Checker** |

The two multiply decal techniques are `Sort Depth`, depth-write off, blend
`DstColor Zero` and `DstColor SrcColor`, polygon offset, `Contents Nonsolid`,
`NoLighting`, `NoShadows`, `VertexColor` (Static, from `decal.tech` and the
decal guide). 103,945 of 104,051 vertices on those 386 surfaces are
`0xFFFFFFFF`; the other 106 belong to two materials and all have alpha 255
(Data). How the
`StandardDrawTexture` program combines a non-white vertex colour with the
blend is **Unknown**; the profile below admits only all-white draws.

## 4. Textures (Data)

All trainyard textures are legacy DDS. 1,089 distinct names occur in 13 packs
(2,230 entries): DXT1 602, DXT5 471, A8 11, DXT3 5. By role: `_d` diffuse is
DXT1 272 / DXT5 86 / DXT3 4, `_local` normal maps are DXT5 334, `_s`
specular maps are DXT1 292, five cube maps (DXT1/DXT5) and 2D light
attenuation strips (A8, e.g. 256×4) and projections (DXT1 128²).

**The main pack holds stubs.** Of the 1,087 textures in `trainyard.mpk`, 752
are ≤ 32 px on the long side (579 are 16×16). 662 of those have larger
copies in the detail packs, whose sizes are mostly 256 (486) and 512 (459),
with 25 at 1,024. Worldspawn lists the 12 packs in `detailPackLayers`, and
ENTS has 12 `detailpack_volume` entities, so the retail game streams them by
region (**Inferred**; the swap policy is **Unknown**). All 670 detail-pack
texture names that occur in more than one pack are byte-identical across
packs. This is the evidence that makes a texture-only detail profile
unambiguous. MODL resources with the same name do differ between the main
and detail packs (see [RESEARCH](RESEARCH.md)); their detail-to-detail
identity is **Unknown**.

For trainyard-referenced materials:

| Role | Materials | ≤ 32 px in main pack (world surfaces) | Larger copy in a detail pack (world surfaces) | Absent everywhere |
|---|---:|---:|---:|---:|
| Diffuse | 369 | 245 (978) | 240 (991) | 11 |
| Normal | 351 | 249 (941) | 229 (927) | 12 |
| Specular | 316 | 238 (926) | 203 (837) | 9 |
| ColorMap | 49 | 16 (152) | 14 (131) | 3 |

Unique referenced images at their best resolution need about 297 MB as RGBA8
with mip chains (diffuse 128, normal 93, specular 59, colour 18), but about
53 MB in their block-compressed form. Today's stub diffuse set is about 38 MB
as RGBA8.

**Normal maps are DXT5 with X in alpha.** In 42 sampled `_local` maps from
`trainyard_pak04` and `_pak08`, every colour endpoint has red 0, blue
endpoints are high (21–31 of 31) and green and alpha span the range. Together
with the G-buffer program in §2.1 this is the "RXGB"/DXT5nm layout.

Environment textures: `hemiday2`, `hemihospitalindoor` (128×32 DXT1 2D) and
`trainyard_cube_cm` are in the atmosphere stream packs; attenuation strips are
split between `global.spk` and `trainyard.mpk`; the seven projection maps
used by trainyard lights are in `trainyard.mpk`.

## 5. Lights and render-side entities (Data)

The trainyard PROC ends with `RenderSideOnlyEntityDicts`: 2,682 dictionaries,
2,025 `func_static` (model, origin, 3×3 rotation, `portalAreas`, optional
`skin`/`noshadows`), 25 `func_static_instance_merged` and **632 `light`**.
ENTS adds 7 game-side point lights (each with a `start_off` key) and one
`target_lightswitch`.

| PROC light field | Distribution |
|---|---|
| `light_type` | PointLight 534, ProjectedLight 93, AmbientTransitionLight 1, absent 4 |
| `light_category` | Key 387, Fill 136, Secondary 93 |
| `light_casts_shadows` | 1: 129, 0: 498 |
| `light_specular` | 1: 485, 0: 142 |
| `light_attenuation_method` | Spherical 502, Rectangular 126 |
| `light_attenuation_map` | `squareLight1a` 573, `pow_0pt25` 34, `pow_0pt75` 20, other 1–4 |
| `light_projection_map` | 93 maps: `warm_spot` 41, `spot01` 18, `fall_50pct` 16, `cage_light` 11, … |
| `light_shader` | 30 lights: `fire_city` 23, `subtle_flicker01/02` 5, `null_light_shader` 2 |
| Other | `_color` above 1.0 on 187 lights; `light_back_lighting` > 0 on 191; 170 are children of a light group; `light_soft_shadows 1` on 619 |

PROC also contains precomputed shadow data: the keywords
`ShadowVolumeBuffers` and `SilTraceBuffers` occur 1,027 and 946 times. The light shaders (`fire_city`, …) are not in
the text sources (only `lightshaders/test.lsh` exists); their tables are
**Unknown**.

## 6. What the current adapter drops or approximates (Static)

| Area | Behaviour today | Where |
|---|---|---|
| Material resolution | Collects only `DiffuseMap`, `NormalMap`, `SpecularMap`, `ColorMap` paths, one technique, `AlphaTest`, `UVScale`, `VertexColor`, `PolygonOffset` and `Contents` (for decal classification) and the presence of `ColorModulate`/`ColorAdd`. Ignores `DetailNormalMap`, other images, `Vector Specular`, `TwoSided`, `NoShadows`, `NoLighting`, `NoDraw`, `Deform`; nested blocks and blend keywords make the class Unsupported | `src/wolf2009/Material.cpp` `ResolveBody` |
| Native declaration | Supported classes become `{ noShadows { map <diffuse> [alphaTest] [vertexColor] } }`: one unlit stage. Normal and specular paths are resolved and then discarded. Everything else, including recognised decals, becomes `map _default` (checker) | `src/idtech4a_render/RenderData.cpp` `BuildMaterial` |
| Texture validation | Decodes mip 0 only to qualify the material; missing or corrupt images become the checker | `RenderData.cpp` `QualifyDiffuseMaterial` |
| Image upload | `DecodeDdsMip0` → RGBA8; the source mip chain is discarded, and the image goes through the engine's normal uncompressed path | `src/idtech4a_render/Wolf2009Render.cpp` `Wolf2009_LoadImage`; `src/wolf2009/Dds.cpp` |
| Cube maps | Rejected by the DDS reader | `Dds.cpp` `DecodeDdsMip0` |
| Normals/tangents | `GeometryVertex` keeps no normal/tangent/binormal; surfaces set `generateNormals` and the engine derives them | `RenderData.h`; `Wolf2009Render.cpp` `AddSurface` |
| UV scale | Baked into vertex UVs | `Wolf2009Render.cpp` `AddSurface` |
| World structure | All `_area*` surfaces merged into one `_area0`, `numAreaNodes = 1`, no portals; submodels are registered but placed only by game-side handlers | `Wolf2009Render.cpp` `Wolf2009_LoadRenderWorld` |
| Render-side dicts | Lights and 2,050 props in `RenderSideOnlyEntityDicts` are not read; only measured `func_static`s are spawned (`NativeSourceMap.cpp`) | `src/idtech4a_gameplay/NativeSourceMap.cpp` |
| Packs | The Android host mounts `trainyard.mpk`, `global.spk`, the VO pack and `mp40.spk` (plus actor packs); no `trainyard_pak01…12` and no atmosphere stream pack. Staging copies the same set | `android/engine/native/EngineHost.cpp` (`Mount`); `tests/engine-android/Stage-EngineData.ps1` |
| `TwoSided` | Not carried to native materials (78 world surfaces) | [renderer README](../src/idtech4a_render/README.md) |
| Lights | No light entity is spawned; there are no interactions, so the scene is fullbright | Code search: no light spawn in `src/` |

## 7. Mapping to idTech4A++ (Static)

The pinned engine is a **forward, per-light** Doom 3 renderer with GLSL ES
3.00 programs (`engine/doom3/neo/renderer/glsl/shader300/doom3/`).

| Wolfenstein feature | idTech4A++ support | New work |
|---|---|---|
| Diffuse × vertex colour | `diffusemap` stage with `vertexColor` | None (already used) |
| Tangent-space normal map | `bumpmap` stage; `interaction.frag` reads `.agb` | Engine's `TD_BUMP` load path copies red into alpha (`Image_load.cpp`, "swap the red and alpha for rxgb support"). Wolf maps already carry X in alpha, so the adapter must pre-swizzle A→R for the normal role (or load them precompressed) |
| Specular map + per-material power/bias/scale | `specularmap` stage; Phong, Blinn-Phong or PBR via `harm_r_lightingModel`; exponent is a **global** cvar (`harm_r_specularExponent*`) | Luminance → intensity conversion and a per-material exponent/bias/scale uniform (small engine patch) |
| Detail normal map | None | New program variant or omit with explicit refusal (24 world surfaces) |
| Alpha test, two-sided, polygon offset, no-shadows | `alphaTest`, `twoSided`, `polygonOffset`, `noShadows` | Carry the keywords |
| Multiply decals | `blend filter` / `blend gl_dst_color, gl_src_color`, `sort decal`, `translucent` | Profile only |
| `NoDraw` shadow casters | Doom 3 `nodraw` / shadow-only materials | Profile only |
| Point/projected lights, attenuation and projection textures | `idRenderLight` with falloff and projection images; rectangular attenuation is the native Doom 3 model | **Spherical** attenuation (502 lights), overbright colours, back-lighting, light groups and light shaders need new code or explicit approximation |
| Sun | Parallel lights; cascaded shadow maps for parallel lights (`tr/tr_shadowmapping.cpp`) | Outdoor-only masking |
| Hemispherical ambient indoor/outdoor | None. Closest: optional `harm_r_globalIllumination` pass (`glsl/draw_glsl_globalIllumination.cpp`), `r_forceAmbient` | New ambient pass sampling the hemisphere map with the world normal; indoor/outdoor classification source is **Unknown** |
| Stencil shadow volumes, soft shadows | Stencil shadows (`tr_stencilshadow.cpp`) and shadow mapping with jitter | Choose shadow maps on mobile; PROC precomputed shadow buffers are optional input |
| Fog | Doom 3 fog lights (`fogLight`) | Map scene/vista fog ranges to fog lights or a view fog pass |
| Environment/cube reflection, glass, water | `environment`, `bumpyEnvironment`, Prey `liquid`, heat haze programs | Cube-map DDS reading, per-technique profiles |
| Bloom, desaturate/scale/pow grade, DOF | None; `rb/rb_postprocess.cpp` holds only retro filters; Prey `screenprocess` | New post pass on the existing framebuffer code |
| BC1/BC3 textures | Upload via `GL_EXT_texture_compression_s3tc` when present and `r_useDXT` is set (`RenderSystem_init.cpp`); runtime ETC1/ETC2 encoder with cache (`image/Image_load_compression.cpp`) | Choose a compressed path per device |
| Areas and portals | Native `idRenderWorld` areas/portals and light-area interaction culling | Build areas/portals from PROC; `portalAreas` keys exist on every dict. Portal geometry location is **Unknown** |

The upstream Quake 4/Prey SDK trees in `engine/` are game code; they add no
renderer feature here. Prey's engine-side programs (`liquid`, `screenprocess`)
are GPL engine code and may be reused under the engine's terms.

## 8. Staged plan

Every stage is a named, opt-in profile with explicit refusals and its own
acceptance. A stage proves only its scope. Ranking is by visual impact per
unit of risk.

### Reference captures (prerequisite for all stages)

Capture the original PC game in an isolated copy with its own profile, never
the owner installation. Use fixed, reproducible views: the player's wagon at
`train_stop`, the station hall and the yard with lights and decals. Record
build, resolution, settings and positions; put selected images with
provenance in `docs/images/` as AGENTS.md requires. The two renderers differ,
so pixel equality is not a goal. Gating checks are structural and use our own
A/B toggles; similarity to the reference is reported, not gated.

### Stage 1 — `SourceDetailTextureV1` (recommended first)

Mount `trainyard_pak01…12.spk` read-only under explicit names. For each
texture request, prefer the largest copy of the same name when every
detail-pack copy is byte-identical; refuse on any conflict and keep the main
pack stub with a reason. MODL detail models stay out of scope until their
identity is closed. Upload compressed: BC1/BC3 directly where the device
exposes S3TC; otherwise transcode to ETC2 into a private cache on import or
first load. Keep source mip chains instead of regenerating them. Stage the
packs through the existing importer.

Acceptance:

- portable: exact counts of upgraded/stub/refused textures per role
  (expected about 240 diffuse, 229 normal, 203 specular);
- offscreen GPU (SwiftShader): the wagon and station views differ from the
  stub build in the affected surfaces, and high-frequency energy per surface
  rises; every upgraded image reports its pack and hash;
- memory: resident texture bytes logged against a stated budget;
- physical device (required): load time, resident memory and absence of
  OOM on at least one Adreno and one Mali ARM64 device.

### Stage 2 — `SourceDecalModulateV1` and `SourceNoDrawCasterV1`

Emit `AmbientDecalModulate` as a filter blend and `AmbientDecal2xModulate` as
`gl_dst_color, gl_src_color`, with `polygonOffset`, decal sort and no depth
write, only for draws whose referenced vertices are all `0xFFFFFFFF`. Make
`NoDraw` shadow casters invisible instead of checkered. Carry `TwoSided`.

Acceptance: checker pixel count in the reference views drops to zero for these
classes; a decal on/off toggle changes only pixels under decal surfaces; the
2× modulate of a mid-grey texel leaves the destination unchanged within one
code value. SwiftShader is sufficient; one device run checks blending
precision.

### Stage 3 — `SourceRenderSideStaticsV1`

Read PROC `RenderSideOnlyEntityDicts`, place the 2,025 `func_static` models
with their origin, rotation, skin and `noshadows`, render only (collision is
owned elsewhere). Refuse `func_static_instance_merged` until its shape is
read. Acceptance: placed count and refusals; offscreen views of the station
hall show the props; per-frame draw counts are recorded. Device run for draw
call cost.

### Stage 4 — `SourceMaterialStagesV1`

Generate full interaction materials: `diffusemap` (with `vertexColor`,
`alphaTest`, `twoSided`), `bumpmap` from `_local` with the A→R pre-swizzle,
`specularmap` converted to Wolfenstein's luminance intensity with the
material's bias/scale, and a per-material exponent. Admit the detail-normal
technique without its detail layer only as a named approximation, or refuse
it. Read source normals and tangents from the vertex buffers
(`SourceTangentFrameV1`) instead of regenerating them. Check the compiled map
`DECL` against the `.mtr` text first.

Acceptance: unit tests for the swizzle and specular conversion with synthetic
DDS; offscreen render of one controlled light over a normal-mapped wall,
compared with the flat-normal build (`r_skipBump`-style toggle) and with
`r_skipSpecular`; tangent-frame agreement statistics against the source
vectors. Lighting appearance needs a device run.

### Stage 5 — `SourceStaticLightsV1` (with `SourceAreaPortalV1`)

Spawn the 632 PROC lights as render lights: point and projected types,
attenuation and projection images, colour and specular colour (including
values above 1), `light_specular 0`, `light_center`. Rectangular attenuation
maps to native falloff; spherical attenuation needs a small program change or
a named approximation. Light shaders, back-lighting and light groups start as
explicit refusals. Shadows stay off in this stage. Rebuild areas and portals
from PROC first, or the forward renderer tests every light against one
291,112-triangle area.

Acceptance: spawned/refused light counts by field; offscreen per-light toggles
change pixels only inside each light's volume; interaction counts per frame
logged. Frame time on a physical device is a required gate.

### Stage 6 — `SourceAtmosphereV1`

Sun as a parallel light restricted to outdoor areas, hemispherical ambient
for outdoor and indoor areas, scene and vista fog, sky dome models and
`lightScale`. This needs the hemisphere-map parameterisation from static
inspection of `DSAmbient_STD_2D.pso`, the indoor/outdoor classification and
the stream-pack name rule (all **Unknown** today). Acceptance: mean
luminance per reference region within a stated tolerance of the PC capture;
fog colour at known distances; device check for gamma.

### Stage 7 — `SourceShadowsV1`

Shadow maps for the 129 shadow-casting lights (Key lights first) and cascades
for the sun; soft-shadow blur as an option. Acceptance: offscreen shadow on/off
masks; device frame-time and thermal budget.

### Stage 8 — `SourcePostProcessV1`

Desaturate/scale/pow grade (`0.4 / 1.3 / 1.6` defaults), bloom at half and
quarter resolution as in `renderer/rendertargetprecache.lst`, then DOF.
Requires the `train_station` post-process values. Acceptance: histogram and
saturation statistics against the reference; device cost.

### Stage 9 — `SourceForwardSpecialsV1`

Glass, water, environment reflections (needs cube DDS support), glowing
windows, flares and parallax: 62 world surfaces plus MODL users. One profile
per technique, each with its own refusal.

## 9. Mobile GLES3 performance risks

- **Forward lighting cost.** Doom 3 draws each surface once per light. With
  632 lights and today's single area, culling is per-surface bounds only.
  Areas/portals and light-area membership are prerequisites for stage 5.
- **Shadows.** Stencil volumes are fill-rate heavy on tile-based GPUs. Point
  light shadow maps cost six faces each; 129 casters need a budget, caching
  and priority by category.
- **Texture memory.** Full-resolution RGBA8 is about 297 MB for referenced
  material images alone; compressed is about 53 MB. Runtime ETC2 encoding of
  about 1,000 images is slow, so cache the result. S3TC availability differs
  between vendors.
- **Overdraw and alpha test.** 386 blended decal surfaces and 254 alpha-tested
  surfaces disable early depth rejection or add blending on tilers.
- **Post-process bandwidth.** Full-screen passes must run at reduced
  resolution, as the original's half/quarter targets do.
- **Precision.** Specular exponents up to 350 under `mediump` can band or
  overflow; Mali and Adreno differ.
- **Draw calls.** Stage 3 adds about 2,000 props; batching or distance culling
  may be needed.
- **Shader variants.** Each new material/light combination adds a program;
  compile stalls must be front-loaded.

## 10. What SwiftShader cannot verify

SwiftShader on the Android x86_64 emulator verifies functional output:
program compile/link, GL errors, offscreen pixel comparisons and ES 3.0 core
formats (ETC2). It cannot establish:

- frame time, sustained performance, thermal throttling or battery cost;
- vendor driver behaviour and `mediump` precision on Mali, Adreno or PowerVR;
- which compressed formats (S3TC, ASTC) a target device exposes;
- memory pressure and the low-memory killer on a real device;
- tile-based costs of blending, alpha test, stencil and shadow maps;
- display gamma/sRGB appearance, anisotropic filtering and MSAA quality.

Stages 1, 5, 6, 7 and 8 therefore each need at least one physical ARM64 run,
preferably on one Adreno and one Mali device, before any claim of visual or
performance parity.

## 11. Open questions

| Question | Status | How to close |
|---|---|---|
| Retail detail-pack swap policy and stub use | Unknown | Static inspection of the streaming code; runtime capture in an isolated copy |
| Specular power packing in G-buffer alpha | Unknown | Read the directional/point light pixel programs |
| Hemisphere map parameterisation | Unknown | Read `DSAmbient_STD_2D.pso` |
| Indoor/outdoor classification (stencil 0/1 source) | Unknown | Static inspection of the G-buffer stencil write and area flags |
| Atmosphere stream-pack name rule | Unknown | Static inspection of the atmosphere loader |
| `train_station` post-process values | Unknown | Compiled declarations reader |
| Light shader tables (`fire_city`, …) | Unknown | Compiled declarations reader |
| Non-white vertex colour on multiply decals | Unknown | Read `StandardDrawTexture.pso` |
| Compiled map `DECL` versus `.mtr` text | Unknown | Compare through `CompiledDeclarationCatalog` |
| MODL detail-pack identity | Unknown | Hash comparison as done for textures |
| Portal geometry for areas | Unknown | PROC/AAS inspection |
| Veil (Shroud render set, 4 trainyard materials, `shroud.mt` templates) | Out of scope here | Separate review when the Veil is reached |

Related pages: [GEOMETRY](GEOMETRY.md), [CHECKPOINT](CHECKPOINT.md),
[ENGINE](ENGINE.md), [renderer adapter](../src/idtech4a_render/README.md).

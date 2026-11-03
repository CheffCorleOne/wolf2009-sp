# Source-backed native MD5 import checks

This target tests the neutral weighted-model adapter without an engine or GL
context. Configure this directory independently and build
`wolf2009-skinned-model` with CMake. `WOLF2009_MINIZ_SOURCE_DIR` can select the
pinned engine miniz directory; otherwise the build requires zlib.

`wolf2009-skinned-model --self-test` uses authored geometry, 52 named joints,
four influences, rotated bind transforms, distinct colour bytes, independent
draws and independently supplied poses. It checks request identity (including
all source epochs), compaction, hierarchy, admission and the source CPU oracle.

`--source-base <owner-supplied SP/base>` reads the original trainyard MPK,
global SPK and character detail SPK. It compares map LOD0 and character LOD0/1
against `PoseGreeterBody` at bind and five source spline sample times, including
a half-frame time. Original data is never written or distributed. Optional
`WOLF2009_SP_BASE` adds this invocation to CTest. The recorded conversion
threshold is 0.0002 game units; source reports print the measured maximum.

The engine importer is a separate layer. Include `EngineSkinnedModel.cmake`
after creating `wolf2009_render_bridge`, and apply the matching engine patch.
It adds source-file support and the `WOLF2009_RENDERER` definition to that
bridge so its MD5 class declarations match the core renderer.

Register exact source-qualified MODL, HKXA-container and SKEL-container paths
and an explicit LOD with `Wolf2009_RegisterSkinnedModel`. The returned typed
MD5R alias is passed to the ordinary model manager. The registration reads
mount epochs; loading pins all distinct mounts, validates the existing named
HKXA/SKEL/body profile, and copies geometry into `idRenderModelMD5`. Pins and
owning source identity remain until normal model purge/destruction. A stale
alias cannot reopen a remounted source. The alias hash is a local cache key,
not authentication. Material and image paths retain their existing mount
identity: reusing a mount label for different data also requires the normal
material/image cache lifecycle or a fresh label.

`EngineSkinnedModelData.h` must precede the engine precompiled header. Its
`Wolf2009_ToNativeModelJoints` conversion accepts independent model-space
poses; `Wolf2009FrameBounds` obtains native bounds for the supplied joints.
Runtime verification must use actual normal-model-manager loads and native
dynamic snapshots, compare source triangle corners/attributes (native deform
topology may append seam vertices), test two independently posed instances,
and cover purge, source epochs, stale reload and reload at the same epoch.
Native default local joints also need reconstruction through the native
hierarchy and comparison with the decoded model bind pose.

This milestone preserves the existing qualified diffuse material, UV and
native RGBA path. Unsupported materials retain the existing explicit checker
diagnostic. Normals/tangents use the native deform pipeline; original authored
skinned lighting, shadow buffers and tangent-handedness equivalence are not
claimed. Tangent handedness remains retained in the neutral data.

The existing named 52-joint body reader supplies this initial loading profile.
There is no automatic skeleton inference, generic HKXA selection, animation
manager hook, pose baking, entity placement or mission binding. The 27-joint
head, its unverified copy timing and facial animation remain outside scope.
Portable/syntax checks alone do not establish native runtime or GPU success.

## Authored source appearances

For a resident model, `Wolf2009_RegisterSkinnedAppearance` accepts an exact
original skin name and material-catalog path. It redecodes the leased source
request, compares retained source bytes, applies `SkinCatalog`, and checks the
provided catalog against the current diffuse profile. Every selected draw must
have a qualified remap, available original DDS, and compatible already imported
UV/RGBA attributes. Unknown, unsupported and conflicting mappings fail before
creating declarations. There is no geometry rebake or generated replacement
texture.

The returned immutable native skin name selects material clones through
`renderEntity.customSkin`. It does not replace a shared mesh shader. Cache keys
retain model, source skin and exact pooled catalog bytes; the catalog has a
128MiB aggregate budget and appearances a 4096-entry budget. Catalog bytes must
remain stable for the caller's intended lifetime. The existing material/image
mount-label cache contract still applies.

Actual Android15 x86_64 headless QA passed with ordinary model-manager loads,
native SIMD hierarchy reconstruction and dynamic MD5 snapshots: 385482 checks,
14 poses, 32619 triangle corners, max position error7.63e-6, UV error0. It also
covered independent instances, cached updates, same-epoch purge/reload, stale
epoch refusal, fresh alias loading and released mount pins.

The original `resist_fighter_b` skin remaps the otherwise unavailable default
material to an original Kreisau texture in `allies_kreisau_agt02.spk`.
The native image loader returned its 1024x1024 BC1 image; RGBA CRC `daf354cd`
matched the independent DDS decoder. Default instance A retained its checker
and exact mesh/material bytes while B selected a separate qualified clone.
Identical registration was idempotent; unknown and unmatched skins produced
no declaration effects. One positive remap is established, not all source skins.

This proof has `gpu=0`. The separately tested world GLES pipeline does not prove
visible or correctly lit skinned characters. Ordinary animation-manager playback
and campaign entity placement remain the next integration work.

## Native body GPU check

Building and requesting both `WithSkinnedChecks` and `WithOffscreenChecks` also
runs the source body through an ordinary render entity in a native render world.
It uses the same EGL pbuffer, BeginFrame/RenderScene/EndFrame, readback and BMP
writer as the world checks. The production authored skin supplies the original
diffuse texture. A separate test skin changes only that source map to native
`_white`; an unchanged camera and a second source spline pose isolate texture
and deformation contributions.

Android15 x86_64 GLES3/SwiftShader passed: 24433 texture-dependent pixels and
41950 changed pixels between the two source poses, at 512x512. The backend bound
one original 1024x1024 TXTR; all three readbacks had GL error0. Frames were
visually inspected and normal world/model destruction released all mounts.
Unrequested skinned checks stay inactive in a build that includes this QA.

This is the original body mesh and diffuse material, with supplied source poses.
There is no head/face, game-entity animation playback, source actor collision or
campaign logic in this GPU check. Full source lighting and shadow equivalence
are not established. Frame outputs stay in ignored local evidence.

The additive geometry-only mesh API is documented in
[MESH_V2.md](../../src/idtech4a_render/MESH_V2.md). It removes the original
one-clip dependency from mesh loading while preserving v1 requests/aliases.
Portable 198 synthetic and 9299 original-data checks passed, including an
HKXA-read trap, own 3/7-joint fixtures, exact named skeletons, LODs and the
selected original standalone head. Android x86_64 native checks also passed:
193461 checks, 10 supplied poses and 23775 corners, with normal teardown.
Animation, actor placement and head attachment require their own bindings.

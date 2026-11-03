# Actual engine lifecycle check

This opt-in integration builds the pinned GPL Doom3 engine and **GPL Doom3
game module**, then runs its ordinary `common->Init`, `LoadGameDLL` /
`GetGameAPI` version 8, session initialization, `common->Frame` and
`common->Shutdown`. No original EXE or game DLL is loaded. The original
Wolf2009 game ABI is 19; claiming compatibility with that ABI would be incorrect.

The `wolf2009/runtime` fixture additionally loads an independently authored
in-memory CM triangle mesh through the ordinary collision model manager. It
checks 16 point/box/cylinder traces, finite surfaces, both-sided sheets, content
masks, slopes, seam sliding, contents/contacts, and native `idPhysics_Player`
walking across a triangle seam. The parser owns its geometry after the input
stream closes; ordinary shutdown must invalidate its model handle. The script
requires all three triangle mesh/player/lifetime markers for that map. Source
HKXS worlds keep their existing completeness gates. No APK packaging is needed
to run these C++ checks.

After the original mover/player checks, the same fixture explicitly loads
`wolf2009/_qa/rounded-support`: an empty native CM plus an owned support BVH
with an authored rounded floor and sphere. This exercises the new CM hooks
without a polygon fallback. Checks include actual trace-model vertices,
transformed frames, masks, physical position/contents, multiple contacts,
pivot arcs and native player input. Expected ambiguous/invalid cases use the
typed adapter directly; the public CM wrapper raises an error on uncertainty.
Normal shutdown must release both the native handle and its sidecar.
The original HKXS mainworld bridge is tested separately with `RealEmptyGameRoot`;
a complete campaign remains outside these fixtures.

The Android entry point is
`Wolf2009_RunHeadlessRuntime` in `src/idtech4a_boot/AndroidRuntime.cpp`.
`net_serverDedicated=1` selects the existing engine path without a GL surface or
audio device. The helper is bounded and must run once per fresh process. It
does not replace the normal Android Activity / `GetIDTechAPI().main` entry.

Build on Windows with the project's installed NDK and CMake:

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556
```

These commands use a separate `build/engine-runtime-android-<abi>` directory.
Both scripts accept `-BuildDirectory` to reuse an explicitly chosen engine build.

The fixture is independently authored: a default material, empty UI documents,
minimal old-format font wire data, default configuration and an empty main
script. No retail game resources are required for the first check. A success
must include `WOLF2009_RUNTIME_STATE common=1 game=1 session=1`, eight common
frames, successful game/module and filesystem teardown, and
`WOLF2009_ACTUAL_ENGINE_RUNTIME_QA_OK`. A filesystem-only check cannot produce
these results.

`-GameplayAssets src/idtech4a_gameplay/assets/base -Map wolf2009/runtime`
adds the independently authored GPL Doom3 player definition, script object,
three-joint model and test room. A separately authored single-area `.proc`
provides a simple floor; collision is built from the fixture's actual `.map`.
`devmap` then takes the ordinary
`idSessionLocal::ExecuteMapChange` → `idGame::InitFromNewMap` → `SpawnPlayer`
path. The headless check does not prove visible rendering or Wolf2009 gameplay.

`-RealEmptyGameRoot <own copy>` instead mounts the original `empty.mpk` before
the ordinary source-qualified `devmap wolf2009/main/PROC/maps/game/empty/empty`.
The generated `.map` stays in memory and contains worldspawn plus the actual
ENTS player start, with no brushes or primitives. Ten decoded HKXS boxes are
owned by a separate support index bound to native CM handle zero. The loader
refuses brush/render fallback, pins the source mount and validates its epoch,
content fingerprints and declared Doom3 contents policy before binding. This
policy is not the retail runtime filter. With the chosen Doom3 player half-width
16, native movement stops at X167.7497 against the source wall at X184. The
MPK remains mounted through game/render shutdown; the reserved VFS correctly
refuses the engine's attempted `.cm` cache writes. This is a Doom3 physics
baseline, not a reproduction of Wolf2009's Havok capsule or movement rules.

`-WithWolfRenderer` / `-WithWolfGameplay` enable the independent bridge CMake
fragments when building. `-WithRenderChecks` additionally compiles the optional
native world/model/image checks; the test's `-RendererGameRoot <own copy>` copies
three MPKs and a renamed material archive only into ignored local evidence and
the isolated device fixture. It requests those checks through an explicit
fixture marker. A combined gameplay + renderer check runs inside one initialized
common/game instance. It still supplies no GL context and proves no GPU output.

For the separate native GPU check, build with `-WithOffscreenChecks` (this also
enables the renderer and native render checks), then pass `-WithOffscreenChecks`
and `-RendererGameRoot <own copy>` to the test. It creates an explicit 512×512
EGL pbuffer on the selected headless emulator, uses the normal native
`BeginFrame` → `idRenderWorld::RenderScene` → `EndFrame` path, and reads the real
framebuffer. It never creates a native window. For each map, a controlled second
frame replaces only proven source DDS references with `_white`; geometry, UVs,
camera, culling and the opaque depth pipeline stay the same.

The primary opaque test passed on 2026-11-03: 254,576 source-texture-dependent
pixels for `empty` and 102,395 for the warehouse, out of 262,144 pixels each.
All four readbacks reported GL error zero. The actual depth/stencil helpers
passed renderbuffer → texture and texture → renderbuffer completeness checks.
Normal `common->Shutdown` released the game, filesystem and GL renderer. The
separate translucent/two-sided/alpha diagnostics used during investigation are
not credited as this primary pass. Two upstream initialization `GL_INVALID_ENUM`
diagnostics remain in the captured log; the test does not claim error-free GL
initialization or complete Wolf2009 material/shader compatibility.

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -WithWolfGameplay -WithOffscreenChecks
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -RealEmptyGameRoot 'C:\path\to\own\Wolfenstein' `
  -RendererGameRoot 'C:\path\to\own\Wolfenstein' -WithOffscreenChecks
```

The build runs the complete pinned patch-chain preparation before configuring;
it does not apply individual overlapping patch fragments itself.

`-WithBrainChecks` builds the bounded native literal BrainTask host QA in the
GPL game module. The test generates authored graphs and mandatory shared
defaults, checks cross-entity Use/SetGlobalVariable through the real entity
registry, and requires both game-module and teardown success markers. The
default map is the authored runtime fixture; it can also run with original
`empty`. [Profile and limits](../engine-brain/README.md).

`-WithSkinnedChecks` enables source-backed MD5 import QA. The test additionally
copies the owner's global and Kreisau character SPKs into ignored evidence.
It loads real weighted source geometry through the normal model manager and
compares native dynamic snapshots, attributes, bounds and mount lifetimes.
This headless check supplies no GL context and establishes no visible character
rendering. [Model profile and limits](../engine-skinned/README.md).

Requesting both skinned and offscreen checks additionally renders the original
body with its authored skin in two source poses. It reads actual GPU frames and
compares a texture-only white control. Three `offscreen-skinned-*.bmp` frames
are pulled into ignored evidence. This separate marker proves body diffuse/
deformation rendering; it does not establish game-entity animation or a full NPC.
The test owns only a unique `/data/local/tmp/wolf2009-runtime-<nonce>` directory
on the explicitly selected target. It does not install or foreground an app.

The first actual Android 15 x86_64 checks passed on 2026-11-03: common/game
startup and shutdown; ordinary `devmap wolf2009/runtime` with a valid native
player, native moving-platform carrying, trigger/target removal, wall stopping
and sliding, increasing game time and eight common frames; source-qualified
original `empty` through the native map/collision/player path; then actual render-world,
model-manager and image-loader checks on owner-supplied empty/trainyard/warehouse
resources. Per-run logs and exact binary SHA-256 values remain under ignored
`evidence/engine-runtime/`.

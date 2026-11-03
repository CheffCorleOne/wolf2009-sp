# Native Android engine host

This APK uses `GetIDTechAPI().main` in the `:engine` process. The native engine
owns EGL, its frame loop, GPL game loading and input interpretation. The host
reuses the pinned Q3E event queue and JNI thread-local attachment sources without
editing them. `tools/prepare-q3e-host.ps1` verifies their exact hashes.

Normal entry asks for the owner's original `SP/base` document tree and copies
only `assets.pk4` and `maps/game/empty/empty.mpk` into a new private source set.
The material archive keeps its exact bytes under `fixtures/wolf-materials.bin`,
so the Doom declaration parser does not index Wolf material syntax. The APK's
`engine-support/base` contains only independently authored compatibility files;
it must contain no map or original retail resource. A failed import cannot
replace the last complete source selection. The original installation is read-only.

Build Java and the native host separately before APK packaging. The host CMake
source is `android/engine/native`; its required arguments are
`WOLF2009_ENGINE_ROOT`, `WOLF2009_Q3E_HOST_SOURCE`, Android API 26 and
`ANDROID_STL=c++_static`. Its output is `libwolf2009-enginehost.so`. Package it
alongside the same-ABI `libdoom3.so` and GPL `libgame.so` with native extraction
enabled. Engine builds must have Wolf streams/render/gameplay and the materials
startup command enabled. No original game DLL is used.

`Stage-EngineData.ps1` is an explicit **post-install** QA helper for headless
`emulator-5556`. It copies original inputs plus generated code support into the
already installed debuggable app's private data through `run-as`. It does not
install or launch an APK, does not use shared storage, and cannot target 5554.
It bypasses the SAF picker only for native-runtime QA; it does not prove the
document-provider import UI. All copied commercial bytes stay in ignored local
evidence and the explicitly selected app-private directory.

```powershell
.\tests\engine-android\Stage-EngineData.ps1 -GameRoot 'C:\path\to\own\Wolfenstein' `
  -SupportBase 'C:\path\to\generated\engine-support\base' -Serial emulator-5556
```

After staging, launch the installed activity with explicit debug-only extras:

```text
am start -n org.wolf2009.viewer/org.wolf2009.engine.EngineActivity --ez engineQa true --ez engineQaQuit true
```

`EngineQa` uses the imported ORIGINAL empty map. It requests the stock
`getviewpos` command through the same Q3E key queue, drives analog input to the
native wall, dispatches an Android SurfaceView touch gesture and verifies changed
native yaw, then exercises pause/resume API and actual surface destruction and
recreation. `ENGINE_ANDROID_QA_OK` requires native dispatch, measured player view
positions, two surface handshakes and continuing input pulls. The optional quit
flag calls the ordinary cooperative quit API. The helper is inactive unless both
the APK is debuggable and `engineQa` was explicitly requested.

Expected evidence includes `ENGINE_HOST_STATE ... game=1`,
`ENGINE_HOST_PLAYER_VIEW`, `ENGINE_ANDROID_QA_WALL`, surface present=0 then 1,
`ENGINE_ANDROID_QA_OK`, and normal engine teardown after `ENGINE_HOST_QUIT_REQUEST`.
Capture native screenshots as a separate visible-rendering check. Actual Android
Activity background/foreground, SAF import, small-screen/large-font layout and
physical ARM64 execution remain separate device checks. API pause plus a recreated
Surface alone is not evidence for every Android lifecycle transition.

At source creation, Java compilation against API 37 and x86_64 native host
compilation passed. No APK runtime success is claimed from those compile checks.

# Android

The published [v0.10.0-engine-dev APK](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.10.0-engine-dev) comes from source checkpoint `55fe192`. It runs the native engine and GPL game module with resources from your own PC copy. No mission is playable from start to finish yet.

Published and installed APK SHA256:

```text
2b75bc10322aebdc913225471b0bbe744fe3e3f015a259d38420b0b32daec008
```

## Requirements and tested scope

- Android 8.0 / API 26 or newer, with OpenGL ES 3.0.
- Packaged ABIs: `arm64-v8a` and `x86_64`.
- Actual runtime validation: Android 15 x86_64 emulator. Physical ARM64 hardware, iOS and WebAssembly remain untested.
- Original game data from your own Wolfenstein 2009 SP installation. The APK contains independently authored compatibility support, without commercial game archives.

Newer C++ arrival, actor and door checks are separate development work. They are not included in APK 0.10.

## Try the published APK

1. Install the APK from the release page.
2. Make `SP/base/assets.pk4` and `SP/base/maps/game/empty/empty.mpk` available on the device, preserving their relative folders.
3. Open the launcher's folder picker and select `SP/base`, its `SP` parent, or the installation root. The launcher labels are currently Russian. Import creates a private copy of those two archives and leaves the original files untouched.
4. Start `empty`. The original test map has blue grid textures. Walk with the movement buttons, look by dragging the viewport or using the look arrows, and use the jump button.
5. Exit through the launcher; reopen the app for another run.

The player starts with `weapon_none`. The touch UI has no crouch or firing control, and sound is disabled in this build. Native MP40/component audio checks do not make those features available through this launcher.

SAF import currently selects **empty only**. Importing again switches a developer-staged trainyard selection back to empty. If the app is backgrounded during initial native loading, its separate engine process may exit; reopen the app. Imported data remains available.

## Developer staging for trainyard

This post-install helper currently targets the dedicated `emulator-5556` only. It reads your original installation, prepares private app data and verifies transferred hashes. It neither installs nor launches an APK.

Set `WOLF2009_GAME_ROOT` to your own installation or `SP/base`, and `WOLF2009_SUPPORT_BASE` to the generated `assets/engine-support/base` directory recorded in the APK build report. Run from the repository root:

```powershell
.\tests\engine-android\Stage-EngineData.ps1 `
  -GameRoot $env:WOLF2009_GAME_ROOT `
  -SupportBase $env:WOLF2009_SUPPORT_BASE `
  -Serial emulator-5556 -WithTrainyard
```

The helper needs these six original inputs:

```text
assets.pk4
maps/game/empty/empty.mpk
maps/game/trainyard/trainyard.mpk
maps/game/global/global.spk
maps/game/trainyard/trainyard_vo_english.spk
streampacks/mp40.spk
```

`prepare-source-session.mjs` also extracts the two compiled global declaration catalogs from the owner's `assets.pk4`, writes the source-session descriptor, and publishes `owner.ready` / `trainyard.ready` after preparation. Commercial bytes stay in ignored local staging and the app's private directory.

Reopen the launcher and start trainyard. APK 0.10 supports the wagon ride to the stop, walking, looking, jumping and the tested Home/return path. Its door, greeter, combat and later mission events are unfinished. Keep the automated `engineQa` option off for manual trainyard: its wall/view assertions are for empty.

## Build an engine APK

The Windows helpers use PowerShell 7, Git, Node 22+, JDK 21, Android CMake 3.22.1, NDK `30.0.16248370`, API 37 and build-tools 37.0.0. An existing local debug keystore is required; pass `-KeyStore` if needed and keep signing material out of Git.

```powershell
.\tools\install-android-sdk.ps1 -Engine
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi arm64-v8a -WithWolfAnimation -Jobs 1
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 -WithWolfAnimation -Jobs 1
.\tools\build-engine-apk.ps1
```

Production inputs require `WOLF2009_ANIMATION=ON` and every `WOLF2009_*_QA=OFF`. The builder rejects stale or mismatched core/game inputs and checks both ABIs, signature, ELF/ZIP 16 KiB alignment and the package allowlist. Output: `build/android-engine/wolf2009-sp-0.10.0-engine-dev.apk`. A local rebuild does not inherit the published artifact's hash or runtime validation.

See [ENGINE](ENGINE.md) for native builds and [the Android host notes](../tests/engine-android/README.md) for explicit QA. The older viewer builder is a separate tool; use `build-engine-apk.ps1` for this engine host.

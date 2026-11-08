# Native engine development

The port extends pinned idTech4A++ commit `ce49510f3b0c04969eab8ebe8238416e84e4f0bb` with independently implemented Wolfenstein 2009 source-data adapters. The engine loads the GPL Doom3 game module through **GameAPI8**. It does not load the original Windows game DLL or reproduce its ABI19.

The Android host obtains the native main entry through `GetIDTechAPI`; the engine owns EGL, frame progression, game loading and shutdown. Portable loaders/planners and native adapters use C++11. Keep the upstream C++98 interface boundaries and API8 layout intact. [PROVENANCE.json](PROVENANCE.json) records pinned dependencies; [DEVELOPMENT](DEVELOPMENT.md) covers the working rules.

The `v0.11.0-engine-dev` APK uses source [78cf2c9](https://github.com/CheffCorleOne/wolf2009-sp/commit/78cf2c92158636bbec8d8249a4eb45114768a6d4) and version code `12`. Manual MP40 equip/fire/reload and Home/return input release passed on Android 15 x86_64 / SwiftShader GLES3. The original installation remains read-only. See [ANDROID](ANDROID.md) for the verified APK hash, staging and observed native counters.

## Prepare and build

The Windows path needs PowerShell 7, Git, Node 22+, Android CMake 3.22.1, Ninja and NDK `30.0.16248370`. The APK builder also needs JDK 21, Android API 37/build-tools 37.0.0 and an existing local signing key. Minimum Android target is API 26; rendering requires GLES3. Run commands from the repository root.

```powershell
.\tools\install-android-sdk.ps1 -Engine
.\tools\prepare-engine.ps1
node tools/check-engine-patch.mjs
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 -WithWolfAnimation -Jobs 1
```

`prepare-engine.ps1` creates or checks the ignored `engine/` checkout, applies the ordered patch chain and refuses unknown local changes. Preserve engine edits as patches rather than leaving them only in that checkout. Do not upgrade the pin incidentally.

`Build-EngineRuntime.ps1` builds paired `libdoom3.so`, `libgame.so` and a runtime runner. Default output is `build/engine-runtime-android-<abi>/`; `-BuildDirectory` selects another directory. Use `arm64-v8a` for the other packaged ABI. `-WithWolfAnimation` also enables the Wolf renderer and gameplay adapters.

For production, omit all QA switches. The animation feature must be ON and every `WOLF2009_*_QA` flag OFF. [ANDROID](ANDROID.md) covers packaging. Coordinate at most two compiler jobs across all worktrees; these examples use one.

## Native QA is a separate build

Use a dedicated build directory so QA libraries cannot become APK inputs. For the source trainyard actor slice:

```powershell
$qa = 'build/engine-qa-x86_64'
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 `
  -BuildDirectory $qa -WithAnimationV2Checks -WithOffscreenChecks -Jobs 1
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -BuildDirectory $qa -RendererGameRoot $env:WOLF2009_GAME_ROOT `
  -WithTrainyardActorChecks -WithOffscreenChecks
```

Set `WOLF2009_GAME_ROOT` to your own game copy. The test runner currently requires an explicitly selected Android x86_64 target and copies inputs into an isolated fixture. It runs ordinary `common->Init`, map/game frames and `common->Shutdown`; it does not reinstall the interactive APK. Offscreen checks create their own EGL target.

The build switch `-WithAnimationV2Checks` compiles the required QA code. Do **not** add that same switch to the source-session test invocation: legacy animation-save QA intentionally refuses the source player's wide trace cache. Run legacy and source-session checks separately.

Runtime selectors include `-WithTrainyardChecks`, `-WithTrainyardArrivalChecks` and `-WithTrainyardActorChecks`; the latter enables the earlier source checks. `-WithTrainyardDoorChecks` uses a separate original-empty session and cannot be combined with those trainyard selectors. Run `eb5902bfd353` passed its three fresh door fixtures plus offscreen regression; it did not execute the trainyard mission or VO chain. Full options and fixture conditions are in [tests/engine-runtime](../tests/engine-runtime/README.md).

## Resource and lifetime boundaries

`src/wolf2009` reads PK4/SPK/MPK and source declarations, geometry, rigs and brain data. Native bindings reuse the normal filesystem, model/animation managers, collision, sound cache and GPL entities. A typed path such as `wolf2009/<mount>/MODL/<name>` identifies a selected mount; another package containing the same raw name is not an automatic substitute.

Bindings retain source identity and mount leases through normal game/render teardown. Missing or unsupported source profiles refuse explicitly. Scene rendering, a decoded archive and a successful syntax check each establish only their own scope. Native gameplay needs actual events, Think/physics/animator state and cleanup.

Selected original MP3 samples have passed component PCM/cache checks. The opt-in `-WithAudioBank` (`WOLF2009_SOURCE_AUDIO_BANK`, off by default) adds the whole-bank scripted-voice service, the strict MP3 receipt hook and the type19 readiness provider; `-WithAudioBankChecks` adds their QA. Run `9941b7d722f0` passed it on Android 15 x86_64; it is not in an APK. [Bank profile](../tests/source-audio-bank/README.md). The v0.11 host sets `s_noSound=0` because MP40 presentation requires a real native reload sample cache. Audible output is untested, and complete weapon/VO audio is unfinished.

Earlier production core/game builds passed for ARM64 and x86_64 with QA disabled. The historical v0.10 APK came from `55fe192`; its installed hash is retained in [ANDROID](ANDROID.md). Native runtime has been checked on Android 15 x86_64. Physical ARM64 execution, iOS and WebAssembly still need their own validation. Current results are recorded in [CHECKPOINT](CHECKPOINT.md) and [TRAINYARD](TRAINYARD.md).

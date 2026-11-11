# Wolfenstein (2009) SP

An open-source C++ engine project for the Wolfenstein (2009) single-player
campaign. Android is the first target; iOS and WebAssembly are planned.
It builds on [idTech4A++](https://github.com/glKarin/com.n0n3m4.diii4a) and reads
data from your own PC copy of the game.

**Early development: the full campaign is not playable.** On trainyard the
native engine carries the player in the original wagon to its arrival, runs the
original brain graphs of five AI characters and follows the opening story chain:
the greeter opens the wagon, runs to the ladder on the original navigation mesh
and turns to the trapdoor, which opens itself; the interrogators perform the
execution scene. These steps pass native tests on an Android 15 x86_64 emulator.
No complete mission is supported yet.

[Download APK 0.13](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.13.0-engine-dev)
· [Android setup](docs/ANDROID.md) · [Current checkpoint](docs/CHECKPOINT.md)
· [Next steps](docs/NEXT-STEPS.md)

| Trainyard through the native player camera | Original MP40 in the native weapon test |
|:---:|:---:|
| ![Native trainyard: inside the original wagon](docs/images/native-trainyard.png) | ![Native MP40 with original arms and textures](docs/images/native-mp40.png) |

Actual 512×512 GLES captures from Android 15 x86_64 tests. Trainyard uses a
partial diffuse-material path; lighting, effects and retail visual parity are
unfinished. The MP40 frame comes from the separate `empty` test map.
[Capture provenance and reload frame](docs/images/README.md).

## What works

| Area | Validated scope |
|---|---|
| Game data | SP PK4 and SPK/MPK readers; compiled declarations, entities, meshes, textures, skeletons and spline clips |
| Native runtime | GPL engine/game startup, source maps, player physics, render-world and resource teardown |
| Player and MP40 | Movement, jumping, crouch physics, firing, damage, ammo and reload in isolated native tests |
| Trainyard | Original world/wagon collision, moving-platform carry and arrival; newer C++ tests also show three boxcars and exercise the relay's own use key |
| Scripted actor | Original 52-joint greeter body, authored skin and 182-frame clip; real animator start, endpoint and stale-identity refusals |
| Animated door | Original two-joint door bound to the moving wagon; real owner-authorized start, endpoint and stale/replacement tests in isolated `empty` fixtures |
| AI actors and brains | Greeter, two interrogators and two hostages with original bodies, heads and accessories; original brain networks with measured node handlers; whole-bank scripted-voice readiness |
| Execution scene | Scripted clips, frame commands, kill and death clips, remaining-time rules; the interrogator holds before combat |
| AI movement | Root motion from the original clips; the greeter's door-open move start, run on the AAS mesh, stop and turn to face the trapdoor |
| Trapdoor | Native `func_door_rotating_old`: opens itself after spawn, hinged collision |
| Combat (portable) | Hunt, enemy conditions, tactical tables and MP40 burst model checked against original data; not yet in the engine |

**APK 0.13** runs the trainyard opening chain above in developer-staged
trainyard. Stage it with `tests/engine-android/Stage-EngineData.ps1 -WithTrainyardGreeterRoute`. This APK was built and verified (both ABIs, signature, no game assets) but was not run on a device before release; the chain itself passed the native QA runs on main. See [Android setup](docs/ANDROID.md) for staging and controls.

Still unfinished: combat in the engine, speech playback, the rest of the
mission chain, objectives, save/load, the original menu and HUD, and PC-level
graphics (the renderer uses diffuse textures only; see
[rendering research](docs/RENDERING.md)). iOS and Linux hosts are planned.
Reading every archive does not make its gameplay executable. See
[validation results](docs/CHECKPOINT-VALIDATION.json) and
[how to continue](docs/CONTINUE.md).

## Build and contribute

Start with [development](docs/DEVELOPMENT.md), [engine builds](docs/ENGINE.md)
and [next steps](docs/NEXT-STEPS.md). [AGENTS.md](AGENTS.md) records the same
evidence, ownership and integration rules for AI contributors.

The Node inventory tests need Node.js 22+ and no npm dependencies:

```powershell
npm test
```

For the portable archive tools on Windows with Zig 0.15.2:

```powershell
.\tools\build-native.ps1 -Zig 'C:\path\to\zig.exe'
npm run test:native
```

Engine changes belong in the shared C++ loaders and native adapters. Useful
next contributions include native AI combat, speech playback, measured BRAI
action handlers and physical Android ARM64 testing. Please
include a reproducible case and distinguish a parser result from a playable
gameplay result. [Roadmap](docs/ROADMAP.md) · [Research notes](docs/RESEARCH.md).

## Source and game data

Project code is [GPL-3.0-or-later](LICENSE). Upstream revisions and third-party
licenses are recorded in [PROVENANCE.json](docs/PROVENANCE.json).
The original Windows game DLL is not loaded by this engine.

The repository and APK contain no original game archives or binaries. You
must supply your own game data. Screenshot artwork belongs to its respective
rights holders; the source-code license does not license that artwork.
This is an independent community project.

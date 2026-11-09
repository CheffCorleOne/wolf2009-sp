# Wolfenstein 2009 SP port

Write public documentation in English. Use Russian when talking to the owner.
This project targets Wolfenstein (2009) single-player.

Before extending the port, read [README.md](README.md),
[DEVELOPMENT](docs/DEVELOPMENT.md), [NEXT-STEPS](docs/NEXT-STEPS.md),
[CONTINUE](docs/CONTINUE.md), [RESEARCH](docs/RESEARCH.md),
[ROADMAP](docs/ROADMAP.md) and [GEOMETRY](docs/GEOMETRY.md).
Use [CHECKPOINT](docs/CHECKPOINT.md) for validated behavior and platform limits.

## Protect the reference and its licenses

- Treat the original installation as read-only. Never change its executables,
  libraries, archives, configs, saves or registry entries. Runtime experiments
  use an isolated copy and a separate profile.
- Keep builds, extracted data and raw evidence under ignored directories such
  as `build/` and `evidence/`. Never commit commercial assets, original binaries,
  decompiled proprietary implementations, credentials or personal paths/logs.
- The owner authorized real runtime screenshots for public documentation.
  Put selected captures and their provenance in `docs/images/`; identify the
  tested scope and artwork ownership. Do not synthesize or retouch proof images.
- Publish reviewed source and APKs without commercial game assets only within
  the user's authorized scope. Preserve every upstream license. A GPL engine
  does not make Quake4 or Prey SDK game code GPL.
- The owner accepts a mixed-license project. Code from the Quake 4 / Prey SDK
  trees in the pinned upstream (`quake4/`, `raven/`, `prey/`, `humanhead/`,
  `mod/quake4/`) may be reused or adapted when it fits Wolfenstein's Raven
  lineage. Keep its original license notice, mark the copied or derived files
  and record them in [PROVENANCE](docs/PROVENANCE.json); do not relabel that
  code as GPL. The commercial-asset, original-binary and decompiled-code rules
  above still apply.

## Extend the shared engine

- `src/wolf2009` owns portable format loaders and source planning. Use C++11
  with zlib or bundled miniz; keep operating-system I/O out of this layer.
  `src/idtech4a_*` contains native adapters.
- `engine/` is an ignored pinned upstream checkout. Keep engine changes
  reproducible in `patches/`, verify the prepared chain, and follow
  [PROVENANCE.json](docs/PROVENANCE.json). Preserve API8 layout and upstream
  language boundaries; qualify narrow opt-ins before changing them.
- Reuse existing archive, declaration, rig, collision, sound and native gameplay
  systems. Prefer reusable handlers over a new loader or viewer for each scene.
  Prioritize shared C++ compatibility; Android is a runtime validation frontend.
- Read original assets through those loaders. Do not generate replacement
  maps, textures or models to conceal unsupported source data.
- Admit behavior through named, explicit source profiles such as
  `MeasuredTrainyardArrivalV1`. Keep unsupported shapes, fields, nodes and
  targets as explicit refusals. Never bypass a guard
  merely to make the next scene run.
- Establish unknown behavior from original data and bounded static/runtime
  evidence. Retain raw names, keys, placement and resource identity. A guessed
  decompiler signature is not a recovered calling contract.
- Keep owning references and map/entity generations through callbacks and
  teardown. Deliver once-only events from actual native state. Resource Ready,
  actor-start ACK and door completion require their own real observations;
  timers and successful file reads cannot substitute for them.

## Coordinate and verify

- Give agents bounded tasks and explicit file ownership. Use isolated worktrees
  for independent changes; preserve other agents' and the owner's edits.
- Allow at most **two compiler jobs total**, across all agents and worktrees.
  Coordinate slots before builds and pass an explicit job limit. Use `-Jobs 1`
  while another build is active; a helper's higher default is not the budget.
- Run checks appropriate to the risk. Portable tests validate data contracts;
  native behavior needs the ordinary event/Think/physics/animator lifecycle,
  failure paths and teardown. Syntax success alone cannot establish gameplay.
- Useful entry points: `npm test`, `tools/build-native.ps1`,
  `npm run test:native`, `tools/build-preview.ps1`, `npm run test:geometry`,
  and `node tools/check-engine-patch.mjs`. Follow each subsystem's test README
  and [ENGINE](docs/ENGINE.md) for native builds.
- Report the tested build, map, platform and scope. Distinguish archive,
  rendering, native runtime and playable-mission evidence. Do not derive a
  campaign completion percentage from parser/resource counts or imply device
  coverage from one emulator. Record remaining failures before handoff.

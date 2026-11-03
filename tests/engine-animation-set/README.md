# Native controlled source animation checks

`SourceControlledChecks.cpp` is an opt-in game-module probe, compiled under
`WOLF2009_ANIMATION_V2_QA`. The existing core v2 driver supplies registered
source aliases, preserving core ownership of model and four-role capabilities.

It exercises ordinary game frames and reads published palettes before helpers
can force an update. Source poses, hierarchy, native snapshots and actual bounds
are checked against the explicit source-rig oracle. The driver uses the original
335/41-frame body clips and 24/21-frame locker clips, plus a different-model
source alias as a refusal control. This is component behavior, not original
container gameplay or campaign coverage.

Run the existing `tests/engine-runtime/Build-EngineRuntime.ps1` with
`-WithAnimationV2Checks`, then `Test-EngineRuntime.ps1 -WithAnimationV2Checks`
with explicit target serial and owner-supplied `RendererGameRoot`.
Production `-WithWolfAnimation` excludes the QA callback.

# Source visual entities

`SourceVisualEntity.{h,cpp}` adds a concrete `idAnimatedEntity` to the GPL game
module when `WOLF2009_ANIMATION` is enabled. It uses ordinary native
`RunFrame` → `Think` → `UpdateAnimation` → `Present` and `ModelCallback`.
The source provider, modelDef, joints and model-manager caches keep their
existing ownership. There is no new API8 structure or game-module export.

The game-thread entry is `Wolf2009_SpawnSourceVisual(request, out, reason)`.
The caller supplies a unique native identifier, an unchanged modelDef made by
`Wolf2009_CreateSourceModelDef`, a finite rigid placement and an explicit
`WolfSourceVisual_OneShotNativeMillisecondsV1` policy/start time. The helper
checks the full request before allocating a game entity. It constructs its own
spawn dictionary, with no clip model, script, targets or bind fields. Its float
placement strings retain nine significant digits; native `SetVector/SetMatrix`
would otherwise round to two decimal places. Only one source clip/cycle is
started, with root motion retained in the visual pose and no actor movement
extraction. Later `SetModel` requests are refused before freeing the active
native model/palette.

This is an explicit visual entity interface, not a translation of original
actor classnames, AI, source ENTS `frame`, controller state or mission events.
No head is attached and no collision is synthesized from render bounds.
The native game clock is not claimed to reproduce the retail actor clock.
Save/restore is explicitly unsupported. At most 128 such entities can be live
through this interface. After successful creation the caller owns the native
entity and should retain references through `idEntityPtr`.

Deleting an entity releases its animator and render-world entry. Shared
modelDefs and zero-reference animation cache entries can still own source
leases: the level owner must release its definitions/models and flush unused
animations before unmounting packages. The helper does not force-release pins.
Preflight errors allocate no entity; post-allocation failures retain the
upstream `SpawnEntityType` error behavior. In particular, its `idAllocError`
catch does not guarantee rollback of an object already registered by `CallSpawn`.

Actual Android 15 x86_64 checks passed: two entities across 506 native frames,
raw palettes read immediately after normal Think, separate forced renderer
callback checks, world-joint placement, skinned snapshots/bounds, one-shot
endpoint with `TH_ANIMATE` inactive, zero collision models, rejected requests
before spawn, model-change refusal, deletion and generation-safe slot reuse.
There were 23,130 entity checks. Together with the existing animator checks:
179,663 checks, 35 pose comparisons, 25 snapshots, one native crossfade and
27 legacy controls. Source descriptors returned to zero at normal shutdown.
This is CPU/game/render-world validation; the separate GLES body proof still
uses supplied source poses. It does not prove an animated entity GPU frame.

Reproduce with `tests/engine-runtime/Build-EngineRuntime.ps1 -WithAnimationChecks`
and `Test-EngineRuntime.ps1 -WithAnimationChecks -RendererGameRoot <own copy>`.
The test requires `WOLF2009_NATIVE_SOURCE_VISUAL_QA_OK` as well as the source
animator, descriptor and normal runtime shutdown markers. Original resources
and private run logs are excluded from the source repository. ARM64 engine/game
also build with QA disabled; a physical ARM64 runtime test remains outstanding.

# Native source animation sets

`Wolf2009_CreateSourceAnimationSet` constructs an ordinary native modelDef with
several named source-v2 clips for one qualified mesh. The existing single-clip
factory and SourceVisual entity retain their APIs. No files are baked or
generated from the commercial animation assets.

Each entry supplies an exact logical name and an already registered animation
alias. The factory checks the actual source model identity and complete ordered
hierarchy for every clip. Equal joint counts and equal names on another model
are insufficient. The immutable declaration has at most 64 clips, 127-byte
identifier keys and 64 KiB of text. Paths are bounded and quoted safely. Full
text is compared on hash collisions and when discovering an existing set.
Native parsing must produce exactly the requested clips, with no animation
flags, frame commands or synchronized multi-animation entries. Exact lookup
uses `GetSpecificAnim`; native random alias groups are not selected.

`SourceControlledEntity` adds a game-private visual entity with two commands:

- `HoldFrame(key, sourceFrame, blendMs)` accepts a zero-based source frame.
- `PlayOnce(key, blendMs)` starts on `gameLocal.time` and holds the final source
  frame at the native ceil endpoint. An incoming blend cannot exceed clip length.

Both use the ordinary animator, Think, UpdateAnimation, Present and model
callback. Fixed-frame playback reaches the direct block-aware source sampler;
it never converts a frame through an inferred integer FPS. An accepted hold
explicitly forces the next native update, including multiple commands in the
same millisecond. The private native frame field is wide enough for frames above
32767. Legacy saves retain their short field and reject an unrepresentable value
before writing. Source animator/entity saves remain unsupported.

The controller preflights placement, names, source identity, clip/frame, clock
overflow and blend ranges. Failed commands leave the channels and controller
state unchanged. It uses full-pose `ANIMCHANNEL_ALL`, one-shot native milliseconds,
no extraction, no frame commands and no looping. A stable held pose keeps native
animation thinking active without resampling unchanged palette data. `Held` is
separate from native one-shot completion.

Each entity retains immutable expected metadata and additional native animation
references. Forgetting set discovery does not invalidate a bound entity.
The registry retains immutable ownership metadata until shutdown; the same
unchanged set can be registered again, without adding another native declaration.
Destroy entities before freeing native declarations/models, then flush unused animation
cache entries before unmounting source packages. Native allocation failure during
`SpawnEntityType` is not guaranteed to roll back by the upstream engine.

These are shared C++ engine/game primitives. Original ENTS `frame`, logical
modelDef filename conversion, container activation, loot, physics, sounds and
other authored commands require their own source bindings. The original
`container_locker_foot_01` is not a generic open/close toggle. The API does not
claim complete original class behavior, a playable mission or a retail clock.

Native checks are added to the existing `-WithAnimationV2Checks` runtime. The
plain three-string QA callback receives two qualified source clips and another
source clip bound to a different actual model. QA remains excluded from
production builds; no API8 import/export layouts change. Original assets and
local run outputs are excluded from publication.

Actual Android 15 x86_64 validation passed both body335/41 and locker24/21
probes: 14524 checks, 19 pose comparisons, 10 native snapshots and 335 ordinary
game frames. Maximum joint error was `7.63e-6`, bounds error zero. Legacy native
frame40001 and short save/restore passed; a source-v2 clip above32767 and the
fatal source-save refusal were not executed. Changed declarations are refused
before native parsing in both factories, preserving already live entities.
The combined source-empty/player, BRAI, V1/V2 animation, mesh and GLES regression
also passed with zero remaining descriptors. ARM64 production compiled with QA
disabled; physical ARM64 runtime is untested. No new APK was packaged.

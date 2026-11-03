# GPL Doom3 gameplay bridge

This adapter feeds decoded collision and explicit entity behavior into the
existing Doom3 game module. It does not extend the train's one-dimensional
`RiderMovement` probe into a character controller. Native `idPlayer` owns
`idPhysics_Player`; native `idClip`, the collision model manager, pushers and
the event queue perform movement, collision, touching and entity removal.

## Implemented boundary

`GameplayMap.cpp` produces a normal Version 2 `.map` and an optional map
script. It accepts exact axis boxes, a proper rigid placement and explicitly
selected runtime roles. Render bounds never become collision. Names, targets,
bind cycles, rotations, contents and output budgets are checked before output.

| Input | Existing GPL system |
|---|---|
| Complete decoded HKXS world boxes | `idMapFile::Parse`, `collisionModelManager->LoadMap`, `idClip` |
| Selected ENTS start | `idPlayerStart`, normal client spawn, `idPlayer` / `idPhysics_Player` |
| Exact model-local boxes | An inline clipmodel with its own name and an independent visual resource |
| Explicit static role | Concrete `idWolf2009Static` registration of the ordinary `idEntity` static lifecycle |
| Explicit moving role and destination | `idMover`, `idPhysics_Parametric`, native pusher contacts; `speed` / `moveToPos` events |
| Explicit touch volume and targets | `idTrigger_Multi`, native touch tests and activation queue |
| Explicit remove-targets role | `idTarget_Remove`, normal deferred removal and registry cleanup |

`idEntity` is abstract in the engine's registration table. The small concrete
static registration preserves its native static physics without changing
collision contents. Stock `idStaticEntity::Spawn` would overwrite those
contents with zero or one.

The optional engine patch preserves a source-qualified map name through the
ordinary Session map command. A request for
`wolf2009/<mount>/PROC/maps/<name>.map` reads same-mount ENTS and HKXS through
the existing read-only filesystem policy and returns generated map text.
Native renderer loading retains the corresponding `.proc` path. Generation
runs outside the stream mutex because it reads two typed resources through
that service. Generated files are never written into the user's game.

To enable it, apply `patches/idtech4a-wolf2009-gameplay.patch` after the
stream/renderer patches and configure the engine with `CORE=ON`, `BASE=ON`,
`WOLF2009_STREAMS=ON`, `WOLF2009_GAMEPLAY=ON`, and
`WOLF2009_GAMEPLAY_DIR` pointing to this directory. The module is GPL Doom3
`game`, with API 8. No proprietary Quake4 SDK implementation or Windows game
DLL is linked or loaded.

## Own bootstrap content

`assets/base` contains independently authored definitions, a script object,
a nonvisual three-joint MD5 skeleton, collision materials, and a small test
room. These make native `idPlayer` available without Doom3 commercial assets.
The script satisfies the GPL player's animation-state bindings; its movement
still runs in C++ physics.

Only the documented Wolf walk/run/crouch speeds and standing/eye heights have
been selected from the port's existing observations. The test's AABB,
acceleration, friction, stepping, gravity and other movement rules are the
GPL Doom3 baseline. This does not establish original Wolf character behavior.
Weapons, Wolf AI, missions, Veil and save compatibility remain separate work.

`BuildStaticGameplayMap` imports one diagnostic start and reports every other
non-world ENTS object as unimplemented. Arbitrary Wolf classnames are never
silently reinterpreted as Doom3 behaviors. Generic mover/trigger roles require
an explicit caller binding; they do not interpret BRAI nodes.

## Collision coverage

The world importer requires complete decoded geometry, a verified identity
world pose, known contents, and no additional source rigid bodies. The model
importer likewise rejects omitted geometry or unknown contents. Contents are
mapped explicitly, including solid/opaque/playerclip/monsterclip combinations
and separately bound trigger volumes. Unsupported bits receive no solid
fallback.

The existing `empty.mpk` qualifies: ten exact boxes and one authored start.
Trainyard and warehouse do not qualify. Rounded hulls, triangles, cylinders,
unresolved transformed subparts, extra rigid bodies and gameplay filtering
still need explicit support. Additional source bodies are inventory records,
not proof of live dynamic objects or their runtime state.

`wolf2009-collision-coverage` reports selected-world decoder omissions separately
from all-source typed-object/body metadata. Triangle counts in that report are
stored HKXS subpart counts, not newly decoded triangle geometry. It makes no
clear-passage claim for a partial map.

The canonical portable `src/wolf2009/WorldCollisionGeometry.*` adds a separate
data-only traversal for rigidly transformed boxes and ConvexVertices. It
retains local and world vertices/planes, original radius, the transform chain,
raw contents and source occurrence. It also retains lower-dimensional hull
payloads with `volumeProfileVerified=false`; retaining bytes does not qualify
them as closed collision volumes. This reader is not connected to the native
map importer yet.

Source plane/vertex consistency uses per-value half-ULP intervals and a
`gamma(7)` float32 dot-product bound. The residual, bound at the maximum
residual, and maximum excess are separate values. Neither source vertices nor
planes are adjusted. This bounds serialization and float arithmetic; it does
not invent a bound for unknown asset-cooking behavior. Unresolved source-plane
residuals, unknown material layouts and all omitted types keep coverage false.

The read-only survey of all 27 maps completed without parser errors: 281,816
raw primitives, including 154,863 boxes, 126,953 ConvexVertices payloads and
86,676 nonzero radii. There were no wrapper/leaf radius disagreements. The
survey retained 102 lower-dimensional hulls and flagged 4,141 hull occurrences
whose plane/vertex residual exceeds the stated float bound. Triangles remain
omitted: 320,559 across the selected world bodies. Complete selected-body data
coverage occurs for `mv2`, `empty` and `soundtest`; `mv2` still fails the native
importer's additional-body requirement.

| World | Decoded boxes | ConvexVertices payloads | Rounded primitives | Existing exact boxes retained |
|---|---:|---:|---:|---:|
| Trainyard | 9,386 | 5,960 | 3,138 | 7,811 of 7,811 |
| Warehouse | 1,364 | 725 | 576 | 993 of 993 |

Trainyard has a source-plane residual of 19.265 game units at a float bound
of 0.0000978. That requires further format/asset investigation. Warehouse's
largest residual is 0.000698 at a bound of 0.00854, while other pairs exceed
their bounds by at most 0.0000267. The reader records those cases explicitly;
neither world qualifies for native walking. A subsequent raw-data audit found
that 3,579 trainyard and 649 warehouse subparts use a zero-stride singleton
material profile. The importer now accepts exactly one child, one material
and a verified zero byte index, retaining rejection of multi-element zero
strides. All 15,346 trainyard and 2,089 warehouse decoded primitives now have
known raw contents. Across 27 maps, 281,501 of 281,816 primitives qualify;
315 cases on six other maps remain explicit unknowns.

Focused static inspection also showed that the retail convex point-ray method
uses `planeEquations`, while its support method scans transposed vertices and
the generic projection adds convex radius separately. Some source planes
actually cut the stored point set. The large residual therefore must not be
repaired by a tolerance or by replacing one representation with the other.
The current guard remains conservative until query-specific native policies
are implemented. The shape's own finish-loaded callback does not rebuild the
planes; higher-level loading still needs investigation.

## Placed world triangle data

`src/wolf2009/WorldTriangleGeometry.*` now joins the selected-body traversal's
reported triangle occurrences to `HavokTriangleMesh` buffers by exact storage
and subpart identities. One buffer document owns shared source arrays; each
occurrence keeps independently transformed world positions and extrusion.
Byte count and a noncryptographic source fingerprint reject accidental joins
between unrelated decoded files. This fingerprint is not authentication.

Retail class reflection and its 64-byte class constructor establish extrusion
at offset 32, index type as a byte at 56, an independent signed flip byte at
57 and triangle offset at 60. The buffer reader preserves those fields and
all raw headers. It no longer conflates the flip byte with the index enum.
Stored index order is retained; flip, welding and triangle-offset behavior
are not silently guessed as native collision rules.

All 27 source maps passed the data join: 44,082 occurrences, 346,951 placed
vertices and 320,559 triangles. The measured occurrences have zero extrusion
and zero flip bytes; all nonempty occurrences retain a nonzero triangle
offset. The unit checks also cover distinct instance placements, rotation of
extrusion without translation, raw padding/W bits, mixed-source rejection,
unknown profiles and memory/count limits.

`allReportedOccurrencesPlaced` refers only to the explicit reported triangle
parts. It does not certify the rest of the source body, extra bodies, runtime
contents or a clear path. The existing world omission/coverage report is
unchanged by the join, and native triangle collision is not yet implemented.

## Checks

Portable data tests and the exporter build independently:

```text
cmake -S src/idtech4a_gameplay -B build/gameplay
cmake --build build/gameplay --parallel 2
ctest --test-dir build/gameplay --output-on-failure
```

Use `WOLF2009_MINIZ_SOURCE_DIR` when zlib is unavailable and
`WOLF2009_EMPTY_MPK` to include a read-only test of the user's original map.
The tests exercise exact plane distances, source contents, entity/visual
separation, rotation and malformed/incomplete data rejection. They do not by
themselves run engine physics.

```text
wolf2009-gameplay-map --fixture OUTPUT_NO_EXTENSION
wolf2009-gameplay-map INPUT.mpk OUTPUT_NO_EXTENSION [START_NAME]
wolf2009-collision-coverage INPUT.mpk [INPUT2.mpk ...]
```

The own source fixture is `assets/base/maps/wolf2009/runtime.map`. With
`WOLF2009_BOOTSTRAP=ON`, `GameplayRuntimeProbe.cpp` is also compiled into the
headless QA host. After the normal `devmap wolf2009/runtime`, it uses the
public GPL `gameEdit`/`game` interfaces to test a moving support, native
forward collision, diagonal wall sliding, and trigger-driven target removal.
Only setup teleports use `EntitySetOrigin`; all measured movement uses native
`game->RunFrame(usercmd)` calls. The host then verifies ordinary game and
filesystem shutdown. No Windows input, focus, game window or graphics context
is involved in those headless checks.

Verified on Android 15 x86_64 through the actual common/game lifecycle:
platform and player both moved 24.576 units, forward movement stopped at
X=143.7496 against the authored X=160 wall, diagonal movement slid along it,
and the native trigger removed its targets and itself. The original `empty`
map also passed source-qualified native game loading directly from its own
MPK: ten boxes, one ENTS start, player stopped at X=167.7497 against the
X=184 inner wall. Both checks completed ordinary game/filesystem shutdown.
The new portable world reader also compiled for ARM64 Android API 26 with
C++11; that is compilation evidence, not an ARM64 runtime test.

The five standalone CTest entries are explicitly listed in
`tests/engine-gameplay/source-manifest.json`, together with the source and
fixture publication allowlist. Set `WOLF2009_TRAINYARD_MPK` and
`WOLF2009_WAREHOUSE_MPK` to add the read-only real-world conservation checks.
`wolf2009-world-collision-test --survey INPUT.mpk ...` checks all supplied
worlds while retaining individual errors. The survey's raw logs and any
generated game-derived maps belong only in ignored `evidence/` or `build/`.

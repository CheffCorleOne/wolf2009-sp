# Native wide trace-model draft

Status: **unfinished, not applied, not compiled or runtime-tested**. Work was
stopped at the owner's request. `unfinished.patch` preserves only the changes
actually written before the pause; `STATE.json` records the intended scope.
It is not a ready engine patch and is outside the automatic patch chain.

The source player's actual standing/crouching shapes use ConvexVertices with
radius0. Native-f32 hull construction produced50 unique vertices,96 triangular
faces and144 edges for each shape. The native limits32/32/16 are insufficient.
The proposed limits are64 vertices/160 edges/96 polygons. A1-based edge index160
requires161 side-cache bits, hence six uint32 words. Raising macros alone would
leave undefined scalar shifts and corrupt collision-cache logic.

The next implementation must audit translation, contents, rotation, loading,
cache reset, custom topology comparison, contacts/debug code and constructors'
separate polygon-edge limit. The trace-model layout must agree across idlib,
core, game and all adapters. Preserve the legacy32/33/16 serialized arrays;
wide records should refuse before writing until a separately designed save
format exists. Deterministically initialize unused storage when reading legacy
records. Tests must cover bit boundaries31/32/63/64/95/127/128/159/160 and actual
source hulls as well as ordinary legacy hulls.

SourcePlayerProfile/NativeSourcePlayer implementation has not been delivered.
Planned integration reuses the real GPL idPlayer, installs separately selected
stand/crouch clip shapes and preserves the map-specific starting loadout. The
source keepDistance2 is distinct from the native0.25 clip epsilon; no equivalent
controller behaviour is claimed from geometry data alone.

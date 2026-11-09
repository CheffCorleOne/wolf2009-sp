# AAS navigation: `DewmAAS 1.40`

Wolfenstein (2009) single-player maps ship their AI navigation in one
`AASS` stream resource per map pack. Its header says `DewmAAS "1.40"`.
The pinned idTech4A++ engine only reads the Doom 3 `DewmAAS 1.07` text format
(or `1.08` in its Quake 4 build). This document describes the 1.40 layout,
what holds for every shipped file, how it maps onto the engine's `idAASFile`,
and what the original binaries do with it. It ends with a recommendation and
acceptance tests for a reader.

Sources: the 27 `AASS` resources in the owner's map packs, the identified
`Wolf2.exe` (`e8604f64…40c9`) and `Gamex86.dll` (`7cb1cbed…2d47`), both read
from private copies. The original installation was only read. Scripts,
disassembly and raw tables stay in the ignored `evidence/aas/` directory.
Nothing was compiled and no game process was started.

Status labels:

- **Established (data)**: holds for all 27 files, or for the stated fraction.
- **Established (static)**: closed by disassembly of the identified binary at
  the cited addresses. No execution was observed.
- **Inferred**: consistent with data and code, not closed.
- **Unknown**: not recovered.

## 1. Where the data lives

Each of the 27 `.mpk` map packs under `SP/base/maps` has exactly one `AASS`
chunk with exactly one member, named after the map (for example
`maps/game/trainyard/trainyard`). No `.spk` pack has an `AASS` chunk. The
26 game maps plus the `maps/tools/mv2` test map account for the 27 files.
`empty` and `soundtest` differ in their bytes despite equal counts.

The game DLL registers a "Load AAS Chunk" handler (`0x20393180`). It builds
the resource name, sets the extension `aas28`, and hands the chunk bytes to
the engine's `idAASFileManager` through an engine-interface virtual call. The
engine parses them with `idAASFileLocal::Load` (`0x101b47b0` in `Wolf2.exe`;
its assert string names `code/aas/AASFile.cpp`). Every file carries
`fileExtension = "aas28"`, which matches the actor definitions'
`use_aas aas28`. **Established (static)** for the call order and extension;
the manager method's argument list is **Inferred**.

## 2. File layout

A 1.40 file is token text with embedded raw binary blocks. It begins:

```
DewmAAS "1.40"

1

settings
{ ... }
```

The integer after the version is the map CRC field. It is `1` in all 27
files. The loader compares it with the expected CRC. On a mismatch it warns,
and it continues only if `ai_allowOldAAS` is set (`0x101b49ed`). The value the
retail game passes is **Unknown**.

The version is parsed as a float. The loader rejects versions below 1.09, and
the `areas` parser depends on the version: ≥ 1.4 uses the current record,
≥ 1.3 an older one, anything lower the `aasAreaOld` record (`0x101b4bad`).
For versions below 1.4, an extra per-area virtual call runs after loading.
**Established (static)**.

### 2.1 Settings block

All 27 files carry identical settings. Keys in file order:

| Key | Value | In Doom 3 1.07? |
|---|---|---|
| `bboxes` | one box `(-14 -14 0)-(14 14 24)` | yes |
| `usePatches`, `writeBrushMap` | 0 | yes |
| `playerFlood` | 1 | yes |
| `allowSwimReachabilities`, `allowFlyReachabilities` | 0 | yes |
| `generateTacticalFeatures` | 1 | no (Quake 4 has it) |
| `fileExtension` | `"aas28"` | yes |
| `gravity` | `(0 0 -1050)` | yes |
| `maxStepHeight` / `maxBarrierHeight` / `maxWaterJumpHeight` / `maxFallHeight` | 13 / 56 / 20 / 90 | yes |
| `minFloorCos` | 0.6499999762 | yes |
| `maxStandHeight` / `maxCrouchHeight` / `maxProneHeight` | 72 / 40 / 24 | **new** |
| `maxGapLength` | 72 | **new** |
| `tt_barrierJump` / `tt_waterJump` / `tt_startWalkOffLedge` | 100 / 100 / 100 | yes |
| `tt_transCrouch` / `tt_transProne` / `tt_gapJump` | 100 / 150 / 200 | **new**; Doom 3 has `tt_startCrouching` instead |

`maxProneHeight= 24` has no space before `=` in every file. The one bounding
box is a prone-height hull (28 × 28 × 24), not the standing actor size
(28 × 28 × 72). Stand and crouch clearance appear as area flags instead.

### 2.2 Sections

After `settings`, the sections always come in this order:

```
planes vertices edges edgeIndex faces faceIndex areas reachabilities nodes
portals portalIndex clusters featureIndex features locationIndex locations
```

Every section except `locations` has the form

```
<name> <count> {
	bin <bytes>
<bytes of raw records>
}
```

The parser reads `<count>` and `{`, then one token. If the token is `bin`,
it reads the byte count, copies that many raw bytes into the in-memory list
and sets the list size to `<count>` (generic index parser `0x101a7b30`; the
same pattern in each section parser). Otherwise it falls back to
text records `index ( fields )`. The binary records are therefore memory
images of the engine's structures: little-endian, x86 alignment, with runtime
pointer fields written as zero. **Established (static)**.

`locations` is always text: `<count> { <i> ( <name> ) … }`, where entry 0 is
`EMPTY`.

All 27 files account for every byte up to EOF under this grammar.
`bytes = count × record size` holds in every section of every file.

### 2.3 Records

Offsets are in bytes. Field names follow the Doom 3 structure where the
mapping is shown below. "Text order" is the field order the parser's text
fallback stores. It ties each offset to the Doom 3 text column that has the
same role.

**planes** — 16 bytes: `float a, b, c, d` (in-memory `idPlane`). A point
lies on the plane when `a·x + b·y + c·z + d = 0`, so `d = −dist`. Planes come in
negated pairs (2k, 2k+1), as an `idPlaneSet` stores them. All 225,082 planes
are paired; 184 have |normal| that differs from 1 by up to 0.5 %.

**vertices** — 12 bytes: `float x, y, z`.

**edges** — 8 bytes: `int32 v0, v1`. Edge 0 is `(0, 0)`.

**edgeIndex** — `int32`, nonzero, |value| < edge count.

**faces** — 16 bytes:

| Off | Type | Field | Text order |
|---|---|---|---|
| 0 | int32 | numEdges | 6 |
| 4 | int32 | firstEdge | 5 |
| 8 | uint16 | planeNum | 1 |
| 10 | uint16 | flags | 2 |
| 12 | int16 | areas[0] (front) | 3 |
| 14 | int16 | areas[1] (back) | 4 |

Only floor faces (`flags & 4`) have edges. Their edge ranges are contiguous
and cover the whole `edgeIndex` list in order. Consecutive edges in a floor
winding share a vertex (120,046/120,046). The index sign does **not** give a
consistent direction, so a reader must chain edges by shared vertex.
Observed flag values: 0, 1, 4, 33 (= 1|0x20, on downward-facing planes with
one area, i.e. ceilings) and 64 (0x40, between two areas). The names for
0x20 and 0x40 are **Unknown**.

**faceIndex** — `int32`, nonzero. A positive entry means the face plane
normal points into the area (98.1 % of area-center tests; the rest are
centers that lie on or outside a boundary). Positive entries
use `areas[0]`, negative entries `areas[1]`, in 100 % of areas.

**areas** — 56 bytes; parser `0x101ac750`:

| Off | Type | Field | Text order | Status |
|---|---|---|---|---|
| 0 | ptr | `reach` list head, rebuilt at load | — | Established (static), `0x101a8a30` |
| 4 | ptr | `rev_reach` list head, rebuilt at load | — | Established (static), `0x101a8a90` |
| 8 | 4 bytes | always 0 | — | Unknown runtime field |
| 12 | float[3] | center (point an AI moves to) | 4–6 | Established (data): z is floor + 0.1 |
| 24 | int32 | firstFace | 12 | Established (data) |
| 28 | 4 bytes | always 0 | — | Unknown runtime field |
| 32 | int16 | cluster; negative = −portal number | 1 | Established (data) |
| 34 | int16[4] | integer xy bounds: minX, minY, maxX, maxY | 7–10 | Inferred; matches the floor winding ±1 in 99.2 % |
| 42 | uint16 | flags (below) | 2 | Established (static) |
| 44 | uint16 | contents (below) | 3 | Established (static) for 0x4, 0x80 |
| 46 | uint16 | numFaces | 11 | Established (data) |
| 48 | uint16 | numFeatures | 13 | Established (data) |
| 50 | uint16 | firstFeature (into `featureIndex`) | 14 | Established (data) |
| 52 | uint16 | numLocations | 15 | Established (static), `0x201394d1` |
| 54 | uint16 | firstLocation (into `locationIndex`) | 16 | Established (static), `0x20139520` |

There is no per-area reach range: reachabilities are sorted by source area,
and the loader links them into per-area lists. There is no stored
`clusterAreaNum`, `travelFlags`, 3D bounds or ceiling height.

Area flags come from the engine's name→bit table (`0x101a6e37`ff), cross-checked
by the game's debug printer (`0x20123b60`ff):

| Bit | Name | Doom 3 1.07 bit with this value |
|---|---|---|
| 0x0001 | `AREA_FLOOR` | same |
| 0x0002 | `AREA_GAP` | same |
| 0x0004 | `AREA_LEDGE` | same |
| 0x0008 | `AREA_LADDER` | same |
| 0x0010 | `AREA_VISITED` | **`AREA_LIQUID`** in Doom 3 |
| 0x0020 | `AREA_CROUCH` | same |
| 0x0040 | `AREA_REACHABLE_WALK` | same |
| 0x0080 | `AREA_REACHABLE_FLY` | same |
| 0x0100 | `AREA_PRONE` | new |
| 0x0200 | `AREA_DRIVABLE` | new |
| 0x0400 | `AREA_DRIVABLE_COMBAT` | new |
| 0x0800 | `AREA_DRIVABLE_NARROW` | new |
| 0x1000 | `AREA_OUTSIDE` | new |
| 0x2000 | `AREA_AXISALLIGNED_RECT` | new (spelling as in the binary) |

The data agrees. Measured from the area center to its ceiling faces,
`AREA_CROUCH` areas have 20–48 units of clearance (5th–95th percentile),
`AREA_PRONE` areas have little or none at the center, and `AREA_OUTSIDE` areas
have a median of 312. All 9,749 `AREA_DRIVABLE` areas also have contents bit
0x10. The reverse does not hold: 15,035 areas have 0x10.

Contents: 0x4 is `AREACONTENTS_CLUSTERPORTAL` and 0x80 is
`AREACONTENTS_LOCATION_VOL` (game debug printer `0x20123bc2`, `0x20123bda`).
Every portal area has 0x4, and no other area does (3,317 portal areas). Every
area with `numLocations > 0` has 0x80; 14,741 further areas have 0x80 and no
location. Bits 0x2, 0x10 and 0x200 occur (277, 9,769 and 3,186 areas with exactly
that value) and are **Unknown**.

Area 0 is the dummy area. In 21 files its flags are `0x1000`. All other area-0
fields are zero.

Of the areas, 190 out of 196,361 have `AREA_FLOOR` but no face with
`flags & 4`. The other areas without floor faces (`flags` usually `0x2` or `0x1002`)
have zero center, bounds and cluster and no reachabilities.

**reachabilities** — 48 bytes, one flat array sorted by `fromAreaNum`:

| Off | Type | Field | Status |
|---|---|---|---|
| 0 | uint32 | travelType (below) | Established (static) |
| 4 | float[3] | start | Established (data) |
| 16 | float[3] | end | Established (data) |
| 28 | int32 | edgeNum (0 for non-edge links) | Established (data) |
| 32 | ptr | `next` in the source area's list | Established (static), `0x101a8a66` |
| 36 | ptr | `rev_next` in the target area's list | Established (static), `0x101a8acd` |
| 40 | int16 | toAreaNum | Established (static), `0x101a8ab0` |
| 42 | int16 | fromAreaNum | Established (static), `0x101a8a46` |
| 44 | uint16 | travelTime | Inferred (copied by the loader; includes zero-cost WALK links such as trainyard 8351 → 8348) |
| 46 | 2 bytes | always 0, not copied by the loader | padding |

Travel flags (engine name table `0x101a6a70`; game debug printer `0x20122eba`ff):

| Bit | Name | Doom 3 1.07 |
|---|---|---|
| 0x1 | `TFL_INVALID` | same |
| 0x2 | `TFL_WALK` | same |
| 0x4 | `TFL_CROUCH` | same |
| 0x8 | `TFL_WALKOFFLEDGE` | same |
| 0x10 | `TFL_BARRIERJUMP` | same |
| 0x20 | `TFL_JUMP` | same |
| 0x40 / 0x80 / 0x100 | `TFL_LADDER` / `TFL_SWIM` / `TFL_WATERJUMP` (game printer only) | same |
| 0x200 | `TFL_PLAYERONLY` | **`TFL_TELEPORT`** in Doom 3 |
| 0x800 | `TFL_FLY` | same |
| 0x800000 | `TFL_PRONE` | new |
| 0x1000000 | `TFL_GAPJUMP` | new |

The values that occur in the files (all 27 combined) are 0x2 (357,582), 0x8, 0x4, 0x10, 0x800000,
0x14, 0x202, 0xC, 0x800008, 0x200, 0x204, 0x800200, and 0x0 (2). Geometry
agrees with the names: `WALKOFFLEDGE` drops 13–90 units; `BARRIERJUMP` rises
13–56 with travel time 113–156; `CROUCH` targets areas with 20–48 units of
clearance; `PRONE` targets `AREA_PRONE` areas. All `TFL_PLAYERONLY` records have
`edgeNum` 0 and long or vertical spans; 11 of them link to or from area 0.
The maximum is 37 reachabilities per area, below Doom 3's limit of 256.

**nodes** — 12 bytes: `int32 children[2]`, `uint16 planeNum`, 2 bytes zero.
A positive child is a node, a negative one is `−area`, and 0 is solid. Node 0 is
a dummy and the root is node 1. The descent rule is the Doom 3 rule: go to
`children[0]` when `a·x+b·y+c·z+d > 0`, otherwise `children[1]`. With that
rule, the centroid of each floor winding raised 1 unit lands in its own
area for 117,772 of 120,046 floor faces. Of the misses, 2,018 land in a
no-floor area above the face, 195 in solid and 61 in another floor area. The
rule is
**Established (data)**; the engine's lookup routine was not traced.

**portals** — 10 bytes: `int16 areaNum, clusters[2], clusterAreaNum[2]`
(text order matches). `areas[areaNum].cluster == −portal` for all 3,317 portals. The
`clusterAreaNum` fields are **not** Doom 3's index within the cluster:
6,628/6,634 of them are area numbers that lie in the corresponding cluster, and
6,274 are joined to the portal area by a reachability. Their role in
routing is **Inferred** ("the area on that side").

**portalIndex** — `int32` portal numbers.

**clusters** — 12 bytes: `int32 firstPortal, numPortals`, `uint16` (0 in
all 12,800 records), then 2 bytes that are `0x105E` in every record
(uninitialised padding). The `numAreas` and `numReachableAreas` fields of
Doom 3 are absent. Each portal listed by a cluster names that cluster on one
of its two sides.

**featureIndex** — `int32` feature numbers; per-area ranges are contiguous
and cover the list. Its length can differ from the feature count (church: 734
vs 736).

**features** (tactical cover points) — 36 bytes; parser `0x101acd80`:

| Off | Type | Field | Text order | Status |
|---|---|---|---|---|
| 0 | float[3] | origin | 7–9 | Inferred |
| 12 | float[2] | facing normal x, y (0.1 granularity, z = 0) | 5–6 | Inferred |
| 20 | 8 bytes | always 0 | — | Unknown |
| 28 | uint16 | flags | 3 | Inferred, Quake 4 `FEATURE_*` bits |
| 30 | uint16 | area | 1 | Established (data): the area lists this feature; 0 in 11 records |
| 32 | uint16 | unknown (nonzero in a few records) | 2 | Unknown |
| 34 | uint8 | height (20–59 or 72) | 4 | Inferred |

Flags fit the Quake 4 tactical-feature bits (cover 1, look left/right 2/4,
look over 8, corner left/right 0x10/0x20, pinch 0x40, vantage 0x80). Bits
0x400, 0x800 and 0x2000 also occur, with unknown meaning.

**locationIndex** — `int32` location numbers ≥ 1; per-area ranges are
contiguous and cover the list.

**locations** — text names. In memory each entry is 44 bytes and holds a
runtime entity handle at +0x24, which the game resolves through its spawn-id
table (`0x20139548`). In trainyard the 36 names after `EMPTY` equal the 36
`func_aas_location` entities of ENTS, in ENTS order; `trap_door_room` is
location 26. Other maps were not compared with their ENTS.

### 2.4 Cross-map invariants

All of the following hold in all 27 files. Totals are 225,082 planes, 306,068
vertices, 415,034 edges, 1,147,184 faces, 196,388 areas, 450,298
reachabilities, 529,913 nodes, 3,344 portals, 12,800 clusters, 9,919
features and 450 location names.

- Fixed section order and byte accounting to EOF; every `bin` size equals
  count × record size.
- Settings identical; a single bounding box.
- Records 0 of edges, faces, nodes and portals are all zero.
- Edge vertex numbers, `edgeIndex`, `faceIndex`, face plane and area numbers,
  node planes and children, area face ranges, and portal/cluster
  cross-references are all within range.
- Area face ranges are contiguous and cover `faceIndex`. Floor face edge
  ranges are contiguous and cover `edgeIndex`. A face has edges iff it is a
  floor face.
- Portal area ↔ `cluster = −portal` ↔ contents bit 0x4 agree one-to-one.
- Every area has at least one BSP leaf.
- Reachabilities are sorted by `fromAreaNum`, never self-loops, `edgeNum` in
  range, bytes 32–39 and 46–47 zero.
- Feature and location ranges per area are contiguous and cover their index
  lists; every location index entry is in `1..count−1`.

Exceptions that a reader must tolerate rather than reject:

- 11 `TFL_PLAYERONLY` reachabilities (airfield_west, downtown, farm,
  zeppelin) with `fromAreaNum` or `toAreaNum` 0, and two with travel type 0.
- 11 features with area 0 (church, downtown, farm, mte, mtw).
- 190 floor-flagged areas without a floor face.

The validator and its full counts are `evidence/aas/scripts/validate.mjs` and
`evidence/aas/validate.local.json`.

## 3. Mapping to the pinned engine

The port builds the GPL Doom 3 game module (no `_RAVEN`), so the relevant
targets are `aasArea_t`, `aasFace_t`, `idReachability` and the related types in
`engine/doom3/neo/tools/compilers/aas/AASFile.h`. Those types are filled by
`idAASFileLocal::Load` and used by the game's `idAASLocal` routing and
pathing code.

| 1.40 | Doom 3 1.07 | Mapping |
|---|---|---|
| planes | `idPlaneSet` | direct (memory layout identical) |
| vertices, edges, edgeIndex, faceIndex, portalIndex | same | direct |
| faces | `aasFace_t` | same fields, different order; edges for floor faces only; flags 0x20/0x40 unmapped |
| areas.center | `center` | direct (Doom 3 recomputes it in `FinishAreas`; must keep the file value) |
| areas.firstFace/numFaces/cluster/flags/contents | same | direct after flag/contents remap |
| areas.bounds2d | `bounds` (3D) | **derived**: xy from file; z needs reconstruction (see §5) |
| — | `clusterAreaNum` | **missing**, must be computed |
| — | `travelFlags` | Doom 3 recomputes it from contents at load |
| reach fields | `idReachability` | direct; source is a flat sorted array instead of per-area text |
| — | `idReachability::number` | missing; Doom 3 assigns it per area |
| nodes | `aasNode_t` | same fields, different order |
| portals | `aasPortal_t` | `areaNum`, `clusters` direct; `clusterAreaNum` has a different meaning |
| clusters | `aasCluster_t` | `firstPortal`, `numPortals` direct; `numAreas`, `numReachableAreas` **missing** |
| settings | `idAASSettings` | common keys direct; stand/crouch/prone/gap keys new; `tt_startCrouching` absent |
| features, featureIndex | none (Quake 4 `aasFeature_t` differs) | new |
| locations, locationIndex | none | new |

Bit remaps are required. Wolf's `TFL_PLAYERONLY` (0x200) uses Doom 3's
`TFL_TELEPORT` value. `TFL_PRONE` and `TFL_GAPJUMP` have no Doom 3 bit.
`AREA_VISITED` (0x10) uses Doom 3's `AREA_LIQUID` value. `AREA_PRONE`,
`AREA_OUTSIDE` and the drivable/rectangle flags are new. The unknown contents
bits 0x2/0x10/0x200 cannot be mapped yet. Doom 3's water bit is 0x2, but
nothing confirms that 0x2 means water here.

### Raven tactical AAS in the pinned tree

The Quake 4 path at `ce49510` uses DewmAAS 1.08. Its area records index a
separate feature list, and its 12-byte `aasFeature_t` stores quantized
coordinates, flags, two encoded normal components, height and weight.
Wolfenstein 1.40 uses 36-byte features with floating-point coordinates
and normals, an area index and additional fields. The eight low cover,
lean, corner, pinch and vantage flags align. Wolf's higher flags and
location lists still need their own contracts.

Quake 4's `AAS_tactical.cpp` uses the feature list to rank cover and tests
candidates with `WalkPathValid`. `WolfAas140V1` retains Wolf feature and
location records as side data and builds the GPL Doom 3 routing structures.
The reader preserves the file's portal lists and derives cluster-local area
indices for the GPL router. No Quake 4 SDK code is incorporated.

The comparison used `tools/compilers/aas/AASFile.h`,
`quake4/ai/AAS_routing.cpp` and `mod/quake4/q4xbase/ai/AAS_tactical.cpp`
under `doom3/neo` in the pinned engine tree.

## 4. How the original uses AAS

| Contract | Evidence | Status |
|---|---|---|
| The game hands each map's `AASS` chunk to the engine as `<name>.aas28`; the engine parses it as text with `bin` blocks. | `0x20393180`, `0x101b47b0` | Established (static) |
| Reachabilities are linked into per-area forward lists (`area+0`, `reach+32`) and reverse lists (`area+4`, `reach+36`) after parsing. | `0x101a8a30`, `0x101a8a90` | Established (static) |
| `idAASFile` exposes `locationIndex[i]` and `locations[i]` (44-byte entries) through virtual slots +0x114 and +0x120. | `0x101b22d0`, `0x101b24c0` | Established (static) |
| Point → area uses the BSP with the Doom 3 descent rule. | data, §2.3 nodes | Established (data); engine routine not traced |
| **ReachedLocation (condition 100)**: the condition resolves an actor and a location entity. It requires the location entity to pass a class check. It then takes the actor's current AAS area from its movement state (`actor+0x154`, then `+0x3c`) and scans that area's `numLocations` entries. It is true when one entry's bound entity is the requested location. Membership is per area, not a volume test. | vtable `0x20518f30` slot 40 → `0x201e95b0` → `0x200421e0` → `0x201394c0` | Established (static) for the control flow; the names of the actor fields are Inferred |
| The same scan only considers a location whose entity byte `+0x4d8` is set while a global mode value is within 9–18. It only considers a location whose byte `+0x4d9` is set while that value is outside 9–18. Two optional masks are tested against `+0x4ac` of the location entity: any overlap with the first mask, or no overlap with the second, makes the whole query false. | `0x20139505`–`0x201395c0` | Established (static) control flow; meanings Unknown (the mask is likely the `LOC_NO_*` keys) |
| A null actor makes the condition return the "reached" value (before the negation flag is applied). | `0x201e95c9` | Established (static); its use is Unknown |
| **GoTo (23) / GoToAndFaceTarget (243)** route computation, travel-flag mask, travel costs, cluster use (`aas_useClusters`), path smoothing and obstacle avoidance (`rvAASPathNode`, path voids, `rvAASRouteSensor`) | descriptors only (ctor `0x20203bb0`, `0x20204630`) | **Unknown** |
| Use of tactical features (`rvAASTacticalSensorLocal::Search`) | strings only | Unknown |

For the trainyard greeter route, the data gives the following. The greeter
origin (176.75, 271.75, 296) stands in area 6199, which is a cluster-portal area
listed in the three `layer3_func_aas_location_split_aas*` locations.
`run_to_ladder_point` (−271, 702, 318) is 6 units above floor area 8040
(cluster 575, location `trap_door_room`). A shortest search over
reachabilities alone connects them with `TFL_WALK` links only: 31 hops through 9
clusters. This shows that a route exists in the data. It is not the route
the original computes; its cost model is a research stand-in.

## 5. Recommendation

**Convert 1.40 into the engine's in-memory `idAASFileLocal` and reuse the GPL
`idAASLocal` routing and pathing. Do not write a separate router.**

Reasons:

- The original routing algorithm is unknown, so a separate router would gain
  no fidelity and would duplicate `idAASLocal` routing, path walking, traces,
  obstacle avoidance and debug drawing. AGENTS.md prefers reuse.
- Geometry, BSP, face, reachability and portal records map field-for-field
  apart from order. The missing pieces can be derived from the file or are
  side data, as listed below.
- Locations and features are not used by Doom 3 code. They belong in a side
  table owned by the Wolf adapter. `ReachedLocation` only needs the area →
  location list and the location → entity binding.

Proposed shape: a portable parser and validator in `src/wolf2009`
(C++11, no OS I/O). It turns 1.40 bytes into a neutral model with explicit
refusals for an unknown version, section, record size, count or range. A narrow
native adapter in `src/idtech4a_*` fills `idAASFileLocal` through an engine
patch in `patches/`. A named profile, e.g. `WolfAas140V1`, gates the
conversion. The patch should keep the Doom 3 text loader unchanged.

Derived fields, which must be computed and checked rather than guessed:

1. `clusterAreaNum` per area and `numAreas`/`numReachableAreas` per cluster,
   using Doom 3's own cluster-numbering rule (reachable areas first; portal areas
   numbered in both clusters). Portal `clusterAreaNum[]` must be rewritten from
   area numbers to these indices.
2. 3D area bounds and non-floor windings: each area is convex and its face
   planes with the `faceIndex` sign bound it, so clipping a box by those half
   spaces reconstructs it. The check: the reconstructed floor equals the stored
   floor winding, and the xy extent equals `bounds2d`.
3. Keep `center` from the file instead of recomputing it.
4. Bit remaps: drop or mask `TFL_PLAYERONLY` for AI; give `TFL_PRONE` and
   `TFL_GAPJUMP` unused bits that AI travel masks exclude until a profile
   admits them; move `AREA_VISITED` out of `AREA_LIQUID`.
5. Map CRC: the file holds `1`. The adapter must use an explicit
   CRC policy; it must not disable the check globally.

Unknowns that block the conversion:

- Which travel flags the original AI enables for `GoTo`/`GoToAndFaceTarget`
  with `mMovement=CombatRun` (walk only, or crouch/prone/ledge/barrier as well).
- Whether Doom 3's routing with recomputed cluster numbering reaches the same
  areas as the shipped portals. This is testable, not yet tested.
- Contents bits 0x2, 0x10, 0x200; face flags 0x20, 0x40; area bytes 8–11 and
  28–31; cluster uint16 at +8; feature bytes 20–27 and field +32.
- Meaning of the location-entity bytes `+0x4d8`/`+0x4d9`/`+0x4ac` and of the
  global mode in [9, 19).
- The retail map CRC value.

Unknowns that would block a separate reader and router: all of the above,
plus the complete original routing algorithm, cost model and path following.

### Acceptance tests for a reader

Portable (data contract):

1. All 27 `AASS` members parse with exact byte accounting to EOF. A changed
   version, an unknown or reordered section, a bad `bin` size, or a
   `count × record size` mismatch is refused with a named reason.
2. Every invariant in §2.4 holds, with the stated exceptions counted exactly
   (11 area-0 player-only links, 2 travel-type-0 links, 11 area-0 features,
   190 floor areas without a floor face).
3. Location names equal the `func_aas_location` entities of each map's ENTS
   in order (trainyard: 36/36, `trap_door_room` = 26).
4. BSP lookup of raised floor-winding centroids returns the owning area for
   117,772 of 120,046 faces (2,018 no-floor, 195 solid, 61 other floor
   area). Any change to these counts must be explained.
5. Derived `clusterAreaNum` and cluster counts satisfy Doom 3's routing
   preconditions. The reconstructed area polytopes reproduce the stored floor
   windings and `bounds2d`.

Native (runtime):

6. The converted file loads through the normal `idAASFileManager` path for
   `use_aas aas28`, survives map restart and teardown, and refuses an
   unprofiled file.
7. On trainyard, `PointReachableAreaNum` places the greeter in area 6199 and
   `run_to_ladder_point` in area 8040. `RouteToGoalArea` returns a route under
   the admitted travel mask, and the greeter reaches the point through ordinary
   Think/physics/animator movement, without teleporting or straight-line moves.
8. A `ReachedLocation trap_door_room` check turns true only once the actor's
   current area lists location 26.

## 6. Reproduce

The ignored `evidence/aas/` directory holds:

- `scripts/extract-all.mjs`: read-only scan of all 1,434 SP stream packages
  for `AASS`.
- `scripts/aas140.mjs`, `scripts/decode.mjs`: splitter and typed decoder.
- `scripts/validate.mjs`, `probe.mjs`, `correlate.mjs`, `heights.mjs`,
  `contracts.mjs`, `bsp2.mjs`, `sides.mjs`, `route.mjs`: the checks above.
- `scripts/pe-strings.mjs`, `peek.mjs`, `testnames.mjs`: string, xref and
  constant lookup in the private binaries.
- `*.local.asm`: NDK `llvm-objdump` output for the cited address ranges.

Disassembly of the cited ranges is the primary static evidence. One REA
6.8.0 / Ghidra 12.1.4 decompilation of `0x201394c0` on the private DLL copy
(Evidence `ev_c5312854…9856`) agrees with the disassembly reading of the
location scan; its inferred signature is not treated as a recovered contract.

## 7. Reader and engine adapter

The portable reader passed 79 authored checks, including 50 refusals, and 89
checks over all 27 extracted AASS members. The corpus has 196,388 areas,
450,298 reachabilities, 9,919 tactical features and 450 location entries
including the dummy entries. Conversion retains raw records and links 388,992
ordinary and 60,464 cross-cluster reaches. It omits 11 links touching area zero
and 831 links that violate the GPL router's portal assumptions. It found 20
reachable areas without finite reconstructed bounds across the corpus.

The opt-in engine adapter serves the reserved
`wolf2009/<label>/PROC/<map>.aas28` name through the ordinary
`idAASFileManager`. It reads the matching typed AASS member, requires the
measured file CRC field of 1 and generated map brush CRC of 0, and refuses
non-finite reachable bounds, links outside the routing index or more than
256 incoming links at an area. The runtime retains location and feature
data until the file manager releases its owner. The ordinary Doom 3 loader
keeps its own CRC and text-file rules.

### Native trainyard result

Run `016093956a80` passed on the Android 15 x86_64 emulator using the
original trainyard and the ordinary `aas_types` → `idAASFileManager` path.
An empty profile refused the reserved file. Two subsequent `devmap` loads
each passed 200 route checks. The loader linked 18,027 reaches, including
2,244 cross-cluster records, and omitted 92 portal-border records.

`PointReachableAreaNum` returned 6199 for the greeter and 8040 for the
floor below `run_to_ladder_point`. The ladder marker itself is in area
8028. Area 8040 listed location 26, `trap_door_room`. Repeated
`RouteToGoalArea` calls traversed 31 WALK reaches under
`TFL_WALK | TFL_AIR`, first reaching 6198 with travel time 326. Three of
the 31 raw edge costs were zero.

Reload released the first file and loaded a fresh one. Engine shutdown
released the second, leaving zero live files. The same run passed the
ordinary trainyard player/wagon session check (1,438 checks / 790 frames).
Greeter locomotion, retail route parity and the actor-bound
`ReachedLocation` condition remain open.

[Build and run the native trainyard checks](../tests/source-aas/README.md).

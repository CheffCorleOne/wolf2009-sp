# Owning map body / entity inventory

`SourceMapBodies` reads the observed HKXS rigid-body objects through the existing
bounded HavokPack reader and joins them to ENTS by exact byte names. The engine
supplies the selected world body name explicitly. Whole HKXS/ENTS bytes, source
fingerprints, typed entity data, body offsets, root shape identities, raw filters
and32 motion-state words per body stay owned by the result. The words are not
converted into entity placement, activation or collision contents.

The classification is deliberately limited:

- `SelectedWorld`: the uniquely named body chosen by the caller.
- `ExactEntityName`: one body name equals one ENTS name; reciprocal indices are
  preserved. This does not mean the entity has been spawned or is solid.
- `ResourceNameUnbound`: an unmatched literal `models/` or `gfx/` name. A future
  explicit class/model binding may instantiate it; this plan never merges it
  into world geometry or enables a zero-filter resource.
- `OtherUnbound`: unmatched names, absent names, empty names and unknown roles.

Body/ENTS names are not case-folded, slash-folded or stripped of extensions.
Duplicate names affecting a selected world/entity match refuse the entire
operation. Unused ambiguous names remain explicit lists. Shared root shapes do
not merge bodies. A nonfinite raw body state is retained with
`motionStateFinite=false`, without qualifying it for a native pose.

`BindSourceMapBodies` is also available for caller-authored inventory/documents.
That overload owns its data and makes no original ENTS-byte provenance claim.
`DecodeSourceMapBodies` performs both checked byte decoders and owns both byte
identities. Neither API traverses or admits geometry, reconstructs retail
filters or decides native static/mover/trigger behavior.

## Tests

Standalone CMake accepts `WOLF2009_MINIZ_SOURCE_DIR` or host zlib. The optional
`WOLF2009_SOURCE_MAP_BODIES_MPK` selects an owner-supplied map for a read-only
original test. The test executable also accepts `--source-map OWNER_MPK`.
No source resources, body tables or proprietary implementations are embedded.

Windows x64 Zig/miniz:2/2 CTest PASS. The synthetic suite has35 checks and15
refusals, covering exact joins, distinct world/resources/instances, empty vs
missing names, source ownership, raw pose/filter preservation, shared shapes,
ambiguities, malformed names/identity and configured budgets. Original empty
passes with1 body and2 entities. Original trainyard passes260 checks with535
bodies,2238 ENTS,227 exact-instance bodies,307 unbound resource-name bodies and
zero other bodies.

A serial read-only run of all27 installed SP MPK also passes:11953 bodies,
56139 entities,4702 exact-instance body bindings,7224 unbound resource-name
bodies, zero other unbound bodies. This includes26 maps under `maps/game` and
the separate `maps/tools/mv2` resource; selected worlds account for27 bodies.

The original trainyard wagon demonstrates why template selection is separate:
`starting_boxcar_model` ENTS2129 has no direct-name body. Its exact resource body
is `models/vehicles/box_car_a`, raw filter0, identity asset basis/origin and a
nonzero stationary swept centre. The planner retains it as an unbound resource;
the native mover must select and instantiate it explicitly.

Passing these checks proves inventory and name binding only. Native map startup,
full geometry coverage, contents/filter interpretation and campaign behavior
are separate acceptance criteria.

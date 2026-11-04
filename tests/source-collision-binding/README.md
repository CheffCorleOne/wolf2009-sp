# Source collision body wire and explicit response profiles

`SourceCollisionBindingChecks.cpp` is linked by the canonical core CMake target
`wolf2009-source-collision-binding` against `wolf2009_support_gameplay_map`.
It supports `--self-test` and `--source-map OWNER_TRAINYARD_MPK`. Original data
is supplied read-only; no source assets, original body tables or proprietary
implementations are embedded in the test.

The wire tests retain exact body-name case/slashes/spaces, typed resource paths,
requested identities and actual source epoch/byte/fingerprint/count metadata.
They check model aliases change with source generations, canonical framing/hex/
decimal fields, integer widths, byte/count limits, invalid policies/paths and
identity mismatch refusals. Successful parsing is a metadata contract, not
runtime authorization or source authenticity.

The source response tests cover:

- strict declared-contents behavior remaining separate from opt-in native
  source-word/cylinder-envelope and solid-entity policies;
- raw triangle child values and explicit zero-child/root fallback, without
  rewriting the source contents-provenance or coverage flags;
- distinct triangles sharing one source part occurrence, using the full
  shape/region/record/subpart/triangle identity; exact duplicates still refuse;
- raw-filter0 model templates needing the explicit solid-entity policy;
- a real independently authored32-point virtual cylinder, including retained
  opaque float payload bits and the non-equivalence of the retail selector;
- `SupportCollisionIndex` requiring its explicit cylinder response option,
  while keeping the same certified physical overlap and original metadata;
- immutable geometry, source identities, raw filters, omission metadata,
  triangle contact facts, cylinder facts and source qualification flags.

The original test checks29812 selected trainyard world shapes:9386 boxes,
5960 hulls,14238 triangles,34 spheres,32 capsules and162 explicit virtual
cylinders. The wagon resource has86 shapes:66 boxes,8 hulls and12 cylinders.
The original535-body ownership plan and2238 ENTS remain separate from selected
world geometry. Default strict decoding/admission still refuses this scenario.
The wagon's raw source filter stays0 even though its separately selected native
solid response is3. Source raw filter/coverage facts are never changed to claim
retail contents or activation equivalence.

Windows x64 Zig/miniz results: synthetic103 checks/76 refusals, original14503
checks/5 strict refusals; canonical targeted CTest2/2 PASS. Native engine startup,
player movement, original runtime filter equivalence and campaign playability
require separate engine/runtime tests and are not asserted here.

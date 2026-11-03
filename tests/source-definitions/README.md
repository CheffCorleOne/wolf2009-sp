# Source entity and model declarations

`SourceDefinitions` is a portable, data-only declaration catalog. It preserves
source bytes, tokens, locations, literal fields, inheritance edges and unknown
model/animation command blocks. Every retained fact owns its archive/member/
declaration provenance. Per-member fingerprints are computed; archive identities
are supplied by the caller. They are identity keys, not authentication.

The explicit policy is `AsciiCaseFoldLiteralChildOverridesV1`. Duplicate members
or declarations, malformed text, missing parents, cycles and conflicting
inherited values without a child override refuse resolution. `AddMember` is
transactional. Source, retained storage, resolution work, memo storage and output
have separate budgets, including repeated identity strings. Unknown commands
remain opaque; the result always has `gameplaySupported=false`.

Exact uppercase `MAPDEF` requires an explicit named model context. The resolver
does not infer that context from a map filename. The source test reads the
trainyard worldspawn's literal `mapSpecificDef` through `DecodeEntities4`, then
passes it to the resolver. Native parser research confirms special contextual
inheritance and child skeleton replacement, while the full runtime grammar and
context/cache lifecycle are outside this literal catalog's contract.

`ResolveModelAnimations(name, context)` is an additive data-only query. It first
runs that unchanged full-closure resolver, then resolves model-level `anim`
references by their full logical names. Its result owns the ordinary result in
`model`, including literal fields, context edges and all ancestor opaque facts.
`references`/`Find(name)` expose the selected entries and their independent
`SourceDefinitionFact` provenances after the catalog has been destroyed.

The separate policy `AsciiCaseFoldAnimLiteralChildOverridesV1` is ours, not proof
of retail parser, hash, inheritance or animation-name selection semantics.
Numeric suffixes stay intact: `walk1` and `walk2` do not synthesize `walk`.
Duplicate own names refuse. Equal inherited entries combine provenance;
different inherited values require an explicit child replacement. A child with
an unsupported payload still blocks an inherited pure entry. Missing/cyclic
dependencies and malformed names refuse even behind overrides. Model-level
`remove`, and another namespace command hidden in an opaque header, refuse
rather than expose an animation whose selection is ambiguous. Other unknown
model commands remain opaque; this is not a complete executable model grammar.

Each entry retains decoded/raw `logicalName` and, only for an unambiguous
one-name/one-asset header, `assetReference` with `hasLiteralAssetReference=true`.
`SingleLiteralNoTail` requires both tokens on the `anim` line, without extra
header tokens, internal whitespace/control bytes or bare list punctuation.
Quoted/bare ordinary literals are accepted. `LiteralHeaderWithOpaqueBlock`
retains that header and its entire block, including an empty or next-line block.
It does not implement flags or frame commands. Multiple assets, extra options
and wrapped headers remain `OtherUnimplemented`; a record without a bounded
literal logical name refuses the query. `runtimeImplemented` and both gameplay
flags remain false. `entityDef` animation keys and artist `export anim` commands
are separate facts and do not enter this table.

The second pass independently uses `resolutionWork` and `resolutionBytes` for
bounded merge work/metadata. Its conservative `resultBytes`/`resultFacts` charges
include the retained ordinary result plus added keys, tokens, origin copies,
repeated identities and container overhead. A failed query leaves the catalog
unchanged. These are retained-data bounds, not exact peak allocator/RSS limits.

The asset token is a declaration reference. No extension conversion, archive
lookup, registered animation alias, skeleton selection, playback clock, loop,
additive policy, callback timing or actor behavior is inferred. In particular,
selected `.md5anim` references are not silently rewritten to HKXA `.hkx` names.

Build the ordinary portable core target `wolf2009-source-definitions`, or
configure this directory independently with CMake. The standalone project reuses
the core's zlib/miniz settings. Run:

```text
wolf2009-source-definitions --self-test
wolf2009-source-definitions --source-catalog YOUR_ASSETS.pk4 --source-map YOUR_TRAINYARD.mpk
```

The original synthetic verification covers 81 checks in eight groups. The
animation-reference checks add 49 checks in five groups for shape classification, inherited
conflicts/barriers, full closure/context, malformed/ambiguous input, ownership
and second-pass budgets. The selected original
proof reads eight ZIP members/100 declarations: entity inheritance selects the
body model, authored skin and detail pack; model inheritance supplies the mesh,
explicit worldspawn context and literal `empty_male_skel.hkx`. Its 1186 opaque
model facts are preserved, including animation commands. This does not establish
the final actor skeleton, animation startup, frame148 semantics, physics, head/
attachments, AI or mission execution. No original member bytes are distributed.

The same optional source-catalog test also reads exactly
`def/mapobjects/animated_containers.def` into a separate catalog. The selected
locker declaration has six names: four pure references and two opaque
frame-sound blocks. Tests retain its exact member fingerprint and source spans,
keep the `.md5anim` literals, and make no runtime/extension-bridge claim. No
additional CMake target or source asset is required.

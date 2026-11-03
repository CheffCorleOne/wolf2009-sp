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

Build the ordinary portable core target `wolf2009-source-definitions`, or
configure this directory independently with CMake. The standalone project reuses
the core's zlib/miniz settings. Run:

```text
wolf2009-source-definitions --self-test
wolf2009-source-definitions --source-catalog YOUR_ASSETS.pk4 --source-map YOUR_TRAINYARD.mpk
```

Synthetic verification covers 81 checks in eight groups. The selected original
proof reads eight ZIP members/100 declarations: entity inheritance selects the
body model, authored skin and detail pack; model inheritance supplies the mesh,
explicit worldspawn context and literal `empty_male_skel.hkx`. Its 1186 opaque
model facts are preserved, including animation commands. This does not establish
the final actor skeleton, animation startup, frame148 semantics, physics, head/
attachments, AI or mission execution. No original member bytes are distributed.

# Owning compiled declaration catalog

`CompiledDeclarationCatalog` consumes the canonical portable
`CompiledDeclarations` transport DTO. It does not decode another binary format,
read files, register native declarations or execute entity/model/tree syntax.
The standalone CLI uses `Archive` only for an optional owner-supplied trainyard
packet. No original bytes are distributed.

`AddDocument(document, source)` appends one explicitly identified selected load
unit. `source` retains an opaque unique load identity, exact provenance path,
Global/Map/Explicit role, epoch and optional supplied byte count/FNV. Epoch zero
is an offline source, not proof of a live mount lease. The optional identity
must match the document. Independently of that option, admission checks every
transport span and canonical framing byte and recomputes the document's full
FNV from its owning fields. This catches caller mutation of the public DTO; FNV
is a local identity, not authentication.

The catalog deep-copies each admitted document into shared immutable ownership.
`Entry`, `Document`, `Source` and lookup matches keep that ownership after both
caller DTO and catalog destruction. Each entry exposes its original document,
section/group/record coordinates, load/document/catalog ordinals, decoded type
and name, and record-local type/name/body/envelope spans. Type/name spans retain
quotes; the body includes its braces. `Group()` and `Record()` preserve exact
source-file labels, defaultTypeId, rawId, transport spans and raw payload. Both
prefix word pools stay available in the document, including empty load units.
No pointer into a caller vector or temporary packet escapes.

Every record must contain one complete named ASCII declaration envelope:
type, name, balanced brace body and only comments/whitespace afterwards. The
lexer checks line/block comments, quoted strings, escaped quote/backslash,
nesting and exact end. Other quoted backslash pairs remain literal; header
names cannot contain control bytes. Bodies remain opaque: no key/value,
inheritance, callback or type-specific qualification occurs. Non-ASCII/control
binary payloads may be valid transport data but are explicitly unsupported by
this text-catalog profile. Malformed envelopes or a later bad record refuse the
entire append; existing documents, counters and indexes remain unchanged.

`Lookup(type, name, policy)` returns Missing, Unique or Ambiguous with **all**
owning matches in explicit append/record order. Type keys fold ASCII case only;
their raw spelling remains in the entry. The default Exact policy compares name
bytes exactly. Explicit AsciiCaseSlashLastDotV1 folds ASCII, replaces backslash
with slash and removes the suffix beginning at the last dot. The dot is not
reset at a directory separator. Even an empty resulting key stays a key; it
does not select `_default`. Raw names and paths are never rewritten.

All duplicates and canonical collisions remain visible. There is no first/last
winner, implicit global/map/loose priority, rawId lookup, typed-index import or
reload policy. The group default type does not identify every payload's actual
typename. rawId is not a universal type or precedence field. Missing means only
absent from this **selected catalog**, not runtime namespace absence and not
permission to synthesize a default blend tree. The existing source-only
`SourceDefinitions::AddMember` policy and strong transaction are unchanged.

Load/entry/source-byte, record-text/token/nesting, cumulative parse-work and
retained-data budgets apply before publication. Retained charges conservatively
include documents, strings, header temporaries, index nodes, output copies and
capacity growth; they are not exact allocator/RSS limits. Lookup has independent
match/copy-byte caps. A bounded packet that passes transport admission can still
fail a stricter catalog limit.

Configure this directory independently with CMake, optionally supplying the
existing pinned miniz directory through `WOLF2009_MINIZ_SOURCE_DIR`. Build
`wolf2009-compiled-catalog` and run `--self-test`. The optional
`WOLF2009_COMPILED_CATALOG_SOURCE_MAP` CMake setting, or
`--source-map YOUR_TRAINYARD.mpk`, reads the unique exact
`DECL/maps/game/trainyard/trainyard` resource.

The selected original check validates all 3269 named envelopes, retained raw
payloads/order/prefixes, 974 entityDef and 31 model headers, one exact locker
model and zero header matches for four explicitly queried locker animBlendTree
names. These are selected-packet facts. The model's group defaultTypeId3 and
rawId334 are retained independently from its actual `model` typename. Neither
the inventory nor absence result establishes entity gameplay, type semantics,
native import or complete runtime declaration coverage.

All types here are private portable C++11 objects. No STL object/layout is an
API8 or game/core DSO interface; a future native consumer must use its existing
readonly capability/serialization boundary and an explicit qualified projection.

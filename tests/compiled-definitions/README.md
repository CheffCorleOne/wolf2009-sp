# Compiled SourceDefinitions frontend checks

This standalone C++11 suite checks `SourceDefinitions::AddCompiledRecord` through
real owning `CompiledDeclarations::Read` and `CompiledDeclarationCatalog` load
units. No compiled packet is treated as an ordinary source `.def` member.

Synthetic fixtures cover qualified 0/0 strings and 4/1 signed-int32 literals,
owning numeric words, raw tuples and spellings, quoted whitespace/escapes, opaque values and parent
barriers, typed/profile conflicts, full missing/cyclic dependency gates, model
grammar, malformed tuples and forged catalog entries, transactional duplicate
refusals, owning document/pool/source provenance after destruction, and input/
result budgets including an unused large record retained by a selected entry.
Opaque index equality is additionally scoped to one immutable source document:
equal tuple tokens from different pool documents require an explicit child
override. Replacing public entry owners/ordinals/spans must not bypass the
catalog's immutable-origin check or the frontend's transaction/budgets.
The initial 0/4 literal profile is extended after separate storage/getter evidence:

- Kind3/aux1 retains inline float32 bits in `numericWords`. Its bounded decimal
  profile accepts signed int32 or unsigned uint32 spellings and keeps the raw
  lexeme. It does not reproduce arbitrary-width lexer overflow.
- Kind1/aux1 retains one pool1 float word as `CompiledFloat32`; aux2..255 retains
  the full checked span as `CompiledFloatArray`. Kind2/aux1..255 retains pool0
  words as `CompiledIntegerArray`. The latter count-span profile is DATA only;
  no multi-int source consumer is claimed from the selected packet.
- Kind4/aux1 keeps its previous signed integer value token and also owns the
  corresponding word. Float/pool numeric results have EMPTY `value`: no decimal
  source tokens are synthesized. Nonfinite float bits remain data.

Known numeric inheritance compares encoding, raw type/aux and exact decoded
words, so different pool indices or documents can coalesce only when their
admitted words agree. Truly opaque equality still requires one immutable
document. Unsupported types/counts/inline spellings remain opaque barriers;
malformed references of an admitted pool profile refuse import atomically.
Numeric vectors are charged before copying in input, facts, resolved fields
and the second animation-reference result phase.
The synthetic suite contains 348 checks in eleven groups. The previous
plaintext and model-animation-reference standalone suites also remain checks
of their separate source profiles; no numeric text equivalence is added there.

The explicit source policies remain data-only. Qualified string/integer value
tokens omit the two metadata tokens, while `origins[].data.values` retains the
original tuple for every encoding. Unsupported typed fields expose no qualified
result value and still shadow a parent's value. Finite-value, vector/matrix,
unit, boolean and numeric/text conversions require separate receiver policies;
gameplay support and native registry/index import are not inferred.

Build this directory independently and run `wolf2009-compiled-definitions
--self-test`. `WOLF2009_MINIZ_SOURCE_DIR` selects an already present pinned miniz
directory; otherwise the test inputs use zlib. No entire-core subdirectory or
unrelated tests are imported. Main CMake/native wiring belongs to the owner.

Optional `WOLF2009_COMPILED_DEFINITIONS_SOURCE_MAP`, or CLI `--source-map
YOUR_TRAINYARD.mpk`, reads the unique exact DECL resource freshly through
`Archive`, then imports all 974 entityDef and 31 model records with a 2048-member
frontend limit. It resolves the selected `container_locker_foot_01` class/model,
signed flags/delay/health, and its model's six animation references (four pure,
two opaque blocks), retaining record-relative and packet/source/pool provenance.
It also resolves selected `weapon_mp40`, checks six kind1/count3 spans against
the fresh packet's pool bytes and three kind3 scalar words. Original tuples,
pool indices and owning pool spans remain available after catalog destruction.
Importing 1005 declarations does not qualify every parent closure, runtime class,
numeric receiver, authored callback or gameplay path. The container's compiled
spawnclass is checked as `rvAnimatedContainer`; the separate env_* source model
declaration is not substituted. No `.md5anim` to `.hkx` bridge is inferred, and
no original member or packet bytes are distributed.

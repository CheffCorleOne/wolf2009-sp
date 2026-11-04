# Encoded whole audio-bank data plan

`SourceAudioBankPlan` is portable C++11 data ownership/admission for the explicit
`SndsOnlyEncodedV1` profile. It contains no native sound/cache, decoder, resource
service, runtime FSM, mixer, VO action or game-clock dependency. A valid plan
means the complete archive data was admitted and retained. It has no Ready,
decoded, registered or playback flag.

The caller supplies a seekable read-only stream, its caller-established exact
virtual filename and a request with the same byte-exact filename. This first
profile accepts a safe ASCII `.spk` filename; it performs no host-path lookup,
locale expansion, fallback or association inference. Optional expected source
size/FNV must match. The caller establishes the filename-to-stream provenance;
FNV identities are local cache keys, not authentication.

Preparation first takes one bounded complete immutable source snapshot. The
existing `Archive::Scan` and `ReadEntry` then operate on that same owned memory:
table and member reads cannot accidentally mix different caller-stream versions.
Their existing zlib/checksum/size/table/alignment/exact-EOF checks remain in use.
Every chunk and entry is processed in source order. There is no selected-member
or silently skipped type path. Empty SNDS chunks remain in the manifest, but an
empty bank, empty encoded member or any non-SNDS raw tag is refused.

`Chunks()` retains each source ordinal, raw FourCC/header bytes, file header/data
offsets, compressed/inflated lengths, compressed-byte FNV and contiguous member
range. `Members()` retains raw type/name, separate normalized lookup name,
chunk/entry ordinals, **inflated-chunk** payload offset, length/FNV and owning
encoded bytes. `SourceBytes()` also retains the complete original package
snapshot, including compressed data, tables and padding. Copies share immutable
ownership; returned references remain valid while a plan copy is retained.

Raw names are unchanged. Duplicate raw names and ASCII-case/slash collisions
are refused across the whole bank, matching the VFS lookup comparison. The
native SNDS transport route additionally checks exact name plus a stripped
`.mp3` alias: a pair such as `sound/x` and `sound/x.mp3` is rejected because that
request would be ambiguous. This is transport-name qualification; member bytes
remain codec-opaque, even when the name ends in `.mp3`. Raw exact lookup performs
no alias rewriting. This policy is deliberately stricter than the generic
archive reader and does not broaden supported native audio formats.

Source, chunk, member, aggregate encoded, name, count and retained-data budgets
are checked. Compressed headers are preflighted before `Archive::Scan` to bound
its aggregate inflation. Before individual reads, a checked charge also covers
each whole-chunk inflation/table parse repeated by `ReadEntry`: the work budget
is the sum of `chunk.inflatedBytes * (1 + chunk.memberCount)`. This prevents a
small retained bank with thousands of tiny entries from authorizing excessive
repeated inflation. A separate bound applies the same accounting to compressed
input bytes: a valid inefficient zlib stream need not have a good compression
ratio. The retained-data charge covers the original snapshot, member
bytes and manifest fields; it is not an allocator/RSS measurement. Temporary
archive inflation is separately bounded by the existing archive limits and
the plan's chunk budget. Input failures throw without publishing a partial
owner or replacing a caller's prior retained plan.

The future core consumer must independently attach strict per-entry decoder and
native cache receipts to **all** enumerated members, with actual byte comparison
in addition to length/FNV. Mount epochs, generation/cancellation, cache leases,
foreign-alias ownership, partial native rollback and eventual whole-bank
completion belong to that separate service. Data admission must never substitute
for any of those results.

The standalone target links only this data plan, the existing archive reader
and zlib or the already installed pinned miniz. It does not change shared root
CMake. With a C++11 host compiler:

```sh
cmake -S tests/source-audio-bank -B build/source-audio-bank \
  -DWOLF2009_MINIZ_SOURCE_DIR=/existing/pinned/miniz
cmake --build build/source-audio-bank --parallel 1
ctest --test-dir build/source-audio-bank --output-on-failure
```

Omit the miniz path to use an installed zlib. For the optional private original
data check, also supply
`-DWOLF2009_ORIGINAL_AUDIO_BANK=/owner/SP/base/streampacks/scripted_voice_kc1_trn_vo_english.spk`.
The test opens that file read-only; no original payload is in the repository.
It checks the independently audited one-chunk/19-member/389672-byte dataset and
prints every source name/length/FNV. These are data checks, with no audio decode.

Validation: C++11 host build with `-Wall -Wextra -Werror` passed; CTest2/2 passed.
Synthetic185 checks/53 refusals cover multiple/empty chunks, nonzero padding,
source and shared ownership, raw identities and offsets, exact filename,
expected-source changes, unknown/late types, duplicate and transport aliases,
empty/late members, corrupt/truncated/zlib-trailing data, complete coverage and
all configured budget classes, exact repeated-inflation work and preflight
refusal of128 oversized-work chunk claims before zlib. The original run covered all19 entries, source
385486 bytes/FNV`c2301fb35aa8f5b2`, encoded total389672 bytes. Native integration
and strict decoding remain outside these checks.

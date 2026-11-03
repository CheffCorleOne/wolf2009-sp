# Compiled declaration transport

`CompiledDeclarations::Read` is a portable C++11 owning transport reader. The
caller must select `CompiledDeclFormat::TwoSectionLfTextV1`; that label is our
explicit admission profile, not a version or magic found in the bytes. The
default `Unspecified` and unknown profiles refuse with a typed error.

The profile consists of two little-endian uint32 count/word arrays, followed by
two LF text sections. A section count is followed by that many groups. A group
contains a nonempty printable-ASCII source-file label, a default type ID and a
record count, each on one LF-terminated line. A record contains a raw ID line,
a byte-length line and exactly that many payload bytes. Numeric metadata is
canonical unsigned decimal uint32: no sign, leading zeroes or whitespace.
Metadata CR, NUL and other nonprintable/non-ASCII bytes refuse. There is no
separator beyond the declared payload length; exact EOF is required.

All spans are half-open source offsets. Text metadata spans exclude LF; binary
pool count spans cover four bytes. All strings, word bits and payloads are owned.
The prefix bits remain opaque. Independent consumer evidence attributes the
group value to a default type; an explicit payload type may override it. The
raw record ID is not imported as a universal type/index. The result retains both
section ordinals through its arrays and computes a local FNV-1a source identity,
not authentication.

Payload bytes may be empty, binary, contain NUL/CR/LF or resemble metadata. This
layer never parses declaration names, commands, property schemas or namespace
precedence. Complete-looking text is not promoted to plain-source declarations
or native gameplay support. Catalog/index qualification belongs to a separate
layer.

Limits cover source bytes, cumulative pool words/groups/records, per-record
payload size, metadata line size and conservative retained output storage.
Counts and minimum possible framing are checked before count-sized allocation
or loops. String/vector storage is charged before allocation, including capacity
allowance; these are not exact allocator/peak-RSS guarantees. A failed read
returns no partial result and never changes caller bytes. `CompiledDeclError`
reports `InvalidData`, `Unsupported` or `LimitExceeded` plus the reader cursor.

Configure this directory independently, optionally using the existing pinned
miniz directory through `WOLF2009_MINIZ_SOURCE_DIR`. No entire-core subdirectory
or unrelated tests are imported. Run `wolf2009-compiled-declarations --self-test`.
Set `WOLF2009_COMPILED_DECL_SOURCE_MAP` or pass `--source-map YOUR_TRAINYARD.mpk`
for the selected read-only original source proof. It locates exactly one
`DECL/maps/game/trainyard/trainyard` resource through `Archive`, checks transport
counts/spans/identities and compares every raw payload to its source byte slice.
No original packet/resource bytes are distributed. Main project CMake/native
integration is outside this standalone module.

`WOLF2009_COMPILED_DECL_SOURCE_BASE=YOUR_SP/base` adds only two further selected
source checks. `_empty` reads the unique `DECL/maps/game/empty/empty` from
`maps/game/empty/empty.mpk`. `_global_decl_absence` freshly scans the selected
`maps/game/global/global.spk` and asserts its recorded zero DECL chunks. The
absence check never invokes this decoder and establishes no runtime namespace
absence. No substitute global artifact or all-map/all-animation sweep is used.
The CLI equivalents are `--source-package PACKAGE EXACT_RESOURCE` and
`--source-decl-absence GLOBAL.spk`. The trainyard `--source-map` option remains.

The three selected-source cases retain separate scope: trainyard transport is
2,112,750 bytes, pools 0/1467, sections 886/111 and 3269 records; empty transport
is 357,867 bytes, pools 0/47, sections 23/37 and 641 records. Both compare all
word bits, metadata spans/values and raw payload spans to freshly extracted
bytes, including exact EOF and independent retained FNV identities. Global SPK
absence is an archive observation, not a third transport-decoding pass.

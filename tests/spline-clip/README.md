# Named HKXA transform data

`SplineClipData::Load(bytes, exactName)` reads a named spline object through
the canonical checked Havok 5.6 packfile reader. It requires unambiguous named
`Animation Container` and `animNames` variants, equal parallel object/name
counts and exact source-name selection. It never invents extensions, folds
case, chooses the first clip or assigns a skeleton from a resource prefix.

Output owns the selected compressed bytes and metadata, source byte count/FNV
identity, raw 128-byte animation/40-byte container headers, pointer/array facts
and independent `SplineTransformInput`. The original buffer can be destroyed.
Per-block transform end is `blockOffset + floatBlockOffset`, checked before
payload allocation. Raw individual-track arrays are preserved, not generated
or treated as sequential decoder addresses. Codec format/scale admission is
separate. No existing Greeter/Skeleton/native/wire files are modified.

Bindings are optional raw facts. Their state is `Absent` or `Uninterpreted`;
sparse tables are not required to match animation count. Known 24-byte binding
records retain raw signed indices, target pointers and unknown blend hints.
Unknown record layouts retain bounded pointer/type/first-word facts. Neither
absence nor any nonempty record establishes an identity mapping, bone order,
additive math, missing-track defaults, rig compatibility or native admission.
Float tracks, extracted motion and annotations remain uninterpreted facts.

The reader is portable C++11 with no OS access. Independent synthetic fixtures
cover malformed names/types/fixups/ranges/budgets, source/result ownership and
binding facts without gameplay promotion. Standalone CMake registers only its
own tests. Set `WOLF2009_SPLINE_CLIP_SOURCE_BASE` to an owner's read-only
`SP/base`; optionally provide the existing `WOLF2009_MINIZ_SOURCE_DIR`.
Source QA selects the three previously checked trainyard clips plus a cached
24-frame two-track locker clip from that same packet. It does not scan all
archives or claim source rig binding, actor behavior or retail clock semantics.

Standalone verification: 91 synthetic checks in six groups and four selected
original clips passed (2/2 CTest). Source clips include the 335-frame two-block
pickup and the 24-frame two-track locker animation. On the selected greeter
times, the maximum translation/quaternion-component errors against the existing
decoder were `7.11e-15` and `2.23e-16`. This establishes reader/independent-codec
compatibility for those inputs, not a general source rig binding.

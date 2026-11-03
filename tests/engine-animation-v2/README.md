# Portable animation wire v2

Native integration and actual runtime results are documented separately in
[V2.md](../../src/idtech4a_anim/V2.md). `EngineAnimationV2Checks.cpp` and
`SourceAnimatorV2Checks.cpp` are opt-in native checks; the standalone project
below builds only the wire fixtures and unchanged v1 control.

Standalone transport/identity checks only. Native registration/provider dispatch,
Havok decoding, mesh/rig compatibility and retail playback are not implemented by
this module. Fixtures own arbitrary payload bytes and 3/7-bone metadata.

Namespace is `wolf2009::animation2`; v1 files and aliases are unchanged. New alias:
`wolf2009/_animation/anim/v2/<16 lowercase hex>.md5anim`. FNV-1a is a local cache
key/byte identity, not authentication. `Same` compares the complete validated
serialized metadata, not the hash.

LE wire order:

1. Eight bytes `W2ANIM2\0`.
2. Ten u32 fields: version 2, visual policy 1, rig policy 1, geometry profile
   (1 weighted64 / 2 weighted-or-rigid64), LOD policy, LOD slot, bones, frames, blocks,
   maxFramesPerBlock.
3. Four IEEE64 values: duration, frameDuration, blockDuration, blockInverseDuration.
4. Four role records, each u64 epoch/byte count/FNV followed by u32-length path:
   MODL, mesh SKEL, animation HKXA, animation SKEL.
5. Four u32-length strings: modelAlias, meshSkeletonName, clipName,
   animationSkeletonName.
6. Two u32-length original payloads: animation HKXA, animation-role SKEL.

Paths must already be canonical typed renderer paths. Object names are exact
printable source text; no case folding, extension invention or path inference.
The two skeleton names may differ even when their container path is the same.
Shared mount labels require equal epochs; one identical source path requires
equal byte identities. The model alias must use its declared mesh-v2 mount/LOD.
Only the producer can verify its actual registered identity/hierarchy.
LOD policy is mandatory: `Authored=1`, or `SingleResourceWithoutLodRecords=2`.
Policy 2 requires slot zero as an explicit compatibility slot; it does not
invent an authored source LOD record. Vertex declaration profile is independent.

Strings are limited to 4096 bytes, metadata to 64 KiB, each source/payload to
64 MiB and the envelope to 128 MiB +64 KiB. Counts admit 1..256 bones, at least
two frames, 1..4096 blocks and 2..256 maxFramesPerBlock. Block coverage uses the
same shared-endpoint domain as the spline codec. Positive finite source timings
must agree within its relative clock tolerance and ceil(duration*1000) must fit
native signed milliseconds. No integer FPS is inferred. Unsupported schema and
policy/profile values fail explicitly; truncated, inconsistent, oversized and
trailing data never fall through to v1.

Configure this directory in an isolated build folder, build with one job and
run CTest. The second target compiles the existing unmodified v1 checks as an
independent compatibility control. Tests cover full field/key roundtrips,
endianness, independent roles/names, shared-label epochs, counts/clocks,
corruption, every prefix truncation, ownership, payload identities and budgets.
No source assets, native engine, device or original game launch is involved.

Verified standalone checkpoint: 918 v2 checks in six groups and the unmodified
377-check v1 control passed (2/2 CTest), built with C++11 and one build job.

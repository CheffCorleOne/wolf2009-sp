# One-root LOD compatibility profile

`SkinnedLodPolicy::RootAuthoredMeshUnspecifiedSlotV1=3` admits an explicit
compatibility selection slot zero. The MODL must contain exactly one authored
root LOD distance, which must be finite and positive. Each mesh either declares
LOD zero or omits that field; at least one mesh must omit it. More root slots,
nonzero mesh markers, absent root records, and a nonzero request slot are refused.

The source root distances and every mesh's marker presence/value remain in
source order through preparation and resident native source information.
`selectionUsesCompatibilitySlot` distinguishes the request from an authored
per-mesh LOD fact. An absent marker is never reported as an authored zero.
This is a caller-selected compatibility policy; original missing-field runtime
semantics have not been proved. In particular, this profile cannot select from
multiple authored root slots.

Existing Authored and SingleResourceWithoutLodRecords policies remain strict.
Their cache domains and old registration signatures are unchanged. Policy three
has its own full-request cache domain, and its value is retained by Animation
WireV2 and source actor requests. It adds no weapon/class/frame-command support.

`wolf2009-root-lod-profile` tests owned three/seven-joint MODLs with independent
weighted64 and rigid64 buffers, mixed marker presence, source colors/UVs, joint
local skinning, profile cache order, bad roots/markers/slots and metadata budgets.
The existing mesh, v1/v2 wire and actor suites remain separate controls.

The optional `--source-base <owner SP/base>` path reads the exact original MP40
detail SPK and global SPK. It compares the prepared bind and fractional poses
for idle/fire/fire2/fire3/fire4/reload against existing source skinning and
checks four-role descriptor roundtrips. No original bytes are embedded here.
This CPU check establishes geometry/rig/descriptor compatibility only. Native
model-manager/animator execution and GPU rendering require separate checks;
source weapon commands, surface visibility, offsets, rates and cross-package
texture binding remain explicit follow-up work.

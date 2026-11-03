# Source player hull and native collision checks

The portable `NativeFloatExactConvexSurfaceV1` policy consumes caller-selected
local binary32 supports, retains every point verbatim, and constructs an
outward, closed triangular surface. Duplicate points, non-surface/interior
points left unused, coplanar volume, inconsistent edge orientation and native
capacity overflow refuse. It never shrinks, displaces or simplifies supports.
The final limits are64 vertices,160 edges and96 triangular faces.

Orientation uses a conservative double filter with a bounded exact dyadic
integer fallback for zero and near-coplanar determinants. IEEE binary32 inputs,
ordinary round-to-nearest evaluation and disabled fast math/contraction are the
supported arithmetic profile. No original hull tables or assets are embedded.
Native planes and edge normals are separately converted through the GPL float
CM interface, with finite/unit/plane-error guards before output is committed.

`SourcePlayerHullTest.cpp` checks tetrahedron, coplanar cube faces, an authored
50-point surface, exact-sign boundaries including subnormals, determinism and
refusals. An optional private source point file supplies two owner-provided
standing/crouching shapes. The file is text: group count2, then each point count
and xyz triples. This is an offline test input, never a runtime dependency.

`WideTraceBitsTest.cpp` compiles the actual CM cache header in C++98. It checks
all161 bits, boundaries31/32/63/64/95/96/127/128/143/144/159/160, independent
clear/set operations, memcpy and invalid-index bounds. The cache stays POD
storage with six32-bit words; no constructor/destructor or scalar wide shifts.

`SourcePlayerHullNativeChecks.cpp` is a game-module QA entry point, called only
after ordinary game/map/CM initialization. It exercises actual `idTraceModel`,
custom hull cache identity, constructor polygon-edge bounds, translations,
contents, contacts and rotation through `idCollisionModelManager`. It uses its
own geometry. Production source-player tests cover the original selected HKXS
shapes and `idPhysics_Player` movement separately.

The engine patch changes the shared trace-model ABI for all translation units
and replaces vertex/edge side caches with bounded multiword operations. The
legacy save record remains32 vertex slots,33 edge slots and16 polygon slots;
wide records refuse before the first record byte. This is not support for
retail saves or source-player save/restore. The patch does not qualify retail
keepDistance, collision filters, controller acceleration or camera behaviour.

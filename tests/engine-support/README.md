# Source support gameplay map checks

`SupportGameplayMap` prepares a complete, owning selected-world support index,
per-shape material/contents responses, and a start-only native map. Its CM has
one empty `worldMap` model: no render bounds, brushes or polygons substitute
for the source geometry. The native bridge binds the index to handle zero.

The explicit `DeclaredDoom3BitsV1` compatibility policy requires complete
geometry, hierarchy, contents, child filters, contact metadata and body coverage.
It refuses additional rigid bodies, trigger contents, unresolved cylinder
response semantics and unknown material mappings. It does not qualify the
original game's runtime filter or automatically spawn mission entities.

The standalone CMake target is `wolf2009-support-gameplay-map`. It reuses the
portable core's zlib/miniz configuration. `--self-test` performs 108 checks over
seven authored groups; `--source-map <owner-supplied MPK>` reads same-archive
HKXS and ENTS without changing the archive. Set `WOLF2009_SUPPORT_SOURCE_MAP`
at configure time to add the latter invocation to CTest.

Actual native integration is exercised by `tests/engine-runtime` with
`-RealEmptyGameRoot`. It checks exact masks/materials, traces, cached identity
and feature reuse, wrong-key refusal, reserved CM paths, live mount leases and
ordinary teardown. A cached source identity includes mount epoch and bounded
source-byte fingerprints; those fingerprints are cache keys, not authentication.
See [runtime instructions](../engine-runtime/README.md).

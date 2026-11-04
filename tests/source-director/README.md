# Ordinary source arrival director

This is a game-module slice, not a viewer or a generic BRAI interpreter.
`WolfSourceMapRequest.arrivalProfile = "MeasuredTrainyardArrivalV1"` explicitly
adds three initially hidden source `func_static` boxcars, their real source CM
geometry, the authored skin where present, a relay receiver, and an ordinary
native Think owner. An empty selector preserves the boot-only checkpoint.
Unknown selected declaration/instance fields refuse before movement starts.

The director reuses `DecodeTrainArrivalH1023` and `TrainArrivalRuntime` for source
graph qualification. Its source registry is a traversal oracle: a source `alive`
flag does not mean a native entity exists. Actual native effects are delivered
through `WolfNativeSourceRegistry`, with exact names and captured `idEntityPtr`
spawn generations. Use never falls through to arbitrary native Activate; each
receiver owns its explicit Check/Use implementation. Event identity includes
map generation, movement event ID and source node ID. Failed attempts are also
guarded against replay; arbitrary receiver callbacks do not promise rollback.

At the actual mover's committed arrival, all three show targets are preflighted
before consuming its once-only arrival. The ordinary native static entities show
and restore `CONTENTS_SOLID`. Their source compound collision is mandatory;
there is no render-bounds or missing-geometry fallback. This response is an
explicit native compatibility profile, not proof of retail filter parity.

The quake currently has no implementation. Its removal records
`ImplementationAbsent`, and the successful native removal count stays zero.
The explicit **partial** profile permits the three measured show effects after
that outcome: the saved causal PC bypass isolated Remove70 and preserved all
three shows. It never advertises a complete source prefix or full mission.

The source node's incoming `train_Stop` and relay's own `train_stop` remain
distinct. The original `rvTarget_BRAINUse` ignores the incoming string and forwards
its own literal key. The explicit ENTS2031-to-2034 profile now prepares one
`TrainDoorUseEvent` with `useKey=train_stop`; this is not case folding. Missing,
duplicate, recursive, extra or other source targets refuse. The original incoming
actor remains generation checked until handoff. The event is only a request;
the consumer must validate its captured native greeter generation before OnUsed.
Receipt/`relayEvents` does not acknowledge actual actor startup. Door/actor
factories, VO readiness and later Wait/cleanup/console nodes are not linked here.
Future actor start and door activation must use the same generation/event and
actual animator ACK in the same native game-clock boundary.
The inherited Use actor-notification semantics also remain outside this slice;
own-key request creation is not complete class/gameplay parity.

Root integration adds `NativeSourceRegistry.cpp`, `NativeSourceDirector.cpp`,
core `TrainArrival.cpp`, and the native checks to its game build. The session
descriptor needs an explicit selector; this branch does not modify shared
CMake, upstream patches, SourceSession or the Android host.

Native checks provided (not automatically enabled by production):

- `Wolf2009_RunSourceRegistryChecks`: independently authored native fixtures;
  wrong generations/keys, missing source versus missing implementation,
  exact removal, native slot reuse, conflicting event payload, exception/replay,
  Check-to-Inspect/Deliver reentry and restored guards after preflight exceptions.
- `Wolf2009_RunTrainyardDirectorChecks`: caller selects and assembles this
  original map profile first. Drives ordinary RunFrame, tests pre-arrival hide,
  real native show/solid response, partial outcomes, no Query effects, arrival
  consumption and no replay. Does not test full mission, actor or door playback.

The portable `SourceRelayTest.cpp` uses the same header-only qualification and
one-shot route helper as the native receiver. It tests unrelated incoming keys
(which cannot be repaired by case folding), exact own-key output, unchanged source
facts, request identities, one-shot consumption and explicit target refusals.
Build its standalone CMake target with one job; it needs no engine or game assets.
Portable CMake/Ninja/CTest passed 1/1 (36 checks); the changed native Map/Director
and QA translation units passed NDK syntax. Native gameplay execution of this
new handoff remains an integration check.

No proprietary data is stored in these tests. Build/runtime validation is a
separate required integration step; source inspection alone is not a PASS.

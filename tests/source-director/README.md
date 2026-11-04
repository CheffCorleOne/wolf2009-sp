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
distinct. Without a qualified relay key rule, receipt is `UnsupportedOperation`
and `TakeRelayRequest` returns no event. An exact admitted receipt can hand off a
`TrainDoorUseEvent`; it is not an actor-start acknowledgement. Door/actor
factories, VO readiness and later Wait/cleanup/console nodes are not linked here.
Future actor start and door activation must use the same generation/event and
actual animator ACK in the same native game-clock boundary.

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

No proprietary data is stored in these tests. Build/runtime validation is a
separate required integration step; source inspection alone is not a PASS.

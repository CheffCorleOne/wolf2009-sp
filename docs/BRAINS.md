# BRAI graphs

`src/wolf2009/BrainGraph` is a portable C++11 structural decoder shared with
the narrow startup handler. It reads NUL-delimited token records, leading-tab
nesting, node IDs and output targets. The observed `h1023/32/32/32/0` and
`g1023/32/32/32` envelopes are explicit formats. Duplicate IDs, unresolved
targets, missing parents, truncation and exceeded limits cause rejection.

The decoded representation retains every record, byte range, parent, token,
node and output. Empty strings and legacy 8-bit text are preserved. Output
flags and nested condition fields remain preserved. A parsed output is not
an instruction to execute its destination unconditionally.
Cycles and branches are valid structures and are preserved.

## Coverage

The native decoder passed the owner's 633 BRAI resources under `SP/base/maps`
without extracting or changing them. These contain 36,735 records, 1,502,214
tokens, 14,147 nodes, 13,158 outputs and 154 distinct node types. There are
632 `h1023` resources and one `g1023`. All outputs resolved within their own
graph. This measures structural coverage, not gameplay compatibility.

The fixed output record has 99 tokens: flags, a separately retained opaque
header, 32 destination triples and a final weight-decay value. Each triple
contains node ID, selector weight and an auxiliary field. The retail loader
creates links only for positive signed node IDs. `BrainOutput::destinations`
now resolves all populated slots, including sparse forward references;
`targetNode` remains the first populated destination for old consumers.
Other record lengths retain a partial projection and explicitly report
`destinationsComplete=false`.

The 633-resource corpus contains 13,757 destinations, with 205 multi-target
outputs and 599 links in secondary slots. Those links were previously retained
only in raw tokens. All destinations resolve; none was invented to repair a
graph. Raw record bytes, tokens and slot positions remain available.

## Common transition selection

`BrainFlow` adds an independently written portable C++ selector based on
focused static inspection of the owner's `Gamex86.dll`, SHA-256
`7cb1cbedec4b24e612fddf815ce96bbad3f79dff6160767bd778d77fc9382d47`.
The inspected loader, dispatcher and condition/selection procedures establish:

- Output conditions are short-circuit AND. A depth-two `-1` resolves to the
  owning task's primary condition; it is not an unconditional true flag.
- The normal pass considers outputs in source order and stops at the first
  successful transition. Probe-only, disabled and processed one-shot outputs
  follow their separate eligibility flags.
- One destination is chosen by an integer weighted selector with a supplied
  15-bit random sample and a strict cumulative upper bound. Multiple zero
  weights become ones. Raw destination auxiliaries remain opaque.
- The timer predicate compares `now > activeTaskStart + duration`; duration
  `-1` never elapses before optional inversion. Clock units, timer arming and
  expression resolution remain the caller's responsibility.

The selector supports the measured normal pass, unit weight decay and a bounded
weight sum. Other activation modes, decay values, unknown conditions, missing
required context and timer overflow return explicit unsupported results.
Callers supply condition evaluation and randomness and own task activation,
reset, event delivery and entity side effects. The graph must stay alive and
immutable for its selector state. This is common flow machinery, not a full
task network, mission interpreter or AI implementation.

The selector/IR admission check passed all 633 original resources using private
hash-checked fixtures. It does not imply that their conditions or task actions
have been executed. Synthetic tests exercise weighted boundaries, all-zero
weights, ordered short-circuiting, primary-condition binding, one-shot state,
explicit unsupported cases and strict timer boundaries.

The default per-resource limits are 4 MiB source data, 20,000 records,
500,000 tokens, 1,024 bytes per token and depth 64. Synthetic tests cover
branches, cycles, forward references, nested conditions, unknown bytes,
empty tokens, both envelopes, malformed topology and each limit.

## Common normal Task lifecycle

`BrainTask` adds a generic C++11 runtime around `BrainFlow`: initialization,
root entry, active-task transitions, reset/bind hooks, conditions, integer
clock, bounded transition loops and a single pending Use-key latch. Selection
still uses the existing checked output selector. Only explicitly admitted
normal, non-yielding task/condition implementations are allowed. Probe/stack
activation, root-as-destination activation, unknown flags, duration expressions and missing host behavior
return unsupported. Reset preserves host condition state and does not repeat
initialization. Timer sampling and strict expiry boundaries have independent
helpers. Use keys compare ASCII case-insensitively; a newer notification
replaces the pending key. Source-root exit hooks run after the selected target
has entered, which matters for consuming the Use latch.

Focused inspection of the same hash-identified Gamex86.dll covered initialize
`0x201dc590`, normal activation `0x201dd3f0`, Tick `0x201df480` and reset
`0x201df7a0`. Proprietary implementations and binary bytes are not included.

The host owns entity lookup, task actions, external predicates, condition
state and randomness. Callbacks are synchronous. Recursive execution and
callback exceptions poison the instance with a terminal unsupported result;
already applied host effects are not rolled back. Subsequent ticks/reset do
not resume those effects. Raw graph data is never modified.

Eleven synthetic test groups cover entry/exit order, shared primary condition
references, repeat/once state, strict timers, Use consumption, reset, bounded
cycles, invalid profiles and callback reentry/exceptions. Two original graphs
also replayed through an explicitly declared **TestHost**: warehouse door
Wait50/startup/keyed-Use and tavern bartender Wait500/near-condition loop.
The host supplied external truth and recorded variable/action calls. These
checks use real BRAI data with the new kernel; they are not observations of
the original runtime and do not establish native door, animation or mission
behavior. Native engine entity/action bindings remain to be implemented.

The portable CMake build includes `wolf2009-brain-task --self-test` and its
CTest. `--fixture PATH` runs only the two declared private replay profiles;
commercial fixture bytes are not included in the repository.

## Verified action boundaries

Targeted static inspection of the same identified Gamex86.dll also established
two action boundaries for the next native host. Type68 `UseTarget` Enter
(`0x201ed440`) resolves actor with owner fallback, then target, then the key,
and performs a synchronous virtual Use call. The verified ScriptRunner
receiver checks CanUseKey before changing its latch; Tick is separate.
Type220 `SetGlobalVariable` Enter (`0x201e75c0`) expands the expression string,
then the variable name, evaluates a signed int32 and writes the shared game
dictionary. Dictionary initialization imports `globalVars` defaults.

These are static findings, not native action implementations. The first
proposed admission is Me/exact literal names and keys plus bounded decimal
integer values. General expressions, arbitrary Use receivers, clock units and
cross-map/save variable lifetime remain unqualified. A TestHost with an empty
dictionary does not reproduce the original global defaults or their lifetime.

## Reproduce

The ordinary portable CMake build includes `wolf2009-brains` and its CTest
self-test, plus `wolf2009-brain-flow --self-test` and
`wolf2009-brain-task --self-test`. The Windows Zig build script also builds
the structural inspection executable.

```powershell
.\build\native\wolf2009-brains.exe --self-test
$packages = Get-ChildItem 'C:\path\to\Wolfenstein\SP\base\maps' -Recurse -File |
    Where-Object { $_.Extension -in '.mpk', '.spk' } |
    Select-Object -ExpandProperty FullName
.\build\native\wolf2009-brains.exe @packages
```

No game assets are included. `BrainStartup` still accepts only its established
direct OnSpawn → MoveToEnt profile with default output fields and a sentinel.
It deliberately refuses branches or unknown conditions. The existing mover,
arrival and door handlers do not yet constitute a general interpreter. The
next step is to attach independently verified action/condition handlers to
this common representation and compare behavior across missions.

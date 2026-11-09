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
normal, non-yielding task/condition implementations are allowed. Under the
default `NormalV1` profile, probe/stack activation, root-as-destination
activation, unknown flags, duration expressions and missing host behavior
return unsupported. Probe outputs run only under the opt-in
`ProbeLeafPassV1` profile described below. Reset preserves host condition state and does not repeat
initialization. Timer sampling and strict expiry boundaries have independent
helpers. Use keys compare ASCII case-insensitively; a newer notification
replaces the pending key. Source-root exit hooks run after the selected target
has entered, which matters for consuming the Use latch.

Hosts can declare a root priority. Roots enter and dispatch in ascending
priority with graph order preserved for ties. Ordinary task Initialize and
Bind hooks retain sparse ID order. The literal native host uses the observed
descriptor priorities for OnSpawn (10) and OnUsed (30). The portable
regression reverses their order in the graph and confirms that a pending Use
survives OnSpawn and is consumed on the matching OnUsed Exit.
[Original dispatch evidence](AI-ACTOR.md#root-order-on-the-first-think-static).

The BrainTask self-test passed 19 groups. The native literal-host regression
passed on Android 15 x86_64 in run `5db43b860188` on the authored
`wolf2009/runtime` fixture, with zero live tasks and globals at teardown.
The reverse-order test is portable. The original trainyard actors and their
networks remain the next native stage.

Focused inspection of the same hash-identified Gamex86.dll covered initialize
`0x201dc590`, normal activation `0x201dd3f0`, Tick `0x201df480`, reset
`0x201df7a0` and the probe procedures listed below. Proprietary implementations and binary bytes are not included.

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
behavior. The bounded native literal host below is a separate implementation;
those two original TestHost replays do not become native mission replays.

### Probe outputs: `ProbeLeafPassV1`

A runtime constructed with `BrainTaskProfile::ProbeLeafPassV1` adds the probe
pass for output flag `0x20`. It follows the static contract in
[TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §3.1, which was rechecked against the
assembly of Tick `0x201df480`, eligibility `0x201ddb60`, task pass
`0x201ddbd0`, flush `0x201ddaf0`/`0x201dda90`, output fire `0x201dda10` and
probe activation `0x201dd760`:

- Each loop iteration runs the normal pass first. It then marks the probe
  outputs of the current active task, including a task entered in that
  iteration. The flush follows: active task, then roots. The loop repeats
  while a normal transition happened. Without a transition, the probe pass and
  flush still run once per Tick, so a probe whose conditions become true later
  fires on that Tick.
- `BrainFlow` `MarkBrainProbeOutputs` evaluates the AND conditions of every
  eligible probe output in source order and marks each passing output
  `0x40000`. It does not stop at the first one and draws no random sample.
  Eligibility is checked in this order: disabled (`0x100000`), no destination,
  processed (`0x80000`) together with once or probe, then pass kind. A
  processed probe output therefore stays ineligible until its source re-enters.
  All conditions of a pass are evaluated before any marked output fires.
- `FireBrainMarkedProbeOutput` clears the marker, runs the common weighted
  destination selection shared with the normal pass and marks the output
  processed.
- Probe entry sets task flags `0x20` and `0x8000` and clears `0x10` and `0x40`.
  It clears repeatable processed state of the task and its outputs, enters
  output conditions in reverse order and enters the task unless it is a
  processed once task. The active task, its start time and the current task are
  unchanged; no Exit is called. The host sees the probe source as
  `activeNode` and `0x20` in `taskFlags`. A probe entry adds to
  `BrainTaskResult::probes`, not to `transitions` or the transition budget.

The profile is deliberately leaf-only. Before any host hook, it refuses:

- a probe destination that has outputs of its own;
- a probe destination that is also the destination of a normal output;
- probe outputs on root tasks (the inspected Tick never probe-passes a root);
- a destination for which the host did not set `probeLeafEntryKnown`;
- serialized probe output flags other than `0x20` and `0x21` (stack `0x40`,
  disabled, runtime markers and combinations with `0x10`).

Serialized task flag `0x20` is admitted only under this profile. It is saved
probe-entered state, retained as raw flags and never used as activation
semantics. **Unknown and therefore refused or not modeled:** Exit or cleanup
of probe-entered tasks, normal entry of a task that was probe-entered, root
probe outputs, the `0x40` stack mode, and the network byte that firing a
destination with a per-task byte set raises (Tick returns early while it is
set); host admission must keep such tasks out, as in the normal profile. The profile is not a general probe implementation. In the
five trainyard chain graphs every probe destination is a leaf and none is a
normal destination; durable effects started by a probe Enter belong to the
host subsystem that owns them.

Seven synthetic groups cover probe order with several probes and a normal
transition in the same Tick (the node 35 output shape), late probe conditions
without active-task or start-time changes, condition timing, once and re-arm on
source re-entry, every refusal above, profile-off equivalence and probe-entry
reentry, exceptions and permitted `NotifyUse`. Two further `BrainFlow` groups
test marking, flushing, weighted selection and flag refusal directly.

Two private original trainyard graphs replay through a declared probe
**TestHost**. It treats every task as an opaque recorded action and every
condition as scripted external truth:

- `train_greeter`: spawn probes 7/61/68/21 from node 2. After `train_stop`
  and scripted VO truth for node 29, one Tick runs 29 → 35, probes 6/33/72,
  normal 35 → 10 and probe 58 from node 10. The probe entries keep node 35
  or 10 as active task and start time 200 and are never exited.
- `nazi_interragator`: spawn probes from nodes 2 and 38. After
  `player_leaves_train`, the chain 43 → 21 → 22 → 25 fires probes 42/41/40 and
  26/33/35. These do not fire again while node 25 waits. With scripted
  AnimFinished truth, 25 → 17 → 6 → 7 fires 16/46/47 and 10.

These replays exercise the kernel order on original data. They are not
observations of the original runtime and do not implement AI, VO, animation,
HUD, mission or kill behavior. No native host uses this profile yet.

The portable CMake build includes `wolf2009-brain-task --self-test` and its
CTest. `--fixture PATH` runs only the two declared private replay profiles;
`--probe-fixture greeter|interrogator PATH` runs the two declared probe
replays. Commercial fixture bytes are not included in the repository.

## Verified action boundaries

Targeted static inspection of the same identified Gamex86.dll also established
two action boundaries for the next native host. Type68 `UseTarget` Enter
(`0x201ed440`) resolves actor with owner fallback, then target, then the key,
and performs a synchronous virtual Use call. The verified ScriptRunner
receiver checks CanUseKey before changing its latch; Tick is separate.
Type220 `SetGlobalVariable` Enter (`0x201e75c0`) expands the expression string,
then the variable name, evaluates a signed int32 and writes the shared game
dictionary. Dictionary initialization imports `globalVars` defaults.

These static findings define the bounded native action admission below:
Me/exact literal names and keys plus canonical nonnegative decimal int32
integer values. General expressions, arbitrary Use receivers, native source-clock
integration and cross-map/save variable lifetime remain unqualified. A TestHost with an empty
dictionary does not reproduce the original global defaults or their lifetime.

## Measured retail clock

Focused static analysis of the identified Gamex86/Wolf2 pair establishes the
millisecond unit and its actual producer. Session dispatch at `10095631` calls
game Frame `20076240`; the common interface supplies `1000 / com_userCommandHz`
milliseconds per tic. Metadata defaults to60Hz; the actual installed setting
was not observed. A frame can carry a batch of `numTics` rather than one tic.

The primary clock accumulates float32 deltas, including batching and a local
scale, then truncates the float to integer member `20682ca8`. Brain Network
`201df4d1` and Timer `201e7f62` read that integer. The old setter `200839f0`
belongs to `idGameEdit`; it is not the ordinary frame producer. Repeatedly
adding floor(1000/Hz) does not reproduce this clock. Even six individual60Hz
updates and one six-tic batch can differ at an integer boundary; this is an
arithmetic consequence, not an observed runtime trace.

Static code also establishes local scaling, `g_stopTime`, a separate Session
pause gate, conditional reset and serialization of float clock state. The
controllers choosing those scales, exact UI pause policy, map-preserve policy
and original runtime scheduling still require attribution. Native Save/Restore
cannot preserve this state by storing only an integer timestamp.

## Native literal host

`EngineBrainTask` and the registered GPL entities `idWolfBrainTask` /
`idWolfBrainGlobals` run the common kernel inside the actual game module.
Source BRAI paths are typed and tied to the declared mount; a separate explicit
diagnostic namespace contains independently authored public test graphs.
The host admits OnSpawn/OnUsed/Wait/UseTarget/SetGlobalVariable with the known
normal lifecycle and primary Exists/Used/Timer conditions. Other handlers,
unknown host/graph fields, expressions and binding profiles refuse admission.

An explicit provider owns a mandatory initialized int32 defaults dictionary.
All declared members pass group preflight before any new runtime is built or
entered. Binding runs after spawn, and Think supplies the declared caller
clock to Tick. Owner/provider/actor/target references use native generation IDs;
deleted objects and reused names/slots cannot silently rebind a session.
Keyed Use checks the admitted receiver's CanUseKey gate before its latch and
never calls Tick recursively or substitutes ordinary GPL EV_Activate.

The Android x86_64 game-module check executed an authored cross-entity
OnSpawn → strict32 timer → Use → SetGlobalVariable chain. The receiver observed
the selected actor and wrote7 into shared defaults; sender wrote1. Mixed-case
valid Use followed by an unknown key preserved the pending valid input.
Reset preserved globals, same-slot replacement did not inherit the deleted
receiver's binding, and unsupported groups produced no actions. Tasks and
providers were absent after explicit test teardown and normal engine shutdown.

This host currently declares diagnostic millisecond time and explicit provider
scope. Retail float-clock sequencing and pause/reset integration, `globalVars`
decoding, campaign/global save lifetime, arbitrary receivers and the expression
interpreter remain incomplete.
Native Save/Restore refuses this unsupported state rather than fabricating it.
Native action availability does not imply original door/animation/mission support.

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

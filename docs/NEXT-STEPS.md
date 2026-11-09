# Next steps

The current target is to continue trainyard after the wagon arrives. Player,
MP40, arrival, actor and door components have separate validation; the complete
mission and campaign are still unfinished. See [CHECKPOINT](CHECKPOINT.md) for the
current runtime result and [DEVELOPMENT](DEVELOPMENT.md) for the workflow.

## 1. Register a complete native SNDS bank (component passed)

Branch `work/audio-ready-20261108` integrates the data plan and the strict
receipt into an opt-in native bank service (`WOLF2009_SOURCE_AUDIO_BANK`).
Run `9941b7d722f0` on Android 15 x86_64 registered all 19 members of the
English scripted-voice pack with 19 full native decode receipts and exact
cache identities. It refused unknown types, corrupt/truncated data, name
ambiguity, a non-MP3 member and one shortened member as whole banks, and
tested retained mount/pin ownership, retry, stale generations, purge loss and
teardown. [Profile and results](../tests/source-audio-bank/README.md).

Still open: the opt-in is not in an APK; other packs (for example the
interrogator banks) and OpenAL hardware-buffer configurations are not
qualified; physical ARM64 is untested.

## 2. Give type19 an actual readiness result (component passed)

`src/idtech4a_gameplay/NativeScriptedVOReadiness.h` binds `ScriptedVOStreamIn`
to the retained bank, its raw definition, locale and owner generation, with
distinct Pending, Ready, Failed, Cancelled and Lost states. The same run's
game-module probe passed 49 checks: once-only Ready for a live owner, no
progress from frames alone, cancellation, owner end and slot reuse, and loss
when the cache purge ended the backing lifetime.

The trainyard greeter route now consumes it through a thin adapter (see 3).
This result does not claim speech.

## 3. Connect greeter use, animation and door (stops at node 10)

Done in the real trainyard session (branch `work/greeter-chain-20261108`,
[TRAINYARD](TRAINYARD.md)): the relay route into greeter OnUsed with the
relay's own key, a uniquely resolved greeter and door, and the node 29 request.
With the audio-bank opt-in, node 29 consumes one whole-bank Ready, the route
enters node 35, and it holds node 10 GoToAndFaceTarget with an explicit
refusal. Cancel, bank loss and stale owners before and after Ready, missing
targets and normal teardown were exercised.

Still open: node 35 only stores the move-start animation. The door's
`triggerStartMoveEnt` is inferred to fire from node 10's move start, which needs
native AI locomotion. Acceptance for the remainder: node 35 probes fire as
leaves; a real native node 10 move start plays the stored animation and
activates the bound door once under event/generation/tick checks; the door
endpoint and end-idle are observed once in this session. The actor owner's
direct PlayOnce is not an accepted substitute unless shown to be the original
contract.
## 4. Reach the exit trigger and first combat

Follow the next original trigger/event chain from the train into the first
encounter. Add the shared entity, AI/navigation, weapon or damage handlers that
the measured source behavior requires.

Acceptance: the player reaches the encounter through normal movement and can
fight it with native weapons, damage, death and recovery. Record the next
unsupported event explicitly; do not skip it or grant unearned progression.

## 5. Save, reload and widen mission coverage

Restore player, entity/brain, inventory and resource ownership without duplicate
events or stale references. Re-enter and tear down the map repeatedly. Then
apply the same handlers to the next mission and qualify its source differences.

A campaign claim needs tested progression across missions and transitions.
Physical Android ARM64 validation also remains necessary; emulator success
does not establish device coverage. iOS and WebAssembly follow the shared
engine work and their own platform validation.

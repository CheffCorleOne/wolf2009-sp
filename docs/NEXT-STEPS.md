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

Still open: no brain host consumes it yet. The relay/greeter host must replace
any caller-supplied readiness boolean with this provider before node 29 can
advance in the real chain. This result does not claim speech.

## 3. Connect greeter use, animation and door

Resume the original relay route into greeter `OnUsed`, then `AnimStart` and the
bound door. Preserve the relay's own key and use the actor-issued start ACK.

Acceptance: source targets resolve uniquely; real native animation start
precedes door activation under the required event/generation/tick checks.
Observe the door endpoint and end-idle transition once. Exercise cancellation,
missing/stale targets and normal teardown. Separate actor/door QA results do
not establish that the complete automatic chain runs.

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

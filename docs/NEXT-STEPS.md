# Next steps

The current target is to continue trainyard after the wagon arrives. Player,
MP40, arrival, actor and door components have separate validation; the complete
mission and campaign are still unfinished. See [CHECKPOINT](CHECKPOINT.md) for the
current runtime result and [DEVELOPMENT](DEVELOPMENT.md) for the workflow.

## 1. Register a complete native SNDS bank

Use the explicit English scripted-voice pack. The data plan enumerates its
19 unique SNDS members and owns their encoded bytes; it makes no decode or
readiness claim. The strict MP3 receipt leaf at `950c8d1` is frozen on the
separate `work/audio-receipt-20261104` branch. The data plan is on
`work/audio-bank-20261104` at `be6b8c4`. Native-cache runtime validation and
whole-bank registration are still pending.

Acceptance: every supported member gets a full native decode receipt and exact
cache registration. Unknown types, corrupt/truncated data, name ambiguity or
one failed member refuse the whole bank. Retain source/mount/cache ownership;
test partial failure, retry, stale generations and teardown. A selected sample
or successful archive read cannot complete this gate.

## 2. Give type19 an actual readiness result

Bind `ScriptedVOStreamIn` to that retained bank and its source definition,
locale and owner. Keep pending, failed, cancelled and lost states distinct.

Acceptance: readiness follows successful whole-bank work, arrives once for a
live owner, and is invalidated when its backing lifetime ends. No timer or
caller-supplied boolean advances node 29. This result does not claim speech.

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

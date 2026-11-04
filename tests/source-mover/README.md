# Source direct linear native mover

`NativeSourceMover` prepares exact ENTS/BRAI input with the existing verified
direct OnSpawn1→MoveToEnt132 reader. It retains the complete owning documents,
raw authored placement/bind, startup IDs and all unexecuted-node/output counts.
The source effective entityDef's remaining class behavior is a separate caller
admission; this adapter does not pass source dictionaries to Doom scripts.

Preparation performs no Spawn or movement. Native Spawn requires an already
qualified, callback-owned, non-world CM model alias. The base `idMover::Spawn`
needs that clip before subclass Spawn, so `noclipmodel` without an explicit
qualified CM alias is not accepted. The native subclass retains the prepared
data and uses real `idPhysics_Parametric`/native pusher contacts. It has no
per-frame mover or passenger position correction. A qualified visual model or
an explicit no-visual diagnostic policy is required. Raw nonempty bind needs
an independently established caller world pose; no source parent is guessed.
An authored skin requires an explicit parsed native skin alias even in the
no-visual diagnostic. Native pointer/text consistency and the entity's actual
customSkin are checked. The caller keeps the qualified source skin/catalog/
render binding alive until the mover is removed; source and native skin names
remain observable separately. An unrequested skin on an unskinned source is
also refused.

The level owner delivers OnSpawn after assembly. Native `MoveToPos` supplies
ordinary motion and physics-frame quantization. `DoneMoving` commits one
arrival; a separate exactly-once handoff exposes its retained source node.
The base completion's exact local endpoint is also published to the native
absolute pose immediately, after verifying the final sampled pose is within
0.01 game units; this preserves the base endpoint correction without waiting
another physics tick before the source arrival. This is not per-frame movement.
Later graph outputs are not automatically treated as executed. Identity
failure stops native motion. Native clips are unlinked before source collision
leases close. Source save/restore is explicitly unimplemented.
Generic Activate is refused by this narrow source profile instead of inheriting
the stock mover's unrelated Activate-to-spline behavior.

The compound CM pusher uses flags0, as ordinary GPL `idMover` does. Native
actor contacts and blocked pushing work against that geometry. `PUSHFL_CLIP`
would require a convex trace model and is not selected: the mover's own compound
shape is not swept against the static world. Retail motion clock, crushing,
Havok response and complete source class behavior are not claimed.
Native-float zero/overflow speeds and nonzero moves shorter than one native
millisecond refuse before Spawn; the base mover otherwise truncates their time
to zero before physics-frame rounding. The maximum admitted path is one hour.

`prepare-fixture.mjs --output <isolated base>` writes independently authored
ENTS and two BRAI files only. `NativeSourceMoverChecks.cpp` runs inside the game
module after the caller preloads its own normal native CM fixture. It checks
inactive construction, explicit/repeated spawn events, two independent movers,
source speed/endpoints, real player transport, arrival handoff, stale-owner
halt and entity/source-owner cleanup. The loose fixture policy is available
only with `WOLF2009_ANIMATION_V2_QA`; production requests use typed source VFS.
These fixtures do not replace collision for an original game entity.

Game compilation also requires the existing `BrainStartup.cpp`, `LinearMover.cpp`,
`BrainGraph.cpp` and `Entities.cpp`. Shared CMake and runtime invocation are owned
by the parent integration task. Syntax success alone is not native runtime proof.

Actual Android15 x86_64 source-session run `b1eef2682ddf` passed146 checks over
204 frames with the source player and two independently authored movers,
including carry, repeated OnSpawn, endpoint/arrival and stale-owner cleanup.
Original trainyard run `9463fe061801` separately passed the ordinary source
map startup and source wagon ride, including its original model/skin and
86-shape body, actual game-camera contribution and map shutdown. These are
native compatibility results; they do not establish retail Havok parity or
execution of the remaining mission graph.

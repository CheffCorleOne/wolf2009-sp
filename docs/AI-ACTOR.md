# AI actor subsystem (`idAI`)

Reviewed 2026-11-09. This page records what the hash-identified retail
`Gamex86.dll` (`7cb1cbed…2d47`) does when trainyard and mtw brains drive an
actor: spawn and lifecycle, scripted animation, speech, scripted locomotion and
the combat entry points. It maps each contract to the GPL idTech4 game code in
the pinned engine and gives a staged native plan. It ranks as items 4, 5, 6
and 8 in [CAMPAIGN-RE](CAMPAIGN-RE.md) §6.2.
Stage 1 and the interrogator chain in §8 are implemented and observed
natively. The rest is a contract and work plan. For what runs, see
[CHECKPOINT](CHECKPOINT.md).

Labels follow [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md):

- **Static**: read from the disassembly of the identified DLL, with the call
  site or data table that establishes it. No execution was observed.
- **Data**: read from original ENTS, BRAI or `.def` text.
- **Inferred**: consistent with Static and Data facts, not closed.
- **Unknown**: no adequate evidence yet.

Names such as `ScriptedAnim` or `StopScripted` below are descriptive labels
chosen for this page. Where the DLL carries its own debug string (for example
`idAI::Show`, `idAI::UpdateAnimation`, `idAI::AttackHitscan`,
`idAI::BrainSpawn`), the page says so. A label is not a recovered signature.
Argument roles come from push order and from the brain property tables, not
from decompiler names. Offsets are virtual-table byte offsets (`+0x468`) or
object-field offsets (`ai+0x3820`) in this build only.

Method: the Android NDK `llvm-objdump` text of a private copy of the DLL,
annotated by private scripts with string, RTTI type-descriptor, vftable and
event-definition names. No game process, compiler, debugger or new Ghidra
import was used. Raw disassembly and scripts stay in the ignored
`evidence/ai-actor/`.

## 1. Summary

| Area | Contract | Status |
|---|---|---|
| Class and registration | `idAI` type info: Spawn `0x20157d00`, post-spawn `0x2015d820`, state table, 100-entry event table; 20 RTTI subclasses, 15 spawned | Static |
| Hidden spawn | Base PostSpawn dispatches Hide when the spawn arg is true. AI PostSpawn shows only actors with false `hide`. Hide saves and clears think flags, so a hidden AI's brain does not tick | Static |
| Activate | `idAI` activate virtual `+0x368`: marks activated, ignores dead AI, honors `maxActiveAI`, unhides the AI and its team chain; a visible AI instead forwards the activator's `useKey` to its brains | Static |
| Use | `noUnhideOnUse` with a non-thinking AI ignores Use entirely; otherwise brains are notified, then Activate runs | Static |
| Brain binding | `brain` spawn arg loads one network at `ai+0x9f4`; extra networks in a list; ticked only from `idAI` Think while thinking | Static |
| Scripted animation | Animate → `ScriptedAnim` virtual `+0x468`; AnimFinished polls animator done-ness with blend frames at 24 frames/s | Static |
| Animation overrides | Idle/death/move-start overrides are strings in the AI; move start is consumed once by an action check during animation update | Static |
| SetScriptedManual | Enters/leaves a scripted-manual state; entering shows a hidden AI unless an internal flag is set | Static |
| Frame commands | Model frame commands are parsed to type codes. `trigger` posts `activate` on the named entity; `speak` posts the actor's `speak` event | Static (dispatch); event bodies partly Unknown |
| Speech | Say → actor `Speak` virtual `+0x3ac`; line key resolved through the actor's spawn args; random `lipcount_` variants; SayFinished compares the current line | Static; sample and lip-sync binding partly Unknown |
| Scripted locomotion | GoToAndFaceTarget → scripted move `0x201ac2b0` (move command, face target, movement style bits); Exit stops it; per-frame move update `+0x4dc` calls the AAS route sensor | Static (interface only) |
| Hunt | Sets the enemy through the awareness list and the AI manager; the task ends at Enter | Static |
| Hitscan | Attack `+0x620` dispatches hitscan to `idAI::AttackHitscan` `+0x628`, which calls the shared game hitscan once per `num_projectiles` | Static (entry); damage model Unknown |
| Pain/death/combat roots | HasTakenPain, CombatJustStarted, EnemyDead read actor state; death animation override precedence; **Aware calls a `return false` body** | Static |

## 2. Object model

### 2.1 Classes (Static)

`idAI` derives `idActor < rvAFEntity < idAnimatedEntity < idEntity < idClass`.
Its vftable has 503 slots; `idActor` has 261, so `idAI`-specific virtuals start
at `+0x414`. RTTI names 20 subclasses. The 15 that ENTS spawns
([CAMPAIGN-RE](CAMPAIGN-RE.md) §4) are:

| Class | Entities, first map | Overrides vs `idAI` (of note) |
|---|---|---|
| `idAI` | 3276, trainyard | — |
| `rvMonsterSniffer` | 120, mtw | 17 (Think, Show) |
| `rvMonster_Scribe` | 69, mtw | 15 |
| `rvGeistSwarm` | 44, digsite | 18 (Think, Hide, Show, move update) |
| `rvMonsterAKHeavyTrooper` | 40, mtw | 34 (Think, Hide, Show, death anim, move start) |
| `rvMonsterEliteGuard` | 23, mte | 54 (via `rvMonsterIntelligentMeleeBase`) |
| `rvMonsterAKRocketTrooper` | 20, downtown | 22 |
| `rvMonsterAKAssassin` | 16, hospital | 31 (Hide, Show, Speak) |
| `rvMonsterAKFlameTrooper` | 10, cannery | 29 |
| `rvMonsterRevenant` | 10, church | 63 |
| `rvMonsterAltered` | 3, hospital | 34 (Attack) |
| `rvMonsterHGArmor` | 1, blacksun | 32 (Use) |
| `rvMonsterWalker` | 1, cannery | 25 (Hide, Show, Attack) |
| `rvMonsterGeistQueenBlackSun/Ground/Hybrid` | 1 each, castle_top | 11 / 18 / 15 |

Every spawned subclass overrides slot `+0x218`, the kill/damage entry that
KillTarget uses for non-AI entities. Trainyard spawns only plain `idAI`.

### 2.2 Registration and Raven-style states (Static)

The `idAI` type-info constructor call (`0x20183ac0`) passes, in order: class
and superclass names, the event table `0x205be330`, instance factory
`0x20183370`, Spawn `0x20157d00`, a state table starting with `State_Idle`,
save `0x20150ab0`, restore `0x2015dc50` and a post-spawn method `0x2015d820`.
`idActor` and most entity classes share the base post-spawn `0x200588e0`;
`idAI` and `idPlayer` override it.

The AI runs named states through a state thread: `ScriptedAnim` posts
`State_ScriptedAnim` and `State_ScriptedStop`; scripted manual posts
`Wait_ScriptedManual`; the initial state comes from virtual `+0x76c`. Behaviors
such as the move start are *actions* evaluated through a shared action helper
(`0x2015f950`). This architecture differs from the Doom 3 script-driven `idAI`
in the pinned engine (see §7).

The event table maps 100 events, including `activate`, `use`, `speak`,
`speak_deferred`, `scriptedAnim`, `scriptedMove`, `scriptedDone`,
`interactiveRagdoll`, `<kill>`, `attack`, `hideWeapon` and `unbindBrain`. Event
names come from the `idEventDef` static initializers.

### 2.3 Field map used below (Static, this build)

| Field | Meaning (from its readers and writers) |
|---|---|
| `ent+0x6c` | spawn args dictionary |
| `ent+0xb4` / `+0xb8` | think flags / flags saved by Hide; bit `0x1` gates Think, bit `0x10` gates brain ticks |
| `ent+0x158` | entity flags: `0x1` notarget, `0x8` takes damage, `0x10` hidden |
| `ai+0x954` | AI state: `0x4` dead, `0x8` activated, `0x20` scripted move, `0x40` scripted manual |
| `ai+0x9f4`, `+0x9fc`/`+0xa08` | bound brain network; count/array of extra networks |
| `ai+0x11b5` | scripted-move style bits (movement type, ignore clusters `0x40`, end in crouch `0x8`) |
| `ai+0x11b1` bit `0x20` | movement disabled (EnableMovement) |
| `ai+0x37b4`, `+0x3820`, `+0x388c` | idle, move-start and death animation override strings |
| `ai+0x38d4` | scripted animation index |
| `ai+0x3b68` | move-start animation index chosen by the move-start check |
| `actor+0x710`, `+0x734`, `+0x758`–`+0x760` | current speech line and sound name, start time, end time |
| `actor+0x6de` | speech requested but not yet confirmed playing |

## 3. Spawn and lifecycle

### 3.1 Spawn (Static)

`idActor` Spawn (`0x2000c290`) reads team, rank, FOVs, `undying`, `nospeak`,
`noPainWhileSpeaking`, footsteps, blink, `def_ai_voice`,
`def_scripted_voice`/`def_scripted_voice2` with their `detailPack`, and the
head (`idActor::SetupHead`). `idAI` Spawn (`0x20157d00`) then reads about 150
keys, among them `overrideIdleAnim`, `ragdollOnDeath`, `playDeathAnim`,
`removeOnDeath`, `removeInstantlyOnDeath`, `no_drops`,
`disableAnnouncements`, `canQuickKill`, `hideWeapon`, `disableCover`,
`ignoreGrenades`, `def_weapon`, `def_hitscanOverride`, `nopain`, `noDamage`,
`notarget`, `minRemoveDelay`/`maxRemoveDelay`, `speakStopRange` (default 100)
and `overrideRunAnim`/`overrideWalkAnim`. It ends by entering the initial
state, calling `idAI::BrainSpawn` and the swarm spawn. The trainyard
interrogator keys in [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §4 are all read
here.

### 3.2 Hidden spawn and Show (Static)

Map load calls the generation-checked pending-entity drain at `0x20077986`.
The normal spawn branch of that drain (`0x2006efe0`) calls the PostSpawn
wrapper `0x202afe70` at `0x2006f04d`. Generic PostSpawn (`0x202afbf0`)
recurses through the superclass type at `idTypeInfo+0x2c` first, then runs
the current class's distinct PostSpawn callback at `idTypeInfo+0x18`.

Base `idEntity` PostSpawn (`0x200588e0`) reads `hide` and, if true, calls
virtual Hide (`+0xfc` at `0x20058920`). The dictionary lookup
(`0x2040d3a0`, comparator `0x204084f0`) folds ASCII case, so it recognizes
the two interrogators' and two hostages' authored `Hide 1`. The `idAI`
vtable `0x20501e74`
dispatches that call to `0x2014e7d0`. Its actor Hide chain reaches
`idEntity::Hide` (`0x20047970`), which sets the hidden flag, saves think
flags from `+0xb4` to `+0xb8` and deactivates the entity
(`0x20047a12`–`0x20047a27`). The subsequent `idAI` PostSpawn
(`0x2015d820`) calls virtual Show only when `hide` is false
(`0x2015d861`–`0x2015d86d`). It also clears `ai+0x955` bit `0x2` and
handles `leader`, `drivingGoal`, `triggerStartMoveEnt` and `giveMission<N>`.

`idEntity::Show` restores the saved think flags. `idAI::Think` (`+0x3c`,
`0x2014d790`) requires think bit `0x1`, and the brain tick wrapper
`0x201aba80` requires bit `0x10`. Thus after the base callback has hidden
an AI, the normal Think path reaches its brain only after Show restores
those flags. The dispatch and flag writes come from static analysis.
The first live flag values and the frame of interrogator 1's first brain
tick have yet to be observed on the original runtime.

### 3.3 Activate and Use (Static)

`idAI` overrides the activate virtual `+0x368` (`0x201ad110`; the `activate`
event calls it):

1. Set activated (`ai+0x954|0x8`). Return if dead.
2. If spawn arg `maxActiveAI` is nonzero and smaller than the live active-AI
   count, return.
3. Run the `idActor` part: if the activator is an `rvSpawner`, register with it.
4. If hidden: Show the AI, then Show every member of its team chain.
5. Otherwise, if the activator has a `useKey` spawn arg, deliver that key to
   the AI's brains (the notify below).

The hidden branch calls Think directly only with `g_stopTime` set. The checks
at `0x201ad182` and `0x201ad1b0` read the cvar through `0x205dc174`.
Its shipped default is `0`, which leaves Think to the normal frame loop.

`idAI` Use (`0x201ac870`): when `noUnhideOnUse` is true **and** think bit
`0x10` is clear, return without any effect. Otherwise notify the brains with
(actor, key), then call the base Use, which processes `activate` and reaches
step 1. The notify (`0x201ac7d0`) records the time when the local player is the
user. The bound network and each extra network each apply `CanUseKey` before
notification. The checks are at `0x201ac806` and `0x201ac842`, respectively.
This identifies the internal flag in [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §3.2
as think bit `0x10`.

#### Root order on the first Think (Static)

The parser reads the task-type descriptor table at `0x205d4708`
(`0x202001bb`), stores the descriptor at task offset `+0x10`
(`0x201e06ae`) and adds root tasks to the network's root list
(`0x202003e6`–`0x202003f1`). The insertion procedure `0x201dcaf0`
compares signed descriptor fields at `+0x2c` and inserts before the first
larger nonnegative priority (`0x201dcc80`–`0x201dcc8e`,
`0x201dce90`–`0x201dceba`). Roots with equal nonnegative priorities retain
their insertion order. OnSpawn (type 1, descriptor `0x205d3d10`) has
priority 10. OnUsed (type 3, descriptor `0x205d3d78`) has priority 30.

The dispatcher at `0x201ddce0` scans the list forward and returns after the
first successful root transition. Tick (`0x201df480`) disables further root
scans for the rest of that network Tick. First-time initialization
(`0x201dc590`) preserves the pending Use. It enters roots through the
same sorted list after initializing all tasks in sparse ID order
(`0x201dc671`–`0x201dc777`). A matching repeatable OnUsed Exit
(`0x201e6ee0`) clears it after its transition.

If the interrogator's first Think finds both roots eligible, it selects
OnSpawn `1 → 11` before OnUsed `12 → 43` for `player_leaves_train`.
The pending key survives the OnSpawn transition and is available at the next
eligible root scan. The comparison establishes precedence given both roots
are eligible. Whether E4 actually presents both ready roots in the original
runtime remains unobserved.

### 3.4 Brain binding (Static)

`idAI::BrainSpawn` (`0x201ac9d0`) frees any previous network, reads `brain`,
loads it into `ai+0x9f4`, reads `brainIsPostBind` and reports
`Unable to load brain '%s' on entity '%s'` on failure. `idAI::BrainBind` and
`idAI::BrainSet` (debug strings) maintain the extra-network list. Tasks reach
other actors only through property resolution (`mActor`, `mSpeaker`,
`mHunter`). Trainyard interrogator 2 has no `brain` key; interrogator 1's graph
drives it. The `unbindBrain` event exists. Tick order inside Think: brain
networks tick after the early think checks and before the move update `+0x4dc`
(Static, `0x2014d790`).

### 3.5 Death, kill and removal (Static unless marked)

- KillTarget (69, `0x201ed6b0`): for an `idAI` it calls the `<kill>` event body
  (`0x20184e50`) with `mBypassDeathAnim`; other entities get virtual `+0x218`.
- Death animation (`+0x69c`, `0x2016b1d0`): the AnimOverrideDeath string
  (`ai+0x388c`) wins, then spawn arg `deathAnimOverride`, then a computed
  directional choice.
- AnimOverrideDeath (57) stores `mDeathAnim` only. With E5b/E6 this means the
  hostages die through their override clips when KillTarget fires at Animate
  entry (Inferred, consistent with Data).
- SetUndying (253) writes `actor+0x5f9`; the player has a separate path.
- StartRemovalAttempt (403): unless the owning network is in a refused state, it
  stores `mInitialTime` (`ai+0x984`), `mOutOfSightTime` (`ai+0x988`) and a
  pending flag (`ai+0x98c`). A per-frame check (`0x2014d230`) compares them
  with time; out-of-view logic and the removal call are Inferred from names.
- `removeOnDeath`, `ragdollOnDeath` and corpse limits are Spawn keys; their
  runtime use was not traced (Unknown).

## 4. Scripted animation

### 4.1 Property layout (Static)

Brain property records are 0x34 bytes: type, name, description, default,
related property, field offset. Embedded conditions put their own properties
at the condition's offset. Animate (51) embeds AnimFinished at `+0x4c`:
`mAnimName`, `mActor`, `mBlendFrames` (default 2), `mAnimSucceeded`,
`mAnimFailTime`; then `mNumLoops` (default 0) and `mEndWithIdle` (default
true).

### 4.2 Animate and AnimFinished (Static)

Animate Enter (`0x201f5290`):

1. `mAnimSucceeded = false`; `mAnimFailTime = now + 1`.
2. For an `idAI`: `ScriptedAnim(name, blendFrames, numLoops, endWithIdle)`
   (virtual `+0x468`, `0x201ac4d0`); the result becomes `mAnimSucceeded`.
3. For an `rvVehicle` or `idAnimated`: look the clip up by name; if found, set
   `mAnimSucceeded` and play it on channel 4 at rate 1, or cycle it `mNumLoops`
   times.

`ScriptedAnim`: gate through virtual `+0x458` (`endWithIdle` argument); look
up the clip (`0x20017300`); a missing clip reports through `+0x350` and
returns false. Otherwise store the index (`ai+0x38d4`), play it once or cycle
it, and, unless the AI is in a vehicle, stop movement and post
`State_ScriptedAnim` then `State_ScriptedStop`.

AnimFinished Evaluate (`0x201ebe70`), then XOR with the condition's invert bit:

- `mAnimSucceeded` false → true once `now > mAnimFailTime`, so a failed
  Animate completes on the next later tick.
- `idAI` → `now > mAnimFailTime` **and** the actor is not playing `mAnimName`
  or its remaining time is at most `blendFrames × 1000 / 24` ms
  (`0x200174a0`/`0x200174e0`, the animator's remaining-time query).
- vehicle/`idAnimated` → the same remaining-time test without the name check.
- no resolvable entity → true.

Completion therefore comes from the animator's own clip time, not from a
timer. The test is the same as Doom 3 `idAnimState::AnimDone` with
`FRAME2MS(blendFrames)`.

### 4.3 Overrides, manual mode, watch and lock (Static)

| Task | Effect |
|---|---|
| AnimOverrideIdle 54 | `ai+0x37b4 = mAnimName` (empty resets) and marks the idle stale (`ai+0x95b|0x80`) |
| AnimOverrideMove/Turn 55/56 | store walk/run and left/right turn names |
| AnimOverrideDeath 57 | `ai+0x388c = mDeathAnim` |
| AnimOverrideMoveStart 59 | `ai+0x3820 = mAnim`; nothing plays |
| SetScriptedManual 33 | see below |
| EnableMovement 99 | `ai+0x11b1` bit `0x20` = !`mEnable` |
| WatchTarget 52 | arms its timer; head-only look at `mWatchTarget` (or turn toward it when `mTurnTowards`); `mTurnAngleOverride` → `ai+0x1250` |
| LockLocation 97 | `mLocation` names an `aasLocationEntity` → lock; otherwise unlock |

**Move start (closes the open item in [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md)
§1).** Virtual `+0x6c4` returns the override when non-empty, otherwise a
default name built from the movement style (for example a `_dash` form). The
move-start check `0x201c7330` runs only while a move start is pending
(`ai+0x11b7` bit `0x10`), the AI is not dead, in a vehicle or in a blocking
mode, and movement is enabled. It resolves the name to a clip, stores it in
`ai+0x3b68`, **clears the override string** and returns true. The action
helper then plays it. The check runs inside the leg-locomotion state machine,
virtual `+0x704` (`0x20166d00`), which `idAI::UpdateAnimation` calls
(Static). That state machine sets the pending bit when it leaves its idle state
for a move. In its moving state it first tries a second start action
(`0x201c8780`), then this check, and clears the bit when one of them runs.
**Inferred:** `srp_tyd_nml_train_door_open` plays once, as the start of the
next move that leaves idle, which is greeter node 10.

**SetScriptedManual(true)** (`0x201ac600`): no-op if dead or already manual.
If the AI is hidden and `ai+0x955` bit `0x2` is clear, **Show it**. Set manual
and announcement-off flags, clear look-for-enemy and the enemy, stop movement
and post `Wait_ScriptedManual` plus the initial state. **(false)** clears the
flags and restores `disableAnnouncements` and `idleLookForEnemy` from spawn
args. **Inferred:** because a hidden AI does not tick, an AI's own graph cannot
call this while hidden; another graph can (interrogator 1 probes it on
interrogator 2 and the hostages).

### 4.4 Frame commands (Static)

Two parsers map frame-command names to type codes. Parser `0x204003c0` gives
`speak` 0x45 and `interactive_ragdoll` 0x70. Dispatcher `0x203fe5d0`
(called from `0x20403980` and `0x20403b40`) switches on those codes:

- `trigger <name>`: find the entity; post `activate` with the owner as
  activator. A missing entity is reported, not skipped silently.
- `speak <line>`: post the owner's `speak` event (line, 1). A neighbouring
  code posts `speak_deferred`.
- `interactive_ragdoll` / `no_interactive_ragdoll`: post the corresponding
  owner events.
- A second dispatcher `0x200268d0` (parser `0x200277a0`) also implements
  `trigger`, `trigger_alias` and `light_fade`.

Which dispatcher serves trainyard HKXA clips, and the frame-crossing rule
(each frame once per pass, including loops), are **Unknown**. Bodies of the
`speak` (`0x201832d0`) and `interactiveRagdoll` events were not read.

## 5. Speech

### 5.1 Say and SayFinished (Static)

Say (12) embeds SayFinished at `+0x4c`: `mLine`, `mSpeaker`, `mVOsucceeded`,
`mFailDelay` (default 1000 ms); then `mDebugText`, `mUseSubtitle`,
`mClearFocus`, `mUseOcclusion`, `mSuppressSubtitle`.

Say Enter (`0x201f5e90`) resolves the speaker as **`idActor`**, not only
`idAI`. It stops current speech, then calls the actor's `Speak` virtual
`+0x3ac` with the line, a variant flag of 1, `mFailDelay`,
`!mSuppressSubtitle`, two empty strings, `mClearFocus` and `mUseOcclusion`.
The result is `mVOsucceeded`. For an `idAI` it also sets the scripted-speech
flags `ai+0x3748 = 1`, `ai+0x374c = 0`. `mUseSubtitle` and `mDebugText` are
not read at Enter.

`idActor::Speak` (`0x20005ce0`):

1. Refuse while `nospeak`-like state `actor+0x6dd` is set or the line is
   empty.
2. **Variant:** if the variant flag is set and spawn arg `lipcount_<line>` is
   greater than 1, pick r uniformly in [0, count) with the game LCG; r > 0
   appends `r+1` (`line` → `line2`, `line3`, …). Data: `lipcount_brainAnnounce 4`
   on `trainyard_erik_engle`.
3. **Resolve:** the (variant) line is a key in the actor's spawn args; its value
   is lower-cased and a trailing `_stream` is stripped. Data:
   `scripted_voice_wi1_trn` maps `lipsync_trn_1007_wi1` to
   `trn_1007_wi1_stream`.
4. Missing value: speech end = now + `mFailDelay`; warn
   `Missing vo '%s' on '%s'`; return false.
5. Otherwise record line, sound name, start time; play the sound on the speaker
   entity (`actor+0x658`) or the actor's own voice channel; set the end time to
   start + sound length; mark `actor+0x6de` pending.

SayFinished (`0x201ec1e0`): if `mVOsucceeded` is false, true once
`now > condition start + mFailDelay`. Otherwise false while the actor's
current line equals `mLine` and it is speaking or the request is pending;
true in every other case, including a missing speaker. "Speaking" is
`now < end time`, or a still-playing speaker entity, or the actor's voice
channel playing (`0x200037c0`). A line replaced by another Say therefore
finishes the first SayFinished.

**Inferred:** the actor's spawn args contain the `def_scripted_voice` and
`def_ai_voice` keys, because Speak looks only in `actor+0x6c`. The merge in
Spawn was not read.

### 5.2 Stop, stream-out and speakers (Static)

- StopSpeaking (15): actor `+0x3b0` with `mStopAnims`, `mFadeOutTime`.
- ScriptedVOStreamOut (490): `mDefName`, else the owner's
  `def_scripted_voice`, released through `0x201976c0`. Invalid owner/def pairs
  are reported. It is the counterpart of the type 19 stream-in already
  implemented.
- EnableSpeaker (499): resolves an `idSound`; enable starts it, disable stops
  it with `mFadeOut`.
- SayInterrupted (496) and the speak-stop-range roots read
  `speakStopRange` state; their bodies were not read.

### 5.3 Link to the repository's VO readiness

`src/wolf2009/ScriptedVoice*` resolve `lipsync_<name>` → `<name>_stream` →
localized sample; `SourceAudioBankNative` and `NativeScriptedVOReadiness`
gate type 19 on a whole-bank Ready. The original adds two behaviors that this
code does not have yet: the `lipcount_` variant choice and the `_stream` strip
before playback. **Unknown:** how the stripped name selects the SNDS member
(whether `_stream` is re-appended or a stream flag is set), the role of the
global speech interface (`0x205e6780`) and lip-sync. The head entity
(`actor+0x4d0`) is queried during Speak and `idAI` Speak references
`lipsync_fall`. That is the extent of the lip-sync evidence.

## 6. Locomotion and combat entry points

### 6.1 GoToAndFaceTarget (Static)

CAMPAIGN-RE attributed the `idAI` cast and `+0x45c`/`+0x460` calls to Enter
`0x201f1420`. They belong to the adjacent **Exit** `0x201f15d0`.

Properties: embedded ReachedAndFacing at `+0x4c` (`mActor`, `mLocation`,
`mFaceTarget`, `mRange` default 20, `mInternalPos`), then `mMovement` (Walk 0,
Run 1, CombatWalk 2, CombatRun 3, StealthWalk 4, StealthRun 5, Dash 6),
`mIgnoreClusters`, `mEndInCrouch`.

Enter: resolve the AI, the goal and the face entity (missing ones are
reported), call the scripted move `0x201ac2b0(goal, face, range, …)`, then
write `ai+0x11b5`: ignore clusters `0x40`, end in crouch `0x8`, movement
style bits (Walk `0x2`, Run `0x1`, CombatWalk `0x12`, CombatRun `0x11`,
StealthWalk `0x22`, StealthRun `0x21`, Dash `0x5`).

Scripted move: gate `+0x458`; clear the previous scripted command (`+0x46c`);
an `aasLocationEntity` goal uses the location move (`0x201980d0`); any other
goal uses the entity move (`0x2019a720`). Failure calls the scripted stop
`+0x45c`. Success sets scripted move (`ai+0x954|0x20`), resets style bits and
stores the face target (`0x2019a340`). The entity move takes an `aasPathNode`
position or the goal origin. When the AI is already within range (2D distance
and a height tolerance), it posts `reached` on the goal and stops. Otherwise it
issues the generic move `+0x520` (`0x2019d620`, command 6, goal, range). The
move start is armed later by the leg-locomotion state machine (§4.3), not
here.

Exit: `+0x45c` then `+0x460(1)`. A normal transition out of node 10 stops the
scripted move. A probe-entered GoTo has no Exit and continues
([TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §3.1).

**Per-frame consumer:** idAI Think → move update `+0x4dc` (`0x2019f660`). It
calls `rvAASRouteSensor::Search` (`0x20136940`, two sites) and
`rvAASRouteSensor::HandleAnimTranslation` (`0x20133760`). Tactical queries go
through `rvAASTacticalSensorLocal::Search`. **AAS boundary (interface only):**
the actor supplies goal, range, travel flags (ignore clusters) and movement
style; the route sensor returns path and animation-translation data. Field
layouts and the routing algorithm belong to the AAS 1.40 work and are not
recorded here. Reached/ReachedAndFacing evaluation was not read in this pass.

### 6.2 Hunt (Static)

Hunt (43): embedded EnemyDead at `+0x4c`, then `mPrey` (default `MyEnemy`),
`mLocked`, `mUpdatePosition`. Enter stops any scripted move (`+0x45c`,
`+0x460(1)`) and calls `0x201aa390(prey, locked, updatePosition, 1)`. That
procedure:

1. Ignore a null prey or a dead AI.
2. Get or create the awareness entry for the prey (`0x201aa270`).
3. Register with the global AI manager (`0x2078cf60`) at level 2.
4. Post `announce` if there was no enemy and the entry is visible.
5. Update the last known position when `mUpdatePosition` is set.
6. Set the enemy (`+0x5a8`) and the lock bit, and arm timers of 5000 or 500 ms.

The task does nothing else. Combat continues in the AI's own states.
TRAINYARD-CHAIN's "Hunt continuation" is therefore the AI combat state machine,
not the network.

### 6.3 Attack and hitscan (Static entry, Unknown model)

The attack virtual `+0x620` (`0x20173e50`, which handles melee, bayonet and
grenade names) dispatches hitscan attacks to `idAI::AttackHitscan` `+0x628`
(`0x20179190`). It reads `num_projectiles` and calls the shared game hitscan
`0x2007e2f0` once per projectile. Its nine other callers include
`rvTKWeapon`, the `HitScanAttack`/`SoundHitScanAttack`/`MeleeAttack`/
`GraspableAttack` attack classes, `rvFighterPlane` and a `def_hitscan` user.
Whether the player's weapons use it was not established. **Unknown:**
the trigger of AI attacks (state machine versus frame command), aim/spread,
`attack_base_hitscan` and `def_hitscanOverride` handling, and whether the
native player MP40 path matches `0x2007e2f0`.

### 6.4 Pain, death and combat roots (Static)

| Condition | Reads |
|---|---|
| HasTakenPain 104 | actor pain time `actor+0x5ec` against its range properties |
| CombatJustStarted 108 | current enemy (`+0x384`) and the time it was acquired (`0x201a4740`) |
| EnemyDead 42 | enemy state through `0x201a64a0` |
| IsDead 41 | resolves the entity twice; the dead test was not read |
| **Aware 103** | calls a body that returns false (shared `xor al,al; ret`); for an `idAI` it is true only when inverted |

**Aware, Static with a caveat:** in this build the Aware condition cannot report
awareness. OnAware roots (27 uses, first in trainyard `nazi_patrol`) have a
non-default Initialize (`0x201fee30`). They may be raised another way. Do not
implement Aware as "AI is alert" without that root's path.

## 7. Engine mapping (pinned `engine/doom3/neo/game`)

| Original contract | GPL idTech4 facility | New work |
|---|---|---|
| Hidden spawn, Show/Hide, think gating | `idEntity::Hide/Show`, `BecomeActive/BecomeInactive`, `idAI::Hide/Show` | `idAI` post-spawn rule; brain tick gated by think flags |
| Activate/Use | `idAI::Activate`, `Event_Activate`, `idEntity` Use | `maxActiveAI`, team-chain show, `useKey` forwarding, `noUnhideOnUse` |
| Brain binding | none (Doom 3 uses `idThread` scripts) | Bind `BrainTask` instances per AI; extra networks; Use notify |
| Scripted anim / AnimFinished | `idActor::GetAnim`, `idAnimState::PlayAnim/CycleAnim/AnimDone`, `idAnimator` | `ScriptedAnim` state, fail-time rule, name check; source HKXA clips through existing bindings |
| Overrides | `idActor::Event_OverrideAnim`, `Event_SetAnimPrefix` | Named override strings; one-shot move start consumed by an action |
| Frame commands | `idAnim::AddFrameCommand`, `CallFrameCommands` (`FC_TRIGGER`, `FC_SOUND_VOICE`) | `speak` → actor speech, `interactive_ragdoll` events, Wolf type codes; source frame-command data |
| Speech | `idEntity::StartSound` on `SND_CHANNEL_VOICE`, `idActor` head | `Speak` resolution, `lipcount_` variants, `_stream` strip, end time, SayFinished line rule, whole-bank VO gate |
| Scripted move | `idAI::MoveToEntity/MoveToPosition/FaceEntity/TurnToward/ReachedPos/StopMove`, `idAAS` routing | Movement-style bits, move start, `reached` event, AAS 1.40 adapter behind the route-sensor interface |
| Hunt / enemy | `idAI::SetEnemy`, `UpdateEnemyPosition` | Awareness entries and AI manager levels |
| Hitscan | `idAI::CreateProjectile`, `TriggerWeaponEffects`; player hitscan in this port | AI hitscan through the shared hitscan; source damage defs |
| Death | `idAI::Killed`, `idActor` death anims, AF ragdoll | Death override precedence; KillTarget with bypass flag |

The pinned engine's `idAI` is Doom 3's script-driven class (`ai_*.script`,
`AI_ACTIVATED`, `idThread`). The original is a native state machine with
named states and actions. **Recommendation:** keep Doom 3's entity,
animator, physics and AAS-routing primitives. Write a new source-qualified
`WolfSourceAI` behavior layer on `idActor` instead of driving Doom 3's AI
scripts. The pinned upstream also carries Raven's Quake 4 SDK game code
(`quake4/`, `raven/`, `mod/quake4/*/ai`: `rvAIManager`, tactical AAS, AI
movement), the closest public relative of this lineage. It may be reused or
adapted under its own license, marked and recorded as [AGENTS](../AGENTS.md)
requires. Each reused piece must still match the original's measured
contract; shared ancestry is not evidence of identical behavior.

## 8. Staged native plan

Each stage is an explicit source profile with named refusals for everything
outside it. Acceptance means native observations on the original trainyard
map, plus portable replays where noted.

### Stage 1 — `SourceAIShellV1` (recommended first)

Scope: spawn the two interrogators, both hostages and the greeter as
source-qualified actors from compiled entityDefs. Reuse the native source
model and animation owners for body and head and qualify their joint and
attachment bindings separately. Apply the §3.2 hide rule,
think-flag gating of brain ticks, §3.3 Activate/Use and §3.4 brain binding.
Show (154), RemoveTarget (70) and KillTarget (69) on AI may stay explicit
refusals.

Acceptance:
- `hide 1` actors stay hidden and their networks do not tick, measured by
  zero task entries until shown.
- `trigger_once_1` or the E4 relay shows each interrogator once; a second
  Activate does not re-show or duplicate events.
- A Use with `noUnhideOnUse` on a non-thinking AI changes nothing.
- A visible AI activated by a relay with `useKey` latches that key once.
- Teardown and map reload leave zero actors, networks and pending keys.

#### Trainyard asset and attachment contracts

The selected compiled declarations and original ENTS identify these
body and head pairs. The full-detail meshes live in the named detail SPKs.

| ENTS name | Body model definition | Head definition | Detail SPK |
|---|---|---|---|
| `nazi_interragator_1`, `nazi_interragator_2` | `model_new_wehrmacht_infantry_v2` | `heads_enemy_02` | `wehrmacht_infantry_mp40.spk` |
| `trainyard_hostage_1` | `model_civilian_male_02` | `heads_abm_agt02` | `allies_kreisau_agt01.spk` |
| `trainyard_hostage_2`, `train_greeter` | `model_civilian_male_04` | `heads_civilian_male_03` | `allies_kreisau_agt02.spk` |

The global SKEL member contains distinct
52-joint `EmptyMale` and `HumanoidMale` skeletons. The trainyard SKEL member
contains the head skeletons, with 32 joints for enemy head 02 and 27
for each of the other two. Resolve model and animation skeletons by
their exact roles.

`idActor::SetupHead` (`0x20007700`) reads `def_head` and `joint_head`.
All five actors resolve `joint_head` to `chest`. The source head maps
its `copy_joint` values to same-name body joints unless the body supplies
a `copy_joint <name>` remap (`0x2008eb20`). The `rvHead` controller
(`0x2008df50`, virtual slot `+0x304`) invokes the base controller, then
copies the selected joint transforms through `0x2008cf10`. Exact native
body/head pose sampling and the observed motion-phase difference still
need a runtime check.

The non-player Spawn path sets up the head before choosing accessories
(`0x2000b621`–`0x2000b66a`). The ordinary `def_attach<N>` loop at
`0x2000bb45` spawns contiguous entries. The separate hat group applies
`attach_hat_chance` and chooses among contiguous hat entries
(`0x2000b66c`–`0x2000bade`). Hostage 1 inherits the default chance of 1
and declares one hat, so the draw selects that hat. The ordinary holster
entry has no chance gate on this path. The attachment reader
(`0x200092a0`) uses the child's joint, angles, origin and orientation.
The binder (`0x20008910`) selects the separate head for a `head` joint
when a live head exists. The helmet and hat therefore use the head rig.
The holster and pouch use the body's `chest` and `hips`. It binds before
applying the local axis and origin (`0x20008d62`–`0x20008dd9`).
Android 15 x86_64 run `a9fd9bd4a72b` passed 11,353 checks over 1,184 frames
on original trainyard. The engine assembled five bodies, five separate heads
and six accessories. The QA checked chest binds, three copied joints per
head, the accessories' world transforms and ten native mesh snapshots.
Both interrogators woke once through player contact with `trigger_once_1`.
The arrival relay latched `train_stop`, and the exit relay delivered
`player_leaves_train` to the greeter and first interrogator. Both hostages
stayed hidden until the QA called the same native Activate entry point.
Map shutdown released all shell owners, actors, networks and pending keys.

The shell presents the original mesh bind poses. Scripted clips belong to
the later animation stage. The common teeth
material falls back because its original diffuse is in the global pack
while the head models load from the character packs. The greeter holds
OnSpawn node 2 (type 49). The first interrogator's network runs through the
profile below. [Reproduce the native check](../tests/source-ai-shell/README.md).

#### Interrogator chain — `SourceInterrogatorChainV1`

The first interrogator's own network runs through the held stepper
`SourceHeldStepV1` (`src/wolf2009/BrainActions.*`). It follows the Tick order
of §3.3 and [BRAINS](BRAINS.md): root scan by priority, normal pass, then
probe pass and flush of the active task. Before it commits a normal
transition, it asks the host to admit the destination and every probe
destination of that destination. A refusal holds the network at its current
task, with no exit, flow-state change or side effect. The handlers live in
`src/idtech4a_gameplay/NativeSourceBrainActions.*` under the per-type
profiles of `SourceBrainActionsV1`, so other actors and maps can reuse them.

Contracts recovered for this profile (Static, this build):

| Node type | Enter | Native effect |
|---|---|---|
| 19 ScriptedVOStreamIn | `0x201f6560`: resolve `mOwner` (`+0x6c`); a missing owner gives the warning `Can't find ent`; a non-empty `mDefName` goes to the VO manager stream-in with (def, owner), then return | `ScriptedVOStreamInWholeBankV1` request for (`mDefName`, owner body). The network does **not** wait: outputs 11→20 and 20→2 carry no condition. Each Think updates the request. Its Ready is consumed once, and the bank inventory is checked against the measured pack (wi1 11, wi2 9 members) |
| 33 SetScriptedManual | `0x201f7190`: cast `mActor` (`+0x4c`, default `Me`) to `idAI`; bool `mSetToTrue` (`+0x88`, default `true`); call `0x201ac600` | §4.3. Show runs only when the AI is hidden and `ai+0x955` bit `0x2` is clear. idAI Spawn sets that bit (`0x20158694`) and idAI PostSpawn clears it first (`0x2015d828`), so it marks the Spawn→PostSpawn window. Entering also sets the ignore-enemies bit `0x4` of `+0x1c84` and `+0x1e14` (the IgnoreEnemiesSet bit). Leaving restores `disableAnnouncements` and `idleLookForEnemy` from spawn args (default `"0"`) |
| 54 AnimOverrideIdle | `0x201f5930`: cast `mActor` (`+0x70`) to `idAI`; `ai+0x37b4 = mAnimName` (`+0x4c`, default `test`); `ai+0x95b \|= 0x80` | Stored on the actor and marked stale |
| 57 AnimOverrideDeath | `0x201f5c60`: cast `mActor` (`+0x4c`) to `idAI`; `ai+0x388c = mDeathAnim` (`+0x88`, empty default) | Stored only |
| 102 NoTargetSet | `0x201f2d70` ([BRAIN-TASKS](BRAIN-TASKS.md)) | Native `fl.notarget` on the body or player |
| 253 SetUndying | `0x201f2fa0`: cast `mActor` (`+0x4c`) to `idActor`; byte `+0x5f9 = mEnable` (`+0x88`). For an `idPlayer` (type-range check) it evaluates the bool again and writes the same byte | Recorded; no native damage consumer |

**Idle override consumer (Static).** The leg state machine `0x20166d00`
(vslot `+0x704`) returns early for a dead AI, a non-zero `ai+0x3b60` or
`ai+0x95c` bit `0x1`. Its idle branch also sets the stale bit when
`0x20016d10(4)` or vslot `+0x3f4` returns true; those bodies were not read.
It continues only while `ai+0x955` bit `0x1` is clear. It then clears the
stale bit and takes the name from vslot `+0x6a8` (`0x20168550`). That
function returns the override when it is non-empty and otherwise builds a
default idle name. The name is copied to `ai+0x3790` and played through
`+0x6ac` and `+0x328` unless the actor already plays it.

The native actor consumes a stale override in its Think, after the brain
tick. It records the current idle and resolves the name against the actor's
declared model animations, in the `model_trainyard_base` map context. All
four start idles resolve. Clip playback stays an explicit refusal because
this actor profile presents the reference pose; playback belongs to stage 2.
The default-name builder for an empty override was not read, so a reset is
recorded as a refusal.

Observed on original trainyard (Android 15 x86_64):

1. In the first Think after the `trigger_once_1` wake, the network enters
   11, 20, 2 (probes 3, 23, 24, 29, 30, 31, 32, 44, 45) and 38 (probes 39,
   36, 37): 4 transitions and 12 probes. Probes 23 and 24 show both hidden
   hostages through SetScriptedManual. This is a Show, not an Activate, so
   their wake count stays 0.
2. The wi1 and wi2 banks become Ready in that Think, once each, with 11 and 9
   members. The network does not wait for them.
3. Node 38 has no normal output, so the network rests there.
4. The exit relay's `player_leaves_train` reaches OnUsed 12. The network
   enters 43 (probes 42, 41, 40 reset the idles), then 21 and 22 (death
   overrides `srp_tyd_execute_kc_one` and `_two`). It holds before type 51
   Animate node 25 as an explicit refusal. The OnUsed Exit consumes the key.
5. The second interrogator has no brain. Its state follows the first
   interrogator's probes 3, 39 and 45. A stale or missing subject gives the
   original no-op (`null-subject`).

**shake_train** (`MeasuredTrainyardExitV1`, NativeSourceTrainyardExit). After
node 19, the unconditioned tail runs in the same tick:

1. 31 EnablePlayerWeapons. Lock bit = !`mEnabled`, mapped to the GPL
   `enableWeapon`/`disableWeapon` player events. `weaponEnabled` feeds
   `Wolf2009_UpdateSourceWeapon`. The original bit's player-side reader at
   `0x20300bfc` clears two fields of the player's weapon-command record
   (`+0x15d0`), as it does for a dead player. The equivalence is Inferred.
2. 32 NoTargetSet(Player, raw `"fasle"`, which is false). Sets native
   `fl.notarget`.
3. 33 EnableCredits(false). Processes `disableCreditsScreen` on the local
   player. The GPL player does not handle it; there is no native credits UI.
4. 34 SetUndying(Player, false). Recorded only; no native consumer.

The network then rests at node 34, which has no outputs.

### Stage 2 — `SourceAIScriptedAnimV1`

Scope: Animate 51/AnimFinished 50 with the §4.2 rules, AnimOverrideIdle 54,
AnimOverrideDeath 57, AnimOverrideMoveStart 59 (store only),
SetScriptedManual 33, and the `trigger`/`speak` frame commands. This covers
E5b–E7 and greeter idles X1/X2.

Acceptance:
- Animate start and AnimFinished come from native animator state. The
  remaining-time boundary at `mBlendFrames × 1000/24` ms is tested on both
  sides.
- A missing clip makes AnimFinished true on the next later tick.
- Executions fire `execution_gunshot` and `execution_gunshot2` exactly once
  per clip play, at frame 207, through `activate` on the speaker entities.
- Hostages die through their override clips; ragdoll stays a named refusal.
- Replaying with the clip replaced by another Animate finishes the first
  AnimFinished through the name check.

### Stage 3 — `SourceAISpeechV1`

Scope: Say 12/SayFinished 10, StopSpeaking 15, ScriptedVOStreamOut 490 and the
`speak` frame command on the whole-bank gate, for wi1/wi2/kc1 trainyard banks.

Acceptance:
- Line resolution through spawn args reproduces the Data mapping for every Say
  in the five trainyard chain graphs.
- A missing line finishes after `mFailDelay`.
- `lipcount_` selection uses an injected random sample and covers the
  boundaries 0 and count−1.
- SayFinished is false while the sample plays and true after the native
  voice channel stops; no timer substitutes for the playback observation.
- Stream-out of a bank in use is refused until speech ends.
- Lip-sync stays a named refusal.

### Stage 4 — `SourceAIScriptedMoveV1` (greeter node 10)

Scope: GoToAndFaceTarget 243 / ReachedAndFacing 242 and move start. Requires
the AAS 1.40 adapter from the AAS work, called only through the route
interface in §6.1.

Acceptance:
- From the real door completion, node 10 issues one scripted move to
  `run_to_ladder_point` with CombatRun bits.
- The first locomotion frames play `srp_tyd_nml_train_door_open` once, and the
  override is then empty.
- Every frame follows an AAS route: no teleport and no straight-line fallback.
- ReachedAndFacing becomes true only from the actor's real position and yaw.
- Exit on 10 → 31 stops the move.
- Re-entry by E5a while moving is recorded as an explicit case.

### Stage 5 — `SourceAICombatEntryV1`

Scope: Hunt 43, SetUndying 253, IgnoreEnemiesSet 49, NoTargetSet 102, AI
hitscan through the shared hitscan path, pain/death roots, with
CombatJustStarted, HasTakenPain and EnemyDead.

Acceptance:
- After E8 both interrogators acquire the player once each.
- Their shots damage the player through the native damage path with source
  damage defs.
- Player death and respawn do not repeat once-only events.
- Aware stays a named refusal until §6.4 is resolved.

## 9. Unknowns

- Whether the interrogator's first Think actually receives both an eligible
  OnSpawn root and a pending E4 Use in the original session.
- Frame-command dispatcher used for HKXA clips; frame-crossing rules; bodies of
  the `speak`, `speak_deferred` and `interactiveRagdoll` events.
- How the stripped `_stream` name selects the SNDS member; the speech interface
  at `0x205e6780`; lip-sync data and head animation.
- Reached/ReachedAndFacing/Facing evaluation; the generic move `+0x520` and
  move update `+0x4dc` internals; GoTo re-entry while moving.
- AI attack timing, aim, spread and damage-def selection; equivalence of
  `0x2007e2f0` with the native player hitscan.
- How OnAware roots fire given the constant-false Aware body; IsDead and
  SayInterrupted bodies; `removeOnDeath` and corpse handling.
- Equivalence of the `.def` keys quoted here with the compiled `.decls`.

## Reproduce

Read-only inputs: a private copy of `Gamex86.dll` with the SHA-256 above, its
NDK `llvm-objdump` text, the RTTI table from the campaign pass, the editor
`.def` text of `assets.pk4` and the trainyard BRAI dumps. Private scripts in
ignored `evidence/ai-actor/scripts/`: `fn.mjs` (annotated procedure print),
`xref.mjs`, `strref.mjs`, `dref.mjs`, `vft.mjs` (full vftables), `vs.mjs`
(slot lookup), `props.mjs` (brain property tables), `evdefs.mjs` and
`events.mjs` (event definitions and the `idAI` event table). Output stays in
the same ignored directory.

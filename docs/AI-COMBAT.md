# AI combat: Hunt, enemy state and the first AI hitscan

Reviewed 2026-11-11. This page closes the static contracts that the trainyard
interrogators need right after the execution scene: brain task Hunt (43),
IgnoreEnemiesSet (49) and NoTargetSet (102), the conditions EnemyDead (42),
CombatJustStarted (108), HasTakenPain (104) and Aware (103), and the native
`idAI` combat path from "has an enemy" to an AI hitscan shot. It maps each
piece to the GPL Doom 3 and Raven Quake 4 code in the pinned upstream and
plans a `SourceAIHuntV1` profile. It continues [AI-ACTOR](AI-ACTOR.md) §6.2,
§6.3 and stage 5, and [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) row E8, §4 and
§5 items 8, 10 and 11. Movement is planned separately in
[AI-MOVE](AI-MOVE.md); this page only names the movement calls that combat
makes.

Labels follow [AI-ACTOR](AI-ACTOR.md):

- **Data**: read from original ENTS, BRAI or `.def` text.
- **Static**: read from the disassembly of the hash-identified retail
  `Gamex86.dll` (`7cb1cbed…2d47`) at the named procedure or call site. Not
  executed.
- **Inferred**: consistent with the above, not closed.
- **Unknown**: no adequate evidence.

Names in `code` such as `senses.fl.ignoreEnemies`, `combat.tacticalCurrent`
or `weapon.shots` are the DLL's own debug strings, paired with fields by the
`idAI` debug printer `0x2017d810`. Spawn-key names are the strings the spawn
code passes to the dictionary. Event names come from the `idEventDef` static
initializers, state names from the `idAI` state table and cvar names from the
`idCVar` objects. Other names are descriptive labels. Offsets are for this
build only. A decompiler signature is not used as a contract anywhere on this
page.

## 1. Summary

| Question | Answer | Status |
|---|---|---|
| What does Hunt do? | Its Enter stops the scripted move and calls one AI procedure (`0x201aa390`). That procedure creates or refreshes an awareness entry for the prey, reports an awareness event, makes the entry the enemy, raises `senses.fl.alert` and arms the enemy-update timer. Hunt has no Exit and no Update | Static |
| When does Hunt end? | In the Enter's own Tick. The task completes at once; its embedded EnemyDead is only its primary condition. Interrogator node 6's normal output to Say 7 has no condition | Static, Data |
| Who keeps fighting? | The `idAI` state machine. Its default state is `State_Combat`, which picks a *tactical* state from a bit mask built from `tactical_*` spawn keys and runtime checks | Static |
| Which tactical do the interrogators get? | Most likely `AITACTICAL_NOCOVER` (`State_CombatNoCover`): cover is disabled by `disableCover 1`, Hunt needs an enemy lost for at least 10 s, and Idle is removed once there is an enemy. `CHARGE` and `HIDE` are not excluded | Inferred |
| Who fires? | A per-Think weapon controller (`idAI` virtual `+0x434`) independent of the tactical state. It starts bursts when a fire predicate passes and calls `idAI::AttackHitscan` once per shot | Static |
| Weapon data | `def_weapon weapon_mp40` → `def_hitscan_ai hitscan_mp40_ai` → `def_damage damage_mp40_ai` (`damage 6`); `def_stats_ai stats_mp40_ai` (3–8 shots, 120 ms apart, 1–2 bursts, 500–1000 ms between bursts, range 0–2000, spread 3.1) | Data, Static (key use) |
| IgnoreEnemiesSet | With `mIgnore` true it first clears the enemy, then writes `senses.fl.ignoreEnemies`. A set flag also blocks becoming alert | Static |
| NoTargetSet | Writes entity `+0x158` bit 0 (`notarget`). Hunt does not test it | Static |
| Boolean properties | Case-insensitive compare with `true`. `"fasle"` and `"1"` are false | Static |
| Aware | Calls a body that always returns false | Static |

Correction to the task brief: `0x201f2c60` is the Enter of IgnoreEnemiesSet,
not a combat state machine entry. [CAMPAIGN-RE](CAMPAIGN-RE.md) lists it as
such. The combat entry is `State_Combat` (`0x201ad990`) and the tactical
update (`+0x430`, `0x201d76e0`), §5.

## 2. Trainyard data

### 2.1 The graph after the execution (Data)

`brains/trainyard/nazi_interragator` (owner ENTS 2091):

| Node | Type | Properties | Outputs |
|---|---|---|---|
| 17 | SetScriptedManual 33 | `mSetToTrue false` | P→16 (same, interrogator 2), P→46, P→47 (SetUndying `false` on both), N→6 |
| 6 | Hunt 43 | `mPrey Player`, `mUpdatePosition true`; `mHunter` default `Me` | P→10, N→7 (no condition) |
| 10 | Hunt 43 | `mHunter nazi_interragator_2`, `mPrey Player`, `mUpdatePosition true` | — |
| 7 | Say 12 | `lipsync_trn_1007_wi1` ("Halt! Halt!!") | N [prim SayFinished] → 8 |
| 8 | Say 12 | `lipsync_trn_1008_wi2`, speaker interrogator 2 | — |

`mLocked` is false everywhere on this path. The network ends at node 8. The
GoToNoStop nodes 5, 9, 18 and 19 (`mLocation Player`, `mRange 100`) have no
inbound link and stay unreachable.

Other trainyard uses of the same types:

- `nazi_patrol` (owner 2049 `stage1_wehrmacht_officer_1`): roots OnAware 2
  and 41 (embedded Aware), OnCombat 3 (embedded CombatJustStarted, raw
  `mWasInCombat`/`mIsInCombat`/`mTimeChecked`) and OnPain 4 (embedded
  HasTakenPain) all lead to node 5, IgnoreEnemiesSet(false) on the three
  `allies_kreisau_contact_*`. Node 5 normal → 40, Hunt by the officer with
  default prey `MyEnemy`, which probes 39 and 15 (Hunt by the two infantry).
- `allies_contact_1`: IgnoreEnemiesSet and NoTargetSet on the allies, and
  Hunt 330–332 with `mPrey demo_heavy_trooper`.
- `train_greeter` 2, 44, 45: IgnoreEnemiesSet (default true) on the greeter
  and on `nazi_interragator_3/4`.
- `shake_train` 32: NoTargetSet(`Player`, raw `mNoTarget "fasle"`).

Trainyard therefore uses CombatJustStarted, HasTakenPain and Aware only in
`nazi_patrol`'s roots, and EnemyDead only as Hunt's embedded primary.

### 2.2 The two interrogators (Data)

ENTS 2091 and 2092 **both** carry `disableCover 1`, `tactical_object_bounce
0`, `Grenades 0`, `action_grenadeAttack 0`, `canQuickKill 0`, `no_drops 1`,
`speakStopRange 0`, `Hide 1` and `anim idle`. Combat keys inherited through
`trainyard_nazi_talker_N` → … → `soldier_base` (`.def` text of `assets.pk4`):

| Key | Value | Source |
|---|---|---|
| `team` | 1 | `enemies_base` |
| `tactical_cover`, `tactical_hide`, `tactical_relocate`, `tactical_object_bounce`, `tactical_invis_object` | 1 | `soldier_base` (`tactical_rush`, `tactical_disarm` are also set but not read by §5.3) |
| `huntLostTimeMin`, `huntLostTimeAdd` | 4000, 4000 | `enemies_base` |
| `reactionTimeBase` | 700 | `enemies_base` |
| `sightedNoAttackDuration` | 1500 | `enemies_base` |
| `visRange`, `fov` | 2800, 170 | `actor_base` |
| `attack_base_hitscan` | 1 | `soldier_base` |
| `def_weapon` | `weapon_mp40` | `enemy_human_base` |

Compiled `.decls` equivalence of these keys was not checked.

### 2.3 AI weapon and damage definitions (Data)

| Def | Keys that matter here |
|---|---|
| `weapon_mp40` (inherits `weapon_base`) | `def_hitscan_ai hitscan_mp40_ai`, `def_stats_ai stats_mp40_ai`; `weapon_base` gives `def_hitscan hitscan_base` and `def_melee_ai damage_melee_rifle_ai`. No `def_projectile`, no `def_continuous` |
| `hitscan_mp40_ai` → `hitscan_mp40_bullet` → `hitscan_bullet_base` | `def_damage damage_mp40_ai`, `num_projectiles 1`, `tracerchance 0.85`, `fx_tracer`, impact effects. `hitscan_bullet_base` also has `def_projectile projectile_bullet`; the AI path does not read that key from this dictionary (§5.6) |
| `damage_mp40_ai` → `damage_mp40` → `damage_bullet_base` | `damage 6.0` (player MP40: 5), `damageBoostRange 100`, `damageBoostScale 2.0`, `damageNoUndyingDist 30.0f`, `push 40`, `damage_type bullet` |
| `stats_mp40_ai` | `minShots 3`, `maxShots 8`, `shotWaitTime 120`, `minBursts 1`, `maxBursts 2`, `burstWaitTimeMin 500`, `burstWaitTimeMax 1000`, `clipSize 32`, `attackRange "0 2000"`, `weaponSpread 3.1`, `weaponSpreadAdjustFactor 0.75`, `weaponSpreadAdjustMinMS 1000`, `weaponSpreadAdjustMaxMS 6000`, `minAimDot 0.95`, AI fire sounds |

The `damageBoost*` and `damageNoUndyingDist` consumers were not read
(**Unknown**). `damageNoUndyingDist` matters because the player's undying
flag is cleared at map start (shake_train 34).

## 3. Brain side (Static)

### 3.1 Task slot layout

Read from the vftables of Hunt, IgnoreEnemiesSet, NoTargetSet,
GoToAndFaceTarget and the roots:

| Slot | Meaning | Hunt | IgnoreEnemiesSet / NoTargetSet |
|---:|---|---|---|
| 1 | Initialize (`or flags, 0x2000`) | inherited | inherited |
| 2 | Enter | `0x201f2890` | `0x201f2c60` / `0x201f2d70` |
| 3 | Exit (GoToAndFaceTarget: `0x201f15d0`) | `ret` | `ret` |
| 6 | Bind (clears cached entity handles) | `0x201f7a00`: embedded condition, then `mPrey` | `0x201f2d60` |
| 10 | Primary condition getter | `0x201ef400`: returns the embedded condition at `+0x4c` | `0x200e13b0`: null |
| 11 | Root poll | `0x200b1700`: true | true |

Roots (OnCombat, OnPain, OnAware) instead evaluate their embedded condition
in slot 11 (`0x201ff140` → condition slot 10).

### 3.2 Hunt 43

Properties (descriptor group `0x205c8d68`): embedded EnemyDead at `+0x4c`,
whose `mHunter` (default `Me`, "Whos enemy are we talking about?") is at
task `+0x6c`; `mPrey` `+0xa8` (default `MyEnemy`); `mLocked` `+0xe4`
(false, "ignore all other combatants until told otherwise or the prey is
dead"); `mUpdatePosition` `+0x108` (false, "cheat and instantly know where
the enemy is").

Enter `0x201f2890`:

1. Resolve `mHunter` and cast to `idAI`. Not an AI: return.
2. Stop the scripted move: virtual `+0x45c`, then `+0x460(1)`.
3. Read `mUpdatePosition`, then `mLocked`, then resolve `mPrey`.
4. Call `0x201aa390(prey, locked, updatePosition, 1)` (§3.3).

There is no Exit and no per-frame Update. The task completes in the Tick of
its Enter. RangeAttack (`.?AURangeAttack@rvBrain`, Enter `0x201f29f0`) has an
identical property group (`0x205c8e70`) and also calls `0x201aa390`.

### 3.3 Hunt procedure `0x201aa390`

1. Return if the prey is null or the AI is dead (`aifl.dead`, `ai+0x954`
   bit 0x4). Hunt does **not** test the prey's `notarget` bit, team or
   `senses.fl.ignoreEnemies` here.
2. Get or create the prey's awareness entry (`0x201aa270(prey, 1)`, §4.2).
   An existing entry gets its byte `+0x24` set to `(b & ~0x10) | 0x48`.
3. Report an awareness event to the manager object at `0x2078cf60`
   (`0x201939f0(ai, 2, 1, perceived && updatePosition, 0)`; "perceived" is
   entry byte `+0x24` bit 0x2). That procedure acts only for team-1 AI with
   `sensesActive` and checks the cvars `ai_logStealthEvents` and
   `ai_debugStealth`. Its effect is **Unknown**.
4. If the AI had no enemy (`+0x384` is null) and the entry is perceived,
   post `announce` (`0x202b0130`) with delay 500 and integer arguments 19, 0,
   22 (meaning **Unknown**).
5. If `mUpdatePosition`, update the entry's last known position
   (`0x201a7420`, not read in detail).
6. Call SetEnemy (virtual `+0x5a8`, `0x201a6da0`) with the old and new
   entries (§4.3).
7. Copy `mLocked` into `ai+0x1c85` bit 0x1.
8. `senses.timeEnemySet` (`+0x1bbc`) = now. Unless `ai+0x95e` bit 0x1,
   `senses.timeUpdateEnemy` (`+0x1bc0`) = now + 5000 if the entry is
   perceived, else now + 500.
9. Store the entry as the current enemy (`ai+0x1b18`) and set entry bits
   `+0x25` 0x10 and 0x08 and `+0x24` 0x08.
10. If `senses.fl.alert` is still clear, call SetAlert(1, entry) (§4.3).

"now" is the actor's virtual `+0x50` time in milliseconds.

### 3.4 IgnoreEnemiesSet 49 and NoTargetSet 102

- **IgnoreEnemiesSet** `0x201f2c60`: properties `mIgnore` `+0x4c` (default
  true, "whether to ignore enemies") and `mActor` `+0x70` (default `Me`).
  The actor is cast to `idAI`; anything else is a no-op. When `mIgnore` is
  true it first calls ClearEnemy (`0x201a4680`, §4.3), then writes
  `senses.fl.ignoreEnemies` (`ai+0x1c84` bit 0x4) = `mIgnore`. With the flag
  set, SetAlert(1) refuses (§4.3), so a later SetEnemy leaves the AI not
  alert.
- **NoTargetSet** `0x201f2d70`: `mNoTarget` `+0x4c` (default true, "whether
  to be ignored by enemies") and `mActor` `+0x70`. Any entity class: entity
  byte `+0x158` bit 0 = `mNoTarget`. The readers are the AI-side procedures
  listed in [MISSION-SYSTEMS](MISSION-SYSTEMS.md); this page did not read how
  perception filters on it.
- **Boolean properties** (`0x2020ff40`): the expanded property text is
  compared case-insensitively with `true`. Only `true` in any case is true.
  shake_train's `"fasle"` therefore writes false, which is also the
  player's spawn value.

### 3.5 Conditions

All four resolve their actor property (`+0x20`) and return
`result XOR invert`, where invert is task flag bit 0x2 (`0x201dab50`).

| ID | Condition | Evaluate | Behavior |
|---:|---|---|---|
| 42 | EnemyDead | `0x201ea9f0` | `mHunter` cast to `idAI`, else false. True when its enemy (`+0x384`) exists and has health ≤ 0. If that enemy is itself an `idAI`, it must also have been dead for longer than the hunter's `newEnemyDebounceTime` (`ai+0x1b2c`, default 2000 ms; time from enemy `+0x1cac`). A dead player counts at once |
| 108 | CombatJustStarted | `0x201eae00` | `idAI`, alive. Once per game frame it shifts `mIsInCombat` (`+0x5d`) into `mWasInCombat` (`+0x5c`) and sets `mIsInCombat` = has enemy, or now < `ai+0x1204`. True on the rising edge. `mTimeChecked` is `+0x60` |
| 104 | HasTakenPain | `0x201eacc0` | `idActor`, alive. `mMinThreshold` (`+0x5c`, −1 = no lower limit) and `mMaxThreshold` (`+0x80`). True when `actor+0x5ec`, truncated to an integer, is non-zero and inside the range. `+0x5ec` is last frame's pain damage: the `idAI` Think step copies `+0x5e8` into it and clears `+0x5e8` (§5.1) |
| 103 | Aware | `0x201eabc0` | `idAI`; calls `0x202fce30` (`xor al, al; ret`). Always false unless inverted |

## 4. Enemy state (Static)

### 4.1 Field map

From the debug printer and the spawn code (`0x201a4b50`):

| Field | Name or spawn key (default) |
|---|---|
| `ai+0x1c84` | bits: 0x1 `sensesActive` (1), 0x2 `senses.fl.alert` (`alert`, 0), 0x4 `senses.fl.ignoreEnemies` (`ignoreEnemies`, 0), 0x8 `ignoreEnemiesUntilHurt`, 0x10 `ignorePlayer`, 0x20 `ignorePlayerUntilHurt`, 0x40 `ignoreOcclusion` |
| `ai+0x1c85` | 0x1 Hunt `mLocked`; 0x2 `trackPlayerVisibility`; 0x4 `ignoreNovis` |
| `ai+0x1b18` | current enemy awareness entry; `+0x1b1c` a second cached entry |
| `ai+0x1aec` / `+0x1af8` | awareness entry count / array |
| `ai+0x1b20`…`+0x1b30` | `newEnemyDebounceTime` (2000) at `+0x1b2c`, `reactionTimeBase` (700) at `+0x1b30` |
| `ai+0x1bbc` / `+0x1bc0` | `senses.timeEnemySet` / `senses.timeUpdateEnemy` |
| `ai+0x1bcc` | time of the scheduled `idleLookForEnemy` |
| `ai+0x1be4`/`+0x1be8`/`+0x1bec` | `senses.fovRange` / `visRange` / `earRange` |
| `ai+0x51c` | team |
| `ent+0xd8` | health (float) |

### 4.2 Awareness entries

`0x201a5b90` finds an entry by entity: first `+0x1b18`, then `+0x1b1c`, then a
linear search of the list. `0x201aa270` creates a missing one through
`0x201a97c0` and `0x201a9b90`. An entry holds the entity handle at `+0x4`
(spawn id << 13 | entity number), flag bytes `+0x24`/`+0x25`, a sub-record
pointer at `+0xc`, the last time seen at `+0x14` and a second time at `+0x18`.
`0x201a6320(ent, t)` is "seen within t ms"; `0x201a62e0(ent)` returns now −
`+0x18`. The perception update that fills entries every frame was not read
(**Unknown**).

### 4.3 SetEnemy, ClearEnemy and SetAlert

- **SetEnemy** (virtual `+0x5a8`, `0x201a6da0`, arguments old and new
  entry): clears `ai+0x1e20`; resets `aim.focusTime` when negative; for a
  team-1 AI with a live `+0x790` entity calls its virtual `+0x3c8(0)`; for a
  non-sniper AI unregisters from the old enemy (`0x2000a350`) and registers
  with the new one (`0x2000a470`) when the respective entry has bit
  `+0x24` 0x4; calls `0x20166c30(1, 0)`; and calls SetAlert(1, new) when not
  alert. It does not write `ai+0x1b18`; Hunt and ClearEnemy do.
- **ClearEnemy** (`0x201a4680`): with a locked enemy clears bit 0x4 of
  `ai+0x1824`; calls SetEnemy(old, null); clears the lock, the enemy entry
  and `senses.timeEnemySet`. When `ai+0x1edc` > 0 and `ai+0x1e15` bit 0x8 it
  issues a generic move (`+0x520`, command 9 `MOVE_TO_POSITION` to
  `ai+0x1ebc`).
- **SetAlert** (`0x201a49d0(alert, entry)`) acts only when `sensesActive`
  is set, the requested state differs from `senses.fl.alert`, the AI is
  alive and, for alert = 1, `senses.fl.ignoreEnemies` is clear. It writes
  `senses.fl.alert`. Going alert: clears `senses.timeUpdateEnemy`, cancels
  `delayAlert` and `delayTagFound`, picks a delay of 0–800 ms (entry bit
  `+0x25` 0x40) or 500–1500 ms (otherwise; 0 when `now − ai+0xe0 < 500`),
  stores now + delay in `ai+0x1bcc`, posts `idleLookForEnemy(1)` after that
  delay and schedules announcement slot 4 (`0x20171ae0(4, 0)`). Leaving:
  unless locked, ClearEnemy, then posts `announceAllClear` after 1000 ms.
  The random numbers come from the game LCG at `0x2063376c`.

## 5. The combat state machine (Static)

### 5.1 Think order

`idAI` virtual `+0x428` (`0x201ad640`), when virtual `+0x438` allows it:

1. Weapon controller, virtual `+0x434` (`0x20178380`, §5.6).
2. `0x201a5890`, `0x201c36d0`, `0x201d00b0` (not read; perception and
   cover candidates are **Inferred**).
3. Run the main state thread (`ai+0x9a0`) and the second thread
   (`ai+0x3970`) through `0x202c3230`.
4. Pain bookkeeping: `actor+0x5ec` = `actor+0x5e8`; `actor+0x5e8` = 0.

### 5.2 States and results

The `idAI` state table (`0x205bf030`) maps names to methods, among them
`State_Idle`, `State_Combat` (`0x201ad990`), `State_CombatCover`,
`State_CombatNoCover` (`0x201ae630`), `State_CombatHide`, `State_CombatHunt`,
`State_CombatCharge`, `State_CombatRelocate` and the scripted states. The
initial state (virtual `+0x76c`, `0x2014ce10`) is **`State_Combat`**.

State results: stage 0 code returns 8 + stage (`State_Combat` stage 0 → 9,
`State_CombatNoCover` 8/9/10), and 4 re-runs next frame. 2 ends the state.
These match Quake 4's `SRESULT_WAIT = 4` and `SRESULT_STAGE(x)` with a base
two higher than Quake 4's 6 (**Inferred**).

**`State_Combat`**: stage 0 sets `combat.tacticalCurrent` to 0 and calls
`0x20166030`; stage 1 sets it to 0 again and calls the tactical update with
the available mask and delay 0 until it switches state.

### 5.3 Tactical selection

Tactical values, from the selector (`0x201d6d60`) and the state-name table
`0x205bf668` (index = value):

| Value | `AITACTICAL_*` | State | Mask bit | Spawn key (default) |
|---:|---|---|---|---|
| 0 | NONE | `State_Combat` | — | — |
| 1 | IDLE | `State_Idle` | 0x2 | always |
| 2 | MOVE_FOLLOW | `State_MoveFollowLeader` | 0x4 | `tactical_moveFollow` (1) |
| 3 | COVER | `State_CombatCover` | 0x8 | `tactical_cover` (0) |
| 4 | NOCOVER | `State_CombatNoCover` | — | fallback |
| 5 | HIDE | `State_CombatHide` | 0x20 | `tactical_hide` (0) |
| 6 | HUNT | `State_CombatHunt` | 0x40 | `tactical_hunt` (1) |
| 7 | RELOCATE | `State_CombatRelocate` | 0x80 | `tactical_relocate` (1) |
| 8 | PULL_ALARM | `State_CombatPullAlarm` | 0x100 | `tactical_pull_alarm` (0) |
| 9 | KICK_TABLE | `State_CombatKickTable` | 0x200 | `tactical_kick_table` (1) |
| 10 | CHECK_CORPSE | `State_CheckCorpse` | 0x400 | `tactical_checkCorpse` (1) |
| 11 | GOTO_VEHICLE | `State_GoToVehicle` | 0x800 | `tactical_gotoVehicle` (1) |
| 12 | GOTO_TURRET | `State_GoToTurret` | 0x1000 | `tactical_gotoTurret` (1) |
| 13 | WANDER | `State_Wander` | 0x2000 | `tactical_wander` (1) |
| 14 | CHARGE | `State_CombatCharge` | 0x4000 | `tactical_charge` (1) |
| 15 | CHECK_OBJECT_BOUNCE | `State_CheckObjectBounce` | 0x8000 | `tactical_object_bounce` (0) |
| 16 | CHECK_INVIS_OBJECT | `State_CheckInvisObject` | 0x10000 | `tactical_invis_object` (0) |
| 17 | DERIVED_BEHAVIOR | `State_DerivedBehavior` | 0x20000 | `tactical_derived` (0) |
| 18 | COUNTER_DEFEND | `State_CounterDefend` | 0x40000 | `tactical_counter` (1) |
| 19 | MELEE_READY | `State_MeleeReady` | 0x80000 | `tactical_melee_ready` (1) |

`combat.tacticalAvailable` (`ai+0x1e1c`) is built at spawn (`0x201d68d0`):
0x2 plus one bit per true key. Values are parsed with `atoi`.

**Tactical update** (virtual `+0x430`, `0x201d76e0(mask, delay)`): refuses
while an action runs (`ai+0x3b60`), in some animation states, while
`ai+0x1ce4` is set, or when `delay` is non-zero and less than `delay` ms
have passed since `ai+0x1e20`. It then filters the mask (`0x201d6ea0`) and
selects (`0x201d6d60`).

**Filter** (`0x201d6ea0`), the parts relevant here. "Override" means
`combat.coverOverrideTime` (`ai+0x1f68`) is −1 or in the future.

- IDLE is removed when there is an enemy.
- HIDE wins at once when the `ai+0x141c` entity is alive and movement is
  enabled; otherwise it is removed.
- COVER needs its predicate (virtual `+0x478`, `0x201bb470`): `ai_disableCover`
  off, `combat.fl.disableCover` (`ai+0x1e14` bit 0x8, spawn key
  `disableCover`) clear, movement enabled, no override, and a cover feature
  (`ai+0xa40` virtual `+0x64`).
- HUNT needs no override and CanHunt (virtual `+0x61c`, `0x201aded0`): an
  enemy, `ai_disableHunt` off, movement enabled, an enemy entry sub-record,
  now − `ai+0x970` ≥ the hunt delay, and the enemy **not** seen within the
  hunt delay. The hunt delay (`0x201ade50`) is (`ai+0x1b3c` ×
  `ai_huntTimeAdd` + `ai_huntTimeMin`) × 1000 ms; team 0 uses
  `ai_huntTimeAddAllies`/`ai_huntTimeMinAllies` (5.0 and 15.0) instead of
  5.0 and 10.0. HUNT also needs `ai+0x1b3c` ≤ 4 while not already hunting.
- RELOCATE needs movement and `combat.relocate` (`ai+0x1e70`); MELEE_READY
  needs `ai+0x1e15` bit 0x10; COUNTER_DEFEND, CHECK_OBJECT_BOUNCE and
  CHECK_INVIS_OBJECT use the constant-false body and are always removed.
  CHARGE (`0x201b23c0`), PULL_ALARM, KICK_TABLE, WANDER and the vehicle and
  turret checks were read only in part.

**Selector priority**: HIDE, CHARGE, GOTO_VEHICLE, GOTO_TURRET, RELOCATE,
KICK_TABLE, PULL_ALARM, COUNTER_DEFEND, MELEE_READY, CHECK_CORPSE,
CHECK_OBJECT_BOUNCE, CHECK_INVIS_OBJECT, COVER, HUNT, MOVE_FOLLOW, WANDER,
IDLE, DERIVED_BEHAVIOR, else NOCOVER. If the result equals
`combat.tacticalCurrent`, only `ai+0x1e20` is refreshed. Otherwise SetTactical
(`0x201d6ce0(t, 1, 0)`) stores the time and value, notifies the state thread
about the old state, posts the new state by name (value 0 posts the initial
state) and calls the change hook `0x201d6820`.

**Interrogators after Hunt (Inferred).** Available mask: IDLE,
MOVE_FOLLOW, COVER, HIDE, HUNT, RELOCATE, KICK_TABLE, CHECK_CORPSE,
GOTO_VEHICLE, GOTO_TURRET, WANDER, CHARGE, CHECK_INVIS_OBJECT,
COUNTER_DEFEND and MELEE_READY. With the player as a freshly set enemy,
IDLE, COVER, HUNT, COUNTER_DEFEND and CHECK_INVIS_OBJECT drop out for the
reasons above, and RELOCATE and MELEE_READY start false. The result is
`State_CombatNoCover` unless CHARGE, HIDE, KICK_TABLE, CHECK_CORPSE, a
vehicle or turret check, MOVE_FOLLOW or WANDER passes; those predicates were
not closed.

### 5.4 `State_CombatNoCover` (`0x201ae630`)

Every frame it first calls the tactical update with delay 1000 ms and the
action check (virtual `+0x42c`); either one returning true ends the frame.
Stage 0 sets a timer `ai+0x1fe8` = now + 1000–4000 ms. Stage 1: when the
timer has passed, no focus entity (`+0x3cc`) is set and CanHunt is true, it
issues a short random move (`0x2019e4c0`) and goes to stage 2. With a focus
entity that is not seen within 2000 ms and the AI not moving, it issues an
entity move (`0x2019a720`). Stage 2 waits until the move ends (or CanHunt
fails) and returns to stage 0. NoCover itself never fires a weapon.

### 5.5 Actions

Actions (`Action_*`, run through `ai+0x3b60`) can force fire. The event
`beginForcedFire` (`0x20183730`) applies only while the running action has
flag byte `+0x1` bit 0x80 (the `allowForceRangedFire` keys of
`soldier_base`): it sets `ai+0x1e15` bit 0x4, `weapon.waitTime` 0 and
`weapon.shots` = `maxShots`. `endForcedFire` (`0x20183760`) clears both.
Which animation frame commands post these events was not read.

### 5.6 Weapon controller and AttackHitscan

**Fields** (`0x2017b600`, spawn of the AI weapon): weapon dictionary
`+0x2060`; `def_stats_ai` dictionary `+0x2064` (falls back to the weapon
dictionary); `def_melee_ai` `+0x2068`; `def_hitscan` replaced by
`def_hitscan_ai` when present `+0x206c`; `def_projectile` `+0x2070`;
`def_continuous` `+0x2074`. From the stats dictionary: `minShots` `+0x207c`
(default 1), `maxShots` `+0x2080`, `shotWaitTime` `+0x2084` (150),
`min/maxContinuousTime` `+0x2088`/`+0x208c`, `min/maxBursts`
`+0x2090`/`+0x2094`, `min/maxCoverBursts`, `burstWaitTimeMin/Max`
`+0x20a0`/`+0x20a4`, `attackRange` `+0x21a0`/`+0x21a4`
(`weapon.attackRangeMin/Max`), `minAimDot` `+0x21b0`, the spread keys
`+0x21b8`…`+0x21d0`. Runtime: `weapon.waitTime` `+0x2224`, `weapon.shots`
`+0x2228`, `weapon.bursts` `+0x222c`, last-shot time `+0x223c`, weapon entity
`+0x2248`.

**Controller** (`+0x434`, `0x20178380`), each Think while alive (a dead AI
only with byte `ai+0x2244` set, probably `attackWhileDead`) and with
`ai+0x958` bits 0x6 clear:

1. With shots > 0 and now ≥ `weapon.waitTime`, fire one shot. With a
   projectile def (outside an alt-fire hitscan mode) it calls virtual
   `+0x62c`. Otherwise it calls
   **`idAI::AttackHitscan`** (virtual `+0x628`, `0x20179190`) with attack
   name `base` (`second` while the action has bit 0x1 of byte `+0x1`), the
   hitscan dictionary (`ai+0x2e20` override, else `+0x206c`) and −1.
2. After a shot: `weapon.waitTime` = now + `shotWaitTime`; shots − 1; at
   0, bursts − 1 (not below 0) and `ai+0xa68` = now + 100–300 ms.
3. When the fire predicate (virtual `+0x648`) passes, start a burst: shots
   = random in [`minShots`, `maxShots`] (exactly 1, with 0–60 ms extra wait,
   when `maxShots` is 1). The wait before the first shot is 0 when the last
   shot is older than `shotWaitTime` + `burstWaitTimeMax`, else random in
   [`burstWaitTimeMin`, `burstWaitTimeMax`]. With chance 1/10 it calls
   `0x20171f50(25, 0, 0)`, which works on the same 31-slot announcement table
   as `0x20171ae0` (combat chatter, **Inferred**).
4. Continuous weapons (`def_continuous`) use the continuous-time keys
   instead; `weapon_mp40` has none.

**Fire predicate** (`0x20176d30`): no shots left and the continuous time
passed; then either forced fire, or all of: aim dot (`ai+0xf28`) ≥
`minAimDot`, `aim.facingWall` and `aim.facingFriendly` clear, the
`targetLocked*` gate, not in two specific actions, an enemy, line of sight
when `targetLOSOnly`, the enemy seen within `ai+0x1e68` ms and known for at
least `ai+0x1e6c` ms, enemy not dead (`0x201a64a0`) with health > 0, enemy
distance inside `attackRange` (`0x201a8400`), `ai_disableAttacks` off and a
final check `0x201aea20(0, 0)` false.

**AttackHitscan** (`0x20179190`) applies `weapon.spreadHorizontal/Vertical`
and the time-based spread adjustment, reads `num_projectiles` from the
hitscan dictionary and calls the shared game hitscan `0x2007e2f0` once per
projectile (as [AI-ACTOR](AI-ACTOR.md) §6.3 recorded). Muzzle joint, aim
point (`aimAtChest`) and the spread formula were not closed.

## 6. Engine mapping

| Original contract | Reusable upstream code | New work |
|---|---|---|
| Brain tasks 43/49/102 and conditions | none (Doom 3 uses scripts) | Portable task/condition handlers over the existing `BrainTask` lifecycle; boolean `true` rule |
| Enemy entries, SetEnemy, ClearEnemy, SetAlert | Quake 4 `idAI::SetEnemy`, `ClearEnemy`, `UpdateEnemyPosition`, `rvAIEnemy_t` (`quake4/ai/AI.cpp`, `AI.h`); GPL Doom 3 `idAI::SetEnemy`, `enemy` handle | Wolf awareness list, entry bits, timers, `mLocked`, SetAlert gates, events `idleLookForEnemy`/`announce`/`announceAllClear` |
| State thread and results | Quake 4 `rvStateThread`, `stateParms_t`, `SRESULT_*` (`quake4/gamesys/State.h`, `StateThread.*`) | Wolf result numbering (stage base 8) |
| `State_Combat`, tactical update, filter, selector | Quake 4 `State_Combat`, `UpdateTactical`, `UpdateTactical_r`, `FilterTactical`, `CheckTactical`, `tacticalState[]` (`quake4/ai/AI_States.cpp`) | Wolf enum (20 values incl. HUNT, RELOCATE, CHARGE), mask keys, priority order, override and hunt-delay rules |
| `State_CombatNoCover` | Quake 4 `State_CombatRanged`/`State_CombatTurret` shape | Wolf stage logic and timers |
| Weapon controller and bursts | none in Quake 4 (it fires from `rvAIAction` ranged attacks and animation frame events); GPL Doom 3 `idAI::CreateProjectile` | New controller: bursts, waits, fire predicate, forced fire |
| AttackHitscan | Quake 4 `idAI::Attack` → `gameLocal.HitScan`; port player hitscan already native | Per-shot spread, `num_projectiles`, source damage def; equivalence of `0x2007e2f0` with the port's player hitscan |
| Pain accumulator and HasTakenPain | Quake 4 `pain.takenThisFrame`/`lastTakenTime` | `+0x5e8`/`+0x5ec` frame shift |

The Quake 4 SDK files (`doom3/neo/quake4/ai/AI*.cpp`, `AI.h`,
`quake4/gamesys/State.h`) are in the pinned upstream's git objects. Code
copied or adapted from them keeps its license notice, is marked and is
recorded in [PROVENANCE](PROVENANCE.json); it is not relabelled GPL. Shared
ancestry is not evidence of identical behavior: every reused piece must still
match the contracts above. The Quake 4 `State_Combat` and tactical-state
table are close structural relatives; the Wolf enum, priority and filters
differ and must come from this page.

## 7. Implementation plan: `SourceAIHuntV1`

Every stage is a named profile with explicit refusals. No unknown rule is
filled in silently; each stand-in has a name that appears in the run receipt.
Movement uses whatever [AI-MOVE](AI-MOVE.md)'s `SourceAIMoveV1` provides;
until then every combat move is a named refusal and the AI fights from where
it stands.

### 7.1 Portable layer (`src/wolf2009`)

1. **Brain handlers.** Hunt 43: resolve `mHunter`/`mPrey`, read
   `mLocked`/`mUpdatePosition`, complete in the same Tick, expose EnemyDead
   as its primary condition. IgnoreEnemiesSet 49 and NoTargetSet 102 as
   §3.4. Conditions 42, 104, 108 as §3.5, with Aware 103 implemented as the
   original constant false (documented, not a refusal). The boolean rule
   `Icmp(text, "true") == 0` for these properties.
2. **Tactical tables.** The §5.3 enum, mask keys and defaults, the selector
   priority, and a filter interface whose unread predicates return a named
   "unknown predicate" result.
3. **Burst model.** The §5.6 controller as a pure function of time, stats
   dictionary values, a supplied LCG and a fire-predicate input.

Portable acceptance (original data, read-only):

- Interrogator graph: from node 17, one Tick enters 6, probes 10 and enters
  7. A host fake records exactly one Hunt call per interrogator with prey
  `Player`, `locked = false`, `updatePosition = true`.
- `"fasle"` → false, `"True"` → true, `"1"` → false.
- Both interrogators' available mask equals the §5.3 list (read from their
  ENTS and `.def` chain); with `disableCover 1` the cover predicate fails.
- `stats_mp40_ai` replay: bursts of 3–8 shots 120 ms apart; first-burst wait
  0; next-burst wait 500–1000 ms within the `shotWaitTime +
  burstWaitTimeMax` window; never a shot before `weapon.waitTime`.
- CombatJustStarted is true in exactly one frame after the enemy appears;
  HasTakenPain is true only in the frame after damage, within thresholds.

### 7.2 Native layer (`src/idtech4a_gameplay`, engine patch)

1. **`SourceAIEnemyStateV1`.** Per-AI awareness list (one entry per prey
   entity, handle-checked), current enemy, `senses.fl.*` from spawn keys,
   SetEnemy/ClearEnemy/SetAlert as §4.3 including the timers. The awareness
   manager call (§3.3 step 3), `announce*` speech and announcement slots
   are named refusals (`AwarenessEventUnknownV1`, `AnnounceRefusedV1`).
   `idleLookForEnemy` is posted and its arrival recorded; its handler body is
   a named refusal until read.
2. **`SourceAIStateThreadV1`.** A Wolf state thread on the native AI with
   `State_Combat` as the initial state and the scripted states already in
   use (scripted animation and manual). Leaving scripted manual returns to
   `State_Combat`.
3. **`SourceAITacticalV1`.** Tactical update with the delay rule; filter
   with the closed predicates (§5.3); every unread predicate refuses its
   tactical by name. Selection of `NOCOVER` posts `State_CombatNoCover`
   implemented as §5.4, with its moves routed to `SourceAIMoveV1` or refused.
   `State_Idle` and the other states refuse.
4. **`SourceAIWeaponControllerV1`.** The §5.6 controller on the
   AI-held MP40 with `stats_mp40_ai`. The fire predicate uses the closed
   checks. The aim dot (`ai+0xf28`) needs the aim system: first stand-in
   `TorsoYawAimDotV1` (dot between the AI's view axis and the enemy
   direction), named in the receipt. `targetLocked*`, LOS (`targetLOSOnly`)
   and the final check `0x201aea20` are refusals until read, except that LOS
   may use a native trace under the name `TraceLosV1`.
5. **`SourceAIHitscanV1`.** One hitscan per `num_projectiles` from the
   weapon's flash joint toward the enemy's chest with `weaponSpread`
   degrees of uniform cone spread (`ConeSpreadV1`, a stand-in), dealing
   `damage_mp40_ai` through the native damage path that already handles
   player damage, death and respawn. `damageBoost*` and
   `damageNoUndyingDist` are recorded as unsupported keys.

### 7.3 Native acceptance (original trainyard, Android 15 x86_64)

- After node 17, node 6 runs once and node 10 is entered as a probe in the
  same Tick. Each interrogator records exactly one Hunt: enemy = player,
  `senses.fl.alert` set, `timeEnemySet` = that Tick, `timeUpdateEnemy` =
  Tick + 5000 or + 500 (whichever the perceived bit gives), `mLocked` clear.
- Node 7 starts in the same Tick as node 6 (no condition); node 8 follows
  SayFinished. Hunt has no Exit receipt.
- Each interrogator's tactical selection is logged once with the filtered
  mask and the chosen value. Any refused predicate appears by name. Expected
  `NOCOVER` (value 4); another value is recorded as a finding, not forced.
- With the player within 2000 units, alive and inside `minAimDot`, the first
  burst starts. Shots are ≥ 120 ms apart, bursts hold 3–8 shots, and every
  hit applies 6 damage before boost rules. No shot fires while
  `ai_disableAttacks` is set, while the player is dead, or while
  `senses.fl.ignoreEnemies` is set on the shooter.
- When the player dies, fire stops in the same frame, EnemyDead (with
  `mHunter` = interrogator 1) becomes true, and respawn does not repeat
  Hunt, VO 7/8 or any once-only output.
- Teardown and map reload leave no awareness entry, pending
  `idleLookForEnemy`, forced-fire state or weapon owner.

### 7.4 What comes next

For the patrol (`nazi_patrol`) and later maps: the perception update that
fills awareness entries from sight and sound (with `notarget`,
`ignorePlayer*`, `fov`, `visRange`), the CHARGE, HIDE, RELOCATE and
cover predicates, `State_CombatCover`/`Hunt`/`Charge`, the actions and
their frame events (`beginForcedFire`), the awareness manager and global
awareness levels, and OnAware's root path given the constant-false Aware.

## 8. Unknowns

- The awareness manager `0x2078cf60` (`0x201939f0`) and its event level 2;
  the meaning of entry bits (`+0x24` 0x2/0x4/0x8/0x10/0x40, `+0x25`) and of
  the entry sub-record `+0xc`; the last-known-position update `0x201a7420`.
- The `idleLookForEnemy` handler (`0x20183810`), the `announce` arguments,
  the announcement slots (`0x20171ae0`) and `0x20166c30`.
- The perception and cover updates `0x201a5890`, `0x201c36d0`, `0x201d00b0`
  and how they use `notarget` and the `ignore*` flags.
- CHARGE, HIDE (`ai+0x141c`), KICK_TABLE, PULL_ALARM, WANDER, MOVE_FOLLOW
  and the vehicle and turret predicates; the action check `+0x42c`.
- `ai+0x1204` (CombatJustStarted's timer), `ai+0x970`, `ai+0x1b3c` (hunt
  order), `ai+0xe0`, `ai+0x95e` bit 0x1.
- Aim: how `ai+0xf28`, `aim.facingWall` and `aim.facingFriendly` are
  computed; the `targetLocked*` gate; `0x201aea20`; AttackHitscan's muzzle,
  aim point and spread formula; equivalence of `0x2007e2f0` with the port's
  player hitscan.
- Consumers of `damageBoostRange`, `damageBoostScale` and
  `damageNoUndyingDist`; compiled `.decls` equivalence of all `.def` keys.
- Which frame commands post `beginForcedFire`/`endForcedFire`.

## 9. Corrections to earlier pages

- [AI-ACTOR](AI-ACTOR.md) §6.4: HasTakenPain reads last frame's pain
  **damage** (`+0x5ec`, compared with damage thresholds), not a pain time.
  §6.2 is confirmed; its "timers of 5000 or 500 ms" are
  `senses.timeUpdateEnemy`.
- [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §4: both interrogators carry
  `disableCover 1` and the grenade keys, not only #1. The "Hunt
  continuation" row is the `idAI` combat state machine of §5.
- [BRAIN-TASKS](BRAIN-TASKS.md) §5: IgnoreEnemiesSet's "bit 2" is
  `senses.fl.ignoreEnemies` and the call before it (`0x201a4680`) is
  ClearEnemy.
- [CAMPAIGN-RE](CAMPAIGN-RE.md) §6.2 item 8 names `201f2c60` as a combat
  subsystem target; it is IgnoreEnemiesSet's Enter.

## Reproduce

Read-only inputs: a private copy of `Gamex86.dll` with the hash above and its
NDK `llvm-objdump` text, the RTTI/vftable tables from the campaign pass, the
trainyard BRAI dumps and ENTS JSON from the trainyard chain pass, and the
editor `.def` text of `assets.pk4`. Private scripts in ignored
`evidence/ai-combat/scripts/` (`d.mjs`: annotated procedure dump;
`printer.mjs`: debug-printer field map; `keys.mjs`: spawn-key/field
pairing, which needs the one-key shift noted in `FINDINGS.local.md`) reuse
the helpers in `evidence/ai-actor/scripts/` (`fn`, `xref`, `strref`, `dref`,
`vs`, `events`). Quake 4 references: `git show
HEAD:doom3/neo/quake4/ai/AI_States.cpp` (and `AI.cpp`, `AI.h`,
`gamesys/State.h`) in the engine checkout. No compiler, game process,
emulator or Ghidra import was used.

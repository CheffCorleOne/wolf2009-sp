# Mission systems: objectives, pickups, saves and transitions

Reviewed 2026-11-10. This page records the contracts of the mission-level
systems that trainyard needs after its first fight and that the trainyard →
mtw transition needs: the mission manager and objectives, item pickup and the
player inventory, autosave entities, stream transitions, and the player and
entity flags that brain tasks set. **Nothing here is implemented or observed at
runtime.** It is a contract, an engine mapping and a staged plan.

Sources: the hash-identified `Gamex86.dll` (`7cb1cbed…2d47`) and `Wolf2.exe`
(`e8604f64…`), both private copies; the original trainyard and mtw ENTS, the
trainyard BRAI dumps and the editor `.def` text of `assets.pk4`. Disassembly
came from the Android NDK `llvm-objdump`. No compiler, game process, Ghidra
import or Android action was used. Raw disassembly and scripts stay in ignored
`evidence/mission-systems/`.

Labels follow [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) and
[BRAIN-TASKS](BRAIN-TASKS.md):

- **Static** — read in the disassembly and checked at its call sites. No
  execution was observed. "Established" in a status column means Static.
- **Data** — read from original ENTS/BRAI/def bytes.
- **Inferred** — consistent with Static and Data facts, not closed.
- **Unknown** — no adequate evidence.

Names such as `MissionQuit` or `Finish` are working labels. Where the binary
carries its own name (debug strings such as `rvMissionManager::MissionQuit`,
`rvStreamTech::QueueMapTransition`, `idPlayer::ReadWriteSaveGame`), that name is
used. A label is not a recovered signature: arguments are described from the
pushes and the `this` register at the call sites.

## 1. Mission manager

### 1.1 Identity and storage (Static)

The singleton at `0x20876490` is `rvMissionManager` (RTTI vftable
`0x2055c244`, constructors `0x203ccdd0`/`0x203cd1c0`). It derives from `idClass`
and receives posted events. `mission_base` declares `spawnclass rvMission` with
`nospawn 1`: missions are not entities. The manager builds one mission record
per `def_missionNNN` key of `mission_library` (parser `0x203c7b10`).

| Manager member | Meaning |
|---|---|
| `+0x21d50` | Hash table from lowercased mission name to record. Lookup `0x203c2100(name)` returns null when absent. |
| `+0x39d5c` / `+0x39d68` | Active-mission list (count, array). `0x203bd210(record)` tests membership. |
| `+0x39d70` | Current (tracked) mission. |
| `+0x20f0` | **Save latch** byte. It is the global `0x20878580` that MissionGive writes (`0x20876490 + 0x20f0`). |
| `+0x2104` / `+0x20f8` | All mission records (array, count). |

| Mission record member | Def key or meaning (default) |
|---|---|
| `+0x4` | The mission's spawn-argument dictionary |
| `+0x18` | Mission name |
| `+0x8` | `objectiveGive` (`1`): an objective success gives an inactive mission first |
| `+0x9` | `singleObjective` (`1`): objectives run in order, one current objective |
| `+0xa` | `filler` (`0`): no autosave on give, no stats screen |
| `+0xb` | `failOnMapChange` (`0`) |
| `+0xc` | `failOnDeath` (`0`) |
| `+0xec` | `dontImmediatelyAssign` (`0`): give does not make it the current mission |
| `+0xed` | `isStoryMission` (`0`) |
| `+0xc8` | Mission state (§1.2) |
| `+0xcd` | Completed at least once |
| `+0xcf` | Replay: copied from `+0xcd` at give |
| `+0xdc` | Start time, −1 when not timed |
| `+0x3dc` / `+0x3e8` | Objective list (count, array); `0x203bc7c0(index)` bounds-checks |
| `+0x3f0` | Current objective index (`singleObjective` only), −1 when none |

The key-to-field pairs come from the parse order: each key read is followed by
the store of the previous key's value. The defaults are the literal default
strings passed with each key.

### 1.2 State values (Static)

| `+0xc8` | Meaning | Writers |
|---:|---|---|
| 0 | Finished (succeeded, or quit after completion) | Finish success path, MissionQuit |
| 1 | Unavailable (prerequisites not met) | reset `0x203c3d80`; periodic update `0x203c8c80` |
| 2 | Available | periodic update `0x203c8c80` when `0x203c4a10` passes |
| 3 | Active | Give, objective-target `forceReset`, MissionQuit |
| 4 | Succeeded, finish pending | `0x203c5eb0` |
| 5 | Failed | Failure `0x203ca680` |

The availability update `0x203c8c80` moves records between 1 and 2 using the
prerequisite test `0x203c4a10`. Each run schedules the next one 500–2499 ms
later (game LCG); Give and Finish force an immediate run. Its other callers
were not traced.

Objective state (`+0x9c` of an objective record):

| Value | Meaning | Readers |
|---:|---|---|
| 0 | Hidden, not yet given | activation `0x203be870` (0 → 1 plus a HUD event) |
| 1 | Active | **MissionIsObjectiveActive** (type 373) |
| 2 | Complete | objective success, completion check `0x203cc110`, `%s_status` |

At give (`0x203c91f0`) every objective starts at `objectiveHidden_N ? 0 : 1`.
In replay, objectives with `objectiveSkipOnReplay_N` are marked 2 and skipped.

### 1.3 Published global variables (Static, `0x203c1b10`)

After each change the manager writes two keys into the shared game dictionary
that SetGlobalVariable and the expression evaluator use ([BRAIN-TASKS §3](BRAIN-TASKS.md)):

| Key | Value |
|---|---|
| `<mission>_status` | −1 for state 0; 0 for states 1–2; for states ≥ 3 the current objective index + 1 (`singleObjective`) or 1 |
| `<mission>_complete` | `"1"` when `+0xcd` is set, else `"0"` |

`Trainyard_to_MTW` gates its sound pack on `(mission_trainyard_status == -1)`
(**Data**). The finished trainyard mission therefore publishes −1 before the
transition is queued, which matches the order in §3.

### 1.4 Give (Static, `0x203c9f50`)

Arguments: mission name, `silent` (suppress the new-objective HUD message),
`makeCurrent`. Callers: MissionGive task `(name, 0, 1)`, `rvTarget_Mission_Give`
`(name, 0, 1)`, objective success on an inactive mission `(name, 1, 1)`, and
MissionQuit/go-to-mission re-gives `(name, 1, 0)` after clearing the latch.

1. Unknown name → nothing except the end-of-call latch reset (step 9).
2. State must be 0, 1 or 2; otherwise return without touching the latch.
3. Copy `+0xcd` to the replay flag; state 3; clear `+0xce`; start time −1.
   When `0x203beff0` passes, the start time is the current time and the
   mission is registered with the stats object `0x20739e48`. **Inferred:**
   `0x203beff0` tests whether the mission lists the current map; it compares
   the mission's map keys with a name from the engine (`0x2061f098` slot
   `+0x1b8`).
4. Append to the active list if absent.
5. If `makeCurrent`, or no current mission exists: make it current unless
   `singleObjective` is off, `dontImmediatelyAssign` is set, or the manager
   flag `+0x39d7c` is set.
6. Build the objectives (`0x203c91f0(silent)`), publish globals, and fire the
   mission's `OnGive` activation list (§1.7).
7. With a player: a player call through an empty function (no effect in this
   build), and, with more than one active mission, a manager call
   `0x203bc340` (**Unknown**; the def keys `tip_title_multi_mission` /
   `tip_text_multi_mission` suggest a tip).
8. Restart the availability update.
9. **Autosave:** if not loading (`gameLocal+0x63f14` clear), the game state
   (`gameLocal+0x63f10`) is 4, the mission is not `filler`, the latch is set,
   and more than 10 000 ms have passed since the last map-load completion
   (`gameLocal+0x63c28`, written by `rvStreamTech` at the end of a load), the
   manager appends `contentManagerCommand save full` to the command buffer.
10. The latch is set back to 1.

### 1.5 Objective success (Static, `0x203cc450`, on the mission record)

Arguments: zero-based objective index, `silent`.

1. If the mission was completed before (`+0xcd`) it must be active, else return.
2. If `objectiveGive` is set and the mission is not active, give it silently.
3. Resolve the objective; an index outside the list falls back to the current
   objective in `singleObjective` mode, else nothing.
4. `singleObjective`: an objective already at state 2 returns immediately.
   Otherwise every earlier objective that is not complete is completed first,
   recursively and silently.
5. Set state 2. If `objectiveSetGlobalVar_N` is non-empty, assign it 1 in the
   shared dictionary. A non-empty complete text replaces the objective text.
6. `singleObjective`: advance the current index (−1 after the last), skipping
   `objectiveSkipOnReplay_N` objectives in replay. Not silent → HUD
   "objective complete / new objective" (`0x203c1ef0`), silent → HUD refresh.
   Fire objective activation lists `OnDone` for the old and `OnGive` for the
   new current objective, then publish globals.
   Without `singleObjective`: not silent → a temporary HUD message.
7. Completion check `0x203cc110`: when **every** objective has state 2, mark
   success (`0x203c5eb0`: state 4, set completed, queue `completeMessageN`
   temp messages) and call Finish `(name, 0)` (§1.6).
8. If the mission is still active and `singleObjective`, it becomes the
   tracked mission for the HUD.

### 1.6 Finish (Static, `0x203ca2e0`)

Arguments: mission name, `quit`. State below 3 prints "Inactive mission
finished" and returns.

State 4 (success):

1. For each `setGlobalVar` key of the mission's entityDef, assign the named
   variable 1.
2. Record the elapsed time when the mission was timed.
3. With a player, `quit` clear and not `filler`: a manager call `0x203bc1f0`
   (**Unknown**, presumably stats) and `openStatsScreen` posted to the player. The delay is 0 when the stats
   object flag `+0x2b0` is set (`statsScreenLoadImmediately`, §3), otherwise
   1000 ms with a fade to black. When the mission belongs to the current map,
   the game state becomes 6.
4. A non-empty `def_mission_arc_next` posts `nextArcMission` to the manager.
5. State 0.

States 3 and 5: reset (`0x203c3d80`: state 1, replay cleared, globals
published). Then, for all states: remove the mission's entries from the
manager list at `+0x3df18` (**Inferred:** mission timers), fire the mission
activation list (state 0 → `OnDone`, entry state 5 → `OnFail`, else `OnLose`),
publish globals, remove from the active list, and clear the current mission if
it was this one.

### 1.7 Activation lists (Static, `0x203bf300`, `0x203bf580`)

| Type | Mission list (key) | Type | Objective list (key) |
|---:|---|---:|---|
| 0 | `OnGive` (`def_activateOnGive*`) | 6 | `def_objectiveActivateOnGive_N` |
| 1 | `OnMapChange` | 7 | `def_objectiveActivateOnMapChange_N` |
| 2 | `OnDone` | 8 | `def_objectiveActivateOnDone_N` |
| 3 | `OnLose` | | |
| 4 | `OnFail` | | |
| 5 | `OnDeath` (player death, `0x203cbde0`) | | |

Any other type prints "Invalid mission activation list type". The
`mission_trainyard` def has no activation keys (**Data**); church and dtn do.
Player death runs list 5 for each active mission and fails missions with
`failOnDeath`.

### 1.8 Brain tasks and conditions

| ID | Node | Behavior | Status |
|---:|---|---|---|
| 200 | MissionGive | `mMission` (mission text), `saveOnMissionGive` (bool, default `true`): writes the bool to the save latch, then Give `(resolved name, silent 0, makeCurrent 1)`. | established |
| 216 | MissionObjectiveSuccess | `mMission`, `mObjectiveNumber` (int text, `1`), `mSilent` (bool, `false`): unknown mission → no-op; else objective success `(number − 1, silent)` on the record. | established |
| 373 | MissionIsObjectiveActive (condition) | Mission found **and** active **and** objective `number − 1` exists **and** its state is 1. Hidden (0) and complete (2) objectives give false. `NOT` inverts. | established |
| 204 | MissionFailure | Copies the resolved `mMission`, resolves the property at `+0x70`, calls Failure `(name, text, 0, 0)`. Failure sets state 5; its other effects were not read. | Enter established; Failure partial |

### 1.9 Map entities (Static)

| Class | Spawn | Activate |
|---|---|---|
| `rvTarget_Mission_Give` | `saveOnMissionGive` (default `1`) → `+0x308` | Writes `+0x308` to the save latch, Give `(mission, 0, 1)`, then ActivateTargets(activator). Trainyard ENTS 1730 has `saveonmissiongive 0`: **no autosave at the trainyard give.** |
| `rvTarget_MissionObjective_Success` | keys read at activation | If `globalExpression` is set it must evaluate true, and a local player must exist and be alive (player slot `+0x404`, health ≤ 0 → dead). Otherwise nothing happens, **not even ActivateTargets**. Then, if the mission exists: `forceReset` (default `0`) resets it to active through Finish `(name, 1)`; objective success `(objectiveNumber − 1, silent)`. `objectiveNumber` defaults to 0, so −1 selects the current objective. ActivateTargets(activator) follows even when the mission is unknown. |
| `rvTarget_Mission_Failure` | — | Failure `(mission, description, 0, 0)`, then ActivateTargets. |

## 2. Trainyard objectives (Data)

`mission_trainyard` has 10 objectives, `objectiveGive` and `singleObjective` on
by default, `canAbort 0`, `def_hubTransition Trainyard_to_MTW` and no
`continue_*` keys.

| Obj. | ENTS target (objectiveNumber) | Fired by |
|---:|---|---|
| 1 | 1719 `target_objective_success_1` | trigger 1676 at the trap door |
| 2 | 1726 `_2` | `grab_gun_trigger_relay` (after the MP40 pickup, §4) |
| 3 | 1727 `_3` | allies_contact_1 node 317 |
| 4 | 1720 `_4` | `trigger_relay_dynamite_door_explodes` |
| 5 | 1723 `_5` | `combat_trigger_17` |
| 6 | 1728 `_6` | allies_contact_1 node 69 |
| 7 | 1725 `_7` | not found in ENTS or trainyard BRAI |
| 8 | 1724 `_8` | `room_explode_trigger_relay` |
| 9 | 1729 `target_objective_success_nine` | allies_contact_1 node 158 |
| 10 | 1722 `target_objective_success_9` | the end video (§3) |

The entity named `target_objective_success_9` completes objective **10**. With
`singleObjective`, completing a later objective also completes every earlier
one, so a missing objective 7 trigger does not block the mission.
`train_mover` node 34 tests objective 7 with MissionIsObjectiveActive.

## 3. How trainyard ends

| Step | Evidence | Behavior |
|---|---|---|
| T1 | Data | ENTS 1755 `layer54_trigger_once_1` activates 1754 `trigger_fade` (`fadeTime 0.5`, `indefinite 1`), which targets 1745. |
| T2 | Static `0x2010cc90` | `rvTarget_ScaleformPlayVideo` activate: ignored while a video plays. With `g_skipCinematics` it activates its targets at once. Otherwise it applies `unloadToDPack`/`unloadCharacterPacks`/`unloadDetailPacks`/`endOfLevel`/`purgeInactiveZones`, unlocks the matching `ReplayCinematics` entry, stores the activator and starts `videos/trans_tra2mid`. `statsScreenLoadImmediately 1` sets the stats object flag `+0x2b0` and fades the player view to black. |
| T3 | Static `0x2010cc10` | Its Think waits until the video handle is gone or reports state 2 (finished), then activates its targets with the stored activator (or itself) and stops thinking. **Targets fire at video end, not at video start.** |
| T4 | Data + Static | Targets: `layer8_target_achievement_1` (`Newby`) and `target_objective_success_9`, which completes objective 10. |
| T5 | Static §1.5–1.6 | All objectives are complete: success, Finish. `<mission>_status` becomes −1 and `_complete` 1. Because the flag from T2 is set, `openStatsScreen` is posted with no delay; the game state becomes 6. |
| T6 | Static `0x202fd6e0` | Player `openStatsScreen` opens menu 8 on the Scaleform manager `0x20875db8`. |
| T7 | Static `0x203a5960` → `0x203b4b10` | The stats screen's `ScaleformMissionCompleteContinue` command calls MissionQuit `(stats mission, continue 1)`. |
| T8 | Static `0x203cab30` (§5.1) | MissionQuit queues `Trainyard_to_MTW` through `rvStreamTech::QueueMapTransition`. |
| T9 | Static `0x2039c170`, Data | The stream tech loads `mtw.mpk` and warps the player to `mtw_from_trainyard_respawn` (mtw ENTS 1648, (−2121,−3248,−250), angle 180). That respawn point is the target of `trigger_stream_dest` 1647 `mtw_from_trainyard`. |

**Inferred:** the stats mission at `0x208b0204` is the mission that just
finished; its writer was not traced. Trainyard's `trans_tra2mid` is not the
same as the cannery `endOfLevel` path; trainyard does not set `endOfLevel`.

## 4. Pickups and the inventory

### 4.1 `idItem` (Static)

The Wolf class follows the Raven `idItem` closely; the Quake 4 SDK
`idItem::Event_Trigger`, `Pickup` and respawn code has the same structure.

| Member / slot | Meaning |
|---|---|
| `+0x30d` | Can be picked up (armed) |
| touch `0x20092060` | Other must be `idPlayer`; slot `+0x254` CanPickup must pass; then slot `+0x2fc` Pickup(player) |
| activate `0x200920b0` | Hidden item: show, refresh the highlight, update visuals, return. Not armed and `triggerFirst`: arm, refresh highlight, return. Otherwise, a player activator gets Pickup(player). |
| CanPickup `0x20092b90` | Armed; local player present and not blocked by several player states (a live handle at `+0x76c`, others not identified); an inventory state combined with `noGraspPickup` also refuses. `inv_weapon` items are refused while the player's weapons are disabled (`+0x958` bit 3), or when the inventory already owns that weapon at full ammo (`0x2038a570`). `ammo_type` items are refused when that ammo is full. Otherwise an optional `+0x324` expression must hold. |
| Pickup `0x20091ed0` | GiveToPlayer must succeed; then the pickup event (`0x20045d90(7,0,0)`) and post-pickup handling `0x20091c80`. |
| GiveToPlayer core `0x20091a30` | slot `+0x2f8` → player GiveItem; `snd_acquire`; `unique` items set the global `<map>_<name>` to 1; **ActivateTargets(player)**; `def_collectibleSet` and `def_intel_data` registration. |
| post-pickup `0x20091c80` | Hide and go inactive. With `respawn_sp` 0 (the default) or `dropped`, and neither `no_respawn` nor `inv_carry`: post `<immediateremove>` after 5000 ms. With a non-zero `respawn_sp`, not `dropped` and not `no_respawn`: post `<respawnFx>` and `respawn` instead. |

Player `GiveItem` (`0x2031bb10`) refuses `inv_weapon` items while weapons are
disabled. It collects every `inv_*` key without the prefix and calls Give
(`0x2031b930`) for each. `health`/`maxhealth` raise health up to the cap; other
keys go to the player inventory (`player+0x53c`, `RvPlayerInventory`, slot
`+0x6c`).

`RvPlayerInventory` has 12 weapon slots (`+0x84`…). A slot's name is at
`+0x1c`; its slot `+0x8` reports ownership. **PlayerHasWeapon** (type 448)
calls inventory slot `+0x90` (`0x203879a0`), which returns the ownership flag
of the slot whose name matches. The name compare is the engine string compare
`0x20408460`, also used for mission names (case behavior not checked).

### 4.2 Trainyard pickup chain (Data + Static)

1. Animation frame 600 of `srp_trn_nml_2005_ee` triggers `activate_weapons`
   ([TRAINYARD-CHAIN §2.5](TRAINYARD-CHAIN.md)).
2. `activate_weapons` (relay) activates `weapon_mp_2` (`weapon_mp40`),
   `item_dynamite`, `ammo_mdl24_large` and the DLC flamethrower. All have
   `triggerFirst 1` and no `Hide` key, so the first activation **arms** them.
3. Touch on any armed item by the player picks it up. Its target
   `trigger_relay_weapon_pickup` (`idTrigger_Multi`, `wait 0`, `delay 0`)
   fires synchronously and activates the four items and the DLC tip with the
   player as activator. Each armed item is then picked up: **touching one item
   collects all four.** A relay re-entered in the same frame is ignored,
   because the trigger stores `time + 1` before firing (`0x20114883`).
4. `weapon_mp40`: `inv_weapon weapon_mp40`, `inv_ammo_mp40 32`, `inv_item 1`.
   `ammo_mdl24_large`: `inv_weapon weapon_grenade`, `inv_ammo_mdl24 3`.
   Both are refused while weapons are disabled.
5. allies_contact_1 nodes 293/294 wait for PlayerHasWeapon(`weapon_mp40`);
   node 203 probes `grab_gun_trigger_relay` (objective 2, HUD ammo, patrol) and
   `save` (§6).

**Unknown:** whether the item that started the pickup is given twice when the
relay re-activates it before it is hidden. The order (targets activated inside
GiveToPlayer, hide afterwards) is Static; the inventory result of a second
Give was not read. A native implementation must reproduce the order and record
the resulting ammo, not assume it.

### 4.3 Weapon enable (Static)

`idTarget_EnableLevelWeapons` activate and the player events
`enableWeapon`/`disableWeapon` are no-ops in this build. The only weapon gate
is player `+0x958` bit 3 (§7).

## 5. Transitions

### 5.1 MissionQuit (Static, `0x203cab30`)

Arguments: mission record (null → current mission), `continue`. Used by the
stats-screen continue (`continue 1`), the journal/menu quit (`0x203ac050`,
`continue 0`) and a console path (`0x202c3f40`).

1. `0x203beff0` must pass (Inferred: the mission lists the current map),
   else return.
2. Player cleanup: shroud powers are removed when the player flag `+0x95a`
   bit 0 is set or the map is `blacksun`; otherwise, when quitting
   (`continue 0`, not replay) and `removePowersOnQuit` is set, the four power
   slots are cleared. A Veil/shroud manager call follows (`0x208b7410`,
   **Unknown**). **Player `+0x958` bit 3 (weapons
   disabled), `+0x95a` bit 1 and `+0x957` bit 4 (locked) are cleared**,
   `r_shadows` is set to 1 and post-process globals are reset.
3. Key set: with `continue` and either `continue_map` or
   `def_continueTransition`, the `continue_*` keys; otherwise the `hub_*` keys
   and `def_hubTransition`.
4. The mission is set to state 3 and finished with `quit 1`. A completed
   mission goes back to state 0; an uncompleted one is given again
   `(name, 1, 0)` with the latch cleared, so it stays assigned.
5. `continue` stores the mission as `+0x39d78`.
6. The stream interface (`*0x205e6780`, `rvStreamTech`) receives the map pack,
   the player position and point, the detail and immediate-mode packs and the
   load screen.
7. A non-empty transition def is looked up as an entityDef and passed to
   `QueueMapTransition`. A missing def prints "Failed to find map transition
   def". Without a def the code appends `map <hub_map>` to the command buffer.

### 5.2 `QueueMapTransition` (Static, `0x203998c0`)

Clears queued state, then reads from the transition def: `mapPack` (required,
else "no map pack defined"), `detailPack*`, `immediateModePack*`, `soundPack*`
(each gated by its `soundPackGlobal*` expression), `warp` (stream slot `+0x48`
with a stored offset and the flags 1, 1) and `loadscreen`.

`Trainyard_to_MTW` (**Data**): `mapPack maps/game/mtw/mtw.mpk`, detail packs
`mtw_pak02/03/05`, immediate-mode `allies_kreisau_soldier_01`, sound pack
`scripted_voice_kc2_mt1.spk` gated by
`(mission_trainyard_status == -1) && (mission_filler_mt_bm_1_status != -1)`,
load screen `entering_midtown_west`, warp `mtw_from_trainyard_respawn`.

### 5.3 Load completion and what carries across (Static, `0x2039c170`)

At the end of a pack load the stream tech warps the **existing** local player
(and a vehicle the player occupies) to the named warp entity, or to a player
start for `_playerstart_`, offset by the stored delta unless the source asked
for no offset. It then writes the map-load completion time
(`gameLocal+0x63c28`), clears the loading flag and sets game state 4.

Carried across, because they are game-level objects that the map-pack swap
does not own: the player entity with its inventory, health and flags; the
mission manager; the shared global-variable dictionary. **Inferred** from the
warp of the same player pointer and from the singleton storage; the entity
teardown of the old pack was not read.

### 5.4 `rvTrigger_Stream` (Static)

Spawn `0x2011cde0` reads `mapPack`, `detailPack*`, `immediateModePack*`,
`warp`, `warpNoOffset`, `warpUseFacing`, `loadscreen`, `def_mapTransition`,
`confirm`, `facing`/`facingAngle` and `missionName`. A trigger with both a map
pack and a transition def is reported.

- **Touch** (`0x20119bb0`): player only, after the trigger's requirement check
  (slot `+0x304`). With `confirm` it shows a confirmation prompt (`#str_007220`
  and the mission title for `missionName`). Otherwise an optional facing test
  gates the start.
- **Activate** (`0x20117560`): starts the transition unless `confirm` is set and
  not yet accepted.
- **Start** (`0x20117580`): requires the stream tech. A direct `mapPack`
  without a def is queued; `detailPack*` are queued as type 4 and
  `immediateModePack*` are loaded; `warp` uses the trigger's own origin, so the
  player keeps the offset to an identical trigger in the destination;
  `loadscreen`; then QueueMapTransition for `def_mapTransition`; finally
  ActivateTargets(local player).

Trainyard has 17 `trigger_stream` entities that carry only `detailPack*` /
`immediateModePack*` (in-map streaming) and one `trigger_stream_dest`
(`intro_to_trainyard_dest`). None changes the map (**Data**).

### 5.5 `rvDetailPackVolume` (Static spawn, Inferred use)

Spawn `0x2038fc80` collects `detailPack*` keys (with a "too many detail packs"
limit), stores the `mins`/`maxs` box and registers itself with the stream tech
(slot `+0x70`, list at `+0x180`). Trainyard has 12 `detailpack_volume`
entities. **Inferred:** the stream tech keeps the listed packs resident while
the camera is inside the box. The selection code was not read.

## 6. Save and checkpoints

| Fact | Evidence | Status |
|---|---|---|
| `rvFuncSaveGame` (`save_game_entity`) activate: if the local player's health is above 0, sets player `+0x960` to now + 3500 ms and appends `contentManagerCommand save full` to the command buffer. Spawn reads nothing. | `0x2009e500` | established |
| Player `+0x960` is an **undying-until** time: idPlayer Damage (slot `+0x1e4`, `0x203130e0`) keeps health above 0 while now is before it, like the SetUndying byte `+0x5f9`. A save therefore cannot capture a death in progress. | `0x203130e0` | established |
| MissionGive / `rvTarget_Mission_Give` autosave: same command, gated by §1.4 step 9. | `0x203c9f50` | established |
| Death restart: `ScaleformDiedRestart` → restart menu → `contentManagerCommand loadLast`. | `0x203a59c0`, `0x203abff0` | established |
| `Wolf2.exe` handles `contentManagerCommand`: `save full` and `save partial` call one save routine (`0x1009c280`) with a partial flag; `load last` has its own path. The routine names slots `full%02d`/`part%02d` and stores map and mission text metadata. An engine state check (`0x10865c30` slot `+0x60`) can skip the save. | `Wolf2.exe` `0x1009d867`, `0x1009c280` | partial |
| The save is a whole-game savegame: the DLL has `idGameLocal::SaveGame`, `idGameLocal::InitGlobalsFromSaveGame` and per-class `ReadWriteSaveGame` for `idEntity`, `idPlayer`, `RvPlayerInventory`, `rvMissionManager` (missions, objectives, district data), `rvAIManager`, `idMover`, `idThread` and pending `idEvent`s. | strings | Inferred (call chain from the engine to the game API not traced) |
| Trainyard save points: ENTS 1687 `save`, used by allies_contact_1 nodes 198 (probe), 199 and 207, by `layer7_trigger_once_3` and by `combat_trigger_7`. | Data | — |

**What an autosave records.** Inferred from the above: everything the game
serializes, including entity state, brain networks owned by entities, pending
events, the mission manager and the global-variable dictionary. There is no
partial checkpoint record. `info_player_respawn` entities with `triggerFirst`
are a separate respawn-point system; their relation to SP death is Unknown,
since the SP death menu reloads the last save.

### 6.1 Native checkpoint design

A native checkpoint must restore the same observable state as a reload of a
full save. It should not be a position-only respawn.

- **Trigger points:** `rvFuncSaveGame` activation and MissionGive with the
  latch, with the exact gates above (health > 0; game state, loading, filler
  and the 10 s post-load window for MissionGive).
- **Undying window:** set the native player's undying-until time to now +
  3500 ms on `rvFuncSaveGame`, and honor it in native damage.
- **Snapshot:** build on the GPL `idGameLocal::SaveGame`/`idSaveGame` path
  (doom3 and the Quake 4 SDK both have it; Quake 4 adds a save type). Every
  native source owner (map runtime, brain networks and task state, AI shells,
  movers, doors, items, triggers, the mission manager, global variables,
  player inventory and source weapon) needs an explicit Save/Restore. While any
  live owner lacks one, the checkpoint is a **named refusal**, not a partial
  save.
- **Restore** (`loadLast`): reload the map through the ordinary path, restore
  the snapshot, then re-establish resource readiness from real observations
  ([AGENTS](../AGENTS.md)). A restored task must not re-fire once-only events.

## 7. Player and entity flags

| Flag | Writers | Consumers | Status |
|---|---|---|---|
| Entity `+0x158` bit 0 (NoTargetSet) | NoTargetSet; player and AI code | The `idEntity` debug dump (`0x2004b260`) names it `notarget`. Tested in AI-side code (`0x20138e80`, `0x20139c80`, `0x2013cf30`, idAI `0x201512c0`, `0x20162ff0`). | flag identity established; acquisition use Inferred (GPL `fl.notarget`) |
| Entity `+0x158` bit 3 (EnableDamage) | EnableDamage; many spawns | Named `takedamage` in the same dump. `idEntity` Damage (slot `+0x1e4`, `0x20048920`), `idActor` (`0x20004400`) and `idPlayer` (`0x203130e0`) Damage return early when it is clear. | established (GPL `fl.takedamage`) |
| Entity `+0x158` bit 4 | Show/Hide | `hidden`; idItem activate tests it | established |
| Entity `+0x159` bit 7 (SetUsable) | SetUsable; spawn and state code of animated, searchable and other classes (`0x2009f760`–`0x200a48d0` and others) | The player's focus classification (`0x203142a0`) treats an entity as a use target only with this bit; searchable highlight (`0x2009e2e0`) requires it. | established for focus and highlight; full use-key path Inferred |
| Player `+0x957` bit 4 (Lock/UnlockPlayer) | LockPlayer/UnlockPlayer and about 20 other DLL sites; MissionQuit and go-to-mission clear it | The input handler `0x203007a0` skips the crouch toggle; `0x20306c90` and the focus code `0x203142a0` test it. | partial: the full set of suppressed controls is Inferred |
| Player `+0x958` bit 3 (EnablePlayerWeapons, inverted) | EnablePlayerWeapons; player spawn when `weapons_disabled` is set (`0x2030c81e`); pendant code (`0x203da200`, `0x203da2a0`) and others; MissionQuit clears it | The input handler clears the attack fields of the input state (`0x20300bfc`). CanPickup and GiveItem refuse `inv_weapon` items. Inventory slot `+0x3c` (`0x2038d360`) treats it as "weapons unavailable". | established |
| Player `+0x5f9` (SetUndying) | SetUndying | Player Damage keeps health above 0 | established |
| Player `+0x960` | `rvFuncSaveGame` | Player Damage undying-until time | established |

In trainyard, shake_train node 31 and allies_contact_1 node 5 run
EnablePlayerWeapons with its default `true`: both **enable** weapons (**Data**).
The disabled state at mission start therefore comes from elsewhere. The start
entity 367 carries `SetWeaponNone 1` and `mission mission_trainyard`; their
consumers were not found under those exact names (**Unknown**).

## 8. Engine mapping

GPL idTech 4 (`doom3/neo/game`) and the Raven Quake 4 SDK
(`doom3/neo/quake4`, `raven`) in the pinned upstream may be reused under their
own licenses (see [AGENTS](../AGENTS.md) and [PROVENANCE](PROVENANCE.json)).
Matching structure is not proof of identical behavior.

| Wolf system | Native capability needed | Closest upstream | Existing code |
|---|---|---|---|
| `rvMissionManager`, records, objectives | Portable mission model built from the `mission_library` defs: states, flags, objectives, activation lists, globals; native host for HUD events | No equivalent manager; Quake 4 `idPlayer::GiveObjective/CompleteObjective/FailObjective` hold a flat objective list | none |
| Mission globals | Write into the shared dictionary SetGlobalVariable uses | — | SetGlobalVariable 220 (runtime-tested) |
| MissionGive 200, Objective 216, 373, targets | Brain handlers and entity classes over the model | Quake 4 `idTarget_*` activate pattern | brain kernel, Use routing |
| Stats screen and continue | A non-UI "mission complete continue" step that calls MissionQuit (UI deferred) | — | none |
| `rvTarget_ScaleformPlayVideo` | Video owner with a real end observation, or a declared skip profile equal to `g_skipCinematics` | doom3 `idTarget_*` + cinematic | none |
| `idItem`, `idTrigger_Multi` relay | Item arm/show/touch/pickup with synchronous ActivateTargets | Quake 4 `idItem` (`Item.cpp`), `idTrigger_Multi` | trigger slices only |
| `RvPlayerInventory` | 12 named weapon slots, ammo pools, `inv_*` Give | doom3/Quake 4 `idInventory::Give`, `idPlayer::GiveItem` | `NativeSourceWeapon` (MP40 view, ammo, fire), `wolf2009DevEquipMP40` dev grant |
| PlayerHasWeapon 448 | Inventory ownership query | `idInventory` weapon bits | none |
| Weapon gate (`+0x958` bit 3) | Native player flag read by `Wolf2009_UpdateSourceWeapon(…, enabled)` and by pickup | Quake 4 `idPlayer::Event_DisableWeapon` (`weaponEnabled`) | `enabled` parameter exists |
| Lock (`+0x957` bit 4) | Native player input gate | doom3 `idPlayer` `noclip`/cinematic input suppression | source input patch |
| `takedamage`/`notarget`/usable | Map to `fl.takedamage`, `fl.notarget` and a Wolf usable bit | GPL `idEntity::fl` | native damage path |
| `rvFuncSaveGame`, autosave, `loadLast` | Checkpoint controller over the GPL savegame, with refusals | doom3 `idGameLocal::SaveGame`, Quake 4 `SaveGame(f, saveType)` | legacy save record only |
| `rvTrigger_Stream`, transition defs | Native map change with warp to a named entity and persistent player state | doom3 `idTarget_EndLevel`, `idPlayer::SavePersistantInfo`/`RestorePersistantInfo` | source map runtime |
| `rvDetailPackVolume`, in-map stream triggers | Stream-pack residency (can stay "all packs resident" while memory allows) | — | MPK/SPK loaders |

## 9. Staged native implementation

Each stage runs through the ordinary native lifecycle (spawn, Think, events,
teardown) under a named source profile and records refusals for everything
outside it.

| Stage | Profile (proposed) | Work | Acceptance tests |
|---:|---|---|---|
| M1 | portable `SourceMissionModelV1` | Parse `mission_library` and mission defs into records with §1.1 fields and defaults; states, objective states, Give, objective success, completion, Finish, globals and activation-list typing. No HUD. | Table tests on original defs: trainyard has 10 objectives, `singleObjective` and `objectiveGive` on, `filler` off. Give from states 0/1/2 succeeds and from 3 is ignored. Success of objective 10 completes 1–9 silently and finishes the mission: `mission_trainyard_status` −1, `_complete` 1. Hidden objectives start 0. MissionIsObjectiveActive true only for state 1. |
| M2 | native `SourceMissionTargetsV1` | MissionGive 200, MissionObjectiveSuccess 216, MissionIsObjectiveActive 373, `rvTarget_Mission_Give`, `rvTarget_MissionObjective_Success` with the dead-player and `globalExpression` gates. | Greeter probe 72 gives `mission_trainyard` with the latch 0 and **no** save request. Trigger 1676 completes objective 1 once; a second touch does nothing. A dead player's objective target fires nothing. |
| M3 | native `SourcePickupInventoryV1` | Inventory with 12 named slots and ammo; `idItem` arm/show/touch/CanPickup/Pickup/hide/remove; the weapon gate; PlayerHasWeapon 448; the MP40 grant feeds the existing source weapon controller instead of the dev command. | Before `activate_weapons`, touching the MP40 does nothing. After it, one touch collects all four items through the relay, in relay order, and the resulting ammo totals are recorded. With weapons disabled, the MP40 and grenades are refused and dynamite is accepted. allies 293/294 change false → true. Items are removed 5000 ms later. Teardown leaves no item owner. |
| M4 | native `SourcePlayerFlagsV1` | Lock, weapons-disabled, takedamage, notarget, usable and undying-until on the native player and entities. | Weapon fire refused between EnablePlayerWeapons false and true. Crouch toggle refused while locked. Damage ignored without takedamage. A non-usable greeter produces no Use. |
| M5 | native `TrainyardEndV1` | `trigger_fade`, the video target with a real end observation (or an explicit skip profile), objective 10, Finish, a headless "continue" step, MissionQuit, QueueMapTransition for `Trainyard_to_MTW`. | Targets fire after the video ends, not at start. MissionQuit clears the lock and weapon bits. The transition request carries `mtw.mpk`, the three detail packs, the gated VO pack (gate true) and warp `mtw_from_trainyard_respawn`. |
| M6 | native `SourceMapTransitionV1` | Load the destination map with the same player, inventory, flags and globals; warp to the named entity; record the load-completion time. | mtw starts with the player at (−2121,−3248,−250) facing 180, holding the MP40 and its ammo, with `mission_trainyard_status` −1. A second transition request during a load is refused. |
| M7 | native `SourceCheckpointV1` | `rvFuncSaveGame`, the MissionGive autosave gate, the undying window, Save/Restore for native owners, `loadLast` after death. | `save` at allies node 207 produces one snapshot. Death within 3.5 s of it is prevented. After death, `loadLast` restores player, inventory, objectives, brain task states and entity removal/visibility as saved, without re-firing once-only events. Any owner without Save/Restore makes the save an explicit refusal. |
| M8 | `SourceStreamPacksV1` | In-map `trigger_stream` and detail-pack volumes as declared residency, without changing maps. | Entering a stream trigger requests its packs once; teardown releases them. |

Order: M1 → M2 → M3 → M4 can proceed while the first fight is built, because
M3 and M4 unblock the patrol encounter (PlayerHasWeapon, the weapon gate). M5
and M6 close trainyard → mtw. M7 should come before long play sessions. M8 can
stay a residency stub while every pack fits in memory.

## 10. Unknowns

- The writer of the stats mission (`0x208b0204`) and the stats menu's other
  commands.
- Whether the item that starts the relay is given twice (§4.2).
- The consumers of `SetWeaponNone` and of `mission` on `info_player_start`; how
  a new campaign gives `mission_trainyard` before greeter probe 72.
- Failure (`0x203ca680`) beyond setting state 5, and the fail screens
  (`fail_restart_*`, `fail_abandon_*`, `missionfailstage2`).
- The complete set of controls the lock bit suppresses, and the
  `+0x957`/`+0x958` bits used by AI code (shared `idActor` offsets).
- The engine-side save chain (session → game API) and the save file layout; what
  the `partial` save variant omits.
- The stored warp offset used by QueueMapTransition for mission-flow warps.
- Detail-pack volume residency rules and the old map pack's entity teardown.
- `info_player_respawn` (`triggerFirst`, `safehouse`) in SP: death reloads the
  last save, so its role is open.
- Objective 7's trigger in trainyard.

## Reproduce

Read-only inputs: the installed trainyard and mtw MPKs (normalized ENTS from
`evidence/campaign`), the editor `.def` text of `assets.pk4`, the BRAI dumps of
`evidence/trainyard-chain`, and private copies of `Gamex86.dll` and `Wolf2.exe`
with the hashes above. Private scripts in ignored
`evidence/mission-systems/scripts/` dump procedures and contexts from the NDK
`llvm-objdump` text, read strings, query ENTS and dump `Wolf2.exe` procedures.
They reuse the `evidence/ai-actor` helpers for annotation, vtables and event
tables.

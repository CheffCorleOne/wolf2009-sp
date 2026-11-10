# Midtown West: from the trainyard warp to the digsite transition

Research plan for the first hub. Reviewed 2026-11-10. This page maps what the
original data and bounded static evidence say happens in `mtw` between the
`Trainyard_to_MTW` warp and the `MTW_to_Digsite` transition. **Nothing here is
implemented or observed at runtime by this work.** It is a contract and
handler plan, not a playability claim.

Sources: the owner's `mtw.mpk` (ENTS with 5,659 entities and 133 BRAI
resources), the editor `.def` text of `assets.pk4`, and the hash-identified
`Gamex86.dll` (`7cb1cbed…2d47`, private copy only). Disassembly came from the
Android NDK `llvm-objdump`. No compiler, game process, Ghidra import or
Android action was used. Raw dumps, disassembly and scripts stay in ignored
`evidence/mtw-chain/`.

Labels follow [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md):

- **Data**: read directly from original ENTS/BRAI/def bytes.
- **Static**: closed by disassembly of the identified DLL and checked at its
  call sites. No execution was observed.
- **Inferred**: consistent with data and static facts, not closed.
- **Unknown**: no adequate evidence yet.

Node references are `ID/type`; "P" is a probe output, "N" a normal output,
`prim` the source task's embedded condition. Entity references are ENTS ids.
Expressions use the evaluator of [BRAIN-TASKS §3](BRAIN-TASKS.md)
(right-associative levels, missing names read as 0). Mission state values and
the published `<mission>_status`/`<mission>_complete` globals are those of
[MISSION-SYSTEMS §1](MISSION-SYSTEMS.md).

## 1. Summary

Midtown West is not one scripted chain like trainyard. Its story runs through
four mechanisms that trainyard does not use:

1. **Scenario deferral.** 2,475 of the 5,659 entities carry `scenario 1`. The
   map load does not spawn them. A `target_scenario` (`rvScenario`) spawns
   its set later, when a trigger or a `trigger_choice` activates it (§3.1).
2. **Expression gates.** `spawnExpression` (570 entities), `showExpression`
   (27) and `globalExpression` (26 entities, plus the numbered
   `globalExpression<N>`/`forceExpression<N>` keys of `trigger_choice`) read
   the shared global dictionary that the mission manager publishes (§3.2).
3. **Filler missions.** "Visit the Black Market" and "Find Erik Engle" are
   given and closed by the manager's filler update from their `fillerGlobal`
   expressions, not by a MissionGive node (§3.4).
4. **Player action keys and hub brains.** Contacts react to the player's
   action key (`OnActionKeyed`) and coordinate through `UseAllTargets`, local
   variables and root enabling (§3.5).

The story path to digsite is (**Data**, order Inferred where noted):

| Step | What happens | Gate |
|---|---|---|
| A | Warp to 1648; tutorial guide 2619 spawns 49 units ahead and locks the player | `spawnExpression` true at arrival |
| B | Arrival trigger 2629 → `tutorialStart`; guide speaks, opens the journal tutorial, then `tutorialDone`; filler update; player unlocked | journal closed (`IsMenuOpen` false) |
| C | Player enters the black market; scenarios `bm_pre_digsite` and `SHelias_setup_scenario` spawn the collector 2618 | trigger_multiple 2600/2608/2609 |
| D | Action key on the collector → purchase locker → objective success of `mission_filler_mt_bm_1` → autosave command | locker closed |
| E | Black-market filler finishes; the route gate 2702 hides; filler `mission_filler_mt_kc_1` is given; trigger 2630 sends the guide to the safe house | `mission_filler_mt_bm_1_status == -1` |
| F | Player opens the safe-house door 2583 and goes down; scenarios `mtw_kc_safehouse_setup1(_front)` spawn Caroline, Erik and the briefing runner 3559 | trigger_multiple 2645, 2581 |
| G | Action key on Caroline or Erik → `briefing` → MissionObjectiveSuccess `mission_filler_mt_kc_1`, **MissionGive `mission_digsite`**, guide removed, journal tutorial trigger | `mission_digsite_status == 0` |
| H | Leaving the safe house: escort scenario; zone A forces `pre_digsite_zone_a` (16 enemy actors); door 2591 unlocks | `mission_digsite_status` ≥ 1 |
| I | Player touches `trigger_stream` 1626 `mtw_to_dig`, confirms, `MTW_to_Digsite` is queued | `mission_digsite_complete == 0`, confirm |

## 2. Arrival

| Fact | Value | Label |
|---|---|---|
| Warp | `Trainyard_to_MTW` → 1648 `mtw_from_trainyard_respawn` (`info_player_respawn`, (−2121,−3248,−250), angle 180). The transition def also loads detail packs `mtw_pak02/03/05`, immediate-mode `allies_kreisau_soldier_01` and the gated `scripted_voice_kc2_mt1.spk`. | Data; MISSION-SYSTEMS §5.2 |
| Stream destination | 1647 `mtw_from_trainyard` (`trigger_stream_dest`) lists the same packs and targets 1648. Whether the warp activates it is Unknown. | Data |
| Globals at arrival | `mission_trainyard_status` −1, `mission_trainyard_complete` 1, `mission_digsite_status` 0. `mission_filler_mt_bm_1_status` is 0 or, if the filler update already ran in trainyard, 1. Both satisfy every mtw gate below. | Inferred from MISSION-SYSTEMS §1.3 and §3.4 |
| Live brains at map load | 464 entities carry a `brain` key. 446 pass their `spawnExpression` at arrival, but 442 of those are scenario-deferred. Only four spawn with the map: 827 `mousescript` (`mtw/mouse`, an ambient trigger_facing reaction), 2619 `tutorial_guy` and two path corners 3544/3546 that carry a focus brain. | Data + Static §3.1 |
| Kreisau contact | 2619 `tutorial_guy` (`mtw_kc_tutorial_guy` → `allies_kreisau_soldier_01`, `idAI`), (−2121,−3199,−249), `ignoreEnemies 1`, `speakStopRange 1000`, brain `mtw/tutorial_guy` (101 nodes, roots 1, 6, 8, 10, 39, 97), `spawnExpression` `(mission_trainyard_status == -1) && (mission_filler_mt_bm_1_status != -1) && (mission_digsite_status == 0)` | Data |
| Arrival trigger | 2629 `layer3_trigger_once_1229126297` at (−2128,−3243,−212), 38 units above the warp point. Targets `tutorial_to_bm` (2621 relay → 2623 `target_brainuse`, `useKey tutorialStart`) and 2637 `target_setglobalvariable` (`mtw_entered = 1`, `spawnExpression mtw_entered != 1`). | Data; that the warped player is inside its HKXS box is Inferred |

No `target_mission_give` exists in mtw. The first mission the hub gives is the
digsite story mission, from brain node 11 of `digsite_briefing_runner`.

## 3. Hub systems recovered in this pass

### 3.1 Scenarios (Static)

**Deferral at map load.** The map-entity inhibit check (Doom 3 lineage
`InhibitEntitySpawn`, reached from the spawn drain) tests, in order:
difficulty filters, `minMachineSpec`, `si_entityFilter`, then `scenario`. A
true `scenario` key with the global scenario-spawn flag (`0x2073874c`) clear
makes the check copy the entity's spawn dictionary into the scenario manager
(singleton `0x206e6618`, `0x200f0840`, at most 3,000 stored dictionaries,
error "Out of scenario entity arg storage!") and inhibit the spawn. Only
after that branch are `spawnExpression`, `spawnMission` /
`spawnMissionObjective`, `unique`, `lightGroupParent` and the flamethrower DLC
flag tested. A deferred entity's `spawnExpression` is therefore evaluated when
its scenario spawns it, not at map load.

**`rvScenario` spawn (`0x200f1bc0`).** Reads `safehouse`, `resetOnDeath`
(default 1), `respawnOnDeath`, `fireAndForget`, `lockEntityPercent`,
`lockAIPercent`, `autoUse`, `autoResetAfterDuration` (default −1),
`zoneGroup` and `soundPack`, then registers with the manager (`0x200f12d0`).

**Activation.** The `activate` event (`0x200eead0`) only posts
`spawnEntities` with zero delay. Its handler (`0x200f2760`) runs the spawn
pass `0x200f1f50` and then a virtual call on the scenario. The spawn pass:

1. Returns at once when the scenario is already active (`+0x380`).
2. Marks it active, sets the scenario-spawn flag and requests the
   `soundPack` from the stream interface (type 8).
3. For each stored dictionary of this scenario, last to first: copies it,
   renames it `name_<n>` if the name is taken, adds `frame` when the scenario
   has one, spawns it through the normal entity spawn, posts `<findTargets>`
   to the new entity, and posts `activate` with the scenario as activator when
   the dictionary has `scenario_activate 1`. `scenario_persistent 1` removes
   the dictionary from the scenario's list, so that entity is spawned only
   once. Otherwise the spawned handle is kept for reset. AI and breakables
   count towards the `lockAIPercent`/`lockEntityPercent` bookkeeping.
4. Clears the scenario-spawn flag. With `fireAndForget` the kept handles are
   dropped. Otherwise `autoResetAfterDuration` ≥ 0 posts `autoreset`,
   `autoUse` calls a virtual with key `1`, and `lockManual` is read.

`reset` (`0x200f2780`) cancels a pending `spawnEntities`, and for an active
scenario with `respawnOnDeath` or `fireAndForget` it runs a cleanup
(`0x200f0510`), then respawns when `fireAndForget` is set.
`target_scenario_reset` (`rvTarget_ResetScenario`, 10 in mtw) presumably
posts it. `target_scenario_link` (`rvTarget_ScenarioLink`, 4) activates its
targets through the base path (`0x2003ef10`).

**Data.** 127 scenarios in mtw own 2,605 target links to deferred entities;
118 entities are owned by more than one scenario (the black-market and
safe-house casts per story phase). 12 deferred entities belong to no
scenario. How the manager assigns stored dictionaries to scenarios is
**Inferred** from the target keys. The assignment code (`0x200f12d0`,
`0x200ef530`, `0x200f11b0`) was not read.

### 3.2 Expression gates (Static)

| Key | Where | Behavior |
|---|---|---|
| `spawnExpression` | inhibit check | False inhibits the spawn, unless the integer of the cvar at `0x205dcb9c` is nonzero (cvar name not resolved). Evaluated once, at spawn. |
| `showExpression` | `idEntity` spawn `0x2005c09e` | False posts `hide` at spawn. The entity is then registered with every mission record whose `<name>_status` or `<name>_complete` occurs as a substring of the text (`0x203c2040`). After each publish of that mission's globals (`0x203c1b10` → `0x203bf7e0`) every live registered entity re-evaluates and receives `show` or `hide`. Expressions on other variables are not re-evaluated. |
| `globalExpression` on triggers | `idTrigger` spawn stores it; requirement check `+0x304` (`0x20113d70`) | The trigger fires only with a local player, inside its difficulty and health ranges, when two flag bytes are clear, and when a non-empty `globalExpression` is true. |
| `globalExpression` on doors | `0x200b53a0` | A door is locked when a non-empty `globalExpression` is false **or** its `locked` key is nonzero. Callers were not enumerated. |
| `trigger_choice` keys | §3.3 | Per-target `globalExpression<N>`, `forceExpression<N>`. |

`target_setglobalvariable` (`rvTarget_SetGlobalVariable`, 8 in mtw) activate
(`0x2010afb0`): assigns `expression` to `var` in the shared dictionary with
the evaluator, then activates its targets. It does not publish mission
globals, so it does not re-evaluate `showExpression`.

### 3.3 `trigger_choice` (`rvTrigger_Choice`, Static)

Spawn (`0x2011d9e0`) reads `remove`, `noRepeatAll`, `onceOnlyAll`,
`persistent` and `zoneInactiveTime`. The target builder (`0x2011f6b0`)
records, per `target<N>`, `chance<N>` (default 1.0), `noRepeat<N>`,
`onceOnly<N>`, `globalExpression<N>` and `forceExpression<N>`.

The trigger action (`+0x308`, `0x2011a500`) walks the targets in list order:

1. A disabled (`onceOnly` already used) or missing target fails.
2. A non-empty `forceExpression` that is true selects that target
   **immediately**; the first such target in list order wins.
3. The previously selected target fails when `noRepeatAll` or its
   `noRepeat` is set.
4. A non-empty `globalExpression` that is false fails.
5. Otherwise its chance joins the weighted pool.

Without a forced target, the zone-inactive-timer check (`0x20117b30`) and
the scenario-lock-percentage check (`0x20117bf0`) must pass. A weighted
random pick follows (game LCG). The selected target is activated with the
original activator. `onceOnlyAll` or `onceOnly<N>` disables it afterwards.
The status strings name the outcomes: "Pass", "Fail: Previously disabled",
"Fail: No repeats", "Fail: Global expression", "Pass: Force expression",
"Fail: Zone inactive timers", "Fail: Scenario lock percentages",
"Fail: No valid targets". The bodies of the two helper checks were not read.

Evaluated on original data (portable evaluator, list order as stored):

| Choice | At arrival | After MissionGive digsite |
|---|---|---|
| 2599 `layer0_trigger_choice_2` (black market) | forced `bm_pre_digsite` | forced `bm_pre_digsite` |
| 2597 / 2644 `layer0_trigger_choice_1/3` (safe house) | forced `mtw_kc_safehouse_setup1` / `_front` | same |
| 1663 `layer21_trigger_choice_1` (zone A) | weighted pool `pre_chruch_zone_a`, `Patrol_NorthWest` | forced `pre_digsite_zone_a` |
| 1664 `layer21_trigger_choice_2` (zone D) | forced `pre_digsite_zone_d_2` (empty) | same |
| 1665 `layer21_trigger_choice_3` (zone C) | forced `mtw_side_mission` (`mission_warehouse_status == 0`) | same |
| 1666 `layer21_trigger_choice_4` (zone E) | forced `zone_e_empty` | same |

**Data:** zone A before the give can therefore pick a church-phase or patrol
scenario. Whether the player reaches zone A before the briefing is
Unknown; the guide's route to the safe house runs through zones E/D.

### 3.4 Filler missions (Static)

Filler missions (`filler 1`) carry `fillerGlobal` (the def key; the JSON
column `fillerCondition` holds its text), `fillerPriority`,
`fillerForceSelect` and `showFillerSuccess`.

- **Request.** `target_mission_filler_update` activate (`0x203bc670`) and
  every mission-global publish (`0x203c1b10`) store `now + 1` in manager
  `+0x2140` unless a request is pending or updates are blocked
  (`0x208b020d`). The tutorial's node 91 targets 2604 `update_fillers`.
- **Update** (`0x203cc890`). Postponed by one frame while a modal Scaleform
  menu is open or the player is in one of three states. Otherwise, under a
  re-entrancy byte: pass 1 evaluates each filler's `fillerGlobal`. An
  **active** filler whose expression became false gets objective success
  `(0, not silent)` when `showFillerSuccess` is set, else Finish with quit.
  Pass 2 **gives** every inactive filler whose expression is true
  (`Give(name, 0, makeCurrent)`). The `makeCurrent` byte (`+0x2b0`) is
  presumably `fillerForceSelect` (Inferred).

| Filler | `fillerGlobal` | Given | Closed |
|---|---|---|---|
| `mission_filler_mt_bm_1` "Visit the Black Market" | `(mission_trainyard_complete == 1) && (mission_filler_mt_bm_1_complete == 0)` | first filler update after trainyard completes (possibly still in trainyard) | collector node 9 MissionObjectiveSuccess (objective 1 of 1) |
| `mission_filler_mt_kc_1` "Find Erik Engle" | `(mission_filler_mt_bm_1_complete == 1) && (mission_digsite_status == 0)` | the update that follows the black-market finish | briefing node 46 (and Caroline node 17) objective success |

### 3.5 Player action key on an AI (Static, partial)

The player's use handler (`0x20318130`) dispatches on the player's focus
type (`player+0x1a8c`). Type 7 on a live focus entity calls the `idAI`
action-key method `0x201ac930`. That method forwards to a delegate
(`ai+0x3f7c`) when one is set. Otherwise it latches the `ActionKeyed` byte on
the bound network and on every extra network (`0x201dac90`). An AI without a
network but with flag `ai+0x95d & 0x10` says `lipsync_bsy` instead. The
focus classifier (`0x203142a0`) assigns type 7 when `0x20313e00` accepts the
entity; that test was not read. `OnActionKeyed` (type 4) is a root whose
behavior is its primary condition `ActionKeyed` (71), which reads that byte
([BRAIN-TASKS §4](BRAIN-TASKS.md)).

### 3.6 Zones

`rvZone` activate (`0x201010d0`) activates its own zone and deactivates
every other zone that is not marked manual. `rvZoneDeactivator` deactivates
its zone, or logs "Attempting to manually deactivate a zone … not set up for
manual". The activation and deactivation bodies were not read. Zone
triggers in mtw pair a `zonefloodentity` with the zone `trigger_choice`.

## 4. Ordered story chain

### 4.1 Arrival and the guide (A, B)

Graph `mtw/tutorial_guy`, owner 2619.

| Step | Original data | Status of the pieces |
|---|---|---|
| A1 | 8/1 OnSpawn [prim Exists] → 40/289 EnableRootTask(` 39`, false) → 127/494 EnableChatter(false) P→ 65/430 EnableStepAside(false). 40 P→ **63/185 LockPlayer** | OnSpawn rt; 289, 185 contract; 494, 430 unknown (subsystem) |
| B1 | Player in 2629 → 2621 `tutorial_to_bm` (`trigger_relay`) → 2623 `target_brainuse` `tutorialStart` → 2619. 2629 also activates 2637 (`mtw_entered = 1`). | trigger volume: contract (`idTrigger_Multi`); relay/brainuse: rt for literal profiles only |
| B2 | 10/3 OnUsed `tutorialStart` → 17/19 VO-in `scripted_voice_kc2_mt1` (P→ 14/52 WatchTarget 3000 ms) → 16/12 Say `lipsync_mt1_0005_kc2` → 18/51 Animate `st1_mg1_nml_conversation` → 22/30 loop until SayFinished(0005) → 23/30 Wait 500 ms | 19 rt; 12, 51 contract; 52 unknown |
| B3 | 23 → **11/469 StartJournalTutorial** → 12/30 Wait 1000 ms [prim AND **NOT IsMenuOpen**] → 13/68 UseTarget `Me` `tutorialDone` | 469, 467 contract; journal UI deferred |
| B4 | 1/3 OnUsed `tutorialDone` → 2/19 VO-in → 3/12 Say `lipsync_mt1_0051_kc2` (P→ 89/52) → 93/51 → 4/51 → 77/51 [SayFinished 0051] → **91/68 UseTarget `update_fillers`** (2604) P→ **64/186 UnlockPlayer**, P→ 66/430 → 51/490 VO-out | 186 contract; 2604 class: contract (§3.4) |
| B5 | 51 → 88/30 → 100 or 101/426 GoToAndFaceTargetNoStop `tutorial_guy_dest1` (2620), face `tutorial_facing` (2634) → 25/30 → reminder loop 28/30 with ReachedLocation(`tutorial_bm_loc` 2635, Player), CheckLocalVariable `remindTime < @time`, `!shown`, SetLocalVariable `remindTime = @time + 6000` (27), `random = random + 1` and `random % 3 == …` (35) to pick nag lines 0053/0054/0055 | 426, 21, 420 unknown (subsystem); 100, 195, 196 contract |
| B6 | 2638 trigger (`triggerWithSelf 1`, `useKey wait`) near (−1914,−2995) → 97/3 OnUsed `wait` → 62/243 GoToAndFaceTarget dest1 [NOT `shown`] or 99 → 102/51 idle loop | 243 unknown |

### 4.2 Black market (C, D)

| Step | Original data |
|---|---|
| C1 | Player enters 2600/2608/2609 (`trigger_multiple`, `wait` 1/10/10) → 2599 `layer0_trigger_choice_2` (forced `bm_pre_digsite` 3519, 36 targets) and 2580 `SHelias_setup_scenario` (`safehouse 1`; targets `purchasing_agent_first`, `container_gold_1`, `purchasing_agent`; `shbm_doorman` and `shbm_doorman_brainuse` are named but absent) |
| C2 | The scenario spawns 2618 `purchasing_agent_first` (`allies_black_market_pa`, brain `mtw/bm_collector_first`, `spawnExpression (mission_filler_mt_bm_1_status >= 0)`), the guards 3520/3574 (`Hide 1`, `noUnhideOnUse 1`), the Weiss brothers 3521/3522 and their focus, animation and conversation runners |
| D1 | `bm_collector_first`: 1/1 OnSpawn → 2/33 SetScriptedManual → 3/188 EndBranch. 4/4 **OnActionKeyed** → 5/290 EnableAllRootTasks(false) → 6/52 WatchTarget(−1) → 7/19 VO-in (owner default) → 11/51 Animate `st1_nil_nml_talk` → equal-weight {12,13,16,17,18}/12 Say `@cn_line1…5` (the collector's def keys `cn_line*`, resolved by §1.3 step 4 of BRAIN-TASKS) → 19/30 → 21/490 VO-out |
| D2 | 21 → **10/293 OpenPurchaseLocker** (`mAmmoOnly` default false) → [CheckGlobalVariable `mission_filler_mt_bm_1_status != -1`] 14/30 Wait 500 [NOT IsMenuOpen] → 15/30 Wait 1000 → **9/216 MissionObjectiveSuccess `mission_filler_mt_bm_1`** → 20/456 ExecConsoleCommand `contentManagerCommand save full` → 8/290 EnableAllRootTasks → 2. Otherwise 10 → 22 → 20. |
| D3 | Objective 1 of 1 complete → success → Finish: `mission_filler_mt_bm_1_status` −1, `_complete` 1 (MISSION-SYSTEMS §1.5–1.6). The publish requests a filler update, which gives `mission_filler_mt_kc_1` (§3.4). Filler missions do not autosave on give; the explicit command of D2 saves. |

### 4.3 Route to the safe house (E, F)

| Step | Original data |
|---|---|
| E1 | The publish of D3 re-evaluates `showExpression (mission_filler_mt_bm_1_status >= 0)` on the locked gate 2702 `func_door_rotating_289` and the lock prop 2703: both receive `hide` (Static §3.2). |
| E2 | 2630 `layer3_trigger_once_1229126361` (`globalExpression mission_filler_mt_bm_1_status == -1`, `spawnExpression mission_filler_mt_kc_1 != -1`; the bare name reads 0) → 2622 `tutorial_to_kc` (relay, `useKey returnToSafehouse`, targets 2619 and 2626) and 2713 `func_remove` (removes 2702, 2703 and the absent `func_static_66561`) |
| E3 | 6/3 OnUsed `returnToSafehouse` → 58/289 → 59/19 → 7/12 Say 0062 → 61/123 GoToNoStop `tutorial_guy_dest2` → 60/30 with three once outputs (flags 17) on Reached `tutorial_cover1/3/4` (`path_corner_cover_ally`, `scripted_run 1`) → nag Says 0071/0072/0073 → 19/426 to `tutorial_guy_dest4` (−3549,−380) facing Player → 21 → 75/19 → 9/12 Say 0085 → 20/51 → 76/490 → **24/73 UnbindBrain** |
| F1 | Safe-house front door 2583 `SHkri_frontdoor` (`usable 1`, `noAI 1`) → 2665 relay (`wait 20`) → 2602 `target_scenario_link` → 2617 `SHkri_doorman_brainuse` (`door`) → doorman 3565 (`kc_guard_pre_digsite`): 4/3 OnUsed `door` → 7/464 `entrance` or, with the player in `kc_entrance_loc` (2601), 3/464 `exit` |
| F2 | 2581 `SHkrieg_setup_spawn1` → 2644 (forced `mtw_kc_safehouse_setup1_front`: doorman, briefing control, movement control). 2645 `layer0_trigger_multiple_7` → 2597 (forced `mtw_kc_safehouse_setup1` 3625, 65 targets: Caroline 3564, Erik 3560, runner 3559, entrance trigger 3527, soldiers, idle and focus runners). Four of its targets fail their `spawnExpression` at arrival. |
| F3 | 3527 `layer61_trigger_once_1211927761` (deferred, `spawnExpression mtw_erik_caroline_1 == 0`) → 3558 → runner 1/3 OnUsed `entrance` → 9/220 `mtw_erik_caroline_1 = 1` → 47/464 `disableAction` → VO-in `scripted_voice_cb_kc1` (Caroline) and `scripted_voice_ee_kc1` (Erik) → 39/495 StopAllSpeakingAI → 41/30 → 0400 conversation (3, 5, 4, 8, 10) → 38/470, 48/470 SetSpeakStopRange 350 → VO-out ×2 → 28/464 `enableAction` → 13/188 EndBranch |

The runner's `MyTarget 1` is Caroline (`target`) and `MyTarget 2` is Erik
(`target1`), assuming target-list order follows the key order (Inferred,
TRAINYARD-CHAIN §3.2).

### 4.4 Briefing and mission give (G)

| Step | Original data |
|---|---|
| G1 | Action key on Caroline: `caroline_pre_digsite` 6/4 OnActionKeyed [prim AND `mission_digsite_status == 0`] → 3/68 UseTarget `digsite_briefing_runner` **`briefing`** → 16/220 `digsite_briefing = 1` → 17/216 objective success `mission_filler_mt_kc_1`. Erik: `erik_pre_digsite` 2/4 [same gate] → 27/289 → 43/68 `briefing` → 44/220. |
| G2 | Runner 2/3 OnUsed `briefing` → 22/19, 33/19 VO-in → 29/464 `disableAction` → 40/495 StopAllSpeakingAI P→ 14/51 `srp_kc1_nml_0600_vo_ee` (Erik), 16/51 `…_vo_cb` (Caroline) → Says 15 `0605_ee`, 17 `0606_cb`, 27 `0607_cb`, 19 `0608_ee`, 21 `0609_cb`, 23 `0610_ee`, 25 `0611_cb`, separated by Waits of 400–800 ms |
| G3 | 25 → 46/216 objective success `mission_filler_mt_kc_1` → **11/200 MissionGive `mission_digsite`** (`saveOnMissionGive` default true) → 45/70 RemoveTarget `tutorial_guy` → 31/68 UseTarget `digsite_tutorial` (3523 `trigger_tutorial`, `actionText tutorial_journal`) → VO-out ×2 → [AnimFinished `srp_kc1_0605_caroline` on Caroline] 37/30 Wait `"5.00 1"` (5000 ms) → 30/464 `enableAction` → 32/188 EndBranch |
| G4 | Give (MISSION-SYSTEMS §1.4): `mission_digsite` state 3, objectives built, `mission_digsite_status` 1. Autosave command when more than 10 s have passed since the mtw load. `mission_filler_mt_kc_1`'s expression becomes false; it is already complete. |

`mission_digsite` objective 1 is `objectiveEnt_1a digsite_transfer_point`
(1658, beside the stream trigger), `objectiveSkipOnReplay_1 1`. No mtw entity
completes it; digsite presumably does (Unknown).

### 4.5 To the digsite (H, I)

| Step | Original data |
|---|---|
| H1 | Safe-house exit: 3504 `layer112_trigger_multiple_1` (`showExpression mission_digsite_status == 1`, `toggleTriggerFirst`) → `layer112_target_scenario_1` (escort 3426 `pre_digsite_kcguy`, brain with OnDeath and ally cover path corners) |
| H2 | Zone A triggers (1677–1680, 1625) → 1663 forced `pre_digsite_zone_a` (3440, 25 targets, 16 enemy actors incl. `pre_digsite_courtyard1/2` brains, `lockAIPercent 0.7`) |
| H3 | 3515 `layer112_trigger_once_30` (`showExpression mission_digsite_status == 1`) → `layer112_target_scenario_2` → door guard 3439 (`digsite_doorguy`: IgnoreEnemiesSet, SetScriptedManual, EnableMovement false; OnUsed `Act` → Say `lipsync_dig_0105_kc1`) |
| H4 | 2591 `door_to_dig_site` (`globalExpression mission_digsite_status != 0`, `lockedText #str_001754`, `noActivateTargetsLocked 1`): locked until the give (Static §3.2) |
| I1 | 1626 `mtw_to_dig` (`trigger_stream`, (−6132,1086,−98), `spawnExpression mission_digsite_complete == 0`, `facing 1`, `facingAngle 270`, `confirm 1`, `missionName mission_digsite`, `def_mapTransition MTW_to_Digsite`). Touch shows the confirm prompt with the mission title; acceptance activates it; Start queues `MTW_to_Digsite` (`digsite.mpk`, warp `digsite_respawn`, load screen `entering_digsite`) (MISSION-SYSTEMS §5.4). |

Whether a hidden trigger (H1, H3 before the give) can fire was not read
(Unknown).

## 5. Hub systems: usage and status

Counts are mtw ENTS entities or mtw BRAI nodes. Status is from
[CAMPAIGN-RE.json](CAMPAIGN-RE.json) after this pass.

| System | mtw usage | Status | Contract |
|---|---:|---|---|
| Scenario deferral and `rvScenario` | 2,475 deferred; 127 scenarios | contract-established-not-implemented | §3.1 (assignment, lock and cleanup bodies open) |
| `rvTrigger_Choice` | 9 | contract-established-not-implemented | §3.3 (zone-timer and lock checks open) |
| `rvTarget_ResetScenario` / `rvTarget_ScenarioLink` | 10 / 4 | unknown | link = ActivateTargets; reset handler `0x200f2780` read |
| `spawnExpression` / `showExpression` / `globalExpression` | 570 / 27 / 26 | engine-level keys, no JSON row | §3.2 |
| Filler update (`rvTarget_Mission_Filler_Update`) | 1 entity, 2 filler missions on this path | contract-established-not-implemented | §3.4 |
| `rvTarget_SetGlobalVariable` | 8 | contract-established-not-implemented | §3.2 |
| `rvSpawner` (`func_spawner`) | 56 | unknown | events: activate `0x202aef50`, hide/show, active-entity queries |
| `rvZone` / `rvZoneDeactivator` | 6 / 4 | unknown | §3.6 |
| OnActionKeyed 4 / ActionKeyed 71 | 42 / 42 | contract (static-partial) | §3.5 |
| OpenPurchaseLocker 293 | 2 | contract | posts `openPurchaseLocker(ammoOnly)`; locker UI deferred |
| StartJournalTutorial 469, `rvTrigger_Tutorial` | 1, 1 | contract / unknown | player event `startJournalTutorial`; trigger has its own touch `0x201148b0` |
| LockPlayer 185 / UnlockPlayer 186 | 1 / 1 | contract | player `+0x957` bit 4 |
| UnbindBrain 73 | 11 | unknown (subsystem) | BRAIN-TASKS §5 |
| SetLocalVariable 195 / CheckLocalVariable 196 / CheckGlobalVariable 221 / ArgSet 298 | 281 / 200 / 100 / 149 | contract | BRAIN-TASKS §3–4 |
| SetGlobalVariable 220 | 57 | runtime-tested (literal profile) | general expression form not implemented |
| UseAllTargets 464 / EnableRootTask 289 / EnableAllRootTasks 290 / EndBranch 188 | 538 / 197 / 29 / 62 | contract | BRAIN-TASKS |
| Hub monsters `rvMonsterSniffer` / `rvMonster_Scribe` / `rvMonsterAKHeavyTrooper` | 16 / 5 / 11 | unknown | none is a target of the ten scenarios on this path (Data) |
| Patrols and zone enemies (`idAI` `enemies_*`) | 682 actors | idAI contract | AI-ACTOR; combat stage 5 |
| Ambient and conversation actors (`mt1_*`…`mt4_*` classnames, e.g. `mt1_5200_a` inherits `midtown_nazi_base`) and their `common/*` runners (`modular_*`, `conversation_*`, `focus_control_*`, `anim_control_*`) | 46 actors; 252 runners | unknown node types (WatchTarget, FacingFOV, speak ranges) | not on the critical path; no `civilian_base` classname was isolated in this pass |
| Vehicles `rvHkVehicleSingleBody` | 61 | unknown | parked props on this path |

## 6. Status per step

"rt" = implemented and runtime-tested on main under a literal profile;
"contract" = Static contract, no general handler; "unknown" = identity only.
Every step also needs `idAI` actors spawned from compiled entityDefs
(AI-ACTOR stage 1) and, for scenario actors, §3.1.

| Step | rt | contract | unknown |
|---|---|---|---|
| A | OnSpawn, Exists | EnableRootTask, LockPlayer; `spawnExpression` | EnableChatter, EnableStepAside |
| B | OnUsed, Used, Wait, Timer, ScriptedVOStreamIn, UseTarget (literal) | Say, SayFinished, Animate, AnimFinished, StartJournalTutorial, IsMenuOpen, UnlockPlayer, ScriptedVOStreamOut, SetLocalVariable, CheckLocalVariable, ReachedLocation, filler update, trigger volumes, relays | WatchTarget, GoToAndFaceTargetNoStop, GoToAndFaceTarget, Reached, ReachedXY, `rvTarget_SetGlobalVariable` key path |
| C | — | scenarios, `trigger_choice`, `spawnExpression` | trigger_multiple `wait` re-arm under the native host |
| D | Wait, SetScriptedManual (literal) | OnActionKeyed/ActionKeyed, EnableAllRootTasks, OpenPurchaseLocker, CheckGlobalVariable, MissionObjectiveSuccess, ExecConsoleCommand, EndBranch, mission manager | the purchase locker UI, focus type 7 test |
| E | — | `showExpression` re-evaluation, trigger `globalExpression`, `func_remove` (`idTarget_Remove` base activate) | GoToNoStop, Reached on cover points, UnbindBrain, ally cover path corners |
| F | — | usable door → relay → scenario link → brainuse, UseAllTargets, StopAllSpeakingAI, SetSpeakStopRange chain | door use by the player, SetSpeakStopRange 470, `door` handling in the doorman's movement runner (GoTo, FaceTarget, StopMove) |
| G | SetGlobalVariable (literal), RemoveTarget (literal), UseTarget | MissionGive with autosave, MissionObjectiveSuccess, Say/Animate, duration Wait unit 1 | `rvTrigger_Tutorial`, AnimFinished on a clip the graph never started |
| H | — | `trigger_choice`, scenarios, door lock expression | AI combat (Hunt, cover, hitscan), escort cover movement, OnDeath, IgnoreEnemiesSet, EnableMovement |
| I | — | `rvTrigger_Stream` touch/confirm/start, QueueMapTransition | headless confirm acceptance; facing test |

## 7. Ranked handler plan

Each item runs through the ordinary native lifecycle under a named source
profile and records explicit refusals for anything outside it. The order is
the order in which the chain blocks.

1. **`SourceScenarioDeferralV1`: scenario deferral and `rvScenario` spawn
   pass** [Static §3.1; assignment Inferred]. Acceptance: loading mtw defers
   exactly 2,475 entities and spawns none of them. Activating 3625 spawns its
   65 targets once, with deferred `spawnExpression` evaluated at that time
   (four refused at arrival). A second activation while active does nothing.
   `scenario_persistent` dictionaries are not respawned. Name collisions get
   `_<n>` suffixes. Teardown removes the spawned entities and the stored
   dictionaries. **Observed fact:** deferral and spawn order are Static; the
   per-scenario dictionary assignment is an inference to close by reading
   `0x200f12d0`/`0x200ef530` before relying on overlap behavior.
2. **`SourceExpressionGatesV1`: evaluator plus `spawnExpression`,
   `showExpression` (mission-publish re-evaluation), trigger and door
   `globalExpression`** [Static §3.2, BRAIN-TASKS §3]. Acceptance: table
   tests on the mtw texts at arrival and after each publish; 2619 spawns at
   arrival; 2702/2703 hide on the publish that sets
   `mission_filler_mt_bm_1_status` −1 and not before; 2630 does not fire
   before it; 2591 stays locked until `mission_digsite_status` is nonzero.
3. **`SourceMissionModelV1` + `SourceMissionTargetsV1` + filler update**
   [Static, MISSION-SYSTEMS M1/M2 and §3.4]. Acceptance: with trainyard
   complete, one filler update gives `mission_filler_mt_bm_1`; collector
   node 9 completes it (`_status` −1, `_complete` 1); the next update gives
   `mission_filler_mt_kc_1`; briefing node 46 completes it and node 11 gives
   `mission_digsite` (status 1) and queues one `save full` only when the load
   is older than 10 s. Updates are postponed while a modal menu is open.
4. **`SourceTriggerChoiceV1`** [Static §3.3; two helper checks Unknown].
   Acceptance: the selections of the §3.3 table under both global states;
   forced targets win in list order; `onceOnly` disables; the zone-timer and
   lock checks remain explicit refusals until read.
5. **Hub brain core: local/global variables, ArgSet, UseAllTargets,
   EnableRootTask/EnableAllRootTasks, EndBranch, StopAllSpeakingAI, Lock/
   UnlockPlayer** [Static, BRAIN-TASKS]. Acceptance: the BRAIN-TASKS §4.1
   tests on tutorial nodes 27/28/35 and collector nodes 5/8; the player
   cannot move between tutorial nodes 63 and 64.
6. **`SourceActionKeyV1`: player action key → `idAI` 0x201ac930 →
   ActionKeyed latch** [Static §3.5; focus type 7 test Unknown]. Acceptance:
   an action key on the collector latches root 4 once; disabled roots (node
   5) ignore further keys; Caroline's root 6 takes the `briefing` branch only
   while `mission_digsite_status == 0`. Read `0x20313e00` first.
7. **UI-backed tasks as headless receipts**: StartJournalTutorial,
   OpenPurchaseLocker, `rvTrigger_Tutorial`, the stream-trigger confirm
   [Static entries; UI deferred]. Acceptance: each records its event and
   argument; `IsMenuOpen` reports the real menu state of the native host
   (false with no menu), never a timer; the stream confirm is accepted only
   by an explicit headless "accept" input, recorded.
8. **AI presentation for hub contacts** (AI-PRESENTATION stages 2–3: Say,
   SayFinished, Animate, AnimFinished, VO stream out, WatchTarget,
   SetSpeakStopRange) on the `kc2_mt1`, `bm2`, `cb_kc1` and `ee_kc1` banks.
   Acceptance: tutorial B2–B4 and briefing G2 complete from real clip and
   speech observations; the `@cn_line*` lines resolve from the collector's
   def.
9. **Hub locomotion** (GoTo, GoToNoStop, GoToAndFaceTarget(NoStop), Reached,
   ReachedXY, ReachedLocation, FacingFOV, ally cover path corners, UnbindBrain)
   on mtw AAS [subsystem RE; AAS reader exists]. Acceptance: the guide walks
   2619 → dest1 → dest2 → covers 1/3/4 → dest4 on AAS routes, with each once
   output firing once.
10. **First hub combat** (zone A, escort, door guard): AI-ACTOR stage 5 plus
    OnDeath, IgnoreEnemiesSet, EnableMovement [subsystem RE]. Acceptance: the
    16 zone A actors spawn only after the give, engage the player and die
    through the native death path.
11. **`SourceMapTransitionV1` for `MTW_to_Digsite`** [Static MISSION-SYSTEMS
    §5]. Acceptance: touching 1626 facing 270 with confirm accepted queues
    `digsite.mpk` with warp `digsite_respawn`; the player, inventory,
    mission manager and globals carry over; 1626 does not exist once
    `mission_digsite_complete` is 1.

Items 1–5 need no AI subsystem work and can be built and tested on mtw
entities and portable replays. Items 8–10 reuse the trainyard AI stages.
Reaching mtw at all also requires MISSION-SYSTEMS M5/M6 (trainyard end and
the transition).

## 8. Open questions

- Scenario dictionary assignment, the lock-percentage and zone-inactive
  checks, scenario cleanup on reset (`0x200f0510`).
- The cvar at `0x205dcb9c` that overrides false `spawnExpression`.
- Whether the filler update already gives `mission_filler_mt_bm_1` in
  trainyard (the stats menu postpones it).
- Focus type 7 (`0x20313e00`) and the full player action-key input path.
- Whether hidden triggers (H1, H3) fire; how `hide` affects a locked door's
  collision (2702).
- Relay key delivery when a `trigger_relay` with `useKey` activates an
  `idAI` (E2 delivers `returnToSafehouse` through the relay and again through
  2626); which activator's `useKey` the AI Activate reads.
- G3's AnimFinished on `srp_kc1_0605_caroline`, a clip the runner never
  starts; the AI-PRESENTATION AnimFinished contract decides it.
- Who completes `mission_digsite` objective 1 (`digsite_transfer_point`).
- `rvTrigger_Tutorial` touch/activate and the journal tutorial UI events.
- Compiled-declaration equivalence of the `.def` keys quoted here.

## Reproduce

Read-only inputs: the installed `SP/base/maps/game/mtw/mtw.mpk` (normalized
ENTS from `evidence/campaign/ents`, BRAI corpus from
`evidence/brain-runtime`), the editor `.def` text of `assets.pk4`, and a
private copy of `Gamex86.dll` with the hash above. Private scripts in
ignored `evidence/mtw-chain/scripts/`: `dump.mjs` and `trace.mjs` (named
BRAI dumps and traces), `ents.mjs` and `xref.mjs` (entity and cross-reference
queries), `expr.mjs` (the evaluator), `brains.mjs` (brain owners with gates),
`choices.mjs` (`trigger_choice` selection), `fnc.mjs` and `evtab.mjs`
(procedure extraction and class event tables over the existing NDK
`llvm-objdump` text).

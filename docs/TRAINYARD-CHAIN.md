# Trainyard: from the opened wagon door to the first enemies

Research plan for [NEXT-STEPS](NEXT-STEPS.md) gate 4. Reviewed 2026-11-08.
This page maps what the original data and bounded static evidence say happens
after the greeter/door sequence. **Nothing here is implemented or observed at
runtime by this work.** It is a contract and handler plan, not a playability
claim.

Sources: the owner's trainyard MPK (SHA-256 `22a703b7…0832`, unchanged), its
ENTS, HKXS, AASS and 16 BRAI resources, text `.def` members of `assets.pk4`,
and the hash-identified `Gamex86.dll` (`7cb1cbed…2d47`, private copy only).
Raw dumps, disassembly and scripts stay in ignored `evidence/trainyard-chain/`.

Labels used below:

- **Data** — read directly from original ENTS/BRAI/HKXS/AASS/def bytes.
- **Static** — closed by disassembly of the identified DLL, checked against call
  sites. No execution was observed.
- **Inferred** — consistent with data and static facts, not closed.
- **Unknown** — no adequate evidence yet.

The `.def` text in `assets.pk4` is the editor source of the declarations. The
measured runtime loads compiled `.decls` ([DECLARATIONS](DECLARATIONS.md)); keys
quoted from `.def` text still need confirmation against the compiled namespace.

## 1. Three graphs are live when the door opens

| Owner (ENTS) | BRAI | State at door completion |
|---|---|---|
| 2129 `starting_boxcar_model` / `func_mover` | `brains/trainyard/movetrain` | After node 6 (relay Use) it waits in node 7 |
| 2034 `train_greeter` / `trainyard_greeter` | `brains/trainyard/train_greeter` | Already left node 35; running node 10 or later |
| 2132 `train_shaker` / `func_scriptrunner` | `brains/trainyard/shake_train` | Finished shortly after map spawn |

Two facts change the expected order:

1. **Data + Static.** Greeter node 35 (`AnimOverrideMoveStart`, type 59) has no
   embedded condition and its normal output to node 10 has no condition. Its
   Enter (`0x201f5d40`) resolves its actor, dynamic-casts to `idAI`, and stores
   the `mAnim` string in an AI member. It plays nothing and returns. The network
   therefore moves to node 10 `GoToAndFaceTarget` in the same Tick.
   **Inferred:** `srp_tyd_nml_train_door_open` is consumed as the move-start
   animation of node 10's locomotion toward `run_to_ladder_point`. The PC trace
   in which the greeter leaves while the door opens fits this. The consumer of
   that AI member was not traced. Implementations should treat "actor animation
   start" as the start of the node 10 move, not as an action of node 35.
2. **Data.** The doorway is blocked by ENTS 2194 `layer3_func_static_3`, not by
   the door. Its HKXS body is solid (raw contents 1): a box with half extents
   5×42.5×54 units, translated (−3,−544,0) and bound to the wagon. After the
   measured +320 Y wagon translation it spans about x 198–208, y 198–283,
   z 294–402. This is the doorway. The animated door has `noclipmodel 1`.
   Movetrain removes this blocker in node 8, after node 7 `Wait "6000 0"`
   starts at the greeter relay Use. The player's exit is gated by that timer,
   not by door animation completion.

## 2. Ordered original chain

Node references are `ID/type`. "P" is a probe output (flag `0x20`), "N" a
normal output (flag `0x10`). Conditions: `prim` is the source task's embedded
condition; other names are explicit condition records.

### 2.1 Map spawn (precondition, already running before arrival)

| Step | Original data | Notes |
|---|---|---|
| S1 | shake_train 1/1 OnSpawn → 30/30 Wait `"1 1"` → 19/68 UseTarget `trigger_once_1` → 31/357 EnablePlayerWeapons → 32/102 NoTargetSet(`Player`, raw `mNoTarget="fasle"`) → 33/468 EnableCredits(false) → 34/253 SetUndying(`Player`, false) | All N, prim on 1 and 30 only. 26 of 34 nodes are unreachable from roots 1 and 2. |
| S2 | ENTS 2033 `trigger_once_1`, `trigger_once`, `triggerFirst 1`, raw `frame 182`, `target1..4` = `nazi_interragator_1`, `nazi_interragator_2`, `train_greeter`, `layer38_target_lightswitch_1` | No plain `target` key. The `.def` text says `triggerFirst` means it does not activate until triggered. S1 is its only arming source in ENTS and all 16 trainyard BRAI resources. |
| S3 | interrogator 1/1 OnSpawn → 11/19 VO-in `scripted_voice_wi1_trn` → 20/19 VO-in `scripted_voice_wi2_trn` (owner `nazi_interragator_2`) → 2/33 SetScriptedManual → 38/54 AnimOverrideIdle `srp_tyd_execute_nzi_01_start_idle` | Outputs 11→20 and 20→2 have **no** condition: the graph does not wait for these banks. Probes from 2: SetScriptedManual on interrogator 2 and both hostages, death-override reset and NoTargetSet on both hostages, SetUndying on both interrogators. |
| S4 | greeter 1/1 OnSpawn → 2/49 → 24/83 → 73/52 WatchTarget(−1) | Probes: 7/351 HideWeapon, 61/75 SetUsable(Me,false), 68/33, 21/19 VO-in kc1. |

Raw `"fasle"` must be preserved. Its boolean value under the original property
parser is **Unknown**.

### 2.2 After the relay Use (door phase, owned by other work)

| Step | Original data |
|---|---|
| D1 | movetrain 6/68 UseTarget `train_greeter_brainuse_1` (raw `train_Stop`) → N hdr3 → 7/30 Wait `"6000 0"` |
| D2 | greeter 3/3 OnUsed `train_stop` → 29/19 VO-in `scripted_voice_kc1_trn` [prim] → 35/59 |
| D3 | greeter 35/59 P→ 6/12 Say `lipsync_trn_0505_ka1`; P→ 33/68 UseTarget `target_hud_component_2`; P→ 72/68 UseTarget `target_mission_give_1`; N (no condition) → 10/243 |

### 2.3 From door completion to the exit

| Step | Original data | Contract status |
|---|---|---|
| E1 | greeter 10/243 GoToAndFaceTarget `run_to_ladder_point` (ENTS 2037, `target_null`, (−271,702,318)), face `sewer_trapdoor`, `mMovement=CombatRun`, `mRange="20.00 0"`. P→ 58/306 ShowTip `#str_007235`/`#str_007236`, `mAutoHide=false`. N [prim ReachedAndFacing] → 31/30 | Movement needs AI locomotion on AAS (§4) |
| E2 | movetrain 7/30 [prim Timer] → 8/70 RemoveTarget `layer3_func_static_3` → 9/456 ExecConsoleCommand `CheckOneOfMany` | Removal of a bound solid body; console command semantics **Unknown** |
| E3 | Player walks through the doorway into ENTS 2094 `exit_train_trigger` (`trigger_once`) | HKXS box half (4,48,56) at (196,232,352), raw contents 4096, `rvCallBackPhantom`; body rotation not decoded |
| E4 | 2094 activates ENTS 2032 `train_greeter_brainuse_2` (`useKey player_leaves_train`, `target train_greeter`, `target1 nazi_interragator_1`) | Relay loop: **Static**, §3.2 |
| E5a | greeter 4/3 OnUsed `player_leaves_train` → N aux3 → 10/243 | Root re-activation of the active task; re-entry behavior of GoTo **Unknown** |
| E5b | interrogator 12/3 OnUsed `player_leaves_train` → [prim] 43/54 AnimOverrideIdle `""` (P→ 42,41,40 reset idles) → 21/57 AnimOverrideDeath(`trainyard_hostage_1`, `srp_tyd_execute_kc_one`) → 22/57 (hostage 2, `srp_tyd_execute_kc_two`) → 25/51 Animate `srp_tyd_execute_nzi_one` | 21→22→25 have no condition |
| E6 | 25/51 P→ 26/51 Animate `srp_tyd_execute_nzi_two` (actor `nazi_interragator_2`); P→ 33/69 KillTarget hostage 1; P→ 35/69 KillTarget hostage 2. N [prim AnimFinished] → 17/33 | Kill fires at animation entry; death clip comes from the death override |
| E7 | Model `model_trainyard_base` frame commands: `nzi_one` f112 speak `lipsync_trn_1001_wi1`, f207 trigger `execution_gunshot` (ENTS 2151 speaker); `nzi_two` f97 speak `1006_wi2`, f207 trigger `execution_gunshot2` (2207); `kc_one`/`kc_two` `no_interactive_ragdoll`, f207 fx + trigger `blood_decal_1/2` (477/478, hidden, no clip), f230 `interactive_ragdoll` | Frame-command dispatcher for AI clips needed |
| E8 | 17/33 SetScriptedManual(false) P→ 16/33 (interrogator 2), 46/253 and 47/253 SetUndying(false) both interrogators; N → 6/43 Hunt(`Player`, `mUpdatePosition=true`) P→ 10/43 Hunt by interrogator 2; N (no condition) → 7/12 Say `lipsync_trn_1007_wi1` → [prim SayFinished] 8/12 Say `1008_wi2` | **First enemy engagement** |

Alternative arming: when armed, `trigger_once_1` covers the platform outside
the doorway. Its box has half extents 46×132×60, translated (2,−54,−12) from
(252,250,352). Touching it activates both interrogators, the greeter and the
door light (ENTS 479 → light 972, `start_off 1`). **Inferred:** the hidden
interrogators (`Hide 1`) become visible here or through the E4 Use. The
`.def` editor text describes `hide` as hidden and non-solid until triggered.
`idAI` Activate handling was not inspected. ENTS 2209 `layer3_trigger_once_2`
duplicates this trigger at (260,242,352). Nothing in ENTS or trainyard BRAI
arms it, so it should stay dormant.

### 2.4 Escape and the end of the first encounter

| Step | Original data |
|---|---|
| X1 | greeter 31/30 Wait `"100 0"`: N [prim Timer AND ReachedLocation(`trap_door_room`, `Player`)] → 11/12; else N [prim] → 32/51 Animate `st1_nil_nml_idle` → [prim] 31 |
| X2 | 11/12 Say `lipsync_trn_1105_ka1` P→ 53/52, 34/51 `srp_tyd_point_floor`, 69/307 HideTip; [prim] → 67/54 `srp_tyd_nml_hide_idle` → 57/51 `srp_tyd_nml_to_hide` → 60/30 Wait `"1 0"` → weighted {54,55,56} (all weight 0 → equal) → nag Says 59/62/63 → 60 |
| X3 | ENTS 1676 `trigger_objective_success_1`: box half (42.6,41.8,10) at (−251,587,237) → ENTS 1719 objective 1 success |
| X4 | ENTS 2095 `shut_door`: box half (112,64,4) at (−240,576,132) → `trap_door` (**no such entity**), ENTS 2036 `train_greeter_brainuse_4` (`player_safe`), ENTS 2136 `sewer_trapdoor` (`rvRotatingDoor`, `start_open 1`, `toggle 1`) |
| X5 | greeter 5/3 OnUsed `player_safe` → 22/19 VO-in wi1 (owner `wehrmacht_infantry_talker_1`) → 25/19 VO-in wi2 → 38/70 Remove interrogator 1 (P: remove interrogator 2 and both hostages; Show `nazi_interragator_3/4`) → 44/49 → 50/30 Wait `"2 1"` → Says 9,12,15,16,17 → 37/68 `target_brainuse_announcements_pause` → 18/68 `gunshot` → 19/30 → 26/490 → 20/68 `gunshot` (P: remove interrogators 3/4) |

`trap_door_room` is ENTS 1612 `func_aas_location` and AAS location index 26.
In HKXS it is a phantom box with half extents (92,112,60) at (−156,656,372).

### 2.5 What follows (outline only)

Beyond the sewer, ENTS 2042 `top_of_ladder_trigger` activates
`player_arrived_brainuse` (Erik Engle, `allies_contact_1`),
`info_player_respawn_1`, `start_cleanup` (`func_remove` of the intro actors) and
`allies_path_corner_18` (absent). Animation frame 600 of `srp_trn_nml_2005_ee`
triggers `activate_weapons`. That arms the first pickups: `weapon_mp_2`
(`weapon_mp40` item), dynamite, ammo and a DLC flamethrower. Allies node 293/294
waits on `PlayerHasWeapon(weapon_mp40)`. Node 203 then probes
`grab_gun_trigger_relay` and `save` (`rvFuncSaveGame`). The relay shows the
patrol `stage1_wehrmacht_officer_1` and `stage1_wehrmacht_infantry_1/2`
(brain `nazi_patrol` with OnAware/OnCombat/OnPain roots). It also completes
objective 2. **This patrol is the first encounter the original lets the
player fight with a picked-up weapon.** The interrogators in §2.3 are the first
enemies the player meets, while still holding only `weapon_none`.

## 3. Contracts established in this pass

### 3.1 Probe outputs (flag `0x20`) — Static

Verified in the assembly of Tick `0x201df480`, eligibility `0x201ddb60`, task
pass `0x201ddbd0`, flush `0x201ddaf0`/`0x201dda90`, output fire `0x201dda10`
and probe activation `0x201dd760`:

- Each loop iteration runs the normal root/active pass first. It then runs a
  probe pass on the current active task, including a task entered in that
  iteration. The flush follows. The loop repeats while a normal transition
  happened.
- In the probe pass, every eligible probe output whose conditions pass is
  marked `0x40000`. The pass does not stop at the first one. A probe output is
  ineligible once processed (`0x80000`) until its source re-enters. Non-once
  outputs are cleared at entry.
- Flush visits the active task, then roots. Each marked output is cleared and
  fired through the common weighted destination selection. It is then marked
  processed.
- Probe activation of the destination task sets task flag `0x20` and `0x8000`
  and clears `0x10`/`0x40`. It clears repeatable processed state. For each
  output in file order it enters that output's conditions in reverse order.
  It then enters the task unless it is a processed once task. **It does not change the active task or its start time
  and does not exit the current task.**

In the five chain graphs (train_greeter, nazi_interragator, shake_train,
movetrain, nazi_chatting), every probe destination is a leaf. None is also a
normal destination. A bounded "probe leaf" profile therefore needs only Enter
side effects. Durable effects that probe Enter starts, such as Say, Animate,
GoTo for another actor and Hunt, continue in their owning subsystems. The
network neither follows nor exits them. **Unknown:** Exit/cleanup of probe
tasks entered this way and the `0x40` (stack) mode, which is not used here.

### 3.2 Use routing — Static

- `rvTarget_BRAINUse` activate handler `0x20108560` loops over **all** entries
  of its target list. Each live entry receives virtual Use (`+0x24c`) with the
  relay's own `useKey` and the original actor. Stale references are skipped.
  This comes from the retained relay disassembly; the earlier slice admitted one
  target only. Target-list order follows the spawn-time target dictionary
  (**Unknown** for duplicate prefixes; both orders deliver the same key here).
- RTTI vftables show that `idTrigger_Multi`, `idSound`, `idTarget_Remove`,
  `rvFuncSaveGame`, `rvTarget_Mission_Give`, `rvTarget_HudComponent` and
  `rvTarget_BRAINUse` share the base Use `0x200438d0`. It processes the
  `activate` event with the actor and ignores the key. BRAI UseTarget on these
  targets is an Activate.
- `idAI` overrides Use (`0x201ac870`). Unless spawn arg `noUnhideOnUse` is set
  and an internal flag is clear, it first notifies the AI's network and each
  entry of a second network list. Each receives (actor, key) after CanUseKey. It
  then calls the base Use (activate). A key Use on an AI therefore both
  latches OnUsed and activates it. Activate behavior of `idAI` is **Unknown**.
  The name `noUnhideOnUse` indicates that Activate unhides it (**Inferred**).

### 3.3 Other bounded facts

- Condition descriptors were read from the same registry pattern: 10
  SayFinished, 18 ScriptedVOStreamedIn, 21 Reached, 22 Facing, 42 EnemyDead,
  50 AnimFinished, 62 TaskDone, 64 Used, 65 Timer, 74 Exists, 100
  ReachedLocation, 131 MoverDoneMoving, 242 ReachedAndFacing, 373
  MissionIsObjectiveActive, 424 IsInTurret, 446 AnyNazisAlert, 448
  PlayerHasWeapon (115 conditions, 282 tasks).
- Wait durations are `"<value> <unit>"`. Saved `mActualDuration` values agree
  with unit `1` = seconds and `0` = milliseconds in the greeter graph
  (`"2 1"`→2000, `"100 0"`→100). shake_train stores stale values
  (`"1 1"` with 10000). **Inferred**; the current native host admits only its
  literal profile.
- Serialized task `^ID` flags 16/32/0 in these graphs coincide with
  normal-entered/probe-entered/never-entered nodes. They are saved runtime state
  and must not be used as activation semantics.

## 4. The first enemies

| Fact | Value | Status |
|---|---|---|
| Entities | 2091 `nazi_interragator_1` (`trainyard_nazi_talker_1`, (64,628,296), angle 270, brain `trainyard/nazi_interragator.brain`); 2092 `nazi_interragator_2` (`trainyard_nazi_talker_2`, (160,684,296), no brain key) | Data |
| Spawn keys | Both `Hide 1`, `anim idle`, `speakStopRange 0`, `tactical_object_bounce 0`; #1 also `Grenades 0`, `no_drops 1`, `action_grenadeAttack 0`, `canQuickKill 0`, `disableCover 1` | Data |
| Victims | 2149/2150 `trainyard_hostage_1/2` (`trainyard_allies01/02`, team 0, health 15, `Hide 1`, `hideWeapon 1`) | Data |
| Definition chain | `trainyard_nazi_talker_N` → `enemies_wehrmacht_infantry_mp40` → `enemies_wehrmacht_infantry` → `enemy_human_base` … `actor_base`; `spawnclass idAI`, team 1, health 15, size 28×28×72, `use_aas aas28`, `def_weapon weapon_mp40`, `attack_base_hitscan 1`, melee `damage_melee_rifle` range 50, `ragdollOnDeath 1`, `removeOnDeath 1` | `.def` text; compiled check pending |
| Model | `model_new_wehrmacht_infantry_v2`, mesh `…/body_ewm_inf_v2.md5r` (MODL in `streampacks/wehrmacht_infantry_mp40.spk`), head `heads_enemy_02`, helmet attachment, voice `ai_voice_wi1/2`, scripted voice `scripted_voice_wi1/2_trn` | Data |
| Scripted clips | All `srp_tyd_execute_*` start idles, executions and kc idles are in trainyard HKXA (187 names) | Data |
| VO banks | English packs: wi1_trn 11 SNDS entries, wi2_trn 9, kc2_trn 12, ee_trn 58 (entry counts; uniqueness not checked) | Data |
| Navigation | One AASS `maps/game/trainyard/trainyard`, `DewmAAS "1.40"`, one bbox (−14,−14,0)–(14,14,24), `fileExtension aas28`, `generateTacticalFeatures 1`. Text header plus `bin` sections, 4-byte multiples: planes 8744×16, vertices 12838×12, edges 17112×8, faces 50495×16, areas 8447×56, reachabilities 18119×48, nodes 21362×12, portals 220×10, clusters 598×12, features 380×36, locationIndex 3396, 37 named locations | Data; field meanings **Unknown** |
| Engine AAS | Pinned GPL loader reads `DewmAAS` 1.07/1.08 text, not this 1.40 binary hybrid | Data |
| Behavior | Execution, then SetUndying(false), then Hunt(`Player`); Hunt leaves the network immediately; Hunt continuation after its task is no longer active | **Unknown** |
| Damage to player | Player `SetUndying(false)` at spawn (S1); AI hitscan parameters live in AI code and damage defs | Partly Data, rest **Unknown** |

Minimum native systems to reproduce E3–E8 honestly:

1. Source-qualified AI entity: compiled entityDef spawn of `idAI`-equivalent
   actors with body, head and attachment binding. It must keep `Hide`
   semantics, team, health, undying and no-target state and the death-animation
   override. Death must run an actual native death path; ragdoll can be an
   explicit refusal at first.
2. AI animation channel: scripted Animate/idle overrides on the existing native
   animator, plus a frame-command dispatcher for `speak`, `trigger` and `fx`.
3. Navigation: an AAS 1.40 reader, or a declared native route profile proven
   equivalent on the measured areas. Hunt and GoTo need reachability. Faking
   paths by teleport or straight-line moves is excluded.
4. AI combat: target acquisition of `Player`, MP40 hitscan with source damage
   defs and the native player damage/death/respawn already tested.
5. Brain kernel: probe pass, non-literal Wait units and the AI tasks and
   conditions listed in §5.

## 5. Handler coverage and ranked plan

Already present in shared code: BRAI decoding of all 633 graphs; normal flow
and Task lifecycle (`BrainFlow`/`BrainTask`); native literal host for
OnSpawn 1, OnUsed 3, Wait 30 (literal), UseTarget 68 to brain receivers and
SetGlobalVariable 220; conditions Exists/Used/Timer as primaries; arrival
director (Remove 70 and Show-by-Use on qualified statics); single-target relay
`MeasuredTrainyardOwnLiteralV1`; `TrainDoorSignal`; scripted actor playback;
animated door; player MP40 hitscan/damage; player death/respawn. No source
trigger volumes, AI, AAS, item pickup, objectives, HUD, tips or save entity are
implemented.

Ranked missing handlers (contract status in brackets):

1. **Probe pass, leaf profile** in `BrainTask` [Static, §3.1]. Unblocks D3, E1,
   E6, E8, X2 and S3. Acceptance: the synthetic graph fires all eligible probes
   once per source entry, in output order. Active task and start time stay
   unchanged. Repeat source entry re-arms only non-once outputs. A probe target
   with outputs, or flag `0x40`, is refused. Replay greeter node 35 through a
   TestHost and observe probes 6/33/72 plus normal transition to 10 in one Tick.
2. **Source trigger volumes** (`trigger_once`/`idTrigger_Multi`) from HKXS
   `rvCallBackPhantom` boxes, with `triggerFirst` arming, `wait -1` and
   `target`/`targetN` [Data; `triggerFirst` from `.def` text, native
   handler not inspected]. Acceptance: real player movement enters 2094 exactly
   once. Dormant 2209 never fires; 2033 fires only after S1 arms it. Teardown and
   map reload leave no stale touch. Body rotation must be decoded, not assumed.
3. **Removal of the bound doorway blocker** [Data]: movetrain 7→8 with
   Wait unit parsing. Acceptance: the player cannot pass the doorway before node
   8. Removal destroys exactly ENTS 2194's native collision and keeps the wagon
   bind; the player then leaves through normal movement. Node 9 stays an explicit
   refusal until `CheckOneOfMany` is identified.
4. **Multi-target `target_brainuse`** [Static, §3.2]. Acceptance: 2032 delivers
   `player_leaves_train` to both receivers with the original actor. A missing
   or stale target is skipped without blocking the other. Single relay test
   remains green.
5. **AI receiver Use** [Static, §3.2]: network notify then Activate. Acceptance:
   OnUsed latches on interrogator 1 and greeter. Activate behavior stays a
   declared profile until `idAI` activate/unhide is inspected.
6. **Hidden AI spawn and show** (`Hide 1`, Show 154, RemoveTarget 70 on AI)
   [Data; activate-unhide Inferred].
7. **AI scripted animation tasks**: Animate 51, AnimOverrideIdle 54,
   AnimOverrideDeath 57, AnimOverrideMoveStart 59, conditions AnimFinished 50
   [59 Enter Static; others Unknown]. Acceptance: real native clip start/finish
   observations; frame commands dispatch once per clip play.
8. **KillTarget 69, SetUndying 253, SetScriptedManual 33, NoTargetSet 102,
   IgnoreEnemiesSet 49** [Unknown handlers; names/descriptions from registry].
9. **AAS 1.40 reader and GoToAndFaceTarget 243 / ReachedAndFacing 242 /
   ReachedLocation 100** [Data layout counts only]. Acceptance: section byte
   accounting to EOF, location names ↔ ENTS `func_aas_location`, then a native
   route from the greeter origin to `run_to_ladder_point`.
10. **Hunt 43, Say 12/SayFinished 10, ScriptedVOStreamIn/Out 19/490** with the
    whole-bank gate from NEXT-STEPS 1–2 generalized to wi1/wi2 [Unknown for
    Hunt/Say; VO gate as already documented].
11. **AI combat and damage**: MP40 AI hitscan, player damage, enemy death
    [Unknown AI fire policy; player side tested natively].
12. Presentation/progression probes: `rvTarget_HudComponent` (`showCompass`),
    `rvTarget_Mission_Give` (`mission_trainyard`, `saveonmissiongive 0`),
    ShowTip 306/HideTip 307, objective success targets, `rvFuncSaveGame`
    [Unknown semantics; all are Activate targets per §3.2]. Until implemented,
    they must be recorded as explicit unsupported receipts, never skipped silently.

Gate 4 acceptance for this segment: from a real arrival, the player waits for
blocker removal, walks out, triggers 2094 once and sees the execution driven by
native clip observations. The hostages die and both interrogators hunt and
damage the player. The player escapes to `trap_door_room` and triggers 1676 and
2095, after which the greeter's `player_safe` branch runs. Every unimplemented
action in that path appears as a named refusal. Death and respawn must not
duplicate once-only events. Fighting with weapons belongs to the later patrol
encounter.

## 6. Objective, HUD, save and Veil events on this path

| Event | Source | Data |
|---|---|---|
| Mission give | greeter 35 P→72 → ENTS 1730 | `mission_trainyard`, `saveonmissiongive 0`; the start entity 367 also carries `mission mission_trainyard` and `SetWeaponNone 1` |
| Compass | greeter 35 P→33 → ENTS 2147 | `showCompass 1` |
| Tip | greeter 10 P→58, hidden by 11 P→69 / 22 P→52 | `#str_007235`, `#str_007236` |
| Objective 1 success | ENTS 1676 → 1719 | `objectiveNumber 1`; mission def objective 1 entity `objective_1` at (−251,589,72) |
| Credits/weapons | shake_train 31, 33 | EnablePlayerWeapons, EnableCredits(false) |
| Save | first on this map after the sewer: allies 203 P→207 `save`; ENTS 2042 also arms `info_player_respawn_1` | Outside this segment |
| Veil | None of the five chain graphs or listed entities references Veil | Bounded to this segment |

## 7. Open questions

- `idAI` Activate/unhide, Hunt task lifetime, KillTarget with a death
  override, and the AI member that stores the move-start override.
- `idTrigger_Multi` activate handler and the raw `frame 182` key on triggers.
- Whether the `speaker` 2154 carrying a `brain` key runs shake_train a second time.
- `ExecConsoleCommand CheckOneOfMany` and the missing `trap_door` target.
- HKXS body rotation for trigger boxes, and the player/phantom filter for raw contents 4096.
- Boolean parsing of `"fasle"`; Wait unit semantics; probe task Exit.
- AAS 1.40 field layouts and tactical features.
- Compiled-declaration equivalence of the `.def` keys quoted here.

## Reproduce

Read-only inputs: the owner's `SP/base/maps/game/trainyard/trainyard.mpk`,
`assets.pk4`, `streampacks/*_vo_english.spk`, and a private copy of
`Gamex86.dll`. The private scripts normalize ENTS, dump BRAI with registry
names, compute reachability, scan Task/Condition descriptors and read HKXS
bodies and AAS section sizes. They live only under `evidence/trainyard-chain/`.
Disassembly used the Android NDK `llvm-objdump` on selected ranges. No game
process, compiler or new Ghidra import was used.

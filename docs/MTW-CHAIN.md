# Midtown West: from the trainyard warp to the digsite transition

Research plan for the first hub. Reviewed 2026-11-10. **Work in progress.**
This page maps what the original data and bounded static evidence say happens
in `mtw` between the `Trainyard_to_MTW` warp and the `MTW_to_Digsite`
transition. Nothing here is implemented or observed at runtime by this work.
It is a contract and handler plan, not a playability claim.

Sources: the owner's `mtw.mpk` (ENTS, 133 BRAI resources), the editor `.def`
text of `assets.pk4`, and the hash-identified `Gamex86.dll` (`7cb1cbed…2d47`,
private copy only). Raw dumps, disassembly and scripts stay in ignored
`evidence/mtw-chain/`. Labels follow [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md):
**Data**, **Static**, **Inferred**, **Unknown**.

## 1. Arrival

| Fact | Value | Label |
|---|---|---|
| Warp | `Trainyard_to_MTW` → ENTS 1648 `mtw_from_trainyard_respawn` (`info_player_respawn`, (−2121,−3248,−250), angle 180) | Data |
| Stream destination | ENTS 1647 `mtw_from_trainyard` (`trigger_stream_dest`) targets 1648 and lists detail packs `mtw_pak02/03/05` | Data |
| Globals at arrival | `mission_trainyard_status` −1, `mission_trainyard_complete` 1; every other mission status 0 | Inferred from MISSION-SYSTEMS §1.3 |
| Kreisau contact | ENTS 2619 `tutorial_guy` (`mtw_kc_tutorial_guy`, brain `mtw/tutorial_guy`), 49 units in front of the warp | Data |

## 2. Hub spawn gates (Static, `0x2006f0xx` inhibit check)

The map-entity inhibit check reads, in order: difficulty filters,
`minMachineSpec`, `si_entityFilter`, then **`scenario`**: an entity with a
true `scenario` key is not spawned while the scenario-spawn flag
(`0x2073874c`) is clear. Its spawn dictionary is copied into the scenario
manager (`0x206e6618`, `0x200f0840`, at most 3000 entries) instead. Only then
are `spawnExpression` (evaluated with the BRAIN-TASKS §3 evaluator on the
shared dictionary; a false result inhibits unless a cvar at `0x205dcb9c` is
set), `spawnMission`/`spawnMissionObjective`, `unique`, `lightGroupParent` and
the flamethrower DLC flag tested.

2,475 of 5,659 mtw entities carry `scenario 1` (**Data**). They exist only
after a `target_scenario` (`rvScenario`) spawns them, and their
`spawnExpression` is evaluated at that later time.

*(Sections on the ordered chain, hub systems, status and the ranked plan
follow.)*

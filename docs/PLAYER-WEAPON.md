# Player, MP40, inventory and pickups

The native source session uses the ordinary `devmap`, `SpawnPlayer`,
`idPhysics_Player` and `game->Draw` paths. API8 is unchanged; no Windows DLL
is loaded. An explicit data-only `wolf2009-session.cfg` selects four compiled
catalogs, the source player, the starting loadout and the exact stand/crouch
HKXS bodies. Without the descriptor the stock Doom gameplay path stays.

## Player and MP40 controller

Trainyard selects `player_marine`, health 100 / mass 200, owned slots 0/1/11
and `weapon_none`. The base defaults with an MP40 do not replace the mission
start. Both player shapes use the exact native-f32 support points (50
vertices, 144 edges, 96 triangular faces). The shared trace-model ABI
64/160/96 and the 161-bit CM caches are built consistently in core and game.
The legacy save record stays 32/33/16; the wide trace is not serialized.

The MP40 view loads the named source MODL/SKEL/HKXA through the read-only VFS:
49 joints, 7 surfaces, 4808 vertices, 6488 triangles. `NativeBaseDiffuseV1`
draws the three base surfaces with the original sleeve, hand and gun
diffuse; the four upgrades stay hidden. Idle/fire/reload have 80/5/48 frames
and their source durations.

`SourceWeaponController` is the only frame-command scheduler. Frame 1
`weapon_attack` runs a native ray and Damage with the source scalar 5; clip 32
and the 120 ms interval are kept. Reload moves ammo at the end of the source
clip. POI/eye/sound commands reach a separate presentation owner.
`NativeWeaponLocalAimAndPointTrackingV1` follows the animated wrist; the
retail camera solver is not reproduced.

## Inventory — `SourcePickupInventoryV1`

`src/wolf2009/SourceInventory.*` models `RvPlayerInventory` (Static, Gamex86
`7cb1cbed…`). Each native source player owns one; spawn and respawn reset it.

| Contract | Source | Native |
|---|---|---|
| 12 weapon slots named by `def_weapon0..11` | `+0x84`, slot name `+0x1c` | `SourceInventory` |
| Slot amount `+0x38`, maximum `+0x3c`; owned when amount > 0 | `0x2038b090` | `SourceInventorySlot::Owned` |
| Add clamps at the maximum and returns the excess | `0x2038b3c0` | same |
| Weapon slot maximum 1 | **Inferred**: a second weapon give overflows and fails (`0x2038a03f`) | 1 |
| Nine ammo slots with a fixed name table | `0x203544e0` | `SourceAmmoSlotNames` |
| Ammo maximum = the weapon's `InvAmmoCapacity` | `0x20358134` (RvWeapon slot `+0x20`; call timing **Unknown**) | applied when the inventory is built (trainyard: mp40 200, mdl24 3); an ammo type without a declared capacity refuses gives |
| `weapon` give adds 1 and fails on overflow; `ammo_*` is refused when full, else added | `0x20389840` | `SourceInventory::Give` |
| `generic` and `collectible` are accepted | `0x2038f2b0` | same |
| GiveItem refuses `inv_weapon` items while weapons are disabled, gives every `inv_*` key, succeeds when any give succeeds | `0x2031bb10` | `SourceInventory::GiveItem` |
| PlayerHasWeapon (448) = ownership of the slot with that name | slot `+0x90`, `0x203879a0` | `Wolf2009_EvaluateSourceBrainCondition` |

Refused explicitly: `InvInitialAmmo` and the clip object that RvWeapon `+0x20`
fills to `ClipSize` (their call timing is not traced), the dropped-item random
halving, health keys and the `+0x21ba` GiveItem gate. Inventory key order is
the resolved field order (alphabetical); the original dictionary order is not
established and does not change the result for these items.

**Ammo split (`SourceAmmoSplitV1`, compatibility policy).** When the
inventory first owns `weapon_mp40`, the source weapon controller is equipped
after the outermost pickup dispatch with `clip = min(ClipSize, pool)` and
`reserve = pool − clip`. The `ammo_mp40` pool then belongs to the controller,
and later `ammo_mp40` gives are refused. The original clip/reserve layout is
not established.

## Pickups — `idItem` and `idTrigger_Multi`

`src/idtech4a_gameplay/NativeSourcePickups.*` (session key `mapPickupProfile
SourcePickupInventoryV1`) spawns from the original ENTS and compiled class
definitions the relays `activate_weapons` (1739) and
`trigger_relay_weapon_pickup` (1749) (`idTrigger_Multi`, wait 0, delay 0) and
the items `weapon_mp_2` (1738), `item_dynamite` (1736) and `ammo_mdl24_large`
(1737) with a `CONTENTS_TRIGGER` cube.

| Step | Static | Native |
|---|---|---|
| Activate on a hidden item: show, return | `0x200920b0` | `Show`; the highlight refresh is not modelled; contents and the pending removal stay |
| Activate on an unarmed `triggerFirst` item: arm (`+0x30d`), return | same | `armed` |
| Otherwise a player activator gets Pickup, without CanPickup | same | same |
| Touch: player only, CanPickup, Pickup | `0x20092060` | native `EV_Touch` from the player's trigger contacts |
| CanPickup: armed; local player; `inv_weapon` refused while weapons are disabled, for an unknown weapon slot or full ammo; `ammo_type` refused when unknown or full | `0x20092b90` | same; the other player-state gates (`+0x76c`, `0x20003010`, slot `+0x3ec`, `noGraspPickup`) have no native state and are not modelled |
| Pickup → GiveToPlayer: GiveItem, then **ActivateTargets(player)**, then post-pickup | `0x20091ed0`, `0x20091a30` | same order; `snd_acquire`, the player `+0x224` notice and pickup event 7 are recorded as not modelled |
| Post-pickup: contents 0, hide, inactive; `<immediateremove>` after 5000 ms (respawn_sp 0, not dropped, no `no_respawn`/`inv_carry`) | `0x20091c80` | `EV_Remove` posted 5000 ms later |
| A relay fires its targets synchronously and ignores a re-entry in the same frame (stores time + 1) | `0x20114883` | same |
| `flamethrowerPreOrderDLC` entities are inhibited without the pre-order entitlement | `0x2006f498` | `NoPreOrderDLCEntitlementV1`: the flamethrower (1747) and its tip (1748) are spawn-inhibited receipts |

Touch volume (`ItemTouchCubeV1`, compatibility policy): a cube of half-size
`triggersize` around the item origin. The idEntity spawn default `"32"`
(`0x2003a65b`, `ent+0x4b0`) is Static; its use as the pickup volume is
**Inferred**. Items present no world model in this profile.

**The touched item is given twice.** Touching `weapon_mp_2` picks it up. Its
GiveToPlayer fires the relay before the item hides, and the relay
re-activates the still visible, armed MP40, whose activate path picks it up
again. The second `weapon` give overflows and fails; the second
`inv_ammo_mp40 32` succeeds. The native runs record `ammo_mp40` 32 → 64 (cap
200), then clip 32 / reserve 32, `ammo_mdl24` 3 (cap 3) and the grenade slot
owned. This reproduces the Static order and give semantics natively; it is
not an observation of the original game. A dynamite already picked up and
hidden is shown again by a later relay fire (activate on a hidden item) and
is still removed at its first pickup time + 5000 ms.

## Player flags — `SourcePlayerFlagsV1`

The bytes live on the native source player (`WolfSourcePlayerFlags`) and are
reset at spawn and respawn.

| Byte | Writers | Native consumer |
|---|---|---|
| `+0x958` bit 3, weapons disabled | EnablePlayerWeapons (357) | the source weapon update clears the attack input (`0x20300bfc`); the weapon stays raised and visible; CanPickup/GiveItem refuse `inv_weapon` items. The GPL `enableWeapon`/`disableWeapon` events are no longer sent; they are no-ops in the original. |
| `+0x957` bit 0x10, lock | LockPlayer (185), UnlockPlayer (186) | the crouch toggle is skipped (`0x203008e4`): a locked player keeps its crouch state. The wider clearing at `0x20300c3d` (movement, look and other input fields, whose meanings are Inferred) is **refused** (not mapped). |
| `+0x5f9`, undying | SetUndying (253) on the player | player Damage keeps health at 1 (`0x203130e0`) |
| `+0x960`, undying-until | `rvFuncSaveGame` (M7, not native) | the same Damage clamp while now < until |
| ent `+0x158` bit 3, takedamage | EnableDamage (252) | GPL `fl.takedamage`; idEntity/idPlayer Damage return early |
| ent `+0x158` bit 0, notarget | NoTargetSet (102) | GPL `fl.notarget`; no native source-AI target acquisition exists yet, so nothing reads it |
| ent `+0x159` bit 7, usable | SetUsable (75) | recorded per native entity generation; the player use-focus selection (`0x203142a0`) is not native, and neither the pickups nor the chain need a player Use action |

The undying clamp is implemented as a `Killed` override on the native source
player that applies only inside its `Damage`: the GPL pain reaction after the
clamp is not reproduced. The damage-flag exceptions (`0x40`, `0x10000`) and
the `+0x965` mode are not mapped.

Correction to [MISSION-SYSTEMS](MISSION-SYSTEMS.md) §7: the focus function
`0x203142a0` tests `+0x957` bit 0x10 and `+0x159` of the **candidate entity**,
not of the local player. Only the input handler reads the player's lock bit.

No trainyard producer of an initial weapons-disabled state exists:
`weapons_disabled` appears only in tavern data, and `SetWeaponNone` on
`info_player_start` has no traced consumer. The native player therefore
starts with bit 3 clear, unarmed (`weapon_none`).

## Developer MP40 grant

`wolf2009DevEquipMP40` is a QA/development path. It refuses unless the cheat
cvar `wolf2009_qaDevWeaponGrant` is 1, and it never writes inventory
ownership, so PlayerHasWeapon stays false. The APK's manual MP40 key needs
that cvar.

## Verification

Trainyard runs and the declared QA stand-ins:
[tests/source-pickups](../tests/source-pickups/README.md). Portable checks:
`wolf2009-source-inventory` (22 checks) and `wolf2009-brain-actions` (44
checks).

The player/MP40 regression on original `empty` passed again with this code
in run `a139744211d1`: MP40 with and without the view 924 checks each,
session 25 checks.

## Limits

Native movement, eye height, clock, scalar damage, the ammo split, the touch
cube and base diffuse are explicit compatibility policies. Retail
keepDistance, camera recoil and spread, shader parity, muzzle and impact
effects, weapon and pickup sounds, item world models and the HUD are not
reconstructed. Saves, AI combat and missions are not implemented; no mission
is playable.

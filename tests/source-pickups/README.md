# Native trainyard pickup and player-flag checks

`TrainyardPickupChecks.cpp` runs `SourcePickupInventoryV1` and
`SourcePlayerFlagsV1` inside the ordinary trainyard session with the AI shell
and the measured exit (shake_train), through the GPL Doom 3 game module.

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 -WithAnimationV2Checks `
  -WithAudioBankChecks -Jobs 1
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -RendererGameRoot 'C:\path\to\Wolfenstein' -AudioBankGameRoot 'C:\path\to\Wolfenstein' `
  -WithAudioBankChecks -WithTrainyardPickupChecks -PickupCase Main   # or Disabled
```

## Declared QA stand-ins

- **Teleport.** The route from the wagon to the pickups needs the sewer,
  ladder and allies stages, which are not native. After shake_train ends,
  the QA places the player on the floor east of the MP40 (z 336; the items
  lie on a table top at z 377 with a stairwell to the north) and logs the
  floor grid. From there the player moves only through ordinary user
  commands and native touch contacts.
- **`activate_weapons`.** The original fires it from frame 600 of
  `srp_trn_nml_2005_ee` on allies_contact_1, which has no native AI clip
  playback. The QA sends the native `EV_Activate` to the relay with no
  activator.
- **Synthetic brain actions.** `EnablePlayerWeapons(false/true)`,
  `EnableDamage`, `SetUndying`, `LockPlayer`/`UnlockPlayer` and `SetUsable`
  are applied as specs labelled `qa-synthetic` through the production
  handlers. shake_train node 31 (EnablePlayerWeapons true) and node 34
  (SetUndying false) run from the original BRAI.
- **Undying-until.** The QA writes the player's `+0x960` time directly,
  standing in for `rvFuncSaveGame` (M7).

## Checks

Main:

1. Start: `weapon_none`, no inventory weapon, weapons-disabled clear, three
   live items with touch cubes (half-size 32), flamethrower and tip
   spawn-inhibited. allies 293/294 PlayerHasWeapon (from the original
   records) is false.
2. shake_train node 31 writes the native flag once (enabled).
3. Touching the unarmed MP40 is refused.
4. `activate_weapons` arms each item once (flamethrower inhibited).
5. One MP40 touch: two MP40 pickups (touch, then the relay's re-activation),
   one dynamite and one grenade pickup, three same-frame relay re-entries
   ignored, dispatch depth 2. Gives: `ammo_mp40` 32 → 64, the second weapon
   give refused by overflow. The source weapon becomes the MP40 with clip 32,
   reserve 32. PlayerHasWeapon flips false → true once.
6. Double touch: 20 more frames in the volumes pick up nothing.
7. Firing: 4 shots, then none for 30 held frames while weapons are disabled
   (the weapon stays visible), then 4 more after enabling.
8. Removal: each item is removed 5000–5100 ms after its first pickup; a later
   relay fire records the three stale targets as `target-expired`.
9. Damage is ignored without takedamage; the undying byte and the
   undying-until time each keep health at 1; damage after the window applies.
10. Locked, the player cannot crouch; unlocked, it crouches and stands.
11. SetUsable(train_greeter, false) is recorded.

Disabled: weapons are disabled before arming. The MP40 touch is refused by
CanPickup. Touching the dynamite picks it up (twice, through the relay) while
the relay's MP40 and grenade pickups are refused by GiveItem and both stay
armed and visible. After enabling, one MP40 touch collects the MP40 (twice)
and the grenades, and the relay shows the hidden dynamite again; it is still
removed at its first pickup time + 5000 ms.

Teardown must report `live_pickup_owners=0 live_items=0 live_entity_flags=0`
together with the AI shell, exit and voice-stream counters.

## Results (Android 15 x86_64 emulator, original trainyard, headless)

| Run | Case | Result |
|---|---|---|
| `7d016cb86f81` | Main | 985 checks / 705 frames, `live_banks=0` |
| `0c0bb7b1e270` | Disabled | 1307 checks / 774 frames |

Regressions on the same build: AI shell chain `9eabc3f38921` (11430 / 1192),
trainyard exit `d880d2b2387f` (5970 / 1298), player/MP40 session on original
`empty` `a139744211d1`. The AI shell chain fails when the test also passes
`-WithAudioBankChecks` (the bank QA then starts with the shell's banks live);
the reference runs pass it only to the build.

This is component evidence, not a playable mission: the route to the pickups,
the allies stage and the patrol encounter are not native.

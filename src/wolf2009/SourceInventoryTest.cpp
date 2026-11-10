// SPDX-License-Identifier: GPL-3.0-or-later
// Authored tables only (no original data); the slot names follow the player
// definition's def_weapon layout recorded in docs/PLAYER-WEAPON.md.
#include "SourceInventory.h"
#include <iostream>
#include <stdexcept>

namespace wolf2009 {
namespace {
unsigned checks = 0;
void Need(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
SourceInventoryProfile Profile() {
    SourceInventoryProfile p;
    const char *slots[12] = {"weapon_none", "weapon_fists", "weapon_mp40", "weapon_mauser98", "weapon_mp43",
        "weapon_panzerschreck", "weapon_flammenwerfer", "weapon_plasma_cannon", "weapon_tesla", "weapon_lf44",
        "weapon_grenade", "weapon_pendant"};
    for (unsigned i = 0; i < 12; ++i) p.weaponSlots[i] = slots[i];
    p.weaponAmmoTypes[2] = "ammo_mp40";
    p.weaponAmmoCapacity[2] = 200;
    p.weaponAmmoCapacityKnown[2] = true;
    p.weaponAmmoTypes[10] = "ammo_mdl24";
    p.weaponAmmoCapacity[10] = 4;
    p.weaponAmmoCapacityKnown[10] = true;
    return p;
}
const SourceItemFields Mp40 = {{"def_AmmoType", "ammo_mp40"}, {"inv_ammo_mp40", "32"}, {"inv_item", "1"},
                               {"inv_name", "#str"}, {"inv_weapon", "weapon_mp40"}};
const SourceItemFields Grenades = {{"inv_ammo_mdl24", "3"}, {"inv_name", "#str"}, {"inv_weapon", "weapon_grenade"}};
const SourceItemFields Dynamite = {{"inv_generic", "1"}, {"inv_icon", "x"}, {"inv_item", "1"}, {"inv_name", "#str"}};

void Weapons() {
    SourceInventory inventory(Profile());
    Need(!inventory.HasWeapon("weapon_mp40") && !inventory.HasWeapon("weapon_unknown"), "fresh inventory owns nothing");
    std::string why;
    bool expression = false;
    Need(!inventory.CanPickup(Mp40, true, expression, why) && !why.empty(), "MP40 refused while weapons are disabled");
    auto disabled = inventory.GiveItem(Mp40, true);
    Need(!disabled.given && disabled.refusedWeaponsDisabled && disabled.receipts.empty(), "GiveItem refuses inv_weapon while disabled");
    Need(inventory.CanPickup(Mp40, false, expression, why) && expression, "MP40 admitted when enabled");
    auto first = inventory.GiveItem(Mp40, false);
    Need(first.given && inventory.HasWeapon("weapon_mp40") && inventory.Ammo(0).amount == 32, "first MP40 give");
    // A second give of the same item (relay re-activation before hide).
    auto second = inventory.GiveItem(Mp40, false);
    unsigned weaponRefusals = 0, ammoGives = 0;
    for (const auto &r : second.receipts) {
        weaponRefusals += r.key == "weapon" && r.outcome == "refused" && r.overflow == 1;
        ammoGives += r.key == "ammo_mp40" && r.outcome == "given";
    }
    Need(second.given && weaponRefusals == 1 && ammoGives == 1 && inventory.Ammo(0).amount == 64,
         "second give: weapon add overflows, ammo still adds");
    for (int i = 0; i < 5; ++i) inventory.GiveItem(Mp40, false);
    Need(inventory.Ammo(0).amount == 200 && inventory.Ammo(0).Full(), "ammo clamps at InvAmmoCapacity");
    Need(!inventory.CanPickup(Mp40, false, expression, why), "owned weapon with full ammo refused");
    auto full = inventory.GiveItem(Mp40, false);
    Need(!full.given, "full ammo and owned weapon give nothing");
    float taken = 0;
    Need(inventory.TransferAmmoToController("ammo_mp40", taken, why) && taken == 200 && inventory.Ammo(0).amount == 0,
         "controller takes the pool");
    Need(inventory.Give("ammo_mp40", "20").outcome == "refused" && !inventory.TransferAmmoToController("ammo_mp40", taken, why),
         "post-equip ammo gives and repeated transfers are refused");
}

void Others() {
    SourceInventory inventory(Profile());
    std::string why;
    bool expression = false;
    Need(inventory.CanPickup(Dynamite, true, expression, why) && expression, "dynamite has no weapon gate");
    auto dynamite = inventory.GiveItem(Dynamite, true);
    Need(dynamite.given && !dynamite.refusedWeaponsDisabled, "dynamite accepted while weapons are disabled (generic)");
    Need(!inventory.GiveItem(Grenades, true).given, "grenades refused while weapons are disabled");
    auto grenades = inventory.GiveItem(Grenades, false);
    Need(grenades.given && inventory.HasWeapon("weapon_grenade") && inventory.Ammo(5).amount == 3, "grenade weapon and ammo");
    inventory.GiveItem(Grenades, false);
    Need(inventory.Ammo(5).amount == 4, "mdl24 clamps at its capacity");
    Need(inventory.Give("ammo_flame", "100").outcome == "refused", "undeclared capacity refused");
    Need(inventory.Give("name", "x").outcome == "ignored" && inventory.Give("health", "10").outcome == "ignored",
         "non-inventory keys give nothing");
    const SourceItemFields pickupOnly = {{"inv_weapon", "weapon_grenade"}, {"inv_zz", "1"}, {"weapon_pickup_only", "1"}};
    auto stopped = inventory.GiveItem(pickupOnly, false);
    Need(!stopped.given && stopped.receipts.size() == 1, "weapon_pickup_only stops after a failed weapon give");
    const SourceItemFields ammo = {{"ammo_type", "ammo_mp40"}, {"inv_ammo_mp40", "20"}};
    Need(inventory.CanPickup(ammo, false, expression, why) && !expression, "ammo_type item admitted without the expression gate");
    Need(SourceAmmoSlotIndex("ammo_shroud") == 8 && SourceAmmoSlotIndex("ammo_none") == -1, "static ammo table");
    SourceInventoryProfile bad = Profile();
    bad.weaponSlots[3] = "weapon_mp40";
    bool threw = false;
    try { SourceInventory duplicate(bad); } catch (const std::exception &) { threw = true; }
    Need(threw, "duplicate slot names refused");
}
} // namespace

int RunSourceInventoryTests() {
    checks = 0;
    try {
        Weapons();
        Others();
        std::cout << "SOURCE_INVENTORY_PORTABLE_OK checks=" << checks << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "SOURCE_INVENTORY_PORTABLE_FAIL checks=" << checks << " reason=" << error.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

#ifdef WOLF2009_SOURCE_INVENTORY_TEST_MAIN
int main() { return wolf2009::RunSourceInventoryTests(); }
#endif

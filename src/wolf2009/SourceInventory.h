// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_INVENTORY_H
#define WOLF2009_SOURCE_INVENTORY_H
// Portable player inventory and item-give contracts (profile
// SourcePickupInventoryV1). No entity, file or engine type is used here.
//
// Static contracts (hash-identified Gamex86.dll 7cb1cbed..., docs/PLAYER-WEAPON.md):
// - RvPlayerInventory owns 12 weapon slots (+0x84) named by the player's
//   def_weapon0..11. A slot (RvInventorySlot) holds amount +0x38 and maximum
//   +0x3c; it is owned when amount > 0 (slot +0x8, 0x2038b090). Add (slot +0x14,
//   0x2038b3c0) adds |x| and clamps at the maximum; the excess is returned.
// - Nine ammo slots are indexed by a fixed name table (0x203544e0):
//   ammo_mp40 0, ammo_mp43 1, ammo_panzer 2, ammo_mauser 3,
//   ammo_particle_cannon 4, ammo_mdl24 5, ammo_flame 6, ammo_tesla 7,
//   ammo_shroud 8. A weapon's InvAmmoCapacity sets its ammo slot maximum
//   (0x20358134, RvWeapon slot +0x20).
// - RvInventory Give (0x20389840): "weapon" adds 1 to the named weapon slot
//   and fails when that add overflows (already owned); "ammo_*" fails when the
//   ammo slot is full, otherwise adds; RvPlayerInventory (0x2038f2b0) also
//   accepts "generic" and "collectible". Other keys give nothing.
// - Player GiveItem (0x2031bb10): refuses an item with inv_weapon while the
//   player's weapons are disabled; gives every inv_* key without its prefix
//   and succeeds when any give succeeds. A failed "weapon" give stops the loop
//   only for weapon_pickup_only items.
// - idItem CanPickup (0x20092b90), inventory part: inv_weapon items are refused
//   while weapons are disabled, when the weapon slot is unknown, or when its
//   ammo slot is full; ammo_type items are refused when the ammo slot is
//   unknown or full.
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wolf2009 {
struct SourceInventorySlot {
    std::string name;
    float amount = 0, maximum = 0;
    bool maximumKnown = false;
    bool Owned() const { return amount > 0; }
    bool Full() const { return maximumKnown && amount >= maximum; }
};
// Data supplied by the caller from original declarations.
struct SourceInventoryProfile {
    std::array<std::string, 12> weaponSlots;          // def_weapon0..11
    // Per weapon slot: its def_AmmoType and InvAmmoCapacity, when declared.
    std::array<std::string, 12> weaponAmmoTypes;
    std::array<float, 12> weaponAmmoCapacity{};
    std::array<bool, 12> weaponAmmoCapacityKnown{};
};
const std::array<const char *, 9> &SourceAmmoSlotNames();
int SourceAmmoSlotIndex(const std::string &name);  // -1 when absent

struct SourceGiveReceipt {
    std::string key, value, outcome, reason;  // outcome: given, refused, ignored
    float before = 0, after = 0, overflow = 0;
};
struct SourceGiveItemResult {
    bool given = false, refusedWeaponsDisabled = false;
    std::vector<SourceGiveReceipt> receipts;
};
// Item fields: the effective entity dictionary in its iteration order.
typedef std::vector<std::pair<std::string, std::string> > SourceItemFields;

class SourceInventory {
  public:
    explicit SourceInventory(const SourceInventoryProfile &);
    const SourceInventorySlot &Weapon(std::size_t slot) const { return weapons_.at(slot); }
    const SourceInventorySlot &Ammo(std::size_t slot) const { return ammo_.at(slot); }
    int WeaponIndex(const std::string &name) const;
    // PlayerHasWeapon (448) via inventory slot +0x90 (0x203879a0): the
    // ownership of the slot whose name matches, false when none matches.
    bool HasWeapon(const std::string &name) const;
    SourceGiveReceipt Give(const std::string &key, const std::string &value);
    SourceGiveItemResult GiveItem(const SourceItemFields &, bool weaponsDisabled);
    // CanPickup, inventory part. Returns false with a reason when refused;
    // `expressionApplies` reports whether the item's optional expression gate
    // is consulted next (it is skipped for ammo_type items).
    bool CanPickup(const SourceItemFields &, bool weaponsDisabled, bool &expressionApplies, std::string &reason) const;
    // The native weapon controller takes the rounds of an owned weapon. The
    // pool drops to zero and later gives to that ammo type are refused until a
    // shared pool exists (explicit, see docs/PLAYER-WEAPON.md).
    bool TransferAmmoToController(const std::string &ammoType, float &taken, std::string &reason);
    bool AmmoTransferred(int ammoSlot) const { return ammoSlot >= 0 && ammoSlot < 9 && transferred_[std::size_t(ammoSlot)]; }
    unsigned Gives() const { return gives_; }
  private:
    std::array<SourceInventorySlot, 12> weapons_;
    std::array<std::string, 12> weaponAmmoTypes_;
    std::array<SourceInventorySlot, 9> ammo_;
    std::array<bool, 9> transferred_{};
    unsigned gives_ = 0;
};
int RunSourceInventoryTests();
} // namespace wolf2009
#endif

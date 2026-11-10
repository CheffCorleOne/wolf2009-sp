// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceInventory.h"
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
bool StartsWith(const std::string &text, const char *prefix) {
    const std::string p(prefix);
    return text.size() >= p.size() && text.compare(0, p.size(), p) == 0;
}
// atoi-compatible integer text (the original reads values with atoi).
int AtoiText(const std::string &text) { return std::atoi(text.c_str()); }
const std::string *Field(const SourceItemFields &fields, const char *key) {
    for (const auto &field : fields)
        if (field.first == key) return &field.second;
    return nullptr;
}
bool BoolField(const SourceItemFields &fields, const char *key) {
    const auto *value = Field(fields, key);
    return value && AtoiText(*value) != 0;
}
// RvInventorySlot Add (0x2038b3c0): amount += |x|, clamp at maximum, return excess.
float Add(SourceInventorySlot &slot, float value) {
    const float magnitude = std::fabs(value);
    if (!(magnitude > 0)) return 0;
    slot.amount += magnitude;
    if (slot.maximumKnown && slot.amount > slot.maximum) {
        const float excess = slot.amount - slot.maximum;
        slot.amount = slot.maximum;
        return excess;
    }
    return 0;
}
} // namespace

const std::array<const char *, 9> &SourceAmmoSlotNames() {
    static const std::array<const char *, 9> names = {{"ammo_mp40", "ammo_mp43", "ammo_panzer", "ammo_mauser",
        "ammo_particle_cannon", "ammo_mdl24", "ammo_flame", "ammo_tesla", "ammo_shroud"}};
    return names;
}
int SourceAmmoSlotIndex(const std::string &name) {
    const auto &names = SourceAmmoSlotNames();
    for (std::size_t i = 0; i < names.size(); ++i)
        if (name == names[i]) return int(i);
    return -1;
}

SourceInventory::SourceInventory(const SourceInventoryProfile &profile) {
    for (std::size_t i = 0; i < 12; ++i) {
        Need(!profile.weaponSlots[i].empty() && profile.weaponSlots[i].size() <= 256, "Inventory weapon slot name is absent");
        for (std::size_t j = 0; j < i; ++j) Need(profile.weaponSlots[j] != profile.weaponSlots[i], "Inventory weapon slot names repeat");
        weapons_[i].name = profile.weaponSlots[i];
        weaponAmmoTypes_[i] = profile.weaponAmmoTypes[i];
        // Inferred: a weapon slot holds at most one weapon. A second "weapon"
        // give overflows its add and fails (0x2038a03f), as Static requires.
        weapons_[i].maximum = 1;
        weapons_[i].maximumKnown = true;
    }
    const auto &names = SourceAmmoSlotNames();
    for (std::size_t i = 0; i < 9; ++i) ammo_[i].name = names[i];
    for (std::size_t i = 0; i < 12; ++i) {
        if (!profile.weaponAmmoCapacityKnown[i]) continue;
        const int ammo = SourceAmmoSlotIndex(profile.weaponAmmoTypes[i]);
        Need(ammo >= 0, "Weapon ammo capacity names an ammo type outside the static table");
        Need(std::isfinite(profile.weaponAmmoCapacity[i]) && profile.weaponAmmoCapacity[i] >= 0 &&
             profile.weaponAmmoCapacity[i] <= 100000, "Weapon ammo capacity is not a bounded number");
        auto &slot = ammo_[std::size_t(ammo)];
        Need(!slot.maximumKnown || slot.maximum == profile.weaponAmmoCapacity[i],
             "Two weapons declare different capacities for one ammo type");
        slot.maximum = profile.weaponAmmoCapacity[i];
        slot.maximumKnown = true;
    }
}

int SourceInventory::WeaponIndex(const std::string &name) const {
    for (std::size_t i = 0; i < weapons_.size(); ++i)
        if (weapons_[i].name == name) return int(i);
    return -1;
}

bool SourceInventory::HasWeapon(const std::string &name) const {
    const int slot = WeaponIndex(name);
    return slot >= 0 && weapons_[std::size_t(slot)].Owned();
}

SourceGiveReceipt SourceInventory::Give(const std::string &key, const std::string &value) {
    SourceGiveReceipt r;
    r.key = key;
    r.value = value;
    ++gives_;
    if (key == "weapon") {
        const int slot = WeaponIndex(value);
        if (slot < 0) { r.outcome = "refused"; r.reason = "no weapon slot carries this name"; return r; }
        auto &s = weapons_[std::size_t(slot)];
        r.before = s.amount;
        r.overflow = Add(s, 1.0f);
        r.after = s.amount;
        if (r.overflow != 0) { r.outcome = "refused"; r.reason = "weapon slot add overflowed (already owned)"; return r; }
        r.outcome = "given";
        return r;
    }
    if (StartsWith(key, "ammo_")) {
        const int slot = SourceAmmoSlotIndex(key);
        if (slot < 0) { r.outcome = "refused"; r.reason = "ammo type outside the static nine-slot table"; return r; }
        auto &s = ammo_[std::size_t(slot)];
        r.before = s.amount;
        r.after = s.amount;
        if (!s.maximumKnown) { r.outcome = "refused"; r.reason = "ammo slot capacity is not declared by an owned weapon definition"; return r; }
        if (transferred_[std::size_t(slot)]) {
            r.outcome = "refused";
            r.reason = "ammo pool is held by the native weapon controller; post-equip ammo gives are outside this profile";
            return r;
        }
        if (s.Full()) { r.outcome = "refused"; r.reason = "ammo slot is full"; return r; }
        // The "dropped" random halving applies to dropped items only.
        r.overflow = Add(s, float(AtoiText(value)));
        r.after = s.amount;
        r.outcome = "given";
        return r;
    }
    if (key == "generic" || key == "collectible") {
        r.outcome = "given";
        r.reason = "RvPlayerInventory accepts " + key;
        return r;
    }
    if (key == "health" || key == "maxhealth") {
        r.outcome = "ignored";
        r.reason = "player health give is outside SourcePickupInventoryV1";
        return r;
    }
    r.outcome = "ignored";
    r.reason = "inventory gives nothing for this key";
    return r;
}

SourceGiveItemResult SourceInventory::GiveItem(const SourceItemFields &fields, bool weaponsDisabled) {
    SourceGiveItemResult result;
    if (weaponsDisabled && Field(fields, "inv_weapon")) {
        result.refusedWeaponsDisabled = true;
        return result;
    }
    const bool pickupOnly = BoolField(fields, "weapon_pickup_only");
    for (const auto &field : fields) {
        if (!StartsWith(field.first, "inv_")) continue;
        auto receipt = Give(field.first.substr(4), field.second);
        const bool given = receipt.outcome == "given";
        result.receipts.push_back(receipt);
        if (given) { result.given = true; continue; }
        if (receipt.key == "weapon" && pickupOnly) break;
    }
    return result;
}

bool SourceInventory::CanPickup(const SourceItemFields &fields, bool weaponsDisabled, bool &expressionApplies,
                                std::string &reason) const {
    expressionApplies = true;
    reason.clear();
    if (const auto *weapon = Field(fields, "inv_weapon")) {
        if (weaponsDisabled) { reason = "inv_weapon item refused while weapons are disabled (player+0x958 bit 3)"; return false; }
        const int slot = WeaponIndex(*weapon);
        if (slot < 0) { reason = "inv_weapon names no weapon slot"; return false; }
        // 0x2038a570: the weapon's own ammo type (weapon definition, not the
        // item) must not be full; a weapon without an ammo type passes.
        const int ammo = SourceAmmoSlotIndex(weaponAmmoTypes_[std::size_t(slot)]);
        if (ammo >= 0 && ammo_[std::size_t(ammo)].Full()) { reason = "weapon ammo slot is full"; return false; }
        return true;
    }
    if (const auto *ammoType = Field(fields, "ammo_type")) {
        expressionApplies = false;
        const int ammo = SourceAmmoSlotIndex(*ammoType);
        if (ammo < 0) { reason = "ammo_type names no ammo slot"; return false; }
        if (ammo_[std::size_t(ammo)].Full()) { reason = "ammo slot is full"; return false; }
        return true;
    }
    return true;
}

bool SourceInventory::TransferAmmoToController(const std::string &ammoType, float &taken, std::string &reason) {
    taken = 0;
    reason.clear();
    const int ammo = SourceAmmoSlotIndex(ammoType);
    if (ammo < 0) { reason = "ammo type outside the static table"; return false; }
    if (transferred_[std::size_t(ammo)]) { reason = "ammo pool was already transferred"; return false; }
    taken = ammo_[std::size_t(ammo)].amount;
    ammo_[std::size_t(ammo)].amount = 0;
    transferred_[std::size_t(ammo)] = true;
    return true;
}
} // namespace wolf2009

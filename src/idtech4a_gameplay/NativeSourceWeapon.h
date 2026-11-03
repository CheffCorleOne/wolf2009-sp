// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_WEAPON_H
#define WOLF2009_NATIVE_SOURCE_WEAPON_H
// Game-private C++98 seam. No object here crosses GetGameAPI/API8.
class idPlayer;
class idEntity;
class idAnimatedEntity;
class idStr;
class WolfSourceDeclarationSet;

enum WolfSourceWeaponCommandKind {
    WolfSourceWeapon_WeaponAttack=1, WolfSourceWeapon_PointOfInterest=2,
    WolfSourceWeapon_SoundWeapon=3, WolfSourceWeapon_EyeAngles=4
};
struct WolfSourceWeaponFrameCommand {
    WolfSourceWeaponCommandKind kind;
    unsigned frame;
    const char* argument; // owning profile text; valid for the callback only
    float values[5];
    unsigned valueCount;
};
// A prepared immutable native source animation set, with source-time metadata.
// Retain/release are a pair; the controller retains the context until teardown.
// Dispatch implements all admitted non-attack commands. WeaponAttack is handled
// by the controller's native ray/Damage path, and must not be fired a second time.
struct WolfSourceWeaponVisual {
    const char* modelDefinition;
    unsigned fireFrames,reloadFrames;
    double fireDurationSeconds,reloadDurationSeconds;
    const void* context;
    void (*retain)(const void*);
    void (*release)(const void*);
    bool (*preflight)(const void*,idStr&);
    bool (*dispatch)(const void*,idAnimatedEntity*,const WolfSourceWeaponFrameCommand&,int,idStr&);
    bool (*place)(const void*,idAnimatedEntity*,idPlayer*,int,idStr&);
    WolfSourceWeaponVisual():modelDefinition(0),fireFrames(0),reloadFrames(0),
        fireDurationSeconds(0),reloadDurationSeconds(0),context(0),retain(0),release(0),preflight(0),dispatch(0),place(0){}
};
struct WolfSourceWeaponState {
    int clip,reserve,phase,lastShotTime,reloadEndTime,lastHitEntity;
    unsigned shots,emptyClicks,reloads,damageCalls,misses,frameCommands;
    bool hasViewModel,unsupported;
    const char* sourceWeapon; // valid until equip/detach/map teardown
    WolfSourceWeaponState():clip(0),reserve(0),phase(0),lastShotTime(-1),reloadEndTime(-1),lastHitEntity(-1),
        shots(0),emptyClicks(0),reloads(0),damageCalls(0),misses(0),frameCommands(0),hasViewModel(false),unsupported(false),sourceWeapon(0){}
};
// Initial campaign attachment admits weapon_none only. It consumes the selected
// namespace, not a stock Doom weapon definition or an invented starting MP40.
bool Wolf2009_AttachSourceWeapon(idPlayer*,const WolfSourceDeclarationSet&,const char* sourceWeapon,idStr& reason);
// Explicit MP40 equip for an admitted pickup/test. Source inventory accounting
// is supplied explicitly: clip and reserve are distinct bounded native counts.
// A null visual is the separately reported no-view diagnostic profile.
bool Wolf2009_EquipSourceWeapon(idPlayer*,const WolfSourceDeclarationSet&,const char* sourceWeapon,
    const WolfSourceWeaponVisual* visual,int clip,int reserve,idStr& reason);
void Wolf2009_DetachSourceWeapon(idPlayer*);
// Explicit native respawn policy: clear an equipped weapon/ammo to the retained
// weapon_none profile. It requires no live frontend or re-read of game data.
void Wolf2009_ResetSourceWeaponForPlayer(idPlayer*);
// Returns true if this player is source-controlled, including refused/disabled
// input. Insert before stock idPlayer::UpdateWeapon handling. Call once per game
// frame after CalculateFirstPersonView; enabled=false suppresses input/effects.
bool Wolf2009_UpdateSourceWeapon(idPlayer*,bool attackHeld,bool enabled=true);
// Returns true when the source controller consumed the reload request.
bool Wolf2009_ReloadSourceWeapon(idPlayer*);
bool Wolf2009_QuerySourceWeapon(const idPlayer*,WolfSourceWeaponState&,idStr& reason);
bool Wolf2009_IsSourceWeaponOwner(const idPlayer*);
int Wolf2009_LiveSourceWeapons();
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_PLAYER_H
#define WOLF2009_NATIVE_SOURCE_PLAYER_H
// Game-private C++98 interface; no GetGameAPI/API8 extension.
class idEntity;class idPlayer;class idPhysics_Player;class idClipModel;
class idDict;class idStr;class idVec3;
class WolfSourceDeclarationSet;
namespace wolf2009 {class SourcePlayerProfile;}
enum WolfSourcePlayerPolicy {
    WolfSourcePlayer_Unspecified=0,
    WolfSourcePlayer_NativeIdPlayerFirstPersonV1=1
};
struct WolfSourcePlayerRequest {
    WolfSourcePlayerPolicy policy;
    const char *playerDefinition,*sourceMapName,*startupDeclaration;
    const char *startupLibrary,*startupLibraryKey;
    const char *exactClipResourcePath,*exactStandBodyName,*exactCrouchBodyName;
    unsigned long long expectedClipBytes,expectedClipFingerprint;
    WolfSourcePlayerRequest():policy(WolfSourcePlayer_Unspecified),
        playerDefinition(0),sourceMapName(0),startupDeclaration(0),
        startupLibrary(0),startupLibraryKey(0),exactClipResourcePath(0),
        exactStandBodyName(0),exactCrouchBodyName(0),expectedClipBytes(0),expectedClipFingerprint(0){}
};
class WolfSourcePlayerPrepared {
  public:
    ~WolfSourcePlayerPrepared();
  private:
    struct Impl;Impl* impl;
    explicit WolfSourcePlayerPrepared(Impl*);
    WolfSourcePlayerPrepared(const WolfSourcePlayerPrepared&);
    WolfSourcePlayerPrepared& operator=(const WolfSourcePlayerPrepared&);
    friend WolfSourcePlayerPrepared* Wolf2009_PrepareSourcePlayer(
        const WolfSourceDeclarationSet&,const WolfSourcePlayerRequest&,idStr&);
    friend bool Wolf2009_InstallSourcePlayer(const WolfSourcePlayerPrepared&,idStr&);
};
WolfSourcePlayerPrepared* Wolf2009_PrepareSourcePlayer(const WolfSourceDeclarationSet&,
    const WolfSourcePlayerRequest&,idStr& reason);
bool Wolf2009_InstallSourcePlayer(const WolfSourcePlayerPrepared&,idStr& reason);
void Wolf2009_ClearSourcePlayer();
bool Wolf2009_HasSourcePlayerProfile();
// No installed profile means handled=false and legacy SpawnPlayer continues.
// With a profile all failures are handled=true: no silent stock fallback.
bool Wolf2009_TrySpawnSourcePlayer(int clientNum,const idDict&,
    idEntity*& out,bool& handled,idStr& reason);
bool Wolf2009_TrySetSourcePlayerClip(idPlayer*,idPhysics_Player&,
    bool& handled,idStr& reason);
// Root's CheckDuck hook owns PMF_DUCKED/playerSpeed. This function tests a
// complete stand hull at the SAME origin before growing, swaps exact stand/
// crouch clips through native LoadModel, and returns the accepted duck state.
// Source player death keeps its last valid source hull in this first policy.
bool Wolf2009_TrySourcePlayerDuck(idEntity*,idClipModel*,const idVec3& origin,
    bool wantsDuck,bool dead,bool& ducked,bool& handled,idStr& reason);
bool Wolf2009_IsSourcePlayer(const idEntity*);
const wolf2009::SourcePlayerProfile* Wolf2009_SourcePlayerProfile(const idEntity*);
// End-of-idPlayer::Init hook: applies the selected source loadout on Spawn and
// ordinary respawn after the GPL persistent dictionary has initialized itself.
void Wolf2009_SourcePlayerInitialized(idPlayer*);
struct WolfSourcePlayerState {
    bool sourcePlayer,exactLocalHull,ducked,bodyPlaceholder,retailCameraImplemented;
    int health,maximumHealth,startingGold,selectedSourceWeapon;
    unsigned sourceOwnedWeapons,nativeInventoryWeapons,standVertices,crouchVertices;
    unsigned standEdges,crouchEdges,standPolygons,crouchPolygons;
    float mass,keepDistance,normalEyeHeight,crouchEyeHeight;
    WolfSourcePlayerState():sourcePlayer(false),exactLocalHull(false),ducked(false),
        bodyPlaceholder(false),retailCameraImplemented(false),health(0),maximumHealth(0),
        startingGold(0),selectedSourceWeapon(-1),sourceOwnedWeapons(0),nativeInventoryWeapons(0),
        standVertices(0),crouchVertices(0),standEdges(0),crouchEdges(0),standPolygons(0),crouchPolygons(0),
        mass(0),keepDistance(0),normalEyeHeight(0),crouchEyeHeight(0){}
};
bool Wolf2009_QuerySourcePlayer(const idEntity*,WolfSourcePlayerState&,idStr& reason);
int Wolf2009_LiveSourcePlayers();

// SourcePlayerFlagsV1: the original player bytes as native player state.
// They live on the native source player entity, are reset by its Spawn/respawn
// initialization and end with it. Consumers (Static, Gamex86 7cb1cbed...):
// - weaponsDisabled = player+0x958 bit 3. The input handler clears the attack
//   fields (0x20300bfc); CanPickup/GiveItem refuse inv_weapon items.
// - locked = player+0x957 bit 0x10. The input handler skips the crouch toggle
//   (0x203008e4). Its wider input clearing (0x20300c3d) is not mapped.
// - undying = actor+0x5f9 and undyingUntilMs = player+0x960: idPlayer Damage
//   (0x203130e0) keeps health at 1 when it would fall below 1.
struct WolfSourcePlayerFlags {
    bool weaponsDisabled,locked,undying;
    int undyingUntilMs;
    unsigned weaponsDisabledWrites,lockWrites,undyingWrites;
    unsigned attackSuppressedFrames,crouchToggleRefusals,undyingClamps,damageCalls;
    WolfSourcePlayerFlags():weaponsDisabled(false),locked(false),undying(false),undyingUntilMs(-1),
        weaponsDisabledWrites(0),lockWrites(0),undyingWrites(0),attackSuppressedFrames(0),crouchToggleRefusals(0),
        undyingClamps(0),damageCalls(0){}
};
// Null for a non-source player.
WolfSourcePlayerFlags* Wolf2009_SourcePlayerFlags(idEntity*);
const WolfSourcePlayerFlags* Wolf2009_SourcePlayerFlags(const idEntity*);
namespace wolf2009 {class SourceInventory;}
// The source player's RvPlayerInventory model (SourcePickupInventoryV1); null
// for a non-source player or when the profile refused an inventory.
wolf2009::SourceInventory* Wolf2009_SourcePlayerInventory(idEntity*);
const wolf2009::SourceInventory* Wolf2009_SourcePlayerInventory(const idEntity*);
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_WEAPON_PRESENTATION_H
#define WOLF2009_NATIVE_SOURCE_WEAPON_PRESENTATION_H
// Game-private C++11 helper. No object or STL type crosses API8.
#include <memory>
#include <string>
#include "NativeSourceWeapon.h"
namespace wolf2009 {class SourceWeaponProfile;}
class WolfSourceDeclarationSet;
enum class WolfSourceWeaponPresentationPolicy {
    Unspecified, NativeWeaponLocalAimAndPointTrackingV1
};
struct WolfSourceWeaponPresentationRequest {
    WolfSourceWeaponPresentationPolicy policy=WolfSourceWeaponPresentationPolicy::Unspecified;
    std::string nativeModelDefinition,exactReloadSampleResource;
};
struct WolfSourceWeaponPresentationState {
    unsigned commands=0,pointCommands=0,eyeCommands=0,soundCommands=0,soundsStarted=0;
    int boundEntity=-1,lastCommandTime=-1,lastPlaceTime=-1,soundLengthMs=0;
    bool pointActive=false,pointPositionValid=false,soundEmitterPlaying=false;
    // Native compatibility state: POI tracks the actual animated joint in world
    // space. Eye angles rotate the weapon locally, leaving the player camera
    // and attack ray untouched. This is not the original viewcomponent solver.
    std::string pointJointName;
    float pointWeight=0,pointWorld[3]={0,0,0};
    float eyeTarget[3]={0,0,0},eyeApplied[3]={0,0,0},eyeRate=0,eyeGain=0;
};
class WolfSourceWeaponPresentation {
  public:
    ~WolfSourceWeaponPresentation();
    bool Preflight(idStr& reason) const;
    bool Dispatch(idAnimatedEntity*,const WolfSourceWeaponFrameCommand&,int timeMs,idStr& reason);
    bool Place(idAnimatedEntity*,idPlayer*,int timeMs,idStr& reason);
    bool Query(WolfSourceWeaponPresentationState&,idStr& reason) const;
    const char* NativeReloadSound() const;
    const char* ReloadSampleCapability() const;
    WolfSourceWeaponPresentation(const WolfSourceWeaponPresentation&)=delete;
    WolfSourceWeaponPresentation& operator=(const WolfSourceWeaponPresentation&)=delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceWeaponPresentation(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceWeaponPresentation> Wolf2009_PrepareSourceWeaponPresentation(
        const WolfSourceDeclarationSet&,const wolf2009::SourceWeaponProfile&,
        const WolfSourceWeaponPresentationRequest&,idStr&);
};
// Requires an unchanged registered source animation set and exact source SNDS.
// It validates all authored non-attack commands/joints before binding audio.
// One presentation owns one native weapon entity generation. Prepare a fresh
// owner for another equip/entity; source metadata is retained independently.
std::unique_ptr<WolfSourceWeaponPresentation> Wolf2009_PrepareSourceWeaponPresentation(
    const WolfSourceDeclarationSet&,const wolf2009::SourceWeaponProfile&,
    const WolfSourceWeaponPresentationRequest&,idStr& reason);
#endif

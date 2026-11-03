// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_WEAPON_PROFILE_H
#define WOLF2009_SOURCE_WEAPON_PROFILE_H
#include "SourceDefinitions.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wolf2009 {
enum class SourceWeaponPolicy { Unspecified, NoneV1, MP40PrimaryNativeV1 };
enum class SourceWeaponCommandKind { WeaponAttack=1, PointOfInterest=2, SoundWeapon=3, EyeAngles=4 };
struct SourceWeaponCommand {
    SourceWeaponCommandKind kind=SourceWeaponCommandKind::WeaponAttack;
    unsigned frame=0; // authored one-based source frame, never a baked FPS
    std::string argument;
    std::vector<float> values;
    std::vector<SourceDefinitionFact> origins;
};
struct SourceWeaponConfiguration {
    std::string name,weaponclass,ammoType,viewModel,worldModel,detailPack,attackDefinition,damageDefinition;
    int clipSize=0,reserveCapacity=0,initialAmmo=0,pickupAmmo=0,ammoRequired=0,autoFireIntervalMs=0;
    float maxRange=0,damage=0,sourceAutoFireSeconds=0;
    // The complete source closures below retain sound-driven fire, spread,
    // kick, effects/upgrades etc. This profile implements primary unmodified
    // single-bullet fire through NativeEyeRaySingleBulletV1 and a separately
    // named native simulation clock; it does not claim those retail effects.
};
class SourceWeaponProfile {
  public:
    ~SourceWeaponProfile();
    SourceWeaponPolicy Policy() const;
    const SourceWeaponConfiguration& Configuration() const;
    const ResolvedSourceDefinition& Weapon() const;
    const ResolvedSourceDefinition& Ammo() const;
    const ResolvedSourceDefinition& Attack() const;
    const ResolvedSourceDefinition& Damage() const;
    const ResolvedSourceModelAnimations& ViewModel() const;
    const ResolvedSourceModelAnimations& WorldModel() const;
    const std::vector<ResolvedSourceDefinition>& EffectDefinitions() const;
    const std::vector<SourceWeaponCommand>& FireCommands() const;
    const std::vector<SourceWeaponCommand>& ReloadCommands() const;
    SourceWeaponProfile(const SourceWeaponProfile&)=delete;
    SourceWeaponProfile& operator=(const SourceWeaponProfile&)=delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit SourceWeaponProfile(std::unique_ptr<Impl>);
    friend std::shared_ptr<const SourceWeaponProfile> PrepareSourceWeaponProfile(
        const SourceDefinitions&,const std::string&,SourceWeaponPolicy,const SourceDefinitionContext&);
};
// Compiled typed declarations only. Every input closure owns its complete
// numeric words, opaque fields, raw animation commands and source provenance.
// Missing/malformed selected children never expose inherited fallback values.
std::shared_ptr<const SourceWeaponProfile> PrepareSourceWeaponProfile(
    const SourceDefinitions&,const std::string& name,SourceWeaponPolicy,
    const SourceDefinitionContext& = SourceDefinitionContext());

enum class SourceWeaponClockPolicy { Unspecified, NativeSimulationMillisecondsV1 };
struct SourceWeaponTiming {
    SourceWeaponClockPolicy policy=SourceWeaponClockPolicy::Unspecified;
    unsigned fireFrames=0,reloadFrames=0;
    double fireDurationSeconds=0,reloadDurationSeconds=0;
};
enum class SourceWeaponPhase { None, Ready, Reloading, Disabled };
enum class SourceWeaponEventKind { StartFire, StartReload, ReloadComplete, EmptyClick, FrameCommand, Disabled };
struct SourceWeaponEvent {
    SourceWeaponEventKind kind=SourceWeaponEventKind::FrameCommand;
    // Index into FireCommands or ReloadCommands according to reloadCommand.
    std::size_t command=0;
    bool reloadCommand=false;
    int timeMs=0; // native dispatch time; source frame remains in the command
};
struct SourceWeaponRuntimeState {
    SourceWeaponPhase phase=SourceWeaponPhase::None;
    int clip=0,reserve=0,lastTimeMs=-1,lastShotTimeMs=-1,reloadEndTimeMs=-1;
    unsigned shots=0,emptyClicks=0,reloads=0,frameCommands=0;
    bool attackHeld=false;
};
// Portable, independently authored native compatibility controller. The first
// press fires immediately; subsequent Hold shots require now>last+interval.
// There is at most one shot per simulation step, no time-jump bullet burst.
// Ammo is an explicit caller-selected clip+reserve pair (not inferred from
// retail InvInitialAmmo accounting). Reload commits only at its source-duration
// ceil-ms endpoint. Disabling/holstering cancels reload without moving ammo.
// Source frame commands dispatch once on crossings, including frame1 at start.
// All validation precedes mutation; backwards clocks reject transactionally.
class SourceWeaponController {
  public:
    SourceWeaponController(std::shared_ptr<const SourceWeaponProfile>,const SourceWeaponTiming&,int clip,int reserve);
    const SourceWeaponRuntimeState& State() const;
    const SourceWeaponProfile& Profile() const;
    std::vector<SourceWeaponEvent> Step(int timeMs,bool attackHeld,bool requestReload,bool enabled=true);
  private:
    std::shared_ptr<const SourceWeaponProfile> profile_;
    SourceWeaponTiming timing_;
    SourceWeaponRuntimeState state_;
    int animationStartMs_=-1;
    bool reloadAnimation_=false,emptyLatched_=false;
    std::size_t nextCommand_=0;
};
}
#endif

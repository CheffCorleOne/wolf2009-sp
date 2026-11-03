// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_PLAYER_PROFILE_H
#define WOLF2009_SOURCE_PLAYER_PROFILE_H
#include "SourceDefinitions.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace wolf2009 {
enum class SourcePlayerPolicy { Unspecified, NativeIdPlayerFirstPersonV1 };
struct SourcePlayerProfileRequest {
    SourcePlayerPolicy policy=SourcePlayerPolicy::Unspecified;
    std::string playerDefinition,sourceMapName,startupDeclaration;
    // Explicit library/key/name cross-check. No map-path normalization guesses.
    std::string startupLibrary,startupLibraryKey;
};
struct SourcePlayerNativeControls {
    // Observed compiled defaults mapped to the GPL controller. These are not
    // a claim of retail camera, acceleration, sprint or keepDistance parity.
    float normalHeight=72,crouchHeight=36,normalEyeHeight=64,crouchEyeHeight=30;
    float walkSpeed=100,runSpeed=140,crouchSpeed=80,jumpHeight=48;
};
class SourcePlayerProfile {
  public:
    ~SourcePlayerProfile();
    SourcePlayerPolicy Policy()const;
    const SourcePlayerProfileRequest& Request()const;
    const ResolvedSourceDefinition& Player()const;
    const ResolvedSourceDefinition& Startup()const;
    const ResolvedSourceDefinition& StartupLibrary()const;
    const ResolvedSourceModelAnimations& SourceModel()const;
    const SourcePlayerNativeControls& Controls()const;
    const std::array<std::string,12>& WeaponSlots()const;
    const std::vector<unsigned>& DefaultWeaponSlots()const;
    const std::vector<unsigned>& StartingWeaponSlots()const;
    unsigned StartingWeaponSlot()const;
    unsigned DefaultWeaponSlot()const;
    int Health()const;
    int MaximumHealth()const;
    int StartingGold()const;
    float Mass()const;
    float CapsuleFallbackRadius()const;
    float KeepDistance()const;
    float ExtraGravity()const;
    const std::string& StandClipReference()const;
    const std::string& CrouchClipReference()const;
    // Every source field remains in Player/Startup. These lists name active
    // source systems which this native first-person policy does not implement.
    const std::vector<std::string>& UnsupportedPlayerFeatures()const;
    const std::vector<std::string>& UnsupportedStartupFields()const;
    SourcePlayerProfile(const SourcePlayerProfile&)=delete;
    SourcePlayerProfile& operator=(const SourcePlayerProfile&)=delete;
  private:
    struct Impl;std::unique_ptr<Impl> impl;
    explicit SourcePlayerProfile(std::unique_ptr<Impl>);
    friend std::shared_ptr<const SourcePlayerProfile> PrepareSourcePlayerProfile(
        const SourceDefinitions&,const SourcePlayerProfileRequest&);
};
std::shared_ptr<const SourcePlayerProfile> PrepareSourcePlayerProfile(
    const SourceDefinitions&,const SourcePlayerProfileRequest&);

struct SourcePlayerClip {
    std::string selectedBodyName,sourceType;
    std::size_t sourceBytes=0;
    std::uint64_t sourceFingerprint=0;
    std::uint32_t bodyOffset=0,shapeOffset=0,rawFilter=0,rawRadiusBits=0;
    unsigned declaredVertices=0,supportBlocks=0;
    // All padded support lanes and raw planes survive. Native hulls use the
    // explicitly declared support vertices, rounded once to binary32 GU.
    std::vector<std::array<std::uint32_t,3>> rawSupportWords;
    std::vector<std::array<std::uint32_t,4>> rawPlaneWords;
    std::array<std::uint32_t,16> rawBodyTransform{};
    std::vector<std::array<float,3>> nativeLocalPoints;
    std::vector<unsigned char> source;
};
// Exact-name direct ConvexVertices HKXS profile, radius zero. Owns complete
// input and raw body transform; native local hull deliberately does not apply
// an asset-body/COM pose as an entity placement or invent capsule padding.
SourcePlayerClip DecodeSourcePlayerClip(const std::vector<unsigned char>&,
    const std::string& exactBodyName);
}
#endif

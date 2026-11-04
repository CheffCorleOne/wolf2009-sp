// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SUPPORT_GAMEPLAY_MAP_H
#define WOLF2009_SUPPORT_GAMEPLAY_MAP_H
#include "GameplayMap.h"
#include "SupportCollisionWorld.h"
#include "SourceMapBodies.h"
#include <cstdint>
#include <memory>
namespace wolf2009 {
// Explicit GPL gameplay compatibility policy. This does not provide the
// original BitsCollisionFilter's runtime userData masks or eligibility gate.
enum class SupportGameplayContentsPolicy {
    DeclaredDoom3BitsV1, NativeSourceWordsCylinderEnvelopeV1,
    NativeSolidEntityCylinderEnvelopeV1
};
struct SupportGameplayResponse {
    std::uint32_t contents = 0;
    std::string material;
};
std::vector<SupportGameplayResponse> AdmitSupportGameplayWorld(
    const SupportCollisionWorld&,SupportGameplayContentsPolicy);
// Explicit selected-body response profiles. The source document remains raw;
// admission does not set its retail nativeRunnable/contents coverage flags.
std::vector<SupportGameplayResponse> AdmitSelectedSupportGameplayBody(
    const SupportCollisionWorld&,SupportGameplayContentsPolicy);
SupportCollisionWorld DecodeSelectedSupportGameplayBody(const std::vector<unsigned char>&,
    const std::string& exactBodyName,SupportGameplayContentsPolicy,bool localResourceFrame=false);
struct SupportGameplayMap {
    SupportCollisionWorld world;
    std::vector<SupportGameplayResponse> responses;
    std::string canonicalMapName, mountLabel, identity, mapText, cmText;
    std::uint64_t mountEpoch = 0;
    std::shared_ptr<const SourceMapBodyPlan> bodies;
    SupportGameplayContentsPolicy contentsPolicy=SupportGameplayContentsPolicy::DeclaredDoom3BitsV1;
    bool nativeCylinderEnvelopeResponse=false;
    std::size_t sourceEntities = 0, unimplementedEntities = 0;
};
// Same-mount HKXS/ENTS snapshots. Output contains an authored start and a
// zero-geometry native CM; actual collision remains in the owning support world.
// Unknown contents/contact metadata, additional bodies and ambiguous starts
// are refused before any engine state is changed. No legacy brush fallback.
SupportGameplayMap PrepareSupportGameplayMap(const std::vector<unsigned char>& hkxs,
    const std::vector<unsigned char>& ents,const std::string& canonicalMapName,
    std::uint64_t mountEpoch,const std::string& requestedStart = std::string(),
    SupportGameplayContentsPolicy policy=SupportGameplayContentsPolicy::DeclaredDoom3BitsV1);
}
#endif

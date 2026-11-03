// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_HAVOK_TRIANGLE_QUERY_H
#define WOLF2009_HAVOK_TRIANGLE_QUERY_H
#include "HavokTriangleMesh.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace wolf2009 {
enum class HavokTriangleQueryStatus { Resolved, Unavailable, Unsupported, Invalid };
struct HavokTriangleQueryLimits {
    std::size_t rawArrayBytes = 128u << 20;
    std::size_t triangles = 1000000, vertices = 1000000;
    double coordinateGameUnits = 10000000;
};
struct HavokTriangleQueryResult {
    std::uint32_t sourceStorageOffset = 0, sourceRecordOffset = 0;
    std::uint64_t sourceRegion = 0;
    std::size_t triangleIndex = 0;
    HavokTriangleQueryStatus geometryStatus = HavokTriangleQueryStatus::Unavailable;
    HavokTriangleQueryStatus materialStatus = HavokTriangleQueryStatus::Unavailable;
    HavokTriangleQueryStatus weldingStatus = HavokTriangleQueryStatus::Unavailable;
    std::string geometryReason, materialReason, weldingReason;
    std::array<std::int32_t,3> indexSelectors{{0,1,2}};
    std::array<std::uint32_t,3> indices{{0,0,0}};
    // Native storage-local XYZ in GAME UNITS, including the observed storage
    // scale. Caller applies the already established rigid occurrence placement.
    std::array<CollisionVec3,3> positions;
    std::uint8_t rawIndexType = 0, rawFlipByte = 0;
    std::int32_t signedFlip = 0;
    std::uint32_t rawTriangleOffset = 0;
    std::int64_t weldingIndex = 0;
    std::uint32_t weldingArrayCount = 0;
    std::uint16_t weldingInfo = 0;
    std::uint8_t weldingType = 0;
    std::uint32_t rawRadiusBits = 0;
    double radiusGameUnits = 0;
    bool radiusFinite = false, extrusionFinite = false;
    std::array<std::uint32_t,4> rawExtrusionBits{};
    CollisionVec3 extrusionGameUnits;
    std::uint8_t rawMaterialIndexType = 0, effectiveMaterialIndexType = 0;
    std::uint16_t materialIndexByteStride = 0;
    std::int32_t materialByteStride = 0;
    std::uint32_t materialIndex = 0, rawCollisionFilterInfo = 0;
    bool nullMaterialIndexBase = false;
    // Data-only candidate: zero radius/extrusion and WELDING_TYPE_NONE(6).
    // This never certifies native CM, convex sweep or contact equivalence.
    bool flatNativeSurfaceCandidate = false;
};
// The StorageExtendedMesh finish-loaded profile rebinds record pointers to its
// retained owner arrays. No host IO, mutations, dangling result views, inferred
// contents/masks, MOPP traversal or contact response. Raw inputs stay intact.
// Geometry/material/welding resolve independently. Nonempty welding payloads
// are not retained by the current buffer reader: keep index/range facts and an
// explicit Unavailable status rather than inventing the selected word.
HavokTriangleQueryResult ResolveHavokTriangleQuery(
    const HavokTriangleStorage& storage, const HavokTriangleSubpart& subpart,
    std::size_t triangleIndex, const HavokTriangleQueryLimits& limits = HavokTriangleQueryLimits());

enum class HavokTriangleRayStatus { Hit, Miss, Unavailable, InvalidInput, FilteredOut };
struct HavokTriangleRayOptions {
    double maximumFraction = 1;
    // Optional caller policy over the raw child filter word. This is not a
    // claim that Havok filterInfo is equivalent to the game's contents mask.
    bool requireAnyRawFilterBits = false;
    std::uint32_t rawFilterMask = 0xffffffffu;
};
struct HavokTriangleRayResult {
    HavokTriangleRayStatus status = HavokTriangleRayStatus::Unavailable;
    double fraction = 1;
    CollisionVec3 position, normal;
    std::string reason;
};
// Isolated original triangle-leaf ray profile: two sided, source relative edge
// epsilon, strict maximum fraction and source from/to-plane endpoint rules.
// Radius, extrusion and welding are deliberately irrelevant to this ray body;
// they remain relevant to surface/sweep/contact selection above. Arithmetic is
// portable double, not a bit-exact emulator of the original x86 float pipeline.
HavokTriangleRayResult RayCastHavokTriangleSegment(
    const HavokTriangleQueryResult& triangle, const CollisionVec3& from,
    const CollisionVec3& to, const HavokTriangleRayOptions& options = HavokTriangleRayOptions());
} // namespace wolf2009
#endif

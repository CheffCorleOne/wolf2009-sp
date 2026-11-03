// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_HAVOK_PRIMITIVE_SUPPORT_H
#define WOLF2009_HAVOK_PRIMITIVE_SUPPORT_H
#include "StaticCollision.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
enum class HavokPrimitiveKind { Sphere = 1, Capsule = 2, Cylinder = 3 };
enum class HavokPrimitiveSupportStatus { Resolved, Unresolved };
enum class HavokCylinderGeometryProfile { Unresolved, RetailDefault16VirtualEnvelope };
struct HavokCylinderGeometryMetadata {
    HavokCylinderGeometryProfile profile = HavokCylinderGeometryProfile::Unresolved;
    std::uint32_t rawCylinderRadiusBits = 0, rawBaseRadiusFactorBits = 0;
    std::uint32_t quantizationKBits = 0, virtualVertexStepBits = 0;
    // Observed on the verified retail trainyard process. Not a tessellation
    // input, not an assertion about every thread's FPU control state.
    std::uint32_t observedRoundThresholdBits = 0;
    bool hasRuntimeThresholdObservation = false;
    // The retail direction quantizer has measured non-max-dot selections.
    bool matchesNativeSupportSelection = false;
};
struct HavokPrimitiveSelection {
    std::uint32_t shapeOffset;
    HavokPrimitiveKind kind;
    HavokPrimitiveSelection(std::uint32_t offset, HavokPrimitiveKind type)
        : shapeOffset(offset), kind(type) {}
};
struct HavokPrimitiveSupportLimits {
    std::size_t sourceBytes = 64u << 20, objects = 100000, fixups = 500000;
    std::size_t shapes = 100000, decodedBytes = 32u << 20;
    double coordinateGameUnits = 10000000, maximumRadiusGameUnits = 1000;
};
struct HavokPrimitiveSupport {
    HavokPrimitiveKind kind = HavokPrimitiveKind::Sphere;
    HavokPrimitiveSupportStatus status = HavokPrimitiveSupportStatus::Unresolved;
    std::uint32_t sourceShapeOffset = 0;
    std::uint64_t sourceRegion = 0;
    std::string sourceType, reason;
    // Exact serialized bytes, not a native class or vtable. Only the first
    // rawHeaderBytes bytes belong to the object: Sphere32, Capsule64, Cylinder96.
    std::array<unsigned char, 96> rawHeader{};
    std::size_t rawHeaderBytes = 0;
    std::uint32_t rawRadiusBits = 0;
    // Finite describes the raw word only. Known permits use of radiusGameUnits;
    // it is false for the unresolved Cylinder support profile.
    bool radiusFinite = false, radiusKnown = false;
    double radiusGameUnits = 0;
    // Resolved declared geometry = convex(corePoints) + sphere(radiusGameUnits).
    // Sphere has one origin; Capsule has the two XYZ endpoints in source order.
    // Endpoint W lanes stay raw, including nonfinite payloads. No deduplication.
    std::vector<CollisionVec3> corePoints;
    HavokCylinderGeometryMetadata cylinder;
    std::vector<std::uint16_t> virtualVertexIds;
};
struct HavokPrimitiveSupportDocument {
    std::uint64_t sourceFingerprint = 0; // FNV-1a over source bytes, not authentication
    std::size_t sourceBytes = 0, decodedBytes = 0;
    std::vector<HavokPrimitiveSupport> shapes;
    // Interpretation/cache identity is (source bytes, selected objects, this
    // profile). The byte fingerprint and sourceRegion remain source identities.
    HavokCylinderGeometryProfile cylinderProfile = HavokCylinderGeometryProfile::Unresolved;
};
struct HavokCylinderVirtualCore {
    HavokCylinderGeometryMetadata metadata;
    std::array<CollisionVec3,32> corePoints;
    std::array<std::uint16_t,32> vertexIds{};
    double radiusGameUnits = 0; // original convex radius16, not cylRadius20
};
// Shared pure generator/cache validator input. The explicit profile defines
// convex(all32 verified native virtual vertices)+source radius16, with raw
// perpendiculars and staged float32 arithmetic. It is neither a regular ring
// nor the original direction-selection/contact algorithm. Unresolved/unknown
// profiles and malformed/nonfinite/out-of-budget geometry throw. Factor24 and
// all W words stay raw and do not affect this geometry representation.
HavokCylinderVirtualCore BuildHavokCylinderVirtualCore(
    const std::array<unsigned char,96>& rawHeader,HavokCylinderGeometryProfile profile,
    const HavokPrimitiveSupportLimits& limits = HavokPrimitiveSupportLimits());

// Checked Havok5.6/32-bit LE leaf reader. Selection is explicit: an empty list
// reads no shapes; repeated offsets or a mismatched exact virtual-fixup type
// are errors. No shape graph, placement, contents, body binding or collision
// response is inferred. Malformed bounds/nonfinite resolved geometry throw.
// Source metres are converted to game units (one unit = .0254 metres).
// This default overload preserves Cylinder raw headers as Unresolved: no analytic
// cylinder or guessed ring approximation is substituted for its support code.
HavokPrimitiveSupportDocument DecodeHavokPrimitiveSupport(
    const std::vector<unsigned char> &hkxs,
    const std::vector<HavokPrimitiveSelection> &selection,
    const HavokPrimitiveSupportLimits &limits = HavokPrimitiveSupportLimits());
// Explicit opt-in overload. The preceding overload keeps Cylinder unresolved.
HavokPrimitiveSupportDocument DecodeHavokPrimitiveSupport(
    const std::vector<unsigned char>& hkxs,const std::vector<HavokPrimitiveSelection>& selection,
    HavokCylinderGeometryProfile cylinderProfile,
    const HavokPrimitiveSupportLimits& limits = HavokPrimitiveSupportLimits());
} // namespace wolf2009
#endif

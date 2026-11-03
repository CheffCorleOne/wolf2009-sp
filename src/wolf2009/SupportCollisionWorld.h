// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SUPPORT_COLLISION_WORLD_H
#define WOLF2009_SUPPORT_COLLISION_WORLD_H
#include "HavokTriangleQuery.h"
#include "HavokPrimitiveSupport.h"
#include "WorldTriangleGeometry.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wolf2009 {
enum class SupportCollisionKind { Box, ConvexVertices, Triangle, Sphere, Capsule, Cylinder };
struct SupportCollisionIdentity {
    std::uint32_t sourceShapeOffset = 0, sourceRecordOffset = 0;
    std::uint64_t sourceRegion = 0, occurrence = 0;
    std::size_t sourceSubpartIndex = static_cast<std::size_t>(-1);
    std::size_t triangleIndex = static_cast<std::size_t>(-1);
};
struct SupportCollisionBounds { CollisionVec3 minimum, maximum; };
enum class SupportCollisionContentsProvenance {
    SelectedBodyCompoundValue,
    TriangleOccurrenceValueUnmapped
};
struct SupportCollisionFilter {
    // Retain these two sources separately. A triangle child filter word is
    // NOT established to be the game's contents, even when both equal 3.
    std::uint32_t inheritedRawContents = 0, rawChildFilterInfo = 0;
    bool inheritedContentsKnown = false, childFilterKnown = false;
    SupportCollisionContentsProvenance contentsProvenance =
        SupportCollisionContentsProvenance::SelectedBodyCompoundValue;
    HavokTriangleQueryStatus childFilterStatus = HavokTriangleQueryStatus::Unavailable;
};
struct SupportCollisionTriangleMetadata {
    std::uint8_t rawIndexType = 0, rawFlipByte = 0, weldingType = 0;
    std::uint32_t rawTriangleOffset = 0, rawRadiusBits = 0;
    std::int64_t weldingIndex = 0;
    std::uint32_t weldingArrayCount = 0;
    std::uint16_t weldingInfo = 0;
    HavokTriangleQueryStatus weldingStatus = HavokTriangleQueryStatus::Unavailable;
    std::array<std::uint32_t,4> rawExtrusionBits{};
    // Only NONE(6) with a resolved empty/retained word is qualified here.
    // This describes source metadata, not a completed contact response solver.
    bool contactMetadataQualified = false;
};
struct SupportCollisionShape {
    SupportCollisionKind kind = SupportCollisionKind::Box;
    SupportCollisionIdentity source;
    // Exact source support core, LOCAL game units. All native hull lanes are
    // included; no plane intersection, radius subtraction or deduplication.
    // Repeated/collinear/coplanar/single-point cores are legal.
    std::vector<CollisionVec3> corePoints;
    std::size_t declaredCorePoints = 0;
    double radius = 0; // sphere radius, separate from the core
    CollisionRigidTransform placement;
    // Raw retained placement projection, radius included, outward-rounded.
    // A solver that canonicalizes near-orthogonal matrices MUST recompute its
    // own bounds using the same canonical pose; these do not enclose that
    // potentially different rotation merely because of outward rounding.
    SupportCollisionBounds worldBounds;
    SupportCollisionFilter filter;
    SupportCollisionTriangleMetadata triangle;
    // Present only for the explicitly selected cylinder virtual-envelope
    // profile. Its native direction-selection mismatch is preserved per shape.
    HavokCylinderGeometryMetadata cylinder;
};
enum class SupportCollisionOmissionResolution {
    Unresolved,
    ImportedTriangleSubpart,
    IndependentRayPlaneDiagnostic,
    IndependentVolumeDiagnostic,
    ImportedPrimitiveSupport
};
struct SupportCollisionOmission {
    WorldCollisionOmission source; // original placement, counts and raw value
    SupportCollisionOmissionResolution resolution = SupportCollisionOmissionResolution::Unresolved;
    std::size_t firstShape = static_cast<std::size_t>(-1), shapeCount = 0;
};
struct SupportCollisionWorld {
    std::uint64_t sourceFingerprint = 0;
    std::size_t sourceBytes = 0;
    std::string selectedBodyName;
    std::uint32_t sourceBodyOffset = 0, rootShapeOffset = 0;
    std::uint32_t rootRawFilterInfo = 0; // source word only; no filter/mask mapping
    bool rootRawFilterKnown = false;
    std::vector<SupportCollisionShape> shapes;
    std::vector<SupportCollisionOmission> sourceOmissions;
    std::map<std::string,std::size_t> sourceUnsupported, unresolved;
    std::size_t totalCorePoints = 0, expectedTriangles = 0, importedTriangles = 0;
    std::size_t otherSourceRigidBodies = 0;
    // These concern ONLY the explicitly selected source body. Legacy ray
    // plane/volume guards do not qualify the independent support point set.
    bool selectedGeometryComplete = true, selectedHierarchyComplete = true;
    // Raw inherited contents availability and raw TRIANGLE child-filter
    // availability; neither certifies a gameplay mask/filter interpretation.
    bool selectedContentsComplete = true, selectedChildFiltersComplete = true;
    bool selectedContactMetadataComplete = true;
    bool selectedCoverageComplete = false;
    // Selection is distinct from geometry coverage and runtime entity binding.
    bool allSourceRigidBodiesSelected = false;
    bool entityBindingsResolved = false, nativeRunnable = false; // always false
};
struct SupportCollisionWorldLimits {
    std::size_t shapes = 1000000, corePointsPerShape = 1024, totalCorePoints = 8000000;
    std::size_t sourceRecords = 300000;
    double coordinateGameUnits = 10000000, maximumRadiusGameUnits = 1000;
    HavokTriangleQueryLimits triangleQuery;
};
// Source-data import for future support queries/BVH. Inputs must describe the
// same bounded source bytes, selected body and exact occurrence placements.
// Duplicate/missing joins and malformed/nonfinite placements or bounds throw.
// Unknown arrays/hierarchies/shapes/materials remain explicit and incomplete.
// Nonzero triangle extrusion remains unresolved; radius and degenerate cores
// do NOT require flat-ray/native-surface qualification. Original ray planes
// are neither read nor repaired. No filter mapping, physics activation, MOPP
// bytecode traversal, entity binding or native-runnable world is inferred.
SupportCollisionWorld BuildSupportCollisionWorld(
    const WorldCollisionGeometry& world, const WorldTriangleGeometry& triangles,
    const SupportCollisionWorldLimits& limits = SupportCollisionWorldLimits());
// Explicit leaf-document phase. Every selected Sphere/Capsule/Cylinder omission
// must join its unique source object; unrelated/missing/contradictory records
// are errors. Resolved leaves require a known wrapper-radius chain agreeing
// with radius16. Radius values are never added together. Cylinder requires
// its explicit verified virtual-envelope profile; default stays unresolved.
// The preceding overload retains its original omission behavior.
SupportCollisionWorld BuildSupportCollisionWorld(
    const WorldCollisionGeometry& world, const WorldTriangleGeometry& triangles,
    const HavokPrimitiveSupportDocument& primitives,
    const SupportCollisionWorldLimits& limits = SupportCollisionWorldLimits());
} // namespace wolf2009
#endif

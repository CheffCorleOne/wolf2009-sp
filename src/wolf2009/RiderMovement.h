// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_RIDER_MOVEMENT_H
#define WOLF2009_RIDER_MOVEMENT_H
#include "ModelPrimitiveCollision.h"
namespace wolf2009 {
// A central, straight, translation-only opening-wagon slice. No strafe, jump,
// gravity/depenetration, rotating parent, door traversal or general level walk.
struct RiderMovementParameters {
    double forwardSpeed = 140, backwardSpeed = 126;
    // Calibrated first60Hz ticks: forward131.23, backward124.95 units/s.
    // Bounded linear approximation, not original Havok integration.
    double forwardAcceleration = 7873.8, backwardAcceleration = 7497;
    double halfWidth = 16, height = 72, maximumSupportGap = 2;
    double fixedSeconds = 1.0 / 60.0, coordinateLimit = 10000000;
    double maximumParentSpeed = 256, maximumParentDisplacement = 128;
};
struct RiderMovementQualification {
    std::string instanceId;
    std::uint32_t queryMask = CollisionPlayerMask;
    // Caller established the exact opening scene/entity/state and original
    // central W/release/S+carry probes. Never inferred from raw asset flags.
    bool centralOpeningCaseVerified = false;
};
struct RiderMovementProfile {
    RiderMovementParameters parameters;
    std::string instanceId, selectedBodyName;
    CollisionVec3 initialLocalFeet;
    double minimumY = 0, maximumY = 0, supportTop = 0, supportGap = 0;
    std::uint32_t floorShape = 0, backShape = 0, frontShape = 0;
    std::size_t backPlane = 0, frontPlane = 0;
    std::uint64_t sourceToken = 0, geometryToken = 0;
    bool ready = false;
};
enum class RiderMovementStatus {
    Ready, Initialized, Moved, Resting, EndContact,
    Unqualified, InvalidParameters, InvalidInput, InvalidState, InvalidTimeStep,
    UnknownGeometry, UnsupportedCorridorOverlap, UnsupportedSupport,
    UnsupportedEndFaces, UnsupportedReversal, WrongParent, StaleParent,
    ExcessiveParentMotion, UnsupportedParentMotion
};
struct RiderMovementSetup {
    RiderMovementStatus status = RiderMovementStatus::Unqualified;
    RiderMovementProfile profile;
    const char *reason = "Unqualified central corridor";
    bool Succeeded() const { return status == RiderMovementStatus::Ready; }
};
// Floor and both end limits come from checked HKXS source geometry. Other
// primitive/omission bounds must be disjoint from the swept standing corridor.
// A broadphase overlap is UNKNOWN and prevents setup, never a fabricated wall.
RiderMovementSetup BuildRiderMovementProfile(const ModelPrimitiveCollision &model,
    CollisionVec3 genuineInitialLocalFeet, const RiderMovementQualification &qualification,
    const RiderMovementParameters &parameters = RiderMovementParameters());
struct RiderMovementParent {
    std::string instanceId;
    CollisionVec3 translation;
    std::uint64_t sample = 0;
};
enum class RiderMovementInput { Backward = -1, Released = 0, Forward = 1 };
struct RiderMovementState {
    CollisionVec3 localFeet, localOffset, worldFeet, parentTranslation;
    double relativeSpeed = 0;
    std::uint64_t sourceToken = 0, parentSample = 0;
    bool initialized = false;
};
struct RiderMovementResult {
    RiderMovementStatus status = RiderMovementStatus::InvalidState;
    RiderMovementState state;
    CollisionVec3 worldVelocity;
    std::uint32_t contactShape = 0;
    std::size_t contactPlane = 0;
    const char *reason = "Invalid rider state";
    bool Succeeded() const {
        return status == RiderMovementStatus::Initialized || status == RiderMovementStatus::Moved ||
               status == RiderMovementStatus::Resting || status == RiderMovementStatus::EndContact;
    }
};
RiderMovementResult InitializeRiderMovement(const RiderMovementProfile &profile,
                                           const RiderMovementParent &parent);
// Pure transactional fixed-step update: failure returns unchanged state.
// Offset is in cart-local coordinates; worldFeet includes parent carry once.
// Release before reversing: direct nonzero-speed reversal is not yet qualified.
RiderMovementResult StepRiderMovement(const RiderMovementProfile &profile,
    const RiderMovementState &state, RiderMovementInput input,
    const RiderMovementParent &nextParent, double elapsedSeconds);
int RunRiderMovementTests();
} // namespace wolf2009
#endif

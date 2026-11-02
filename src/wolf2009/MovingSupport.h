// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MOVING_SUPPORT_H
#define WOLF2009_MOVING_SUPPORT_H
#include "StaticCollision.h"
#include <cstdint>
namespace wolf2009 {
// Generic, translation-only platform kinematics. This module does not decode
// HKXS, choose model bodies, resolve entity flags, animate doors, or walk.
struct MovingSupportBox {
    StaticCollisionBox box;
    // This exact query mask was established to block this qualified box.
    // It does NOT replace unknown raw contents with guessed content bits.
    std::uint32_t verifiedQueryMask = 0;
    bool topFaceVerified = false;
};
struct MovingSupportGeometry {
    std::string instanceId;
    std::vector<MovingSupportBox> localBoxes;
    bool geometryComplete = false;
};
struct MovingSupportPose {
    CollisionVec3 translation;
    std::uint64_t sample = 0;
};
struct MovingSupportWorld {
    std::string instanceId;
    CollisionVec3 translation;
    std::uint64_t sample = 0;
    std::vector<MovingSupportBox> boxes;
    bool geometryComplete = false;
};
struct MovingSupportLimits {
    std::size_t boxes = 20000;
    double coordinateGameUnits = 10000000;
    double maximumContactGap = 4;
    double maximumMotion = 128;
    double maximumSpeed = 256;
    double maximumElapsedSeconds = 2.1;
};
// Returns owned world boxes; raw contents and their known flags are preserved.
// Invalid/nonfinite input, oversized arrays or bounds throw std::runtime_error.
MovingSupportWorld PlaceMovingSupport(const MovingSupportGeometry &geometry, const MovingSupportPose &pose,
                                      const MovingSupportLimits &limits = MovingSupportLimits());
struct MovingSupportWitness {
    bool contactEstablished = false;
    // A partial shape set requires an explicit qualification of THIS contact.
    // It never establishes that the surrounding scene is safe to traverse.
    bool qualifiedPartialContact = false;
    std::size_t boxIndex = static_cast<std::size_t>(-1);
    std::uint32_t queryMask = CollisionPlayerMask;
    double maximumGap = .01;
};
struct MovingSupportContact {
    bool valid = false, qualifiedPartialContact = false;
    std::string instanceId;
    std::uint64_t sample = 0;
    std::size_t boxIndex = static_cast<std::size_t>(-1);
    std::uint32_t sourceShapeOffset = 0, queryMask = 0;
    CollisionVec3 localFeet;
    double halfWidth = 0, height = 0, gap = 0, maximumGap = 0;
};
enum class MovingSupportStatus {
    ContactEstablished,
    CandidateOnly,
    NoContactWitness,
    PartialGeometry,
    WrongSupport,
    StaleSample,
    ChangedGeometry,
    UnverifiedSupportFace,
    UnverifiedMask,
    ContactLost,
    ExcessiveMotion,
    InvalidInput
};
struct MovingSupportResult {
    MovingSupportStatus status = MovingSupportStatus::InvalidInput;
    MovingSupportContact contact;
    CollisionVec3 candidateFeet, platformDelta;
    bool hasCandidate = false;
    // ALWAYS false: applying the candidate requires a separate collision/path
    // check. In particular a partial scene's miss cannot certify a clear path.
    bool clearanceVerified = false;
    bool geometryComplete = false;
};
// Checks an explicitly established top-face contact without snapping feet.
// Positive horizontal overlap and a bounded, nonnegative top gap are required.
MovingSupportResult EstablishMovingSupport(const MovingSupportWorld &platform, CollisionVec3 feet,
                                           double halfWidth, double height,
                                           const MovingSupportWitness &witness,
                                           const MovingSupportLimits &limits = MovingSupportLimits());
// Produces only a carry candidate. Requires the same instance and exact box
// layout, newer sample, a still-established contact and support under the rider.
// No input state is changed. Wrong/stepped-off/stale/excessive cases yield no
// candidate. Rotation, collision response, riding velocity and jumping are not
// inferred. Caller must explicitly end a contact when jumping/leaving support.
MovingSupportResult TransportMovingSupport(const MovingSupportWorld &before, const MovingSupportWorld &after,
                                           const MovingSupportContact &contact, CollisionVec3 currentFeet,
                                           bool contactStillEstablished, double elapsedSeconds,
                                           const MovingSupportLimits &limits = MovingSupportLimits());
int RunMovingSupportTests();
} // namespace wolf2009
#endif

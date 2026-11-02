// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_PLAYER_MOVE_H
#define WOLF2009_PLAYER_MOVE_H
#include "StaticCollision.h"
#include <cstddef>
namespace wolf2009 {
// Independent prototype controller, not recovered retail character physics.
// Walk speed 100 and standing height 72 were observed in the original runtime.
// All other defaults are prototype choices. halfWidth 16 is an AABB proxy,
// not the player.def capsule radius 13.75. No steps or jumps are implemented.
struct PlayerMoveParameters {
    double halfWidth = 16;
    double height = 72;
    double speed = 100;
    double gravity = 800;
    double terminalFallSpeed = 1000;
    double fixedSeconds = 1.0/60.0;
    std::size_t slideIterations = 4;
};
struct PlayerMoveState {
    CollisionVec3 feet,velocity;
    bool grounded = false;
    bool initialized = false;
};
enum class PlayerMoveStatus {
    Initialized, Advanced, PartialGeometry, UnverifiedBodyPose,
    UnselectedBodies, NoGeometry, InvalidParameters, InvalidState,
    InvalidInput, MalformedGeometry, OverlappingGeometry
};
struct PlayerMoveResult {
    PlayerMoveStatus status = PlayerMoveStatus::InvalidState;
    PlayerMoveState state;
    std::size_t contacts = 0;
    bool movementClipped = false; // Remaining displacement dropped at iteration budget.
    bool Succeeded() const {
        return status == PlayerMoveStatus::Initialized || status == PlayerMoveStatus::Advanced;
    }
};
// Fail closed unless the selected geometry is complete, its body pose verified,
// no other rigid bodies omitted, and at least one box present. Original player
// collision masks and dynamic gameplay are not reconstructed by this module.
// Overlap is refused; authored feet are never raised, lowered, or depenetrated.
PlayerMoveResult InitializePlayerMove(const StaticCollisionScene& scene,
                                     CollisionVec3 authoredFeet,
                                     const PlayerMoveParameters& parameters = PlayerMoveParameters());
// One fixed step. worldXYInput.z must be zero. XY magnitude is clamped to one;
// direction is in world space, not camera space. Horizontal velocity responds
// immediately. Gravity, continuous box sweeps and wall sliding are applied.
// The input state is not mutated; on failure the returned state is unchanged.
PlayerMoveResult StepPlayerMove(const StaticCollisionScene& scene,
                               const PlayerMoveState& state,
                               CollisionVec3 worldXYInput,
                               const PlayerMoveParameters& parameters = PlayerMoveParameters());
const char* PlayerMoveStatusText(PlayerMoveStatus status);
int RunPlayerMoveTests();
}
#endif

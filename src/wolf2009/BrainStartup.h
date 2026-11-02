// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_BRAIN_STARTUP_H
#define WOLF2009_BRAIN_STARTUP_H
#include "LinearMover.h"
#include <cstdint>
namespace wolf2009 {
struct BrainStartupPlan {
    SelfLinearMove move;
    std::uint32_t onSpawnNodeId = 0, moveNodeId = 0;
    std::size_t unexecutedNodeCount = 0, moveOutputRecords = 0;
};
// Exact observed direct OnSpawn(type1)->MoveToEnt(type132) startup profile.
// The destination-ID field was verified by a one-byte causal A/B in the original
// runtime, not inferred from node order. Rejects delays/intermediate actions,
// branches, unknown startup flags/edge fields and ambiguous or missing nodes.
// This is not a general brain interpreter. Later nodes remain unexecuted.
BrainStartupPlan DecodeDirectBrainStartupH1023(const std::vector<unsigned char> &data,
                                               const LinearMoverLimits &limits = LinearMoverLimits());
// One entity's startup component. The level owner delivers its actual OnSpawn
// event after establishing world transforms. Construction and Step never create
// a spawn event; repeated event deliveries have no further effect.
class BrainStartupRuntime {
  public:
    BrainStartupRuntime(const BrainStartupPlan &plan, const EntityDocument &entities,
                        LinearMoverVec3 currentWorldOrigin);
    bool OnSpawn(); // true only for the first delivered event
    LinearMoverStep Step(double seconds) {
        return mover_.Step(seconds);
    }
    LinearMoverVec3 Position() const {
        return mover_.Position();
    }
    bool SpawnDelivered() const {
        return spawnDelivered_;
    }
    bool Active() const {
        return mover_.Active();
    }
    bool Arrived() const {
        return mover_.Arrived();
    }

  private:
    LinearMover mover_;
    bool spawnDelivered_ = false;
};
int RunBrainStartupTests();
} // namespace wolf2009
#endif

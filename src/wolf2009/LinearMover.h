// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_LINEAR_MOVER_H
#define WOLF2009_LINEAR_MOVER_H
#include "Entities.h"
#include <cstddef>
#include <string>
#include <vector>
namespace wolf2009 {
struct LinearMoverLimits {
    std::size_t sourceBytes = 4u * 1024u * 1024u, records = 20000, tokens = 500000, tokenBytes = 1024,
                nesting = 64;
};
struct SelfLinearMove {
    std::string destinationName;
    double speed = 0;
    std::size_t sourceRecord = 0;
    // Only a configuration extraction. No activation/topology/condition semantics.
};
// Observed h1023/32/32/32/0 header, top-level MoveToEnt/type132 profile only.
// Requires exactly one
// self mover, explicit destination, constant positive speed, zero time/accel/
// decel/max speed/max time, and no custom sounds. Other records are bounded
// opaque data, never executed. Unsupported or ambiguous configurations throw.
SelfLinearMove DecodeSelfLinearMoveH1023(const std::vector<unsigned char> &data,
                                         const LinearMoverLimits &limits = LinearMoverLimits());
struct LinearMoverVec3 {
    double x, y, z;
    LinearMoverVec3(double a = 0, double b = 0, double c = 0) : x(a), y(b), z(c) {}
};
struct LinearMoveDefinition {
    LinearMoverVec3 start, destination;
    double speed = 0;
    std::string destinationName;
};
// currentWorldOrigin must come from the caller's established entity transform;
// a bound entity's ENTS origin is NOT assumed to be world space. Destination
// must be a unique exact ENTS name with an explicit unbound origin.
LinearMoveDefinition ResolveSelfLinearMove(const SelfLinearMove &configuration,
                                           const EntityDocument &entities,
                                           LinearMoverVec3 currentWorldOrigin);
struct LinearMoverStep {
    LinearMoverVec3 position, delta;
    bool active = false, arrived = false;
};
class LinearMover {
  public:
    explicit LinearMover(const LinearMoveDefinition &definition);
    // Activation is always explicit; this does not interpret brain links.
    void Activate();
    LinearMoverStep Step(double seconds); // finite 0..1 seconds; no lost-time catchup
    LinearMoverVec3 Position() const {
        return position_;
    }
    bool Active() const {
        return active_;
    }
    bool Arrived() const {
        return arrived_;
    }

  private:
    LinearMoveDefinition definition_;
    LinearMoverVec3 position_;
    double length_ = 0, travelled_ = 0;
    bool active_ = false, arrived_ = false;
};
// Apply only after the caller has independently established the rider relation.
// Translation only: no contact detection, rotation, collision, or binding logic.
LinearMoverVec3 TranslateConfirmedRider(LinearMoverVec3 rider, const LinearMoverStep &step);
int RunLinearMoverTests();
} // namespace wolf2009
#endif

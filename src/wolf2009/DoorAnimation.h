// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_DOOR_ANIMATION_H
#define WOLF2009_DOOR_ANIMATION_H
#include <vector>
#include <array>

namespace wolf2009 {
// Global/model-space transform of the rigid door joint. Quaternion x,y,z,w.
struct DoorJointPose {
    std::array<double, 3> translation{{0, 0, 0}};
    std::array<double, 4> rotation{{0, 0, 0, 1}};
};
// Model-space curve and rigid-joint poses of the observed two-bone train door.
// Actor placement, phase transitions and activation belong to level logic.
class DoorAnimation {
  public:
    static DoorAnimation Load(const std::vector<unsigned char> &hkxa,
                              const std::vector<unsigned char> &skel);
    double Duration() const { return duration_; }
    double ClosedX() const { return values_.at(0); }
    double OpenX() const { return values_.at(values_.size() - 1); }
    double LocalX(double seconds) const;
    double Slide(double seconds) const { return LocalX(seconds) - ClosedX(); }
    const DoorJointPose &ReferenceBindPose() const { return reference_; }
    const DoorJointPose &ClosedPose() const { return closed_; }
    const DoorJointPose &EndIdlePose() const { return end_; }
    // Active movement clamped to its endpoints; end_idle is a separate phase.
    DoorJointPose SamplePose(double seconds) const;

  private:
    DoorAnimation() = default;
    double duration_ = 0, frameDuration_ = 0;
    std::vector<unsigned char> knots_;
    std::vector<double> values_;
    DoorJointPose reference_, closed_, moving_, end_;
};
} // namespace wolf2009
#endif

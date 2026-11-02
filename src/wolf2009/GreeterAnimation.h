// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_GREETER_ANIMATION_H
#define WOLF2009_GREETER_ANIMATION_H
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace wolf2009 {

struct GreeterAnimationLimits {
    std::size_t sourceBytes = 64u * 1024u * 1024u, objects = 100000, fixups = 1000000;
    std::size_t tableEntries = 4096, curveControls = 256, totalControls = 20000;
    std::size_t compressedClipBytes = 1024u * 1024u;
};

struct GreeterJointPose {
    std::array<double, 3> translation{{0, 0, 0}};
    std::array<double, 4> rotation{{0, 0, 0, 1}}; // x,y,z,w, unit quaternion
};

struct GreeterJoint {
    std::string name;
    int parent = -1;
    GreeterJointPose referenceLocal, referenceModel;
};

struct GreeterPose {
    double seconds = 0;
    std::vector<GreeterJointPose> local, model;
};

// The named 52-joint humanoid clip in map HKXA, paired with humanoid_male SKEL
// from global.spk. One 182-frame/24Hz spline block: 16-bit vector components,
// linear/cubic curves, 40-bit smallest-three rotations, identity animated scale.
// Component-wise B-spline quaternion interpolation followed by normalization,
// not an assumed SLERP. No retargeting, root-motion extraction or world/entity
// placement is implied. Auxiliary/face data remains bounded opaque data.
class GreeterAnimation {
  public:
    static GreeterAnimation Load(const std::vector<unsigned char> &mapHkxa,
                                 const std::vector<unsigned char> &globalSkel,
                                 const GreeterAnimationLimits &limits = GreeterAnimationLimits());
    const std::string &ResourceName() const;
    const std::string &SkeletonName() const;
    const std::vector<GreeterJoint> &Joints() const;
    double Duration() const;
    double FrameDuration() const;
    std::size_t FrameCount() const;
    std::size_t OpaqueAuxiliaryBytes() const;
    // Finite time, clamped to endpoints. A returned pose owns its joint values.
    GreeterPose SamplePose(double seconds) const;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit GreeterAnimation(std::shared_ptr<const Impl> impl) : impl_(impl) {}
};

enum class GreeterPlaybackStatus { NotStarted, Playing, Finished };
struct GreeterPlaybackPose {
    GreeterPlaybackStatus status = GreeterPlaybackStatus::NotStarted;
    double committedStartTime = 0, elapsed = 0;
    GreeterPose pose;
};

// Owns a shared immutable loaded clip. Start is called at an actual committed
// actor start boundary, never inferred from wagon timing. A second Start fails
// even after finish; use a fresh instance for a new explicit start. This clock
// view does not dispatch BRAI outputs or claim animation-completion semantics.
class GreeterAnimationPlayback {
  public:
    explicit GreeterAnimationPlayback(const GreeterAnimation &animation) : animation_(animation) {}
    bool Start(double committedStartTime);
    GreeterPlaybackPose SampleAt(double clock) const;
    bool Started() const { return started_; }

  private:
    GreeterAnimation animation_;
    bool started_ = false;
    double start_ = 0;
};

int RunGreeterAnimationTests();
} // namespace wolf2009
#endif

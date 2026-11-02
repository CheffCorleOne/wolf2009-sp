// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_DOOR_MODEL_H
#define WOLF2009_DOOR_MODEL_H
#include "DoorAnimation.h"
#include "Geometry.h"
#include <array>
namespace wolf2009 {
struct RigidDoorGeometry {
    GeometryScene bindGeometry;
    DoorJointPose bindJoint;
    std::string jointName;
};
// Narrow MD5R1 profile: two named joints, one render buffer, one rigid palette
// entry, inline float3/blend-u32/normal/tangent/binormal/RGBA/float2 vertices.
// No multiweight skinning, external buffers, collision or animation is inferred.
RigidDoorGeometry DecodeRigidDoorModel1(const std::vector<unsigned char> &model,
                                        const std::string &resourceName,
                                        const GeometryLimits &limits = GeometryLimits());
// Applies animatedGlobal * inverse(bindGlobal), preserving UV/indices/materials.
// The original vertices are already in model bind space, not joint-local space.
GeometryScene PoseRigidDoor(const RigidDoorGeometry &model, const DoorJointPose &pose);
// Column-major affine matrix for bind-space vertices, directly usable as an
// OpenGL model uniform before the entity's authored world transform.
std::array<float, 16> RigidDoorSkinMatrix(const DoorJointPose &bind, const DoorJointPose &pose);
// Verified two-bone train-door HKXA/SKEL profile. Closed pose comes from idle,
// movement pose from door_move, and final pose from end_idle; bind != idle.
class DoorModel {
  public:
    static DoorModel Load(const std::vector<unsigned char> &model, const std::string &resourceName,
                          const DoorAnimation &animation,
                          const GeometryLimits &limits = GeometryLimits());
    const RigidDoorGeometry &Source() const { return source_; }
    const GeometryScene &ClosedGeometry() const { return closedGeometry_; }
    const DoorJointPose &ClosedPose() const { return animation_.ClosedPose(); }
    const DoorJointPose &EndIdlePose() const { return animation_.EndIdlePose(); }
    // Active door_move samples, clamped at its endpoints. Finishing the clip
    // does not imply a transition to end_idle; the caller owns that event.
    DoorJointPose SamplePose(double seconds) const;
    GeometryScene SampleGeometry(double seconds) const;
    std::array<float, 16> SampleMatrix(double seconds) const {
        return RigidDoorSkinMatrix(source_.bindJoint, SamplePose(seconds));
    }
    std::array<float, 16> ClosedMatrix() const {
        return RigidDoorSkinMatrix(source_.bindJoint, ClosedPose());
    }
    double Duration() const { return animation_.Duration(); }

  private:
    DoorModel(RigidDoorGeometry source, DoorAnimation animation);
    RigidDoorGeometry source_;
    DoorAnimation animation_;
    GeometryScene closedGeometry_;
};
int RunDoorModelTests();
} // namespace wolf2009
#endif

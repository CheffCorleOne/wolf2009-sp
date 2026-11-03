// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_GREETER_MODEL_H
#define WOLF2009_GREETER_MODEL_H
#include "Geometry.h"
#include "GreeterAnimation.h"
#include <array>

namespace wolf2009 {

struct GreeterInfluence {
    std::array<std::uint16_t, 4> joints{{0, 0, 0, 0}};
    std::array<double, 4> weights{{1, 0, 0, 0}};
    bool mirroredTangent = false;
};

struct GreeterSkinGeometry {
    GeometryScene bindGeometry;
    std::vector<GreeterJointPose> bindModelJoints;
    std::vector<std::string> jointNames;
    std::vector<GreeterInfluence> influences;
    std::size_t levelOfDetail = 0;
};
typedef GreeterSkinGeometry GreeterBodyGeometry;

// Narrow 52-joint body MD5R1 profile, not a head/face/attachment decoder.
// Select an explicit authored LOD; missing LOD never falls back to another pack
// or model. Inline 64-byte float3/palette-u8x4/weight-float3/normal/tangent/RGBA/UV
// render vertices, absolute 16-bit indices and per-batch Transform palettes only.
// Retail vertex-shader evidence establishes abs(storedWeight0), weight1/2,
// implicit weight3, and the first weight's independent tangent-handedness sign.
GreeterBodyGeometry DecodeGreeterBody1(const std::vector<unsigned char> &data,
                                       const std::string &resourceName,
                                       const std::vector<GreeterJoint> &animationSkeleton,
                                       std::size_t levelOfDetail,
                                       const GeometryLimits &limits = GeometryLimits());

// Linear blend skinning: sum(weight * animatedModel * inverse(bindModel) * v).
// Input positions are already in model bind space. Returns model-space render
// positions/UV/materials; no world placement, skin remap, normals/lighting, head,
// head-joint copying, facial animation, accessories or collision are implied.
GeometryScene PoseGreeterBody(const GreeterBodyGeometry &body, const GreeterPose &pose);
// Shared CPU operation for the decoded 52-joint body or 27-joint head source.
// Pose joint order must match that source's named bind skeleton.
GeometryScene PoseGreeterSkinGeometry(const GreeterSkinGeometry &source, const GreeterPose &pose);

class GreeterModel {
  public:
    static GreeterModel Load(const std::vector<unsigned char> &model, const std::string &resourceName,
                             const GreeterAnimation &animation, std::size_t levelOfDetail = 0,
                             const GeometryLimits &limits = GeometryLimits());
    const GreeterBodyGeometry &Source() const { return source_; }
    GeometryScene SampleGeometry(double seconds) const;

  private:
    GreeterModel(GreeterBodyGeometry source, GreeterAnimation animation);
    GreeterBodyGeometry source_;
    GreeterAnimation animation_;
};

struct GreeterHeadAttachment {
    GreeterSkinGeometry source;
    std::vector<GreeterJoint> skeleton;
    std::vector<GreeterJointPose> idleLocal;
    // Actor joint indices resolved by name: chest, shoulders, neck, head.
    std::array<std::size_t, 4> actorJoints{{0, 0, 0, 0}};
    bool chestPlacementVerified = true;
    // Original VM data shows up to 0.24 units/2.84 degrees between some copied
    // joints during motion. Current-pose copying is an explicit prototype,
    // not a reproduction of original update ordering/procedural head blending.
    bool originalCopyTimingVerified = false;
    bool facialAnimationSupported = false;
};

// Separate actual 27-bone head skeleton and static 2-frame idle from map SKEL/
// HKXA. Decode the head's weighted and rigid render buffers, exact LOD and
// original model bind transforms. No substitute head, generated pose or 52-bone
// retarget is used. The caller supplies its verified actor skeleton.
GreeterHeadAttachment
LoadGreeterHead1(const std::vector<unsigned char> &model, const std::string &resourceName,
                 const std::vector<unsigned char> &mapHkxa, const std::vector<unsigned char> &mapSkel,
                 const std::vector<GreeterJoint> &actorSkeleton, std::size_t levelOfDetail = 0,
                 const GeometryLimits &limits = GeometryLimits());

// Head root follows actor chest. Named shoulders/neck/head use the current
// actor model transforms; other joints retain the decoded head idle local pose.
// Both local/model arrays refer to the actor's model space, so the actor world
// placement is applied once to body and head. See the explicit timing limitation.
GreeterPose PoseGreeterHeadCurrentActor(const GreeterHeadAttachment &head, const GreeterPose &actor);
GeometryScene PoseGreeterHeadGeometryCurrentActor(const GreeterHeadAttachment &head,
                                                  const GreeterPose &actor);

int RunGreeterModelTests();
} // namespace wolf2009
#endif

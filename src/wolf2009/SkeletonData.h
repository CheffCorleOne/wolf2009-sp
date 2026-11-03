// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SKELETON_DATA_H
#define WOLF2009_SKELETON_DATA_H
#include "GreeterAnimation.h" // Existing neutral joint/pose value types only.
#include <cstdint>
namespace wolf2009 {
struct SkeletonDataLimits {
    std::size_t sourceBytes=64u*1024u*1024u, objects=100000, fixups=1000000;
    std::size_t skeletons=4096, joints=256, nameBytes=65536;
};
struct SkeletonJointData {
    std::string name;
    int parent=-1;
    std::array<std::uint32_t,12> rawReference{{}}; // Full hkQsTransform, including unused lanes.
    std::uint32_t rawBoneWord=0;
    GreeterJointPose referenceLocal,referenceModel;
    bool referencePoseQualified=false;
};
struct SkeletonData {
    std::string name;
    std::uint32_t sourceObjectOffset=0;
    std::vector<SkeletonJointData> joints;
    bool referencePoseQualified=false;
};
// Exact object name in the named Animation Container, never a first/count/name
// heuristic. Supported pack layout is validated by the canonical HavokPack.
// Topology is a bounded single ordered tree. Raw reference words are retained
// even when scale/nonfinite pose data cannot be represented as a rigid pose.
SkeletonData ReadSkeletonData(const std::vector<unsigned char>& bytes,const std::string& name,
                              const SkeletonDataLimits& limits=SkeletonDataLimits());
}
#endif

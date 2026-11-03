// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SKINNED_MODEL_DATA_H
#define WOLF2009_SKINNED_MODEL_DATA_H
#include "RenderData.h"
#include "GreeterModel.h"
#include "SkeletonData.h"
#include <array>
#include <cstdint>
#include <map>
namespace wolf2009 {
namespace idtech4a {
// An explicit request, not skeleton inference or a model-def/clip transition.
// Every field is part of the native model-manager cache identity.
struct SkinnedModelRequest {
    std::string model, animation, skeleton;
    std::size_t levelOfDetail = 0;
    // MODL/HKXA/SKEL mount epochs. Zero means no runtime lease context (portable
    // fixtures); the native binding requires all three to be known/nonzero.
    std::array<std::uint64_t, 3> sourceEpochs{{0, 0, 0}};
};
SkinnedModelRequest MakeSkinnedModelRequest(const std::string& model,
                                           const std::string& animation,
                                           const std::string& skeleton,
                                           std::size_t levelOfDetail);
bool SameSkinnedModelRequest(const SkinnedModelRequest& a, const SkinnedModelRequest& b);
// Local cache key only. Hash collisions are checked against the complete request.
std::string SkinnedModelCacheName(const SkinnedModelRequest& request);
class SkinnedModelRequests {
  public:
    std::string Register(const SkinnedModelRequest& request);
    bool Find(const std::string& nativeName, SkinnedModelRequest& request) const;
    void Clear() { requests_.clear(); }
  private:
    std::map<std::string, SkinnedModelRequest> requests_;
};
struct SkinnedSourceIdentity {
    std::string path;
    std::size_t bytes = 0;
    std::uint64_t fingerprint = 0; // FNV-1a byte identity, not authentication.
};
struct SkinnedJoint {
    std::string name;
    int parent = -1;
    GreeterJointPose bindModel, bindLocal;
};
struct SkinnedWeight {
    std::uint16_t joint = 0;
    float weight = 0;
    std::array<float, 3> jointLocalPosition{{0, 0, 0}};
};
struct SkinnedSurface {
    RenderSurface geometry;
    std::vector<std::array<SkinnedWeight, 4>> weights;
    std::vector<std::uint32_t> sourceVertices;
    // Retained source fact; native diffuse rendering derives tangents from UVs.
    std::vector<unsigned char> mirroredTangents;
};
struct SkinnedModelLimits {
    std::size_t joints = 256, surfaces = 4096, vertices = 1000000, indices = 6000000;
    std::size_t sourceBytes = 64u * 1024u * 1024u;
};
struct PreparedSkinnedGeometry {
    std::string modelPath;
    std::size_t levelOfDetail=0;
    std::size_t otherLodMeshes = 0; // Decoder excluded these authored LODs.
    SkinnedLodPolicy lodPolicy=SkinnedLodPolicy::Authored;
    bool sourceHasLodRecords=true;
    bool selectionUsesCompatibilitySlot=false;
    std::vector<double> sourceLodDistances;
    std::vector<SkinnedMeshLodFact> sourceMeshLods;
    std::vector<SkinnedJoint> joints;
    std::vector<SkinnedSurface> surfaces;
};
struct SkinnedModelData : PreparedSkinnedGeometry {
    SkinnedModelRequest request;
    SkinnedSourceIdentity modelSource, animationSource, skeletonSource;
};
struct SkinnedModelSourceInfo {
    SkinnedModelRequest request;
    SkinnedSourceIdentity model, animation, skeleton;
};
// Additive v2: no animation source, placeholder identity or implicit skeleton.
struct SkinnedMeshRequestV2 {
    std::string model,skeletonContainer,skeletonName;
    std::size_t levelOfDetail=0;
    SkinnedVertexProfile geometryProfile=SkinnedVertexProfile::Weighted64;
    SkinnedLodPolicy lodPolicy=SkinnedLodPolicy::Authored;
    std::array<std::uint64_t,2> sourceEpochs{{0,0}}; // MODL, SKEL.
};
SkinnedMeshRequestV2 MakeSkinnedMeshRequestV2(const std::string& model,const std::string& container,
    const std::string& skeletonName,std::size_t lod,SkinnedVertexProfile profile);
SkinnedMeshRequestV2 MakeSkinnedMeshRequestV2(const std::string& model,const std::string& container,
    const std::string& skeletonName,std::size_t lod,SkinnedVertexProfile profile,SkinnedLodPolicy lodPolicy);
bool SameSkinnedMeshRequestV2(const SkinnedMeshRequestV2& a,const SkinnedMeshRequestV2& b);
std::string SkinnedMeshCacheNameV2(const SkinnedMeshRequestV2& request);
class SkinnedMeshRequestsV2 {
    std::map<std::string,SkinnedMeshRequestV2> requests_;
  public:
    std::string Register(const SkinnedMeshRequestV2& request);
    bool Find(const std::string& alias,SkinnedMeshRequestV2& out) const;
    void Clear(){requests_.clear();}
};
struct SkinnedMeshSourceInfoV2 {
    SkinnedMeshRequestV2 request;
    SkinnedSourceIdentity model,skeleton;
    bool sourceHasLodRecords=true,selectionUsesCompatibilitySlot=false;
    std::vector<double> sourceLodDistances;
    std::vector<SkinnedMeshLodFact> sourceMeshLods;
};
struct SkinnedMeshDataV2 : PreparedSkinnedGeometry {
    SkinnedMeshRequestV2 request;
    SkinnedSourceIdentity modelSource,skeletonSource;
    SkeletonData skeleton;
};
SkinnedMeshDataV2 LoadSkinnedMeshV2(const SkinnedMeshRequestV2& request,const ResourceReader& read,
    const SkinnedModelLimits& limits=SkinnedModelLimits());
// Neutral conversion: per-draw compaction and joint-local MD5 weight offsets.
// Does not bake poses, apply world placement, remap skins, or invent materials.
SkinnedModelData PrepareSkinnedModel(const SkinnedModelRequest& request,
                                     const GreeterSkinGeometry& source,
                                     const std::vector<GreeterJoint>& skeleton,
                                     const SkinnedModelLimits& limits = SkinnedModelLimits());
// Initial qualified source profile: existing 52-joint body + named HKXA/SKEL.
// It deliberately reuses their validators; arbitrary HKXA/heads are unsupported.
SkinnedModelData LoadSkinnedModel(const SkinnedModelRequest& request,
                                  const ResourceReader& read,
                                  const SkinnedModelLimits& limits = SkinnedModelLimits());
// Independent supplied model-space joints, useful before the animation-manager
// hook exists. Result order matches compact surfaces, not the original buffer.
std::vector<std::array<float, 3>> SkinPreparedSurface(const SkinnedModelData& model,
                                                    std::size_t surface,
                                                    const std::vector<GreeterJointPose>& joints);
std::vector<std::array<float,3>> SkinPreparedSurface(const PreparedSkinnedGeometry& model,std::size_t surface,
    const std::vector<GreeterJointPose>& joints);
}
}
#endif

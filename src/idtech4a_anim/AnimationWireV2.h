// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ANIMATION_WIRE_V2_H
#define WOLF2009_ANIMATION_WIRE_V2_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace wolf2009 { namespace animation2 {
typedef std::vector<unsigned char> Bytes;
enum class Failure { InvalidData, Unsupported, LimitExceeded };
class Error : public std::runtime_error {
    Failure failure_;
  public:
    Error(Failure failure,const char* message):std::runtime_error(message),failure_(failure){}
    Failure Category() const { return failure_; }
};
enum class VisualPolicy : std::uint32_t { OrdinaryVisualNoExtraction=1 };
enum class RigPolicy : std::uint32_t { CallerIdentityFullLocalPose=1 };
enum class GeometryProfile : std::uint32_t { Weighted64=1, WeightedOrRigid64=2 };
enum class LodPolicy : std::uint32_t { Authored=1, SingleResourceWithoutLodRecords=2, RootAuthoredMeshUnspecifiedSlotV1=3 };
enum class SourceRole : std::size_t { Model=0, MeshSkeleton=1, Animation=2, AnimationSkeleton=3 };
struct SourceIdentity { std::uint64_t epoch=0,bytes=0,fingerprint=0; };
struct Binding {
    VisualPolicy visualPolicy=VisualPolicy::OrdinaryVisualNoExtraction;
    RigPolicy rigPolicy=RigPolicy::CallerIdentityFullLocalPose;
    GeometryProfile geometryProfile=GeometryProfile::Weighted64;
    LodPolicy lodPolicy=LodPolicy::Authored;
    // Policies 2/3 use slot zero for compatibility, not a missing authored value.
    std::uint32_t levelOfDetail=0,bones=0,frames=0,blocks=0,maxFramesPerBlock=0;
    double duration=0,frameDuration=0,blockDuration=0,blockInverseDuration=0;
    // Fixed roles: MODL, mesh SKEL, animation HKXA, animation SKEL. Names of
    // the two selected skeleton objects are independent, even in one SKEL.
    std::array<SourceIdentity,4> sources;
    std::array<std::string,4> paths;
    std::string modelAlias,meshSkeletonName,clipName,animationSkeletonName;
};
struct Envelope {
    Binding binding;
    Bytes animation,skeleton; // Original HKXA and animation-role SKEL only.
};
const std::size_t MaximumStringBytes=4096;
const std::size_t MaximumMetadataBytes=64u*1024u;
const std::size_t MaximumSourceBytes=64u*1024u*1024u;
const std::size_t MaximumEnvelopeBytes=2*MaximumSourceBytes+MaximumMetadataBytes;
// FNV-1a is a local cache/byte identity, never authentication. Metadata compares
// the complete declared key. These functions do not qualify a clip, rig,
// mesh hierarchy, curve format/scale, native provider or retail playback.
std::uint64_t Fingerprint(const unsigned char* data,std::size_t bytes);
Bytes Metadata(const Binding& binding);
Bytes Serialize(const Envelope& envelope);
Envelope Parse(const Bytes& bytes);
std::string Alias(const Binding& binding);
bool Same(const Binding& a,const Binding& b);
} }
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_RIG_ANIMATION_H
#define WOLF2009_SOURCE_RIG_ANIMATION_H
#include "SkeletonData.h"
#include "SplineClipData.h"
namespace wolf2009 {
enum class SourceRigPolicy {
    Unspecified,
    // Caller chooses the exact SKEL. Curves are full local rigid poses with
    // track i -> bone i, without additive/reference-pose/partial-track fill.
    CallerIdentityFullLocalPoseV1
};
enum class SourceRigFailure { InvalidData, Unsupported, LimitExceeded };
class SourceRigError : public std::runtime_error {
    SourceRigFailure failure_;
  public:
    SourceRigError(SourceRigFailure failure,const std::string& text) : std::runtime_error(text),failure_(failure) {}
    SourceRigFailure Failure() const { return failure_; }
};
struct SourceRigRequest {
    std::string clipName,skeletonName;
    SourceRigPolicy policy=SourceRigPolicy::Unspecified;
};
struct SourceRigLimits {
    SplineClipDataLimits clip;
    SkeletonDataLimits skeleton;
    SplineTransformLimits codec;
    std::size_t combinedSourceBytes=128u*1024u*1024u;
    double modelTranslationMagnitude=100000000;
};
struct SourceRigIdentity {
    SourceRigRequest request;
    std::size_t animationBytes=0,skeletonBytes=0;
    std::uint64_t animationFingerprint=0,skeletonFingerprint=0; // Local byte identities only.
    std::uint32_t animationObjectOffset=0,nameTableIndex=0;
    std::uint32_t opaqueFloatTracks=0,opaqueAnnotationTracks=0;
    bool hasExtractedMotionFact=false;
};
// Portable pose composition over a named clip and an explicitly chosen rig.
// The first profile admits only a no-binding-table packet and full coverage.
// It does not infer a rig from count/names, retarget, blend additive clips,
// fill missing tracks, extract movement, apply placement or dispatch events.
// Nonidentity scale is refused by the shared codec before pose production.
class SourceRigAnimation {
  public:
    // Profile failures use SourceRigError. Checked reader/codec failures retain
    // SplineClipError/SplineCodecError; skeleton failures are runtime_error.
    // All derive from std::runtime_error; no failed load returns a fallback.
    static SourceRigAnimation Load(const std::vector<unsigned char>& hkxa,
        const std::vector<unsigned char>& skel,const SourceRigRequest& request,
        const SourceRigLimits& limits=SourceRigLimits());
    const SourceRigIdentity& Identity() const;
    const SkeletonData& Skeleton() const;
    const SplineTransformCodec& Curves() const;
    double Duration() const;
    double FrameDuration() const;
    std::size_t FrameCount() const;
    GreeterPose SamplePose(double seconds) const;
  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit SourceRigAnimation(std::shared_ptr<const Impl> impl) : impl_(impl) {}
};
} // namespace wolf2009
#endif

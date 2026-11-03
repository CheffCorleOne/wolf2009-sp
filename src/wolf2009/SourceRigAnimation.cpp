// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceRigAnimation.h"
#include <algorithm>
#include <cmath>
#include <utility>
#ifdef __FAST_MATH__
#error Source_rig_animation_requires_finite_IEEE_arithmetic
#endif
namespace wolf2009 {
namespace {
void Need(bool value,const char* text,SourceRigFailure failure=SourceRigFailure::InvalidData) {
    if(!value) throw SourceRigError(failure,text);
}
std::uint64_t Fingerprint(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned char byte:bytes) hash=(hash^byte)*UINT64_C(1099511628211);
    return hash;
}
GreeterJointPose Compose(const GreeterJointPose& parent,const GreeterJointPose& child,double translationLimit) {
    const auto& q=parent.rotation;const auto& v=child.translation;const auto& r=child.rotation;
    const std::array<double,3> twiceCross{{2*(q[1]*v[2]-q[2]*v[1]),2*(q[2]*v[0]-q[0]*v[2]),2*(q[0]*v[1]-q[1]*v[0])}};
    GreeterJointPose out;
    out.translation={{parent.translation[0]+v[0]+q[3]*twiceCross[0]+q[1]*twiceCross[2]-q[2]*twiceCross[1],
                      parent.translation[1]+v[1]+q[3]*twiceCross[1]+q[2]*twiceCross[0]-q[0]*twiceCross[2],
                      parent.translation[2]+v[2]+q[3]*twiceCross[2]+q[0]*twiceCross[1]-q[1]*twiceCross[0]}};
    out.rotation={{q[3]*r[0]+q[0]*r[3]+q[1]*r[2]-q[2]*r[1],q[3]*r[1]-q[0]*r[2]+q[1]*r[3]+q[2]*r[0],
                   q[3]*r[2]+q[0]*r[1]-q[1]*r[0]+q[2]*r[3],q[3]*r[3]-q[0]*r[0]-q[1]*r[1]-q[2]*r[2]}};
    double norm=0;for(double value:out.rotation) norm+=value*value;
    Need(std::isfinite(norm)&&norm>1e-12,"Invalid source rig composed quaternion");
    for(double& value:out.rotation) value/=std::sqrt(norm);
    for(double value:out.translation) Need(std::isfinite(value)&&std::abs(value)<=translationLimit,"Source rig model translation exceeds pose domain");
    return out;
}
}
struct SourceRigAnimation::Impl {
    SourceRigIdentity identity;
    SkeletonData skeleton;
    SplineTransformCodec curves;
    double modelTranslationMagnitude;
    Impl(SourceRigIdentity id,SkeletonData rig,SplineTransformCodec source,double magnitude)
        :identity(std::move(id)),skeleton(std::move(rig)),curves(std::move(source)),modelTranslationMagnitude(magnitude) {}
};
SourceRigAnimation SourceRigAnimation::Load(const std::vector<unsigned char>& hkxa,const std::vector<unsigned char>& skel,
    const SourceRigRequest& request,const SourceRigLimits& limits) {
    Need(request.policy==SourceRigPolicy::CallerIdentityFullLocalPoseV1,"Source rig requires an explicit full local-pose identity policy",SourceRigFailure::Unsupported);
    Need(hkxa.size()<=limits.combinedSourceBytes&&skel.size()<=limits.combinedSourceBytes-hkxa.size(),
         "Source rig combined input-byte budget",SourceRigFailure::LimitExceeded);
    Need(std::isfinite(limits.modelTranslationMagnitude)&&limits.modelTranslationMagnitude>0,"Invalid source rig model translation limit");
    const SplineClipData clip=SplineClipData::Load(hkxa,request.clipName,limits.clip);
    Need(clip.bindingCount==0&&clip.bindings.empty()&&clip.bindingState==SplineBindingFactsState::Absent,
         "Source rig profile does not admit a packet with binding metadata",SourceRigFailure::Unsupported);
    SkeletonData skeleton=ReadSkeletonData(skel,request.skeletonName,limits.skeleton);
    Need(clip.input.transformTracks==skeleton.joints.size(),"Source rig full-pose track/bone count mismatch",SourceRigFailure::Unsupported);
    SplineTransformCodec curves=SplineTransformCodec::Load(clip.input,SplineScalePolicy::RequireIdentityScale,limits.codec);
    SourceRigIdentity identity;identity.request=request;
    identity.animationBytes=hkxa.size();identity.skeletonBytes=skel.size();
    identity.animationFingerprint=clip.sourceFingerprint;identity.skeletonFingerprint=Fingerprint(skel);
    identity.animationObjectOffset=clip.animationObjectOffset;identity.nameTableIndex=clip.nameTableIndex;
    identity.opaqueFloatTracks=clip.input.floatTracks;identity.opaqueAnnotationTracks=clip.annotationTrackCount;
    identity.hasExtractedMotionFact=clip.extractedMotion.hasDataFixup||clip.extractedMotion.rawWord!=0;
    return SourceRigAnimation(std::make_shared<Impl>(std::move(identity),std::move(skeleton),std::move(curves),limits.modelTranslationMagnitude));
}
const SourceRigIdentity& SourceRigAnimation::Identity()const{return impl_->identity;}
const SkeletonData& SourceRigAnimation::Skeleton()const{return impl_->skeleton;}
const SplineTransformCodec& SourceRigAnimation::Curves()const{return impl_->curves;}
double SourceRigAnimation::Duration()const{return impl_->curves.Duration();}
double SourceRigAnimation::FrameDuration()const{return impl_->curves.FrameDuration();}
std::size_t SourceRigAnimation::FrameCount()const{return impl_->curves.FrameCount();}
GreeterPose SourceRigAnimation::SamplePose(double seconds)const {
    Need(std::isfinite(seconds),"Nonfinite source rig sample time");
    GreeterPose out;out.seconds=(std::max)(0.0,(std::min)(seconds,Duration()));
    const auto tracks=impl_->curves.Sample(out.seconds);
    out.local.reserve(tracks.size());out.model.reserve(tracks.size());
    for(std::size_t i=0;i<tracks.size();++i) {
        GreeterJointPose local;local.translation=tracks[i].translation;local.rotation=tracks[i].rotation;
        for(double value:local.translation) Need(std::isfinite(value)&&std::abs(value)<=impl_->modelTranslationMagnitude,"Source rig root/local translation exceeds pose domain");
        const int parent=impl_->skeleton.joints[i].parent;
        out.local.push_back(local);out.model.push_back(parent<0?local:Compose(out.model[std::size_t(parent)],local,impl_->modelTranslationMagnitude));
    }
    return out;
}
} // namespace wolf2009

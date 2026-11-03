// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiderMovement.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace wolf2009 { namespace {
const double Epsilon = .0002;
CollisionVec3 Add(CollisionVec3 a, CollisionVec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
CollisionVec3 Sub(CollisionVec3 a, CollisionVec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
CollisionVec3 Mul(CollisionVec3 a, double n) { return {a.x*n,a.y*n,a.z*n}; }
double Dot(CollisionVec3 a, CollisionVec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double Length(CollisionVec3 a) { return std::sqrt(Dot(a,a)); }
bool Near(double a,double b,double epsilon=1e-7) { return std::abs(a-b)<=epsilon; }
bool Same(CollisionVec3 a,CollisionVec3 b) { return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z); }
bool Finite(CollisionVec3 a,double m) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&std::abs(a.x)<=m&&std::abs(a.y)<=m&&std::abs(a.z)<=m; }
bool Positive(double a,double maximum) { return std::isfinite(a)&&a>0&&a<=maximum; }
bool ParametersValid(const RiderMovementParameters& p) {
    return Positive(p.coordinateLimit,10000000)&&Positive(p.forwardSpeed,10000)&&
        Positive(p.backwardSpeed,10000)&&Positive(p.forwardAcceleration,1e9)&&
        Positive(p.backwardAcceleration,1e9)&&Positive(p.halfWidth,1000)&&
        Positive(p.height,1000)&&std::isfinite(p.maximumSupportGap)&&p.maximumSupportGap>=0&&
        p.maximumSupportGap<=16&&Near(p.fixedSeconds,1.0/60.0,1e-12)&&
        Positive(p.maximumParentSpeed,10000)&&Positive(p.maximumParentDisplacement,10000);
}
void Mix(std::uint64_t& h,std::uint64_t v) { for(int k=0;k<8;++k){h^=static_cast<unsigned char>(v>>(k*8));h*=1099511628211ull;} }
void Mix(std::uint64_t& h,double v) { std::uint64_t u;static_assert(sizeof(double)==8,"IEEE64 required");std::memcpy(&u,&v,8);Mix(h,u); }
void Mix(std::uint64_t& h,const std::string& s) { for(char c:s){h^=static_cast<unsigned char>(c);h*=1099511628211ull;}Mix(h,std::uint64_t(s.size())); }
std::uint64_t ProfileToken(const RiderMovementProfile& p) {
    std::uint64_t h=14695981039346656037ull;Mix(h,p.geometryToken);Mix(h,p.instanceId);Mix(h,p.selectedBodyName);
    Mix(h,p.initialLocalFeet.x);Mix(h,p.initialLocalFeet.y);Mix(h,p.initialLocalFeet.z);
    Mix(h,p.minimumY);Mix(h,p.maximumY);Mix(h,p.supportTop);Mix(h,p.supportGap);
    Mix(h,std::uint64_t(p.floorShape));Mix(h,std::uint64_t(p.backShape));Mix(h,std::uint64_t(p.frontShape));
    Mix(h,std::uint64_t(p.backPlane));Mix(h,std::uint64_t(p.frontPlane));
    const auto& a=p.parameters;const double values[]={a.forwardSpeed,a.backwardSpeed,a.forwardAcceleration,a.backwardAcceleration,a.halfWidth,a.height,a.maximumSupportGap,a.fixedSeconds,a.coordinateLimit,a.maximumParentSpeed,a.maximumParentDisplacement};
    for(double v:values)Mix(h,v);return h?h:1;
}
bool ProfileValid(const RiderMovementProfile& p) {
    return p.ready&&ParametersValid(p.parameters)&&!p.instanceId.empty()&&p.instanceId.size()<=4096&&
        !p.selectedBodyName.empty()&&p.selectedBodyName.size()<=4096&&p.geometryToken&&p.sourceToken&&
        p.floorShape&&p.backShape&&p.frontShape&&Finite(p.initialLocalFeet,p.parameters.coordinateLimit)&&
        std::isfinite(p.minimumY)&&std::isfinite(p.maximumY)&&p.minimumY<p.initialLocalFeet.y&&
        p.initialLocalFeet.y<p.maximumY&&std::abs(p.minimumY)<=p.parameters.coordinateLimit&&
        std::abs(p.maximumY)<=p.parameters.coordinateLimit&&std::isfinite(p.supportTop)&&
        std::isfinite(p.supportGap)&&p.supportGap>=-1e-7&&p.supportGap<=p.parameters.maximumSupportGap&&
        Near(p.initialLocalFeet.z-p.supportTop,p.supportGap)&&p.sourceToken==ProfileToken(p);
}
bool BoundsValid(const StaticCollisionBox& b,double limit) {
    return Finite(b.minimum,limit)&&Finite(b.maximum,limit)&&b.minimum.x<=b.maximum.x&&b.minimum.y<=b.maximum.y&&b.minimum.z<=b.maximum.z;
}
StaticCollisionBox Bounds(const ConvexContactHull& h) {
    StaticCollisionBox b;b.minimum=b.maximum=h.vertices.at(0);
    for(const auto& v:h.vertices){b.minimum.x=std::min(b.minimum.x,v.x);b.minimum.y=std::min(b.minimum.y,v.y);b.minimum.z=std::min(b.minimum.z,v.z);b.maximum.x=std::max(b.maximum.x,v.x);b.maximum.y=std::max(b.maximum.y,v.y);b.maximum.z=std::max(b.maximum.z,v.z);}
    const CollisionVec3 r(h.radius,h.radius,h.radius);b.minimum=Sub(b.minimum,r);b.maximum=Add(b.maximum,r);return b;
}
bool Overlap(const StaticCollisionBox& a,const StaticCollisionBox& b) {
    return a.minimum.x<b.maximum.x-Epsilon&&a.maximum.x>b.minimum.x+Epsilon&&
           a.minimum.y<b.maximum.y-Epsilon&&a.maximum.y>b.minimum.y+Epsilon&&
           a.minimum.z<b.maximum.z-Epsilon&&a.maximum.z>b.minimum.z+Epsilon;
}
bool Eligible(const ConvexContactHull& h,std::uint32_t mask) { return !h.contentsKnown||(h.rawContents&mask)!=0; }
RiderMovementSetup Failed(RiderMovementStatus status,const char* reason) { RiderMovementSetup r;r.status=status;r.reason=reason;return r; }
struct End { bool found=false;double y=0;std::uint32_t shape=0;std::size_t plane=0,ties=0; };
End FindEnd(const ModelPrimitiveCollision& model,CollisionVec3 feet,const RiderMovementQualification& q,
            const RiderMovementParameters& p,int direction) {
    End best;const CollisionVec3 center=Add(feet,{0,0,p.height*.5}),half(p.halfWidth,p.halfWidth,p.height*.5);
    ConvexContactLimits limits;limits.coordinate=p.coordinateLimit;
    for(const auto& primitive:model.localPrimitives){const auto& h=primitive.geometry;
        if(primitive.kind!=ModelPrimitiveKind::ConvexHull||!Eligible(h,q.queryMask))continue;
        for(std::size_t i=0;i<h.planes.size();++i){const auto& plane=h.planes[i];const double length=Length(plane.normal);const CollisionVec3 n=Mul(plane.normal,1/length);
            if(std::abs(n.x)>1e-6||std::abs(n.z)>1e-6||n.y*direction>-.999999)continue;
            const double support=std::abs(n.x)*half.x+std::abs(n.y)*half.y+std::abs(n.z)*half.z+h.radius;
            const double y=(support-plane.distance/length-n.x*center.x-n.z*center.z)/n.y;
            if(!std::isfinite(y)||(y-feet.y)*direction<=Epsilon)continue;
            ConvexContactQualification witness;witness.instanceId=q.instanceId;witness.verifiedQueryMask=q.queryMask;
            witness.sourceShapeOffset=h.sourceShapeOffset;witness.sourceRegion=h.sourceRegion;witness.planeIndex=i;
            const double beyond=std::max(.01,std::abs(y-feet.y)*.01);
            const auto hit=SweepConvexContact(h,witness,{},center,half,{0,y-feet.y+direction*beyond,0},q.queryMask,true,limits);
            if(hit.status!=ConvexContactStatus::ProvenFaceHit)continue;
            const double endpoint=hit.center.y;
            if(!best.found||(endpoint-best.y)*direction<-Epsilon){best.found=true;best.y=endpoint;best.shape=h.sourceShapeOffset;best.plane=i;best.ties=1;}
            else if(Near(endpoint,best.y,Epsilon))++best.ties;
        }
    }return best;
}
} // namespace
RiderMovementSetup BuildRiderMovementProfile(const ModelPrimitiveCollision& model,CollisionVec3 feet,
    const RiderMovementQualification& q,const RiderMovementParameters& p) {
    if(!ParametersValid(p))return Failed(RiderMovementStatus::InvalidParameters,"Invalid calibrated rider parameters");
    if(!q.centralOpeningCaseVerified||q.instanceId.empty()||q.instanceId.size()>4096||q.queryMask!=CollisionPlayerMask)
        return Failed(RiderMovementStatus::Unqualified,"Opening scene/input/carry context was not established");
    if(!Finite(feet,p.coordinateLimit))return Failed(RiderMovementStatus::InvalidInput,"Invalid genuine local feet");
    if(!model.localPoseVerified||model.localPrimitives.empty()||model.localPrimitives.size()>20000||model.omittedGeometryBounds.size()>20000||!model.omittedBoundsComplete)
        return Failed(RiderMovementStatus::UnknownGeometry,"Local pose or omitted geometry bounds are unknown");
    try {
        std::vector<StaticCollisionBox> bounds;std::uint64_t geometryToken=14695981039346656037ull;
        ConvexContactLimits limits;limits.coordinate=p.coordinateLimit;
        for(const auto& primitive:model.localPrimitives){const auto& h=primitive.geometry;
            if(!h.sourceShapeOffset||!h.sourceRegion)return Failed(RiderMovementStatus::UnknownGeometry,"Missing source geometry provenance");
            // Reuse the public bounded validator without qualifying any face.
            SweepConvexContact(h,ConvexContactQualification(),{},Add(feet,{0,0,p.height*.5}),{p.halfWidth,p.halfWidth,p.height*.5},{},q.queryMask,true,limits);
            const auto b=Bounds(h);if(!BoundsValid(b,p.coordinateLimit))return Failed(RiderMovementStatus::UnknownGeometry,"Invalid primitive bounds");
            bounds.push_back(b);Mix(geometryToken,h.sourceRegion);
        }
        std::size_t floor=static_cast<std::size_t>(-1),ties=0;double bestGap=p.maximumSupportGap+1;
        for(std::size_t i=0;i<model.localPrimitives.size();++i){const auto& primitive=model.localPrimitives[i];const auto& b=bounds[i];
            if(primitive.kind!=ModelPrimitiveKind::AxisBox||primitive.geometry.radius>1e-7||!Eligible(primitive.geometry,q.queryMask))continue;
            const double gap=feet.z-b.maximum.z;
            if(gap< -1e-7||gap>p.maximumSupportGap||feet.x-p.halfWidth<b.minimum.x||feet.x+p.halfWidth>b.maximum.x||feet.y-p.halfWidth<b.minimum.y||feet.y+p.halfWidth>b.maximum.y)continue;
            if(gap<bestGap-1e-7){floor=i;bestGap=gap;ties=1;}else if(Near(gap,bestGap))++ties;
        }
        if(floor==static_cast<std::size_t>(-1)||ties!=1)return Failed(RiderMovementStatus::UnsupportedSupport,"No unique full-footprint axis-box support under genuine feet");
        const End back=FindEnd(model,feet,q,p,-1),front=FindEnd(model,feet,q,p,1);
        if(!back.found||!front.found||back.ties!=1||front.ties!=1)return Failed(RiderMovementStatus::UnsupportedEndFaces,"No unique source-qualified flat end faces");
        StaticCollisionBox corridor;corridor.minimum={feet.x-p.halfWidth,back.y-p.halfWidth,feet.z};corridor.maximum={feet.x+p.halfWidth,front.y+p.halfWidth,feet.z+p.height};
        const auto& support=bounds[floor];
        if(corridor.minimum.y<support.minimum.y-Epsilon||corridor.maximum.y>support.maximum.y+Epsilon)
            return Failed(RiderMovementStatus::UnsupportedSupport,"Support does not cover the complete source-bounded central path");
        for(std::size_t i=0;i<bounds.size();++i)if(Eligible(model.localPrimitives[i].geometry,q.queryMask)&&Overlap(corridor,bounds[i]))
            return Failed(RiderMovementStatus::UnsupportedCorridorOverlap,"A primitive bound overlaps the central path; no invented obstacle or clear passage");
        for(const auto& omission:model.omittedGeometryBounds){if(!omission.boundsKnown||!omission.sourceRegion||!BoundsValid(omission.conservativeBounds,p.coordinateLimit))
                return Failed(RiderMovementStatus::UnknownGeometry,"Unknown omitted-shape bound");
            Mix(geometryToken,omission.sourceRegion);
            if((!omission.conservativeBounds.contentsKnown||(omission.conservativeBounds.contents&q.queryMask))&&Overlap(corridor,omission.conservativeBounds))
                return Failed(RiderMovementStatus::UnsupportedCorridorOverlap,"Omitted shape may overlap central path");
        }
        RiderMovementSetup out;out.status=RiderMovementStatus::Ready;out.reason="Qualified central path only";
        auto& profile=out.profile;profile.parameters=p;profile.instanceId=q.instanceId;profile.selectedBodyName=model.selectedBodyName;profile.initialLocalFeet=feet;
        profile.minimumY=back.y;profile.maximumY=front.y;profile.floorShape=model.localPrimitives[floor].geometry.sourceShapeOffset;
        profile.backShape=back.shape;profile.frontShape=front.shape;profile.backPlane=back.plane;profile.frontPlane=front.plane;
        profile.supportTop=support.maximum.z;profile.supportGap=bestGap;profile.geometryToken=geometryToken?geometryToken:1;
        profile.ready=true;profile.sourceToken=ProfileToken(profile);
        if(!ProfileValid(profile))return Failed(RiderMovementStatus::InvalidState,"Invalid constructed rider profile");return out;
    }catch(const std::exception&){return Failed(RiderMovementStatus::UnknownGeometry,"Malformed or unsupported source geometry");}
}
RiderMovementResult InitializeRiderMovement(const RiderMovementProfile& profile,const RiderMovementParent& parent) {
    RiderMovementResult out;if(!ProfileValid(profile))return out;
    if(parent.instanceId!=profile.instanceId){out.status=RiderMovementStatus::WrongParent;out.reason="Wrong parent instance";return out;}
    const auto world=Add(parent.translation,profile.initialLocalFeet);
    if(!Finite(parent.translation,profile.parameters.coordinateLimit)||!Finite(world,profile.parameters.coordinateLimit)){out.status=RiderMovementStatus::InvalidInput;out.reason="Invalid parent placement";return out;}
    out.status=RiderMovementStatus::Initialized;out.reason="Central rider initialized";out.state.initialized=true;
    out.state.localFeet=profile.initialLocalFeet;out.state.worldFeet=world;out.state.parentTranslation=parent.translation;
    out.state.sourceToken=profile.sourceToken;out.state.parentSample=parent.sample;return out;
}
RiderMovementResult StepRiderMovement(const RiderMovementProfile& profile,const RiderMovementState& state,
    RiderMovementInput input,const RiderMovementParent& parent,double seconds) {
    RiderMovementResult out;out.state=state;
    if(!ProfileValid(profile)||!state.initialized||state.sourceToken!=profile.sourceToken||
       !Finite(state.localFeet,profile.parameters.coordinateLimit)||!Finite(state.localOffset,profile.parameters.coordinateLimit)||
       !Finite(state.worldFeet,profile.parameters.coordinateLimit)||!Finite(state.parentTranslation,profile.parameters.coordinateLimit)||
       !std::isfinite(state.relativeSpeed)||state.relativeSpeed>profile.parameters.forwardSpeed+1e-7||state.relativeSpeed< -profile.parameters.backwardSpeed-1e-7||
       !Near(state.localFeet.x,profile.initialLocalFeet.x)||!Near(state.localFeet.z,profile.initialLocalFeet.z)||
       !Same(state.localOffset,Sub(state.localFeet,profile.initialLocalFeet))||!Same(state.worldFeet,Add(state.parentTranslation,state.localFeet))||
       state.localFeet.y<profile.minimumY-1e-7||state.localFeet.y>profile.maximumY+1e-7)return out;
    const int command=static_cast<int>(input);
    if(command< -1||command>1){out.status=RiderMovementStatus::InvalidInput;out.reason="Only verified forward/release/backward input is supported";return out;}
    if(!std::isfinite(seconds)||!Near(seconds,profile.parameters.fixedSeconds,1e-9)){out.status=RiderMovementStatus::InvalidTimeStep;out.reason="Rider requires its calibrated fixed step";return out;}
    if(parent.instanceId!=profile.instanceId){out.status=RiderMovementStatus::WrongParent;out.reason="Wrong parent instance";return out;}
    if(parent.sample<=state.parentSample){out.status=RiderMovementStatus::StaleParent;out.reason="Parent sample is not newer";return out;}
    if(!Finite(parent.translation,profile.parameters.coordinateLimit)){out.status=RiderMovementStatus::InvalidInput;out.reason="Invalid parent pose";return out;}
    const auto delta=Sub(parent.translation,state.parentTranslation);const double distance=Length(delta);
    if(std::abs(delta.x)>1e-6||std::abs(delta.z)>1e-6){out.status=RiderMovementStatus::UnsupportedParentMotion;out.reason="Only verified rail-axis parent translation is supported";return out;}
    if(distance>profile.parameters.maximumParentDisplacement||distance>profile.parameters.maximumParentSpeed*seconds+1e-6){out.status=RiderMovementStatus::ExcessiveParentMotion;out.reason="Parent movement exceeds qualified budgets";return out;}
    if(command*state.relativeSpeed< -1e-7){out.status=RiderMovementStatus::UnsupportedReversal;out.reason="Release to rest before reversing unqualified transition";return out;}
    const auto& p=profile.parameters;const double target=command>0?p.forwardSpeed:command<0?-p.backwardSpeed:0;
    const double acceleration=(command>0||(command==0&&state.relativeSpeed>0))?p.forwardAcceleration:p.backwardAcceleration;
    const double change=acceleration*seconds;double velocity=state.relativeSpeed;
    if(velocity<target)velocity=std::min(target,velocity+change);else velocity=std::max(target,velocity-change);
    double y=state.localFeet.y+velocity*seconds;bool end=false;
    if(y<=profile.minimumY&&velocity<0){y=profile.minimumY;velocity=0;end=true;out.contactShape=profile.backShape;out.contactPlane=profile.backPlane;}
    if(y>=profile.maximumY&&velocity>0){y=profile.maximumY;velocity=0;end=true;out.contactShape=profile.frontShape;out.contactPlane=profile.frontPlane;}
    RiderMovementState next=state;next.localFeet.y=y;next.localOffset=Sub(next.localFeet,profile.initialLocalFeet);
    next.relativeSpeed=velocity;next.parentTranslation=parent.translation;next.parentSample=parent.sample;next.worldFeet=Add(parent.translation,next.localFeet);
    if(!Finite(next.worldFeet,p.coordinateLimit)){out.status=RiderMovementStatus::InvalidInput;out.reason="Rider world position exceeds budget";return out;}
    out.state=next;out.worldVelocity=Add(Mul(delta,1/seconds),{0,velocity,0});
    out.status=end?RiderMovementStatus::EndContact:(!Near(y,state.localFeet.y)?RiderMovementStatus::Moved:RiderMovementStatus::Resting);
    out.reason=end?"Measured source end-face contact":"Central path step only";return out;
}
} // namespace wolf2009

// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceRagdoll.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#ifdef __FAST_MATH__
#error Source_ragdoll_requires_finite_IEEE_arithmetic
#endif

namespace wolf2009 { namespace {
void Need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
const double Units=1.0/.0254;
CollisionVec3 Add(CollisionVec3 a,CollisionVec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CollisionVec3 Mul(CollisionVec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double Dot(CollisionVec3 a,CollisionVec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
CollisionVec3 Cross(CollisionVec3 a,CollisionVec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
CollisionVec3 Rotate(const CollisionRigidTransform& t,CollisionVec3 p){return Add(Add(Mul(t.axis[0],p.x),Mul(t.axis[1],p.y)),Mul(t.axis[2],p.z));}
CollisionRigidTransform Compose(const CollisionRigidTransform& a,const CollisionRigidTransform& b){CollisionRigidTransform t;for(int i=0;i<3;++i)t.axis[i]=Rotate(a,b.axis[i]);t.translation=Add(a.translation,Rotate(a,b.translation));return t;}
double C(CollisionVec3 a,int i){return i==0?a.x:i==1?a.y:a.z;}
bool Finite(CollisionVec3 a,double bound){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&std::abs(a.x)<=bound&&std::abs(a.y)<=bound&&std::abs(a.z)<=bound;}
void Limits(const SourceRagdollLimits& l){
    Need(l.sourceBytes>=208&&l.objects&&l.fixups&&l.bodies&&l.constraints&&l.joints&&l.shapeVisits&&l.boxes&&l.nesting&&l.names,"Invalid ragdoll count limits");
    Need(std::isfinite(l.coordinateGameUnits)&&l.coordinateGameUnits>0&&std::isfinite(l.maximumRadiusGameUnits)&&l.maximumRadiusGameUnits>=0,"Invalid ragdoll numeric limits");
}
void Rigid(const CollisionRigidTransform& t,const SourceRagdollLimits& l){
    Need(Finite(t.translation,l.coordinateGameUnits),"Ragdoll translation exceeds finite domain");
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)Need(std::isfinite(Dot(t.axis[i],t.axis[j]))&&std::abs(Dot(t.axis[i],t.axis[j])-(i==j?1.:0.))<=.00002,"Ragdoll basis is scaled/nonorthonormal");
    Need(std::abs(Dot(Cross(t.axis[0],t.axis[1]),t.axis[2])-1)<=.00002,"Ragdoll basis is reflected/improper");
}
std::uint64_t Fingerprint(const std::vector<unsigned char>& b){std::uint64_t h=14695981039346656037ull;for(unsigned char c:b){h^=c;h*=1099511628211ull;}return h;}
class Reader : public detail::HavokPack {
    const SourceRagdollLimits& limits;
    SourceRagdoll out;
    std::size_t nameBytes=0,visits=0,boxes=0;
    std::uint64_t occurrence=0;
    std::vector<std::uint32_t> stack;
    std::vector<WorldCollisionTransformRecord> transforms;
    CollisionVec3 V(std::size_t p){return {F(p),F(p+4),F(p+8)};}
    unsigned B(std::size_t p){DataRange(p,1);return bytes[sections[2].absolute+p];}
    std::string Name(std::uint32_t p){const auto s=DataString(p);Need(s.size()<=limits.names-nameBytes,"Ragdoll aggregate name budget exceeded");nameBytes+=s.size();return s;}
    template<std::size_t N> void Raw(std::uint32_t p,std::array<std::uint32_t,N>& v){ArrayRange(p,N,4);for(std::size_t i=0;i<N;++i)v[i]=DU(p+i*4);}
    CollisionRigidTransform Transform(std::uint32_t p){
        DataRange(p,64);CollisionRigidTransform t;for(int i=0;i<3;++i)t.axis[i]=V(p+i*16);t.translation=Mul(V(p+48),Units);Rigid(t,limits);return t;
    }
    std::pair<std::uint32_t,std::size_t> Array(std::uint32_t p,std::size_t stride,std::size_t max){
        DataRange(p,12);const std::size_t n=DU(p+4);Need(n<=max,"Ragdoll array count exceeds budget");
        if(!n)return {0,0};const auto a=Pointer(p);ArrayRange(a,n,stride);return {a,n};
    }
    void Missing(SourceRagdollBody& b,std::uint32_t p,const std::string& type,const CollisionRigidTransform& placement){
        SourceRagdollOmission o;o.sourceObjectOffset=p;o.type=type;o.placement=placement;b.omissions.push_back(o);out.omissions.push_back(o);
    }
    double Radius(std::uint32_t p){const auto r=F(p+16)*Units;Need(r>=0&&r<=limits.maximumRadiusGameUnits,"Ragdoll radius exceeds finite nonnegative budget");return r;}
    void Shape(SourceRagdollBody& b,std::uint32_t p,const CollisionRigidTransform& t,std::uint32_t filter,bool hasFilter){
        Need(visits<limits.shapeVisits,"Ragdoll shape visit budget exceeded");++visits;
        Need(stack.size()<limits.nesting&&std::find(stack.begin(),stack.end(),p)==stack.end(),"Ragdoll shape cycle/nesting limit");
        stack.push_back(p);Need(occurrence!=(std::numeric_limits<std::uint64_t>::max)(),"Ragdoll occurrence overflow");++occurrence;
        const auto type=Type(p);
        if(type=="hkpListShape"){
            const auto children=Array(p+24,16,limits.shapeVisits);
            for(std::size_t i=0;i<children.second;++i){const auto at=children.first+std::uint32_t(i*16);Shape(b,Pointer(at),t,DU(at+4),true);}
        }else if(type=="hkpConvexTranslateShape"||type=="hkpConvexTransformShape"){
            WorldCollisionTransformRecord r;r.sourceObjectOffset=p;r.radius=Radius(p);r.hasRadius=true;
            if(type=="hkpConvexTranslateShape"){DataRange(p,48);r.kind=WorldCollisionTransformKind::Translate;r.local.translation=Mul(V(p+32),Units);}
            else {DataRange(p,96);r.kind=WorldCollisionTransformKind::ConvexMatrix;r.local=Transform(p+32);}
            Rigid(r.local,limits);const auto combined=Compose(t,r.local);Rigid(combined,limits);
            transforms.push_back(r);Shape(b,Pointer(p+24),combined,filter,hasFilter);transforms.pop_back();
        }else if(type=="hkpBoxShape"){
            Need(boxes<limits.boxes,"Ragdoll box aggregate budget exceeded");++boxes;SourceRagdollBox box;
            Raw(p,box.rawHeader);box.sourceShapeOffset=p;box.occurrence=occurrence;box.local=t;box.halfExtents=Mul(V(p+32),Units);box.radius=Radius(p);
            Need(Finite(box.halfExtents,limits.coordinateGameUnits)&&box.halfExtents.x>=0&&box.halfExtents.y>=0&&box.halfExtents.z>=0,"Ragdoll box extents invalid");
            box.rawChildFilter=filter;box.hasChildFilter=hasFilter;box.transforms=transforms;
            for(const auto& r:transforms)if(r.radius!=box.radius)box.radiusHierarchyVerified=false;
            b.boxes.push_back(box);
        }else Missing(b,p,type,t);
        stack.pop_back();
    }
    SourceRagdollBody Body(std::uint32_t o){
        Need(Type(o)=="hkpRigidBody","Ragdoll body pointer has unexpected type");SourceRagdollBody b;b.sourceObjectOffset=o;Raw(o,b.rawHeader);
        b.name=Name(Pointer(o+112));Need(!b.name.empty(),"Ragdoll rigid body name is empty");b.rootShapeOffset=Pointer(o+16);b.rawFilterInfo=DU(o+44);
        b.pose=Transform(o+224);b.motionType=B(o+216);b.responseType=B(o+128);
        b.inverseInertiaMetres=V(o+400);b.inverseMass=F(o+412);
        Need(b.inverseMass>=0&&b.inverseInertiaMetres.x>=0&&b.inverseInertiaMetres.y>=0&&b.inverseInertiaMetres.z>=0,"Ragdoll inverse mass/inertia is negative");
        b.friction=F(o+132);b.restitution=F(o+136);b.linearDamping=F(o+388);b.angularDamping=F(o+392);
        b.linearVelocity=Mul(V(o+416),Units);b.angularVelocity=V(o+432);b.centerOfMassLocal=Mul(V(o+352),Units);
        Need(Finite(b.centerOfMassLocal,limits.coordinateGameUnits)&&Finite(b.linearVelocity,limits.coordinateGameUnits)&&Finite(b.angularVelocity,limits.coordinateGameUnits),"Ragdoll COM/velocity exceeds budget");
        Shape(b,b.rootShapeOffset,CollisionRigidTransform(),0,false);b.geometryComplete=b.omissions.empty()&&!b.boxes.empty();return b;
    }
    void Hinge(std::uint32_t o,const std::map<std::uint32_t,std::size_t>& bodyIndices){
        Need(Type(o)=="hkpConstraintInstance","Ragdoll constraint pointer has unexpected type");SourceRagdollHinge h;h.sourceInstanceOffset=o;Raw(o,h.rawInstance);h.sourceDataOffset=Pointer(o+12);
        Need(pointers.find(o+16)==pointers.end(),"Ragdoll constraint modifiers are unsupported");
        for(int i=0;i<2;++i){const auto b=Pointer(o+20+i*4);const auto found=bodyIndices.find(b);Need(found!=bodyIndices.end(),"Ragdoll constraint refers outside selected body list");h.body[i]=found->second;}
        Need(h.body[0]!=h.body[1],"Ragdoll constraint joins a body to itself");h.name=Name(Pointer(o+32));h.priority=B(o+28);
        if(Type(h.sourceDataOffset)!="hkpLimitedHingeConstraintData"){
            SourceRagdollOmission missing;missing.sourceObjectOffset=h.sourceDataOffset;missing.type=Type(h.sourceDataOffset);out.omissions.push_back(missing);return;
        }
        Raw(h.sourceDataOffset,h.rawData);const auto a=h.sourceDataOffset+16;
        Need((DU(a)&65535u)==2&&(DU(a+144)&65535u)==18&&(DU(a+164)&65535u)==17&&(DU(a+176)&65535u)==14&&(DU(a+192)&65535u)==12&&(DU(a+196)&65535u)==5,"Ragdoll limited hinge atom sequence is unsupported");
        h.localFrame[0]=Transform(a+16);h.localFrame[1]=Transform(a+80);
        Need(B(a+146)<=1&&B(a+166)<=1&&B(a+178)<=1,"Ragdoll hinge enabled flags are noncanonical");
        h.motor.enabled=B(a+146)!=0;h.motorAxis=B(a+147);h.motor.targetAngle=F(a+156);
        h.frictionEnabled=B(a+166)!=0;h.frictionAxis=B(a+167);h.frictionAxes=B(a+168);h.maxFrictionTorque=F(a+172);
        h.limitEnabled=B(a+178)!=0;h.limitAxis=B(a+179);h.minimumAngle=F(a+180);h.maximumAngle=F(a+184);h.limitTau=F(a+188);
        Need(h.limitAxis<3&&h.motorAxis<3&&h.frictionAxis<3&&h.frictionAxes<=3-h.frictionAxis,"Ragdoll hinge axis index/count invalid");
        Need(h.minimumAngle<=h.maximumAngle&&h.maxFrictionTorque>=0,"Ragdoll hinge angle/torque domain invalid");
        const auto m=pointers.find(a+160);Need(m!=pointers.end()||DU(a+160)==0,"Ragdoll motor pointer lacks its required fixup");
        if(m!=pointers.end()){
            h.motor.present=true;h.motor.sourceObjectOffset=m->second;h.motor.type=Type(m->second);
            if(h.motor.type!="hkpPositionConstraintMotor"){
                SourceRagdollOmission missing;missing.sourceObjectOffset=m->second;missing.type=h.motor.type;out.omissions.push_back(missing);
            }else{
                Raw(m->second,h.motor.rawHeader);h.motor.minForce=F(m->second+12);h.motor.maxForce=F(m->second+16);
                h.motor.tau=F(m->second+20);h.motor.damping=F(m->second+24);h.motor.proportionalRecoveryVelocity=F(m->second+28);h.motor.constantRecoveryVelocity=F(m->second+32);
                Need(h.motor.minForce<=h.motor.maxForce,"Ragdoll motor force interval invalid");
            }
        }else Need(!h.motor.enabled,"Enabled ragdoll hinge motor is missing");
        out.hinges.push_back(h);
    }
  public:
    Reader(const std::vector<unsigned char>& b,const SourceRagdollLimits& l):detail::HavokPack(b,l.sourceBytes,l.objects,l.fixups),limits(l){}
    SourceRagdoll Read(const std::string& name){
        Need(!name.empty()&&name.size()<=limits.names,"Invalid ragdoll variant name");const auto variants=Array(U(28),16,limits.objects);std::size_t matches=0;
        for(std::size_t i=0;i<variants.second;++i){const auto at=variants.first+std::uint32_t(i*16);if(DataString(Pointer(at))!=name)continue;
            Need(++matches==1,"Duplicate named ragdoll root variant");Need(DataString(Pointer(at+4))=="hkaRagdollInstance","Named ragdoll variant class mismatch");out.sourceObjectOffset=Pointer(at+8);
        }
        Need(matches==1&&Type(out.sourceObjectOffset)=="hkaRagdollInstance","Named hkaRagdollInstance missing/type mismatch");out.rootVariantName=name;
        const auto o=out.sourceObjectOffset;DataRange(o,48);const auto bodies=Array(o+8,4,limits.bodies),constraints=Array(o+20,4,limits.constraints),boneMap=Array(o+32,4,limits.joints);
        Need(bodies.second>0,"Selected ragdoll has no bodies");const auto skeleton=Pointer(o+44);Need(Type(skeleton)=="hkaSkeleton","Ragdoll skeleton pointer has wrong type");
        const auto skeletonName=Name(Pointer(skeleton));SkeletonDataLimits sk;sk.sourceBytes=limits.sourceBytes;sk.objects=limits.objects;sk.fixups=limits.fixups;sk.joints=limits.joints;sk.nameBytes=limits.names;
        out.skeleton=ReadSkeletonData(bytes,skeletonName,sk);Need(out.skeleton.sourceObjectOffset==skeleton,"Ragdoll skeleton differs from named animation-container skeleton");
        Need(boneMap.second==out.skeleton.joints.size(),"Ragdoll bone-to-body map count differs from skeleton");
        for(std::size_t i=0;i<boneMap.second;++i){const auto word=DU(boneMap.first+i*4);const std::int64_t v=word>=0x80000000u?std::int64_t(word)-0x100000000ll:word;
            Need(v>=-1&&(v<0||std::uint64_t(v)<bodies.second),"Ragdoll bone-to-body map index out of range");out.boneToBody.push_back(std::int32_t(v));}
        std::map<std::uint32_t,std::size_t> bodyIndices;std::set<std::string> bodyNames;
        for(std::size_t i=0;i<bodies.second;++i){const auto body=Pointer(bodies.first+i*4);Need(bodyIndices.insert({body,i}).second,"Duplicate ragdoll body pointer");auto b=Body(body);Need(bodyNames.insert(b.name).second,"Duplicate ragdoll body name");out.bodies.push_back(std::move(b));}
        std::set<std::uint32_t> seen;for(std::size_t i=0;i<constraints.second;++i){const auto c=Pointer(constraints.first+i*4);Need(seen.insert(c).second,"Duplicate ragdoll constraint pointer");Hinge(c,bodyIndices);}
        out.sourceBytes=bytes.size();out.sourceFingerprint=Fingerprint(bytes);out.rawSource=bytes;out.complete=out.omissions.empty();
        for(const auto& b:out.bodies)out.complete=out.complete&&b.geometryComplete;
        return std::move(out);
    }
};
}
SourceRagdoll DecodeSourceRagdoll(const std::vector<unsigned char>& bytes,const std::string& name,const SourceRagdollLimits& limits){Limits(limits);return Reader(bytes,limits).Read(name);}
SourceRagdollNativeBox BuildSourceRagdollNativeBox(const SourceRagdollBody& body,SourceRagdollHullPolicy policy,const SourceRagdollLimits& limits){
    Limits(limits);Need(policy==SourceRagdollHullPolicy::NativeConservativeHullV1,"Explicit native ragdoll hull policy is required");
    Need(body.geometryComplete&&body.omissions.empty()&&body.boxes.size()==1,"Native ragdoll V1 requires exactly one complete source box; compound convexification is not implicit");
    const auto& b=body.boxes.front();Rigid(b.local,limits);Need(b.radiusHierarchyVerified,"Native ragdoll box wrapper radii differ");
    Need(Finite(b.halfExtents,limits.coordinateGameUnits)&&b.halfExtents.x>0&&b.halfExtents.y>0&&b.halfExtents.z>0&&std::isfinite(b.radius)&&b.radius>=0&&b.radius<=limits.maximumRadiusGameUnits,"Native ragdoll box geometry invalid");
    SourceRagdollNativeBox out;out.local=b.local;out.sourceRadius=b.radius;out.sourceShapeOffset=b.sourceShapeOffset;
    double expanded[3];for(int i=0;i<3;++i){const double sum=C(b.halfExtents,i)+b.radius;expanded[i]=std::nextafter(sum,(std::numeric_limits<double>::infinity)());Need(std::isfinite(expanded[i])&&expanded[i]<=limits.coordinateGameUnits,"Native rounded box exceeds finite coordinate budget");}
    out.halfExtents={expanded[0],expanded[1],expanded[2]};double squared=0;for(int i=0;i<3;++i){const auto delta=std::nextafter(expanded[i]-C(b.halfExtents,i),(std::numeric_limits<double>::infinity)());const auto term=std::nextafter(delta*delta,(std::numeric_limits<double>::infinity)());squared=std::nextafter(squared+term,(std::numeric_limits<double>::infinity)());}
    const auto length=std::nextafter(std::sqrt(squared),(std::numeric_limits<double>::infinity)());
    out.maximumRoundingExcess=std::nextafter(std::max(0.,length-b.radius),(std::numeric_limits<double>::infinity)());return out;
}
CollisionVec3 SourceRagdollHingeLimitCenter(CollisionVec3 axis,CollisionVec3 reference,double minimum,double maximum){
    const double pi=std::acos(-1.);Need(Finite(axis,2)&&Finite(reference,2)&&std::abs(Dot(axis,axis)-1)<.00002&&std::abs(Dot(reference,reference)-1)<.00002&&std::abs(Dot(axis,reference))<.00002,"Native hinge midpoint needs orthonormal axis/reference");
    Need(std::isfinite(minimum)&&std::isfinite(maximum)&&minimum<maximum&&std::abs(minimum)<=2*pi&&std::abs(maximum)<=2*pi&&maximum-minimum<=pi+1e-6,"Native hinge midpoint interval unsupported");
    const double center=(minimum+maximum)*.5;const auto result=Add(Mul(reference,std::cos(center)),Mul(Cross(axis,reference),std::sin(center)));const auto length=std::sqrt(Dot(result,result));Need(std::isfinite(length)&&length>0,"Native hinge midpoint invalid");return Mul(result,1/length);
}
bool SourceRagdollNativeAnchorRetargetAllowed(double residual){return std::isfinite(residual)&&residual>=0&&residual<=.5;}
}

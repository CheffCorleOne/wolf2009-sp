// SPDX-License-Identifier: GPL-3.0-or-later
// Small geometry adapter. GJK/EPA and conservative advancement remain Bullet's
// reviewed implementation; no dynamics world or source-data cooking is used.
#include "RoundedConvex.h"
#include "BulletCollision/CollisionShapes/btConvexShape.h"
#include "BulletCollision/NarrowPhaseCollision/btContinuousConvexCollision.h"
#include "BulletCollision/NarrowPhaseCollision/btGjkPairDetector.h"
#include "BulletCollision/NarrowPhaseCollision/btGjkEpaPenetrationDepthSolver.h"
#include "BulletCollision/NarrowPhaseCollision/btPointCollector.h"
#include "BulletCollision/NarrowPhaseCollision/btVoronoiSimplexSolver.h"
#include <algorithm>
#include <cmath>
#include <limits>

#if defined(__FAST_MATH__)
#error RoundedConvex interval certificates require ordinary IEEE arithmetic, without fast-math.
#endif

namespace wolf2009 {
namespace {
static_assert(sizeof(btScalar)==sizeof(double),"RoundedConvex requires one double-precision Bullet ABI");
static_assert(std::numeric_limits<double>::is_iec559,"RoundedConvex requires IEEE double arithmetic");
using Status=RoundedConvexStatus;
using Reason=RoundedConvexReason;
const double infinity=std::numeric_limits<double>::infinity();
bool Finite(double x){return std::isfinite(x);}
bool Finite(const CollisionVec3 &p){return Finite(p.x)&&Finite(p.y)&&Finite(p.z);}
bool Finite(const btVector3 &p){return Finite(p.x())&&Finite(p.y())&&Finite(p.z());}
double Dot(CollisionVec3 a,CollisionVec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
CollisionVec3 Cross(CollisionVec3 a,CollisionVec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
btVector3 V(CollisionVec3 p){return btVector3(p.x,p.y,p.z);}
CollisionVec3 V(const btVector3 &p){return {p.x(),p.y(),p.z()};}
double Component(CollisionVec3 p,int i){return i==0?p.x:i==1?p.y:p.z;}
double Down(double x){return std::nextafter(x,-infinity);}
double Up(double x){return std::nextafter(x,infinity);}
struct Interval {double lo,hi;Interval(double x=0):lo(x),hi(x){}Interval(double a,double b):lo(a),hi(b){}};
Interval Add(Interval a,Interval b){return {Down(a.lo+b.lo),Up(a.hi+b.hi)};}
Interval Sub(Interval a,Interval b){return {Down(a.lo-b.hi),Up(a.hi-b.lo)};}
Interval Mul(Interval a,Interval b){const double p[]={a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
    return {Down(*std::min_element(p,p+4)),Up(*std::max_element(p,p+4))};}
Interval Norm(const btVector3 &p){Interval sum;
    for(int k=0;k<3;++k)sum=Add(sum,Mul(Interval(p[k]),Interval(p[k])));
    return {Down(std::sqrt(std::max(0.,sum.lo))),Up(std::sqrt(std::max(0.,sum.hi)))};}
Interval Dot(const btVector3 &a,const btVector3 &b){Interval r;
    for(int k=0;k<3;++k)r=Add(r,Mul(Interval(a[k]),Interval(b[k])));return r;}
Interval WorldCoordinate(const btTransform &t,CollisionVec3 p,int row){Interval r(t.getOrigin()[row]);
    for(int k=0;k<3;++k)r=Add(r,Mul(Interval(t.getBasis()[row][k]),Interval(Component(p,k))));return r;}
bool LimitsValid(const RoundedConvexLimits &l){return l.maxVertices>0&&l.maxVertices<=1000000&&
    Finite(l.coordinateGameUnits)&&l.coordinateGameUnits>0&&l.coordinateGameUnits<=1e9&&
    Finite(l.maximumRadiusGameUnits)&&l.maximumRadiusGameUnits>=0&&l.maximumRadiusGameUnits<=l.coordinateGameUnits&&
    Finite(l.distanceToleranceGameUnits)&&l.distanceToleranceGameUnits>=1e-12&&l.distanceToleranceGameUnits<=.01&&
    l.maxCastIterations>=1&&l.maxCastIterations<=64&&Finite(l.targetSeparationGameUnits)&&
    l.targetSeparationGameUnits>=0&&l.targetSeparationGameUnits<=l.maximumRadiusGameUnits;}
bool ShapeValid(const RoundedConvexShape &s,const RoundedConvexLimits &l){
    if(s.corePoints.empty()||s.corePoints.size()>l.maxVertices||!Finite(s.radius)||s.radius<0||s.radius>l.maximumRadiusGameUnits)return false;
    for(const auto &p:s.corePoints)if(!Finite(p)||std::abs(p.x)>l.coordinateGameUnits||
        std::abs(p.y)>l.coordinateGameUnits||std::abs(p.z)>l.coordinateGameUnits)return false;
    return true;
}
bool Pose(const CollisionRigidTransform &p,const RoundedConvexLimits &l,btTransform &out){
    if(!Finite(p.translation)||std::abs(p.translation.x)>l.coordinateGameUnits||
       std::abs(p.translation.y)>l.coordinateGameUnits||std::abs(p.translation.z)>l.coordinateGameUnits)return false;
    for(int i=0;i<3;++i){if(!Finite(p.axis[i]))return false;
        for(int j=0;j<3;++j)if(std::abs(Dot(p.axis[i],p.axis[j])-(i==j?1.:0.))>2e-5)return false;}
    if(std::abs(Dot(p.axis[0],Cross(p.axis[1],p.axis[2]))-1)>2e-5)return false;
    const btMatrix3x3 matrix(p.axis[0].x,p.axis[1].x,p.axis[2].x,
        p.axis[0].y,p.axis[1].y,p.axis[2].y,p.axis[0].z,p.axis[1].z,p.axis[2].z);
    btQuaternion q;matrix.getRotation(q);
    if(!Finite(q.x())||!Finite(q.y())||!Finite(q.z())||!Finite(q.w())||q.length2()<.99)return false;
    q.normalize();out=btTransform(q,V(p.translation));return true;
}
bool WorldValid(const RoundedConvexShape &s,const btTransform &t,const RoundedConvexLimits &l,double padding=0){
    if(s.radius>l.coordinateGameUnits-padding)return false;
    const double r=s.radius+padding;
    for(const auto &p:s.corePoints){const btVector3 world=t(V(p));if(!Finite(world))return false;
        for(int k=0;k<3;++k)if(std::abs(world[k])>l.coordinateGameUnits-r)return false;}
    return true;
}
bool SameRotation(const btTransform &a,const btTransform &b){
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(a.getBasis()[i][j]!=b.getBasis()[i][j])return false;return true;}
double RadiusBound(const RoundedConvexShape &s,double padding=0){double maximum=0;
    for(const auto &p:s.corePoints)maximum=std::max(maximum,Norm(V(p)).hi);
    // In Bullet's scalar setRotation, every entry has <20u absolute forward
    // error relative to its exact quaternion rotation; both row/column error
    // sums are <60u. gamma(64*epsilon) is larger (epsilon=2u). This bounds the
    // matrix operator norm over ALL normalized-quaternion orientations. Shape
    // radii themselves are unchanged. All bound arithmetic rounds outwards.
    const double eps=std::numeric_limits<double>::epsilon(),gamma=64*eps/(1-64*eps);
    const auto core=Mul(Interval(maximum),Interval(Up(1+gamma)));
    return Add(core,Add(Interval(s.radius),Interval(padding))).hi;
}
bool MotionAdmit(const RoundedConvexShape &s,const btTransform &from,const btTransform &to,
                 const RoundedConvexLimits &l,double padding,Reason &reason){
    reason=Reason::InvalidShape;
    if(s.radius>l.coordinateGameUnits-padding||!WorldValid(s,from,l,padding)||!WorldValid(s,to,l,padding))return false;
    if(!SameRotation(from,to)){
        reason=Reason::InvalidTransform;const double radius=RadiusBound(s,padding);
        for(int k=0;k<3;++k)if(Add(Interval(std::max(std::abs(from.getOrigin()[k]),std::abs(to.getOrigin()[k]))),
            Interval(radius)).hi>l.coordinateGameUnits)return false;
    }
    return true;
}
RoundedConvexBoundsResult PoseBounds(const RoundedConvexShape &shape,const btTransform &t,double padding=0){
    RoundedConvexBoundsResult r;
    double lo[3]={infinity,infinity,infinity},hi[3]={-infinity,-infinity,-infinity};
    const auto radial=Add(Interval(shape.radius),Interval(padding));
    for(const auto &p:shape.corePoints)for(int k=0;k<3;++k){const auto v=WorldCoordinate(t,p,k);
        lo[k]=std::min(lo[k],Sub(v,radial).lo);hi[k]=std::max(hi[k],Add(v,radial).hi);}
    r.minimum={lo[0],lo[1],lo[2]};r.maximum={hi[0],hi[1],hi[2]};
    r.valid=Finite(r.minimum)&&Finite(r.maximum);r.reason=r.valid?Reason::None:Reason::InvalidTransform;return r;
}
class BorrowedShape : public btConvexShape {
    const RoundedConvexShape &source;btScalar margin;btVector3 scale,shift;
public:
    BorrowedShape(const RoundedConvexShape &s,double padding=0,btVector3 sweep=btVector3(0,0,0))
        :source(s),margin(s.radius+padding),scale(1,1,1),shift(sweep){m_shapeType=CUSTOM_CONVEX_SHAPE_TYPE;}
    btVector3 localGetSupportingVertexWithoutMargin(const btVector3 &d)const override{
        btVector3 best=V(source.corePoints[0]);btScalar maximum=best.dot(d);
        for(std::size_t i=1;i<source.corePoints.size();++i){const btVector3 p=V(source.corePoints[i]);const auto value=p.dot(d);
            if(value>maximum){maximum=value;best=p;}}
        if(shift.dot(d)>0)best+=shift;return best;
    }
    btVector3 localGetSupportingVertex(const btVector3 &d)const override{
        btVector3 n=d;if(n.length2()<=SIMD_EPSILON*SIMD_EPSILON)n=btVector3(1,0,0);
        return localGetSupportingVertexWithoutMargin(d)+margin*n.normalized();
    }
    void batchedUnitVectorGetSupportingVertexWithoutMargin(const btVector3 *d,btVector3 *p,int n)const override{
        for(int i=0;i<n;++i)p[i]=localGetSupportingVertexWithoutMargin(d[i]);}
    void getAabb(const btTransform &t,btVector3 &lo,btVector3 &hi)const override{
        for(int k=0;k<3;++k){btVector3 d(0,0,0);d[k]=1;
            hi[k]=t(localGetSupportingVertex(d*t.getBasis()))[k];lo[k]=t(localGetSupportingVertex(-d*t.getBasis()))[k];}}
    void getAabbSlow(const btTransform &t,btVector3 &lo,btVector3 &hi)const override{getAabb(t,lo,hi);}
    void setLocalScaling(const btVector3 &)override{} // immutable, never invoked by these queries
    const btVector3 &getLocalScaling()const override{return scale;}
    void setMargin(btScalar)override{} // source radius is immutable
    btScalar getMargin()const override{return margin;}
    int getNumPreferredPenetrationDirections()const override{return 0;}
    void getPreferredPenetrationDirection(int,btVector3 &n)const override{n.setZero();}
    void calculateLocalInertia(btScalar,btVector3 &inertia)const override{inertia.setZero();}
    const char *getName()const override{return "Wolf2009BorrowedCorePlusRadius";}
};
struct Projections {Interval minimum,maximum;};
Projections Project(const RoundedConvexShape &s,const btTransform &t,const btVector3 &n,double padding=0){
    double minLo=infinity,minHi=infinity,maxLo=-infinity,maxHi=-infinity;
    for(const auto &p:s.corePoints){Interval value;
        for(int k=0;k<3;++k)value=Add(value,Mul(WorldCoordinate(t,p,k),Interval(n[k])));
        minLo=std::min(minLo,value.lo);minHi=std::min(minHi,value.hi);
        maxLo=std::max(maxLo,value.lo);maxHi=std::max(maxHi,value.hi);}
    const auto radial=Mul(Add(Interval(s.radius),Interval(padding)),Norm(n));
    return {Sub(Interval(minLo,minHi),radial),Add(Interval(maxLo,maxHi),radial)};
}
Interval Gap(const RoundedConvexShape &a,const btTransform &ta,const RoundedConvexShape &b,const btTransform &tb,
             const btVector3 &n,double padding=0){return Sub(Project(a,ta,n,padding).minimum,Project(b,tb,n).maximum);}
RoundedConvexResult Invalid(Reason reason){RoundedConvexResult r;r.reason=reason;return r;}
bool WitnessesMatch(const Projections &pa,const Projections &pb,const btVector3 &normal,
                    const RoundedConvexResult &r,double tolerance){
    const auto qa=Dot(V(r.pointOnA),normal),qb=Dot(V(r.pointOnB),normal);
    return !(qa.hi<pa.minimum.lo-tolerance||qa.lo>pa.minimum.hi+tolerance||
             qb.hi<pb.maximum.lo-tolerance||qb.lo>pb.maximum.hi+tolerance);
}
RoundedConvexResult RefineWitnessNormal(const RoundedConvexShape&,const btTransform&,
    const RoundedConvexShape&,const btTransform&,const RoundedConvexLimits&,double,
    const RoundedConvexResult&);
RoundedConvexResult Closest(const RoundedConvexShape &a,const btTransform &ta,const RoundedConvexShape &b,const btTransform &tb,
                            const RoundedConvexLimits &l,double padding=0,const btVector3 &shift=btVector3(0,0,0),bool verifyWitness=true,
                            bool allowUnwitnessedSeparation=false){
    const BorrowedShape sa(a,padding,shift),sb(b);btVoronoiSimplexSolver simplex;btGjkEpaPenetrationDepthSolver epa;
    btGjkPairDetector detector(&sa,&sb,&simplex,&epa);btGjkPairDetector::ClosestPointInput input;
    input.m_transformA=ta;input.m_transformB=tb;btPointCollector contact;detector.getClosestPoints(input,contact,0);
    RoundedConvexResult r;r.status=Status::Indeterminate;r.reason=Reason::NoClosestPoints;
    if(detector.m_curIter>=0)r.gjkIterations=static_cast<std::size_t>(detector.m_curIter);
    if(!contact.m_hasResult||!Finite(contact.m_distance)||!Finite(contact.m_pointInWorld))return r;
    r.signedDistance=contact.m_distance;r.hasDistance=true;
    btVector3 n=contact.m_normalOnBInWorld;const double length2=n.length2();
    if(!Finite(n)||!Finite(length2)||std::abs(length2-1)>.0001){r.reason=Reason::TangentOrTouching;return r;}
    n.normalize();r.normalBtoA=V(n);r.hasNormal=true;r.pointOnB=V(contact.m_pointInWorld);
    r.pointOnA=V(contact.m_pointInWorld+r.signedDistance*n);r.hasWitnesses=Finite(r.pointOnA);
    if(!r.hasWitnesses)return r;
    if(verifyWitness){
        const auto pa=Project(a,ta,n,padding),pb=Project(b,tb,n);
        const double tol=l.distanceToleranceGameUnits;
        const auto gap=Sub(pa.minimum,pb.maximum);
        if(!WitnessesMatch(pa,pb,n,r,tol)){
            r.hasWitnesses=false;r.reason=Reason::WitnessMismatch;
            // GJK's approximate closest points can fail qualification while
            // its suggested axis independently proves physical separation.
            // Distance/Overlap need only that outward-rounded support plane;
            // do not advertise the rejected distance/witnesses as contacts.
            // Cast never uses this plane-only fallback: its physical and
            // inflated queries both require qualified closest witnesses.
            if(allowUnwitnessedSeparation&&r.signedDistance>tol&&gap.lo>tol){
                r.hasDistance=false;r.status=Status::Separated;r.reason=Reason::SeparationCertificate;
                r.separationLowerBoundGameUnits=gap.lo;return r;}
            return shift.length2()==0?RefineWitnessNormal(a,ta,b,tb,l,padding,r):r;}
        if(r.signedDistance>tol){
            if(gap.lo<=tol){r.reason=Reason::UncertifiedSeparation;return r;}
            r.status=Status::Separated;r.reason=Reason::SeparationCertificate;r.separationLowerBoundGameUnits=gap.lo;return r;}
    }
    r.reason=Reason::None;
    if(r.signedDistance< -l.distanceToleranceGameUnits)r.status=Status::InitialOverlap;
    else if(r.signedDistance<=l.distanceToleranceGameUnits)r.status=Status::Hit;
    else r.status=Status::Separated; // internal axis suggestion only when verifyWitness=false
    return r;
}
RoundedConvexResult RefineWitnessNormal(const RoundedConvexShape &a,const btTransform &ta,
    const RoundedConvexShape &b,const btTransform &tb,const RoundedConvexLimits &limits,double padding,
    const RoundedConvexResult &input){
    if(input.status!=Status::Indeterminate||input.reason!=Reason::WitnessMismatch||
       !input.hasNormal||!input.hasDistance||
       !Finite(input.pointOnA)||!Finite(input.pointOnB))return input;
    const btVector3 original=V(input.normalBtoA);
    // These are only candidate axes, never an assumption about source faces.
    // An unqualified GJK normal is not a reference certificate, so its error
    // amplified by a long support edge cannot bound a valid candidate. Instead
    // qualify each candidate directly against the unchanged shapes/points and
    // original tolerance, including an independent full vector-relation bound.
    const btTransform* frames[]={&tb,&ta};
    for(const auto* frame:frames)for(int axis=0;axis<3;++axis){
        btVector3 local=original*frame->getBasis();if(local[axis]==0)continue;local[axis]=0;
        btVector3 normal=frame->getBasis()*local;const double length2=normal.length2();
        if(!Finite(normal)||!Finite(length2)||length2<=SIMD_EPSILON*SIMD_EPSILON)continue;
        normal.normalize();
        auto candidate=input;candidate.normalBtoA=V(normal);
        const auto pa=Project(a,ta,normal,padding),pb=Project(b,tb,normal);
        if(!WitnessesMatch(pa,pb,normal,candidate,limits.distanceToleranceGameUnits))continue;
        // Keep BOTH original physical points. Their vector relation to the
        // refined normal must also pass an independent outward-rounded bound.
        Interval residualSquared;
        for(int k=0;k<3;++k){const auto residual=Sub(Sub(Interval(Component(candidate.pointOnA,k)),
            Interval(Component(candidate.pointOnB,k))),Mul(Interval(candidate.signedDistance),Interval(normal[k])));
            residualSquared=Add(residualSquared,Mul(residual,residual));}
        const double residual=Up(std::sqrt(std::max(0.,residualSquared.hi)));
        if(!Finite(residual)||residual>limits.distanceToleranceGameUnits)continue;
        const auto gap=Sub(pa.minimum,pb.maximum);
        if(candidate.signedDistance>limits.distanceToleranceGameUnits){
            if(gap.lo<=limits.distanceToleranceGameUnits)continue;
            candidate.status=Status::Separated;candidate.reason=Reason::SeparationCertificate;
            candidate.separationLowerBoundGameUnits=gap.lo;
        }else{
            candidate.status=candidate.signedDistance< -limits.distanceToleranceGameUnits?Status::InitialOverlap:Status::Hit;
            candidate.reason=Reason::None;candidate.separationLowerBoundGameUnits=0;
        }
        candidate.hasWitnesses=true;candidate.normalRefined=true;
        return candidate;
    }
    return input;
}
bool Admit(const RoundedConvexShape &a,const CollisionRigidTransform &pa,const RoundedConvexShape &b,
           const CollisionRigidTransform &pb,const RoundedConvexLimits &l,btTransform &ta,btTransform &tb,Reason &reason){
    reason=Reason::InvalidLimits;if(!LimitsValid(l))return false;
    reason=Reason::InvalidShape;if(!ShapeValid(a,l)||!ShapeValid(b,l))return false;
    reason=Reason::InvalidTransform;if(!Pose(pa,l,ta)||!Pose(pb,l,tb)||!WorldValid(a,ta,l)||!WorldValid(b,tb,l))return false;
    return true;
}
btTransform Interpolate(const btTransform &a,const btTransform &b,double fraction){
    btTransform t;btContinuousConvexCollision::interpolateLinearSlerp(a,b,fraction,t);return t;}
double WholeMotionGap(const RoundedConvexShape &a,const btTransform &a0,const btTransform &a1,
                      const RoundedConvexShape &b,const btTransform &b0,const btTransform &b1,
                      const btVector3 &axis,double padding){
    if(!Finite(axis)||axis.length2()<.9)return -infinity;
    const auto start=Gap(a,a0,b,b0,axis,padding);
    if(SameRotation(a0,a1)&&SameRotation(b0,b1))return std::min(start.lo,Gap(a,a1,b,b1,axis,padding).lo);
    // A shortest quaternion arc is at most pi. Each rotating support point's
    // motion is bounded by arc length times its local radius. RadiusBound's
    // gamma term covers matrix rounding, too: pi*gamma > 2*gamma for the two
    // endpoint errors. Nonrotating shapes contribute zero.
    Interval angular;
    if(!SameRotation(a0,a1))angular=Add(angular,Mul(Interval(Up(SIMD_PI)),Interval(RadiusBound(a,padding))));
    if(!SameRotation(b0,b1))angular=Add(angular,Mul(Interval(Up(SIMD_PI)),Interval(RadiusBound(b))));
    angular=Mul(angular,Norm(axis));
    Interval delta;
    for(int k=0;k<3;++k){const auto relative=Sub(Sub(Interval(a1.getOrigin()[k]),Interval(a0.getOrigin()[k])),
            Sub(Interval(b1.getOrigin()[k]),Interval(b0.getOrigin()[k])));
        delta=Add(delta,Mul(relative,Interval(axis[k])));}
    const double linearClosing=std::max(0.,-delta.lo);
    return Sub(start,Add(Interval(linearClosing),angular)).lo;
}
bool InvariantCoordinate(const btTransform &from,const btTransform &to,int coordinate){
    // Match Interpolate's recovered quaternion profile. A product of two
    // quaternions supported only on {this axis,w} retains those exact zeros.
    // Thus setRotation's corresponding row is (0,0,1), up to permutation,
    // for every interior phase: no sin/cos derivative or accuracy bound is used.
    btQuaternion q0=from.getRotation(),q1=to.getRotation();q0.normalize();q1.normalize();
    for(int i=0;i<4;++i)if(!Finite(q0[i])||!Finite(q1[i]))return false;
    for(int i=0;i<3;++i)if(i!=coordinate&&(q0[i]!=0||q1[i]!=0))return false;
    return true;
}
bool MotionCoordinateRange(const RoundedConvexShape &shape,const btTransform &from,const btTransform &to,
                           int coordinate,double padding,Interval &range){
    btVector3 axis(0,0,0);axis[coordinate]=1;
    const bool fixed=SameRotation(from,to);
    if(!fixed&&!InvariantCoordinate(from,to,coordinate))return false;
    Interval core;
    if(fixed){
        btTransform local=from;local.setOrigin(btVector3(0,0,0));
        const auto p=Project(shape,local,axis,padding);core={p.minimum.lo,p.maximum.hi};
    }else{
        double lo=infinity,hi=-infinity;
        for(const auto &p:shape.corePoints){const double value=Component(p,coordinate);
            lo=std::min(lo,value);hi=std::max(hi,value);}
        const auto radius=Add(Interval(shape.radius),Interval(padding));
        core={Sub(Interval(lo),radius).lo,Add(Interval(hi),radius).hi};
    }
    // Enclose the ACTUAL scalar lerp arithmetic, including its rounded
    // subtraction, multiplication and addition for all represented t in[0,1].
    // Independent body ranges may lose shared-translation correlation; that
    // costs a certificate rather than introducing an unproved error allowance.
    const Interval origin=Add(Interval(from.getOrigin()[coordinate]),
        Mul(Sub(Interval(to.getOrigin()[coordinate]),Interval(from.getOrigin()[coordinate])),Interval(0,1)));
    range=Add(core,origin);
    // Interpolate returns the original endpoint matrices at exactly0/1.
    // Enclose these independently even if quaternion recovery rounded away a
    // tiny off-axis component. No endpoint/interior identity is assumed.
    const auto first=Project(shape,from,axis,padding),last=Project(shape,to,axis,padding);
    range.lo=std::min(range.lo,std::min(first.minimum.lo,last.minimum.lo));
    range.hi=std::max(range.hi,std::max(first.maximum.hi,last.maximum.hi));
    return Finite(range.lo)&&Finite(range.hi);
}
double AlignedMotionGap(const RoundedConvexShape &a,const btTransform &a0,const btTransform &a1,
                        const RoundedConvexShape &b,const btTransform &b0,const btTransform &b1,double padding){
    if(SameRotation(a0,a1)&&SameRotation(b0,b1))return -infinity;
    double best=-infinity;
    for(int coordinate=0;coordinate<3;++coordinate){
        Interval ar,br;
        if(!MotionCoordinateRange(a,a0,a1,coordinate,padding,ar)||
           !MotionCoordinateRange(b,b0,b1,coordinate,0,br))continue;
        // Independent +coordinate and -coordinate separating planes. A GJK
        // suggestion need not be exactly axial for this algebraic proof.
        best=std::max(best,std::max(Sub(Interval(ar.lo),Interval(br.hi)).lo,
                                  Sub(Interval(br.lo),Interval(ar.hi)).lo));
    }
    return best;
}
Reason CastReason(btContinuousConvexCollision::DetailedStatus s){using C=btContinuousConvexCollision;
    switch(s){case C::DetailedIterationLimit:return Reason::IterationLimit;case C::DetailedNoProgress:return Reason::NoProgress;
    case C::DetailedOutsideInterval:return Reason::OutsideInterval;case C::DetailedNotApproaching:return Reason::NonApproachingNormal;
    case C::DetailedNoMotion:return Reason::TangentOrTouching;case C::DetailedInvalidOptions:return Reason::InvalidLimits;
    default:return Reason::NoClosestPoints;}}
} // namespace

bool CanonicalRoundedConvexPose(const CollisionRigidTransform &in,CollisionRigidTransform &out,
    const RoundedConvexLimits &limits){
    btTransform canonical;if(!LimitsValid(limits)||!Pose(in,limits,canonical))return false;
    CollisionRigidTransform converted;converted.translation=V(canonical.getOrigin());
    for(int i=0;i<3;++i)converted.axis[i]={canonical.getBasis()[0][i],canonical.getBasis()[1][i],canonical.getBasis()[2][i]};
    out=converted;return true;
}
RoundedConvexResult DistanceRoundedConvex(const RoundedConvexShape &a,const CollisionRigidTransform &poseA,
    const RoundedConvexShape &b,const CollisionRigidTransform &poseB,const RoundedConvexLimits &limits){
    btTransform ta,tb;Reason reason;if(!Admit(a,poseA,b,poseB,limits,ta,tb,reason))return Invalid(reason);
    return Closest(a,ta,b,tb,limits,0,btVector3(0,0,0),true,true);
}
RoundedConvexResult OverlapRoundedConvex(const RoundedConvexShape &a,const CollisionRigidTransform &poseA,
    const RoundedConvexShape &b,const CollisionRigidTransform &poseB,const RoundedConvexLimits &limits){
    return DistanceRoundedConvex(a,poseA,b,poseB,limits);
}
RoundedConvexBoundsResult RoundedConvexBounds(const RoundedConvexShape &shape,const CollisionRigidTransform &pose,
    const RoundedConvexLimits &limits){
    RoundedConvexBoundsResult r;r.reason=Reason::InvalidLimits;if(!LimitsValid(limits))return r;
    r.reason=Reason::InvalidShape;if(!ShapeValid(shape,limits))return r;
    btTransform t;r.reason=Reason::InvalidTransform;if(!Pose(pose,limits,t)||!WorldValid(shape,t,limits))return r;
    return PoseBounds(shape,t);
}
RoundedConvexBoundsResult RoundedConvexMotionBounds(const RoundedConvexShape &shape,
    const CollisionRigidTransform &from,const CollisionRigidTransform &to,const RoundedConvexLimits &limits){
    RoundedConvexBoundsResult r;r.reason=Reason::InvalidLimits;if(!LimitsValid(limits))return r;
    r.reason=Reason::InvalidShape;if(!ShapeValid(shape,limits))return r;
    btTransform t0,t1;r.reason=Reason::InvalidTransform;if(!Pose(from,limits,t0)||!Pose(to,limits,t1))return r;
    if(!MotionAdmit(shape,t0,t1,limits,limits.targetSeparationGameUnits,r.reason))return r;
    if(SameRotation(t0,t1)){
        const auto a=PoseBounds(shape,t0,limits.targetSeparationGameUnits),b=PoseBounds(shape,t1,limits.targetSeparationGameUnits);
        r.minimum={std::min(a.minimum.x,b.minimum.x),std::min(a.minimum.y,b.minimum.y),std::min(a.minimum.z,b.minimum.z)};
        r.maximum={std::max(a.maximum.x,b.maximum.x),std::max(a.maximum.y,b.maximum.y),std::max(a.maximum.z,b.maximum.z)};
    }else{
        const double radius=RadiusBound(shape,limits.targetSeparationGameUnits);double lo[3],hi[3];
        for(int k=0;k<3;++k){lo[k]=Sub(Interval(std::min(t0.getOrigin()[k],t1.getOrigin()[k])),Interval(radius)).lo;
            hi[k]=Add(Interval(std::max(t0.getOrigin()[k],t1.getOrigin()[k])),Interval(radius)).hi;}
        r.minimum={lo[0],lo[1],lo[2]};r.maximum={hi[0],hi[1],hi[2]};
    }
    r.valid=Finite(r.minimum)&&Finite(r.maximum);r.reason=r.valid?Reason::None:Reason::InvalidTransform;return r;
}
RoundedConvexResult CastRoundedConvex(const RoundedConvexShape &a,const CollisionRigidTransform &fromA,
    const CollisionRigidTransform &toA,const RoundedConvexShape &b,const CollisionRigidTransform &fromB,
    const CollisionRigidTransform &toB,const RoundedConvexLimits &l){
    btTransform a0,b0,a1,b1;Reason reason;
    if(!Admit(a,fromA,b,fromB,l,a0,b0,reason)||!Admit(a,toA,b,toB,l,a1,b1,reason))return Invalid(reason);
    const double skin=l.targetSeparationGameUnits;
    if(!MotionAdmit(a,a0,a1,l,skin,reason)||!MotionAdmit(b,b0,b1,l,0,reason))return Invalid(reason);
    auto initial=Closest(a,a0,b,b0,l);
    initial.separationLowerBoundGameUnits=0; // A one-pose certificate is not a cast certificate.
    if(initial.status==Status::Indeterminate)return initial;
    if(initial.status==Status::InitialOverlap){initial.fraction=0;initial.hasFraction=true;return initial;}
    auto queryInitial=skin?Closest(a,a0,b,b0,l,skin):initial;
    if(queryInitial.status==Status::Indeterminate){initial.status=Status::Indeterminate;
        initial.reason=queryInitial.reason;return initial;}
    if(queryInitial.status==Status::InitialOverlap){initial.status=Status::InitialOverlap;initial.reason=Reason::WithinTargetSeparation;
        initial.fraction=0;initial.hasFraction=true;return initial;}
    const auto certified=[&](double lower)->RoundedConvexResult{
        RoundedConvexResult out;out.status=Status::Indeterminate;out.reason=Reason::UncertifiedSeparation;
        if(lower>l.distanceToleranceGameUnits){out=Closest(a,a1,b,b1,l);out.status=Status::Separated;
            out.reason=Reason::SeparationCertificate;out.separationLowerBoundGameUnits=lower;out.fraction=1;out.hasFraction=true;}
        return out;};
    const auto separated=[&](const btVector3 &axis)->RoundedConvexResult{
        return certified(WholeMotionGap(a,a0,a1,b,b0,b1,axis,skin));};
    if(queryInitial.hasNormal){auto cert=separated(V(queryInitial.normalBtoA));if(cert.status==Status::Separated)return cert;}
    if(SameRotation(a0,a1)&&SameRotation(b0,b1)){
        // The convex sweep under fixed orientations is an exact endpoint
        // support hull in relative coordinates. GJK only suggests an axis;
        // our independently outward-rounded endpoint projections certify it.
        const btVector3 relative=(a1.getOrigin()-a0.getOrigin())-(b1.getOrigin()-b0.getOrigin());
        const btVector3 local=relative*a0.getBasis();
        const auto suggestion=Closest(a,a0,b,b0,l,skin,local,false);
        if(suggestion.hasNormal){auto cert=separated(V(suggestion.normalBtoA));if(cert.status==Status::Separated)return cert;}}
    const auto aligned=certified(AlignedMotionGap(a,a0,a1,b,b0,b1,skin));
    if(aligned.status==Status::Separated)return aligned;
    BorrowedShape sa(a,skin),sb(b);btVoronoiSimplexSolver simplex;btGjkEpaPenetrationDepthSolver epa;
    btContinuousConvexCollision caster(&sa,&sb,&simplex,&epa);btConvexCast::CastResult cast;
    cast.m_fraction=1;cast.m_allowedPenetration=0;btContinuousConvexCollision::Diagnostics diagnostics;
    const auto castStatus=caster.calcTimeOfImpactWithStatus(a0,a1,b0,b1,cast,l.distanceToleranceGameUnits,
        static_cast<int>(l.maxCastIterations),diagnostics);
    RoundedConvexResult out;out.status=Status::Indeterminate;out.reason=CastReason(castStatus);
    if(diagnostics.iterations>=0)out.castIterations=static_cast<std::size_t>(diagnostics.iterations);
    if(castStatus!=btContinuousConvexCollision::DetailedHit)return out;
    if(!Finite(cast.m_fraction)||cast.m_fraction<0||cast.m_fraction>1||!Finite(cast.m_normal))return out;
    const auto ta=Interpolate(a0,a1,cast.m_fraction),tb=Interpolate(b0,b1,cast.m_fraction);
    out=Closest(a,ta,b,tb,l);
    out.castIterations=static_cast<std::size_t>(diagnostics.iterations);
    out.separationLowerBoundGameUnits=0;
    out.fraction=cast.m_fraction;out.hasFraction=true;
    if(!out.hasNormal||!out.hasWitnesses||!out.hasDistance||out.status==Status::Indeterminate){out.status=Status::Indeterminate;return out;}
    if(std::abs(out.signedDistance-skin)>2*l.distanceToleranceGameUnits){out.status=Status::Indeterminate;out.reason=Reason::WitnessMismatch;return out;}
    btVector3 raw=cast.m_normal;const double n2=raw.length2();
    if(!Finite(n2)||std::abs(n2-1)>.0001){out.status=Status::Indeterminate;out.reason=Reason::ConflictingNormal;return out;}raw.normalize();
    const btVector3 n=V(out.normalBtoA);
    const double rounding=a.radius+b.radius+skin;
    const double uncertainty=rounding>0?std::min(1.,std::sqrt(2*l.distanceToleranceGameUnits/rounding)):0;
    if(raw.dot(n)<1-std::max(1e-8,uncertainty*uncertainty*2)){out.status=Status::Indeterminate;out.reason=Reason::ConflictingNormal;return out;}
    btVector3 linearA,angularA,linearB,angularB;
    btTransform unused;
    btContinuousConvexCollision::interpolateLinearSlerp(a0,a1,0,unused,&linearA,&angularA);
    btContinuousConvexCollision::interpolateLinearSlerp(b0,b1,0,unused,&linearB,&angularB);
    const btVector3 velocityA=linearA+angularA.cross(V(out.pointOnA)-ta.getOrigin());
    const btVector3 velocityB=linearB+angularB.cross(V(out.pointOnB)-tb.getOrigin());
    const double closing= -n.dot(velocityA-velocityB);
    const double speed=(linearA-linearB).length()+angularA.length()*RadiusBound(a,skin)+angularB.length()*RadiusBound(b);
    if(!Finite(closing)||closing<=0){out.status=Status::Indeterminate;out.reason=Reason::NonApproachingNormal;return out;}
    if(closing<=speed*uncertainty+64*std::numeric_limits<double>::epsilon()*std::max(1.,speed)){
        out.status=Status::Indeterminate;out.reason=Reason::TangentOrTouching;return out;}
    out.status=Status::Hit;out.reason=Reason::None;return out;
}
const char *RoundedConvexStatusName(RoundedConvexStatus s){switch(s){case Status::Hit:return "Hit";case Status::Separated:return "Separated";
    case Status::InitialOverlap:return "InitialOverlap";case Status::Indeterminate:return "Indeterminate";case Status::Invalid:return "Invalid";}return "Invalid";}
const char *RoundedConvexReasonName(RoundedConvexReason r){switch(r){
#define WOLF_REASON(x) case Reason::x:return #x
    WOLF_REASON(None);WOLF_REASON(InvalidShape);WOLF_REASON(InvalidTransform);WOLF_REASON(InvalidLimits);WOLF_REASON(NoClosestPoints);
    WOLF_REASON(IterationLimit);WOLF_REASON(NoProgress);WOLF_REASON(OutsideInterval);WOLF_REASON(NonApproachingNormal);WOLF_REASON(TangentOrTouching);
    WOLF_REASON(UncertifiedSeparation);WOLF_REASON(ConflictingNormal);WOLF_REASON(WitnessMismatch);WOLF_REASON(WithinTargetSeparation);WOLF_REASON(SeparationCertificate);
#undef WOLF_REASON
    }return "InvalidReason";}
} // namespace wolf2009

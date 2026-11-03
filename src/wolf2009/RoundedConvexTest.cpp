// SPDX-License-Identifier: GPL-3.0-or-later
// Analytic fixtures authored for this project. No original game data is embedded.
#include "RoundedConvex.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace wolf2009;
using Status=RoundedConvexStatus;
using Reason=RoundedConvexReason;
const double pi=3.141592653589793238462643383279502884;
int checks=0,hits=0,distances=0;
void Check(bool condition,const char *message){if(!condition)throw std::runtime_error(message);++checks;}
CollisionVec3 Add(CollisionVec3 a,CollisionVec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CollisionVec3 Sub(CollisionVec3 a,CollisionVec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CollisionVec3 Scale(CollisionVec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double Dot(CollisionVec3 a,CollisionVec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
double Length(CollisionVec3 a){return std::sqrt(Dot(a,a));}
CollisionVec3 Unit(CollisionVec3 a){return Scale(a,1/Length(a));}
CollisionRigidTransform Pose(CollisionVec3 origin={0,0,0},double angle=0){CollisionRigidTransform p;p.translation=origin;
    p.axis[0]={std::cos(angle),std::sin(angle),0};p.axis[1]={-std::sin(angle),std::cos(angle),0};return p;}
CollisionVec3 Transform(const CollisionRigidTransform &p,CollisionVec3 v){return Add(p.translation,
    Add(Add(Scale(p.axis[0],v.x),Scale(p.axis[1],v.y)),Scale(p.axis[2],v.z)));}
RoundedConvexShape Box(double x,double y,double z,double radius=0){RoundedConvexShape shape;shape.radius=radius;
    for(int i=0;i<8;++i)shape.corePoints.push_back({i&1?x:-x,i&2?y:-y,i&4?z:-z});return shape;}
RoundedConvexShape Point(double radius=0){RoundedConvexShape shape;shape.corePoints.push_back({0,0,0});shape.radius=radius;return shape;}
void Print(const char *name,const RoundedConvexResult &r){std::cout<<name<<" status="<<RoundedConvexStatusName(r.status)
    <<" reason="<<RoundedConvexReasonName(r.reason)<<" fraction="<<r.fraction<<" distance="<<r.signedDistance
    <<" normal="<<r.normalBtoA.x<<','<<r.normalBtoA.y<<','<<r.normalBtoA.z
    <<" cast_iterations="<<r.castIterations<<" lower="<<r.separationLowerBoundGameUnits<<'\n';}
void DistanceCase(const char *name,const RoundedConvexShape &a,CollisionVec3 origin,const RoundedConvexShape &b,
                  double expected,CollisionVec3 normal){
    const auto r=DistanceRoundedConvex(a,Pose(origin),b,Pose());Print(name,r);
    Check(r.status==(expected<0?Status::InitialOverlap:Status::Separated),"Distance status");
    Check(r.hasDistance&&r.hasNormal&&r.hasWitnesses,"Distance valid fields");
    Check(std::abs(r.signedDistance-expected)<1e-6,"Analytic distance");
    Check(Dot(r.normalBtoA,normal)>.99999,"Analytic distance normal");
    if(expected>0)Check(r.separationLowerBoundGameUnits>0,"Distance separating support certificate");
    ++distances;
}
RoundedConvexResult HitCase(const char *name,const RoundedConvexShape &a,const CollisionRigidTransform &fromA,
    const CollisionRigidTransform &toA,const RoundedConvexShape &b,const CollisionRigidTransform &fromB,
    const CollisionRigidTransform &toB,double fraction,CollisionVec3 normal,double motionBound,
    RoundedConvexLimits limits=RoundedConvexLimits(),double motionError=5e-6){
    const auto r=CastRoundedConvex(a,fromA,toA,b,fromB,toB,limits);Print(name,r);
    Check(r.status==Status::Hit,"Analytic cast did not produce qualified Hit");
    Check(r.hasFraction&&r.hasDistance&&r.hasNormal&&r.hasWitnesses,"Hit valid fields");
    Check(std::abs(r.fraction-fraction)*motionBound<motionError,"Analytic TOI motion error");
    Check(std::abs(r.signedDistance-limits.targetSeparationGameUnits)<=2*limits.distanceToleranceGameUnits,"Physical distance at TOI");
    Check(r.separationLowerBoundGameUnits==0,"A hit cannot carry a one-pose cast-clearance certificate");
    Check(Dot(r.normalBtoA,normal)>.9999,"Analytic TOI normal");++hits;return r;
}
void AxisHit(const char *name,const RoundedConvexShape &a,const RoundedConvexShape &b,CollisionVec3 from,
    CollisionVec3 to,double fraction,CollisionVec3 normal,RoundedConvexLimits limits=RoundedConvexLimits()){
    HitCase(name,a,Pose(from),Pose(to),b,Pose(),Pose(),fraction,normal,Length(Sub(to,from)),limits);
}
void Analytic(){
    const auto point=Point(),sphere=Point(.25),cube=Box(1,1,1,.5),sharp=Box(1,1,1),box=Box(.25,.25,.25);
    DistanceCase("distance-face",point,{3,0,0},cube,1.5,{1,0,0});
    DistanceCase("distance-edge",point,{2,2,0},cube,std::sqrt(2.)-.5,Unit({1,1,0}));
    DistanceCase("distance-corner",point,{2,2,2},cube,std::sqrt(3.)-.5,Unit({1,1,1}));
    DistanceCase("distance-actor-box",box,{3,0,0},cube,1.25,{1,0,0});
    DistanceCase("distance-source-radii-once",sphere,{3,0,0},cube,1.25,{1,0,0});
    AxisHit("sharp-face",point,sharp,{4,0,0},{0,0,0},.75,{1,0,0});
    AxisHit("rounded-face",point,cube,{4,0,0},{0,0,0},.625,{1,0,0});
    AxisHit("rounded-edge",point,cube,{4,1.3,0},{0,1.3,0},.65,{.8,.6,0});
    const double x=std::sqrt(.12);
    AxisHit("rounded-corner",point,cube,{4,1.2,1.3},{0,1.2,1.3},(3-x)/4,{x/.5,.4,.6});
    AxisHit("rounded-box",box,cube,{4,0,0},{0,0,0},.5625,{1,0,0});
    AxisHit("rounded-sphere",sphere,cube,{4,0,0},{0,0,0},.5625,{1,0,0});
    DistanceCase("overlap-box",box,{.5,.1,.2},cube,-1.25,{1,0,0});
    const auto overlap=CastRoundedConvex(box,Pose({.5,.1,.2}),Pose({4,0,0}),cube,Pose(),Pose());Print("initial-overlap",overlap);
    Check(overlap.status==Status::InitialOverlap&&overlap.hasFraction&&overlap.fraction==0,"Cast start overlap must report fraction zero");
    RoundedConvexShape triangle;triangle.corePoints={{-2,-2,0},{2,-2,0},{0,2,0}};triangle.radius=.5;
    auto flat=triangle;flat.radius=0;
    AxisHit("flat-triangle-face",box,flat,{0,0,3},{0,0,-1},.6875,{0,0,1});
    AxisHit("rounded-triangle-front",box,triangle,{0,0,3},{0,0,-1},.5625,{0,0,1});
    AxisHit("rounded-triangle-back",box,triangle,{0,0,-3},{0,0,1},.5625,{0,0,-1});
    AxisHit("rounded-triangle-edge",box,triangle,{0,-2.55,3},{0,-2.55,-1},.5875,{0,-.6,.8});
    AxisHit("rounded-triangle-corner",box,triangle,{2.45,-2.55,3},{2.45,-2.55,-1},(2.75-x)/4,{.4,-.6,x/.5});
    DistanceCase("overlap-triangle-box",box,{0,0,.1},triangle,-.65,{0,0,1});
    DistanceCase("overlap-triangle-point",point,{0,0,.1},triangle,-.4,{0,0,1});
    RoundedConvexShape segment;segment.corePoints={{-2,0,0},{2,0,0},{2,0,0}};segment.radius=.5;
    AxisHit("duplicate-triangle-segment-side",box,segment,{0,3,0},{0,-1,0},.5625,{0,1,0});
    AxisHit("duplicate-triangle-segment-cap",box,segment,{5,0,0},{1,0,0},.5625,{1,0,0});
    AxisHit("duplicate-triangle-segment-edge",box,segment,{0,.55,3},{0,.55,-1},.5875,{0,.6,.8});
    AxisHit("duplicate-triangle-segment-corner",box,segment,{2.45,.55,3},{2.45,.55,-1},(2.75-x)/4,{.4,.6,x/.5});
    DistanceCase("overlap-segment",box,{0,.1,0},segment,-.65,{0,1,0});
    const auto moved=Pose({1000,-2000,3000},pi/3);
    const auto normal=Sub(Transform(moved,{.8,.6,0}),moved.translation);
    HitCase("rigid-invariance",point,Pose(Transform(moved,{4,1.3,0}),pi/3),Pose(Transform(moved,{0,1.3,0}),pi/3),
        cube,moved,moved,.65,normal,4);
    const double theta=pi/4-std::acos(1.2/std::sqrt(2.));
    const double rotationalMotion=std::sqrt(3.)*.25*pi/4;
    HitCase("rotation-only-box",box,Pose({1.8,0,0}),Pose({1.8,0,0},pi/4),cube,Pose(),Pose(),
        theta/(pi/4),{1,0,0},rotationalMotion,RoundedConvexLimits(),1e-5);
    // An offset point sweeps a circle. Contact occurs after 45 degrees, where
    // upstream integrateTransform's angular clamp is a different trajectory.
    RoundedConvexShape orbit=Point(.1);orbit.corePoints[0]={2,0,0};const auto target=Point(.1);
    for(const double endAngle:{pi/2,pi-1e-7,-pi+1e-7}){
        const double targetAngle=endAngle*.85,hitAngle=targetAngle-std::copysign(2*std::asin(.2/4),targetAngle);
        const CollisionVec3 targetPoint={2*std::cos(targetAngle),2*std::sin(targetAngle),0};
        const CollisionVec3 actorPoint={2*std::cos(hitAngle),2*std::sin(hitAngle),0};
        HitCase(endAngle<2?"rotation-hit-after-45":"rotation-near-pi",orbit,Pose(),Pose({0,0,0},endAngle),target,
            Pose(targetPoint),Pose(targetPoint),hitAngle/endAngle,Unit(Sub(actorPoint,targetPoint)),2*std::abs(endAngle),
            RoundedConvexLimits(),3e-5);
    }
    // Caller explicitly rebases local points around a pivot. No hidden center
    // chord or source-point rewrite is performed by the query implementation.
    const CollisionVec3 pivot={3,3,0};const double targetAngle=2*pi/3,hitAngle=targetAngle-2*std::asin(.2/4);
    const auto targetPoint=Add(pivot,{2*std::cos(targetAngle),2*std::sin(targetAngle),0});
    HitCase("arbitrary-pivot-rebased",orbit,Pose(pivot),Pose(pivot,pi),target,Pose(targetPoint),Pose(targetPoint),
        hitAngle/pi,Unit(Sub(Add(pivot,{2*std::cos(hitAngle),2*std::sin(hitAngle),0}),targetPoint)),2*pi,
        RoundedConvexLimits(),3e-5);
    auto exactPi=Pose();exactPi.axis[0]={-1,0,0};exactPi.axis[1]={0,-1,0};
    const CollisionVec3 exactTarget={2*std::cos(targetAngle),2*std::sin(targetAngle),0};
    HitCase("exact-pi-sign-tie",orbit,Pose(),exactPi,target,Pose(exactTarget),Pose(exactTarget),hitAngle/pi,
        Unit(Sub({2*std::cos(hitAngle),2*std::sin(hitAngle),0},exactTarget)),2*pi,RoundedConvexLimits(),3e-5);
    RoundedConvexShape largeOrbit=Point(1e-5);largeOrbit.corePoints[0]={1e6,0,0};const auto smallTarget=Point(1e-5);
    const double tinyEnd=1e-8,tinyTarget=tinyEnd*.85,tinyHit=tinyTarget-2*std::asin(2e-5/2e6);
    const CollisionVec3 tinyTargetPosition={1e6*std::cos(tinyTarget),1e6*std::sin(tinyTarget),0};
    HitCase("tiny-angle-large-core",largeOrbit,Pose(),Pose({0,0,0},tinyEnd),smallTarget,Pose(tinyTargetPosition),Pose(tinyTargetPosition),
        tinyHit/tinyEnd,Unit(Sub({1e6*std::cos(tinyHit),1e6*std::sin(tinyHit),0},tinyTargetPosition)),.01);
    Check(orbit.corePoints[0].x==2&&orbit.radius==.1,"Queries mutated source core/radius");
}
void ClearanceAndStatus(){
    const auto point=Point(),sphere=Point(.25),cube=Box(1,1,1,.5),box=Box(.25,.25,.25);RoundedConvexLimits limits;
    limits.targetSeparationGameUnits=.25;
    const auto r=HitCase("clearance-face",point,Pose({4,0,0}),Pose(),cube,Pose(),Pose(),.5625,{1,0,0},4,limits);
    Check(std::abs(r.pointOnB.x-1.5)<1e-6,"Cast witness must be on physical B, without clearance");
    AxisHit("clearance-box",box,cube,{4,0,0},{0,0,0},.5,{1,0,0},limits);
    AxisHit("clearance-summed-radius",sphere,cube,{4,0,0},{0,0,0},.5,{1,0,0},limits);
    const double x=std::sqrt(.75*.75-.3*.3);
    AxisHit("clearance-oblique-edge",point,cube,{4,1.3,0},{0,1.3,0},(3-x)/4,{x/.75,.3/.75,0},limits);
    const auto within=CastRoundedConvex(point,Pose({1.6,0,0}),Pose({4,0,0}),cube,Pose(),Pose(),limits);Print("initial-clearance",within);
    Check(within.status==Status::InitialOverlap&&within.reason==Reason::WithinTargetSeparation&&within.hasFraction&&within.fraction==0,
        "Already violated clearance status");
    Check(within.hasDistance&&std::abs(within.signedDistance-.1)<1e-6,"Clearance changed physical distance");
    Check(within.separationLowerBoundGameUnits==0,"Initial clearance overlap cannot advertise a motion certificate");
    Check(cube.radius==.5&&point.radius==0&&sphere.radius==.25,"Query clearance changed source radius");
    for(const CollisionVec3 to: {CollisionVec3{4,0,0},CollisionVec3{5,0,0}}){
        const auto clear=CastRoundedConvex(point,Pose({4,0,0}),Pose(to),cube,Pose(),Pose());Print("certified-no-contact",clear);
        Check(clear.status==Status::Separated&&clear.reason==Reason::SeparationCertificate&&clear.separationLowerBoundGameUnits>0,
            "Stationary/away motion requires own full-motion support certificate");}
    const auto pass=CastRoundedConvex(point,Pose({4,2,0}),Pose({-4,2,0}),cube,Pose(),Pose());Print("certified-side-pass",pass);
    Check(pass.status==Status::Separated&&pass.separationLowerBoundGameUnits>.49,"Swept support axis certificate");
    const auto rotateFar=CastRoundedConvex(box,Pose({10,0,0}),Pose({10,0,0},pi/2),cube,Pose(),Pose());Print("certified-rotation",rotateFar);
    Check(rotateFar.status==Status::Separated&&rotateFar.separationLowerBoundGameUnits>0,"Full rotation motion certificate");
    const auto tangent=CastRoundedConvex(point,Pose({4,1.5,0}),Pose({-4,1.5,0}),cube,Pose(),Pose());Print("tangent-indeterminate",tangent);
    Check(tangent.status==Status::Indeterminate,"Tangent cannot be claimed clear or qualified approaching hit");
    RoundedConvexLimits budget;budget.maxCastIterations=1;
    const auto failure=CastRoundedConvex(point,Pose({4,1.2,1.3}),Pose({0,1.2,1.3}),cube,Pose(),Pose(),budget);Print("iteration-budget",failure);
    Check(failure.status==Status::Indeterminate&&failure.reason==Reason::IterationLimit&&failure.castIterations==1,
        "Strict iteration failure must remain Indeterminate with its reason");
    // A sharp contact's normal is not unique. A result may be admitted only if
    // it retains a valid incoming physical normal; no forced axis repair.
    RoundedConvexShape tetra;tetra.corePoints={{0,0,0},{-2,0,0},{-.5,-2,0},{-.5,0,-2}};
    const auto sharp=CastRoundedConvex(point,Pose({4,0,0}),Pose({-1,0,0}),tetra,Pose(),Pose());Print("sharp-vertex-policy",sharp);
    Check(sharp.status==Status::Indeterminate||(sharp.status==Status::Hit&&sharp.hasNormal&&sharp.normalBtoA.x>0),
        "Sharp vertex failure cannot be clear or a nonapproaching normal");
}
void AlignedMotionCertificates(){
    RoundedConvexShape orbit=Point();orbit.corePoints[0]={-80,0,0};
    const auto floor=Box(256,256,7,2);const auto floorPose=Pose({0,0,-9});
    const CollisionVec3 pivot={160,-80,48};RoundedConvexLimits limits;limits.targetSeparationGameUnits=.25;
    // Pose uses mathematical CCW; native positive90 about Z is this -pi/2.
    // The broad motion sphere includes the floor. A pi*radius proof cannot
    // qualify it, while every interpolated orbit point retains exact local Z.
    for(const double angle:{0.,1e-12,-pi/2,pi,-pi}){
        const auto clear=CastRoundedConvex(orbit,Pose(pivot),Pose(pivot,angle),floor,floorPose,floorPose,limits);
        Print("aligned-orbit-floor",clear);
        Check(clear.status==Status::Separated&&clear.reason==Reason::SeparationCertificate&&
            clear.separationLowerBoundGameUnits>47.74,"Invariant-coordinate orbit/floor certificate");
    }
    const auto reversed=CastRoundedConvex(floor,floorPose,floorPose,orbit,Pose(pivot),Pose(pivot,-pi/2),limits);
    Check(reversed.status==Status::Separated&&reversed.separationLowerBoundGameUnits>47.74,
        "Negative-coordinate separating plane missing");
    const auto both=CastRoundedConvex(orbit,Pose(pivot),Pose(pivot,-pi/2),floor,floorPose,Pose({0,0,-9},pi/3),limits);
    Check(both.status==Status::Separated&&both.separationLowerBoundGameUnits>47.74,
        "Both rotating shapes must satisfy the invariant projection proof");
    const auto linear=CastRoundedConvex(orbit,Pose({160,-80,48}),Pose({160,-80,40},-pi/2),
        floor,Pose({0,0,-9}),Pose({0,0,-7},pi/3),limits);
    Check(linear.status==Status::Separated&&linear.separationLowerBoundGameUnits>37.74,
        "Invariant proof did not enclose both origin lerps");
    const auto target=Point(32);const auto targetPose=Pose({160,0,48});
    const double angle=pi/2-2*std::asin((32+.25)/160.);
    const auto position=Add(pivot,{-80*std::cos(angle),80*std::sin(angle),0});
    const auto normal=Unit(Sub(position,targetPose.translation));
    HitCase("aligned-orbit-sphere",orbit,Pose(pivot),Pose(pivot,-pi/2),target,targetPose,targetPose,
        angle/(pi/2),normal,80*pi/2,limits,5e-5);
    Check(DistanceRoundedConvex(orbit,Pose(pivot),target,targetPose).status==Status::Separated&&
        DistanceRoundedConvex(orbit,Pose(pivot,-pi),target,targetPose).status==Status::Separated,
        "Interior-hit control must have separated endpoints");
    HitCase("aligned-endpoints-clear-interior-hit",orbit,Pose(pivot),Pose(pivot,-pi),target,targetPose,targetPose,
        angle/pi,normal,80*pi,limits,5e-5);
    // A noncoordinate rotation axis is deliberately outside this certificate.
    // The point stays well above the floor, but an unsupported derivative/
    // transcendental error assumption must not promote the failed cast to clear.
    const auto k=Unit({.1,0,1});const double c=std::cos(pi/2),s=std::sin(pi/2);
    auto tilted=Pose(pivot);
    for(int i=0;i<3;++i){CollisionVec3 e;e.x=i==0;e.y=i==1;e.z=i==2;
        const CollisionVec3 cross={k.y*e.z-k.z*e.y,k.z*e.x-k.x*e.z,k.x*e.y-k.y*e.x};
        tilted.axis[i]=Add(Add(Scale(e,c),Scale(cross,s)),Scale(k,Dot(k,e)*(1-c)));}
    const auto unknown=CastRoundedConvex(orbit,Pose(pivot),tilted,floor,floorPose,floorPose,limits);
    Print("nonaligned-orbit-no-certificate",unknown);
    Check(unknown.status==Status::Indeterminate&&unknown.separationLowerBoundGameUnits==0,
        "Nonaligned rotation acquired an unproved coordinate certificate");
    const auto crossing=CastRoundedConvex(orbit,Pose({160,-80,48}),Pose({160,-80,-48},-pi/2),
        floor,floorPose,floorPose,limits);
    Check(crossing.status!=Status::Separated&&crossing.separationLowerBoundGameUnits==0,
        "Origin interval crossing the floor was falsely certified clear");
}
void BoundsAndAdmission(){
    const auto cube=Box(1,1,1,.5),point=Point();const auto bounds=RoundedConvexBounds(cube,Pose());
    Check(bounds.valid&&bounds.minimum.x<=-1.5&&bounds.minimum.y<=-1.5&&bounds.minimum.z<=-1.5&&
        bounds.maximum.x>=1.5&&bounds.maximum.y>=1.5&&bounds.maximum.z>=1.5,"Physical rounded bounds");
    Check(bounds.minimum.x>-1.500000000001&&bounds.maximum.x<1.500000000001,"No guessed bounds inflation");
    const auto box=Box(2,1,.5,.3);const auto rotated=RoundedConvexBounds(box,Pose({10,20,30},pi/4));
    const double extent=3/std::sqrt(2.)+.3;
    Check(rotated.valid&&rotated.minimum.x<=10-extent&&rotated.maximum.x>=10+extent&&
        rotated.minimum.y<=20-extent&&rotated.maximum.y>=20+extent&&rotated.minimum.z<=29.2&&rotated.maximum.z>=30.8,
        "Canonical rotated rounded bounds");
    auto nearRigid=Pose();nearRigid.axis[0].x+=2e-6;
    const auto canonical=RoundedConvexBounds(cube,nearRigid);
    Check(canonical.valid&&canonical.maximum.x<1.500000000001,"Bounds must use normalized query pose rather than raw basis");
    for(int axis=0;axis<3;++axis){CollisionVec3 outside={0,0,0};
        if(axis==0)outside.x=canonical.maximum.x+.1;else if(axis==1)outside.y=canonical.maximum.y+.1;else outside.z=canonical.maximum.z+.1;
        const auto q=DistanceRoundedConvex(point,Pose(outside),cube,nearRigid);
        Check(q.status==Status::Separated&&q.separationLowerBoundGameUnits>.09,"Bounds/query canonical pose mismatch");}
    RoundedConvexLimits skin;skin.targetSeparationGameUnits=.25;
    const auto unchanged=RoundedConvexBounds(cube,Pose(),skin);Check(unchanged.maximum.x==bounds.maximum.x,"Physical bounds include query clearance");
    const auto invalid=[&](const RoundedConvexShape &a,const CollisionRigidTransform &pose,RoundedConvexLimits limits=RoundedConvexLimits()){
        Check(DistanceRoundedConvex(a,pose,cube,Pose(),limits).status==Status::Invalid,"Invalid distance admitted");
        Check(CastRoundedConvex(a,pose,pose,cube,Pose(),Pose(),limits).status==Status::Invalid,"Invalid cast admitted");
        Check(!RoundedConvexBounds(a,pose,limits).valid,"Invalid bounds admitted");};
    auto bad=point;bad.corePoints.clear();invalid(bad,Pose());bad=point;bad.radius=-1;invalid(bad,Pose());
    bad=point;bad.radius=std::numeric_limits<double>::infinity();invalid(bad,Pose());
    bad=point;bad.corePoints[0].x=std::numeric_limits<double>::quiet_NaN();invalid(bad,Pose());
    bad=point;bad.corePoints.push_back({1,0,0});RoundedConvexLimits one;one.maxVertices=1;invalid(bad,Pose(),one);
    auto p=Pose();p.axis[0].x=2;invalid(point,p);p=Pose();p.axis[0].x=-1;invalid(point,p);
    p=Pose();p.translation.x=1e8;invalid(point,p);p=Pose();p.axis[1].z=std::numeric_limits<double>::infinity();invalid(point,p);
    for(int kind=0;kind<8;++kind){RoundedConvexLimits l;
        if(kind==0)l.maxCastIterations=0;if(kind==1)l.maxCastIterations=65;if(kind==2)l.distanceToleranceGameUnits=0;
        if(kind==3)l.targetSeparationGameUnits=-1;if(kind==4)l.targetSeparationGameUnits=1001;
        if(kind==5)l.coordinateGameUnits=std::numeric_limits<double>::infinity();if(kind==6)l.maxVertices=0;
        if(kind==7)l.distanceToleranceGameUnits=std::numeric_limits<double>::quiet_NaN();invalid(point,Pose(),l);}
    Check(std::string(RoundedConvexStatusName(Status::Indeterminate))=="Indeterminate","Status text");
    const auto coincident=DistanceRoundedConvex(point,Pose(),point,Pose());Print("zero-radius-coincident",coincident);
    Check(coincident.status!=Status::Separated,"Coincident points cannot certify separation");
    const auto samePoint=CastRoundedConvex(point,Pose(),Pose({1,0,0}),point,Pose(),Pose());Print("coincident-point-cast",samePoint);
    Check(samePoint.status!=Status::Separated,"Initially coincident point cast cannot be clear");
    auto offset=point;offset.corePoints[0]={8,0,0};RoundedConvexLimits tight;tight.coordinateGameUnits=10;tight.maximumRadiusGameUnits=1;
    const auto from=Pose({5,0,0},-pi/2),to=Pose({5,0,0},pi/2);
    Check(RoundedConvexBounds(offset,from,tight).valid&&RoundedConvexBounds(offset,to,tight).valid,"Motion fixture endpoints must be admitted");
    const auto invalidMotion=RoundedConvexMotionBounds(offset,from,to,tight);
    Check(!invalidMotion.valid&&invalidMotion.reason==Reason::InvalidTransform,"Intermediate coordinate overflow must reject motion bounds");
    Check(CastRoundedConvex(offset,from,to,point,Pose(),Pose(),tight).status==Status::Invalid,"Pair and motion-bounds admission must agree");
    RoundedConvexLimits padded;padded.targetSeparationGameUnits=.25;
    const auto translation=RoundedConvexMotionBounds(cube,Pose(),Pose({3,-5,2}),padded);
    Check(translation.valid&&translation.minimum.x<=-1.75&&translation.maximum.x>=4.75&&translation.minimum.y<=-6.75&&
        translation.maximum.y>=1.75&&translation.minimum.z<=-1.75&&translation.maximum.z>=3.75,"Outward full translation bounds include clearance once");
    auto orbit=Point(.1);orbit.corePoints[0]={2,0,0};
    const auto rotation=RoundedConvexMotionBounds(orbit,Pose(),Pose({0,0,0},pi));
    Check(rotation.valid&&rotation.minimum.x<=-2.1&&rotation.maximum.x>=2.1&&rotation.minimum.y<=-2.1&&rotation.maximum.y>=2.1,
        "Rotation enclosure must include the complete orbit, not endpoint union");
    const auto floor=Box(5,5,1,.5),actor=Box(.25,.25,.25);
    const auto floorTangency=CastRoundedConvex(actor,Pose({-1,0,2}),Pose({1,0,2}),floor,Pose(),Pose(),padded);Print("query-skin-floor-tangent",floorTangency);
    Check(floorTangency.status==Status::Indeterminate,"Uncertified query-skin tangent cannot be advertised clear");
    const auto physicalClear=CastRoundedConvex(actor,Pose({-1,0,2}),Pose({1,0,2}),floor,Pose(),Pose());Print("physical-floor-gap",physicalClear);
    Check(physicalClear.status==Status::Separated&&physicalClear.separationLowerBoundGameUnits>.249999,"Physical floor gap independently certifiable");
}
bool SamePose(const CollisionRigidTransform &a,const CollisionRigidTransform &b){
    if(a.translation.x!=b.translation.x||a.translation.y!=b.translation.y||a.translation.z!=b.translation.z)return false;
    for(int i=0;i<3;++i)if(a.axis[i].x!=b.axis[i].x||a.axis[i].y!=b.axis[i].y||a.axis[i].z!=b.axis[i].z)return false;
    return true;
}
void CanonicalPose(){
    auto input=Pose({3,4,5},pi/5);input.axis[0]=Scale(input.axis[0],1+2e-6);
    CollisionRigidTransform converted;Check(CanonicalRoundedConvexPose(input,converted),"Near-orthonormal public pose conversion");
    Check(!SamePose(input,converted)&&Length(Sub(input.translation,converted.translation))==0,"Canonical conversion must normalize basis and preserve origin");
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)Check(std::abs(Dot(converted.axis[i],converted.axis[j])-(i==j?1.:0.))<1e-14,
        "Canonical public basis must be orthonormal");
    auto alias=input;Check(CanonicalRoundedConvexPose(alias,alias)&&SamePose(alias,converted),"Valid pose input/output alias");
    auto shape=Point(.3);shape.corePoints[0]={1.3,-2.7,.4};const auto expected=Transform(converted,shape.corePoints[0]);
    const auto pair=DistanceRoundedConvex(Point(),Pose(Add(expected,{5,0,0})),shape,input);
    Check(pair.status==Status::Separated&&pair.hasDistance&&std::abs(pair.signedDistance-4.7)<1e-12,
        "Public pose conversion must match Distance exactly");
    const auto bounds=RoundedConvexBounds(shape,input);
    Check(bounds.valid&&bounds.minimum.x<=expected.x-.3&&bounds.maximum.x>=expected.x+.3&&
        bounds.minimum.y<=expected.y-.3&&bounds.maximum.y>=expected.y+.3&&bounds.minimum.z<=expected.z-.3&&bounds.maximum.z>=expected.z+.3,
        "Public pose conversion must match physical bounds");
    const auto sentinel=Pose({77,-81,3},pi/3);auto out=sentinel;auto reflected=Pose();reflected.axis[0].x=-1;
    Check(!CanonicalRoundedConvexPose(reflected,out)&&SamePose(out,sentinel),"Reflected pose must leave output unchanged");
    const auto reflectedBefore=reflected;
    Check(!CanonicalRoundedConvexPose(reflected,reflected)&&SamePose(reflected,reflectedBefore),"Invalid aliased pose must be unchanged");
    RoundedConvexLimits badLimits;badLimits.maxVertices=0;
    Check(!CanonicalRoundedConvexPose(input,out,badLimits)&&SamePose(out,sentinel),"Invalid pose limits must leave output unchanged");
    auto outside=input;outside.translation.x=1e8;
    Check(!CanonicalRoundedConvexPose(outside,out)&&SamePose(out,sentinel),"Out-of-domain pose must leave output unchanged");
}
void AnalyticSeries(){
    std::uint32_t state=0x734b190du;
    const auto random=[&](){state^=state<<13;state^=state>>17;state^=state<<5;return double(state)/4294967296.;};
    for(int i=0;i<128;++i){
        const auto a=Point(.1+random()),b=Point(.1+random());RoundedConvexLimits l;
        const double physical=a.radius+b.radius;l.targetSeparationGameUnits=i&1?physical*.1:0;
        const double radius=physical+l.targetSeparationGameUnits,y=radius*(random()-.5),z=radius*(random()-.5);
        const double x=std::sqrt(radius*radius-y*y-z*z),expected=(4*radius-x)/(6*radius);
        const CollisionVec3 base={200*random()-100,200*random()-100,200*random()-100};
        const CollisionVec3 drift={2*random()-1,2*random()-1,2*random()-1};
        const auto b0=Pose(base),b1=Pose(Add(base,drift));
        const auto hit=CastRoundedConvex(a,Pose(Add(base,{4*radius,y,z})),Pose(Add(Add(base,drift),{-2*radius,y,z})),b,b0,b1,l);
        if(hit.status!=Status::Hit)Print("analytic-sphere-series-hit-failure",hit);
        Check(hit.status==Status::Hit&&hit.hasFraction&&hit.hasNormal&&hit.hasWitnesses,"Analytic moving sphere hit");
        Check(std::abs(hit.fraction-expected)*6*radius<4e-6,"Analytic moving sphere TOI");
        Check(Dot(hit.normalBtoA,{x/radius,y/radius,z/radius})>.9999,"Analytic moving sphere normal");
        Check(std::abs(hit.signedDistance-l.targetSeparationGameUnits)<2e-6,"Analytic moving sphere physical distance");
        const auto clear=CastRoundedConvex(a,Pose(Add(base,{4*radius,1.2*radius,0})),
            Pose(Add(Add(base,drift),{-2*radius,1.2*radius,0})),b,b0,b1,l);
        if(clear.status!=Status::Separated)Print("analytic-sphere-series-clear-failure",clear);
        Check(clear.status==Status::Separated&&clear.separationLowerBoundGameUnits>0,"Full moving sphere side-pass certificate");
        const auto overlap=CastRoundedConvex(a,Pose(Add(base,{physical*.25,0,0})),Pose(Add(base,{5*radius,0,0})),b,b0,b1,l);
        Check(overlap.status==Status::InitialOverlap&&overlap.hasFraction&&overlap.fraction==0,"Analytic moving sphere start overlap");
    }
    for(int i=0;i<128;++i){
        const double radius=.01+.09*random(),length=2+8*random();auto a=Point(radius);a.corePoints[0]={length,0,0};const auto b=Point(radius);
        const double angle=(i&1?-1:1)*pi*(.4+.599999*random()),targetAngle=angle*(.6+.3*random());
        const double hitAngle=targetAngle-std::copysign(2*std::asin(2*radius/(2*length)),targetAngle);
        const CollisionVec3 target={length*std::cos(targetAngle),length*std::sin(targetAngle),0};
        const auto hit=CastRoundedConvex(a,Pose(),Pose({0,0,0},angle),b,Pose(target),Pose(target));
        if(hit.status!=Status::Hit)Print("analytic-orbit-series-failure",hit);
        Check(hit.status==Status::Hit&&hit.hasWitnesses&&hit.hasNormal,"Analytic orbit hit");
        Check(std::abs(hit.fraction-hitAngle/angle)*length*std::abs(angle)<5e-6,"Analytic orbit TOI");
    }
    std::cout<<"ANALYTIC_SERIES_OK sphere_hits=128 sphere_separations=128 sphere_initial_overlaps=128 orbit_hits=128\n";
}
void PrivatePoints(const char *path){
    std::ifstream in(path);Check(bool(in),"Cannot read owner-provided private point fixture");std::string name;const auto point=Point();
    while(in>>name){unsigned offset;std::size_t count;RoundedConvexShape shape;in>>offset>>shape.radius>>count;
        Check(in.good()&&count>=3&&count<=1024,"Private point fixture header");
        for(std::size_t i=0;i<count;++i){CollisionVec3 p;in>>p.x>>p.y>>p.z;Check(in.good(),"Private point fixture data");shape.corePoints.push_back(p);}
        auto support=shape.corePoints[0];for(const auto &p:shape.corePoints)if(p.x>support.x)support=p;
        const auto from=Add(support,{shape.radius+4,0,0}),to=Add(support,{-1,0,0});
        const auto r=CastRoundedConvex(point,Pose(from),Pose(to),shape,Pose(),Pose());Print(name.c_str(),r);
        if(shape.radius>0)Check(r.status==Status::Hit&&std::abs(r.fraction-4/(5+shape.radius))*(5+shape.radius)<1e-5,"Original rounded support witness");
        else Check(r.status==Status::Indeterminate,"Known original sharp-vertex ambiguous normal must stay Indeterminate");
    }
}
} // namespace
int main(int argc,char **argv){try{
    std::cout<<std::setprecision(15);Analytic();ClearanceAndStatus();AlignedMotionCertificates();BoundsAndAdmission();CanonicalPose();AnalyticSeries();
    if(argc==3&&std::string(argv[1])=="--private-points")PrivatePoints(argv[2]);
    else Check(argc==1,"Expected optional --private-points <owner supplied fixture>");
    std::cout<<"ROUNDED_CONVEX_OK checks="<<checks<<" analytic_hits="<<hits<<" distances="<<distances<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<"ROUNDED_CONVEX_FAIL "<<e.what()<<'\n';return 1;}}

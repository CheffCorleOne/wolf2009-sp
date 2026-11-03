// SPDX-License-Identifier: GPL-3.0-or-later
// Independent synthetic scenes; no original map or proprietary geometry.
#include "SupportCollisionIndex.h"
#include "MapPhysics.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace wolf2009;
using Status=SupportCollisionQueryStatus;
using PairStatus=RoundedConvexStatus;
const double pi=3.141592653589793238462643383279502884;
int checks=0,comparisons=0;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); ++checks; }
bool Near(double a,double b,double error=1e-6) { return std::abs(a-b)<=error; }
template<class F> void Reject(F f) {
    bool caught=false; try { f(); } catch (const std::exception&) { caught=true; }
    Check(caught,"Invalid/memory-excessive index accepted");
}
CollisionRigidTransform Pose(CollisionVec3 origin={0,0,0},double angle=0) {
    CollisionRigidTransform pose; pose.translation=origin;
    pose.axis[0]={std::cos(angle),std::sin(angle),0}; pose.axis[1]={-std::sin(angle),std::cos(angle),0}; return pose;
}
RoundedConvexShape Point(double radius=0) { RoundedConvexShape shape; shape.corePoints={{0,0,0}}; shape.radius=radius; return shape; }
RoundedConvexShape Box(double x,double y,double z,double radius=0) {
    RoundedConvexShape shape; shape.radius=radius;
    for (int b=0;b<8;++b) shape.corePoints.push_back({b&1?x:-x,b&2?y:-y,b&4?z:-z}); return shape;
}
SupportCollisionWorld World() {
    SupportCollisionWorld world; world.sourceBytes=1024; world.sourceFingerprint=123;
    world.selectedBodyName="maps/synthetic/index"; world.selectedGeometryComplete=world.selectedHierarchyComplete=true;
    world.otherSourceRigidBodies=4; world.allSourceRigidBodiesSelected=false;
    // Source contents/entity bindings remain unqualified. Tests supply an
    // explicit query policy, never convert raw values to gameplay filters.
    world.selectedContentsComplete=world.selectedCoverageComplete=false; return world;
}
void Add(SupportCollisionWorld& world,const RoundedConvexShape& physical,const CollisionRigidTransform& pose,
         SupportCollisionKind kind=SupportCollisionKind::ConvexVertices) {
    SupportCollisionShape shape; shape.kind=kind; shape.corePoints=physical.corePoints; shape.radius=physical.radius;
    shape.declaredCorePoints=physical.corePoints.size(); shape.placement=pose;
    shape.source.occurrence=world.shapes.size()+1; shape.source.sourceShapeOffset=100+16*world.shapes.size();
    shape.source.sourceRegion=1000+world.shapes.size();
    shape.filter.inheritedRawContents=0x80000003u; shape.filter.inheritedContentsKnown=true;
    // Intentionally irrelevant raw-reader cache. BVH must use the solver's
    // canonical pose rather than trust this mutable source AABB.
    shape.worldBounds.minimum={9000000,9000000,9000000}; shape.worldBounds.maximum={9000001,9000001,9000001};
    world.shapes.push_back(shape);
}
RoundedConvexShape Physical(const SupportCollisionShape& shape) {
    RoundedConvexShape result; result.corePoints=shape.corePoints; result.radius=shape.radius; return result;
}
class Policy : public SupportCollisionQueryFilter {
  public:
    SupportFilterDecision otherwise=SupportFilterDecision::Include;
    std::map<std::uint64_t,SupportFilterDecision> occurrences;
    mutable std::size_t calls=0;
    SupportFilterDecision Select(const SupportCollisionIdentity& source,const SupportCollisionFilter&) const override {
        ++calls; const auto found=occurrences.find(source.occurrence);
        return found==occurrences.end() ? otherwise : found->second;
    }
};
struct LinearResult {
    RoundedConvexResult candidate;
    std::size_t shape=static_cast<std::size_t>(-1),uncertain=0,unknown=0,tested=0;
    std::size_t physicalRetries=0,physicalFallbacks=0;
    bool found=false,invalid=false;
};
bool PhysicallyNonblocking(const SupportCollisionQueryLimits& limits,bool overlap,const RoundedConvexResult& strict,
    const RoundedConvexShape& actor,const CollisionRigidTransform& from,const CollisionRigidTransform& to,
    const RoundedConvexShape& obstacle,const CollisionRigidTransform& placed,LinearResult& reference) {
    if (overlap || limits.clearancePolicy!=SupportClearancePolicy::NonBlockingPhysical ||
        limits.geometry.targetSeparationGameUnits<=0) return false;
    const bool mayRetry=strict.status==PairStatus::Indeterminate ||
        (strict.status==PairStatus::InitialOverlap && strict.reason==RoundedConvexReason::WithinTargetSeparation);
    if (!mayRetry) return false;
    auto physical=limits.geometry; physical.targetSeparationGameUnits=0; ++reference.physicalRetries;
    const auto certificate=CastRoundedConvex(actor,from,to,obstacle,placed,placed,physical);
    if (certificate.status!=PairStatus::Separated || certificate.reason!=RoundedConvexReason::SeparationCertificate ||
        certificate.separationLowerBoundGameUnits<=0) return false;
    ++reference.physicalFallbacks; return true;
}
LinearResult Linear(const SupportCollisionWorld& world,const RoundedConvexShape& actor,
    const CollisionRigidTransform& from,const CollisionRigidTransform& to,const Policy& filter,
    const SupportCollisionQueryLimits& limits,bool overlap) {
    LinearResult out;
    for (std::size_t i=0;i<world.shapes.size();++i) {
        const auto& source=world.shapes[i]; const auto select=filter.Select(source.source,source.filter);
        if (select==SupportFilterDecision::Exclude) continue;
        if (select!=SupportFilterDecision::Include) { ++out.unknown; continue; }
        ++out.tested;
        const auto shape=Physical(source);
        const auto r=overlap ? OverlapRoundedConvex(actor,from,shape,source.placement,limits.geometry) :
            CastRoundedConvex(actor,from,to,shape,source.placement,source.placement,limits.geometry);
        if (PhysicallyNonblocking(limits,overlap,r,actor,from,to,shape,source.placement,out)) continue;
        if (r.status==PairStatus::Invalid || r.status==PairStatus::Indeterminate) {
            std::cerr << "LinearPairDiagnostic shape=" << i << " status=" << RoundedConvexStatusName(r.status)
                      << " reason=" << RoundedConvexReasonName(r.reason) << " from=" << from.translation.x << ','
                      << from.translation.y << ',' << from.translation.z << " target=" << source.placement.translation.x
                      << ',' << source.placement.translation.y << ',' << source.placement.translation.z << '\n';
            if (r.status==PairStatus::Invalid) out.invalid=true; else ++out.uncertain;
            continue;
        }
        if (r.status==PairStatus::Separated) continue;
        const double fraction=r.status==PairStatus::InitialOverlap ? 0 : r.fraction;
        const double previous=out.candidate.status==PairStatus::InitialOverlap ? 0 : out.candidate.fraction;
        if (!out.found || fraction<previous || (fraction==previous && i<out.shape)) {
            out.found=true; out.candidate=r; out.shape=i;
        }
    }
    return out;
}
SupportCollisionQueryResult Compare(const SupportCollisionWorld& world,const SupportCollisionIndex& index,
    const RoundedConvexShape& actor,const CollisionRigidTransform& from,const CollisionRigidTransform& to,
    const Policy& filter,const SupportCollisionQueryLimits& limits=SupportCollisionQueryLimits(),bool overlap=false) {
    const auto reference=Linear(world,actor,from,to,filter,limits,overlap);
    Check(!reference.invalid && !reference.uncertain && !reference.unknown,"Linear fixture lacks a definite pair reference");
    const auto actual=overlap ? index.Overlap(actor,from,filter,limits) : index.Cast(actor,from,to,filter,limits);
    Check(actual.hasCandidate==reference.found,"BVH lost or invented a linear pair candidate");
    Check(actual.physicalSeparationRetries==reference.physicalRetries &&
          actual.physicalSeparationFallbacks==reference.physicalFallbacks,"BVH/linear independent physical certificate counts disagree");
    if (reference.found) {
        Check(actual.shapeIndex==reference.shape && actual.source.occurrence==world.shapes[reference.shape].source.occurrence &&
              actual.source.sourceRegion==world.shapes[reference.shape].source.sourceRegion,"BVH selected the wrong nearest occurrence");
        Check(actual.candidate.status==reference.candidate.status && Near(actual.candidate.fraction,reference.candidate.fraction) &&
              Near(actual.candidate.signedDistance,reference.candidate.signedDistance),"BVH changed pair result/witness distance");
        Check(actual.candidate.hasWitnesses && actual.candidate.hasNormal,"Candidate lost physical witnesses");
        const auto& source=world.shapes[reference.shape];
        Check(actual.sourceKind==source.kind && actual.cylinder.profile==source.cylinder.profile &&
              actual.cylinder.matchesNativeSupportSelection==source.cylinder.matchesNativeSupportSelection,
              "BVH candidate lost its geometry interpretation metadata");
        const bool response=source.kind!=SupportCollisionKind::Cylinder &&
            (source.kind!=SupportCollisionKind::Triangle || source.triangle.contactMetadataQualified);
        Check(actual.responseMetadataQualified==response,"BVH changed source response qualification");
    }
    const bool complete=world.selectedGeometryComplete && world.selectedHierarchyComplete;
    const auto expected=!complete ? Status::IncompleteSource : !reference.found ? Status::Separated :
        reference.candidate.status==PairStatus::InitialOverlap ? Status::InitialOverlap : Status::Hit;
    Check(actual.status==expected && actual.selectedBodyQueryComplete==complete && !actual.sceneClearanceVerified,
          "BVH coverage/status generalized beyond the selected source body");
    ++comparisons; return actual;
}
void NearestAndFilters() {
    auto world=World(); const auto cube=Box(1,1,1,.25),actor=Point(.25);
    Add(world,cube,Pose()); Add(world,cube,Pose({5,0,0})); Add(world,cube,Pose({-5,0,0}));
    for (int i=0;i<40;++i) Add(world,cube,Pose({double(i),100+i*5.0,0}));
    SupportCollisionIndexLimits build; build.leafShapes=1; SupportCollisionIndex index(world,build); Policy all;
    const auto nearest=Compare(world,index,actor,Pose({10,0,0}),Pose({-10,0,0}),all);
    Check(nearest.shapeIndex==1 && Near(nearest.candidate.fraction,.175) && nearest.tested==3,
          "BVH failed nearest-of-multiple or did not cull far shapes");
    Check(nearest.responseMetadataQualified && !nearest.sceneClearanceVerified,"Known shape response metadata missing");
    Compare(world,index,actor,Pose({20,20,0}),Pose({30,20,0}),all);
    const auto overlap=Compare(world,index,Box(.25,.25,.25),Pose({.5,.1,.2}),Pose({.5,.1,.2}),all,
                               SupportCollisionQueryLimits(),true);
    Check(overlap.status==Status::InitialOverlap && overlap.shapeIndex==0,"Overlap selected wrong source shape");
    Policy exclude; exclude.occurrences[2]=SupportFilterDecision::Exclude;
    const auto next=Compare(world,index,actor,Pose({10,0,0}),Pose({-10,0,0}),exclude);
    Check(next.shapeIndex==0 && next.excluded==1,"Excluded nearest shape still selected");
    Policy none; none.otherwise=SupportFilterDecision::Exclude;
    const auto excluded=Compare(world,index,actor,Pose({10,0,0}),Pose({-10,0,0}),none);
    Check(excluded.tested==0 && excluded.excluded==3,"Explicit exclusion did not skip narrowphase");
    Policy unknown; unknown.occurrences[2]=SupportFilterDecision::Unknown;
    const auto partial=index.Cast(actor,Pose({10,0,0}),Pose({-10,0,0}),unknown);
    Check(partial.status==Status::Indeterminate && partial.hasCandidate && partial.shapeIndex==0 &&
          partial.unknownFilters==1 && !partial.selectedBodyQueryComplete && !partial.sceneClearanceVerified,
          "Unknown nearer filter discarded a known candidate or produced clear/nearest certainty");
    Policy farUnknown; farUnknown.occurrences[4]=SupportFilterDecision::Unknown;
    const auto culledUnknown=index.Cast(actor,Pose({10,0,0}),Pose({-10,0,0}),farUnknown);
    Check(culledUnknown.status==Status::Hit && culledUnknown.unknownFilters==0 && culledUnknown.shapeIndex==1,
          "Provably distant unknown filter made a local query incomplete");
    auto incomplete=world; incomplete.selectedGeometryComplete=false; SupportCollisionIndex partialIndex(incomplete,build);
    const auto hit=Compare(incomplete,partialIndex,actor,Pose({10,0,0}),Pose({-10,0,0}),all);
    Check(hit.status==Status::IncompleteSource && hit.hasCandidate,"Incomplete source lost its known hit candidate");
    const auto miss=Compare(incomplete,partialIndex,actor,Pose({20,20,0}),Pose({30,20,0}),all);
    Check(miss.status==Status::IncompleteSource && !miss.hasCandidate && !miss.selectedBodyQueryComplete,
          "Incomplete source miss became a clear path");
    incomplete=world; incomplete.selectedHierarchyComplete=false; SupportCollisionIndex hierarchy(incomplete,build);
    Check(hierarchy.Cast(actor,Pose({20,20,0}),Pose({30,20,0}),all).status==Status::IncompleteSource,
          "Unknown hierarchy ignored by source coverage");
    RoundedConvexShape triangleCore; triangleCore.corePoints={{0,-2,-2},{0,2,-2},{0,0,2}}; triangleCore.radius=.25;
    auto triangle=World(); Add(triangle,triangleCore,Pose(),SupportCollisionKind::Triangle);
    triangle.shapes[0].triangle.contactMetadataQualified=false; SupportCollisionIndex response(triangle);
    const auto unqualified=Compare(triangle,response,actor,Pose({4,0,0}),Pose(),all);
    Check(unqualified.hasCandidate && !unqualified.responseMetadataQualified && !unqualified.sceneClearanceVerified,
          "Geometry candidate silently certified welding/contact response");
    auto ties=World(); Add(ties,cube,Pose()); Add(ties,cube,Pose()); SupportCollisionIndex tieIndex(ties,build);
    Check(Compare(ties,tieIndex,actor,Pose({4,0,0}),Pose(),all).shapeIndex==0,"Equal hits do not preserve stable input occurrence order");
}
void CanonicalBoundsAndMotion() {
    Policy all; auto target=Point(.01); target.corePoints[0]={1000000,0,0};
    for (double error:{-4e-6,4e-6}) {
        auto world=World(); auto sourcePose=Pose(); sourcePose.axis[0].x+=error;
        Add(world,target,sourcePose);
        const double raw=1000000*(1+error);
        world.shapes[0].worldBounds.minimum={raw-.01,-.01,-.01}; world.shapes[0].worldBounds.maximum={raw+.01,.01,.01};
        SupportCollisionIndex index(world);
        const auto hit=Compare(world,index,Point(.01),Pose({1000001,0,0}),Pose({999999,0,0}),all);
        Check(hit.status==Status::Hit && hit.tested==1,"Near-orthogonal source basis culled the canonical shape");
        const auto far=Compare(world,index,Point(.01),Pose({raw,0,0}),Pose({raw,0,0}),all);
        Check(far.status==Status::Separated && far.tested==0,"BVH used raw scaled cache instead of canonical pose bounds");
    }
    auto sheared=Pose(); sheared.axis[0].y=4e-6;
    auto shearWorld=World(); Add(shearWorld,target,sheared);
    const auto canonical=RoundedConvexBounds(target,sheared);
    Check(canonical.valid,"Near-proper shear fixture rejected");
    const CollisionVec3 center={.5*canonical.minimum.x+.5*canonical.maximum.x,
                               .5*canonical.minimum.y+.5*canonical.maximum.y,0};
    SupportCollisionIndex shearIndex(shearWorld);
    Compare(shearWorld,shearIndex,Point(.01),Pose({center.x+1,center.y,0}),Pose({center.x-1,center.y,0}),all);
    auto orbit=Point(.1); orbit.corePoints[0]={2,0,0}; const auto smallTarget=Point(.1);
    for (double end:{pi-1e-7,-pi+1e-7}) {
        auto world=World(); const double angle=end*.85;
        Add(world,smallTarget,Pose({2*std::cos(angle),2*std::sin(angle),0}),SupportCollisionKind::Sphere);
        SupportCollisionIndex index(world);
        const auto hit=Compare(world,index,orbit,Pose(),Pose({0,0,0},end),all);
        Check(hit.status==Status::Hit && hit.tested==1,"Rotational swept bounds dropped a hit outside both endpoint AABBs");
    }
    const CollisionVec3 pivot={3,3,0}; auto pivotWorld=World(); const double angle=2*pi/3;
    Add(pivotWorld,smallTarget,Pose({pivot.x+2*std::cos(angle),pivot.y+2*std::sin(angle),0}),SupportCollisionKind::Sphere);
    SupportCollisionIndex pivotIndex(pivotWorld);
    Compare(pivotWorld,pivotIndex,orbit,Pose(pivot),Pose(pivot,pi),all);
    auto skinWorld=World(); Add(skinWorld,Point(1),Pose(),SupportCollisionKind::Sphere); SupportCollisionIndex skinIndex(skinWorld);
    const auto physical=Compare(skinWorld,skinIndex,Point(),Pose({4,1.25,0}),Pose({-4,1.25,0}),all);
    Check(physical.status==Status::Separated && physical.tested==0,"Physical no-skin pass not culled");
    SupportCollisionQueryLimits skin; skin.geometry.targetSeparationGameUnits=.5;
    const auto clearance=Compare(skinWorld,skinIndex,Point(),Pose({4,1.25,0}),Pose({-4,1.25,0}),all,skin);
    Check(clearance.status==Status::Hit && clearance.tested==1 && Near(clearance.candidate.signedDistance,.5),
          "Swept broadphase omitted query skin or changed physical witness distance");
    const auto within=Compare(skinWorld,skinIndex,Point(),Pose({1.25,0,0}),Pose({4,0,0}),all,skin);
    Check(within.status==Status::InitialOverlap && within.candidate.reason==RoundedConvexReason::WithinTargetSeparation,
          "Initial query clearance lost during broadphase");
    Check(skinWorld.shapes[0].radius==1 && orbit.corePoints[0].x==2 && orbit.radius==.1,
          "Index/query modified physical source radius or actor core");
}
void BudgetsLifetimeAndInvalid() {
    Policy all; auto world=World(); const auto cube=Box(1,1,1,.25),actor=Point(.25);
    Add(world,cube,Pose()); Add(world,cube,Pose({5,0,0})); SupportCollisionIndexLimits build; build.leafShapes=1;
    SupportCollisionIndex index(world,build); SupportCollisionQueryLimits cap; cap.candidates=1;
    const auto limited=index.Cast(actor,Pose({10,0,0}),Pose({-10,0,0}),all,cap);
    Check(limited.status==Status::Indeterminate && limited.tested==1 && limited.hasCandidate &&
          !limited.selectedBodyQueryComplete && !limited.sceneClearanceVerified,"Candidate cap discarded a hit or certified nearest/clearance");
    auto one=World(); Add(one,cube,Pose()); SupportCollisionIndex oneIndex(one);
    const auto exactCap=oneIndex.Cast(actor,Pose({4,0,0}),Pose(),all,cap);
    Check(exactCap.status==Status::Hit && exactCap.tested==1 && exactCap.selectedBodyQueryComplete,
          "Exact candidate count unnecessarily reported an exceeded budget");
    cap.candidates=0; Check(index.Cast(actor,Pose({4,0,0}),Pose(),all,cap).status==Status::Invalid,
                            "Zero candidate budget accepted");
    auto cornerWorld=World(); Add(cornerWorld,Box(1,1,1,.5),Pose()); SupportCollisionIndex cornerIndex(cornerWorld);
    SupportCollisionQueryLimits iterations; iterations.geometry.maxCastIterations=1;
    const auto uncertain=cornerIndex.Cast(Point(),Pose({4,1.2,1.3}),Pose({0,1.2,1.3}),all,iterations);
    Check(uncertain.status==Status::Indeterminate && uncertain.uncertainQueries==1 && !uncertain.selectedBodyQueryComplete &&
          uncertain.firstUncertainShapeIndex==0 && uncertain.firstUncertain.status==PairStatus::Indeterminate &&
          uncertain.firstUncertain.reason==RoundedConvexReason::IterationLimit,
          "Pair iteration uncertainty became a BVH miss/clearance");
    std::unique_ptr<SupportCollisionIndex> owned;
    {
        auto local=World(); Add(local,cube,Pose()); owned.reset(new SupportCollisionIndex(local));
        local.shapes[0].corePoints.clear(); local.shapes[0].radius=999; local.shapes.clear();
    }
    Check(owned->Cast(actor,Pose({4,0,0}),Pose(),all).status==Status::Hit,"Index retained dangling source geometry");
    std::unique_ptr<SupportCollisionIndex> cylinderOwned;
    {
        auto local=World(); Add(local,cube,Pose(),SupportCollisionKind::Cylinder);
        local.shapes[0].cylinder.profile=HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope;
        local.shapes[0].cylinder.quantizationKBits=0x402bfe8d;
        local.shapes[0].cylinder.virtualVertexStepBits=0x3ebe8495;
        local.shapes[0].cylinder.observedRoundThresholdBits=0x3efae14e;
        local.shapes[0].cylinder.hasRuntimeThresholdObservation=true;
        local.shapes[0].cylinder.rawCylinderRadiusBits=0x3f800000;
        local.shapes[0].cylinder.rawBaseRadiusFactorBits=0x3e800000;
        cylinderOwned.reset(new SupportCollisionIndex(local)); local.shapes.clear();
    }
    const auto cylinderHit=cylinderOwned->Cast(actor,Pose({4,0,0}),Pose(),all);
    Check(cylinderHit.status==Status::Hit && cylinderHit.sourceKind==SupportCollisionKind::Cylinder &&
          cylinderHit.cylinder.profile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope &&
          cylinderHit.cylinder.quantizationKBits==0x402bfe8d && cylinderHit.cylinder.virtualVertexStepBits==0x3ebe8495 &&
          cylinderHit.cylinder.observedRoundThresholdBits==0x3efae14e && cylinderHit.cylinder.hasRuntimeThresholdObservation &&
          cylinderHit.cylinder.rawCylinderRadiusBits==0x3f800000 && cylinderHit.cylinder.rawBaseRadiusFactorBits==0x3e800000 &&
          !cylinderHit.cylinder.matchesNativeSupportSelection && !cylinderHit.responseMetadataQualified,
          "Owned cylinder candidate lost profile/provenance or certified retail response equivalence");
    SupportCollisionIndex moved(std::move(*owned));
    Check(owned->ShapeCount()==0 && moved.ShapeCount()==1 &&
          owned->Cast(actor,Pose({4,0,0}),Pose(),all).status==Status::Invalid &&
          moved.Cast(actor,Pose({4,0,0}),Pose(),all).status==Status::Hit,"Move construction broke ownership/moved-from guard");
    SupportCollisionIndex assigned{World()}; assigned=std::move(moved);
    Check(moved.ShapeCount()==0 && assigned.ShapeCount()==1 &&
          assigned.Overlap(Box(.25,.25,.25),Pose({.5,.1,.2}),all).status==Status::InitialOverlap,
          "Move assignment lost geometry or ownership");
    auto empty=World(); SupportCollisionIndex emptyIndex(empty);
    Check(emptyIndex.Cast(actor,Pose({4,0,0}),Pose(),all).status==Status::Separated &&
          !emptyIndex.Cast(actor,Pose({4,0,0}),Pose(),all).sceneClearanceVerified,"Complete empty selection claimed scene clearance");
    empty.selectedGeometryComplete=false; SupportCollisionIndex missing(empty);
    Check(missing.Cast(actor,Pose({4,0,0}),Pose(),all).status==Status::IncompleteSource,"Empty partial source became clear");
    auto invalidActor=actor; invalidActor.corePoints.clear();
    Check(emptyIndex.Cast(invalidActor,Pose(),Pose(),all).status==Status::Invalid,"Empty BVH skipped actor admission");
    auto invalidPose=Pose(); invalidPose.axis[0].x=2;
    Check(index.Cast(actor,invalidPose,Pose(),all).status==Status::Invalid,"Invalid query basis admitted");
    auto budget=SupportCollisionIndexLimits(); budget.shapes=1; Reject([&]{SupportCollisionIndex test(world,budget);});
    budget=SupportCollisionIndexLimits(); budget.corePoints=15; Reject([&]{SupportCollisionIndex test(world,budget);});
    budget=SupportCollisionIndexLimits(); budget.bytes=1; Reject([&]{SupportCollisionIndex test(world,budget);});
    budget=SupportCollisionIndexLimits(); budget.leafShapes=0; Reject([&]{SupportCollisionIndex test(world,budget);});
    budget=SupportCollisionIndexLimits(); budget.depth=0; Reject([&]{SupportCollisionIndex test(world,budget);});
    budget=SupportCollisionIndexLimits(); budget.depth=65; Reject([&]{SupportCollisionIndex test(world,budget);});
    auto malformed=world; malformed.shapes[0].corePoints[0].x=std::numeric_limits<double>::quiet_NaN();
    Reject([&]{SupportCollisionIndex test(malformed);});
    malformed=world; malformed.shapes[0].radius=-1; Reject([&]{SupportCollisionIndex test(malformed);});
    malformed=world; malformed.shapes[0].placement.axis[0].x=2; Reject([&]{SupportCollisionIndex test(malformed);});
    SupportCollisionQueryLimits invalidLimits; invalidLimits.geometry.maxCastIterations=0;
    Check(emptyIndex.Cast(actor,Pose(),Pose(),all,invalidLimits).status==Status::Invalid,"Empty BVH skipped invalid query limits");
    SupportCollisionQueryLimits overlapOnly; overlapOnly.geometry.coordinateGameUnits=10;
    overlapOnly.geometry.maximumRadiusGameUnits=1; overlapOnly.geometry.targetSeparationGameUnits=1;
    Check(OverlapRoundedConvex(Point(),Pose({9.5,0,0}),Point(),Pose(),overlapOnly.geometry).status==PairStatus::Separated,
          "Cast-only skin changed the physical overlap fixture");
    Check(emptyIndex.Overlap(Point(),Pose({9.5,0,0}),all,overlapOnly).status==Status::Separated,
          "Overlap broadphase applied cast-only clearance to physical admission");
    // Both endpoints are inside the coordinate budget, but the admitted
    // shortest rotation would move the offset point through X13. Pair policy
    // refuses it; an empty BVH must not bypass the SAME motion admission.
    auto offset=Point(); offset.corePoints[0]={8,0,0};
    SupportCollisionQueryLimits motion; motion.geometry.coordinateGameUnits=10; motion.geometry.maximumRadiusGameUnits=1;
    const auto from=Pose({5,0,0},-pi/2),to=Pose({5,0,0},pi/2);
    const auto direct=CastRoundedConvex(offset,from,to,Point(),Pose(),Pose(),motion.geometry);
    Check(direct.status==PairStatus::Invalid,"Motion admission regression fixture was not outside the pair budget");
    Check(emptyIndex.Cast(offset,from,to,all,motion).status==Status::Invalid,
          "Empty BVH turned an invalid intermediate rotation into a separated result");
}
void ExplicitClearancePolicy() {
    auto world=World(); Add(world,Box(100,100,1),Pose({0,0,-1})); Add(world,Box(1,100,100),Pose({6,0,50}));
    auto actor=Box(.5,.5,.5); for (auto& p:actor.corePoints) p.z+=.5;
    Policy all; SupportCollisionIndex index(world); SupportCollisionQueryLimits strict;
    strict.geometry.targetSeparationGameUnits=.25;
    const auto from=Pose({0,0,.25}),to=Pose({10,0,.25});
    const auto tangent=index.Cast(actor,from,to,all,strict);
    Check(tangent.status==Status::Indeterminate && tangent.hasCandidate && tangent.uncertainQueries==1 &&
          tangent.physicalSeparationRetries==0 && tangent.physicalSeparationFallbacks==0,
          "Default strict clearance changed the tangent floor policy");
    auto nonblocking=strict; nonblocking.clearancePolicy=SupportClearancePolicy::NonBlockingPhysical;
    const auto wall=Compare(world,index,actor,from,to,all,nonblocking);
    Check(wall.status==Status::Hit && wall.shapeIndex==1 && Near(wall.candidate.fraction,.425) &&
          wall.physicalSeparationRetries==1 && wall.physicalSeparationFallbacks==1 && wall.selectedBodyQueryComplete,
          "Explicit physical certificate did not remove only the nonblocking floor");
    Policy unknown; unknown.occurrences[1]=SupportFilterDecision::Unknown;
    const auto unknownFloor=index.Cast(actor,from,to,unknown,nonblocking);
    Check(unknownFloor.status==Status::Indeterminate && unknownFloor.hasCandidate && unknownFloor.unknownFilters==1 &&
          unknownFloor.physicalSeparationRetries==0 && !unknownFloor.selectedBodyQueryComplete,
          "Clearance policy bypassed an unknown source filter");
    auto partial=world; partial.selectedGeometryComplete=false; SupportCollisionIndex incomplete(partial);
    const auto partialWall=Compare(partial,incomplete,actor,from,to,all,nonblocking);
    Check(partialWall.status==Status::IncompleteSource && partialWall.hasCandidate &&
          partialWall.physicalSeparationFallbacks==1 && !partialWall.selectedBodyQueryComplete,
          "A physical certificate promoted incomplete source coverage");
    auto approach=nonblocking; approach.geometry.targetSeparationGameUnits=.5;
    const auto physicallyBlocking=CastRoundedConvex(actor,from,Pose({0,0,-2}),Physical(world.shapes[0]),
        world.shapes[0].placement,world.shapes[0].placement);
    Check(physicallyBlocking.status==PairStatus::Hit,"Blocking floor reference is not a physical hit");
    const auto blocked=index.Cast(actor,from,Pose({0,0,-2}),all,approach);
    Check(blocked.status==Status::InitialOverlap && blocked.hasCandidate && blocked.shapeIndex==0 &&
          blocked.physicalSeparationRetries==1 && blocked.physicalSeparationFallbacks==0,
          "A future physical floor hit was discarded because its initial skin was violated");
    const auto penetrating=index.Cast(actor,Pose({0,0,-.25}),Pose({0,0,-2}),all,approach);
    Check(penetrating.status==Status::InitialOverlap && penetrating.hasCandidate &&
          penetrating.physicalSeparationRetries==0 && penetrating.physicalSeparationFallbacks==0,
          "Actual initial physical overlap entered the clearance-only fallback");
    auto noSkin=nonblocking; noSkin.geometry.targetSeparationGameUnits=0;
    const auto physicalOnly=Compare(world,index,actor,from,to,all,noSkin);
    Check(physicalOnly.status==Status::Hit && Near(physicalOnly.candidate.fraction,.45) &&
          physicalOnly.physicalSeparationRetries==0,"Zero skin unexpectedly retried physical geometry");
    auto capped=nonblocking; capped.candidates=1;
    const auto exhausted=index.Cast(actor,from,to,all,capped);
    Check(exhausted.status==Status::Indeterminate && exhausted.tested==1 && exhausted.physicalSeparationFallbacks==1 &&
          !exhausted.selectedBodyQueryComplete,"Physical fallback bypassed the shape candidate cap");
    Check(world.shapes[0].radius==0 && world.shapes[1].radius==0 && actor.radius==0,
          "Consumer clearance policy changed immutable physical radii");
}
bool Intersects(const RoundedConvexBoundsResult& a,const RoundedConvexBoundsResult& b) {
    return a.minimum.x<=b.maximum.x && a.maximum.x>=b.minimum.x &&
        a.minimum.y<=b.maximum.y && a.maximum.y>=b.minimum.y &&
        a.minimum.z<=b.maximum.z && a.maximum.z>=b.minimum.z;
}
LinearResult LinearCanonicalCandidates(const SupportCollisionWorld& world,
    const std::vector<RoundedConvexBoundsResult>& sourceBounds,const RoundedConvexShape& actor,
    const CollisionRigidTransform& from,const CollisionRigidTransform& to,const SupportCollisionQueryLimits& limits) {
    LinearResult out; const auto motion=RoundedConvexMotionBounds(actor,from,to,limits.geometry);
    Check(motion.valid,"Original probe motion is not admitted");
    for (std::size_t i=0;i<world.shapes.size();++i) {
        if (!Intersects(motion,sourceBounds[i])) continue;
        ++out.tested; const auto& source=world.shapes[i]; const auto physical=Physical(source);
        const auto r=CastRoundedConvex(actor,from,to,physical,source.placement,source.placement,limits.geometry);
        if (PhysicallyNonblocking(limits,false,r,actor,from,to,physical,source.placement,out)) continue;
        if (r.status==PairStatus::Invalid) { out.invalid=true; continue; }
        if (r.status==PairStatus::Indeterminate) { ++out.uncertain; continue; }
        if (r.status==PairStatus::Separated) continue;
        const double fraction=r.status==PairStatus::InitialOverlap ? 0 : r.fraction;
        const double previous=out.candidate.status==PairStatus::InitialOverlap ? 0 : out.candidate.fraction;
        if (!out.found || fraction<previous || (fraction==previous && i<out.shape)) {
            out.found=true; out.candidate=r; out.shape=i;
        }
    }
    return out;
}
SupportCollisionQueryResult OriginalProbe(const std::string& map,const char* probe,
    const SupportCollisionWorld& world,const SupportCollisionIndex& index,
    const std::vector<RoundedConvexBoundsResult>& bounds,const RoundedConvexShape& actor,
    const CollisionRigidTransform& from,const CollisionRigidTransform& to,
    const SupportCollisionQueryLimits& limits=SupportCollisionQueryLimits()) {
    Policy includeAllGeometry; const auto reference=LinearCanonicalCandidates(world,bounds,actor,from,to,limits);
    Check(!reference.invalid,"Original candidate failed canonical pair admission");
    const auto actual=index.Cast(actor,from,to,includeAllGeometry,limits);
    Check(actual.hasCandidate==reference.found && actual.tested==reference.tested &&
          actual.uncertainQueries==reference.uncertain,"Original BVH/canonical linear candidate scan disagree");
    Check(actual.physicalSeparationRetries==reference.physicalRetries &&
          actual.physicalSeparationFallbacks==reference.physicalFallbacks,"Original physical certificate retry/fallback counts disagree");
    if (reference.found) {
        Check(actual.shapeIndex==reference.shape && actual.source.occurrence==world.shapes[reference.shape].source.occurrence &&
              actual.candidate.status==reference.candidate.status && Near(actual.candidate.fraction,reference.candidate.fraction),
              "Original BVH lost nearest known pair candidate/source identity");
        const auto& source=world.shapes[reference.shape];
        Check(actual.sourceKind==source.kind && actual.cylinder.profile==source.cylinder.profile &&
              actual.cylinder.matchesNativeSupportSelection==source.cylinder.matchesNativeSupportSelection,
              "Original BVH lost its candidate interpretation metadata");
        if(source.kind==SupportCollisionKind::Cylinder)
            Check(!actual.responseMetadataQualified && !actual.cylinder.matchesNativeSupportSelection,
                  "Original cylinder envelope certified retail response equivalence");
    }
    const bool complete=world.selectedGeometryComplete && world.selectedHierarchyComplete;
    const auto expected=!complete ? Status::IncompleteSource : reference.uncertain ? Status::Indeterminate :
        !reference.found ? Status::Separated : reference.candidate.status==PairStatus::InitialOverlap ? Status::InitialOverlap : Status::Hit;
    Check(actual.status==expected && actual.selectedBodyQueryComplete==(complete && !reference.uncertain) &&
          !actual.sceneClearanceVerified,"Original partial/uncertain geometry was promoted to scene clearance");
    std::cout << std::setprecision(12) << "WOLF2009_SUPPORT_INDEX_SOURCE_PROBE map=" << map << " probe=" << probe
        << " policy=include-all-geometry runtimeFilterVerified=0 status=" << int(actual.status)
        << " clearance=" << (limits.clearancePolicy==SupportClearancePolicy::Strict ? "Strict" : "NonBlockingPhysical")
        << " shapes=" << index.ShapeCount() << " canonicalCandidates=" << reference.tested
        << " tested=" << actual.tested << " uncertain=" << actual.uncertainQueries
        << " physicalRetries=" << actual.physicalSeparationRetries << " physicalFallbacks=" << actual.physicalSeparationFallbacks
        << " hasCandidate=" << actual.hasCandidate << " selectedBodyQueryComplete=" << actual.selectedBodyQueryComplete
        << " sceneClearanceVerified=" << actual.sceneClearanceVerified;
    if (actual.hasCandidate) std::cout << " sourceShape=" << actual.source.sourceShapeOffset
        << " occurrence=" << actual.source.occurrence << " fraction=" << actual.candidate.fraction
        << " sourceKind=" << static_cast<int>(actual.sourceKind) << " cylinderProfile=" << static_cast<int>(actual.cylinder.profile)
        << " actorX=" << from.translation.x+(to.translation.x-from.translation.x)*actual.candidate.fraction
        << " signedDistance=" << actual.candidate.signedDistance;
    std::cout << '\n'; ++comparisons; return actual;
}
void Original(const char* path,HavokCylinderGeometryProfile cylinderProfile) {
    std::ifstream input(path,std::ios::binary); Check(bool(input),"Cannot open supplied source map");
    const auto archive=Archive::Scan(input); std::string name;
    for (const auto& chunk:archive.Chunks()) if (chunk.type=="PROC") for (const auto& entry:chunk.entries) {
        Check(name.empty(),"Ambiguous source-map world"); name=entry.name;
    }
    const auto bytes=ReadMapResource(input,archive,name,"HKXS",64u<<20);
    const auto source=DecodeWorldCollisionGeometry(bytes,name);
    const auto triangles=DecodeWorldTriangleGeometry(bytes,source);
    std::map<std::uint32_t,HavokPrimitiveKind> selected;
    for (const auto& missing:source.omissions) {
        HavokPrimitiveKind kind;
        if (missing.type=="hkpSphereShape") kind=HavokPrimitiveKind::Sphere;
        else if (missing.type=="hkpCapsuleShape") kind=HavokPrimitiveKind::Capsule;
        else if (missing.type=="hkpCylinderShape") kind=HavokPrimitiveKind::Cylinder;
        else continue;
        const auto inserted=selected.emplace(missing.sourceShapeOffset,kind);
        Check(inserted.second || inserted.first->second==kind,"Source primitive kind identity changed");
    }
    std::vector<HavokPrimitiveSelection> selection;
    for (const auto& entry:selected) selection.emplace_back(entry.first,entry.second);
    const auto primitives=DecodeHavokPrimitiveSupport(bytes,selection,cylinderProfile);
    const auto world=BuildSupportCollisionWorld(source,triangles,primitives);
    SupportCollisionIndex index(world); Check(index.ShapeCount()==world.shapes.size(),"Source index dropped imported support shapes");
    std::vector<RoundedConvexBoundsResult> bounds;
    for (const auto& shape:world.shapes) {
        const auto value=RoundedConvexBounds(Physical(shape),shape.placement);
        Check(value.valid,"Source shape has no canonical bound"); bounds.push_back(value);
    }
    std::size_t generated=0;
    const SupportCollisionKind kinds[]={SupportCollisionKind::Box,SupportCollisionKind::ConvexVertices,
        SupportCollisionKind::Triangle,SupportCollisionKind::Sphere,SupportCollisionKind::Capsule,SupportCollisionKind::Cylinder};
    const char* labels[]={"source-box","source-hull","source-triangle","source-sphere","source-capsule","source-cylinder-envelope"};
    for (int k=0;k<6;++k) for (std::size_t i=0;i<world.shapes.size();++i) {
        const auto& shape=world.shapes[i]; if (shape.kind!=kinds[k]) continue;
        CollisionVec3 centroid;
        for (auto p:shape.corePoints) { centroid.x+=p.x/shape.corePoints.size(); centroid.y+=p.y/shape.corePoints.size(); centroid.z+=p.z/shape.corePoints.size(); }
        auto coreCenter=Point(); coreCenter.corePoints[0]=centroid;
        const auto placed=RoundedConvexBounds(coreCenter,shape.placement); Check(placed.valid,"Source centroid placement failed");
        const CollisionVec3 target={.5*placed.minimum.x+.5*placed.maximum.x,
                                   .5*placed.minimum.y+.5*placed.maximum.y,
                                   .5*placed.minimum.z+.5*placed.maximum.z};
        const auto result=OriginalProbe(name,labels[k],world,index,bounds,Point(.25),
                                       Pose({bounds[i].minimum.x-4.25,target.y,target.z}),Pose(target));
        Check(result.hasCandidate,"Probe into an imported source support core lost every known contact");
        if(shape.kind==SupportCollisionKind::Cylinder) {
            // Neighbouring walls can be the nearest whole-world contact.
            // Explicitly select this occurrence as well, so the source cylinder
            // itself must survive BVH ownership, culling and narrowphase.
            Policy only; only.otherwise=SupportFilterDecision::Exclude;
            only.occurrences[shape.source.occurrence]=SupportFilterDecision::Include;
            const auto actor=Point(.25),obstacle=Physical(shape);
            const auto from=Pose({bounds[i].minimum.x-4.25,target.y,target.z}),to=Pose(target);
            const auto pair=CastRoundedConvex(actor,from,to,obstacle,shape.placement,shape.placement);
            const auto selected=index.Cast(actor,from,to,only);
            Check(pair.status==PairStatus::Hit && selected.status==Status::Hit && selected.tested==1 &&
                  selected.hasCandidate && selected.shapeIndex==i && selected.sourceKind==SupportCollisionKind::Cylinder &&
                  Near(selected.candidate.fraction,pair.fraction) && selected.candidate.hasWitnesses &&
                  selected.cylinder.profile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope &&
                  !selected.cylinder.matchesNativeSupportSelection && !selected.responseMetadataQualified &&
                  !selected.sceneClearanceVerified,"Source cylinder occurrence failed an explicitly filtered BVH cast");
            std::cout << "WOLF2009_SUPPORT_INDEX_SOURCE_CYLINDER_OK map=" << name
                << " occurrence=" << shape.source.occurrence << " tested=" << selected.tested
                << " fraction=" << selected.candidate.fraction << " nativeSelectionEquivalent=0\n";
        }
        ++generated; break;
    }
    if (name=="maps/game/empty/empty") {
        auto player=Box(16,16,36); for (auto& p:player.corePoints) p.z+=36;
        const auto physical=OriginalProbe(name,"empty-player-box-skin0",world,index,bounds,player,Pose({0,0,.25}),Pose({256,0,.25}));
        Check(physical.status==Status::Hit && physical.hasCandidate && Near(physical.candidate.fraction*256,168,.01),
              "Original empty physical wall cast did not stop the authored player box near X168");
        SupportCollisionQueryLimits skin; skin.geometry.targetSeparationGameUnits=.25;
        // The floor starts at the requested skin distance: tangent uncertainty
        // is preserved and compared, not converted into a movement clearance.
        OriginalProbe(name,"empty-player-box-skin025",world,index,bounds,player,Pose({0,0,.25}),Pose({256,0,.25}),skin);
        skin.clearancePolicy=SupportClearancePolicy::NonBlockingPhysical;
        const auto allowed=OriginalProbe(name,"empty-player-box-skin025-nonblocking-physical",world,index,bounds,
            player,Pose({0,0,.25}),Pose({256,0,.25}),skin);
        Check(allowed.status==Status::Hit && allowed.selectedBodyQueryComplete && allowed.hasCandidate &&
              Near(allowed.candidate.fraction*256,167.75,.01) && allowed.physicalSeparationFallbacks==1,
              "Explicit source-empty physical floor certificate did not preserve the wall skin hit");
    }
    Check(generated && !world.nativeRunnable,"No source-derived probe or source became native runnable");
    std::cout << "WOLF2009_SUPPORT_COLLISION_INDEX_SOURCE_OK map=" << name << " generatedProbes=" << generated
        << " cylinderProfile=" << static_cast<int>(cylinderProfile)
        << " supportShapes=" << world.shapes.size() << " sourceGeometryComplete=" << world.selectedGeometryComplete
        << " otherSourceBodies=" << world.otherSourceRigidBodies << " runtimeFilterVerified=0 sceneClearanceVerified=0\n";
}
} // namespace
int main(int argc,char** argv) {
    try {
        NearestAndFilters(); CanonicalBoundsAndMotion(); BudgetsLifetimeAndInvalid(); ExplicitClearancePolicy();
        std::cout << "WOLF2009_SUPPORT_COLLISION_INDEX_SYNTHETIC_OK checks=" << checks << " linearComparisons=" << comparisons << '\n';
        int first=1;
        const bool cylinderEnvelope=argc>1 && std::string(argv[1])=="--cylinder-envelope";
        if (cylinderEnvelope) ++first;
        for (int i=first;i<argc;i+=2) {
            Check(std::string(argv[i])=="--source-map" && i+1<argc,"Usage: --source-map owner-supplied.mpk [--source-map ...]");
            Original(argv[i+1],cylinderEnvelope ? HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope :
                     HavokCylinderGeometryProfile::Unresolved);
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "Support collision index test failed: " << e.what() << '\n'; return 1; }
}

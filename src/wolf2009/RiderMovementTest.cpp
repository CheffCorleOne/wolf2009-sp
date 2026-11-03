// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiderMovement.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace wolf2009 { namespace {
void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(double a,double b,double epsilon=1e-7){return std::abs(a-b)<=epsilon;}
ModelCollisionPrimitive Box(CollisionVec3 lo,CollisionVec3 hi,double radius,std::uint32_t source,ModelPrimitiveKind kind){
    ModelCollisionPrimitive p;p.kind=kind;p.sourceChildShapeOffset=source;auto& h=p.geometry;
    h.sourceShapeOffset=source;h.sourceRegion=1000+source;h.radius=radius;
    for(int mask=0;mask<8;++mask)h.vertices.push_back({mask&1?hi.x:lo.x,mask&2?hi.y:lo.y,mask&4?hi.z:lo.z});
    const double minimum[]={lo.x,lo.y,lo.z},maximum[]={hi.x,hi.y,hi.z};
    for(int k=0;k<3;++k)for(int sign=-1;sign<=1;sign+=2){ConvexContactPlane plane;plane.normal={k==0?double(sign):0,k==1?double(sign):0,k==2?double(sign):0};plane.distance=sign>0?-maximum[k]:minimum[k];h.planes.push_back(plane);}
    return p;
}
ModelPrimitiveCollision Model(){
    ModelPrimitiveCollision m;m.selectedBodyName="models/synthetic/cart";m.sourceBodyOffset=64;m.rootShapeOffset=576;m.localPoseVerified=true;m.geometryComplete=true;m.omittedBoundsComplete=true;
    m.localPrimitives.push_back(Box({-64,-128,-4},{64,128,0},0,10,ModelPrimitiveKind::AxisBox));
    m.localPrimitives.push_back(Box({-64,-104,-4},{64,-100,100},2,20,ModelPrimitiveKind::ConvexHull));
    m.localPrimitives.push_back(Box({-64,100,-4},{64,104,100},2,30,ModelPrimitiveKind::ConvexHull));return m;
}
RiderMovementQualification Qualification(){RiderMovementQualification q;q.instanceId="synthetic/cart";q.centralOpeningCaseVerified=true;return q;}
RiderMovementParent Parent(double y,std::uint64_t sample){RiderMovementParent p;p.instanceId="synthetic/cart";p.translation={100,y,300};p.sample=sample;return p;}
RiderMovementProfile Profile(){const auto r=BuildRiderMovementProfile(Model(),{0,-50,1},Qualification());Need(r.Succeeded(),r.reason);return r.profile;}
RiderMovementState Initial(const RiderMovementProfile& p){const auto r=InitializeRiderMovement(p,Parent(200,0));Need(r.Succeeded(),r.reason);return r.state;}
RiderMovementResult Step(const RiderMovementProfile& p,const RiderMovementState& s,RiderMovementInput input,bool carry=true){return StepRiderMovement(p,s,input,Parent(s.parentTranslation.y+(carry?25./60:0),s.parentSample+1),1./60);}
RiderMovementState Steps(const RiderMovementProfile& p,RiderMovementState s,RiderMovementInput input,int n,bool carry=true){for(int i=0;i<n;++i){const auto r=Step(p,s,input,carry);Need(r.Succeeded(),r.reason);s=r.state;}return s;}
void Unchanged(const RiderMovementResult& r,const RiderMovementState& s){Need(!r.Succeeded()&&r.state.sourceToken==s.sourceToken&&r.state.parentSample==s.parentSample&&Near(r.state.localFeet.y,s.localFeet.y)&&Near(r.state.worldFeet.y,s.worldFeet.y)&&Near(r.state.relativeSpeed,s.relativeSpeed),"Rejected rider step changed state");}
} // namespace
int RunRiderMovementTests(){int passed=0;try{
    const auto profile=Profile();Need(Near(profile.minimumY,-82)&&Near(profile.maximumY,82)&&profile.floorShape==10&&profile.backShape==20&&profile.frontShape==30&&Near(profile.supportTop,0)&&Near(profile.supportGap,1),"Source floor/end projection failed");++passed;
    auto q=Qualification();q.centralOpeningCaseVerified=false;Need(BuildRiderMovementProfile(Model(),{0,-50,1},q).status==RiderMovementStatus::Unqualified,"Unqualified opening accepted");++passed;
    auto m=Model();m.localPoseVerified=false;Need(!BuildRiderMovementProfile(m,{0,-50,1},Qualification()).Succeeded(),"Unknown local pose accepted");m=Model();m.omittedBoundsComplete=false;Need(!BuildRiderMovementProfile(m,{0,-50,1},Qualification()).Succeeded(),"Unknown omissions accepted");++passed;
    m=Model();m.geometryComplete=false;ModelPrimitiveOmission omission;omission.type="synthetic/omitted";omission.sourceRegion=99;omission.boundsKnown=true;omission.conservativeBounds.minimum={-5,-5,-20};omission.conservativeBounds.maximum={5,5,-10};m.omittedGeometryBounds.push_back(omission);Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).Succeeded(),"Disjoint bounded omission blocked scoped path");++passed;
    m.omittedGeometryBounds[0].conservativeBounds.maximum.z=50;Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).status==RiderMovementStatus::UnsupportedCorridorOverlap,"Omitted overlap invented clearance");++passed;
    m.omittedGeometryBounds[0].boundsKnown=false;Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).status==RiderMovementStatus::UnknownGeometry,"Unknown bound invented clearance");++passed;
    m=Model();m.localPrimitives.push_back(Box({-1,-1,0},{1,1,90},0,40,ModelPrimitiveKind::AxisBox));Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).status==RiderMovementStatus::UnsupportedCorridorOverlap,"Interior bound became clear path/fake end wall");++passed;
    m.localPrimitives.back().geometry.contentsKnown=true;m.localPrimitives.back().geometry.rawContents=CollisionMonsterClip;Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).Succeeded(),"Explicit nonplayer contents changed player path");++passed;
    m=Model();auto duplicate=m.localPrimitives.front();duplicate.geometry.sourceShapeOffset=11;m.localPrimitives.push_back(duplicate);Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).status==RiderMovementStatus::UnsupportedSupport,"Ambiguous support selected");++passed;
    m=Model();m.localPrimitives.pop_back();Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).status==RiderMovementStatus::UnsupportedEndFaces,"Missing end face invented boundary");++passed;
    m=Model();m.localPrimitives[1].geometry.planes[0].normal.x=2;Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).status==RiderMovementStatus::UnknownGeometry,"Malformed hull accepted");++passed;
    m=Model();m.localPrimitives[0]=Box({-64,-90,-4},{64,90,0},0,10,ModelPrimitiveKind::AxisBox);Need(BuildRiderMovementProfile(m,{0,-50,1},Qualification()).status==RiderMovementStatus::UnsupportedSupport,"Partial floor extended as artificial support");++passed;
    Need(BuildRiderMovementProfile(Model(),{0,-50,0},Qualification()).Succeeded(),"Exact support top required fake gap");Need(!BuildRiderMovementProfile(Model(),{0,-50,4},Qualification()).Succeeded(),"Excessive initial support gap accepted");++passed;
    RiderMovementParameters bad;bad.forwardSpeed=0;Need(BuildRiderMovementProfile(Model(),{0,-50,1},Qualification(),bad).status==RiderMovementStatus::InvalidParameters,"Invalid speed accepted");bad=RiderMovementParameters();bad.backwardAcceleration=(std::numeric_limits<double>::quiet_NaN)();Need(!BuildRiderMovementProfile(Model(),{0,-50,1},Qualification(),bad).Succeeded(),"Nonfinite acceleration accepted");++passed;
    auto state=Initial(profile);Need(Near(state.worldFeet.x,100)&&Near(state.worldFeet.y,150)&&Near(state.worldFeet.z,301)&&state.relativeSpeed==0,"Initial placement/offset failed");++passed;
    auto result=Step(profile,state,RiderMovementInput::Forward);Need(result.Succeeded()&&Near(result.state.relativeSpeed,131.23)&&Near(result.worldVelocity.y,156.23)&&Near(result.state.localOffset.x,0)&&Near(result.state.localOffset.z,0),"First observed forward/carry response failed");++passed;
    state=result.state;result=Step(profile,state,RiderMovementInput::Forward);Need(result.Succeeded()&&Near(result.state.relativeSpeed,139.98,.03)&&Near(result.worldVelocity.y,165,.03),"Forward settled response differs from PC reference");++passed;
    result=Step(profile,result.state,RiderMovementInput::Released);Need(result.Succeeded()&&Near(result.state.relativeSpeed,8.77)&&Near(result.worldVelocity.y,33.77),"Forward release lost carry");result=Step(profile,result.state,RiderMovementInput::Released);Need(result.Succeeded()&&Near(result.state.relativeSpeed,.02,.03),"Forward release failed to settle");++passed;
    result=Step(profile,result.state,RiderMovementInput::Backward);Need(result.Succeeded()&&Near(result.state.relativeSpeed,-124.95)&&Near(result.worldVelocity.y,-99.95),"Verified backward first response/carry failed");result=Step(profile,result.state,RiderMovementInput::Backward);Need(result.Succeeded()&&Near(result.state.relativeSpeed,-126)&&Near(result.worldVelocity.y,-101),"Backward speed incorrectly equals forward speed");++passed;
    result=Step(profile,result.state,RiderMovementInput::Released);Need(result.Succeeded()&&Near(result.state.relativeSpeed,-1.05)&&Near(result.worldVelocity.y,23.95),"Backward release response failed");result=Step(profile,result.state,RiderMovementInput::Released);Need(result.Succeeded()&&result.state.relativeSpeed==0&&Near(result.worldVelocity.y,25),"Release stopped platform carry");++passed;
    state=Initial(profile);state=Steps(profile,state,RiderMovementInput::Forward,38);state=Steps(profile,state,RiderMovementInput::Released,18);Need(Near(state.localOffset.y,88.68,.04),"Moving W/release route differs from original measured displacement");state=Steps(profile,state,RiderMovementInput::Backward,38);state=Steps(profile,state,RiderMovementInput::Released,18);Need(Near(state.localOffset.y,8.88,.04),"Moving W/release/S route differs from original");++passed;
    auto stopped=Initial(profile);stopped=Steps(profile,stopped,RiderMovementInput::Forward,38,false);stopped=Steps(profile,stopped,RiderMovementInput::Released,18,false);stopped=Steps(profile,stopped,RiderMovementInput::Backward,38,false);stopped=Steps(profile,stopped,RiderMovementInput::Released,18,false);Need(Near(stopped.localOffset.y,state.localOffset.y)&&Near(stopped.worldFeet.y,stopped.localFeet.y+200),"Stopped route or carry double-count mismatch");++passed;
    state=Steps(profile,Initial(profile),RiderMovementInput::Forward,120);Need(Near(state.localFeet.y,82)&&state.relativeSpeed==0,"Front source contact tunneled");result=Step(profile,state,RiderMovementInput::Forward);Need(result.status==RiderMovementStatus::EndContact&&result.contactShape==30,"Front contact lacks source provenance");++passed;
    state=Steps(profile,state,RiderMovementInput::Backward,120);Need(Near(state.localFeet.y,-82)&&state.relativeSpeed==0,"Back source contact tunneled");result=Step(profile,state,RiderMovementInput::Backward);Need(result.status==RiderMovementStatus::EndContact&&result.contactShape==20,"Back contact lacks source provenance");result=Step(profile,result.state,RiderMovementInput::Forward);Need(result.Succeeded()&&result.state.localFeet.y>-82,"Moving away from contact blocked");++passed;
    state=Step(profile,Initial(profile),RiderMovementInput::Forward).state;result=Step(profile,state,RiderMovementInput::Backward);Need(result.status==RiderMovementStatus::UnsupportedReversal,"Unmeasured direct reversal silently generalized");Unchanged(result,state);++passed;
    state=Initial(profile);result=Step(profile,state,static_cast<RiderMovementInput>(2));Need(result.status==RiderMovementStatus::InvalidInput,"Lateral/unrecognized input accepted");Unchanged(result,state);++passed;
    result=StepRiderMovement(profile,state,RiderMovementInput::Forward,Parent(200,1),1./30);Need(result.status==RiderMovementStatus::InvalidTimeStep,"Unmeasured variable step accepted");Unchanged(result,state);++passed;
    auto parent=Parent(200,1);parent.instanceId="different/cart";result=StepRiderMovement(profile,state,RiderMovementInput::Forward,parent,1./60);Need(result.status==RiderMovementStatus::WrongParent,"Wrong support accepted");Unchanged(result,state);++passed;
    result=StepRiderMovement(profile,state,RiderMovementInput::Released,Parent(200,0),1./60);Need(result.status==RiderMovementStatus::StaleParent,"Stale parent accepted");Unchanged(result,state);++passed;
    parent=Parent(200,1);parent.translation.x+=1;result=StepRiderMovement(profile,state,RiderMovementInput::Released,parent,1./60);Need(result.status==RiderMovementStatus::UnsupportedParentMotion,"Unverified cross-rail parent movement accepted");Unchanged(result,state);parent=Parent(300,1);result=StepRiderMovement(profile,state,RiderMovementInput::Released,parent,1./60);Need(result.status==RiderMovementStatus::ExcessiveParentMotion,"Excessive carry accepted");Unchanged(result,state);++passed;
    auto modified=profile;modified.maximumY+=10;Need(!InitializeRiderMovement(modified,Parent(200,0)).Succeeded(),"Profile mutation extended unverified corridor");auto broken=state;broken.localFeet.x+=1;result=Step(profile,broken,RiderMovementInput::Released);Need(!result.Succeeded(),"Changed lateral path accepted");++passed;
    std::cout<<"Rider movement tests: "<<passed<<" passed, 0 failed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Rider movement tests failed after "<<passed<<" checks: "<<e.what()<<'\n';return 1;}}
} // namespace wolf2009

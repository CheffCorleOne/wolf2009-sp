// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceRagdoll.h"
#include "HavokTestFixture.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace wolf2009;using namespace wolf2009_test;
int checks=0;
void Check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
template<class F> void Reject(F f,const char* why){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}Check(rejected,why);}
void Identity(Bytes& b,std::size_t p,float z=0){for(int i=0;i<3;++i)F(b,p+i*16+i*4,1);F(b,p+56,z);F(b,p+60,1);}
HavokFixture Fixture(){
    HavokFixture f(4096);auto& b=f.data;auto& l=f.links;auto& t=f.types;
    l[0]=32;U(b,4,2);U(b,8,0x80000002u);
    Text(b,96,"RagdollInstance");Text(b,112,"hkaRagdollInstance");l[32]=96;l[36]=112;l[40]=768;
    Text(b,144,"Animation Container");Text(b,168,"hkaAnimationContainer");l[48]=144;l[52]=168;l[56]=288;
    t[288]="hkaAnimationContainer";l[288]=352;U(b,292,1);l[352]=384;
    t[384]="hkaSkeleton";l[384]=432;Text(b,432,"own/hinge-rig");l[388]=464;U(b,392,2);b[464]=b[465]=255;
    l[396]=480;U(b,400,2);l[480]=512;l[484]=528;l[404]=640;U(b,408,2);
    t[512]="hkaBone";l[512]=576;Text(b,576,"base");t[528]="hkaBone";l[528]=592;Text(b,592,"lid");
    for(int i=0;i<2;++i){F(b,640+i*48+28,1);for(int k=0;k<3;++k)F(b,640+i*48+32+k*4,1);}
    t[768]="hkaRagdollInstance";l[776]=832;U(b,780,2);U(b,784,0x80000002u);l[788]=848;U(b,792,1);U(b,796,0x80000001u);
    l[800]=864;U(b,804,2);U(b,808,0x80000002u);U(b,864,0);U(b,868,1);l[812]=384;l[832]=1024;l[836]=1600;l[848]=2560;
    for(int i=0;i<2;++i){const auto o=1024+i*576;t[o]="hkpRigidBody";l[o+16]=2304+i*64;l[o+112]=2176+i*32;Text(b,2176+i*32,i?"body_lid":"body_base");
        Identity(b,o+224,float(i));b[o+216]=3;F(b,o+400,2);F(b,o+404,3);F(b,o+408,4);F(b,o+412,.5f);F(b,o+132,.7f);F(b,o+136,.2f);}
    for(int i=0;i<2;++i){const auto o=2304+i*64;t[o]="hkpBoxShape";F(b,o+16,.01f);F(b,o+32,.2f);F(b,o+36,.3f);F(b,o+40,.1f);}
    t[2560]="hkpConstraintInstance";l[2572]=2624;l[2580]=1600;l[2584]=1024;l[2592]=2880;Text(b,2880,"own_hinge");
    t[2624]="hkpLimitedHingeConstraintData";const auto a=2640;U(b,a,2);Identity(b,a+16);Identity(b,a+80,1);
    U(b,a+144,18);l[a+160]=2912;U(b,a+164,17);b[a+166]=1;b[a+168]=1;F(b,a+172,1);
    U(b,a+176,14);b[a+178]=1;F(b,a+180,-.5f);F(b,a+184,1.5f);F(b,a+188,.8f);U(b,a+192,12);U(b,a+196,5);
    t[2912]="hkpPositionConstraintMotor";F(b,2924,-4);F(b,2928,4);F(b,2932,.8f);F(b,2936,1);F(b,2940,2);F(b,2944,3);return f;
}
void Synthetic(){
    Check(SourceRagdollNativeAnchorRetargetAllowed(0),"native anchor exact match admitted");
    Check(SourceRagdollNativeAnchorRetargetAllowed(.5),"explicit native half-unit boundary admitted");
    Check(!SourceRagdollNativeAnchorRetargetAllowed(std::nextafter(.5,1.)),"native anchor projection above bound refused");
    Check(!SourceRagdollNativeAnchorRetargetAllowed(-.01),"negative native anchor residual refused");
    Check(!SourceRagdollNativeAnchorRetargetAllowed((std::numeric_limits<double>::infinity)()),"infinite native anchor residual refused");
    Check(!SourceRagdollNativeAnchorRetargetAllowed((std::numeric_limits<double>::quiet_NaN)()),"NaN native anchor residual refused");
    const auto center=SourceRagdollHingeLimitCenter({0,0,1},{1,0,0},0,std::acos(-1.));
    Check(std::abs(center.x)<1e-12&&std::abs(center.y-1)<1e-12,"right-handed world hinge midpoint");
    for(double angle:{0.,std::acos(-1.)*.5,std::acos(-1.)})Check(center.x*std::cos(angle)+center.y*std::sin(angle)>=-1e-12,"source min/mid/max within full-width cone");
    Check(center.x*std::cos(-.1)+center.y*std::sin(-.1)<0,"negative outside source cone");
    const auto reverseCenter=SourceRagdollHingeLimitCenter({0,0,-1},{1,0,0},0,std::acos(-1.));Check(reverseCenter.y<-.999999,"negative hinge axis preserves handedness");
    auto f=Fixture();const auto bytes=f.File();auto r=DecodeSourceRagdoll(bytes);Check(r.complete&&r.bodies.size()==2&&r.hinges.size()==1,"two-body ragdoll fixture");
    Check(r.rawSource==bytes&&r.sourceBytes==bytes.size(),"own raw source survives");Check(r.boneToBody==std::vector<std::int32_t>({0,1}),"explicit bone mapping");
    Check(r.skeleton.joints[0].name=="base"&&r.skeleton.joints[1].parent==0,"named rig and parents");Check(r.hinges[0].body[0]==1&&r.hinges[0].body[1]==0,"constraint source order preserved");
    Check(std::abs(r.bodies[1].pose.translation.z-1/.0254)<1e-12,"body translation metre conversion");Check(r.bodies[1].inverseMass==.5,"raw inverse mass retained");
    Check(r.hinges[0].motor.present&&!r.hinges[0].motor.enabled&&r.hinges[0].motor.minForce==-4,"disabled motor data retained");
    Check(r.hinges[0].frictionEnabled&&r.hinges[0].maxFrictionTorque==1&&r.hinges[0].minimumAngle==-.5,"hinge limits/friction fields");
    const auto hull=BuildSourceRagdollNativeBox(r.bodies[1],SourceRagdollHullPolicy::NativeConservativeHullV1);
    const auto& box=r.bodies[1].boxes[0];Check(hull.halfExtents.x>=box.halfExtents.x+box.radius,"rounding is outward");
    Check(hull.maximumRoundingExcess>=box.radius*(std::sqrt(3.)-1)-1e-13,"explicit rounding excess");
    for(int axis=0;axis<3;++axis)for(int sign=-1;sign<=1;sign+=2){const double core=axis==0?box.halfExtents.x:axis==1?box.halfExtents.y:box.halfExtents.z;
        const double outer=axis==0?hull.halfExtents.x:axis==1?hull.halfExtents.y:hull.halfExtents.z;Check(outer>=core+box.radius,"all analytic sphere-axis extreme points contained");(void)sign;}
    Reject([&]{BuildSourceRagdollNativeBox(r.bodies[0],SourceRagdollHullPolicy::Unspecified);},"unselected hull policy rejected");
    auto compound=r.bodies[0];compound.boxes.push_back(compound.boxes[0]);Reject([&]{BuildSourceRagdollNativeBox(compound,SourceRagdollHullPolicy::NativeConservativeHullV1);},"compound is never silently convexified");
    for(const auto at:{864u,868u}){auto bad=Fixture();U(bad.data,at,2);Reject([&]{DecodeSourceRagdoll(bad.File());},"bad bone index rejected");}
    {auto bad=Fixture();U(bad.data,864,0xffffffffu);const auto q=DecodeSourceRagdoll(bad.File());Check(q.boneToBody[0]==-1,"unmapped bone is preserved, never identity-guessed");}
    {auto bad=Fixture();bad.links[2584]=1600;Reject([&]{DecodeSourceRagdoll(bad.File());},"self constraint rejected");}
    {auto bad=Fixture();bad.links.erase(2584);Reject([&]{DecodeSourceRagdoll(bad.File());},"missing constraint entity fixup rejected");}
    {auto bad=Fixture();bad.links[836]=1024;Reject([&]{DecodeSourceRagdoll(bad.File());},"duplicate body pointer rejected");}
    {auto bad=Fixture();F(bad.data,2336,(std::numeric_limits<float>::infinity)());Reject([&]{DecodeSourceRagdoll(bad.File());},"infinite box coordinate rejected");}
    {auto bad=Fixture();F(bad.data,2320,-1);Reject([&]{DecodeSourceRagdoll(bad.File());},"negative source radius rejected");}
    {auto bad=Fixture();F(bad.data,1248,-1);Reject([&]{DecodeSourceRagdoll(bad.File());},"reflected body frame rejected");}
    {auto bad=Fixture();bad.types[2304]="own_unsupported_shape";const auto q=DecodeSourceRagdoll(bad.File());Check(!q.complete&&q.omissions.size()==1&&!q.bodies[0].geometryComplete,"unsupported selected geometry is counted");}
    {auto bad=Fixture();bad.types[2624]="own_unsupported_constraint";const auto q=DecodeSourceRagdoll(bad.File());Check(!q.complete&&q.omissions.size()==1&&q.hinges.empty(),"unsupported selected constraint is counted");}
    {auto bad=Fixture();bad.links.erase(2800);bad.data[2786]=1;Reject([&]{DecodeSourceRagdoll(bad.File());},"enabled motor without target rejected");}
    {auto bad=Fixture();bad.data[2818]=2;Reject([&]{DecodeSourceRagdoll(bad.File());},"unknown hinge bool profile refused");}
    for(int kind=0;kind<6;++kind){SourceRagdollLimits l;if(kind==0)l.bodies=1;if(kind==1)l.joints=1;if(kind==2)l.boxes=1;if(kind==3)l.shapeVisits=1;if(kind==4)l.names=3;if(kind==5)l.maximumRadiusGameUnits=.01;Reject([&]{DecodeSourceRagdoll(bytes,"RagdollInstance",l);},"bounded decoder budget rejected");}
    {auto shortBytes=bytes;shortBytes.pop_back();Reject([&]{DecodeSourceRagdoll(shortBytes);},"truncated pack rejected");}
    Reject([&]{DecodeSourceRagdoll(bytes,"other_named_variant");},"wrong named variant rejected");
    // Proper transformed source box: preserve the axis/local placement and the
    // shared wrapper radius. No baked AABB is substituted in data decoding.
    {auto q=Fixture();q.types[3072]="hkpConvexTransformShape";q.links[1040]=3072;q.links[3096]=2304;F(q.data,3088,.01f);
        Identity(q.data,3104);F(q.data,3104,0);F(q.data,3108,1);F(q.data,3120,-1);F(q.data,3124,0);F(q.data,3152,2);
        const auto d=DecodeSourceRagdoll(q.File());Check(d.bodies[0].boxes[0].local.axis[0].y==1&&std::abs(d.bodies[0].boxes[0].local.translation.x-2/.0254)<1e-12,"rotated translated local wrapper retained");
        Check(d.bodies[0].boxes[0].radiusHierarchyVerified,"consistent wrapper radius");F(q.data,3088,.02f);const auto mismatch=DecodeSourceRagdoll(q.File());Check(!mismatch.bodies[0].boxes[0].radiusHierarchyVerified,"wrapper mismatch is visible");Reject([&]{BuildSourceRagdollNativeBox(mismatch.bodies[0],SourceRagdollHullPolicy::NativeConservativeHullV1);},"wrapper mismatch cannot be silently imported");}
}
}
int main(int argc,char** argv){try{Synthetic();std::cout<<"SourceRagdoll synthetic PASS checks="<<checks<<"\n";
    if(argc==2){std::ifstream input(argv[1],std::ios::binary);if(!input)throw std::runtime_error("Cannot read selected private source");std::vector<unsigned char> b((std::istreambuf_iterator<char>(input)),{});const auto r=wolf2009::DecodeSourceRagdoll(b);
        std::cout<<"SOURCE bodies="<<r.bodies.size()<<" hinges="<<r.hinges.size()<<" bones="<<r.skeleton.joints.size()<<" complete="<<r.complete<<"\n";for(const auto& body:r.bodies)std::cout<<"BODY offset="<<body.sourceObjectOffset<<" boxes="<<body.boxes.size()<<" complete="<<body.geometryComplete<<" invMass="<<body.inverseMass<<"\n";}
    return 0;}catch(const std::exception& e){std::cerr<<"SourceRagdoll FAIL: "<<e.what()<<"\n";return 1;}}

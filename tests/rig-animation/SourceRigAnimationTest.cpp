// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceRigAnimation.h"
#include "../HavokTestFixture.h"
#include "../engine-skinned/MeshV2Fixtures.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
using namespace wolf2009;
using namespace wolf2009_test;
namespace {
unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void Near(double a,double b){Check(std::isfinite(a)&&std::abs(a-b)<1e-10,"Analytic authored source rig FK differs");}
void Reject(const std::function<void()>& action,SourceRigFailure failure){bool rejected=false;
    try{action();}catch(const SourceRigError& e){rejected=true;Check(e.Failure()==failure,"Wrong rig refusal category");}Check(rejected,"Unsupported source rig request admitted");}
struct Clip : HavokFixture {
    explicit Clip(unsigned count,bool scale=false):HavokFixture(4096){
        links[0]=16;U(data,4,2);links[16]=128;links[20]=160;links[24]=256;
        Text(data,128,"Animation Container");Text(data,160,"hkaAnimationContainer");types[256]="hkaAnimationContainer";
        links[32]=192;links[36]=208;links[40]=320;Text(data,192,"animNames");Text(data,208,"rvHkStringContainer");types[320]="rvHkStringContainer";
        links[264]=384;U(data,268,1);links[320]=416;U(data,324,1);links[384]=1024;links[416]=512;Text(data,512,"models/own/full.hkx");
        types[1024]="hkaSplineSkeletalAnimation";U(data,1032,5);F(data,1036,.4f);U(data,1040,count);U(data,1060,5);U(data,1064,1);U(data,1068,5);U(data,1072,count*4);
        F(data,1076,.4f);F(data,1080,2.5f);F(data,1084,.1f);links[1088]=1536;U(data,1092,1);links[1100]=1552;U(data,1104,1);links[1136]=1600;
        std::uint32_t cursor=1600+count*4;
        for(unsigned i=0;i<count;++i){data[1600+4*i]=0x55;data[1600+4*i+1]=1;data[1600+4*i+2]=i?0:15;data[1600+4*i+3]=scale&&i==1?1:0;
            F(data,cursor,float(i+1));cursor+=4;
            if(!i){F(data,cursor,0);F(data,cursor+4,0);F(data,cursor+8,float(std::sqrt(.5)));F(data,cursor+12,float(std::sqrt(.5)));cursor+=16;}
            if(scale&&i==1){F(data,cursor,2);cursor+=4;}}
        U(data,1552,cursor-1600);U(data,1140,cursor-1600);
    }
    void Binding(){links[272]=2304;U(data,276,1);links[2304]=2320;types[2320]="hkaAnimationBinding";links[2320]=1024;}
};
SourceRigRequest Request(const char* rig="rig-A") {SourceRigRequest r;r.clipName="models/own/full.hkx";r.skeletonName=rig;r.policy=SourceRigPolicy::CallerIdentityFullLocalPoseV1;return r;}
void Analytic(unsigned count,bool chain){
    Clip source(count);meshv2fixtures::Skeletons skeleton(count);
    if(chain)for(unsigned i=1;i<count;++i){skeleton.pack.data[skeleton.parents+2*i]=static_cast<unsigned char>(i-1);skeleton.pack.data[skeleton.parents+2*i+1]=0;}
    auto sourceBytes=source.File(),skeletonBytes=skeleton.pack.Finish();
    const auto loaded=SourceRigAnimation::Load(sourceBytes,skeletonBytes,Request());
    sourceBytes.assign(1,0);skeletonBytes.assign(1,0); // Loaded sample must own all required source data.
    for(double time:{-1.0,0.0,.173,1.0}){
        const auto pose=loaded.SamplePose(time);Check(pose.local.size()==count&&pose.model.size()==count,"Full rig palette incomplete");
        for(unsigned i=0;i<count;++i){Near(pose.local[i].translation[0],i+1);Near(pose.local[i].translation[1],0);
            Near(pose.model[i].translation[0],1);Near(pose.model[i].translation[1],i?(chain?double((i+1)*(i+2))/2-1:i+1):0);
            Near(pose.model[i].translation[2],0);Near(std::abs(pose.model[i].rotation[2]),std::sqrt(.5));}
        Near(pose.seconds,time<0?0:time>loaded.Duration()?loaded.Duration():time);
    }
    if(!chain){const auto alternate=SourceRigAnimation::Load(source.File(),skeleton.pack.Finish(),Request("rig-B"));
        Check(alternate.Identity().request.skeletonName!=loaded.Identity().request.skeletonName,"Different exact rig identities collapsed");
        const auto a=loaded.SamplePose(.1),b=alternate.SamplePose(.1);
        for(unsigned i=0;i<count;++i)for(unsigned axis=0;axis<3;++axis)Near(a.model[i].translation[axis],b.model[i].translation[axis]);
        meshv2fixtures::Float(skeleton.pack.data,skeleton.reference+32,2);const auto unqualifiedRef=SourceRigAnimation::Load(source.File(),skeleton.pack.Finish(),Request());
        Check(!unqualifiedRef.Skeleton().referencePoseQualified,"Nonrigid reference data incorrectly qualified");
        Near(unqualifiedRef.SamplePose(.1).model[1].translation[1],2);
    }
}
void Refusals(){Clip source(3);meshv2fixtures::Skeletons rig3(3),rig7(7);const auto bytes=source.File(),skel=rig3.pack.Finish();
    auto request=Request();request.policy=SourceRigPolicy::Unspecified;
    Reject([&]{SourceRigAnimation::Load(bytes,skel,request);},SourceRigFailure::Unsupported);
    Reject([&]{SourceRigAnimation::Load(bytes,rig7.pack.Finish(),Request());},SourceRigFailure::Unsupported);
    source.Binding();Reject([&]{SourceRigAnimation::Load(source.File(),skel,Request());},SourceRigFailure::Unsupported);
    SourceRigLimits small;small.combinedSourceBytes=bytes.size()+skel.size()-1;
    Reject([&]{SourceRigAnimation::Load(bytes,skel,Request(),small);},SourceRigFailure::LimitExceeded);
    small=SourceRigLimits();small.modelTranslationMagnitude=0;Reject([&]{SourceRigAnimation::Load(bytes,skel,Request(),small);},SourceRigFailure::InvalidData);
    bool scale=false;try{SourceRigAnimation::Load(Clip(3,true).File(),skel,Request());}catch(const SplineCodecError& e){scale=e.Failure()==SplineCodecFailure::Unsupported;}Check(scale,"Nonidentity curve scale reached a rigid source palette");
    const auto loaded=SourceRigAnimation::Load(bytes,skel,Request());
    Reject([&]{loaded.SamplePose(std::numeric_limits<double>::infinity());},SourceRigFailure::InvalidData);
    small=SourceRigLimits();small.modelTranslationMagnitude=.5;const auto restricted=SourceRigAnimation::Load(bytes,skel,Request(),small);
    Reject([&]{restricted.SamplePose(0);},SourceRigFailure::InvalidData);
}
}
int main(){try{Analytic(3,false);Analytic(7,true);Refusals();std::cout<<"SOURCE_RIG_SYNTHETIC_OK checks="<<checks<<" analytic_fk=1 owned=1 no_reference_substitution=1 full_coverage=1\n";return 0;}
catch(const std::exception& e){std::cerr<<"SOURCE_RIG_SYNTHETIC_FAIL checks="<<checks<<" "<<e.what()<<'\n';return 1;}}

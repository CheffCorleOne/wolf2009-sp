// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceRigAnimation.h"
#include "StreamMounts.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
using namespace wolf2009;
namespace {
unsigned checks=0;
double maximum=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void Compare(double a,double b){const double difference=std::abs(a-b);maximum=(std::max)(maximum,difference);Check(std::isfinite(a)&&std::isfinite(b)&&difference<1e-9,"General source rig differs from original narrow decoder");}
SourceRigRequest Request(const char* clip,const char* skeleton="models/characters/_humanoid_male/humanoid_male.hkx"){
    SourceRigRequest r;r.clipName=clip;r.skeletonName=skeleton;r.policy=SourceRigPolicy::CallerIdentityFullLocalPoseV1;return r;
}
struct MatrixPose {std::array<std::array<double,3>,3> rotation;std::array<double,3> translation;};
MatrixPose Matrix(const std::array<double,4>& q,const std::array<double,3>& t){
    const double x=q[0],y=q[1],z=q[2],w=q[3];MatrixPose out;out.translation=t;
    out.rotation={{{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)}},
                   {{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)}},
                   {{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}}}};return out;
}
void FrameFK(const SourceRigAnimation& animation,double frame){
    const auto actual=animation.SamplePoseFrame(frame);const auto local=animation.Curves().SampleFrame(frame);
    Check(actual.local.size()==local.size()&&actual.model.size()==local.size(),"Source frame FK palette incomplete");
    Check(std::isfinite(actual.seconds)&&actual.seconds>=0&&actual.seconds<=animation.Duration(),"Source frame timestamp outside metadata domain");
    std::vector<MatrixPose> expected;expected.reserve(local.size());
    for(std::size_t i=0;i<local.size();++i){const MatrixPose matrix=Matrix(local[i].rotation,local[i].translation);MatrixPose global=matrix;
        const int parent=animation.Skeleton().joints[i].parent;
        if(parent>=0){const auto& p=expected[std::size_t(parent)];
            for(unsigned row=0;row<3;++row){global.translation[row]=p.translation[row];for(unsigned k=0;k<3;++k)global.translation[row]+=p.rotation[row][k]*matrix.translation[k];
                for(unsigned col=0;col<3;++col){global.rotation[row][col]=0;for(unsigned k=0;k<3;++k)global.rotation[row][col]+=p.rotation[row][k]*matrix.rotation[k][col];}}}
        expected.push_back(global);const auto observed=Matrix(actual.model[i].rotation,actual.model[i].translation);
        for(unsigned axis=0;axis<3;++axis){Compare(actual.local[i].translation[axis],local[i].translation[axis]);Compare(observed.translation[axis],global.translation[axis]);
            for(unsigned col=0;col<3;++col)Compare(observed.rotation[axis][col],global.rotation[axis][col]);}
    }
}
void Run(const std::string& base){
    const std::map<std::string,std::string> paths{{"rig_map",base+"/maps/game/trainyard/trainyard.mpk"},{"rig_skel",base+"/maps/game/global/global.spk"}};
    StreamMounts mounts([&](const std::string& id)->std::unique_ptr<std::istream>{const auto found=paths.find(id);if(found==paths.end())return std::unique_ptr<std::istream>();return std::unique_ptr<std::istream>(new std::ifstream(found->second,std::ios::binary));});
    mounts.Mount("rig_map");mounts.Mount("rig_skel");
    const auto read=[&](const char* mount,const char* type,const char* name){std::vector<StreamResource> found;for(const auto& r:mounts.Find(type,name).matches)if(r.mountId==mount)found.push_back(r);Check(found.size()==1,"Source rig member absent/ambiguous in exact package");return mounts.Read(found.front());};
    const auto hkxa=read("rig_map","HKXA","maps/game/trainyard/trainyard"),skel=read("rig_skel","SKEL","maps/game/global/global");
    const char* greeterName="models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx";
    const auto original=GreeterAnimation::Load(hkxa,skel);
    const auto general=SourceRigAnimation::Load(hkxa,skel,Request(greeterName));
    Check(general.FrameCount()==original.FrameCount()&&general.Duration()==original.Duration()&&general.FrameDuration()==original.FrameDuration()&&general.Skeleton().joints.size()==52,
          "General rig metadata differs from original named profile");
    const auto alternate=SourceRigAnimation::Load(hkxa,skel,Request(greeterName,"models/characters/_humanoid_male/empty_male_skel.hkx"));
    Check(general.Identity().request.skeletonName!=alternate.Identity().request.skeletonName,"Different explicit original rig identities collapsed");
    unsigned samples=0;
    for(int frame=0;frame<182;++frame)for(double fraction:{0.0,.37}){
        const double seconds=(frame+fraction)*original.FrameDuration();const auto expected=original.SamplePose(seconds),actual=general.SamplePose(seconds),other=alternate.SamplePose(seconds);++samples;
        for(unsigned j=0;j<52;++j){Check(general.Skeleton().joints[j].name==original.Joints()[j].name&&general.Skeleton().joints[j].parent==original.Joints()[j].parent,"General rig topology changed source indices");
            for(unsigned axis=0;axis<3;++axis){Compare(actual.local[j].translation[axis],expected.local[j].translation[axis]);Compare(actual.model[j].translation[axis],expected.model[j].translation[axis]);Compare(actual.model[j].translation[axis],other.model[j].translation[axis]);}
            for(unsigned axis=0;axis<4;++axis){Compare(actual.local[j].rotation[axis],expected.local[j].rotation[axis]);Compare(actual.model[j].rotation[axis],expected.model[j].rotation[axis]);}}
    }
    for(double frame:{0.0,17.37,180.999,181.0})FrameFK(general,frame);
    unsigned frameSamples=4;
    const char* additional[]={"models/characters/_humanoid_male/srp_tyd_mg1_order_pickup.hkx","models/characters/_humanoid_male/st1_hg1_nml_idle.hkx"};
    for(const char* name:additional){const auto animation=SourceRigAnimation::Load(hkxa,skel,Request(name));
        for(double time:{0.0,.017,.137,animation.Duration()/2,animation.Duration(),animation.Duration()+1}){
            const auto pose=animation.SamplePose(time);Check(pose.local.size()==52&&pose.model.size()==52,"Additional full source rig palette incomplete");
            for(const auto& joint:pose.model){double norm=0;for(double t:joint.translation)Check(std::isfinite(t),"Source rig model translation nonfinite");for(double q:joint.rotation)norm+=q*q;Check(std::abs(norm-1)<1e-12,"Source rig composed quaternion nonunit");}}
        if(animation.Curves().BlockCount()>1){
            for(double frame:{254.37,std::nextafter(255.0,0.0),255.0,std::nextafter(255.0,256.0),255.37,333.9,334.0}){FrameFK(animation,frame);++frameSamples;}
            Check(animation.FrameCoordinateAtTime(animation.Curves().Source().blockDuration)==255&&animation.FrameCoordinateAtTime(animation.Duration())==334,
                  "Original rig frame inverse lost block/final endpoint metadata");
        }
        std::cout<<"SOURCE_RIG_ORIGINAL clip="<<name<<" bones="<<animation.Skeleton().joints.size()<<" frames="<<animation.FrameCount()<<" blocks="<<animation.Curves().BlockCount()<<" caller_rig_explicit=1\n";
    }
    bool partial=false;try{SourceRigAnimation::Load(hkxa,skel,Request("models/props/locker_foot_01_anim_open.hkx"));}
    catch(const SourceRigError& e){partial=e.Failure()==SourceRigFailure::Unsupported;}Check(partial,"Two-track prop clip silently filled a 52-bone rig");
    Check(general.Identity().animationBytes==hkxa.size()&&general.Identity().skeletonBytes==skel.size()&&general.Identity().opaqueFloatTracks==6,"Source rig identities/opaque float fact lost");
    std::cout<<"SOURCE_RIG_ORIGINAL_OK checks="<<checks<<" greeter_samples="<<samples<<" frame_fk_samples="<<frameSamples<<" max_joint_component_error="<<maximum<<" additional_clips=2 actual_multiblock=1 two_explicit_rigs=1 partial_refused=1 retail_actor=0 native_provider=0\n";
}
}
int main(int argc,char** argv){try{if(argc!=3||std::string(argv[1])!="--source-base")throw std::runtime_error("Usage: --source-base owner-SP/base");std::cout<<std::setprecision(17);Run(argv[2]);return 0;}
catch(const std::exception& e){std::cerr<<"SOURCE_RIG_ORIGINAL_FAIL checks="<<checks<<" "<<e.what()<<'\n';return 1;}}

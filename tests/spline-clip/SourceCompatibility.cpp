// SPDX-License-Identifier: GPL-3.0-or-later
// Four selected owner clips in one existing map packet; no archive-wide scan.
#include "SplineClipData.h"
#include "GreeterAnimation.h"
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
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void Run(const std::string& base){
    const std::map<std::string,std::string> paths{{"selected_map",base+"/maps/game/trainyard/trainyard.mpk"},{"selected_skeleton",base+"/maps/game/global/global.spk"}};
    StreamMounts mounts([&](const std::string& key)->std::unique_ptr<std::istream>{const auto found=paths.find(key);if(found==paths.end())return std::unique_ptr<std::istream>();return std::unique_ptr<std::istream>(new std::ifstream(found->second,std::ios::binary));});
    mounts.Mount("selected_map");mounts.Mount("selected_skeleton");
    const auto read=[&](const std::string& mount,const std::string& type,const std::string& name){std::vector<StreamResource> found;for(const auto& resource:mounts.Find(type,name).matches)if(resource.mountId==mount)found.push_back(resource);Check(found.size()==1,"Selected exact source member missing/ambiguous");return mounts.Read(found.front());};
    const auto hkxa=read("selected_map","hkxa","maps/game/trainyard/trainyard"),skel=read("selected_skeleton","skel","maps/game/global/global");const auto greeter=GreeterAnimation::Load(hkxa,skel);
    const char* names[]={"models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx","models/characters/_humanoid_male/srp_tyd_mg1_order_pickup.hkx","models/characters/_humanoid_male/st1_hg1_nml_idle.hkx","models/props/locker_foot_01_anim_open.hkx"};
    unsigned multiblock=0,differentTracks=0;double maxTranslation=0,maxQuaternion=0;
    for(unsigned n=0;n<4;++n){const auto doc=SplineClipData::Load(hkxa,names[n]);const auto codec=SplineTransformCodec::Load(doc.input);
        Check(doc.name==names[n]&&doc.sourceBytes==hkxa.size()&&doc.input.transformTracks==codec.TrackCount(),"Original selected source identity/count mismatch");
        Check(doc.animationCount==187&&doc.bindingCount==0&&doc.bindings.empty()&&doc.bindingState==SplineBindingFactsState::Absent&&!doc.bindingTable.hasDataFixup,"Original binding absence was not preserved");
        Check(doc.rawAnimationType==5&&doc.rawEndian==0&&doc.input.rawTransformOffsets.empty()&&doc.input.rawFloatOffsets.empty(),"Original raw spline facts changed");
        if(n==0){Check(doc.nameTableIndex==34&&doc.input.transformEndOffsets==std::vector<std::uint32_t>{43640},"Original greeter exact pairing/boundary changed");
            for(double time:{0.0,.017,.137,1.234,7.542}){const auto actual=codec.Sample(time);const auto expected=greeter.SamplePose(time);Check(actual.size()==expected.local.size(),"Greeter palette count mismatch");
                for(std::size_t t=0;t<actual.size();++t){for(unsigned axis=0;axis<3;++axis)maxTranslation=(std::max)(maxTranslation,std::abs(actual[t].translation[axis]-expected.local[t].translation[axis]));for(unsigned axis=0;axis<4;++axis)maxQuaternion=(std::max)(maxQuaternion,std::abs(actual[t].rotation[axis]-expected.local[t].rotation[axis]));}}
        }
        if(n==1){Check(doc.nameTableIndex==1&&doc.input.numFrames==335&&doc.input.blockOffsets==std::vector<std::uint32_t>({0,19792})&&doc.input.rawFloatBlockOffsets==std::vector<std::uint32_t>({16256,5704})&&doc.input.transformEndOffsets==std::vector<std::uint32_t>({16256,25496}),"Original second block boundary semantics changed");++multiblock;}
        if(n==3){Check(doc.input.transformTracks==2&&doc.input.numFrames==24,"Different-track original clip profile changed");++differentTracks;}
        for(double time:{0.0,.017,.137,codec.Duration()/2,codec.Duration(),codec.Duration()+1}){const auto pose=codec.Sample(time);Check(pose.size()==doc.input.transformTracks,"Independent-track source sample count mismatch");
            for(const auto& track:pose){double norm=0;for(unsigned axis=0;axis<3;++axis)Check(std::isfinite(track.translation[axis])&&track.scale[axis]==1,"Original independent track sample nonfinite/unqualified scale");for(double value:track.rotation)norm+=value*value;Check(std::abs(norm-1)<1e-12,"Original independent quaternion is not normalized");}}
        std::cout<<"SOURCE_NAMED_SPLINE name="<<doc.name<<" index="<<doc.nameTableIndex<<" object="<<doc.animationObjectOffset<<" frames="<<codec.FrameCount()<<" blocks="<<codec.BlockCount()<<" independent_tracks="<<codec.TrackCount()<<" float_tracks_opaque="<<doc.input.floatTracks
            <<" binding_count="<<doc.bindingCount<<" binding_state=absent source_fingerprint="<<std::hex<<doc.sourceFingerprint<<std::dec<<" rig_binding=0 native_palette=0\n";
    }
    Check(multiblock==1&&differentTracks==1&&maxTranslation<1e-9&&maxQuaternion<1e-9,"Selected source codec/reader differential failed");
    std::cout<<"WOLF2009_SPLINE_CLIP_SOURCE_OK checks="<<checks<<" clips=4 actual_multiblock=1 different_trackcount_clip=1 max_greeter_translation_error="<<maxTranslation<<" max_greeter_quaternion_error="<<maxQuaternion<<" rig_binding=0 additive_semantics=0 float_evaluation=0 native_runtime=0\n";
}
}
int main(int argc,char** argv){try{if(argc!=3||std::string(argv[1])!="--source-base")throw std::runtime_error("Usage: --source-base owner-SP/base");std::cout<<std::setprecision(17);Run(argv[2]);return 0;
}catch(const std::exception& error){std::cerr<<"WOLF2009_SPLINE_CLIP_SOURCE_FAIL checks="<<checks<<" message="<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-3.0-or-later
// Optional owner-supplied archive check. No extracted source bytes are saved.
#include "AnimationWire.h"
#include "GreeterAnimation.h"
#include "StreamMounts.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
namespace wire=wolf2009::animation;
unsigned checks=0;
void Need(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::array<double,4> Slerp(std::array<double,4> a,std::array<double,4> b,double t){
    double dot=0;for(unsigned i=0;i<4;++i)dot+=a[i]*b[i];
    if(dot<0){for(auto& v:b)v=-v;dot=-dot;}
    const double angle=std::acos((std::min)(1.0,dot));
    const double sa=angle<1e-8?1-t:std::sin((1-t)*angle)/std::sin(angle),sb=angle<1e-8?t:std::sin(t*angle)/std::sin(angle);
    std::array<double,4> out;double norm=0;for(unsigned i=0;i<4;++i){out[i]=sa*a[i]+sb*b[i];norm+=out[i]*out[i];}
    for(auto& v:out)v/=std::sqrt(norm);return out;
}
void Run(const std::string& base){
    const std::map<std::string,std::string> files{{"qa_anim",base+"/maps/game/trainyard/trainyard.mpk"},{"qa_anim_skel",base+"/maps/game/global/global.spk"}};
    wolf2009::StreamMounts mounts([&](const std::string& id)->std::unique_ptr<std::istream>{
        const auto found=files.find(id);if(found==files.end())return std::unique_ptr<std::istream>();
        return std::unique_ptr<std::istream>(new std::ifstream(found->second,std::ios::binary));
    });
    mounts.Mount("qa_anim");mounts.Mount("qa_anim_skel");
    const auto read=[&](const std::string& mount,const std::string& type,const std::string& name){
        std::vector<wolf2009::StreamResource> found;
        for(const auto& resource:mounts.Find(type,name).matches)if(resource.mountId==mount)found.push_back(resource);
        if(type=="modl")for(const auto& resource:mounts.Find(type,name+".md5r").matches)if(resource.mountId==mount)found.push_back(resource);
        Need(found.size()==1,"source animation resource absent/ambiguous in selected archive");return mounts.Read(found.front());
    };
    const auto model=read("qa_anim","modl","models/characters/civilians/civ_male_04/anims/body_cm04");
    wire::Envelope envelope;envelope.animation=read("qa_anim","hkxa","maps/game/trainyard/trainyard");
    envelope.skeleton=read("qa_anim_skel","skel","maps/game/global/global");
    const auto clip=wolf2009::GreeterAnimation::Load(envelope.animation,envelope.skeleton);auto& b=envelope.binding;
    b.modelAlias="wolf2009/qa_anim/md5r/lod0/portable-codec-check/models/characters/civilians/civ_male_04/anims/body_cm04";
    b.paths={{"wolf2009/qa_anim/modl/models/characters/civilians/civ_male_04/anims/body_cm04.md5r","wolf2009/qa_anim/hkxa/maps/game/trainyard/trainyard","wolf2009/qa_anim_skel/skel/maps/game/global/global"}};
    b.clipName=clip.ResourceName();b.skeletonName=clip.SkeletonName();b.duration=clip.Duration();b.frameDuration=clip.FrameDuration();
    b.frames=static_cast<std::uint32_t>(clip.FrameCount());b.joints=static_cast<std::uint32_t>(clip.Joints().size());
    const std::array<const wire::Bytes*,3> sources{{&model,&envelope.animation,&envelope.skeleton}};
    for(unsigned i=0;i<3;++i){b.sources[i].epoch=i==2?2:1;b.sources[i].bytes=sources[i]->size();b.sources[i].fingerprint=wire::Fingerprint(sources[i]->data(),sources[i]->size());}
    const auto serialized=wire::Serialize(envelope);const auto parsed=wire::Parse(serialized);
    Need(wire::Same(parsed.binding,b)&&parsed.animation==envelope.animation&&parsed.skeleton==envelope.skeleton,"real source descriptor did not preserve its original bytes/metadata");
    const int end=int(std::ceil(b.duration*1000));
    Need(end==7542&&b.duration<double(end)*0.001&&b.duration>double(end-1)*0.001,"source ceil-end profile changed");
    const auto final=clip.SamplePose(double(end)*0.001),last=clip.SamplePose(b.duration);
    for(unsigned i=0;i<52;++i)Need(final.local[i].translation==last.local[i].translation&&final.local[i].rotation==last.local[i].rotation,"ceil-end did not clamp to exact source endpoint");
    double difference=0;int sample=-1;
    for(int frame=0;frame<181;++frame){
        const int ms=int(std::floor((frame+0.37)*b.frameDuration*1000));const double coordinate=double(ms)*0.001/b.frameDuration;
        const int lower=int(std::floor(coordinate));const double t=coordinate-lower;
        const auto a=clip.SamplePose(lower*b.frameDuration),c=clip.SamplePose((lower+1)*b.frameDuration),exact=clip.SamplePose(double(ms)*0.001);
        for(unsigned i=0;i<52;++i){
            double d=0;for(unsigned axis=0;axis<3;++axis)d=(std::max)(d,std::abs(exact.local[i].translation[axis]-((1-t)*a.local[i].translation[axis]+t*c.local[i].translation[axis])));
            const auto q=Slerp(a.local[i].rotation,c.local[i].rotation,t);double dot=0;for(unsigned axis=0;axis<4;++axis)dot+=q[axis]*exact.local[i].rotation[axis];
            d=(std::max)(d,1-std::abs(dot));if(d>difference){difference=d;sample=ms;}
        }
        Need(exact.local.size()==52&&exact.model.size()==52,"fractional source palette incomplete");
    }
    Need(difference>0.00001&&sample>=0,"real source curves did not distinguish direct evaluation from 24-Hz resampling");
    std::cout<<std::setprecision(17)<<"WOLF2009_ANIMATION_SOURCE_OK checks="<<checks<<" joints="<<b.joints<<" frames="<<b.frames
        <<" duration="<<b.duration<<" frame_duration="<<b.frameDuration<<" ceil_end_ms="<<end<<" envelope_bytes="<<serialized.size()
        <<" modl_bytes="<<model.size()<<" hkxa_bytes="<<envelope.animation.size()<<" skel_bytes="<<envelope.skeleton.size()
        <<" subframe_vs_baked_delta="<<difference<<" fractional_ms="<<sample<<" native_runtime=0 retail_actor_clock=0\n";
}
}
int main(int argc,char** argv){try{
    if(argc!=3||std::string(argv[1])!="--source-base")throw std::runtime_error("usage: wolf2009-animation-source --source-base SP/base");
    Run(argv[2]);return 0;
}catch(const std::exception& error){std::cerr<<"WOLF2009_ANIMATION_SOURCE_FAIL "<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnimationWire.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace wolf2009::animation;
namespace {
unsigned checks=0;
void Check(bool condition,const char* text) { ++checks;if(!condition)throw std::runtime_error(text); }
void Reject(const std::function<void()>& action) {bool refused=false;try{action();}catch(const std::exception&){refused=true;}Check(refused,"invalid animation descriptor accepted");}
Envelope Own() {
    Envelope e;auto&b=e.binding;
    b.duration=181.0/24;b.frameDuration=1.0/24;
    b.modelAlias="wolf2009/own/md5r/lod0/0000000000000000/models/own/body";
    b.paths={{"wolf2009/own/modl/models/own/body","wolf2009/own/hkxa/maps/own/clip","wolf2009/skel/skel/maps/own/skeleton"}};
    b.clipName="models/own/clip";b.skeletonName="models/own/skeleton";
    e.animation={1,2,3,4,5};e.skeleton={9,8,7};
    for(unsigned i=0;i<3;++i){b.sources[i].epoch=i==2?2:1;b.sources[i].bytes=i==0?42:i==1?e.animation.size():e.skeleton.size();b.sources[i].fingerprint=3;}
    b.sources[1].fingerprint=Fingerprint(e.animation.data(),e.animation.size());b.sources[2].fingerprint=Fingerprint(e.skeleton.data(),e.skeleton.size());return e;
}
void Put32(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes.at(at+i)=static_cast<unsigned char>(value>>(8*i));}
void Put64(Bytes& bytes,std::size_t at,std::uint64_t value){for(unsigned i=0;i<8;++i)bytes.at(at+i)=static_cast<unsigned char>(value>>(8*i));}
}
int main(){try{
    const auto source=Own();const auto encoded=Serialize(source);const auto result=Parse(encoded);
    Check(Same(source.binding,result.binding)&&source.animation==result.animation&&source.skeleton==result.skeleton,"wire roundtrip lost owned fields/payloads");
    Check(Alias(source.binding)==Alias(result.binding),"animation alias changed after serialization");
    for(std::size_t n=0;n<encoded.size();++n){Bytes truncated(encoded.begin(),encoded.begin()+n);Reject([&]{Parse(truncated);});}
    for(unsigned kind=0;kind<12;++kind){Bytes bad=encoded;
        if(kind==0)bad[0]^=1;
        if(kind==1)Put32(bad,8,2);
        if(kind==2)Put32(bad,12,0);
        if(kind==3)Put32(bad,16,53);
        if(kind==4)Put32(bad,20,181);
        if(kind==5)Put64(bad,24,UINT64_C(0x7ff8000000000000));
        if(kind==6)Put64(bad,40,0);
        if(kind==7)Put64(bad,64,5);
        if(kind==8)Put64(bad,48,UINT64_MAX);
        if(kind==9)Put32(bad,112,0xffffffffu);
        if(kind==10)bad.back()^=1;
        if(kind==11)bad.push_back(0);
        Reject([&]{Parse(bad);});
    }
    for(unsigned kind=0;kind<18;++kind){auto b=source.binding;
        if(kind==0){++b.sources[0].epoch;++b.sources[1].epoch;}
        if(kind==1)++b.sources[0].bytes;
        if(kind==2)++b.sources[0].fingerprint;
        if(kind==3)b.modelAlias+="-other";
        if(kind==4)b.paths[0]+="-other";
        if(kind==5)b.paths[1]+="-other";
        if(kind==6)b.paths[2]+="-other";
        if(kind==7)b.clipName+="-other";
        if(kind==8)b.skeletonName+="-other";
        if(kind==9)b.duration=std::nextafter(b.duration,100.0);
        if(kind==10)b.frameDuration=std::nextafter(b.frameDuration,1.0);
        if(kind==11)++b.sources[2].epoch;
        if(kind==12)++b.sources[1].bytes;
        if(kind==13)++b.sources[2].bytes;
        if(kind==14)++b.sources[1].fingerprint;
        if(kind==15)++b.sources[2].fingerprint;
        if(kind==16){b.paths[0]="wolf2009/other/modl/models/own/body";b.modelAlias="wolf2009/other/md5r/lod0/models/own/body";}
        if(kind==17){b.paths[1]="wolf2009/other/hkxa/maps/own/clip";++b.sources[1].epoch;}
        Check(Alias(b)!=Alias(source.binding)&&!Same(b,source.binding),"a full metadata field is absent from the alias/equality key");
    }
    auto invalid=source;invalid.skeleton.push_back(0);Reject([&]{Serialize(invalid);});
    invalid=source;invalid.binding.paths[1]="wolf2009/own/hkxa/../escape";Reject([&]{Serialize(invalid);});
    invalid=source;invalid.binding.paths[1]="wolf2009/own/modl/wrong";Reject([&]{Serialize(invalid);});
    invalid=source;invalid.binding.clipName="bad\"quote";Reject([&]{Serialize(invalid);});
    std::cout<<"WOLF2009_ANIMATION_WIRE_OK checks="<<checks<<" bytes="<<encoded.size()<<" owns_payloads=1 playable_clip_claim=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"WOLF2009_ANIMATION_WIRE_FAIL "<<e.what()<<'\n';return 1;}}

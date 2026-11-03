// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored metadata/payloads. No playable source clip is implied.
#include "AnimationWireV2.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <vector>
namespace {
using namespace wolf2009::animation2;
unsigned checks=0,groups=0;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void Reject(const std::function<void()>& f){
    bool refused=false;try{f();}catch(const Error&){refused=true;}Check(refused,"invalid wire v2 input accepted");
}
void RejectAs(const std::function<void()>& f,Failure category){
    bool refused=false;try{f();}catch(const Error& e){refused=true;Check(e.Category()==category,"wrong typed wire v2 refusal");}Check(refused,"invalid wire v2 input accepted");
}
std::uint64_t Bits(double value){std::uint64_t bits;std::memcpy(&bits,&value,8);return bits;}
std::uint32_t U32(const Bytes& b,std::size_t at){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(b.at(at+i))<<(8*i);return value;}
std::uint64_t U64(const Bytes& b,std::size_t at){std::uint64_t value=0;for(unsigned i=0;i<8;++i)value|=std::uint64_t(b.at(at+i))<<(8*i);return value;}
void Put32(Bytes& b,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<unsigned char>(value>>(8*i));}
void Put64(Bytes& b,std::size_t at,std::uint64_t value){for(unsigned i=0;i<8;++i)b.at(at+i)=static_cast<unsigned char>(value>>(8*i));}
void Clock(Binding& b,unsigned frames,unsigned maximum,double step){
    b.frames=frames;b.maxFramesPerBlock=maximum;b.blocks=(frames-2)/(maximum-1)+1;
    b.frameDuration=step;b.duration=(frames-1)*step;b.blockDuration=(maximum-1)*step;b.blockInverseDuration=1/b.blockDuration;
}
Envelope Own(unsigned bones=3){
    Envelope e;auto& b=e.binding;b.bones=bones;
    Clock(b,bones==3?7:335,bones==3?4:256,bones==3?.125:.037);
    b.modelAlias="wolf2009/mesh/md5r/v2/lod0/0123456789abcdef/models/own/body";
    b.paths={{"wolf2009/mesh/modl/models/own/body","wolf2009/mesh_rig/skel/maps/own/modelrig",
              "wolf2009/animation/hkxa/maps/own/clip","wolf2009/animation_rig/skel/maps/own/animrig"}};
    b.meshSkeletonName="models/own/Declaration Rig A.hkx";b.clipName="models/own/Clip A.hkx";b.animationSkeletonName="models/own/Animation Rig B.hkx";
    for(unsigned i=0;i<4;++i){b.sources[i].epoch=UINT64_C(0x0102030405060708)+i;b.sources[i].bytes=42+i;b.sources[i].fingerprint=UINT64_C(0x8877665544332211)+i;}
    e.animation={0,1,255,4,8};e.skeleton={9,0,3,5};
    b.sources[2].bytes=e.animation.size();b.sources[2].fingerprint=Fingerprint(e.animation.data(),e.animation.size());
    b.sources[3].bytes=e.skeleton.size();b.sources[3].fingerprint=Fingerprint(e.skeleton.data(),e.skeleton.size());return e;
}
void Equal(const Binding& a,const Binding& b){
    Check(a.visualPolicy==b.visualPolicy&&a.rigPolicy==b.rigPolicy&&a.geometryProfile==b.geometryProfile&&a.lodPolicy==b.lodPolicy&&a.levelOfDetail==b.levelOfDetail,
          "wire v2 lost a policy/profile/LOD");
    Check(a.bones==b.bones&&a.frames==b.frames&&a.blocks==b.blocks&&a.maxFramesPerBlock==b.maxFramesPerBlock,"wire v2 lost count/block metadata");
    Check(Bits(a.duration)==Bits(b.duration)&&Bits(a.frameDuration)==Bits(b.frameDuration)&&Bits(a.blockDuration)==Bits(b.blockDuration)&&
          Bits(a.blockInverseDuration)==Bits(b.blockInverseDuration),"wire v2 changed source timing bits");
    for(unsigned i=0;i<4;++i)Check(a.sources[i].epoch==b.sources[i].epoch&&a.sources[i].bytes==b.sources[i].bytes&&a.sources[i].fingerprint==b.sources[i].fingerprint&&a.paths[i]==b.paths[i],"wire v2 lost a role/path/byte identity");
    Check(a.modelAlias==b.modelAlias&&a.meshSkeletonName==b.meshSkeletonName&&a.clipName==b.clipName&&a.animationSkeletonName==b.animationSkeletonName,
          "wire v2 conflated exact object names/model identity");
}
struct Layout {
    std::array<std::size_t,4> ids,pathLengths,names;
    std::size_t metadata=0,animationLength=0,skeletonLength=0;
    explicit Layout(const Bytes& b){
        // Independent positions from the documented v2 format, not its parser.
        std::size_t pos=80;
        for(unsigned i=0;i<4;++i){ids[i]=pos;pathLengths[i]=pos+24;pos+=28+U32(b,pos+24);}
        for(unsigned i=0;i<4;++i){names[i]=pos;pos+=4+U32(b,pos);}metadata=animationLength=pos;
        skeletonLength=animationLength+4+U32(b,animationLength);
    }
};
void RoundtripAndLayout(){++groups;
    for(unsigned count:{3u,7u}){
        auto original=Own(count);const auto input=Serialize(original);const Layout where(input);const auto result=Parse(input);Equal(original.binding,result.binding);
        Check(original.animation==result.animation&&original.skeleton==result.skeleton,"wire v2 did not preserve the two original payloads");
        Check(Same(original.binding,result.binding)&&Alias(original.binding)==Alias(result.binding)&&Alias(result.binding).find("wolf2009/_animation/anim/v2/")==0,"wire v2 metadata/alias roundtrip failed");
        Check(where.metadata==Metadata(original.binding).size()&&where.skeletonLength+4+original.skeleton.size()==input.size(),"wire v2 framing added an unexpected payload");
        const unsigned char header[48]={'W','2','A','N','I','M','2',0,2,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,
            3,0,0,0,7,0,0,0,2,0,0,0,4,0,0,0};
        if(count==3)Check(std::equal(header,header+48,input.begin()),"wire v2 header field order/endianness differs from schema");
        for(unsigned i=0;i<4;++i){Check(U64(input,where.ids[i])==original.binding.sources[i].epoch&&U64(input,where.ids[i]+8)==original.binding.sources[i].bytes&&U64(input,where.ids[i]+16)==original.binding.sources[i].fingerprint,"wire v2 u64 role layout/endianness");}
        Check(U64(input,48)==Bits(original.binding.duration)&&U64(input,56)==Bits(original.binding.frameDuration)&&U64(input,64)==Bits(original.binding.blockDuration)&&U64(input,72)==Bits(original.binding.blockInverseDuration),"wire v2 timing order/endianness");
        original.binding.meshSkeletonName.clear();original.animation.clear();original.skeleton.clear();
        Check(!result.binding.meshSkeletonName.empty()&&result.animation.size()==5&&result.skeleton.size()==4,"wire v2 result borrowed producer storage");
    }
    Check(Fingerprint(NULL,0)==UINT64_C(14695981039346656037),"empty local FNV seed");
    const unsigned char a='a';Check(Fingerprint(&a,1)==UINT64_C(0xaf63dc4c8601ec8c),"wire v2 FNV known vector");
    RejectAs([]{Fingerprint(NULL,1);},Failure::InvalidData);
}
void Keys(){++groups;const auto original=Own().binding;const auto metadata=Metadata(original);const auto alias=Alias(original);
    std::vector<Binding> changed;
    const auto add=[&](const std::function<void(Binding&)>& f){auto b=original;f(b);changed.push_back(b);};
    add([](Binding& b){b.geometryProfile=GeometryProfile::WeightedOrRigid64;});
    add([](Binding& b){b.lodPolicy=LodPolicy::SingleResourceWithoutLodRecords;});
    add([](Binding& b){b.lodPolicy=LodPolicy::RootAuthoredMeshUnspecifiedSlotV1;});
    add([](Binding& b){b.levelOfDetail=1;b.modelAlias.replace(b.modelAlias.find("/lod0/"),6,"/lod1/");});
    add([](Binding& b){b.bones=7;});add([](Binding& b){Clock(b,6,4,b.frameDuration);});
    add([](Binding& b){Clock(b,10,4,b.frameDuration);});add([](Binding& b){Clock(b,b.frames,5,b.frameDuration);});
    for(unsigned timing=0;timing<4;++timing)add([=](Binding& b){double* values[]={&b.duration,&b.frameDuration,&b.blockDuration,&b.blockInverseDuration};*values[timing]=std::nextafter(*values[timing],100.0);});
    for(unsigned role=0;role<4;++role)for(unsigned fact=0;fact<3;++fact)add([=](Binding& b){auto& source=b.sources[role];if(fact==0)++source.epoch;if(fact==1)++source.bytes;if(fact==2)++source.fingerprint;});
    for(unsigned role=0;role<4;++role)add([=](Binding& b){b.paths[role]+="-other";});
    add([](Binding& b){b.modelAlias+="-other";});add([](Binding& b){b.meshSkeletonName+="-other";});
    add([](Binding& b){b.clipName+="-other";});add([](Binding& b){b.animationSkeletonName+="-other";});
    add([](Binding& b){b.meshSkeletonName[0]='M';});add([](Binding& b){b.animationSkeletonName[0]='M';});
    for(const auto& b:changed)Check(metadata!=Metadata(b)&&alias!=Alias(b)&&!Same(original,b),"wire v2 cache/full metadata omits a declared identity field");
    auto swapped=original;std::swap(swapped.meshSkeletonName,swapped.animationSkeletonName);
    Check(!Same(swapped,original)&&Alias(swapped)!=alias,"wire v2 skeleton roles collapsed");
}
void CountsAndClock(){++groups;const auto input=Own().binding;
    for(unsigned bones:{1u,256u}){auto b=input;b.bones=bones;Check(!Metadata(b).empty(),"supported variable bone count refused");}
    for(unsigned span:{2u,256u}){auto b=input;Clock(b,4096u*(span-1)+1,span,.001);Check(b.blocks==4096&&!Metadata(b).empty(),"upper bounded shared-endpoint domain refused");
        ++b.frames;b.duration=(b.frames-1)*b.frameDuration;RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    for(unsigned fault=0;fault<12;++fault){auto b=input;
        if(fault==0)b.bones=0;if(fault==1)b.bones=257;if(fault==2)b.frames=0;if(fault==3)b.frames=1;
        if(fault==4)b.blocks=0;if(fault==5)b.blocks=4097;if(fault==6)b.maxFramesPerBlock=0;if(fault==7)b.maxFramesPerBlock=1;
        if(fault==8)b.maxFramesPerBlock=257;if(fault==9)b.levelOfDetail=256;if(fault==10)b.frames=UINT32_MAX;if(fault==11)b.blocks=1;
        Reject([&]{Metadata(b);});}
    for(unsigned timing=0;timing<4;++timing)for(double value:{0.0,-1.0,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        auto b=input;double* fields[]={&b.duration,&b.frameDuration,&b.blockDuration,&b.blockInverseDuration};*fields[timing]=value;RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    for(unsigned timing=0;timing<4;++timing){auto b=input;double* fields[]={&b.duration,&b.frameDuration,&b.blockDuration,&b.blockInverseDuration};*fields[timing]*=1.01;RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    {auto b=input;Clock(b,2,2,2147483.647);Check(!Metadata(b).empty(),"representable native millisecond endpoint refused");Clock(b,2,2,2147483.648);RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    {auto b=input;Clock(b,2,2,1e-100);Check(!Metadata(b).empty(),"finite consistent fractional clock refused");}
    {auto b=input;b.frameDuration=(std::numeric_limits<double>::max)();RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    {auto b=input;b.blockInverseDuration=(std::numeric_limits<double>::max)();RejectAs([&]{Metadata(b);},Failure::InvalidData);}
}
void Mount(Binding& b,unsigned role,const char* name){
    const auto begin=b.paths[role].find('/')+1,end=b.paths[role].find('/',begin);b.paths[role].replace(begin,end-begin,name);
    if(role==0){const auto a=b.modelAlias.find('/')+1,z=b.modelAlias.find('/',a);b.modelAlias.replace(a,z-a,name);}
}
void RolesAndNames(){++groups;const auto original=Own();
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j){auto e=original;Mount(e.binding,i,"shared");Mount(e.binding,j,"shared");e.binding.sources[j].epoch=e.binding.sources[i].epoch;
        Check(!Serialize(e).empty(),"consistent shared-label epochs refused");++e.binding.sources[j].epoch;RejectAs([&]{Serialize(e);},Failure::InvalidData);}
    {auto e=original;e.binding.paths[1]=e.binding.paths[3];e.binding.sources[1]=e.binding.sources[3];const auto parsed=Parse(Serialize(e));
        Check(parsed.binding.meshSkeletonName!=parsed.binding.animationSkeletonName&&parsed.binding.paths[1]==parsed.binding.paths[3],"one container forced identical selected skeleton roles");
        ++e.binding.sources[1].bytes;RejectAs([&]{Metadata(e.binding);},Failure::InvalidData);}
    {auto b=original.binding;b.paths[1]=b.paths[3];b.sources[1]=b.sources[3];++b.sources[1].fingerprint;RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    for(unsigned role=0;role<4;++role){auto b=original.binding;b.sources[role].epoch=0;Reject([&]{Metadata(b);});b=original.binding;b.sources[role].bytes=0;Reject([&]{Metadata(b);});b.sources[role].bytes=UINT64_MAX;RejectAs([&]{Metadata(b);},Failure::LimitExceeded);}
    const std::vector<std::string> invalidPaths={"wolf2009/mesh/modl/../escape","/wolf2009/mesh/modl/body","wolf2009/mesh/modl/body/","wolf2009//modl/body","wolf2009/mesh/modl/./body","wolf2009\\mesh\\modl\\body","Wolf2009/mesh/modl/body","wolf2009/mesh/modl/b:ody","wolf2009/mesh/modl/b\nody","wolf2009/_animation/modl/body","other/mesh/modl/body"};
    for(const auto& path:invalidPaths){auto b=original.binding;b.paths[0]=path;Reject([&]{Metadata(b);});}
    for(unsigned role=0;role<4;++role){auto b=original.binding;b.paths[role]="wolf2009/mesh/nope/body";RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    for(const char* name:{"wolf2009/mesh/md5r/lod0/0123456789abcdef/body","wolf2009/other/md5r/v2/lod0/0123456789abcdef/body","wolf2009/mesh/md5r/v2/lod1/0123456789abcdef/body","wolf2009/mesh/md5r/v2/lod0/xyz/body"}){auto b=original.binding;b.modelAlias=name;RejectAs([&]{Metadata(b);},Failure::InvalidData);}
    for(unsigned field=0;field<3;++field)for(unsigned fault=0;fault<4;++fault){auto b=original.binding;std::string* names[]={&b.meshSkeletonName,&b.clipName,&b.animationSkeletonName};
        *names[field]=fault==0?std::string():fault==1?std::string("bad\0name",8):fault==2?"bad\nname":std::string(1,char(127));Reject([&]{Metadata(b);});}
    {auto e=original;e.binding.meshSkeletonName="Rig \"A\"";e.binding.animationSkeletonName="Rig B";Equal(e.binding,Parse(Serialize(e)).binding);}
    for(auto policy:{LodPolicy::SingleResourceWithoutLodRecords,LodPolicy::RootAuthoredMeshUnspecifiedSlotV1}) {
        auto e=original;e.binding.lodPolicy=policy;e.binding.geometryProfile=GeometryProfile::WeightedOrRigid64;
        Equal(e.binding,Parse(Serialize(e)).binding);e.binding.levelOfDetail=1;RejectAs([&]{Serialize(e);},Failure::InvalidData);
    }
}
void CorruptionAndFraming(){++groups;const auto source=Own();const auto encoded=Serialize(source);const Layout where(encoded);
    for(std::size_t n=0;n<encoded.size();++n){const Bytes truncated(encoded.begin(),encoded.begin()+n);Reject([&]{Parse(truncated);});}
    for(unsigned version:{0u,1u,3u,UINT32_MAX}){auto b=encoded;Put32(b,8,version);RejectAs([&]{Parse(b);},Failure::Unsupported);}
    {auto b=encoded;b[6]='1';RejectAs([&]{Parse(b);},Failure::Unsupported);}
    for(std::size_t at:{12u,16u,20u,24u})for(unsigned value:{0u,3u,UINT32_MAX}) {
        auto b=encoded;Put32(b,at,at==24&&value==3?4:value);RejectAs([&]{Parse(b);},Failure::Unsupported);
    }
    for(auto at:where.pathLengths)for(unsigned length:{0u,4097u,UINT32_MAX}){auto b=encoded;Put32(b,at,length);RejectAs([&]{Parse(b);},Failure::LimitExceeded);}
    for(auto at:where.names)for(unsigned length:{0u,4097u,UINT32_MAX}){auto b=encoded;Put32(b,at,length);RejectAs([&]{Parse(b);},Failure::LimitExceeded);}
    for(unsigned role=0;role<4;++role){auto b=encoded;Put64(b,where.ids[role]+8,MaximumSourceBytes+1);RejectAs([&]{Parse(b);},Failure::LimitExceeded);}
    for(auto at:{where.animationLength,where.skeletonLength})for(unsigned length:{0u,4097u,UINT32_MAX}){auto b=encoded;Put32(b,at,length);Reject([&]{Parse(b);});}
    for(auto at:{where.animationLength+4,where.skeletonLength+4}){auto b=encoded;b[at]^=1;RejectAs([&]{Parse(b);},Failure::InvalidData);}
    {auto b=encoded;b.push_back(0);RejectAs([&]{Parse(b);},Failure::InvalidData);}
    {auto e=source;std::swap(e.animation,e.skeleton);RejectAs([&]{Serialize(e);},Failure::InvalidData);}
    for(unsigned role:{2u,3u}){auto e=source;++e.binding.sources[role].bytes;RejectAs([&]{Serialize(e);},Failure::InvalidData);e=source;++e.binding.sources[role].fingerprint;RejectAs([&]{Serialize(e);},Failure::InvalidData);}
    Equal(source.binding,Parse(encoded).binding);Check(source.animation==Parse(encoded).animation,"failed parses changed an earlier independent owned input");
}
void Budgets(){++groups;auto e=Own();
    for(unsigned field=0;field<8;++field){auto b=e.binding;std::string* names[]={&b.paths[0],&b.paths[1],&b.paths[2],&b.paths[3],&b.modelAlias,&b.meshSkeletonName,&b.clipName,&b.animationSkeletonName};names[field]->append(4097,'x');RejectAs([&]{Metadata(b);},Failure::LimitExceeded);}
    for(auto& path:e.binding.paths)path.append(MaximumStringBytes-path.size(),'x');
    e.binding.modelAlias.append(MaximumStringBytes-e.binding.modelAlias.size(),'x');
    e.binding.meshSkeletonName.assign(MaximumStringBytes,'M');e.binding.clipName.assign(MaximumStringBytes,'C');e.binding.animationSkeletonName.assign(MaximumStringBytes,'A');
    const auto encoded=Serialize(e);const auto parsed=Parse(encoded);Equal(e.binding,parsed.binding);
    Check(Metadata(e.binding).size()==80+4*24+8*4+8*4096&&Metadata(e.binding).size()<=MaximumMetadataBytes,"wire v2 maximum admitted string/header budget changed");
    {auto b=e.binding;for(auto& id:b.sources)id.bytes=MaximumSourceBytes;Check(!Metadata(b).empty(),"maximum declared per-source byte budget refused");}
    {auto tooLarge=Own();tooLarge.animation.resize(MaximumSourceBytes+1);tooLarge.binding.sources[2].bytes=MaximumSourceBytes;RejectAs([&]{Serialize(tooLarge);},Failure::LimitExceeded);}
    {Bytes tooLarge(MaximumEnvelopeBytes+1);RejectAs([&]{Parse(tooLarge);},Failure::LimitExceeded);}
}
}
int main(){try{
    RoundtripAndLayout();Keys();CountsAndClock();RolesAndNames();CorruptionAndFraming();Budgets();
    std::cout<<"WOLF2009_ANIMATION_WIRE_V2_OK checks="<<checks<<" groups="<<groups<<" bones=3,7 source_roles=4 original_payloads=2 native_runtime=0 source_clip_qualification=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"WOLF2009_ANIMATION_WIRE_V2_FAIL checks="<<checks<<" groups="<<groups<<" message="<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnimationWireV2.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#ifdef __FAST_MATH__
#error Animation_wire_v2_requires_finite_IEEE_arithmetic
#endif
namespace wolf2009 { namespace animation2 {
namespace {
void Need(bool value,const char* message,Failure failure=Failure::InvalidData){
    if(!value)throw Error(failure,message);
}
void ExactName(const std::string& name){
    Need(!name.empty()&&name.size()<=MaximumStringBytes,"wire v2 exact name byte budget",Failure::LimitExceeded);
    for(unsigned char c:name)Need(c>=32&&c<127,"wire v2 name is not printable exact source text");
}
std::vector<std::string> Parts(const std::string& path){
    Need(!path.empty()&&path.size()<=MaximumStringBytes,"wire v2 path byte budget",Failure::LimitExceeded);
    for(unsigned char c:path)Need(c>=32&&c<127&&!(c>='A'&&c<='Z')&&c!='\\'&&c!=':'&&c!='"'&&c!='#'&&
        c!='('&&c!=')'&&c!='{'&&c!='}'&&c!=',',"wire v2 path is not canonical");
    std::vector<std::string> out;
    std::size_t begin=0;
    for(;;){
        const auto end=path.find('/',begin);const auto part=path.substr(begin,end-begin);
        Need(!part.empty()&&part!="."&&part!="..","wire v2 unsafe path component");
        out.push_back(part);if(end==std::string::npos)break;begin=end+1;
    }
    return out;
}
std::string Typed(const std::string& path,const char* type){
    const auto parts=Parts(path);
    Need(parts.size()>=4&&parts[0]=="wolf2009"&&parts[2]==type,"wire v2 source namespace/FourCC mismatch");
    Need(parts[1]!="_animation","wire v2 source cannot use the descriptor mount namespace");
    return parts[1];
}
std::string Decimal(std::uint32_t value){
    std::string out;do{out.push_back(char('0'+value%10));value/=10;}while(value);
    std::reverse(out.begin(),out.end());return out;
}
bool NearTime(double a,double b){
    if(!std::isfinite(a)||!std::isfinite(b))return false;
    if(a==b)return true;
    const double scale=(std::max)(std::abs(a),std::abs(b));
    return scale>0&&std::abs(a-b)/scale<=2e-5;
}
void Validate(const Binding& b){
    Need(b.visualPolicy==VisualPolicy::OrdinaryVisualNoExtraction&&b.rigPolicy==RigPolicy::CallerIdentityFullLocalPose,
         "wire v2 unknown visual/rig policy",Failure::Unsupported);
    Need(b.geometryProfile==GeometryProfile::Weighted64||b.geometryProfile==GeometryProfile::WeightedOrRigid64,
         "wire v2 unknown geometry profile",Failure::Unsupported);
    Need(b.lodPolicy==LodPolicy::Authored||b.lodPolicy==LodPolicy::SingleResourceWithoutLodRecords||
         b.lodPolicy==LodPolicy::RootAuthoredMeshUnspecifiedSlotV1||b.lodPolicy==LodPolicy::WehrmachtIntactLod0V1,
         "wire v2 unknown LOD policy",Failure::Unsupported);
    Need(b.lodPolicy==LodPolicy::Authored||b.levelOfDetail==0,
         "wire v2 compatibility LOD policy requires slot zero");
    Need(b.levelOfDetail<=255&&b.bones>=1&&b.bones<=256&&b.frames>=2&&b.blocks>=1&&b.blocks<=4096&&
         b.maxFramesPerBlock>=2&&b.maxFramesPerBlock<=256,"wire v2 unsupported count domain",Failure::Unsupported);
    const std::uint64_t expectedBlocks=(std::uint64_t(b.frames)-2)/(b.maxFramesPerBlock-1)+1;
    Need(expectedBlocks==b.blocks,"wire v2 blocks do not cover the shared-endpoint frame domain");
    Need(std::isfinite(b.duration)&&b.duration>0&&std::isfinite(b.frameDuration)&&b.frameDuration>0&&
         std::isfinite(b.blockDuration)&&b.blockDuration>0&&std::isfinite(b.blockInverseDuration)&&b.blockInverseDuration>0,
         "wire v2 nonpositive/nonfinite source timing");
    Need(NearTime(b.duration,(b.frames-1)*b.frameDuration)&&
         NearTime(b.blockDuration,(b.maxFramesPerBlock-1)*b.frameDuration)&&
         NearTime(b.blockDuration*b.blockInverseDuration,1),"wire v2 inconsistent source clock");
    Need(std::ceil(b.duration*1000)<=(std::numeric_limits<int>::max)(),"wire v2 ceil-end exceeds native milliseconds");
    const std::array<std::string,4> mounts{{Typed(b.paths[0],"modl"),Typed(b.paths[1],"skel"),Typed(b.paths[2],"hkxa"),Typed(b.paths[3],"skel")}};
    const auto alias=Parts(b.modelAlias);
    Need(alias.size()>=7&&alias[0]=="wolf2009"&&alias[1]==mounts[0]&&alias[2]=="md5r"&&alias[3]=="v2"&&
         alias[4]=="lod"+Decimal(b.levelOfDetail),"wire v2 model alias is not the declared mesh-v2 namespace/LOD");
    Need(alias[5].size()==16,"wire v2 model alias key width");
    for(char c:alias[5])Need((c>='0'&&c<='9')||(c>='a'&&c<='f'),"wire v2 model alias key is not canonical hex");
    ExactName(b.meshSkeletonName);ExactName(b.clipName);ExactName(b.animationSkeletonName);
    for(unsigned i=0;i<4;++i){
        Need(b.sources[i].epoch&&b.sources[i].bytes&&b.sources[i].bytes<=MaximumSourceBytes,
             "wire v2 source byte identity/epoch budget",Failure::LimitExceeded);
        for(unsigned j=0;j<i;++j){
            Need(mounts[i]!=mounts[j]||b.sources[i].epoch==b.sources[j].epoch,"wire v2 inconsistent epochs for a shared mount");
            Need(b.paths[i]!=b.paths[j]||(b.sources[i].bytes==b.sources[j].bytes&&b.sources[i].fingerprint==b.sources[j].fingerprint),
                 "wire v2 inconsistent byte identities for one shared source path");
        }
    }
}
void U32(Bytes& out,std::uint32_t value){for(unsigned i=0;i<4;++i)out.push_back(static_cast<unsigned char>(value>>(8*i)));}
void U64(Bytes& out,std::uint64_t value){for(unsigned i=0;i<8;++i)out.push_back(static_cast<unsigned char>(value>>(8*i)));}
std::uint64_t Bits(double value){
    static_assert(sizeof(double)==8&&std::numeric_limits<double>::is_iec559,"IEEE64 wire timing required");
    std::uint64_t out;std::memcpy(&out,&value,sizeof(out));return out;
}
void Text(Bytes& out,const std::string& text){U32(out,static_cast<std::uint32_t>(text.size()));out.insert(out.end(),text.begin(),text.end());}
void Payload(const Bytes& bytes,const SourceIdentity& identity){
    Need(!bytes.empty()&&bytes.size()<=MaximumSourceBytes,"wire v2 payload byte budget",Failure::LimitExceeded);
    Need(bytes.size()==identity.bytes&&Fingerprint(bytes.data(),bytes.size())==identity.fingerprint,"wire v2 payload identity mismatch");
}
class Reader {
    const Bytes& bytes;
    std::size_t at=0;
  public:
    explicit Reader(const Bytes& data):bytes(data){}
    void Range(std::size_t count)const{Need(at<=bytes.size()&&count<=bytes.size()-at,"wire v2 truncated descriptor");}
    std::uint32_t U32(){Range(4);std::uint32_t out=0;for(unsigned i=0;i<4;++i)out|=std::uint32_t(bytes[at++])<<(8*i);return out;}
    std::uint64_t U64(){Range(8);std::uint64_t out=0;for(unsigned i=0;i<8;++i)out|=std::uint64_t(bytes[at++])<<(8*i);return out;}
    double Double(){const auto bits=U64();double out;std::memcpy(&out,&bits,8);return out;}
    std::string Text(){
        const auto length=U32();Need(length&&length<=MaximumStringBytes,"wire v2 string byte budget",Failure::LimitExceeded);
        Range(length);Need(at<=MaximumMetadataBytes&&length<=MaximumMetadataBytes-at,"wire v2 metadata byte budget",Failure::LimitExceeded);
        std::string out(bytes.begin()+at,bytes.begin()+at+length);at+=length;return out;
    }
    void Magic(){
        static const unsigned char magic[8]={'W','2','A','N','I','M','2',0};Range(8);
        Need(std::equal(magic,magic+8,bytes.begin()+at),"wire v2 schema magic mismatch",Failure::Unsupported);at+=8;
    }
    Bytes Blob(const SourceIdentity& identity){
        const auto length=U32();Need(length&&length<=MaximumSourceBytes,"wire v2 payload byte budget",Failure::LimitExceeded);
        Need(length==identity.bytes,"wire v2 payload length differs from declared source");Range(length);
        Need(Fingerprint(bytes.data()+at,length)==identity.fingerprint,"wire v2 payload fingerprint mismatch");
        Bytes out(bytes.begin()+at,bytes.begin()+at+length);at+=length;return out;
    }
    bool End()const{return at==bytes.size();}
};
} // namespace
std::uint64_t Fingerprint(const unsigned char* data,std::size_t bytes){
    Need(data||!bytes,"wire v2 null fingerprint input");std::uint64_t hash=UINT64_C(14695981039346656037);
    for(std::size_t i=0;i<bytes;++i)hash=(hash^data[i])*UINT64_C(1099511628211);return hash;
}
Bytes Metadata(const Binding& b){
    Validate(b);Bytes out{'W','2','A','N','I','M','2',0};
    U32(out,2);U32(out,std::uint32_t(b.visualPolicy));U32(out,std::uint32_t(b.rigPolicy));U32(out,std::uint32_t(b.geometryProfile));
    U32(out,std::uint32_t(b.lodPolicy));
    U32(out,b.levelOfDetail);U32(out,b.bones);U32(out,b.frames);U32(out,b.blocks);U32(out,b.maxFramesPerBlock);
    U64(out,Bits(b.duration));U64(out,Bits(b.frameDuration));U64(out,Bits(b.blockDuration));U64(out,Bits(b.blockInverseDuration));
    for(unsigned i=0;i<4;++i){U64(out,b.sources[i].epoch);U64(out,b.sources[i].bytes);U64(out,b.sources[i].fingerprint);Text(out,b.paths[i]);}
    Text(out,b.modelAlias);Text(out,b.meshSkeletonName);Text(out,b.clipName);Text(out,b.animationSkeletonName);
    Need(out.size()<=MaximumMetadataBytes,"wire v2 metadata byte budget",Failure::LimitExceeded);return out;
}
Bytes Serialize(const Envelope& envelope){
    Bytes out=Metadata(envelope.binding);
    Payload(envelope.animation,envelope.binding.sources[2]);Payload(envelope.skeleton,envelope.binding.sources[3]);
    Need(envelope.animation.size()<=MaximumEnvelopeBytes-out.size()-8&&
         envelope.skeleton.size()<=MaximumEnvelopeBytes-out.size()-8-envelope.animation.size(),"wire v2 envelope byte budget",Failure::LimitExceeded);
    out.reserve(out.size()+8+envelope.animation.size()+envelope.skeleton.size());
    U32(out,std::uint32_t(envelope.animation.size()));out.insert(out.end(),envelope.animation.begin(),envelope.animation.end());
    U32(out,std::uint32_t(envelope.skeleton.size()));out.insert(out.end(),envelope.skeleton.begin(),envelope.skeleton.end());return out;
}
Envelope Parse(const Bytes& bytes){
    Need(bytes.size()<=MaximumEnvelopeBytes,"wire v2 envelope byte budget",Failure::LimitExceeded);
    Reader r(bytes);r.Magic();Need(r.U32()==2,"wire v2 unknown schema version",Failure::Unsupported);
    Envelope out;auto& b=out.binding;
    b.visualPolicy=static_cast<VisualPolicy>(r.U32());b.rigPolicy=static_cast<RigPolicy>(r.U32());b.geometryProfile=static_cast<GeometryProfile>(r.U32());
    b.lodPolicy=static_cast<LodPolicy>(r.U32());
    b.levelOfDetail=r.U32();b.bones=r.U32();b.frames=r.U32();b.blocks=r.U32();b.maxFramesPerBlock=r.U32();
    b.duration=r.Double();b.frameDuration=r.Double();b.blockDuration=r.Double();b.blockInverseDuration=r.Double();
    for(unsigned i=0;i<4;++i){b.sources[i].epoch=r.U64();b.sources[i].bytes=r.U64();b.sources[i].fingerprint=r.U64();b.paths[i]=r.Text();}
    b.modelAlias=r.Text();b.meshSkeletonName=r.Text();b.clipName=r.Text();b.animationSkeletonName=r.Text();Validate(b);
    out.animation=r.Blob(b.sources[2]);out.skeleton=r.Blob(b.sources[3]);Need(r.End(),"wire v2 trailing bytes");return out;
}
std::string Alias(const Binding& binding){
    const auto metadata=Metadata(binding);auto value=Fingerprint(metadata.data(),metadata.size());std::string hex(16,'0');
    for(int i=15;i>=0;--i){hex[std::size_t(i)]="0123456789abcdef"[value&15];value>>=4;}
    return "wolf2009/_animation/anim/v2/"+hex+".md5anim";
}
bool Same(const Binding& a,const Binding& b){return Metadata(a)==Metadata(b);}
} }

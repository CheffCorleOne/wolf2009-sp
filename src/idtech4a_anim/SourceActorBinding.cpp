// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceActorBinding.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace wolf2009 { namespace actor {
namespace {
const char Prefix[]="wolf2009/_actor/request/v1/";
const unsigned char RequestMagic[8]={'W','A','C','T','R','0','1',0};
const unsigned char CapabilityMagic[8]={'W','A','C','T','C','0','1',0};
const std::size_t MaximumString=4096,MaximumMetadata=64u*1024u,MaximumSource=64u*1024u*1024u;
void Need(bool yes,const char* text,Failure category=Failure::InvalidData){if(!yes)throw Error(category,text);}
std::string Fold(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c+='a'-'A';return text;}
std::string NameKey(std::string text){text=Fold(text);for(char& c:text)if(c=='\\')c='/';const auto dot=text.rfind('.');if(dot!=std::string::npos)text.resize(dot);return text;}
void Text(const std::string& value,bool empty=false){
    Need((empty||!value.empty())&&value.size()<=MaximumString,"actor binding string length",Failure::LimitExceeded);
    for(unsigned char c:value)Need(c>=32&&c<127,"actor binding requires bounded ASCII strings",Failure::Unsupported);
}
bool Logical(const std::string& value){
    if(value.empty()||value.size()>127)return false;
    for(std::size_t i=0;i<value.size();++i){const unsigned char c=value[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i&&c>='0'&&c<='9')))return false;}
    return true;
}
struct Path {std::string canonical,mount,type,name;};
Path Typed(const std::string& raw,const char* type){
    Text(raw);std::string path=Fold(raw);for(char& c:path)if(c=='\\')c='/';
    for(unsigned char c:path)Need(c>=33&&c<127&&c!=':'&&c!='"'&&c!='{'&&c!='}',"unsafe actor typed resource path");
    const std::string prefix="wolf2009/";Need(path.compare(0,prefix.size(),prefix)==0,"actor resource is not source-qualified");
    const auto a=path.find('/',prefix.size());Need(a!=std::string::npos&&a>prefix.size(),"actor resource mount absent");
    const auto b=path.find('/',a+1);Need(b!=std::string::npos&&b>a+1&&b+1<path.size(),"actor resource type/name absent");
    Path out;out.canonical=path;out.mount=path.substr(prefix.size(),a-prefix.size());out.type=path.substr(a+1,b-a-1);out.name=path.substr(b+1);
    Need(out.type==type,"actor resource has the wrong explicit type");
    for(std::size_t begin=0;begin<path.size();){const auto end=path.find('/',begin);const auto count=(end==std::string::npos?path.size():end)-begin;
        const auto part=path.substr(begin,count);Need(!part.empty()&&part!="."&&part!="..","actor resource has unsafe path segments");
        if(end==std::string::npos)break;begin=end+1;}
    return out;
}
void Expected(const ExpectedSource& source){
    if(source.byteIdentityKnown)Need(source.bytes>0&&source.bytes<=MaximumSource,"actor expected source byte budget",Failure::LimitExceeded);
    else Need(!source.bytes&&!source.fingerprint,"actor unknown byte identity has nonzero metadata");
}
void Actual(const SourceIdentity& source){Need(source.epoch&&source.bytes&&source.bytes<=MaximumSource,"actor capability source identity is absent or too large");}
bool SameSource(const SourceIdentity& a,const SourceIdentity& b){return a.epoch==b.epoch&&a.bytes==b.bytes&&a.fingerprint==b.fingerprint;}
void MatchesExpected(const ExpectedSource& expected,const SourceIdentity& actual){
    Actual(actual);Need(!expected.epoch||expected.epoch==actual.epoch,"actor capability returned a different requested epoch");
    Need(!expected.byteIdentityKnown||(expected.bytes==actual.bytes&&expected.fingerprint==actual.fingerprint),"actor capability returned different requested bytes");
}
void Check(const Request& request){
    for(const auto& field:{request.entityDefinition,request.modelDefinition,request.meshReference,request.skeletonReference})Text(field);
    const auto& mesh=request.resources;
    Need(mesh.clips.empty(),"actor wire contains ambiguous duplicate clip selections");
    const auto model=Typed(mesh.modelResource,"modl"),skeleton=Typed(mesh.meshSkeletonResource,"skel");Text(mesh.meshSkeletonName);
    Need(model.canonical==mesh.modelResource&&skeleton.canonical==mesh.meshSkeletonResource,"actor wire resource paths are not canonical");
    Need(NameKey(model.name)==NameKey(request.meshReference),"actor MODL resource differs from the authored mesh reference");
    Need(mesh.meshSkeletonName==request.skeletonReference,"actor named mesh skeleton must exactly match the authored skeleton scalar");
    Need(mesh.geometryProfile==GeometryProfile::Weighted64||mesh.geometryProfile==GeometryProfile::WeightedOrRigid64,"unsupported actor geometry profile",Failure::Unsupported);
    Need(mesh.lodPolicy==LodPolicy::Authored||mesh.lodPolicy==LodPolicy::SingleResourceWithoutLodRecords||
        mesh.lodPolicy==LodPolicy::RootAuthoredMeshUnspecifiedSlotV1,"unsupported actor LOD policy",Failure::Unsupported);
    Need(mesh.levelOfDetail<=4095&&(mesh.lodPolicy==LodPolicy::Authored||!mesh.levelOfDetail),"actor LOD request is invalid");
    for(const auto& identity:mesh.expected)Expected(identity);
    Need(!request.clips.empty()&&request.clips.size()<=MaximumClips,"actor clip count must be 1..64",Failure::LimitExceeded);
    std::set<std::string> names;
    std::map<std::string,std::uint64_t> epochs;
    const auto epoch=[&](const Path& path,const ExpectedSource& source){if(!source.epoch)return;auto prior=epochs.find(path.mount);
        Need(prior==epochs.end()||prior->second==source.epoch,"actor shared mount requested conflicting epochs");epochs[path.mount]=source.epoch;};
    epoch(model,mesh.expected[0]);epoch(skeleton,mesh.expected[1]);
    for(const auto& clip:request.clips){
        Need(Logical(clip.logicalName)&&names.insert(Fold(clip.logicalName)).second,"actor logical animation names are invalid or ambiguous");
        Text(clip.authoredReference);Text(clip.clipName);Text(clip.animationSkeletonName);
        Need(NameKey(clip.clipName)==NameKey(clip.authoredReference),"explicit actor clip does not share the qualified declaration key");
        Need(clip.treeProfile==TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1,"actor tree profile is not qualified",Failure::Unsupported);
        Need(clip.rigPolicy==RigPolicy::CallerIdentityFullLocalPose,"actor rig mapping profile is unsupported",Failure::Unsupported);
        const auto animation=Typed(clip.animationResource,"hkxa"),rig=Typed(clip.animationSkeletonResource,"skel");
        Need(animation.canonical==clip.animationResource&&rig.canonical==clip.animationSkeletonResource,"actor animation resource paths are not canonical");
        Expected(clip.expected[0]);Expected(clip.expected[1]);epoch(animation,clip.expected[0]);epoch(rig,clip.expected[1]);
        Need(clip.frameSounds.size()<=1,"actor profile supports one authored frame1 sound per clip",Failure::Unsupported);
        for(const auto& sound:clip.frameSounds){Text(sound.sourceSound);
            Need(sound.frame==1&&Logical(sound.soundKey)&&sound.soundKey.compare(0,4,"snd_")==0,"unsupported actor frame command",Failure::Unsupported);}
    }
}
class Writer {
    std::size_t maximum;
  public:
    Bytes bytes;
    explicit Writer(std::size_t limit):maximum(limit){}
    void Raw(const void* data,std::size_t count){Need(count<=maximum-bytes.size(),"actor wire byte budget",Failure::LimitExceeded);if(count){const auto* p=static_cast<const unsigned char*>(data);bytes.insert(bytes.end(),p,p+count);}}
    void U32(std::uint32_t value){unsigned char b[4];for(unsigned i=0;i<4;++i)b[i]=static_cast<unsigned char>(value>>(i*8));Raw(b,4);}
    void U64(std::uint64_t value){unsigned char b[8];for(unsigned i=0;i<8;++i)b[i]=static_cast<unsigned char>(value>>(i*8));Raw(b,8);}
    void String(const std::string& value){Text(value,true);U32(std::uint32_t(value.size()));Raw(value.data(),value.size());}
    void Blob(const Bytes& value){Need(value.size()<=UINT_MAX,"actor blob length overflow");U32(std::uint32_t(value.size()));Raw(value.data(),value.size());}
};
class Reader {
    const Bytes& bytes;std::size_t at=0;
  public:
    Reader(const Bytes& value,std::size_t maximum):bytes(value){Need(value.size()<=maximum,"actor wire input byte budget",Failure::LimitExceeded);}
    void Take(void* out,std::size_t count){Need(count<=bytes.size()-at,"truncated actor wire");if(count)std::memcpy(out,bytes.data()+at,count);at+=count;}
    std::uint32_t U32(){unsigned char b[4];Take(b,4);std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(b[i])<<(i*8);return value;}
    std::uint64_t U64(){unsigned char b[8];Take(b,8);std::uint64_t value=0;for(unsigned i=0;i<8;++i)value|=std::uint64_t(b[i])<<(i*8);return value;}
    std::string String(){const auto size=U32();Need(size<=MaximumString&&size<=bytes.size()-at,"actor wire string length",Failure::LimitExceeded);std::string text(size,'\0');Take(size?&text[0]:NULL,size);Text(text,true);return text;}
    Bytes Blob(std::size_t maximum){const auto size=U32();Need(size<=maximum&&size<=bytes.size()-at,"actor wire blob length",Failure::LimitExceeded);Bytes value(size);Take(value.data(),size);return value;}
    void Magic(const unsigned char (&expected)[8]){unsigned char actual[8];Take(actual,8);Need(std::equal(actual,actual+8,expected),"unknown actor wire schema",Failure::Unsupported);Need(U32()==1,"unknown actor wire version",Failure::Unsupported);}
    void End(){Need(at==bytes.size(),"actor wire has trailing bytes");}
};
void PutExpected(Writer& out,const ExpectedSource& value){out.U32(value.byteIdentityKnown?1:0);out.U64(value.epoch);out.U64(value.bytes);out.U64(value.fingerprint);}
ExpectedSource GetExpected(Reader& in){ExpectedSource value;const auto known=in.U32();Need(known<=1,"actor identity boolean is not canonical");value.byteIdentityKnown=known!=0;value.epoch=in.U64();value.bytes=in.U64();value.fingerprint=in.U64();return value;}
void Check(const Capability& value){
    Check(value.request);Text(value.modelAlias);
    const auto path=Typed(value.modelAlias,"md5r");Need(path.name.compare(0,3,"v2/")==0,"actor capability has no native mesh-v2 alias");
    Need(value.clips.size()==value.request.clips.size(),"actor capability clip count differs");
    std::map<std::string,std::uint64_t> epochs;
    std::map<std::string,SourceIdentity> identities;
    for(std::size_t i=0;i<value.clips.size();++i){const auto& clip=value.clips[i];const auto& request=value.request.clips[i];
        Text(clip.animationAlias);Need(clip.animationAlias.compare(0,28,"wolf2009/_animation/anim/v2/")==0,"actor capability has no native animation-v2 alias");
        Need(!clip.animationMetadata.empty()&&clip.animationMetadata.size()<=MaximumMetadata,"actor animation metadata budget",Failure::LimitExceeded);
        Need(clip.frames>=2&&clip.frames<=1000000&&clip.bones>=1&&clip.bones<=256&&clip.lengthMs>=1&&clip.lengthMs<=INT_MAX,"actor capability clip domain");
        const std::string paths[]={value.request.resources.modelResource,value.request.resources.meshSkeletonResource,request.animationResource,request.animationSkeletonResource};
        const ExpectedSource expected[]={value.request.resources.expected[0],value.request.resources.expected[1],request.expected[0],request.expected[1]};
        for(unsigned role=0;role<4;++role){const auto& source=clip.sources[role];MatchesExpected(expected[role],source);
            const auto parsed=Typed(paths[role],role==0?"modl":role==2?"hkxa":"skel");const auto prior=epochs.find(parsed.mount);
            Need(prior==epochs.end()||prior->second==source.epoch,"actor capability shared mount has different epochs");epochs[parsed.mount]=source.epoch;
            const auto previous=identities.find(paths[role]);Need(previous==identities.end()||SameSource(previous->second,source),"actor capability same source has different bytes");identities[paths[role]]=source;}
        if(i)Need(clip.bones==value.clips[0].bones,"actor clips have different bone counts");
    }
}
std::string Hex(std::uint64_t value){std::string result(16,'0');for(int n=15;n>=0;--n){result[std::size_t(n)]="0123456789abcdef"[value&15];value>>=4;}return result;}
std::uint64_t Fingerprint(const Bytes& bytes){std::uint64_t value=UINT64_C(14695981039346656037);for(unsigned char c:bytes){value^=c;value*=UINT64_C(1099511628211);}return value;}
#ifndef WOLF2009_SOURCE_ACTOR_CORE
std::string Scalar(const SourceResolvedLiteral* value,const char* reason){
    Need(value&&(value->encoding==SourceLiteralEncoding::PlainText||value->encoding==SourceLiteralEncoding::CompiledString)&&value->value.size()==1,reason,Failure::Unsupported);
    Text(value->value[0].text);return value->value[0].text;
}
bool SameEntry(const CompiledCatalogEntry& a,const CompiledCatalogEntry& b){
    return a.IsUnmodified()&&b.IsUnmodified()&&a.document==b.document&&a.source==b.source&&a.loadOrdinal==b.loadOrdinal&&a.documentOrdinal==b.documentOrdinal&&
        a.catalogOrdinal==b.catalogOrdinal&&a.section==b.section&&a.group==b.group&&a.record==b.record&&a.type==b.type&&a.name==b.name&&
        a.typeSpan.begin==b.typeSpan.begin&&a.typeSpan.end==b.typeSpan.end&&a.nameSpan.begin==b.nameSpan.begin&&a.nameSpan.end==b.nameSpan.end&&
        a.bodySpan.begin==b.bodySpan.begin&&a.bodySpan.end==b.bodySpan.end&&a.envelopeSpan.begin==b.envelopeSpan.begin&&a.envelopeSpan.end==b.envelopeSpan.end;
}
void Provenance(const SourceDefinitionProvenance& origin,const CompiledDeclarationNamespace& names){
    const auto entry=origin.member.compiledRecord;Need(bool(entry)&&entry->IsUnmodified(),"actor requires owning selected compiled declarations");
    const auto found=names.Lookup(origin.declarationKind,origin.declarationName);
    Need(found.status==CompiledCatalogLookupStatus::Unique&&found.matches.size()==1&&SameEntry(found.matches[0],*entry),"actor definition is not the selected compiled source");
}
std::vector<FrameSound> Commands(const SourceModelAnimationReference& reference,const ResolvedSourceDefinition& entity){
    Need(reference.hasLiteralAssetReference&&!reference.origins.empty(),"actor animation has no literal source reference",Failure::Unsupported);
    std::vector<FrameSound> result;
    for(std::size_t n=0;n<reference.origins.size();++n){const auto& values=reference.origins[n].data.values;std::vector<FrameSound> commands;
        if(reference.shape==SourceAnimationReferenceShape::SingleLiteralNoTail)Need(values.size()==2,"actor pure animation has extra tokens");
        else if(reference.shape==SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock){
            Need(values.size()==4||values.size()==8,"actor animation options/frame commands are unsupported",Failure::Unsupported);
            Need(values[2].brace=='{'&&values.back().brace=='}',"actor animation block framing changed");
            if(values.size()==8){Need(!values[3].brace&&values[3].text=="frame"&&!values[4].brace&&values[4].text=="1"&&
                !values[5].brace&&values[5].text=="sound"&&!values[6].brace&&Logical(values[6].text)&&values[6].text.compare(0,4,"snd_")==0,
                "actor admits only authored frame 1 sound snd_*",Failure::Unsupported);
                FrameSound sound;sound.soundKey=values[6].text;sound.sourceSound=Scalar(entity.Find(sound.soundKey),"actor frame sound key has no qualified source string");commands.push_back(std::move(sound));}
        }else Need(false,"actor animation reference shape is unsupported",Failure::Unsupported);
        if(!n)result=commands;
        else Need(commands.size()==result.size()&&(commands.empty()||(commands[0].soundKey==result[0].soundKey&&commands[0].sourceSound==result[0].sourceSound)),"actor inherited frame sounds conflict");
    }
    return result;
}
#endif


}

const char* RequestPrefix(){return Prefix;}
std::size_t MaximumRequestPathBytes(){return sizeof(Prefix)-1+2*MaximumRequestBytes;}
Bytes SerializeRequest(const Request& value){
    Check(value);Writer out(MaximumRequestBytes);out.Raw(RequestMagic,8);out.U32(1);
    const auto& mesh=value.resources;out.U32(unsigned(mesh.geometryProfile));out.U32(unsigned(mesh.lodPolicy));out.U32(mesh.levelOfDetail);out.U32(std::uint32_t(value.clips.size()));
    for(const auto& field:{value.entityDefinition,value.modelDefinition,value.meshReference,value.skeletonReference,mesh.modelResource,mesh.meshSkeletonResource,mesh.meshSkeletonName})out.String(field);
    for(const auto& source:mesh.expected)PutExpected(out,source);
    for(const auto& clip:value.clips){out.U32(unsigned(clip.treeProfile));out.U32(unsigned(clip.rigPolicy));
        for(const auto& field:{clip.logicalName,clip.authoredReference,clip.animationResource,clip.animationSkeletonResource,clip.clipName,clip.animationSkeletonName})out.String(field);
        for(const auto& source:clip.expected)PutExpected(out,source);out.U32(std::uint32_t(clip.frameSounds.size()));
        for(const auto& sound:clip.frameSounds){out.U32(sound.frame);out.String(sound.soundKey);out.String(sound.sourceSound);}}
    return std::move(out.bytes);
}
Request ParseRequest(const Bytes& bytes){
    Reader in(bytes,MaximumRequestBytes);in.Magic(RequestMagic);Request value;auto& mesh=value.resources;
    mesh.geometryProfile=static_cast<GeometryProfile>(in.U32());mesh.lodPolicy=static_cast<LodPolicy>(in.U32());mesh.levelOfDetail=in.U32();const auto count=in.U32();
    Need(count>=1&&count<=MaximumClips,"actor wire clip count",Failure::LimitExceeded);
    value.entityDefinition=in.String();value.modelDefinition=in.String();value.meshReference=in.String();value.skeletonReference=in.String();
    mesh.modelResource=in.String();mesh.meshSkeletonResource=in.String();mesh.meshSkeletonName=in.String();
    for(auto& source:mesh.expected)source=GetExpected(in);
    value.clips.reserve(count);for(unsigned i=0;i<count;++i){RequestClip clip;clip.treeProfile=static_cast<TreeProfile>(in.U32());clip.rigPolicy=static_cast<RigPolicy>(in.U32());
        clip.logicalName=in.String();clip.authoredReference=in.String();clip.animationResource=in.String();clip.animationSkeletonResource=in.String();clip.clipName=in.String();clip.animationSkeletonName=in.String();
        for(auto& source:clip.expected)source=GetExpected(in);const auto commands=in.U32();Need(commands<=1,"actor wire frame command count",Failure::Unsupported);
        for(unsigned j=0;j<commands;++j){FrameSound sound;sound.frame=in.U32();sound.soundKey=in.String();sound.sourceSound=in.String();clip.frameSounds.push_back(std::move(sound));}value.clips.push_back(std::move(clip));}
    in.End();Check(value);return value;
}
std::string RequestPath(const Request& request){const auto bytes=SerializeRequest(request);std::string result(Prefix);result.reserve(sizeof(Prefix)-1+2*bytes.size());for(unsigned char c:bytes){result.push_back("0123456789abcdef"[c>>4]);result.push_back("0123456789abcdef"[c&15]);}return result;}
Request ParseRequestPath(const std::string& path){
    Need(path.size()<=MaximumRequestPathBytes(),"actor request path budget",Failure::LimitExceeded);const std::size_t prefix=sizeof(Prefix)-1;
    Need(path.compare(0,prefix,Prefix)==0&&path.size()>prefix&&(path.size()-prefix)%2==0,"actor request path prefix/hex length");
    Bytes bytes((path.size()-prefix)/2);const auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw Error(Failure::InvalidData,"actor request hex is not canonical");};
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>((digit(path[prefix+2*i])<<4)|digit(path[prefix+2*i+1]));return ParseRequest(bytes);
}
Bytes SerializeCapability(const Capability& value){
    Check(value);Writer out(MaximumCapabilityBytes);out.Raw(CapabilityMagic,8);out.U32(1);out.Blob(SerializeRequest(value.request));out.String(value.modelAlias);out.U32(std::uint32_t(value.clips.size()));
    for(const auto& clip:value.clips){out.String(clip.animationAlias);out.Blob(clip.animationMetadata);out.U32(clip.frames);out.U32(clip.bones);out.U32(clip.lengthMs);
        for(const auto& source:clip.sources){out.U64(source.epoch);out.U64(source.bytes);out.U64(source.fingerprint);}}
    return std::move(out.bytes);
}
Capability ParseCapability(const Bytes& bytes){
    Reader in(bytes,MaximumCapabilityBytes);in.Magic(CapabilityMagic);Capability value;value.request=ParseRequest(in.Blob(MaximumRequestBytes));value.modelAlias=in.String();const auto count=in.U32();
    Need(count==value.request.clips.size(),"actor capability wire count differs");value.clips.reserve(count);
    for(unsigned i=0;i<count;++i){CapabilityClip clip;clip.animationAlias=in.String();clip.animationMetadata=in.Blob(MaximumMetadata);clip.frames=in.U32();clip.bones=in.U32();clip.lengthMs=in.U32();
        for(auto& source:clip.sources){source.epoch=in.U64();source.bytes=in.U64();source.fingerprint=in.U64();}value.clips.push_back(std::move(clip));}
    in.End();Check(value);return value;
}
std::string CapabilityName(const Capability& value){return "wolf2009/_actor/binding/v1/"+Hex(Fingerprint(SerializeCapability(value)));}
bool Same(const Request& a,const Request& b){return SerializeRequest(a)==SerializeRequest(b);}
CycleSample CycleAtMilliseconds(int elapsedMs,int cycles,double duration){
    Need(std::isfinite(duration)&&duration>0&&duration*1000.0<=INT_MAX,"invalid source cycle duration");
    Need(cycles==-1||cycles>=1,"invalid source cycle count",Failure::Unsupported);
    CycleSample result;if(elapsedMs<=0)return result;const double time=double(elapsedMs)*0.001;
    if(cycles>0&&time>=duration*double(cycles)){result.seconds=duration;result.completedCycles=cycles-1;result.finished=true;return result;}
    const double completed=std::floor(time/duration);
    Need(std::isfinite(completed)&&completed>=0&&completed<=INT_MAX,"source cycle index exceeds native frame domain",Failure::LimitExceeded);
    result.completedCycles=int(completed);result.seconds=std::fmod(time,duration);
    Need(std::isfinite(result.seconds)&&result.seconds>=0&&result.seconds<duration,"source cycle remainder is invalid");return result;
}

#ifndef WOLF2009_SOURCE_ACTOR_CORE
struct Plan::Impl {
    Request request;ResolvedSourceDefinition entity;ResolvedSourceModelAnimations model;SourceDefinitionContext context;
    std::vector<CompiledNamespaceApplication> inputs;std::vector<PlanClip> clips;
};
Plan::Plan(std::shared_ptr<const Impl> value):impl_(std::move(value)){}
const Request& Plan::ResourceRequest()const{Need(Valid(),"empty actor plan");return impl_->request;}
const ResolvedSourceDefinition& Plan::Entity()const{Need(Valid(),"empty actor plan");return impl_->entity;}
const ResolvedSourceModelAnimations& Plan::Model()const{Need(Valid(),"empty actor plan");return impl_->model;}
const SourceDefinitionContext& Plan::Context()const{Need(Valid(),"empty actor plan");return impl_->context;}
const std::vector<CompiledNamespaceApplication>& Plan::SelectedInputs()const{Need(Valid(),"empty actor plan");return impl_->inputs;}
std::size_t Plan::ClipCount()const{Need(Valid(),"empty actor plan");return impl_->clips.size();}
const PlanClip& Plan::Clip(std::size_t i)const{Need(Valid()&&i<impl_->clips.size(),"actor plan clip index");return impl_->clips[i];}
const PlanClip* Plan::FindClip(const std::string& name)const{Need(Valid(),"empty actor plan");for(const auto& clip:impl_->clips)if(clip.request.logicalName==name)return &clip;return NULL;}
bool Plan::MatchesEntityModel(const std::string& entity,const std::string& model,const SourceDefinitionContext& context)const{
    return Valid()&&NameKey(entity)==NameKey(impl_->request.entityDefinition)&&NameKey(model)==NameKey(impl_->request.modelDefinition)&&context.policy==impl_->context.policy&&context.mapModelDefinition==impl_->context.mapModelDefinition;
}
Plan Prepare(const SourceDefinitions& definitions,const CompiledDeclarationNamespace& names,const std::string& entityName,
    const std::string& effectiveModel,const SourceDefinitionContext& context,const ResourceSelection& resources){
    Text(entityName);Text(context.mapModelDefinition,true);Need(context.policy==SourceDefinitionPolicy::AsciiCaseFoldLiteralChildOverridesV1,"actor inheritance profile is unsupported",Failure::Unsupported);
    std::shared_ptr<Plan::Impl> value(new Plan::Impl);value->context=context;value->entity=definitions.Resolve(SourceDefinitionKind::EntityDef,entityName,context);
    for(const auto& origin:value->entity.declarations)Provenance(origin,names);
    const auto modelName=effectiveModel.empty()?Scalar(value->entity.Find("model"),"actor entity has no qualified model string"):effectiveModel;Text(modelName);
    value->model=definitions.ResolveModelAnimations(modelName,context);for(const auto& origin:value->model.model.declarations)Provenance(origin,names);
    auto& request=value->request;request.entityDefinition=value->entity.selected.declarationName;request.modelDefinition=value->model.model.selected.declarationName;
    request.meshReference=Scalar(value->model.model.Find("mesh"),"actor model mesh is not a qualified literal");
    request.skeletonReference=Scalar(value->model.model.Find("skeleton"),"actor model skeleton is not a qualified literal");
    if(const auto* skin=value->model.model.Find("skin"))Need(Scalar(skin,"actor model skin is not a qualified string").empty(),"actor authored appearance needs an explicit appearance binding profile",Failure::Unsupported);
    Need(!value->model.model.Find("offset"),"actor model offset needs an explicit native offset profile",Failure::Unsupported);
    for(const auto& fact:value->model.model.opaqueFacts)Need(Fold(fact.data.key.text)=="anim","actor model has an unsupported non-animation command",Failure::Unsupported);
    request.resources=resources;request.resources.clips.clear();request.resources.modelResource=Typed(resources.modelResource,"modl").canonical;request.resources.meshSkeletonResource=Typed(resources.meshSkeletonResource,"skel").canonical;
    Need(!resources.clips.empty()&&resources.clips.size()<=MaximumClips,"actor selected clip count",Failure::LimitExceeded);
    for(const auto& selection:resources.clips){const auto* reference=value->model.Find(selection.logicalName);
        Need(reference&&reference->logicalName.text==selection.logicalName,"actor requires an exact authored logical animation name");
        Need(selection.treeProfile==TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1,"actor requires an explicit selected-missing default-tree policy",Failure::Unsupported);
        const auto tree=names.Lookup("animBlendTree",reference->assetReference.text);
        Need(tree.status==CompiledCatalogLookupStatus::Missing&&tree.matches.empty(),"actor selected explicit animation tree needs a qualified tree evaluator",Failure::Unsupported);
        PlanClip clip;static_cast<ClipSelection&>(clip.request)=selection;clip.request.authoredReference=reference->assetReference.text;
        clip.request.animationResource=Typed(selection.animationResource,"hkxa").canonical;clip.request.animationSkeletonResource=Typed(selection.animationSkeletonResource,"skel").canonical;
        clip.request.frameSounds=Commands(*reference,value->entity);clip.declaration=*reference;clip.commandOrigins=reference->origins;
        for(const auto& sound:clip.request.frameSounds){const auto selected=names.Lookup("sfx",sound.sourceSound);
            Need(selected.status==CompiledCatalogLookupStatus::Unique&&selected.matches.size()==1&&selected.matches[0].IsUnmodified(),"actor frame sound has no unique selected source SFX declaration",Failure::Unsupported);
            clip.soundDeclarations.push_back(selected.matches[0]);}
        request.clips.push_back(clip.request);value->clips.push_back(std::move(clip));}
    SerializeRequest(request); // All budgets/admission gates before publication.
    Need(names.ApplicationCount()<=256,"actor selected input application budget",Failure::LimitExceeded);
    for(std::size_t i=0;i<names.ApplicationCount();++i)value->inputs.push_back(names.Application(i));
    return Plan(std::move(value));
}
#endif
} }

bool Wolf2009_IsSourceActorRequest(const char* path){
    if(!path)return false;const char* prefix=wolf2009::actor::RequestPrefix();
    for(std::size_t i=0;prefix[i];++i){if(!path[i])return false;char c=path[i];if(c=='\\')c='/';if(c>='A'&&c<='Z')c+='a'-'A';if(c!=prefix[i])return false;}return true;
}

#if defined(WOLF2009_SOURCE_ACTOR_CORE) || defined(WOLF2009_SOURCE_ACTOR_GAME)
#include "AnimationWireV2.h"
#include <cmath>
#ifdef WOLF2009_SOURCE_ACTOR_CORE
#include "EngineSkinnedModelData.h"
#include "SourceRigAnimation.h"
#endif
#include "idlib/precompiled.h"
#ifdef WOLF2009_SOURCE_ACTOR_GAME
#include "game/Game_local.h"
#include "EngineAnimationSet.h"
#include "SourceSoundBinding.h"
#else
#include <pthread.h>
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "EngineAnimationBindingV2.h"
#endif
namespace {
namespace actor=wolf2009::actor;
namespace wire=wolf2009::animation2;
void NativeNeed(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct CloseActorFile {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
typedef std::unique_ptr<idFile,CloseActorFile> ActorFile;
actor::Bytes ReadActorFile(idFile* file,std::size_t maximum){
    NativeNeed(file!=NULL,"source actor readonly file is absent");const int length=file->Length();
    NativeNeed(length>0&&std::size_t(length)<=maximum&&file->Tell()==0,"source actor readonly file size/position");
    actor::Bytes bytes(static_cast<std::size_t>(length));std::size_t at=0;
    while(at<bytes.size()){const int count=file->Read(bytes.data()+at,int(bytes.size()-at));NativeNeed(count>0&&std::size_t(count)<=bytes.size()-at,"short source actor readonly file");at+=std::size_t(count);}
    unsigned char extra=0;NativeNeed(file->Read(&extra,1)==0&&file->Tell()==length&&file->Length()==length,"source actor readonly file EOF/identity changed");return bytes;
}
wire::Envelope ActorAnimationEnvelope(const char* alias){ActorFile file(fileSystem->OpenFileRead(alias,false));return wire::Parse(ReadActorFile(file.get(),wire::MaximumEnvelopeBytes));}
void ActorAnimationMatches(const actor::Capability& capability,std::size_t index,const wire::Binding& binding){
    const auto& request=capability.request;const auto& mesh=request.resources;const auto& selected=request.clips.at(index);const auto& actual=capability.clips.at(index);
    NativeNeed(wire::Alias(binding)==actual.animationAlias&&wire::Metadata(binding)==actual.animationMetadata,"source actor V2 descriptor metadata/full alias differs");
    NativeNeed(binding.modelAlias==capability.modelAlias&&binding.meshSkeletonName==mesh.meshSkeletonName&&binding.clipName==selected.clipName&&
        binding.animationSkeletonName==selected.animationSkeletonName&&binding.levelOfDetail==mesh.levelOfDetail&&
        unsigned(binding.geometryProfile)==unsigned(mesh.geometryProfile)&&unsigned(binding.lodPolicy)==unsigned(mesh.lodPolicy)&&
        binding.visualPolicy==wire::VisualPolicy::OrdinaryVisualNoExtraction&&unsigned(binding.rigPolicy)==unsigned(selected.rigPolicy),"source actor V2 source selection/profile differs");
    const std::string paths[]={mesh.modelResource,mesh.meshSkeletonResource,selected.animationResource,selected.animationSkeletonResource};
    for(unsigned role=0;role<4;++role){const auto& a=actual.sources[role];const auto& b=binding.sources[role];
        NativeNeed(binding.paths[role]==paths[role]&&a.epoch==b.epoch&&a.bytes==b.bytes&&a.fingerprint==b.fingerprint,"source actor V2 four-role byte/epoch identity differs");}
    NativeNeed(binding.frames==actual.frames&&binding.bones==actual.bones&&std::ceil(binding.duration*1000.0)==actual.lengthMs,"source actor V2 count/ceil-end differs");
}
}
#endif

#ifdef WOLF2009_SOURCE_ACTOR_CORE
namespace {
namespace render=wolf2009::idtech4a;
class ActorLeases {
    std::map<std::string,unsigned long long> held;
  public:
    ~ActorLeases(){for(const auto& lease:held)Wolf2009_UnpinMount(lease.first.c_str(),lease.second);}
    void Pin(const std::string& path,const actor::ExpectedSource& expected){
        const auto label=render::ParseResourcePath(path).mount;const auto prior=held.find(label);
        if(prior!=held.end()){NativeNeed(!expected.epoch||expected.epoch==prior->second,"source actor shared mount epoch differs");return;}
        unsigned long long epoch=0;NativeNeed(Wolf2009_PinMount(label.c_str(),epoch)&&epoch,"source actor mount cannot be pinned");
        if(expected.epoch&&expected.epoch!=epoch){Wolf2009_UnpinMount(label.c_str(),epoch);throw std::runtime_error("source actor requested stale mount epoch");}
        try{held.insert(std::make_pair(label,epoch));}catch(...){Wolf2009_UnpinMount(label.c_str(),epoch);throw;}
    }
    unsigned long long Epoch(const std::string& path)const{const auto found=held.find(render::ParseResourcePath(path).mount);NativeNeed(found!=held.end(),"source actor unleased path");return found->second;}
    void Check()const{for(const auto& lease:held){unsigned long long epoch=0;NativeNeed(Wolf2009_MountEpoch(lease.first.c_str(),epoch)&&epoch==lease.second,"source actor leased mount changed");}}
};
class ActorSources {
    std::map<std::string,actor::Bytes> files;
    std::size_t total=0;
  public:
    const actor::Bytes& Read(const std::string& path){const auto prior=files.find(path);if(prior!=files.end())return prior->second;
        ActorFile file(fileSystem->OpenFileRead(path.c_str(),false));NativeNeed(file.get()!=NULL,"source actor exact typed resource is absent");
        const int length=file->Length();NativeNeed(length>0&&std::size_t(length)<=wire::MaximumSourceBytes&&std::size_t(length)<=(128u<<20)-total,"source actor selected source storage budget");
        auto bytes=ReadActorFile(file.get(),wire::MaximumSourceBytes);total+=bytes.size();return files.insert(std::make_pair(path,std::move(bytes))).first->second;}
    actor::SourceIdentity Identity(const std::string& path,const actor::ExpectedSource& expected,const ActorLeases& leases){
        const auto& bytes=Read(path);actor::SourceIdentity identity;identity.epoch=leases.Epoch(path);identity.bytes=bytes.size();identity.fingerprint=wire::Fingerprint(bytes.data(),bytes.size());
        NativeNeed((!expected.epoch||expected.epoch==identity.epoch)&&(!expected.byteIdentityKnown||(expected.bytes==identity.bytes&&expected.fingerprint==identity.fingerprint)),"source actor expected byte identity differs");return identity;
    }
};
class ActorCapabilityFile : public idFile {
    const std::string name;
    const actor::Bytes bytes;
    std::unique_ptr<ActorLeases> leases;
    std::size_t position=0;
  public:
    ActorCapabilityFile(std::string identity,actor::Bytes data,std::unique_ptr<ActorLeases> pins):name(std::move(identity)),bytes(std::move(data)),leases(std::move(pins)){}
    const char* GetName()override{return name.c_str();}
    const char* GetFullPath()override{return name.c_str();}
    int Length()override{return int(bytes.size());}
    int Tell()override{return int(position);}
    ID_TIME_T Timestamp()override{return 0;}
    int Read(void* out,int count)override{if(!out||count<=0)return 0;const auto take=(std::min)(std::size_t(count),bytes.size()-position);if(take)memcpy(out,bytes.data()+position,take);position+=take;return int(take);}
    int Seek(long offset,fsOrigin_t origin)override{const long long base=origin==FS_SEEK_SET?0:origin==FS_SEEK_CUR?static_cast<long long>(position):origin==FS_SEEK_END?static_cast<long long>(bytes.size()):-1;
        if(base<0||offset<-base||offset>static_cast<long long>(bytes.size())-base)return -1;position=std::size_t(base+offset);return 0;}
};
}
idFile* Wolf2009_OpenSourceActorBinding(const char* path){
    try{
        NativeNeed(pthread_equal(XTHREAD_ID_UNWRAP(Sys_GetMainThread()),pthread_self())!=0,"source actor capability requires the native model-manager owner thread");
        NativeNeed(Wolf2009_IsSourceActorRequest(path),"not a source actor request");std::size_t length=0;const auto maximum=actor::MaximumRequestPathBytes();while(length<=maximum&&path[length])++length;
        NativeNeed(length<=maximum,"source actor virtual request path budget");const auto request=actor::ParseRequestPath(std::string(path,length));
        std::unique_ptr<ActorLeases> pins(new ActorLeases);const auto& selected=request.resources;
        pins->Pin(selected.modelResource,selected.expected[0]);pins->Pin(selected.meshSkeletonResource,selected.expected[1]);
        for(const auto& clip:request.clips){pins->Pin(clip.animationResource,clip.expected[0]);pins->Pin(clip.animationSkeletonResource,clip.expected[1]);}
        ActorSources sources;auto meshRequest=render::MakeSkinnedMeshRequestV2(selected.modelResource,selected.meshSkeletonResource,selected.meshSkeletonName,selected.levelOfDetail,
            static_cast<wolf2009::SkinnedVertexProfile>(unsigned(selected.geometryProfile)),static_cast<wolf2009::SkinnedLodPolicy>(unsigned(selected.lodPolicy)));
        meshRequest.sourceEpochs={{pins->Epoch(selected.modelResource),pins->Epoch(selected.meshSkeletonResource)}};
        const auto mesh=render::LoadSkinnedMeshV2(meshRequest,[&](const std::string& source){return sources.Read(source);});
        actor::Capability capability;capability.request=request;
        // All source/profile/hierarchy gates precede native registrations. A
        // later native allocation failure can leave ordinary unused caches;
        // no partial actor or successful capability is published.
        for(const auto& selectedClip:request.clips){actor::CapabilityClip clip;
            clip.sources[0]=sources.Identity(selected.modelResource,selected.expected[0],*pins);clip.sources[1]=sources.Identity(selected.meshSkeletonResource,selected.expected[1],*pins);
            clip.sources[2]=sources.Identity(selectedClip.animationResource,selectedClip.expected[0],*pins);clip.sources[3]=sources.Identity(selectedClip.animationSkeletonResource,selectedClip.expected[1],*pins);
            wolf2009::SourceRigRequest rigRequest;rigRequest.clipName=selectedClip.clipName;rigRequest.skeletonName=selectedClip.animationSkeletonName;rigRequest.policy=wolf2009::SourceRigPolicy::CallerIdentityFullLocalPoseV1;
            const auto rig=wolf2009::SourceRigAnimation::Load(sources.Read(selectedClip.animationResource),sources.Read(selectedClip.animationSkeletonResource),rigRequest);
            NativeNeed(rig.Skeleton().joints.size()==mesh.joints.size(),"source actor animation/mesh hierarchy counts differ");
            for(std::size_t i=0;i<mesh.joints.size();++i)NativeNeed(rig.Skeleton().joints[i].name==mesh.joints[i].name&&rig.Skeleton().joints[i].parent==mesh.joints[i].parent,"source actor animation/mesh full named hierarchy differs");
            NativeNeed(rig.FrameCount()>=2&&rig.FrameCount()<=1000000&&std::isfinite(rig.Duration())&&rig.Duration()>0&&std::ceil(rig.Duration()*1000.0)<=INT_MAX,"source actor clip count/ceil-end domain");
            clip.frames=std::uint32_t(rig.FrameCount());clip.bones=std::uint32_t(mesh.joints.size());clip.lengthMs=std::uint32_t(std::ceil(rig.Duration()*1000.0));capability.clips.push_back(std::move(clip));}
        pins->Check();idStr modelAlias;
        NativeNeed(Wolf2009_RegisterSkinnedMeshV2(selected.modelResource.c_str(),selected.meshSkeletonResource.c_str(),selected.meshSkeletonName.c_str(),int(selected.levelOfDetail),int(selected.geometryProfile),int(selected.lodPolicy),modelAlias),"source actor native mesh registration failed");
        capability.modelAlias=modelAlias.c_str();NativeNeed(capability.modelAlias==render::SkinnedMeshCacheNameV2(meshRequest),"source actor native mesh full request alias differs");
        auto* model=dynamic_cast<idRenderModelMD5*>(renderModelManager->CheckModel(modelAlias));render::SkinnedMeshSourceInfoV2 meshSource;
        NativeNeed(model&&model->IsLoaded()&&!model->IsDefaultModel()&&Wolf2009_GetSkinnedMeshSourcesV2(model,meshSource)&&render::SameSkinnedMeshRequestV2(meshRequest,meshSource.request),"source actor actual native MD5 model identity differs");
        NativeNeed(meshSource.model.bytes==capability.clips[0].sources[0].bytes&&meshSource.model.fingerprint==capability.clips[0].sources[0].fingerprint&&
            meshSource.skeleton.bytes==capability.clips[0].sources[1].bytes&&meshSource.skeleton.fingerprint==capability.clips[0].sources[1].fingerprint,"source actor native mesh source bytes differ");
        for(std::size_t i=0;i<request.clips.size();++i){const auto& clip=request.clips[i];idStr alias;
            NativeNeed(Wolf2009_RegisterSourceAnimationV2(modelAlias,clip.animationResource.c_str(),clip.animationSkeletonResource.c_str(),clip.clipName.c_str(),clip.animationSkeletonName.c_str(),int(clip.rigPolicy),alias),"source actor native animation registration failed");
            const auto envelope=ActorAnimationEnvelope(alias);auto& output=capability.clips[i];output.animationAlias=alias.c_str();output.animationMetadata=wire::Metadata(envelope.binding);ActorAnimationMatches(capability,i,envelope.binding);}
        pins->Check();auto bytes=actor::SerializeCapability(capability);const auto name=actor::CapabilityName(capability);
        return new ActorCapabilityFile(name,std::move(bytes),std::move(pins));
    }catch(const std::exception& error){common->Warning("Wolf2009 source actor binding: %s",error.what());return NULL;}
}
#endif

#ifdef WOLF2009_SOURCE_ACTOR_GAME
namespace {
std::string ActorDeclarationText(const idDecl* definition){
    NativeNeed(definition&&definition->GetState()==DS_PARSED,"source actor native declaration is not parsed");const int count=definition->GetTextLength();
    NativeNeed(count>0&&count<=64*1024,"source actor native declaration text budget");std::vector<char> text(std::size_t(count)+1,0);definition->GetText(text.data());return std::string(text.data(),std::size_t(count));
}
bool ActorFlagsAbsent(const idAnim* clip){const auto& flags=clip->GetAnimFlags();return !flags.prevent_idle_override&&!flags.random_cycle_start&&!flags.ai_no_turn&&!flags.anim_turn;}
void ActorSounds(const actor::Plan& plan,const std::vector<WolfSourceActorSoundBinding>& sounds){
    std::map<std::string,std::string> required;
    std::map<std::string,const wolf2009::CompiledCatalogEntry*> declarations;
    for(std::size_t i=0;i<plan.ClipCount();++i){const auto& clip=plan.Clip(i);NativeNeed(clip.soundDeclarations.size()==clip.request.frameSounds.size(),"source actor selected SFX provenance count differs");
        for(std::size_t n=0;n<clip.request.frameSounds.size();++n){const auto& command=clip.request.frameSounds[n];const auto prior=required.find(command.soundKey);
            NativeNeed(prior==required.end()||(prior->second==command.sourceSound&&actor::SameEntry(*declarations.at(command.soundKey),clip.soundDeclarations[n])),"source actor repeated sound key has conflicting source values/provenance");
            required[command.soundKey]=command.sourceSound;declarations[command.soundKey]=&clip.soundDeclarations[n];}}
    NativeNeed(sounds.size()==required.size(),"source actor sound mapping count does not cover authored commands");std::set<std::string> seen;
    for(const auto& sound:sounds){const auto expected=required.find(sound.sourceKey);idStr why;
        NativeNeed(expected!=required.end()&&expected->second==sound.sourceSound&&seen.insert(sound.sourceKey).second,"source actor sound mapping is unknown, repeated or changed");
        NativeNeed(sound.owner&&sound.owner->Source().name==sound.sourceSound&&actor::SameEntry(sound.owner->Source().declaration,*declarations.at(sound.sourceKey))&&
            sound.nativeSoundAlias==sound.owner->NativeAlias()&&sound.owner->MatchesNative(why),"source actor sound mapping has no unchanged owner from its selected source namespace");
        const auto* shader=declManager->FindSound(sound.nativeSoundAlias.c_str(),false);NativeNeed(shader&&shader->GetState()!=DS_DEFAULTED,"source actor frame sound has no qualified native shader");
    }
}
}
struct WolfSourceActorBinding::Impl {
    actor::Plan plan;
    ActorFile capabilityFile;
    actor::Capability capability;
    std::string capabilityName,nativeModelDefinition,nativeModel,declarationText;
    const idDeclModelDef* definition=NULL;
    idRenderModel* model=NULL;
    std::vector<WolfSourceActorClip> clips;
    std::vector<const idMD5Anim*> references;
    std::vector<WolfSourceActorSoundBinding> sounds;
    ~Impl(){for(const auto* animation:references)animation->DecreaseRefs();}
    bool Matches(idStr& reason)const{
        try{
            NativeNeed(definition&&definition->GetState()==DS_PARSED&&ActorDeclarationText(definition)==declarationText&&
                nativeModelDefinition==definition->GetName(),"source actor animation-set declaration changed");
            NativeNeed(model&&model->IsLoaded()&&!model->IsDefaultModel()&&model==definition->ModelHandle()&&nativeModel==model->Name()&&
                model->NumJoints()==int(capability.clips[0].bones)&&definition->NumAnims()==int(clips.size()+1),"source actor actual MD5 model identity changed");
            NativeNeed(references.size()==clips.size(),"source actor native animation reference ownership differs");
            for(std::size_t i=0;i<clips.size();++i){const auto& expected=clips[i];const int number=definition->GetSpecificAnim(expected.logicalName.c_str());const idAnim* clip=number>0?definition->GetAnim(number):NULL;
                const idMD5Anim* animation=clip&&clip->NumAnims()==1?clip->MD5Anim(0):NULL;
                NativeNeed(clip&&ActorFlagsAbsent(clip)&&expected.logicalName==clip->FullName()&&animation&&animation==references[i]&&animation->IsSourceAnimationV2()&&
                    expected.animationAlias==animation->Name()&&nativeModel==animation->SourceModelAlias()&&animation->SourceMatchesModel(model)&&animation->NumFrames()==expected.frames&&animation->Length()==expected.lengthMs,
                    "source actor actual clip identity/hierarchy/count changed");
                if(expected.frameSounds.empty())NativeNeed(!clip->HasFrameCommands(),"source actor clip gained frame commands");
                else NativeNeed(Wolf2009_IsQualifiedSourceFrameSound(definition,clip),"source actor frame sound profile changed");
            }
            ActorSounds(plan,sounds);reason.Clear();return true;
        }catch(const std::exception& error){reason=error.what();return false;}
    }
};
WolfSourceActorBinding::WolfSourceActorBinding(std::unique_ptr<Impl> value):impl_(std::move(value)){}
WolfSourceActorBinding::~WolfSourceActorBinding(){}
const actor::Plan& WolfSourceActorBinding::SourcePlan()const{return impl_->plan;}
const char* WolfSourceActorBinding::NativeModelDefinition()const{return impl_->nativeModelDefinition.c_str();}
const char* WolfSourceActorBinding::NativeModel()const{return impl_->nativeModel.c_str();}
const char* WolfSourceActorBinding::CapabilityName()const{return impl_->capabilityName.c_str();}
std::size_t WolfSourceActorBinding::ClipCount()const{return impl_->clips.size();}
const WolfSourceActorClip& WolfSourceActorBinding::Clip(std::size_t i)const{NativeNeed(i<impl_->clips.size(),"source actor binding clip index");return impl_->clips[i];}
const WolfSourceActorClip* WolfSourceActorBinding::FindClip(const char* name)const{
    if(!name)return NULL;std::size_t length=0;while(length<=127&&name[length])++length;if(length>127)return NULL;
    for(const auto& clip:impl_->clips)if(clip.logicalName.compare(0,std::string::npos,name,length)==0)return &clip;return NULL;
}
const std::vector<WolfSourceActorSoundBinding>& WolfSourceActorBinding::Sounds()const{return impl_->sounds;}
bool WolfSourceActorBinding::MatchesNative(idStr& reason)const{return impl_->Matches(reason);}
bool WolfSourceActorBinding::MatchesEntityModel(const std::string& entity,const std::string& model,const wolf2009::SourceDefinitionContext& context)const{return impl_->plan.MatchesEntityModel(entity,model,context);}
std::shared_ptr<const WolfSourceActorBinding> Wolf2009_BindSourceActor(const actor::Plan& plan,const std::vector<WolfSourceActorSoundBinding>& sounds,idStr& reason){
    reason.Clear();try{
        NativeNeed(plan.Valid(),"source actor plan is empty");ActorSounds(plan,sounds);std::unique_ptr<WolfSourceActorBinding::Impl> result(new WolfSourceActorBinding::Impl);
        result->plan=plan;result->sounds=sounds;const auto request=actor::RequestPath(plan.ResourceRequest());result->capabilityFile.reset(fileSystem->OpenFileRead(request.c_str(),false));
        result->capability=actor::ParseCapability(ReadActorFile(result->capabilityFile.get(),actor::MaximumCapabilityBytes));
        NativeNeed(actor::Same(plan.ResourceRequest(),result->capability.request),"source actor capability does not echo the full immutable request");
        result->capabilityName=actor::CapabilityName(result->capability);result->nativeModel=result->capability.modelAlias;
        NativeNeed(result->capabilityName==result->capabilityFile->GetName()&&result->capabilityName==result->capabilityFile->GetFullPath(),"source actor readonly capability identity differs");
        result->model=renderModelManager->CheckModel(result->nativeModel.c_str());NativeNeed(result->model&&result->model->IsLoaded()&&!result->model->IsDefaultModel()&&
            result->nativeModel==result->model->Name()&&result->model->IsDynamicModel()==DM_CACHED,"source actor capability has no resident native MD5 model");
        std::vector<WolfSourceAnimationSetEntry> entries;entries.reserve(plan.ClipCount());result->references.reserve(plan.ClipCount());result->clips.reserve(plan.ClipCount());
        for(std::size_t i=0;i<plan.ClipCount();++i){const auto& selected=plan.Clip(i).request;const auto& response=result->capability.clips[i];const auto envelope=ActorAnimationEnvelope(response.animationAlias.c_str());ActorAnimationMatches(result->capability,i,envelope.binding);
            idMD5Anim* animation=animationLib.GetAnim(response.animationAlias.c_str());
            NativeNeed(animation&&animation->IsSourceAnimationV2()&&result->nativeModel==animation->SourceModelAlias()&&animation->SourceMatchesModel(result->model)&&
                animation->NumFrames()==int(response.frames)&&animation->Length()==int(response.lengthMs),"source actor native animation does not match its readonly capability");
            result->references.push_back(animation);animation->IncreaseRefs();
            WolfSourceActorClip clip;clip.logicalName=selected.logicalName;clip.authoredReference=selected.authoredReference;clip.animationAlias=response.animationAlias;
            clip.frames=int(response.frames);clip.lengthMs=int(response.lengthMs);clip.frameSounds=selected.frameSounds;result->clips.push_back(std::move(clip));
            WolfSourceAnimationSetEntry entry;entry.logicalName=selected.logicalName;entry.animationAlias=response.animationAlias;
            if(!selected.frameSounds.empty())entry.frameOneSoundKey=selected.frameSounds[0].soundKey;entries.push_back(std::move(entry));
        }
        idStr nativeDefinition;NativeNeed(Wolf2009_CreateSourceActorAnimationSet(entries,NULL,nativeDefinition,reason),reason.IsEmpty()?"source actor native set creation failed":reason.c_str());
        result->nativeModelDefinition=nativeDefinition.c_str();result->definition=Wolf2009_FindRegisteredSourceAnimationSet(nativeDefinition);
        result->declarationText=ActorDeclarationText(result->definition);NativeNeed(result->Matches(reason),reason.IsEmpty()?"source actor native binding validation failed":reason.c_str());
        return std::shared_ptr<const WolfSourceActorBinding>(new WolfSourceActorBinding(std::move(result)));
    }catch(const std::exception& error){reason=error.what();return std::shared_ptr<const WolfSourceActorBinding>();}
}
#endif

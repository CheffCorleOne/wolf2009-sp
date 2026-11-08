// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceSoundBinding.h"
#include "CompiledDeclarationNamespace.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#ifdef WOLF2009_SOURCE_SOUND_CORE
#include "framework/Wolf2009Streams.h"
#endif
#ifdef WOLF2009_SOURCE_SOUND_GAME
#include "game/Game_local.h"
#endif
namespace {
const char Prefix[]="wolf2009/_sound/sample/v1/";
const std::size_t MaximumSampleBytes=16u*1024u*1024u,MaximumRequestBytes=4608;
void Need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::string Fold(std::string s){for(char& c:s){if(c>='A'&&c<='Z')c+=32;if(c=='\\')c='/';}return s;}
std::string Text(const char* value){Need(value!=NULL,"source sound path is absent");
    std::size_t n=0;while(n<=MaximumRequestBytes&&value[n])++n;Need(n<=MaximumRequestBytes,"source sound request path budget");return std::string(value,n);}
std::uint64_t Hash(const unsigned char* p,std::size_t n){std::uint64_t h=UINT64_C(14695981039346656037);for(std::size_t i=0;i<n;++i)h=(h^p[i])*UINT64_C(1099511628211);return h;}
std::string Hex(std::uint64_t v){std::string s(16,'0');for(int i=15;i>=0;--i){s[std::size_t(i)]="0123456789abcdef"[v&15];v>>=4;}return s;}
struct Resource {std::string path,mount,name;};
Resource ParseResource(const std::string& value){
    Resource out;out.path=Fold(value);Need(!out.path.empty()&&out.path.size()<=4096,"source sound resource path budget");
    for(unsigned char c:out.path)Need(c>32&&c<127&&c!=':'&&c!='"'&&c!='{'&&c!='}',"unsafe source sound resource path");
    std::size_t start=0;std::vector<std::string> parts;
    while(start<out.path.size()){const auto end=out.path.find('/',start);const auto part=out.path.substr(start,end-start);
        Need(!part.empty()&&part!="."&&part!="..","unsafe source sound path component");parts.push_back(part);if(end==std::string::npos)break;start=end+1;}
    Need(out.path.back()!='/'&&parts.size()>=4&&parts[0]=="wolf2009"&&parts[2]=="snds"&&
        parts[1]!="_sound"&&parts[1]!="_actor"&&parts[1]!="_animation","explicit mounted SNDS resource required");
    out.mount=parts[1];const auto typeEnd=out.path.find('/',9+out.mount.size()+1); // after wolf2009/<mount>/snds
    Need(typeEnd!=std::string::npos,"source sound name is absent");out.name=out.path.substr(typeEnd+1);
    Need(out.name.size()>4&&out.name.compare(out.name.size()-4,4,".mp3")==0,"explicit native SNDS MP3 suffix required");return out;
}
std::uint64_t Decimal(const std::string& value){Need(!value.empty()&&(value.size()==1||value[0]!='0'),"noncanonical source sound integer");std::uint64_t n=0;
    for(unsigned char c:value){Need(c>='0'&&c<='9'&&n<=(UINT64_MAX-(c-'0'))/10,"source sound integer overflow");n=n*10+c-'0';}return n;}
struct Request {std::uint64_t epoch=0,bytes=0,fingerprint=0;Resource resource;};
std::string RequestPath(const Request& r){return std::string(Prefix)+std::to_string(r.epoch)+"/"+std::to_string(r.bytes)+"/"+Hex(r.fingerprint)+"/"+r.resource.path;}
Request ParseRequest(const std::string& text){
    Need(text.size()<=MaximumRequestBytes&&text.compare(0,sizeof(Prefix)-1,Prefix)==0,"source sound request prefix/budget");
    std::size_t p=sizeof(Prefix)-1;const auto field=[&](){const auto e=text.find('/',p);Need(e!=std::string::npos,"incomplete source sound request");const auto s=text.substr(p,e-p);p=e+1;return s;};
    Request r;r.epoch=Decimal(field());r.bytes=Decimal(field());const auto hash=field();Need(hash.size()==16,"source sound fingerprint width");
    for(unsigned char c:hash){Need((c>='0'&&c<='9')||(c>='a'&&c<='f'),"source sound fingerprint encoding");r.fingerprint=(r.fingerprint<<4)|(c<='9'?c-'0':c-'a'+10);}
    r.resource=ParseResource(text.substr(p));Need((r.epoch==0&&r.bytes==0&&r.fingerprint==0)||(r.epoch&&r.bytes&&r.bytes<=MaximumSampleBytes),"incomplete source sound expected identity");
    Need(RequestPath(r)==text,"noncanonical source sound request");return r;
}
struct FileCloser {void operator()(idFile* p)const{if(p)fileSystem->CloseFile(p);}};
typedef std::unique_ptr<idFile,FileCloser> File;
std::vector<unsigned char> ReadFile(idFile* file){Need(file!=NULL,"source sound resource missing");const int count=file->Length();
    Need(count>0&&std::size_t(count)<=MaximumSampleBytes,"source sound sample byte budget");std::vector<unsigned char> bytes(static_cast<std::size_t>(count));
    Need(file->Seek(0,FS_SEEK_SET)==0&&file->Read(bytes.data(),count)==count,"short source sound sample read");unsigned char end=0;
    Need(file->Read(&end,1)==0,"source sound sample has trailing bytes");return bytes;
}
}
#ifdef WOLF2009_SOURCE_SOUND_CORE
namespace {
struct Lease {
    std::string mount;unsigned long long epoch=0;
    explicit Lease(const Resource& resource):mount(resource.mount){Need(Wolf2009_PinMount(mount.c_str(),epoch)&&epoch,"source sound mount is absent");}
    ~Lease(){if(epoch)Wolf2009_UnpinMount(mount.c_str(),epoch);}
};
class SampleFile:public idFile {
    const std::string name;
    const std::vector<unsigned char> bytes;
    const std::unique_ptr<Lease> pin;
    std::size_t position=0;
  public:
    SampleFile(std::string path,std::vector<unsigned char> data,std::unique_ptr<Lease> lease):name(std::move(path)),bytes(std::move(data)),pin(std::move(lease)){}
    const char* GetName() override{return name.c_str();}
    const char* GetFullPath() override{return name.c_str();}
    int Length() override{return int(bytes.size());}
    int Tell() override{return int(position);}
    ID_TIME_T Timestamp() override{return 0;}
    int Read(void* p,int n) override{if(!p||n<=0)return 0;const auto count=(std::min)(std::size_t(n),bytes.size()-position);
        if(count)std::memcpy(p,bytes.data()+position,count);position+=count;return int(count);}
    int Seek(long offset,fsOrigin_t origin) override{const long long base=origin==FS_SEEK_SET?0:origin==FS_SEEK_CUR?static_cast<long long>(position):origin==FS_SEEK_END?static_cast<long long>(bytes.size()):-1;
        if(base<0||offset<-base||offset>static_cast<long long>(bytes.size())-base)return -1;position=std::size_t(base+offset);return 0;}
};
}
bool Wolf2009_SourceSoundSampleCapability(const std::string& resourcePath,unsigned long long epoch,
    unsigned long long bytes,unsigned long long fingerprint,std::string& capability,std::string& reason){
    capability.clear();reason.clear();
    try{Request request;request.resource=ParseResource(resourcePath);request.epoch=epoch;request.bytes=bytes;request.fingerprint=fingerprint;
        Need(epoch&&bytes&&bytes<=MaximumSampleBytes,"source sound capability requires a pinned epoch and bounded exact bytes");
        const auto text=RequestPath(request);const auto parsed=ParseRequest(text);
        Need(parsed.resource.path==request.resource.path&&parsed.epoch==epoch&&parsed.bytes==bytes&&parsed.fingerprint==fingerprint,
             "source sound capability round trip differs");
        capability=text;return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}
bool Wolf2009_IsSourceSoundRequest(const char* path){if(!path)return false;
    for(std::size_t i=0;i<sizeof(Prefix)-1;++i)if(path[i]!=Prefix[i])return false;return true;}
idFile* Wolf2009_OpenSourceSoundSample(const char* path){
    try{auto request=ParseRequest(Text(path));std::unique_ptr<Lease> pin(new Lease(request.resource));
        Need(request.epoch==0||request.epoch==pin->epoch,"stale source sound mount epoch");
        File file(fileSystem->OpenFileRead(request.resource.path.c_str(),false));auto bytes=ReadFile(file.get());const auto fingerprint=Hash(bytes.data(),bytes.size());
        Need(request.bytes==0||(request.bytes==bytes.size()&&request.fingerprint==fingerprint),"source sound sample byte identity changed");
        request.epoch=pin->epoch;request.bytes=bytes.size();request.fingerprint=fingerprint;
        return new SampleFile(RequestPath(request),std::move(bytes),std::move(pin));
    }catch(const std::exception& error){common->Warning("Wolf2009 source sound: %s",error.what());return NULL;}
}
#endif
#ifdef WOLF2009_SOURCE_SOUND_GAME
namespace {
const idDecl* Unparsed(const std::string& name){
    if(const auto* found=declManager->FindDeclWithoutParsing(DECL_SOUND,name.c_str(),false))return found;
    for(int i=0;i<declManager->GetNumDecls(DECL_SOUND);++i){const auto* found=declManager->DeclByIndex(DECL_SOUND,i,false);
        if(found&&idStr::Icmp(found->GetName(),name.c_str())==0)return found;}return NULL;
}
bool TextMatches(const idDecl* decl,const std::string& expected){if(!decl||decl->GetTextLength()!=int(expected.size()))return false;
    std::vector<char> text(expected.size()+1,0);decl->GetText(text.data());return std::string(text.data(),expected.size())==expected;}
std::map<std::string,std::string> registered;
}
struct WolfSourceSoundBinding::Impl {
    wolf2009::SourceSound source;
    File sample;
    std::string samplePath,alias,text;
};
WolfSourceSoundBinding::WolfSourceSoundBinding(std::unique_ptr<Impl> p):impl_(std::move(p)){}
WolfSourceSoundBinding::~WolfSourceSoundBinding(){}
const wolf2009::SourceSound& WolfSourceSoundBinding::Source()const{return impl_->source;}
const char* WolfSourceSoundBinding::NativeAlias()const{return impl_->alias.c_str();}
const char* WolfSourceSoundBinding::SampleCapability()const{return impl_->samplePath.c_str();}
bool WolfSourceSoundBinding::MatchesNative(idStr& reason)const{
    reason.Clear();try{
        Need(impl_->source.declaration.IsUnmodified()&&impl_->sample.get()!=NULL,"source sound owning facts/lease lost");
        Need(TextMatches(Unparsed(impl_->alias),impl_->text),"source sound native declaration text changed");
        const auto* shader=declManager->FindSound(impl_->alias.c_str(),false);
        Need(shader&&shader->GetState()!=DS_DEFAULTED&&shader->GetNumSounds()==1&&!shader->HasDefaultSound()&&
             impl_->samplePath==shader->GetSound(0),"source sound native sample parse/identity failed");
        const auto* parms=shader->GetParms();Need(parms&&parms->volume==0&&parms->shakes==0&&parms->soundShaderFlags==0&&
            parms->minDistance==1&&parms->maxDistance==10,"source sound native mix profile changed");return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}
std::shared_ptr<const WolfSourceSoundBinding> Wolf2009_BindSourceSound(
    const wolf2009::CompiledDeclarationNamespace& names,const WolfSourceSoundRequest& request,idStr& reason){
    reason.Clear();try{
        Need(request.mixPolicy==WolfSourceSoundMix_NativeWorldUnitGainV1||request.mixPolicy==WolfSourceSoundMix_NativePlayerUnitGainV1,"explicit source sound native mix policy required");
        const bool player=request.mixPolicy==WolfSourceSoundMix_NativePlayerUnitGainV1;
        const auto lookup=names.Lookup("sfx",request.sourceSound);Need(lookup.status==wolf2009::CompiledCatalogLookupStatus::Unique&&lookup.matches.size()==1,"source sfx missing from selected namespace");
        std::unique_ptr<WolfSourceSoundBinding::Impl> out(new WolfSourceSoundBinding::Impl);
        out->source=wolf2009::PrepareSourceSound(lookup.matches[0],player?wolf2009::SourceSoundProfile::SinglePlayerSampleV1:wolf2009::SourceSoundProfile::SingleWorldSampleV1);
        Request sample;sample.resource=ParseResource(request.sampleResource);
        Need(wolf2009::SourceSoundSampleMatches(out->source,sample.resource.name),"explicit SNDS name differs from authored source WAV reference");
        out->sample.reset(fileSystem->OpenFileRead(RequestPath(sample).c_str(),false));auto bytes=ReadFile(out->sample.get());
        out->samplePath=Text(out->sample->GetName());const auto actual=ParseRequest(out->samplePath);
        Need(actual.resource.path==sample.resource.path&&actual.epoch&&actual.bytes==bytes.size()&&actual.fingerprint==Hash(bytes.data(),bytes.size()),"source sound readonly sample capability identity differs");
        out->text="{\n minDistance 1\n maxDistance 10\n volume 0\n \""+out->samplePath+"\"\n}\n";
        out->alias=(player?"wolf2009/sound/player-unit/v1/":"wolf2009/sound/")+Hex(Hash(reinterpret_cast<const unsigned char*>(out->text.data()),out->text.size()));
        const auto prior=registered.find(out->alias);
        Need(prior==registered.end()||prior->second==out->text,"source sound alias collision with a different complete key");
        Need(prior!=registered.end()||registered.size()<4096,"source sound declaration registration budget");
        if(prior==registered.end()){
            Need(!Unparsed(out->alias),"source sound native namespace occupied");
            auto* decl=declManager->CreateNewDecl(DECL_SOUND,out->alias.c_str(),"<wolf2009-source-sound>");Need(decl!=NULL,"source sound native declaration allocation failed");
            decl->SetText(out->text.c_str());decl->Invalidate();
        }else Need(TextMatches(Unparsed(out->alias),out->text),"source sound native declaration text changed");
        // Parsing occurs only after immutable text verification. Require an
        // enabled native sound cache and a decoded, non-default sample.
        declManager->FindSound(out->alias.c_str(),false);
        std::shared_ptr<const WolfSourceSoundBinding> result(new WolfSourceSoundBinding(std::move(out)));
        Need(result->MatchesNative(reason),reason.IsEmpty()?"source sound native validation failed":reason.c_str());
        registered.insert(std::make_pair(result->impl_->alias,result->impl_->text));return result;
    }catch(const std::exception& error){reason=error.what();return std::shared_ptr<const WolfSourceSoundBinding>();}
}
#endif

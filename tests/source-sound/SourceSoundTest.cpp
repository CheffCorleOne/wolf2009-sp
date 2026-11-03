// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceSound.h"
#include "Archive.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace {
using namespace wolf2009;
typedef std::vector<unsigned char> Bytes;
std::size_t checks=0;
void Check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void Reject(F f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}Check(refused,"unsupported/malformed source sound admitted");}
const char* Body="{ category \"World - Normal\" duration 240 rms 1234 layers 1 samples 1 layer { sample { path sound/own/example.wav weight 1 } } }";
const char* PlayerBody="{ category \"Player\" priority \"High\" duration 240 rms 1234 layers 1 samples 1 layer { sample { path sound/own/example.wav weight 1 attenuationRange 1 attenuationMin 1 } } }";
CompiledCatalogEntry Entry(const std::string& body){
    const std::string text="sfx \"own_sample\" "+body+"\n";
    Bytes bytes(8,0);const auto framing="1\nsound/own.sfx\n2\n1\n0\n"+std::to_string(text.size())+"\n"+text+"0\n";
    bytes.insert(bytes.end(),framing.begin(),framing.end());
    const auto document=CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1);
    CompiledCatalogSource source;source.loadIdentity="own_sound";source.path="own/selected.decls";source.role=CompiledCatalogRole::Explicit;
    CompiledDeclarationCatalog catalog;catalog.AddDocument(document,source);return catalog.Entry(0);
}
SourceSound Prepare(const std::string& body){return PrepareSourceSound(Entry(body),SourceSoundProfile::SingleWorldSampleV1);}
std::string Replace(const std::string& text,const std::string& before,const std::string& after){auto out=text;const auto p=out.find(before);Check(p!=std::string::npos,"invalid test replacement");out.replace(p,before.size(),after);return out;}
void Synthetic(){
    auto sound=Prepare(Body);Check(sound.name=="own_sample"&&sound.category=="World - Normal"&&sound.durationMs==240&&sound.rms==1234&&sound.samplePath=="sound/own/example.wav","source sound facts changed");
    Check(sound.declaration.IsUnmodified()&&sound.declaration.Group().sourceFile=="sound/own.sfx","source sound lost owning provenance");
    Check(SourceSoundSampleMatches(sound,"sound/own/example")&&SourceSoundSampleMatches(sound,"SOUND\\OWN\\EXAMPLE.mp3"),"explicit matching SNDS sample refused");
    for(const char* wrong:{"sound/own/other.mp3","sound/own/example.ogg","sound/own/example.wav","../sound/own/example.mp3","sound//own/example.mp3","/sound/own/example.mp3"})
        Check(!SourceSoundSampleMatches(sound,wrong),"incorrect explicit sample accepted");
    Reject([&]{PrepareSourceSound(Entry(Body),SourceSoundProfile::Unspecified);});
    auto modified=Entry(Body);modified.name="forged";Reject([&]{PrepareSourceSound(modified,SourceSoundProfile::SingleWorldSampleV1);});
    Reject([&]{PrepareSourceSound(CompiledCatalogEntry(),SourceSoundProfile::SingleWorldSampleV1);});
    for(const auto& change:std::vector<std::pair<std::string,std::string>>{{"layers 1","layers 2"},{"samples 1","samples 0"},{"weight 1","weight 0"},
        {"weight 1","weight 1.0"},{"World - Normal","Music"},{"duration 240","duration 4294967296"},{"duration 240","duration 0"},{"rms 1234","rms 32769"},
        {"example.wav","example.ogg"},{"sound/own/example.wav","../example.wav"},{"samples 1","samples 1 samples 1"},{"path sound/own/example.wav","path sound/own/example.wav loop 1"},
        {"rms 1234","rms 1234 unknown 1"},{"duration 240","duration -1"},{"weight 1",""},{"category \"World - Normal\"",""}})
        Reject([&]{Prepare(Replace(Body,change.first,change.second));});
    Check(Prepare(Replace(Body,"duration 240","/*metadata*/duration 240 // line\n")).durationMs==240,"source sound comments changed meaning");
    Check(Prepare(Replace(Body,"path sound/own/example.wav","path \"sound/own/example.wav\"")).samplePath==sound.samplePath,"source quoted sound path changed");
    const auto owned=sound.declaration;Check(owned.Record().text.size()>0&&owned.IsUnmodified(),"source sound provenance lifetime lost");
    const auto player=PrepareSourceSound(Entry(PlayerBody),SourceSoundProfile::SinglePlayerSampleV1);
    Check(player.category=="Player"&&player.priority=="High"&&player.unitSampleAttenuation&&player.durationMs==240&&player.rms==1234,
        "Player sound profile lost explicit category/priority/unit attenuation");
    Check(player.declaration.IsUnmodified()&&player.samplePath==sound.samplePath&&SourceSoundSampleMatches(player,"sound/own/example.mp3"),
        "Player sound lost owning record/exact sample binding");
    Check(sound.priority.empty()&&!sound.unitSampleAttenuation,"Legacy world sound acquired Player-only metadata");
    Reject([&]{PrepareSourceSound(Entry(PlayerBody),SourceSoundProfile::SingleWorldSampleV1);});
    Reject([&]{PrepareSourceSound(Entry(Body),SourceSoundProfile::SinglePlayerSampleV1);});
    for(const auto& change:std::vector<std::pair<std::string,std::string>>{{"priority \"High\"",""},{"priority \"High\"","priority \"Low\""},
        {"priority \"High\"","priority High"},{"category \"Player\"","category \"World - Normal\""},
        {"attenuationRange 1","attenuationRange 0"},{"attenuationMin 1","attenuationMin -1"},{"attenuationMin 1",""},
        {"attenuationRange 1","attenuationRange 1.0"},{"attenuationMin 1","attenuationMin 1 attenuationMin 1"},
        {"weight 1","weight 0"},{"priority \"High\"","priority \"High\" priority \"High\""}})
        Reject([&]{PrepareSourceSound(Entry(Replace(PlayerBody,change.first,change.second)),SourceSoundProfile::SinglePlayerSampleV1);});
    std::cout<<"WOLF2009_SOURCE_SOUND_SYNTHETIC checks="<<checks<<" native_audio_claimed=0\n";
}
Bytes Read(const Archive& a,std::ifstream& file,const char* type,const char* name){std::size_t count=0,chunk=0,entry=0;
    for(std::size_t c=0;c<a.Chunks().size();++c)if(a.Chunks()[c].type==type)for(std::size_t e=0;e<a.Chunks()[c].entries.size();++e)if(a.Chunks()[c].entries[e].name==name){++count;chunk=c;entry=e;}
    Check(count==1,"exact original resource absent/ambiguous");return a.ReadEntry(file,chunk,entry);
}
void Original(const char* path){
    std::ifstream file(path,std::ios::binary);Check(bool(file),"cannot read owner-supplied original map");const auto a=Archive::Scan(file);
    const auto bytes=Read(a,file,"DECL","maps/game/trainyard/trainyard");
    CompiledDeclarationCatalog catalog;CompiledCatalogSource source;source.loadIdentity="selected_map";source.path="owner/trainyard.mpk";source.role=CompiledCatalogRole::Map;
    catalog.AddDocument(CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1),source);
    for(const char* name:{"footlocker_open","footlocker_close"}){
        const auto lookup=catalog.Lookup("sfx",name);Check(lookup.status==CompiledCatalogLookupStatus::Unique,"source sound selection differs");
        const auto sound=PrepareSourceSound(lookup.matches.at(0),SourceSoundProfile::SingleWorldSampleV1);
        const auto stem=std::string("sound/objects/footlocker/")+name;
        Check(sound.samplePath==stem+".wav"&&sound.category=="World - Normal"&&sound.durationMs==(std::string(name)=="footlocker_open"?933u:380u),"original sound metadata differs");
        const auto sample=Read(a,file,"SNDS",stem.c_str());Check(!sample.empty(),"original SNDS sample empty");
        Check(SourceSoundSampleMatches(sound,stem),"original sample reference mismatch");
        std::cout<<"WOLF2009_SOURCE_SOUND_ORIGINAL name="<<name<<" duration="<<sound.durationMs<<" sample_bytes="<<sample.size()<<" native_audio_claimed=0\n";
    }
}
void OriginalGlobal(const char* path){
    std::ifstream file(path,std::ios::binary);Check(bool(file),"cannot read owner-supplied original global declarations");
    file.seekg(0,std::ios::end);const auto length=file.tellg();Check(length>0&&length<=(64<<20),"original global declarations exceed test byte budget");file.seekg(0);
    const Bytes bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    CompiledDeclarationCatalog catalog;CompiledCatalogSource source;source.loadIdentity="selected_global";source.path="owner/global.decls";source.role=CompiledCatalogRole::Global;
    catalog.AddDocument(CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1),source);
    const auto lookup=catalog.Lookup("sfx","player_weapon_mp40_reload");Check(lookup.status==CompiledCatalogLookupStatus::Unique,"original global MP40 reload SFX is absent/ambiguous");
    const auto sound=PrepareSourceSound(lookup.matches.at(0),SourceSoundProfile::SinglePlayerSampleV1);
    Check(sound.declaration.IsUnmodified()&&sound.category=="Player"&&sound.priority=="High"&&sound.unitSampleAttenuation&&sound.durationMs==1694&&sound.rms==4345,
        "Original MP40 reload sound metadata differs");
    Check(sound.samplePath=="sound/weapons/mp40/mp40_reload_6.wav"&&SourceSoundSampleMatches(sound,"sound/weapons/mp40/mp40_reload_6.mp3"),
        "Original MP40 reload SFX/sample binding differs");
    Reject([&]{PrepareSourceSound(lookup.matches.at(0),SourceSoundProfile::SingleWorldSampleV1);});
    std::cout<<"WOLF2009_SOURCE_SOUND_ORIGINAL_PLAYER duration="<<sound.durationMs<<" rms="<<sound.rms<<" native_audio_claimed=0\n";
}
}
int main(int argc,char** argv){try{Synthetic();for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--self-test")continue;
    if(arg=="--source-map"&&i+1<argc){Original(argv[++i]);continue;}
    if(arg=="--source-global-decls"&&i+1<argc){OriginalGlobal(argv[++i]);continue;}
    throw std::runtime_error("usage: --self-test [--source-map selected-trainyard.mpk] [--source-global-decls owner-global.decls]");}return 0;
}catch(const std::exception& error){std::cerr<<"Source sound: "<<error.what()<<'\n';return 1;}}

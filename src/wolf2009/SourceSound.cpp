// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceSound.h"
#include <set>
#include <stdexcept>
#include <vector>
namespace wolf2009 {
namespace {
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string Fold(std::string value){for(char& c:value){if(c>='A'&&c<='Z')c+=32;if(c=='\\')c='/';}return value;}
struct Token {std::string value;bool quoted=false;};
class Reader {
    const std::vector<unsigned char>& bytes;
    std::size_t pos,end,tokens=0;
  public:
    Reader(const CompiledCatalogEntry& e):bytes(e.Record().text),pos(e.bodySpan.begin),end(e.bodySpan.end){
        Need(pos<end&&end<=bytes.size()&&end-pos<=65536,"source sound body bounds/budget");
    }
    Token Next(){
        for(;;){
            while(pos<end&&(bytes[pos]==' '||bytes[pos]=='\r'||bytes[pos]=='\n'||bytes[pos]=='\t'))++pos;
            if(pos+1>=end||bytes[pos]!='/')break;
            if(bytes[pos+1]=='/'){pos+=2;while(pos<end&&bytes[pos]!='\n')++pos;continue;}
            if(bytes[pos+1]=='*'){pos+=2;while(pos+1<end&&!(bytes[pos]=='*'&&bytes[pos+1]=='/'))++pos;
                Need(pos+1<end,"unterminated source sound comment");pos+=2;continue;}break;
        }
        Need(++tokens<=256,"source sound token budget");Token token;if(pos==end)return token;
        const auto add=[&](unsigned char c){Need(c>=32&&c<127,"non-ASCII/control source sound token");
            Need(token.value.size()<4096,"source sound token byte budget");token.value.push_back(char(c));};
        if(bytes[pos]=='{'||bytes[pos]=='}'){add(bytes[pos++]);return token;}
        if(bytes[pos]=='"'){
            token.quoted=true;++pos;bool closed=false;
            while(pos<end){unsigned char c=bytes[pos++];if(c=='"'){closed=true;break;}
                if(c=='\\'&&pos<end&&(bytes[pos]=='"'||bytes[pos]=='\\'))c=bytes[pos++];add(c);}
            Need(closed,"unterminated source sound string");return token;
        }
        while(pos<end&&bytes[pos]!=' '&&bytes[pos]!='\t'&&bytes[pos]!='\r'&&bytes[pos]!='\n'&&bytes[pos]!='{'&&bytes[pos]!='}'){
            Need(bytes[pos]!='"',"quote inside source sound token");add(bytes[pos++]);}
        Need(!token.value.empty(),"empty source sound token");return token;
    }
    void Expect(const char* value){const auto t=Next();Need(!t.quoted&&t.value==value,"unsupported source sound grammar");}
    std::uint32_t Integer(){const auto token=Next();Need(!token.quoted&&!token.value.empty(),"source sound integer expected");
        std::uint32_t value=0;for(unsigned char c:token.value){Need(c>='0'&&c<='9',"source sound integer expected");
            Need(value<=(UINT32_MAX-(c-'0'))/10,"source sound integer overflow");value=value*10+c-'0';}return value;}
};
std::string Path(std::string path){
    path=Fold(path);Need(!path.empty()&&path.size()<=4096&&path.front()!='/'&&path.back()!='/',"invalid source sound path");
    for(unsigned char c:path)Need(c>32&&c<127&&c!=':'&&c!='"'&&c!='{'&&c!='}',"unsafe source sound path byte");
    std::size_t begin=0;while(begin<path.size()){const auto end=path.find('/',begin);const auto part=path.substr(begin,end-begin);
        Need(!part.empty()&&part!="."&&part!="..","unsafe source sound path component");if(end==std::string::npos)break;begin=end+1;}return path;
}
std::string Stem(const std::string& path){const auto slash=path.find_last_of('/'),dot=path.find_last_of('.');
    return dot!=std::string::npos&&(slash==std::string::npos||dot>slash)?path.substr(0,dot):path;}
}
SourceSound PrepareSourceSound(const CompiledCatalogEntry& entry,SourceSoundProfile profile){
    Need(profile==SourceSoundProfile::SingleWorldSampleV1||profile==SourceSoundProfile::SinglePlayerSampleV1,"explicit source sound profile required");
    const bool player=profile==SourceSoundProfile::SinglePlayerSampleV1;
    Need(entry.IsUnmodified()&&Fold(entry.type)=="sfx","source sound requires an unmodified sfx catalog entry");
    SourceSound out;out.declaration=entry;out.name=entry.name;Reader reader(entry);reader.Expect("{");
    std::set<std::string> fields;bool layer=false;
    for(;;){const auto key=reader.Next();if(!key.quoted&&key.value=="}")break;
        Need(!key.quoted&&!key.value.empty()&&fields.insert(key.value).second,"duplicate/missing source sound field");
        if(key.value=="category"){const auto value=reader.Next();Need(value.quoted&&value.value==(player?"Player":"World - Normal"),"unsupported source sound category");out.category=value.value;}
        else if(key.value=="priority"){const auto value=reader.Next();Need(player&&value.quoted&&value.value=="High","unsupported source sound priority");out.priority=value.value;}
        else if(key.value=="duration")out.durationMs=reader.Integer();
        else if(key.value=="rms")out.rms=reader.Integer();
        else if(key.value=="layers"||key.value=="samples")Need(reader.Integer()==1,"source sound profile requires one layer/sample");
        else if(key.value=="layer"){
            reader.Expect("{");reader.Expect("sample");reader.Expect("{");
            std::set<std::string> sampleFields;
            for(;;){const auto field=reader.Next();if(!field.quoted&&field.value=="}")break;
                Need(!field.quoted&&sampleFields.insert(field.value).second,"duplicate source sound sample field");
                if(field.value=="path"){const auto path=reader.Next();Need(!path.value.empty(),"source sound sample path is absent");out.samplePath=path.value;}
                else if(field.value=="weight")Need(reader.Integer()==1,"source sound profile requires sample weight1");
                else if(field.value=="attenuationRange"||field.value=="attenuationMin")Need(player&&reader.Integer()==1,"source player sound requires explicit unit sample attenuation");
                else throw std::runtime_error("unsupported source sound sample property");
            }
            const std::set<std::string> expectedSample=player?std::set<std::string>({"path","weight","attenuationRange","attenuationMin"}):std::set<std::string>({"path","weight"});
            Need(sampleFields==expectedSample,"incomplete source sound sample");out.unitSampleAttenuation=player;reader.Expect("}");layer=true;
        }else throw std::runtime_error("unsupported source sound property");
    }
    const std::set<std::string> expectedFields=player?std::set<std::string>({"category","priority","duration","rms","layers","samples","layer"}):std::set<std::string>({"category","duration","rms","layers","samples","layer"});
    Need(fields==expectedFields&&layer,"incomplete source sound definition");
    Need(out.durationMs>0&&out.durationMs<=3600000&&out.rms<=32768,"source sound metadata range");
    const auto path=Path(out.samplePath);Need(path.size()>4&&path.compare(path.size()-4,4,".wav")==0,"source sound profile requires an authored WAV reference");
    Need(reader.Next().value.empty(),"source sound trailing body data");return out;
}
bool SourceSoundSampleMatches(const SourceSound& sound,const std::string& name){
    try{const auto selected=Path(name);const auto stem=Stem(selected);
        if(stem!=selected&&selected.substr(stem.size())!=".mp3")return false;
        return Stem(Path(sound.samplePath))==stem;}catch(...){return false;}
}
}

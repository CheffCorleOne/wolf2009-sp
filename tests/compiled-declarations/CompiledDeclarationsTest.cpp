// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarations.h"
#include "Archive.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
using namespace wolf2009;
typedef std::vector<unsigned char> Bytes;
unsigned checks=0,groups=0,failed=0;
void Check(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
template<class F>void Group(const char* name,F run){++groups;try{run();}catch(const std::exception& error){++failed;std::cerr<<name<<": "<<error.what()<<'\n';}}
template<class F>void Reject(F run,CompiledDeclFailure expected,const char* reason){bool rejected=false;try{run();}catch(const CompiledDeclError& error){rejected=error.Category()==expected;}Check(rejected,reason);}
template<class F>void RejectAny(F run,const char* reason){bool rejected=false;try{run();}catch(const CompiledDeclError&){rejected=true;}Check(rejected,reason);}
void Word(Bytes& bytes,std::uint32_t value){for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<unsigned char>(value>>shift));}
void Text(Bytes& bytes,const std::string& value){bytes.insert(bytes.end(),value.begin(),value.end());}
Bytes Data(const std::string& text){return Bytes(text.begin(),text.end());}
Bytes Empty(){Bytes b;Word(b,0);Word(b,0);Text(b,"0\n0\n");return b;}
Bytes One(const Bytes& payload,const std::string& label="file.def",const std::string& tag="3",const std::string& id="9",const std::string& length=std::string()){
    Bytes b;Word(b,0);Word(b,0);Text(b,"1\n"+label+"\n"+tag+"\n1\n"+id+"\n"+(length.empty()?std::to_string(payload.size()):length)+"\n");
    b.insert(b.end(),payload.begin(),payload.end());Text(b,"0\n");return b;
}
CompiledDeclarations Read(const Bytes& b,const CompiledDeclLimits& limits=CompiledDeclLimits()){
    return CompiledDeclarations::Read(b,CompiledDeclFormat::TwoSectionLfTextV1,limits);
}
std::uint64_t Hash(const Bytes& b){std::uint64_t out=UINT64_C(14695981039346656037);for(auto c:b){out^=c;out*=UINT64_C(1099511628211);}return out;}
bool Is(const CompiledDeclSpan& span,std::size_t begin,std::size_t end){return span.begin==begin&&span.end==end;}
void Synthetic(){
    Group("explicit profile and empty sets",[]{
        const auto b=Empty();Reject([&]{CompiledDeclarations::Read(b);},CompiledDeclFailure::Unsupported,"Missing explicit profile was admitted");
        Reject([&]{CompiledDeclarations::Read(b,static_cast<CompiledDeclFormat>(99));},CompiledDeclFailure::Unsupported,"Unknown explicit profile admitted");
        const auto result=Read(b);Check(result.sourceBytes==12&&result.sourceFingerprint==Hash(b),"Empty transport identity changed");
        Check(result.pools[0].words.empty()&&result.pools[1].words.empty()&&result.sections[0].groups.empty()&&result.sections[1].groups.empty(),"Empty transport acquired content");
        Check(Is(result.pools[0].countSpan,0,4)&&Is(result.pools[0].dataSpan,4,4)&&Is(result.pools[1].countSpan,4,8)&&
            Is(result.sections[0].countSpan,8,9)&&Is(result.sections[1].countSpan,10,11),"Empty spans included LF or shifted binary counts");
        Bytes zero;Word(zero,0);Word(zero,0);Text(zero,"1\nfile.def\n0\n0\n0\n");const auto group=Read(zero).sections[0].groups[0];
        Check(group.defaultTypeId==0&&group.records.empty()&&group.sourceFile=="file.def","Zero default type/record count rejected");
        Check(Read(One(Bytes())).sections[0].groups[0].records[0].text.empty(),"Zero-length raw payload rejected");
    });
    Group("opaque words binary payload and exact spans",[]{
        const Bytes payload{0,13,10,255,'1',10,'x'};Bytes b;Word(b,2);Word(b,0x12345678);Word(b,0xffffffff);Word(b,1);Word(b,0x7fc01234);
        Text(b,"1\nfile.def\n4294967295\n1\n0\n7\n");b.insert(b.end(),payload.begin(),payload.end());Text(b,"0\n");const auto result=Read(b);
        Check(result.sourceBytes==57&&result.sourceFingerprint==Hash(b),"Binary transport source identity differs");
        Check(result.pools[0].words==std::vector<std::uint32_t>({0x12345678,0xffffffff})&&result.pools[1].words[0]==0x7fc01234,"Word bits were sign/float decoded or endian swapped");
        Check(Is(result.pools[0].countSpan,0,4)&&Is(result.pools[0].dataSpan,4,12)&&Is(result.pools[1].countSpan,12,16)&&Is(result.pools[1].dataSpan,16,20),"Pool count/data spans differ");
        const auto& section=result.sections[0];const auto& group=section.groups[0];const auto& record=group.records[0];
        Check(Is(section.countSpan,20,21)&&Is(group.sourceFileSpan,22,30)&&Is(group.defaultTypeSpan,31,41)&&Is(group.countSpan,42,43),"Group metadata spans differ");
        Check(group.defaultTypeId==0xffffffff&&record.rawId==0&&record.text==payload&&Is(record.idSpan,44,45)&&Is(record.lengthSpan,46,47)&&Is(record.textSpan,48,55),"Raw ID/length/body spans or binary bytes changed");
        Check(Is(result.sections[1].countSpan,55,56),"Binary body was mistaken for metadata lines");
        const auto maximum=Read(One(Data("not a declaration\n0\nfile.def\n3\n1\n"),"raw label","4294967295","4294967295"));
        Check(maximum.sections[0].groups[0].records[0].rawId==0xffffffff&&maximum.sections[0].groups[0].records[0].text==Data("not a declaration\n0\nfile.def\n3\n1\n"),"Opaque payload/default type/raw ID acquired semantic restrictions");
    });
    Group("truncation extra sections and corrupt framing",[]{
        const auto b=One(Bytes{0,'\n','0','\n',255});for(std::size_t n=0;n<b.size();++n){const Bytes cut(b.begin(),b.begin()+n);RejectAny([&]{Read(cut);},"A truncated prefix was accepted");}
        for(const auto* tail:{"\n","0\n","extra"}){auto extra=b;Text(extra,tail);Reject([&]{Read(extra);},CompiledDeclFailure::InvalidData,"Trailing bytes/third section accepted");}
        auto bigEndian=Empty();bigEndian[3]=1;Reject([&]{Read(bigEndian);},CompiledDeclFailure::InvalidData,"Big-endian pool count interpreted heuristically");
        Reject([&]{Read(One(Data("x"),"file.def","3","9","4294967295"));},CompiledDeclFailure::InvalidData,"Payload length past EOF accepted");
        Bytes impossible;Word(impossible,0);Word(impossible,0);Text(impossible,"4294967295\n0\n");Reject([&]{Read(impossible);},CompiledDeclFailure::InvalidData,"Impossible group count allocated before framing bound");
        Bytes impossibleRecords;Word(impossibleRecords,0);Word(impossibleRecords,0);Text(impossibleRecords,"1\na\n0\n4294967295\n0\n");
        Reject([&]{Read(impossibleRecords);},CompiledDeclFailure::InvalidData,"Impossible record count allocated before framing bound");
    });
    Group("canonical LF metadata and uint32 domains",[]{
        for(const auto* number:{"-1","+1","00","01","1 "," 1","1.0","4294967296",""}){
            const auto b=One(Data("x"),"file.def",number);Reject([&]{Read(b);},CompiledDeclFailure::InvalidData,"Noncanonical/out-of-range numeric metadata admitted");
        }
        for(const auto* number:{"-1","01","4294967296"}){Reject([&]{Read(One(Data("x"),"file.def","3",number));},CompiledDeclFailure::InvalidData,"Invalid raw ID admitted");
            Reject([&]{Read(One(Data("x"),"file.def","3","9",number));},CompiledDeclFailure::InvalidData,"Invalid byte-length decimal admitted");}
        for(const auto& label:std::vector<std::string>{"",std::string("a\0b",3),"a\rb","a\tb",std::string(1,char(0x80))})
            Reject([&]{Read(One(Data("x"),label));},CompiledDeclFailure::InvalidData,"Empty/binary/CR metadata label admitted");
        auto cr=Empty();cr.insert(cr.begin()+9,'\r');Reject([&]{Read(cr);},CompiledDeclFailure::InvalidData,"CRLF count metadata was silently normalized");
        const auto spaces=Read(One(Data("x")," ../source label "));Check(spaces.sections[0].groups[0].sourceFile==" ../source label ","Source metadata label was path-normalized/trimmed");
    });
    Group("cumulative count and preallocation budgets",[]{
        const auto b=One(Data("x"));CompiledDeclLimits l;l.sourceBytes=b.size()-1;Reject([&]{Read(b,l);},CompiledDeclFailure::LimitExceeded,"Source-byte cap ignored");
        Bytes two;Word(two,1);Word(two,0);Word(two,1);Word(two,0xffffffff);Text(two,"1\nf\n0\n1\n0\n1\nX1\ng\n32\n1\n0\n1\nY");
        Check(Read(two).sections[1].groups[0].records[0].text==Data("Y"),"Two-section budget control malformed");
        l=CompiledDeclLimits();l.poolWords=1;Reject([&]{Read(two,l);},CompiledDeclFailure::LimitExceeded,"Word budget was reset per pool");
        l=CompiledDeclLimits();l.groups=1;Reject([&]{Read(two,l);},CompiledDeclFailure::LimitExceeded,"Group budget was reset per section");
        l=CompiledDeclLimits();l.records=1;Reject([&]{Read(two,l);},CompiledDeclFailure::LimitExceeded,"Record budget was reset per section/group");
        l=CompiledDeclLimits();l.textBytes=0;Reject([&]{Read(b,l);},CompiledDeclFailure::LimitExceeded,"Per-text cap ignored");
        l=CompiledDeclLimits();l.metadataBytes=8;Check(Read(b,l).sections[0].groups[0].sourceFile=="file.def","Exact metadata limit incorrectly includes LF");
        --l.metadataBytes;Reject([&]{Read(b,l);},CompiledDeclFailure::LimitExceeded,"Metadata byte cap ignored");
        l=CompiledDeclLimits();l.retainedBytes=0;Reject([&]{Read(Empty(),l);},CompiledDeclFailure::LimitExceeded,"Result object omitted from retained cap");
        l=CompiledDeclLimits();l.retainedBytes=80u<<10;const auto large=One(Bytes(64u<<10,0));const auto before=large;
        Reject([&]{Read(large,l);},CompiledDeclFailure::LimitExceeded,"Owning payload copy omitted from retained cap");Check(large==before,"Failed read mutated caller payload bytes");
        l=CompiledDeclLimits();l.retainedBytes=(std::numeric_limits<std::size_t>::max)();l.poolWords=(std::numeric_limits<std::size_t>::max)();
        Check(Read(b,l).sections[0].groups[0].records.size()==1,"Large limit values overflowed allocation arithmetic");
    });
    Group("owning output and source identity",[]{
        CompiledDeclarations result;std::uint64_t identity=0;{auto b=One(Bytes{0,255,'x'});identity=Hash(b);result=Read(b);std::fill(b.begin(),b.end(),0);}
        Check(result.sourceFingerprint==identity&&result.sections[0].groups[0].sourceFile=="file.def"&&result.sections[0].groups[0].records[0].text==Bytes({0,255,'x'}),"Output borrowed caller bytes/lifetime");
        auto b=Empty();const auto old=Read(b).sourceFingerprint;b[9]='\r';RejectAny([&]{Read(b);},"Malformed read altered a previous owning result");
        Check(result.sourceFingerprint==identity&&old!=identity,"Independent read/source identity corrupted previous output");
    });
    std::cout<<"WOLF2009_COMPILED_DECLARATIONS_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed="<<failed<<'\n';
}
void Original(const char* path,const std::string& resource="maps/game/trainyard/trainyard"){
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open selected owner MPK");const auto archive=Archive::Scan(input);
    std::size_t chunk=0,entry=0,matches=0;for(std::size_t c=0;c<archive.Chunks().size();++c)if(archive.Chunks()[c].type=="DECL")
        for(std::size_t e=0;e<archive.Chunks()[c].entries.size();++e)if(archive.Chunks()[c].entries[e].name==resource){chunk=c;entry=e;++matches;}
    Check(matches==1,"Exact selected DECL resource missing/ambiguous");Check(archive.Chunks()[chunk].entries[entry].size<=64u<<20,"Selected DECL source-byte budget");
    const auto bytes=archive.ReadEntry(input,chunk,entry);const auto result=Read(bytes);
    Check(result.sourceBytes==bytes.size()&&result.sourceFingerprint==Hash(bytes),"Original source identity differs");
    std::size_t cursor=0;const auto raw=[&](const CompiledDeclSpan& span){Check(span.end>=span.begin&&span.end<=bytes.size(),"Original metadata span is out of source range");return std::string(bytes.begin()+span.begin,bytes.begin()+span.end);};
    const auto line=[&](const CompiledDeclSpan& span,const std::string& value){Check(span.begin==cursor&&raw(span)==value&&span.end<bytes.size()&&bytes[span.end]=='\n',"Original metadata line/cursor differs");cursor=span.end+1;};
    for(const auto& pool:result.pools){Check(Is(pool.countSpan,cursor,cursor+4)&&pool.countSpan.end<=bytes.size(),"Original word-count span differs");cursor+=4;
        std::uint32_t count=0;for(unsigned i=0;i<4;++i)count|=std::uint32_t(bytes[pool.countSpan.begin+i])<<(8*i);Check(count==pool.words.size(),"Original word-count value differs");
        Check(Is(pool.dataSpan,cursor,cursor+4*pool.words.size())&&pool.dataSpan.end<=bytes.size(),"Original pool-data span differs");
        for(const auto word:pool.words){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes[cursor+i])<<(8*i);Check(value==word,"Original opaque word bits differ");cursor+=4;}}
    std::size_t count=0,payloadBytes=0;for(const auto& section:result.sections)for(const auto& group:section.groups)for(const auto& record:group.records){++count;payloadBytes+=record.text.size();
        Check(record.textSpan.end>=record.textSpan.begin&&record.textSpan.end-record.textSpan.begin==record.text.size()&&record.textSpan.end<=bytes.size()&&
            std::equal(record.text.begin(),record.text.end(),bytes.begin()+record.textSpan.begin),"Original raw payload span/copy differs");}
    for(const auto& section:result.sections){line(section.countSpan,std::to_string(section.groups.size()));for(const auto& group:section.groups){
        line(group.sourceFileSpan,group.sourceFile);line(group.defaultTypeSpan,std::to_string(group.defaultTypeId));line(group.countSpan,std::to_string(group.records.size()));
        for(const auto& record:group.records){line(record.idSpan,std::to_string(record.rawId));line(record.lengthSpan,std::to_string(record.text.size()));Check(record.textSpan.begin==cursor,"Original payload is not adjacent to its length metadata");cursor=record.textSpan.end;}}}
    Check(cursor==bytes.size(),"Original spans do not cover source exactly to EOF");
    if(resource=="maps/game/trainyard/trainyard"){
        Check(result.sourceBytes==2112750&&result.sourceFingerprint==UINT64_C(0xc8d044282ad8a99e),"Trainyard source identity differs from retained byte audit");
        Check(result.pools[0].words.empty()&&result.pools[1].words.size()==1467&&Is(result.pools[1].dataSpan,8,5876)&&result.pools[1].words[0]==0x3f800000,"Trainyard prefix changed");
        Check(result.sections[0].groups.size()==886&&result.sections[1].groups.size()==111&&Is(result.sections[0].countSpan,5876,5879)&&Is(result.sections[1].countSpan,1422636,1422639)&&count==3269&&payloadBytes==2027520,"Trainyard section/count profile changed");
        const auto& group=result.sections[1].groups[52];const auto& locker=group.records[0];Check(group.sourceFile=="def/mapobjects/animated_containers.def"&&group.defaultTypeId==3&&locker.rawId==334&&Is(locker.textSpan,1849093,1849634)&&locker.text.size()==541,"Selected trainyard record changed");
    }else if(resource=="maps/game/empty/empty"){
        Check(result.sourceBytes==357867&&result.sourceFingerprint==UINT64_C(0xa7104ca583f3ffbe),"Empty source identity differs from independent byte audit");
        Check(result.pools[0].words.empty()&&result.pools[1].words.size()==47&&Is(result.pools[1].dataSpan,8,196)&&
            result.sections[0].groups.size()==23&&result.sections[1].groups.size()==37&&Is(result.sections[0].countSpan,196,198)&&Is(result.sections[1].countSpan,177829,177831)&&
            count==641&&payloadBytes==350765,"Empty section/count/span profile changed");
    }
    std::cout<<"WOLF2009_COMPILED_DECLARATIONS_ORIGINAL_OK resource="<<resource<<" bytes="<<result.sourceBytes<<" fingerprint="<<std::hex<<result.sourceFingerprint<<std::dec
        <<" pools="<<result.pools[0].words.size()<<'/'<<result.pools[1].words.size()<<" sections="<<result.sections[0].groups.size()<<'/'<<result.sections[1].groups.size()
        <<" records="<<count<<" payload_bytes="<<payloadBytes<<" transport_only=1 native_type_or_index_import=0\n";
}
void OriginalDeclAbsence(const char* path){
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open selected global package for absence check");
    input.seekg(0,std::ios::end);const auto bytes=input.tellg();Check(bytes==23075785,"Selected global package byte-size identity changed");input.seekg(0);
    const auto archive=Archive::Scan(input);std::size_t declarations=0;for(const auto& chunk:archive.Chunks())if(chunk.type=="DECL")++declarations;
    Check(declarations==0,"Selected global SPK no longer has the recorded DECL-chunk absence");
    std::cout<<"WOLF2009_COMPILED_DECLARATIONS_PACKAGE_ABSENCE_OK package_bytes="<<bytes<<" DECL_chunks=0 transport_invoked=0 runtime_namespace_absence=0\n";
}
}
int main(int argc,char** argv){try{Synthetic();if(failed)return 1;for(int i=1;i<argc;++i){const std::string option(argv[i]);if(option=="--self-test")continue;
    if(option=="--source-map"&&i+1<argc){Original(argv[++i]);continue;}
    if(option=="--source-package"&&i+2<argc){const char* package=argv[++i];const std::string resource=argv[++i];Original(package,resource);continue;}
    if(option=="--source-decl-absence"&&i+1<argc){OriginalDeclAbsence(argv[++i]);continue;}
    throw std::runtime_error("Usage: [--self-test] [--source-map trainyard.mpk] [--source-package package exact-resource] [--source-decl-absence global.spk]");}return 0;
}catch(const std::exception& error){std::cerr<<"Compiled declarations: "<<error.what()<<'\n';return 1;}}

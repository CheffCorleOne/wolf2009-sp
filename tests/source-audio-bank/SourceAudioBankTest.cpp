// SPDX-License-Identifier: GPL-3.0-or-later
// Artificial packet layouts/bytes only. Optional original QA reads user files.
#include "SourceAudioBank.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
using namespace wolf2009;
typedef SourceAudioBankBytes Bytes;
unsigned checks=0,refusals=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void Rejects(const std::function<void()>& callback){bool rejected=false;try{callback();}catch(const std::runtime_error&){rejected=true;}Check(rejected,"Invalid whole-bank input was accepted");++refusals;}
void Put32(Bytes& data,std::uint32_t value){for(unsigned i=0;i<4;++i)data.push_back(static_cast<unsigned char>(value>>(8*i)));}
void Set32(Bytes& data,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)data.at(at+i)=static_cast<unsigned char>(value>>(8*i));}
std::uint32_t U32(const Bytes& data,std::size_t at){return std::uint32_t(data.at(at))|(std::uint32_t(data.at(at+1))<<8)|(std::uint32_t(data.at(at+2))<<16)|(std::uint32_t(data.at(at+3))<<24);}
void Align(Bytes& data){while(data.size()%4)data.push_back(0xa5);}
struct Entry {std::string name;Bytes bytes;};
struct Chunk {std::string type;std::vector<Entry> entries;};
Bytes Plain(const std::vector<Entry>& entries){Bytes result;Put32(result,std::uint32_t(entries.size()));
    for(const auto& entry:entries){result.insert(result.end(),entry.name.begin(),entry.name.end());result.push_back(0);Put32(result,std::uint32_t(entry.bytes.size()));Put32(result,std::uint32_t(entry.bytes.size()));}
    Align(result);for(const auto& entry:entries){result.insert(result.end(),entry.bytes.begin(),entry.bytes.end());Align(result);}return result;}
Bytes Packed(const std::string& type,const Bytes& plain){Check(type.size()==4,"Own fixture FourCC size");uLongf size=compressBound(uLong(plain.size()));Bytes compressed(size);
    Check(compress2(compressed.data(),&size,plain.data(),uLong(plain.size()),6)==Z_OK,"Own synthetic compression failed");compressed.resize(size);
    Bytes result;for(unsigned i=0;i<4;++i)result.push_back(static_cast<unsigned char>(type[3-i]));Put32(result,std::uint32_t(plain.size()));Put32(result,std::uint32_t(compressed.size()));
    result.insert(result.end(),compressed.begin(),compressed.end());return result;}
Bytes Package(const std::vector<Chunk>& chunks){Bytes result;Put32(result,300);for(const auto& chunk:chunks){const auto packed=Packed(chunk.type,Plain(chunk.entries));result.insert(result.end(),packed.begin(),packed.end());}return result;}
std::vector<Chunk> Own(){return {{"SNDS",{{"sound/a",{'h','e','l','l','o'}},{"sOuNd\\B",{4,5}}}},{"SNDS",{}},{"SNDS",{{"audio/c.mp3",{0,0,0,0}}}}};}
SourceAudioBankRequest Request(){SourceAudioBankRequest result;result.policy=SourceAudioBankPolicy::SndsOnlyEncodedV1;result.exactFilename="streampacks/OwnBank.SPK";return result;}
SourceAudioBankPlan Prepare(const Bytes& bytes,const SourceAudioBankRequest& request=Request(),const SourceAudioBankLimits& limits=SourceAudioBankLimits()){
    std::istringstream input(std::string(bytes.begin(),bytes.end()),std::ios::in|std::ios::binary);return SourceAudioBankPlan::Prepare(input,request.exactFilename,request,limits);}
void Synthetic(){const auto source=Package(Own());auto plan=Prepare(source);Check(plan.Valid()&&plan.SourceBytes()==source,"Data plan did not retain the complete source snapshot");
    Check(plan.Request().exactFilename==Request().exactFilename&&plan.Identity().rawFilename==Request().exactFilename&&plan.Identity().version==300&&plan.Identity().sourceBytes==source.size(),"Exact bank source identity was rewritten");
    Check(plan.Identity().rawVersionHeader==std::array<unsigned char,4>{{0x2c,1,0,0}},"Raw version header changed");
    Check(plan.Chunks().size()==3&&plan.Members().size()==3&&plan.Identity().totalEncodedBytes==11,"Whole bank omitted a chunk/member");
    Check(plan.Chunks()[0].firstMember==0&&plan.Chunks()[0].members==2&&plan.Chunks()[1].firstMember==2&&plan.Chunks()[1].members==0&&plan.Chunks()[2].firstMember==2&&plan.Chunks()[2].members==1,"Empty chunk or member order was silently discarded");
    Check(plan.Members()[0].chunkOrdinal==0&&plan.Members()[0].entryOrdinal==0&&plan.Members()[0].payloadOffset==36&&plan.Members()[1].payloadOffset==44&&plan.Members()[2].payloadOffset==24,"Inflated offsets/ordinals differ from independent artificial layout");
    Check(plan.Members()[0].encoded==Bytes({'h','e','l','l','o'})&&plan.Members()[0].encodedLength==5&&plan.Members()[0].encodedFingerprint==UINT64_C(0xa430d84680aabd0b),"Encoded bytes/length or standard FNV1a hello vector differs");
    Check(plan.Members()[1].rawName=="sOuNd\\B"&&plan.Members()[1].lookupName=="sound/b"&&plan.Members()[2].encoded==Bytes(4,0),"Raw source names/opaque silent-looking bytes changed");
    Check(plan.FindExact("SNDS","sOuNd\\B")==&plan.Members()[1]&&!plan.FindExact("SNDS","sound/b")&&!plan.FindExact("snds","sound/a"),"Raw identity lookup guessed aliases/types");
    std::size_t end=4;for(const auto& chunk:plan.Chunks()){Check(chunk.headerOffset==end&&chunk.dataOffset==end+12&&chunk.rawType=="SNDS","Chunk coverage/order/header changed");
        Check(std::equal(chunk.rawHeader.begin(),chunk.rawHeader.end(),source.begin()+end)&&chunk.compressedBytes==U32(source,end+8)&&chunk.inflatedBytes==U32(source,end+4),"Raw chunk header/lengths were synthesized incorrectly");end+=12+chunk.compressedBytes;}
    Check(end==source.size(),"Accepted plan did not cover exact archive EOF");
    // This fixture inflates48+4+28 bytes while scanning, then48 twice and28
    // once for individual reads:204 total. Retained payload bytes alone (11)
    // cannot bound that repeated archive work.
    {SourceAudioBankLimits limits;limits.inflatedWorkBytes=204;Check(Prepare(source,Request(),limits).Members().size()==3,"Exact complete inflation-work budget was refused");}
    for(std::size_t budget:{79u,80u,203u}){SourceAudioBankLimits limits;limits.inflatedWorkBytes=budget;Rejects([&]{Prepare(source,Request(),limits);});}
    {
        const auto& c=plan.Chunks();const std::size_t compressedWork=3*U32(source,std::size_t(c[0].headerOffset)+8)+U32(source,std::size_t(c[1].headerOffset)+8)+2*U32(source,std::size_t(c[2].headerOffset)+8);
        SourceAudioBankLimits limits;limits.compressedWorkBytes=compressedWork;Check(Prepare(source,Request(),limits).Members().size()==3,"Exact complete compressed-input work budget was refused");
        --limits.compressedWorkBytes;Rejects([&]{Prepare(source,Request(),limits);});limits.compressedWorkBytes=1;Rejects([&]{Prepare(source,Request(),limits);});
    }
    {
        // Tiny input claiming128 maximum-sized chunks must hit the aggregate
        // header-work gate BEFORE a zlib scanner allocates/inflates even one.
        Bytes claims;Put32(claims,300);for(unsigned i=0;i<128;++i){claims.insert(claims.end(),{'S','D','N','S'});Put32(claims,64u<<20);Put32(claims,1);claims.push_back(0);}
        bool budgetBeforeZlib=false;try{Prepare(claims);}catch(const std::runtime_error& error){budgetBeforeZlib=std::string(error.what()).find("scan inflation-work budget")!=std::string::npos;}
        Check(budgetBeforeZlib,"Aggregate inflated work was not rejected before Archive::Scan/zlib");++refusals;
    }
    auto retained=plan;plan=SourceAudioBankPlan();Check(!plan.Valid()&&retained.Members()[0].encodedLength==5,"Shared immutable data owner lost bytes");
    {auto mutableSource=source;const auto copy=Prepare(mutableSource);std::fill(mutableSource.begin(),mutableSource.end(),0);Check(copy.SourceBytes()==source&&copy.Members()[0].encoded==retained.Members()[0].encoded,"Caller source mutation changed owned encoded data");}
    Rejects([&]{plan.Identity();});
    {auto request=Request();request.policy=SourceAudioBankPolicy::Unspecified;Rejects([&]{Prepare(source,request);});}
    {std::istringstream input(std::string(source.begin(),source.end()));Rejects([&]{SourceAudioBankPlan::Prepare(input,"streampacks/ownbank.spk",Request());});}
    for(const char* name:{"/absolute.spk","../bank.spk","C:\\bank.spk","packs//bank.spk","pack.mpk"}){auto request=Request();request.exactFilename=name;Rejects([&]{Prepare(source,request);});}
    {auto request=Request();request.expectedSourceFingerprint=1;Rejects([&]{Prepare(source,request);});}
    {auto expected=Request();expected.expectedSourceIdentityKnown=true;expected.expectedSourceBytes=retained.Identity().sourceBytes;expected.expectedSourceFingerprint=retained.Identity().sourceFingerprint;
        Check(Prepare(source,expected).Identity().sourceFingerprint==expected.expectedSourceFingerprint,"Known source identity was refused");
        auto changed=Own();changed[0].entries[0].bytes[0]='H';const auto changedBytes=Package(changed);Rejects([&]{Prepare(changedBytes,expected);});++expected.expectedSourceBytes;Rejects([&]{Prepare(source,expected);});}
    {auto changed=Own();changed.push_back(Chunk{"DECL",{{"opaque",{1,2,3}}}});Rejects([&]{Prepare(Package(changed));});}
    {auto changed=Own();changed[0].type="snds";Rejects([&]{Prepare(Package(changed));});}
    for(const char* name:{"sound/a","SOUND/A","SOUND\\A"}){auto changed=Own();changed[2].entries[0].name=name;Rejects([&]{Prepare(Package(changed));});}
    {auto changed=Own();changed[0].entries[1].name="sound/a";Rejects([&]{Prepare(Package(changed));});}
    for(const auto& names:{std::make_pair("sound/x","sound/x.mp3"),std::make_pair("sound/x.MP3","SOUND\\X"),std::make_pair("sound/x.mp3","sound/x.mp3.mp3")}){
        auto changed=Own();changed[0].entries[0].name=names.first;changed[0].entries[1].name=names.second;Rejects([&]{Prepare(Package(changed));});}
    for(const char* name:{"../escape","/sound/a","sound//a","sound:a","sound/\001a"}){auto changed=Own();changed[0].entries[0].name=name;Rejects([&]{Prepare(Package(changed));});}
    {auto changed=Own();changed[2].entries[0].bytes.clear();Rejects([&]{Prepare(Package(changed));});}
    Rejects([&]{Prepare(Package({}));});Rejects([&]{Prepare(Package({{"SNDS",{}}}));});
    {auto broken=source;broken.pop_back();Rejects([&]{Prepare(broken);});}
    {auto broken=source;broken.push_back(0);Rejects([&]{Prepare(broken);});}
    {auto broken=source;broken[0]=0;Rejects([&]{Prepare(broken);});}
    {auto broken=source;broken.back()^=1;Rejects([&]{Prepare(broken);});}
    {auto broken=source;Set32(broken,8,U32(broken,8)+1);Rejects([&]{Prepare(broken);});}
    {const auto own=Own();auto plain=Plain(own[0].entries);Set32(plain,16,6);Bytes broken;Put32(broken,300);const auto packed=Packed("SNDS",plain);broken.insert(broken.end(),packed.begin(),packed.end());Rejects([&]{Prepare(broken);});}
    {Bytes broken;Put32(broken,300);auto packed=Packed("SNDS",Plain(Own()[0].entries));Set32(packed,8,U32(packed,8)+1);packed.push_back(0);broken.insert(broken.end(),packed.begin(),packed.end());Rejects([&]{Prepare(broken);});}
    // A valid first member/chunk cannot hide a corrupt final chunk or an empty
    // late member. Assignment publishes the new owner only after full coverage.
    {auto prior=retained;auto broken=source;broken.back()^=1;Rejects([&]{prior=Prepare(broken);});Check(prior.SourceBytes()==source&&prior.Members().size()==3,"Late failure replaced a previously retained whole bank");}
    for(unsigned which=0;which<11;++which){SourceAudioBankLimits limits;
        if(which==0)limits.sourceBytes=source.size()-1;if(which==1)limits.chunkBytes=4;if(which==2)limits.chunks=2;if(which==3)limits.members=2;
        if(which==4)limits.memberBytes=4;if(which==5)limits.encodedBytes=10;if(which==6)limits.filenameBytes=Request().exactFilename.size()-1;
        if(which==7)limits.nameBytes=5;if(which==8)limits.totalNameBytes=10;if(which==9)limits.retainedBytes=source.size();if(which==10)limits.archive.compressedChunkBytes=1;
        Rejects([&]{Prepare(source,Request(),limits);});}
    std::cout<<"SOURCE_AUDIO_BANK_SYNTHETIC_OK checks="<<checks<<" refusals="<<refusals<<" chunks=3 members=3 source_owned=1 complete_coverage=1 codec_opaque=1\n";
}
void Original(const char* path){std::ifstream input(path,std::ios::binary);Check(bool(input),"Owner-supplied original bank is absent");SourceAudioBankRequest request;
    request.policy=SourceAudioBankPolicy::SndsOnlyEncodedV1;request.exactFilename="streampacks/scripted_voice_kc1_trn_vo_english.spk";
    const auto plan=SourceAudioBankPlan::Prepare(input,request.exactFilename,request);Check(plan.Identity().sourceBytes==385486&&plan.Chunks().size()==1&&plan.Members().size()==19&&plan.Identity().totalEncodedBytes==389672,"Original bank differs from the independently audited whole package");
    Check(plan.Members().front().rawName=="sound/vo/kreisau_agent_1/trn_0505_ka1"&&plan.Members().front().encodedLength==47350,"Original first member differs");
    for(const auto& member:plan.Members()){Check(member.rawType=="SNDS"&&member.encodedLength==member.encoded.size()&&!member.encoded.empty()&&plan.FindExact(member.rawType,member.rawName)==&member,"Original full member coverage/lookup failed");
        std::cout<<"SOURCE_AUDIO_BANK_MEMBER chunk="<<member.chunkOrdinal<<" entry="<<member.entryOrdinal<<" type="<<member.rawType<<" name="<<member.rawName<<" bytes="<<member.encodedLength<<" fnv="<<std::hex<<std::setw(16)<<std::setfill('0')<<member.encodedFingerprint<<std::dec<<'\n';}
    std::cout<<"SOURCE_AUDIO_BANK_ORIGINAL_OK chunks="<<plan.Chunks().size()<<" members="<<plan.Members().size()<<" source_bytes="<<plan.Identity().sourceBytes<<" member_bytes="<<plan.Identity().totalEncodedBytes<<" source_fnv="<<std::hex<<std::setw(16)<<std::setfill('0')<<plan.Identity().sourceFingerprint<<std::dec<<" encoded_data_plan_only=1\n";
}
}
int main(int argc,char** argv){try{if(argc==2&&std::string(argv[1])=="--self-test")Synthetic();else if(argc==3&&std::string(argv[1])=="--original")Original(argv[2]);else throw std::runtime_error("Usage: --self-test | --original owner-bank.spk");return 0;}
catch(const std::exception& error){std::cerr<<"SOURCE_AUDIO_BANK_FAIL checks="<<checks<<" reason="<<error.what()<<'\n';return 1;}}

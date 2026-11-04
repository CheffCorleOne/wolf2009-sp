// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceAudioBank.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <streambuf>
#include <utility>
namespace wolf2009 {
namespace {
typedef SourceAudioBankBytes Bytes;
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::uint64_t Fingerprint(const unsigned char* bytes,std::size_t size){std::uint64_t value=UINT64_C(14695981039346656037);
    for(std::size_t i=0;i<size;++i)value=(value^bytes[i])*UINT64_C(1099511628211);return value;}
std::string LookupName(std::string name,std::size_t maximum){
    Need(!name.empty()&&name.size()<=maximum,"Audio bank virtual name is empty or exceeds its budget");
    for(char& c:name){const unsigned char byte=static_cast<unsigned char>(c);
        Need(byte>=32&&byte<127&&c!=':'&&c!='"'&&c!='{'&&c!='}',"Audio bank requires safe bounded ASCII virtual names");
        if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
    Need(name.front()!='/'&&name.back()!='/',"Audio bank virtual name is absolute or has an empty final component");
    for(std::size_t begin=0;begin<name.size();){const auto end=name.find('/',begin);const auto part=name.substr(begin,end-begin);
        Need(!part.empty()&&part!="."&&part!="..","Audio bank virtual name has an unsafe component");if(end==std::string::npos)break;begin=end+1;}
    return name;
}
bool Ends(const std::string& name,const char* ending){const std::string suffix(ending);return name.size()>=suffix.size()&&name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0;}
void Charge(std::size_t& value,std::size_t amount,std::size_t maximum,const char* why){Need(value<=maximum&&amount<=maximum-value,why);value+=amount;}
void ChargeRepeated(std::size_t& value,std::size_t amount,std::size_t count,std::size_t maximum,const char* why){
    Need(value<=maximum&&(!count||amount<=(maximum-value)/count),why);value+=amount*count;
}
std::uint32_t U32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
struct ArchiveWork {std::size_t inflated=0,compressed=0;};
ArchiveWork PreflightInflation(const Bytes& bytes,const Limits& archive,const SourceAudioBankLimits& limits){
    Need(bytes.size()>=4&&U32(bytes.data())==300,"Audio bank source version differs");std::size_t at=4,chunks=0;ArchiveWork work;
    while(at<bytes.size()){
        Need(chunks<archive.chunks&&bytes.size()-at>=12,"Audio bank compressed-header count/range budget");++chunks;
        const auto* h=bytes.data()+at;const std::string type{char(h[3]),char(h[2]),char(h[1]),char(h[0])};
        Need(type=="SNDS","Audio bank profile refuses an unexpected typed chunk");
        const std::uint32_t inflated=U32(h+4),compressed=U32(h+8);
        Need(compressed&&compressed<=archive.compressedChunkBytes&&compressed<=bytes.size()-at-12&&inflated>=4&&inflated<=archive.inflatedChunkBytes,
             "Audio bank compressed-header size/range budget");
        Charge(work.inflated,inflated,limits.inflatedWorkBytes,"Audio bank aggregate scan inflation-work budget");
        Charge(work.compressed,compressed,limits.compressedWorkBytes,"Audio bank aggregate scan compressed-work budget");at+=12+std::size_t(compressed);
    }
    Need(at==bytes.size(),"Audio bank compressed headers do not cover exact EOF");return work;
}
Bytes Snapshot(std::istream& input,const SourceAudioBankLimits& limits){
    input.clear();input.seekg(0,std::ios::end);const std::streamoff length=input.tellg();
    Need(length>=4&&std::uint64_t(length)<=limits.sourceBytes&&std::uint64_t(length)<=std::uint64_t((std::numeric_limits<std::size_t>::max)())&&
         std::uint64_t(length)<=std::uint64_t((std::numeric_limits<std::streamsize>::max)()),"Audio bank source size/seek budget");
    Need(std::uint64_t(length)<=limits.retainedBytes,"Audio bank source exceeds retained-data budget");
    input.clear();input.seekg(0,std::ios::beg);Need(bool(input),"Audio bank source seek failed");Bytes bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));Need(bool(input)&&input.gcount()==std::streamsize(bytes.size()),"Short audio bank source snapshot");
    char extra=0;input.read(&extra,1);Need(input.gcount()==0&&input.eof()&&!input.bad(),"Audio bank source grew or failed at EOF");
    input.clear();input.seekg(0,std::ios::end);Need(bool(input)&&input.tellg()==length,"Audio bank source length changed during snapshot");return bytes;
}
// Archive reads one immutable snapshot, never a live source that could change
// between table scan and individual entry reads. This adapter exposes no writes.
class ReadBuffer : public std::streambuf {
  public:
    explicit ReadBuffer(const Bytes& source){auto* begin=const_cast<char*>(reinterpret_cast<const char*>(source.data()));setg(begin,begin,begin+source.size());}
  protected:
    pos_type seekoff(off_type off,std::ios_base::seekdir direction,std::ios_base::openmode mode)override{
        if(!(mode&std::ios_base::in)||(mode&std::ios_base::out))return pos_type(off_type(-1));
        const off_type size=egptr()-eback(),base=direction==std::ios_base::beg?0:direction==std::ios_base::cur?gptr()-eback():direction==std::ios_base::end?size:-1;
        if(base<0||off<-base||off>size-base)return pos_type(off_type(-1));const off_type at=base+off;setg(eback(),eback()+at,egptr());return pos_type(at);
    }
    pos_type seekpos(pos_type position,std::ios_base::openmode mode)override{return seekoff(off_type(position),std::ios_base::beg,mode);}
};
}
struct SourceAudioBankPlan::Impl {
    SourceAudioBankRequest request;
    SourceAudioBankIdentity identity;
    Bytes source;
    std::vector<SourceAudioBankChunk> chunks;
    std::vector<SourceAudioBankMember> members;
};
SourceAudioBankPlan::SourceAudioBankPlan(std::shared_ptr<const Impl> value):impl_(std::move(value)){}
const SourceAudioBankRequest& SourceAudioBankPlan::Request()const{Need(Valid(),"Empty audio bank data plan");return impl_->request;}
const SourceAudioBankIdentity& SourceAudioBankPlan::Identity()const{Need(Valid(),"Empty audio bank data plan");return impl_->identity;}
const SourceAudioBankBytes& SourceAudioBankPlan::SourceBytes()const{Need(Valid(),"Empty audio bank data plan");return impl_->source;}
const std::vector<SourceAudioBankChunk>& SourceAudioBankPlan::Chunks()const{Need(Valid(),"Empty audio bank data plan");return impl_->chunks;}
const std::vector<SourceAudioBankMember>& SourceAudioBankPlan::Members()const{Need(Valid(),"Empty audio bank data plan");return impl_->members;}
const SourceAudioBankMember* SourceAudioBankPlan::FindExact(const std::string& type,const std::string& name)const{Need(Valid(),"Empty audio bank data plan");
    for(const auto& member:impl_->members)if(member.rawType==type&&member.rawName==name)return &member;return NULL;}
SourceAudioBankPlan SourceAudioBankPlan::Prepare(std::istream& input,const std::string& suppliedFilename,const SourceAudioBankRequest& request,const SourceAudioBankLimits& limits){
    Need(request.policy==SourceAudioBankPolicy::SndsOnlyEncodedV1,"Explicit SNDS-only encoded audio bank policy required");
    const auto packageKey=LookupName(request.exactFilename,limits.filenameBytes);LookupName(suppliedFilename,limits.filenameBytes);
    Need(request.exactFilename==suppliedFilename&&Ends(packageKey,".spk"),"Audio bank exact source filename differs or is not an explicit SPK");
    Need(request.expectedSourceIdentityKnown?(request.expectedSourceBytes>=4&&request.expectedSourceBytes<=limits.sourceBytes):
         (!request.expectedSourceBytes&&!request.expectedSourceFingerprint),"Audio bank expected source identity is incomplete");
    std::shared_ptr<Impl> value(new Impl);value->request=request;value->source=Snapshot(input,limits);
    auto& identity=value->identity;identity.policy=request.policy;identity.rawFilename=suppliedFilename;identity.sourceBytes=value->source.size();
    identity.sourceFingerprint=Fingerprint(value->source.data(),value->source.size());std::copy(value->source.begin(),value->source.begin()+4,identity.rawVersionHeader.begin());
    Need(!request.expectedSourceIdentityKnown||(request.expectedSourceBytes==identity.sourceBytes&&request.expectedSourceFingerprint==identity.sourceFingerprint),"Audio bank source bytes differ from the exact requested identity");
    std::size_t retained=0,names=0,encoded=0;Charge(retained,sizeof(Impl),limits.retainedBytes,"Audio bank retained metadata budget");
    Charge(retained,value->source.size(),limits.retainedBytes,"Audio bank retained source budget");
    Charge(retained,request.exactFilename.size(),limits.retainedBytes,"Audio bank retained filename budget");Charge(retained,suppliedFilename.size(),limits.retainedBytes,"Audio bank retained filename budget");
    ReadBuffer buffer(value->source);std::istream snapshot(&buffer);Limits archiveLimits=limits.archive;
    archiveLimits.chunks=(std::min)(archiveLimits.chunks,limits.chunks);archiveLimits.entries=(std::min)(archiveLimits.entries,limits.members);
    archiveLimits.nameBytes=(std::min)(archiveLimits.nameBytes,limits.nameBytes);
    archiveLimits.totalNameBytes=(std::min)(archiveLimits.totalNameBytes,limits.totalNameBytes);
    archiveLimits.inflatedChunkBytes=(std::min)(archiveLimits.inflatedChunkBytes,limits.chunkBytes);
    // The generic scanner inflates every chunk. Bound their aggregate work
    // from compressed headers before invoking it, not after the allocation.
    ArchiveWork work=PreflightInflation(value->source,archiveLimits,limits);
    const auto archive=Archive::Scan(snapshot,archiveLimits);const auto& sourceChunks=archive.Chunks();
    Need(!sourceChunks.empty(),"Audio bank contains no chunks");identity.version=300;value->chunks.reserve(sourceChunks.size());std::size_t totalMembers=0;
    std::set<std::string> lookupNames;
    for(const auto& chunk:sourceChunks){Need(chunk.type=="SNDS","Audio bank profile refuses an unexpected typed chunk");
        Need(chunk.entries.size()<=limits.members-totalMembers,"Audio bank member count budget");totalMembers+=chunk.entries.size();
        // ReadEntry reinflates and reparses this complete chunk for each member.
        // Checked multiplication prevents small retained banks from authorizing
        // unbounded repeated decode/table work, including on 32-bit hosts.
        ChargeRepeated(work.inflated,chunk.inflatedSize,chunk.entries.size(),limits.inflatedWorkBytes,"Audio bank per-entry repeated inflation-work budget");
        ChargeRepeated(work.compressed,chunk.compressedSize,chunk.entries.size(),limits.compressedWorkBytes,"Audio bank per-entry repeated compressed-work budget");
        for(const auto& entry:chunk.entries){const auto lookup=LookupName(entry.name,limits.nameBytes);Need(lookupNames.insert(lookup).second,"Audio bank has duplicate/case/slash-colliding SNDS identities");
            Need(entry.size>0&&entry.size<=limits.memberBytes,"Audio bank empty/oversized encoded member");Charge(encoded,entry.size,limits.encodedBytes,"Audio bank aggregate encoded byte budget");
            Charge(names,entry.name.size(),limits.totalNameBytes,"Audio bank aggregate source-name budget");
            Charge(retained,sizeof(SourceAudioBankMember),limits.retainedBytes,"Audio bank retained member metadata budget");
            for(const auto size:{entry.name.size(),lookup.size(),chunk.type.size(),std::size_t(entry.size)})Charge(retained,size,limits.retainedBytes,"Audio bank retained member/name/byte budget");}
        Charge(retained,sizeof(SourceAudioBankChunk)+chunk.type.size(),limits.retainedBytes,"Audio bank retained chunk metadata budget");
    }
    Need(totalMembers>0,"Audio bank contains no encoded members");
    // The actual SNDS VFS route checks both exact name and a stripped .mp3
    // suffix. Such a pair cannot identify one sample unambiguously. This is
    // transport-name qualification, not codec inference from an extension.
    for(const auto& name:lookupNames)if(Ends(name,".mp3"))Need(!lookupNames.count(name.substr(0,name.size()-4)),"Audio bank SNDS .mp3 transport alias collides with another raw name");
    value->members.reserve(totalMembers);
    for(std::size_t ci=0;ci<sourceChunks.size();++ci){const auto& source=sourceChunks[ci];SourceAudioBankChunk chunk;
        chunk.ordinal=ci;chunk.firstMember=value->members.size();chunk.members=source.entries.size();chunk.rawType=source.type;
        chunk.headerOffset=source.headerOffset;chunk.dataOffset=source.headerOffset+12;chunk.compressedBytes=source.compressedSize;chunk.inflatedBytes=source.inflatedSize;
        Need(chunk.dataOffset<=value->source.size()&&chunk.compressedBytes<=value->source.size()-std::size_t(chunk.dataOffset),"Audio bank chunk snapshot range differs");
        std::copy(value->source.begin()+std::size_t(chunk.headerOffset),value->source.begin()+std::size_t(chunk.dataOffset),chunk.rawHeader.begin());
        chunk.compressedFingerprint=Fingerprint(value->source.data()+std::size_t(chunk.dataOffset),chunk.compressedBytes);
        for(std::size_t ei=0;ei<source.entries.size();++ei){const auto& entry=source.entries[ei];SourceAudioBankMember member;
            member.chunkOrdinal=ci;member.entryOrdinal=ei;member.rawType=source.type;member.rawName=entry.name;member.lookupName=LookupName(entry.name,limits.nameBytes);
            member.payloadOffset=entry.payloadOffset;member.encoded=archive.ReadEntry(snapshot,ci,ei);member.encodedLength=member.encoded.size();
            Need(member.encodedLength==entry.size,"Audio bank encoded member length differs from its source header");member.encodedFingerprint=Fingerprint(member.encoded.data(),member.encoded.size());
            value->members.push_back(std::move(member));}
        value->chunks.push_back(std::move(chunk));
    }
    Need(value->members.size()==totalMembers&&value->chunks.size()==sourceChunks.size(),"Audio bank complete manifest coverage differs");identity.totalEncodedBytes=encoded;
    return SourceAudioBankPlan(std::move(value));
}
}

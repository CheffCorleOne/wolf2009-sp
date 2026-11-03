// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarations.h"
#include <limits>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
CompiledDeclSpan Span(std::size_t begin,std::size_t end) { CompiledDeclSpan out;out.begin=begin;out.end=end;return out; }
struct Reader {
    const Bytes& bytes;
    const CompiledDeclLimits& limits;
    std::size_t position=0,retained=0,words=0,groups=0,records=0;
    Reader(const Bytes& input,const CompiledDeclLimits& cap):bytes(input),limits(cap){}
    void Require(bool condition,const char* reason,CompiledDeclFailure failure=CompiledDeclFailure::InvalidData) const {
        if(!condition)throw CompiledDeclError(failure,position,reason);
    }
    void Add(std::size_t& total,std::size_t amount,std::size_t limit,const char* reason) const {
        Require(total<=limit&&amount<=limit-total,reason,CompiledDeclFailure::LimitExceeded);total+=amount;
    }
    void Keep(std::size_t count,std::size_t width) {
        // Check multiplication and both conservative capacity charges before
        // the caller allocates or iterates over the declared count.
        Require(retained<=limits.retainedBytes&&(!width||count<=(limits.retainedBytes-retained)/width),
                "Compiled declaration retained storage budget",CompiledDeclFailure::LimitExceeded);
        const std::size_t size=count*width;Add(retained,size,limits.retainedBytes,"Compiled declaration retained storage budget");
        Add(retained,size,limits.retainedBytes,"Compiled declaration retained capacity budget");
    }
    std::uint32_t Word() {
        Require(position<=bytes.size()&&bytes.size()-position>=4,"Truncated compiled declaration word");
        const auto value=std::uint32_t(bytes[position])|(std::uint32_t(bytes[position+1])<<8)|
                         (std::uint32_t(bytes[position+2])<<16)|(std::uint32_t(bytes[position+3])<<24);
        position+=4;return value;
    }
    CompiledDeclSpan Line() {
        const auto begin=position;
        while(true){
            Require(position<bytes.size(),"Unterminated compiled declaration metadata line");
            const unsigned char c=bytes[position];
            if(c=='\n'){const auto end=position++;return Span(begin,end);}
            Require(position-begin<limits.metadataBytes,"Compiled declaration metadata line budget",CompiledDeclFailure::LimitExceeded);
            Require(c>=32&&c<=126,"Compiled declaration metadata requires printable ASCII and LF");++position;
        }
    }
    std::uint32_t Decimal(CompiledDeclSpan& source) {
        source=Line();Require(source.begin!=source.end,"Empty compiled declaration decimal");
        Require(source.end-source.begin==1||bytes[source.begin]!='0',"Noncanonical compiled declaration decimal");
        std::uint32_t value=0;
        for(std::size_t i=source.begin;i<source.end;++i){
            const unsigned char c=bytes[i];Require(c>='0'&&c<='9',"Invalid compiled declaration unsigned decimal");
            const auto digit=std::uint32_t(c-'0');
            Require(value<=((std::numeric_limits<std::uint32_t>::max)()-digit)/10,"Compiled declaration decimal overflows uint32");
            value=value*10+digit;
        }
        return value;
    }
    void Pool(CompiledDeclWordPool& pool) {
        const auto countBegin=position;const auto count=Word();pool.countSpan=Span(countBegin,position);
        Require(count<=(bytes.size()-position)/4,"Compiled declaration pool exceeds source bytes");
        Add(words,count,limits.poolWords,"Compiled declaration total pool-word budget");Keep(count,sizeof(std::uint32_t));
        const auto begin=position;pool.words.reserve(count);
        for(std::uint32_t i=0;i<count;++i)pool.words.push_back(Word());
        pool.dataSpan=Span(begin,position);
    }
    void Record(CompiledDeclRecord& record) {
        record.rawId=Decimal(record.idSpan);const auto length=Decimal(record.lengthSpan);
        Require(length<=bytes.size()-position,"Compiled declaration payload exceeds source bytes");
        Require(length<=limits.textBytes,"Compiled declaration per-record byte budget",CompiledDeclFailure::LimitExceeded);
        Keep(length,sizeof(unsigned char));const auto begin=position;position+=length;record.textSpan=Span(begin,position);
        record.text.assign(bytes.begin()+begin,bytes.begin()+position);
    }
    void Group(CompiledDeclGroup& group) {
        group.sourceFileSpan=Line();const auto length=group.sourceFileSpan.end-group.sourceFileSpan.begin;
        Require(length!=0,"Empty compiled declaration source-file label");Keep(length,sizeof(char));Keep(1,sizeof(char));
        group.sourceFile.assign(reinterpret_cast<const char*>(bytes.data()+group.sourceFileSpan.begin),length);
        group.defaultTypeId=Decimal(group.defaultTypeSpan);const auto count=Decimal(group.countSpan);
        Require(count<=(bytes.size()-position)/4,"Compiled declaration record count exceeds minimum framing bytes");
        Add(records,count,limits.records,"Compiled declaration total record budget");Keep(count,sizeof(CompiledDeclRecord));
        group.records.resize(count);for(auto& record:group.records)Record(record);
    }
    void Section(CompiledDeclSection& section) {
        const auto count=Decimal(section.countSpan);
        Require(count<=(bytes.size()-position)/6,"Compiled declaration group count exceeds minimum framing bytes");
        Add(groups,count,limits.groups,"Compiled declaration total group budget");Keep(count,sizeof(CompiledDeclGroup));
        section.groups.resize(count);for(auto& group:section.groups)Group(group);
    }
};
}
CompiledDeclarations CompiledDeclarations::Read(const std::vector<unsigned char>& bytes,CompiledDeclFormat format,const CompiledDeclLimits& limits) {
    if(format!=CompiledDeclFormat::TwoSectionLfTextV1)throw CompiledDeclError(CompiledDeclFailure::Unsupported,0,"An explicit compiled declaration transport profile is required");
    Reader reader(bytes,limits);
    reader.Require(bytes.size()<=limits.sourceBytes&&bytes.size()<=std::size_t((std::numeric_limits<std::ptrdiff_t>::max)()),
                   "Compiled declaration source byte budget",CompiledDeclFailure::LimitExceeded);
    reader.Keep(1,sizeof(CompiledDeclarations));CompiledDeclarations out;
    for(auto& pool:out.pools)reader.Pool(pool);
    for(auto& section:out.sections)reader.Section(section);
    reader.Require(reader.position==bytes.size(),"Trailing bytes after compiled declaration sections");
    out.sourceBytes=bytes.size();out.sourceFingerprint=UINT64_C(14695981039346656037);
    for(const auto byte:bytes){out.sourceFingerprint^=byte;out.sourceFingerprint*=UINT64_C(1099511628211);}
    return out;
}
} // namespace wolf2009

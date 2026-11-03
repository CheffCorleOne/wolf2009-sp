// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarationCatalog.h"
#include <limits>
#include <map>
#include <utility>

namespace wolf2009 {
namespace {
void Need(bool condition,const char* reason,std::size_t offset=0,CompiledDeclFailure category=CompiledDeclFailure::InvalidData) {
    if(!condition)throw CompiledCatalogError(category,offset,reason);
}
void Charge(std::size_t& used,std::size_t amount,std::size_t limit,const char* reason) {
    Need(used<=limit&&amount<=limit-used,reason,0,CompiledDeclFailure::LimitExceeded);used+=amount;
}
void Objects(std::size_t& used,std::size_t count,std::size_t width,std::size_t limit) {
    Need(!width||count<=(std::numeric_limits<std::size_t>::max)()/width,"Compiled catalog storage arithmetic overflow",0,CompiledDeclFailure::LimitExceeded);
    Charge(used,count*width,limit,"Compiled catalog retained object budget");
    Charge(used,count*width,limit,"Compiled catalog retained capacity budget");
}
void StringStorage(std::size_t& used,std::size_t bytes,std::size_t limit) {
    Objects(used,bytes,1,limit);Charge(used,2,limit,"Compiled catalog retained string terminator budget");
}
std::string Fold(std::string text) {for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
std::string Canonical(std::string text) {
    std::size_t dot=std::string::npos;
    for(std::size_t i=0;i<text.size();++i){char& c=text[i];if(c=='\\')c='/';else if(c=='.')dot=i;else if(c>='A'&&c<='Z')c+=32;}
    if(dot!=std::string::npos)text.resize(dot);return text;
}
void Label(const std::string& text,std::size_t cap,const char* reason) {
    Need(!text.empty(),reason);Need(text.size()<=cap,reason,0,CompiledDeclFailure::LimitExceeded);
    for(unsigned char c:text)Need(c>=32&&c<=126,reason);
}
CompiledDeclSpan Span(std::size_t begin,std::size_t end) {CompiledDeclSpan out;out.begin=begin;out.end=end;return out;}
bool SameSpan(const CompiledDeclSpan& span,std::size_t begin,std::size_t end) {return span.begin==begin&&span.end==end;}
struct Accounting {
    const CompiledCatalogLimits& limits;
    std::size_t work,storage;
    Accounting(const CompiledCatalogLimits& l,std::size_t w,std::size_t s):limits(l),work(w),storage(s){}
    void Work(std::size_t count=1){Charge(work,count,limits.parseWork,"Compiled catalog total parse-work budget");}
    void Keep(std::size_t count,std::size_t width){Objects(storage,count,width,limits.retainedBytes);}
    void KeepString(std::size_t bytes){StringStorage(storage,bytes,limits.retainedBytes);}
};
// CompiledDeclarations is an owning but publicly mutable DTO. Check its exact
// canonical transport framing/spans and recompute its complete byte identity
// without allocating another serialization buffer before making a private copy.
struct TransportCheck {
    const CompiledDeclarations& document;Accounting& account;std::size_t position=0;
    std::uint64_t hash=UINT64_C(14695981039346656037);
    TransportCheck(const CompiledDeclarations& d,Accounting& a):document(d),account(a){}
    void Byte(unsigned char c) {
        Need(position<document.sourceBytes,"Compiled catalog DTO exceeds declared source bytes");
        account.Work();hash=(hash^c)*UINT64_C(1099511628211);++position;
    }
    void Word(std::uint32_t word){for(unsigned shift=0;shift<32;shift+=8)Byte(static_cast<unsigned char>(word>>shift));}
    std::uint32_t Count(std::size_t count) {
        Need(count<=(std::numeric_limits<std::uint32_t>::max)(),"Compiled catalog DTO count exceeds transport uint32");return static_cast<std::uint32_t>(count);
    }
    void Line(const std::string& text,const CompiledDeclSpan& span) {
        Need(position<=document.sourceBytes&&text.size()<=document.sourceBytes-position,"Compiled catalog DTO line exceeds source bytes");
        Need(SameSpan(span,position,position+text.size()),"Compiled catalog DTO line span differs");
        for(unsigned char c:text)Byte(c);Byte('\n');
    }
    void Decimal(std::uint32_t word,const CompiledDeclSpan& span){Line(std::to_string(word),span);}
    std::size_t Run() {
        account.Keep(1,sizeof(CompiledDeclarations));std::size_t entries=0;
        for(const auto& pool:document.pools){
            const auto count=Count(pool.words.size());Need(SameSpan(pool.countSpan,position,position+4),"Compiled catalog DTO pool-count span differs");
            Word(count);const auto begin=position;
            Need(pool.words.size()<=(document.sourceBytes-position)/4,"Compiled catalog DTO pool exceeds source bytes");
            account.Keep(pool.words.size(),sizeof(std::uint32_t));for(auto word:pool.words)Word(word);
            Need(SameSpan(pool.dataSpan,begin,position),"Compiled catalog DTO pool-data span differs");
        }
        for(const auto& section:document.sections){
            Decimal(Count(section.groups.size()),section.countSpan);account.Keep(section.groups.size(),sizeof(CompiledDeclGroup));
            for(const auto& group:section.groups){
                Label(group.sourceFile,account.limits.tokenBytes,"Invalid compiled catalog source-file metadata");account.KeepString(group.sourceFile.size());
                Line(group.sourceFile,group.sourceFileSpan);Decimal(group.defaultTypeId,group.defaultTypeSpan);
                Decimal(Count(group.records.size()),group.countSpan);
                Charge(entries,group.records.size(),account.limits.entries,"Compiled catalog document entry budget");account.Keep(group.records.size(),sizeof(CompiledDeclRecord));
                for(const auto& record:group.records){
                    Decimal(record.rawId,record.idSpan);Decimal(Count(record.text.size()),record.lengthSpan);
                    Need(record.text.size()<=account.limits.recordTextBytes,"Compiled catalog per-record text budget",position,CompiledDeclFailure::LimitExceeded);
                    Need(position<=document.sourceBytes&&record.text.size()<=document.sourceBytes-position&&
                         SameSpan(record.textSpan,position,position+record.text.size()),"Compiled catalog DTO payload span differs");
                    account.Keep(record.text.size(),sizeof(unsigned char));for(unsigned char c:record.text)Byte(c);
                }
            }
        }
        Need(position==document.sourceBytes&&hash==document.sourceFingerprint,"Compiled catalog DTO source length/fingerprint differs");return entries;
    }
};
struct Header {
    std::string type,name;CompiledDeclSpan typeSpan,nameSpan,bodySpan,envelopeSpan;
};
struct Lexer {
    const std::vector<unsigned char>& bytes;Accounting& account;std::size_t p=0;
    Lexer(const std::vector<unsigned char>& b,Accounting& a):bytes(b),account(a){}
    bool At(const char* pair)const{return p<bytes.size()&&bytes.size()-p>=2&&bytes[p]==static_cast<unsigned char>(pair[0])&&bytes[p+1]==static_cast<unsigned char>(pair[1]);}
    bool White(unsigned char c)const{return c==' '||c=='\t'||c=='\r'||c=='\n';}
    void Step(){account.Work();++p;}
    void Trivia() {
        for(;;){
            while(p<bytes.size()&&White(bytes[p]))Step();
            if(At("//")){Step();Step();while(p<bytes.size()&&bytes[p]!='\n')Step();}
            else if(At("/*")){
                const auto begin=p;Step();Step();while(p<bytes.size()&&!At("*/"))Step();
                Need(p<bytes.size(),"Unterminated compiled declaration block comment",begin);Step();Step();
            }else return;
        }
    }
    void Push(std::string& text,unsigned char c) {
        Need(text.size()<account.limits.tokenBytes,"Compiled catalog header-token byte budget",p,CompiledDeclFailure::LimitExceeded);
        Need(account.storage<=account.limits.retainedBytes&&account.limits.retainedBytes-account.storage>=2&&
             text.size()<(account.limits.retainedBytes-account.storage-2)/2,
             "Compiled catalog header-token retained budget",p,CompiledDeclFailure::LimitExceeded);
        text.push_back(char(c));
    }
    void Quoted(std::string* value) {
        const auto begin=p;Step();
        while(p<bytes.size()){
            const auto c=bytes[p];
            if(c=='"'){Step();return;}
            if(c=='\\'&&bytes.size()-p>=2&&(bytes[p+1]=='"'||bytes[p+1]=='\\')){
                Step();if(value)Push(*value,bytes[p]);Step();
            }else{if(value)Push(*value,c);Step();}
        }Need(false,"Unterminated compiled declaration quoted string",begin);
    }
    std::string Token(CompiledDeclSpan& span) {
        Trivia();const auto begin=p;std::string text;
        Need(p<bytes.size()&&bytes[p]!='{'&&bytes[p]!='}',"Missing compiled declaration type/name",p);
        if(bytes[p]=='"')Quoted(&text);
        else while(p<bytes.size()&&!White(bytes[p])&&bytes[p]!='{'&&bytes[p]!='}'&&bytes[p]!='"'&&!At("//")&&!At("/*")){Push(text,bytes[p]);Step();}
        Need(!text.empty(),"Empty compiled declaration type/name",begin);
        for(unsigned char c:text)Need(c>=32&&c<=126,"Control byte in compiled declaration header literal",begin);
        account.KeepString(text.size()); // Charge before reading another header token.
        span=Span(begin,p);return text;
    }
    Header Run() {
        // Transport can preserve arbitrary bytes; this catalog profile admits
        // text envelopes only. Binary payloads never become opaque valid decls.
        for(std::size_t i=0;i<bytes.size();++i){account.Work();const auto c=bytes[i];
            Need((c>=32&&c<=126)||White(c),"Compiled catalog requires an ASCII text declaration envelope",i,CompiledDeclFailure::Unsupported);}
        Header out;out.type=Token(out.typeSpan);out.name=Token(out.nameSpan);Trivia();
        Need(p<bytes.size()&&bytes[p]=='{',"Compiled declaration header must be followed by one body",p);
        const auto body=p;std::size_t depth=0;
        while(p<bytes.size()){
            if(At("//")||At("/*")){Trivia();continue;}
            if(bytes[p]=='"'){Quoted(NULL);continue;}
            if(bytes[p]=='{'){
                Need(depth<account.limits.nesting,"Compiled catalog declaration nesting budget",p,CompiledDeclFailure::LimitExceeded);++depth;
            }else if(bytes[p]=='}'){
                Need(depth>0,"Unmatched compiled declaration closing brace",p);--depth;
                if(!depth){Step();out.bodySpan=Span(body,p);out.envelopeSpan=Span(out.typeSpan.begin,p);Trivia();
                    Need(p==bytes.size(),"Extra text after compiled declaration envelope",p);return out;}
            }Step();
        }Need(false,"Unterminated compiled declaration body",body);return out;
    }
};
typedef std::pair<std::string,std::string> Key;
typedef std::map<Key,std::vector<std::size_t>> Index;
void IndexEntry(Index& index,const Key& key,std::size_t ordinal,Accounting& account) {
    auto found=index.find(key);
    if(found==index.end()){
        account.Keep(1,sizeof(Index::value_type)+4*sizeof(void*));account.KeepString(key.first.size());account.KeepString(key.second.size());
        found=index.emplace(key,std::vector<std::size_t>()).first;
    }
    account.Keep(1,sizeof(std::size_t));found->second.push_back(ordinal);
}
}
struct CompiledDeclarationCatalog::Impl {
    CompiledCatalogLimits limits;
    std::size_t sourceBytes=0,work=0,storage=0;
    std::vector<std::shared_ptr<const CompiledDeclarations>> documents;
    std::vector<std::shared_ptr<const CompiledCatalogSource>> sources;
    std::vector<CompiledCatalogEntry> entries;
    Index exact,canonical;
    explicit Impl(const CompiledCatalogLimits& l):limits(l){}
};
const CompiledDeclGroup& CompiledCatalogEntry::Group()const {
    Need(document&&section<document->sections.size()&&group<document->sections[section].groups.size(),"Compiled catalog entry has an invalid document/group");
    return document->sections[section].groups[group];
}
const CompiledDeclRecord& CompiledCatalogEntry::Record()const {
    const auto& owner=Group();Need(record<owner.records.size(),"Compiled catalog entry has an invalid record");return owner.records[record];
}
CompiledDeclarationCatalog::CompiledDeclarationCatalog(const CompiledCatalogLimits& limits):impl_(new Impl(limits)){}
CompiledDeclarationCatalog::~CompiledDeclarationCatalog(){}
void CompiledDeclarationCatalog::AddDocument(const CompiledDeclarations& input,const CompiledCatalogSource& identity) {
    const auto& cap=impl_->limits;
    Need(impl_->documents.size()<cap.loadUnits,"Compiled catalog load-unit budget",0,CompiledDeclFailure::LimitExceeded);
    Label(identity.loadIdentity,cap.tokenBytes,"Invalid compiled catalog load identity");Label(identity.path,cap.tokenBytes,"Invalid compiled catalog source path");
    Need(identity.role==CompiledCatalogRole::Global||identity.role==CompiledCatalogRole::Map||identity.role==CompiledCatalogRole::Explicit,
         "Compiled catalog requires an explicit selected source role",0,CompiledDeclFailure::Unsupported);
    for(const auto& old:impl_->sources)Need(old->loadIdentity!=identity.loadIdentity,"Repeated compiled catalog load identity");
    if(identity.byteIdentityKnown)Need(input.sourceBytes==identity.bytes&&input.sourceFingerprint==identity.fingerprint,"Compiled catalog supplied byte identity differs");
    std::size_t sourceBytes=impl_->sourceBytes;Charge(sourceBytes,input.sourceBytes,cap.totalSourceBytes,"Compiled catalog total source byte budget");
    Accounting account(cap,impl_->work,impl_->storage);TransportCheck check(input,account);const auto count=check.Run();
    Need(impl_->entries.size()<=cap.entries&&count<=cap.entries-impl_->entries.size(),"Compiled catalog cumulative entry budget",0,CompiledDeclFailure::LimitExceeded);
    account.Keep(1,sizeof(CompiledCatalogSource)+2*sizeof(std::shared_ptr<const void>));
    account.KeepString(identity.loadIdentity.size());account.KeepString(identity.path.size());
    auto source=std::make_shared<const CompiledCatalogSource>(identity);
    auto document=std::make_shared<const CompiledDeclarations>(input);
    std::unique_ptr<Impl> next(new Impl(*impl_));const auto unit=next->documents.size();std::size_t ordinal=0;
    for(std::size_t s=0;s<document->sections.size();++s){const auto& section=document->sections[s];
        for(std::size_t g=0;g<section.groups.size();++g){const auto& group=section.groups[g];
            for(std::size_t r=0;r<group.records.size();++r){
                Header header=Lexer(group.records[r].text,account).Run();CompiledCatalogEntry entry;
                account.Keep(1,sizeof(CompiledCatalogEntry));account.KeepString(header.type.size());account.KeepString(header.name.size());
                entry.document=document;entry.source=source;entry.loadOrdinal=unit;entry.documentOrdinal=ordinal++;entry.catalogOrdinal=next->entries.size();
                entry.section=s;entry.group=g;entry.record=r;entry.type=std::move(header.type);entry.name=std::move(header.name);
                entry.typeSpan=header.typeSpan;entry.nameSpan=header.nameSpan;entry.bodySpan=header.bodySpan;entry.envelopeSpan=header.envelopeSpan;
                // Snapshot before setting original_: the snapshot has no seal
                // of its own, so it cannot form an ownership cycle. It retains
                // the same genuinely const private document/source objects.
                account.Keep(1,sizeof(CompiledCatalogEntry));account.KeepString(entry.type.size());account.KeepString(entry.name.size());
                entry.original_=std::make_shared<const CompiledCatalogEntry>(entry);
                const auto type=Fold(entry.type);
                IndexEntry(next->exact,Key(type,entry.name),entry.catalogOrdinal,account);
                IndexEntry(next->canonical,Key(type,Canonical(entry.name)),entry.catalogOrdinal,account);
                next->entries.push_back(std::move(entry));
            }
        }
    }
    next->documents.push_back(std::move(document));next->sources.push_back(std::move(source));
    next->sourceBytes=sourceBytes;next->work=account.work;next->storage=account.storage;impl_.swap(next);
}
std::size_t CompiledDeclarationCatalog::LoadUnitCount()const{return impl_->documents.size();}
std::size_t CompiledDeclarationCatalog::EntryCount()const{return impl_->entries.size();}
CompiledCatalogEntry CompiledDeclarationCatalog::Entry(std::size_t index)const {
    Need(index<impl_->entries.size(),"Compiled catalog entry index is outside selected records");return impl_->entries[index];
}
std::shared_ptr<const CompiledDeclarations> CompiledDeclarationCatalog::Document(std::size_t index)const {
    Need(index<impl_->documents.size(),"Compiled catalog document index is outside selected load units");return impl_->documents[index];
}
std::shared_ptr<const CompiledCatalogSource> CompiledDeclarationCatalog::Source(std::size_t index)const {
    Need(index<impl_->sources.size(),"Compiled catalog source index is outside selected load units");return impl_->sources[index];
}
CompiledCatalogLookup CompiledDeclarationCatalog::Lookup(const std::string& type,const std::string& name,CompiledCatalogNamePolicy policy)const {
    Need(policy==CompiledCatalogNamePolicy::Exact||policy==CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1,
         "Unknown compiled catalog name policy",0,CompiledDeclFailure::Unsupported);
    Label(type,impl_->limits.tokenBytes,"Invalid compiled catalog query type");Label(name,impl_->limits.tokenBytes,"Invalid compiled catalog query name");
    const auto& index=policy==CompiledCatalogNamePolicy::Exact?impl_->exact:impl_->canonical;
    const auto found=index.find(Key(Fold(type),policy==CompiledCatalogNamePolicy::Exact?name:Canonical(name)));CompiledCatalogLookup result;
    if(found==index.end())return result;
    Need(found->second.size()<=impl_->limits.lookupMatches,"Compiled catalog lookup match budget",0,CompiledDeclFailure::LimitExceeded);
    std::size_t bytes=0;
    for(auto i:found->second){const auto& entry=impl_->entries[i];Objects(bytes,1,sizeof(CompiledCatalogEntry),impl_->limits.lookupBytes);
        StringStorage(bytes,entry.type.size(),impl_->limits.lookupBytes);StringStorage(bytes,entry.name.size(),impl_->limits.lookupBytes);
        // An owning match also retains its private attestation after the
        // catalog dies. Account that entry/header memory in the result cap.
        Objects(bytes,1,sizeof(CompiledCatalogEntry),impl_->limits.lookupBytes);
        StringStorage(bytes,entry.type.size(),impl_->limits.lookupBytes);StringStorage(bytes,entry.name.size(),impl_->limits.lookupBytes);}
    result.matches.reserve(found->second.size());for(auto i:found->second)result.matches.push_back(impl_->entries[i]);
    result.status=result.matches.size()==1?CompiledCatalogLookupStatus::Unique:CompiledCatalogLookupStatus::Ambiguous;return result;
}
} // namespace wolf2009

// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarationCatalog.h"
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
template<class F>void Group(const char* name,F test){++groups;try{test();}catch(const std::exception& error){++failed;std::cerr<<name<<": "<<error.what()<<'\n';}}
template<class F>void Reject(F test,CompiledDeclFailure category,const char* reason){bool refused=false;try{test();}catch(const CompiledCatalogError& error){refused=error.Category()==category;}Check(refused,reason);}
void Word(Bytes& bytes,std::uint32_t word){for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<unsigned char>(word>>shift));}
void Text(Bytes& bytes,const std::string& text){bytes.insert(bytes.end(),text.begin(),text.end());}
Bytes Data(const std::string& text){return Bytes(text.begin(),text.end());}
struct InputGroup {
    std::string file;std::uint32_t type;std::vector<std::pair<std::uint32_t,Bytes>> records;
};
Bytes Packet(const std::vector<InputGroup>& first,const std::vector<InputGroup>& second={},
             const std::vector<std::uint32_t>& a={0x11223344,0x3f800000},const std::vector<std::uint32_t>& b={0xffffffff}){
    Bytes bytes;for(const auto* pool:{&a,&b}){Word(bytes,std::uint32_t(pool->size()));for(auto word:*pool)Word(bytes,word);}
    for(const auto* section:{&first,&second}){Text(bytes,std::to_string(section->size())+"\n");
        for(const auto& group:*section){Text(bytes,group.file+"\n"+std::to_string(group.type)+"\n"+std::to_string(group.records.size())+"\n");
            for(const auto& record:group.records){Text(bytes,std::to_string(record.first)+"\n"+std::to_string(record.second.size())+"\n");bytes.insert(bytes.end(),record.second.begin(),record.second.end());}}}
    return bytes;
}
CompiledDeclarations Read(const Bytes& bytes){return CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1);}
CompiledDeclarations One(const std::string& text){return Read(Packet({{"own/source.def",3,{{42,Data(text)}}}}));}
CompiledCatalogSource Source(const char* id,const CompiledDeclarations& document,CompiledCatalogRole role=CompiledCatalogRole::Explicit){
    CompiledCatalogSource source;source.loadIdentity=id;source.path=std::string("own/")+id;source.role=role;source.epoch=17;
    source.byteIdentityKnown=true;source.bytes=document.sourceBytes;source.fingerprint=document.sourceFingerprint;return source;
}
void Add(CompiledDeclarationCatalog& catalog,const CompiledDeclarations& document,const char* name){catalog.AddDocument(document,Source(name,document));}
bool Is(const CompiledDeclSpan& span,std::size_t begin,std::size_t end){return span.begin==begin&&span.end==end;}
std::string Slice(const CompiledCatalogEntry& entry,const CompiledDeclSpan& span){const auto& bytes=entry.Record().text;Check(span.begin<=span.end&&span.end<=bytes.size(),"Header/body span escaped record");return std::string(bytes.begin()+span.begin,bytes.begin()+span.end);}
void Synthetic(){
    Group("owning documents pools and explicit input",[]{
        const auto document=Read(Packet({}));CompiledDeclarationCatalog catalog;Add(catalog,document,"empty");
        Check(catalog.LoadUnitCount()==1&&catalog.EntryCount()==0&&catalog.Document(0)->pools[0].words==std::vector<std::uint32_t>({0x11223344,0x3f800000})&&
            catalog.Document(0)->pools[1].words==std::vector<std::uint32_t>({0xffffffff}),"Empty load unit lost prefix word pools");
        Check(catalog.Source(0)->epoch==17&&catalog.Source(0)->byteIdentityKnown,"Explicit input identity was not retained");
        Check(catalog.Lookup("model","missing").status==CompiledCatalogLookupStatus::Missing,"Empty selected catalog synthesized a declaration");
        Reject([&]{catalog.Entry(0);},CompiledDeclFailure::InvalidData,"Out-of-range entry accepted");
        Reject([&]{catalog.Document(1);},CompiledDeclFailure::InvalidData,"Out-of-range document accepted");
        Reject([&]{catalog.Source(1);},CompiledDeclFailure::InvalidData,"Out-of-range source accepted");
        CompiledCatalogEntry malformed;Reject([&]{malformed.Record();},CompiledDeclFailure::InvalidData,"Unowned entry dereferenced a null document");
    });
    Group("balanced opaque body quotes comments and raw spans",[]{
        const std::string text=R"DECL(/*prefix {*/ MODEL "Path\Sub.Name.md5anim" /*head*/ {
 unknown { "quoted } and \" {" /* ignored } */ }
 // ignored }
 ( arbitrary [ opaque ] body )
} /*suffix*/
)DECL";
        auto document=One(text);CompiledDeclarationCatalog catalog;Add(catalog,document,"quoted");const auto entry=catalog.Entry(0);
        Check(entry.type=="MODEL"&&entry.name=="Path\\Sub.Name.md5anim"&&entry.Record().text==Data(text),"Quoted header or opaque body bytes changed");
        Check(Slice(entry,entry.typeSpan)=="MODEL"&&Slice(entry,entry.nameSpan)=="\"Path\\Sub.Name.md5anim\"", "Raw token spans lost quotes/backslashes");
        Check(Slice(entry,entry.bodySpan).front()=='{'&&Slice(entry,entry.bodySpan).back()=='}'&&entry.envelopeSpan.begin==entry.typeSpan.begin&&
            entry.envelopeSpan.end==entry.bodySpan.end,"Body/envelope spans include trailing trivia or omit braces");
        Check(catalog.Lookup("model","Path\\Sub.Name.md5anim").status==CompiledCatalogLookupStatus::Unique,"Exact-name lookup altered source spelling");
        Check(catalog.Lookup("model","path/sub.name.md5anim").status==CompiledCatalogLookupStatus::Missing,"Exact lookup silently canonicalized a name");
        Check(catalog.Lookup("MoDeL","PATH/SUB.NAME.OTHER",CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1).status==CompiledCatalogLookupStatus::Unique,"Explicit source canonical-name profile differs");
        const auto escaped=One("\"futureType\" \"name\\\"quote\\\\back\\q\" { key value }");CompiledDeclarationCatalog other;Add(other,escaped,"escapes");
        Check(other.Entry(0).name=="name\"quote\\back\\q"&&other.Entry(0).type=="futureType","Unknown quote escapes were executed or raw spellings confused with values");
    });
    Group("named envelopes reject malformed and binary payloads",[]{
        const std::vector<std::string> bad{""," ","type","type name","type {}","{}","type \"\" {}","type name extra {}","type name {} tail","type name {} type second {}",
            "type name {", "type name { } }", "/* unterminated", "type name { /* unterminated }", "type name { \"unterminated }", "type name \"tail\" {}"};
        for(const auto& text:bad){CompiledDeclarationCatalog catalog;const auto document=One(text);Reject([&]{Add(catalog,document,"bad");},CompiledDeclFailure::InvalidData,"Malformed named declaration envelope was admitted");
            Check(catalog.LoadUnitCount()==0&&catalog.EntryCount()==0,"Failed envelope parse committed a partial catalog");}
        for(const Bytes& binary:std::vector<Bytes>{{0,1,255},Data(std::string("type name { a\0b }",17)),Data("type name { \x7f }")}){
            const auto document=Read(Packet({{"binary",3,{{9,binary}}}}));Check(document.sections[0].groups[0].records[0].text==binary,"Transport rejected/changed opaque binary control");
            CompiledDeclarationCatalog catalog;Reject([&]{Add(catalog,document,"binary");},CompiledDeclFailure::Unsupported,"Raw binary transport became a qualified declaration envelope");}
    });
    Group("actual type namespace independent of default type and raw id",[]{
        const auto document=Read(Packet({{"first.def",3,{{7,Data("model same {}")},{7,Data("entityDef same { key value }")},{7,Data("futureKind same { unknown {} }")}}}},
            {{"second.def",0xffffffff,{{7,Data("model.v1 same {}")}}}}));
        CompiledDeclarationCatalog catalog;Add(catalog,document,"types");Check(catalog.EntryCount()==4,"Raw IDs collapsed separate declarations");
        for(const auto* type:{"model","ENTITYDEF","futureKind","model.v1"})Check(catalog.Lookup(type,"same").status==CompiledCatalogLookupStatus::Unique,"Actual typename namespaces were merged or defaultTypeId was treated as type");
        const auto last=catalog.Entry(3);Check(last.section==1&&last.group==0&&last.record==0&&last.documentOrdinal==3&&last.catalogOrdinal==3&&last.loadOrdinal==0&&
            last.Group().defaultTypeId==0xffffffff&&last.Record().rawId==7,"Section/defaultType/rawId/order provenance was altered");
        Check(catalog.Lookup("model/v1","same",CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1).status==CompiledCatalogLookupStatus::Missing,"Type keys were slash/dot normalized");
    });
    Group("canonical collisions preserve all records and explicit load order",[]{
        const auto first=Read(Packet({{"sourceA",3,{{1,Data("model A/B.name.ext {}")},{2,Data("model \"a\\b.name.OTHER\" { different body }")},{3,Data("model dir.withdot/file {}")}}}}));
        CompiledDeclarationCatalog catalog;catalog.AddDocument(first,Source("global",first,CompiledCatalogRole::Global));
        const auto second=One("model A/B.name.ext { fromMap }");catalog.AddDocument(second,Source("map",second,CompiledCatalogRole::Map));
        const auto exact=catalog.Lookup("model","A/B.name.ext");Check(exact.status==CompiledCatalogLookupStatus::Ambiguous&&exact.matches.size()==2&&exact.matches[0].loadOrdinal==0&&exact.matches[1].loadOrdinal==1,"Exact duplicates were overwritten or role precedence guessed");
        const auto canonical=catalog.Lookup("MODEL","a/b.name.any",CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1);
        Check(canonical.status==CompiledCatalogLookupStatus::Ambiguous&&canonical.matches.size()==3&&canonical.matches[0].catalogOrdinal==0&&canonical.matches[1].catalogOrdinal==1&&canonical.matches[2].catalogOrdinal==3,"Canonical collisions lost records or source order");
        Check(canonical.matches[0].Record().text!=canonical.matches[2].Record().text,"A later duplicate overwrote an earlier raw payload");
        Check(catalog.Lookup("model","DIR.other",CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1).matches.size()==1,"Last-dot profile incorrectly reset its dot at a directory separator");
        CompiledDeclarationCatalog emptyKeys;Add(emptyKeys,One("model .hiddenA {}"),"hidden-a");Add(emptyKeys,One("model .hiddenB {}"),"hidden-b");
        Check(emptyKeys.Lookup("model",".query",CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1).status==CompiledCatalogLookupStatus::Ambiguous&&
            emptyKeys.Lookup("model","_default").status==CompiledCatalogLookupStatus::Missing,"Empty canonical names silently synthesized defaults or lost collision identity");
        CompiledDeclarationCatalog reverse;reverse.AddDocument(second,Source("map",second,CompiledCatalogRole::Map));reverse.AddDocument(first,Source("global",first,CompiledCatalogRole::Global));
        Check(reverse.Lookup("model","A/B.name.ext").matches[0].source->role==CompiledCatalogRole::Map,"Catalog sorted roles instead of retaining explicit append order");
    });
    Group("immutable owned records survive caller and catalog destruction",[]{
        CompiledCatalogLookup saved;CompiledCatalogEntry retained;
        {CompiledDeclarationCatalog catalog;auto document=One("unknown Named { retain all bytes }");auto source=Source("owner",document);catalog.AddDocument(document,source);
            document.pools[0].words[0]=0;document.sections[0].groups[0].records[0].text.clear();source.path="changed";
            saved=catalog.Lookup("UNKNOWN","Named");retained=catalog.Entry(0);
            Check(saved.matches.size()==1&&retained.source->path=="own/owner"&&retained.document->pools[0].words[0]==0x11223344,"Catalog retained a mutable caller DTO/source alias");}
        Check(saved.status==CompiledCatalogLookupStatus::Unique&&retained.Record().text==Data("unknown Named { retain all bytes }")&&
            retained.Group().sourceFile=="own/source.def"&&saved.matches[0].document==retained.document,"Owning lookup lost record/prefix/provenance after catalog destruction");
        Check(saved.matches[0].IsUnmodified()&&retained.IsUnmodified(),"Genuine owning entry lost its attestation after catalog destruction");
    });
    Group("private immutable entry attestation rejects public forgeries",[]{
        CompiledDeclarationCatalog catalog;Add(catalog,One("model OwnName { opaque body }"),"sealed");const auto original=catalog.Entry(0);
        Check(original.IsUnmodified(),"Catalog did not attest its admitted record");
        auto copy=original;Check(copy.IsUnmodified(),"Ordinary entry copy lost attestation");
        const auto reject=[&](const CompiledCatalogEntry& forged){Check(!forged.IsUnmodified(),"Changed public field retained catalog attestation");Check(original.IsUnmodified(),"Changing a copied entry changed its private immutable original");};
        {auto entry=original;entry.document.reset();reject(entry);}
        {auto entry=original;entry.source.reset();reject(entry);}
        for(unsigned field=0;field<6;++field){auto entry=original;
            if(field==0)++entry.loadOrdinal;if(field==1)++entry.documentOrdinal;if(field==2)++entry.catalogOrdinal;
            if(field==3)++entry.section;if(field==4)++entry.group;if(field==5)++entry.record;reject(entry);}
        {auto entry=original;entry.type="entityDef";reject(entry);}
        {auto entry=original;entry.name="OtherName";reject(entry);}
        for(unsigned field=0;field<8;++field){auto entry=original;
            if(field==0)++entry.typeSpan.begin;if(field==1)++entry.typeSpan.end;if(field==2)++entry.nameSpan.begin;if(field==3)++entry.nameSpan.end;
            if(field==4)++entry.bodySpan.begin;if(field==5)entry.bodySpan.end=(std::numeric_limits<std::size_t>::max)();
            if(field==6)++entry.envelopeSpan.begin;if(field==7)++entry.envelopeSpan.end;reject(entry);}
        // Identical public bytes do not confer an attestation on an external,
        // caller-mutable object substituted under a const shared_ptr facade.
        auto mutableDocument=std::make_shared<CompiledDeclarations>(*original.document);
        {auto entry=original;entry.document=mutableDocument;reject(entry);mutableDocument->sections[0].groups[0].records[0].text.clear();reject(entry);}
        auto mutableSource=std::make_shared<CompiledCatalogSource>(*original.source);
        {auto entry=original;entry.source=mutableSource;reject(entry);mutableSource->path.assign(512u<<10,'x');reject(entry);}
        CompiledCatalogEntry unsealed;
        unsealed.document=original.document;unsealed.source=original.source;unsealed.loadOrdinal=original.loadOrdinal;
        unsealed.documentOrdinal=original.documentOrdinal;unsealed.catalogOrdinal=original.catalogOrdinal;unsealed.section=original.section;
        unsealed.group=original.group;unsealed.record=original.record;unsealed.type=original.type;unsealed.name=original.name;
        unsealed.typeSpan=original.typeSpan;unsealed.nameSpan=original.nameSpan;unsealed.bodySpan=original.bodySpan;unsealed.envelopeSpan=original.envelopeSpan;
        reject(unsealed);Check(!CompiledCatalogEntry().IsUnmodified(),"Default forged entry acquired attestation");
        std::weak_ptr<const CompiledDeclarations> documentLifetime;std::weak_ptr<const CompiledCatalogSource> sourceLifetime;
        {CompiledDeclarationCatalog temporary;Add(temporary,One("model Temporary {}"),"temporary");auto entry=temporary.Entry(0);
            documentLifetime=entry.document;sourceLifetime=entry.source;Check(entry.IsUnmodified(),"Temporary genuine entry lost its seal");}
        Check(documentLifetime.expired()&&sourceLifetime.expired(),"Entry attestation created an ownership cycle");
    });
    Group("identity and forged DTO refusal preserve existing catalog",[]{
        const auto good=One("model stable {}");CompiledDeclarationCatalog catalog;Add(catalog,good,"stable");
        const auto prior=catalog.Entry(0).document;
        const auto unchanged=[&](){Check(catalog.EntryCount()==1&&catalog.LoadUnitCount()==1&&catalog.Entry(0).document==prior&&catalog.Lookup("model","stable").status==CompiledCatalogLookupStatus::Unique,"Failed append replaced an existing load unit/index");};
        auto source=Source("bad",good);++source.bytes;Reject([&]{catalog.AddDocument(good,source);},CompiledDeclFailure::InvalidData,"Supplied source byte count was ignored");unchanged();
        source=Source("bad",good);source.fingerprint^=1;Reject([&]{catalog.AddDocument(good,source);},CompiledDeclFailure::InvalidData,"Supplied source fingerprint was ignored");unchanged();
        for(unsigned mutation=0;mutation<5;++mutation){auto corrupt=good;
            if(mutation==0)++corrupt.sourceBytes;if(mutation==1)corrupt.pools[0].words[0]^=1;if(mutation==2)++corrupt.sections[0].groups[0].records[0].textSpan.begin;
            if(mutation==3)corrupt.sections[0].groups[0].records[0].text[0]='M';if(mutation==4)++corrupt.sections[0].groups[0].defaultTypeSpan.end;
            auto unknown=Source("corrupt",corrupt);unknown.byteIdentityKnown=false;
            Reject([&]{catalog.AddDocument(corrupt,unknown);},CompiledDeclFailure::InvalidData,"Mutated DTO bypassed framing/span/byte-identity validation");unchanged();}
        Reject([&]{Add(catalog,good,"stable");},CompiledDeclFailure::InvalidData,"Repeated load identity was silently appended");unchanged();
        source=Source("unknown",good);source.role=CompiledCatalogRole::Unspecified;Reject([&]{catalog.AddDocument(good,source);},CompiledDeclFailure::Unsupported,"Unknown source role was guessed");unchanged();
        const auto lateBad=Read(Packet({{"late",3,{{1,Data("model fresh {}")},{2,Data("broken header")}}}}));
        Reject([&]{Add(catalog,lateBad,"late");},CompiledDeclFailure::InvalidData,"Malformed later record was admitted");unchanged();Check(catalog.Lookup("model","fresh").status==CompiledCatalogLookupStatus::Missing,"Earlier record escaped a failed append transaction");
    });
    Group("append parse retained and lookup budgets",[]{
        const auto good=One("model stable {}");
        const auto appendCap=[&](CompiledCatalogLimits limits){CompiledDeclarationCatalog catalog(limits);Reject([&]{Add(catalog,good,"budget");},CompiledDeclFailure::LimitExceeded,"Catalog append budget was ignored");Check(catalog.EntryCount()==0&&catalog.LoadUnitCount()==0,"Budget refusal mutated catalog");};
        CompiledCatalogLimits l;l.loadUnits=0;appendCap(l);l=CompiledCatalogLimits();l.entries=0;appendCap(l);
        l=CompiledCatalogLimits();l.totalSourceBytes=good.sourceBytes-1;appendCap(l);l=CompiledCatalogLimits();l.recordTextBytes=1;appendCap(l);
        l=CompiledCatalogLimits();l.tokenBytes=2;appendCap(l);l=CompiledCatalogLimits();l.retainedBytes=1;appendCap(l);l=CompiledCatalogLimits();l.parseWork=1;appendCap(l);
        l=CompiledCatalogLimits();l.nesting=1;CompiledDeclarationCatalog nested(l);Reject([&]{Add(nested,One("type name { {} }"),"nested");},CompiledDeclFailure::LimitExceeded,"Nested-body cap ignored");
        l=CompiledCatalogLimits();l.entries=1;CompiledDeclarationCatalog count(l);Add(count,good,"first");Reject([&]{Add(count,One("model second {}"),"second");},CompiledDeclFailure::LimitExceeded,"Entry budget was reset per document");Check(count.EntryCount()==1&&count.LoadUnitCount()==1,"Cumulative entry budget damaged prior document");
        l=CompiledCatalogLimits();l.lookupMatches=1;CompiledDeclarationCatalog matches(l);Add(matches,good,"first");Add(matches,good,"second");
        Reject([&]{matches.Lookup("model","stable");},CompiledDeclFailure::LimitExceeded,"Lookup match copy cap ignored");
        l=CompiledCatalogLimits();l.lookupBytes=1;CompiledDeclarationCatalog output(l);Add(output,good,"one");Reject([&]{output.Lookup("model","stable");},CompiledDeclFailure::LimitExceeded,"Lookup owning-result byte cap ignored");
        l=CompiledCatalogLimits();l.lookupBytes=3*sizeof(CompiledCatalogEntry);CompiledDeclarationCatalog sealedOutput(l);Add(sealedOutput,good,"sealed-output");
        Reject([&]{sealedOutput.Lookup("model","stable");},CompiledDeclFailure::LimitExceeded,"Owning lookup omitted retained attestation/header memory");
        l=CompiledCatalogLimits();l.parseWork=l.retainedBytes=l.totalSourceBytes=(std::numeric_limits<std::size_t>::max)();CompiledDeclarationCatalog wide(l);Add(wide,good,"wide");Check(wide.EntryCount()==1,"Large budget values overflowed valid catalog arithmetic");
        Reject([&]{wide.Lookup("model","stable",static_cast<CompiledCatalogNamePolicy>(99));},CompiledDeclFailure::Unsupported,"Unknown name policy silently defaulted");
        Reject([&]{wide.Lookup("","stable");},CompiledDeclFailure::InvalidData,"Empty query type accepted");
    });
    std::cout<<"WOLF2009_COMPILED_CATALOG_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed="<<failed<<'\n';
}
void Original(const char* path){
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open owner-supplied trainyard MPK");const auto archive=Archive::Scan(input);
    std::size_t found=0,chunk=0,entry=0;for(std::size_t c=0;c<archive.Chunks().size();++c)if(archive.Chunks()[c].type=="DECL")
        for(std::size_t e=0;e<archive.Chunks()[c].entries.size();++e)if(archive.Chunks()[c].entries[e].name=="maps/game/trainyard/trainyard"){chunk=c;entry=e;++found;}
    Check(found==1,"Exact selected trainyard DECL packet is missing/ambiguous");const auto bytes=archive.ReadEntry(input,chunk,entry);const auto document=Read(bytes);
    Check(document.sourceBytes==2112750&&document.sourceFingerprint==UINT64_C(0xc8d044282ad8a99e),"Selected trainyard packet differs from independent transport audit");
    CompiledDeclarationCatalog catalog;auto source=Source("selected-trainyard",document,CompiledCatalogRole::Map);source.path="wolf2009/selected/DECL/maps/game/trainyard/trainyard";catalog.AddDocument(document,source);
    Check(catalog.EntryCount()==3269&&catalog.Document(0)->pools[1].words.size()==1467,"Original catalog lost records or prefix pool");
    std::size_t entities=0,models=0;for(std::size_t i=0;i<catalog.EntryCount();++i){const auto item=catalog.Entry(i);Check(item.catalogOrdinal==i&&item.documentOrdinal==i&&item.loadOrdinal==0&&item.IsUnmodified(),"Original record order or attestation changed");
        Check(item.Record().textSpan.end<=bytes.size()&&std::equal(item.Record().text.begin(),item.Record().text.end(),bytes.begin()+item.Record().textSpan.begin),"Original owning raw payload differs from input bytes");
        std::string type=item.type;for(char& c:type)if(c>='A'&&c<='Z')c+=32;if(type=="entitydef")++entities;if(type=="model")++models;}
    const auto locker=catalog.Lookup("model","model_env_locker_foot_01_anim");Check(locker.status==CompiledCatalogLookupStatus::Unique&&locker.matches.size()==1,"Exact locker model header is absent/ambiguous");
    Check(locker.matches[0].Group().defaultTypeId==3&&locker.matches[0].Record().rawId==334&&Is(locker.matches[0].Record().textSpan,1849093,1849634),"Locker actual model namespace was inferred from default type or rawId");
    for(const auto* suffix:{"idle","open","idle_open","close"})Check(catalog.Lookup("animBlendTree",std::string("models/props/locker_foot_01_anim_")+suffix+".md5anim",CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1).status==CompiledCatalogLookupStatus::Missing,"Selected trainyard catalog contains an explicit locker tree header");
    Check(entities==974&&models==31,"Selected original entity/model header counts changed");
    std::cout<<"WOLF2009_COMPILED_CATALOG_ORIGINAL_OK records="<<catalog.EntryCount()<<" entitydefs="<<entities<<" models="<<models<<" locker_model=1 selected_missing_locker_trees=4 bytes="<<document.sourceBytes<<" source_fingerprint="<<std::hex<<document.sourceFingerprint<<std::dec<<" selected_catalog_only=1 runtime_absence=0 semantics=0 native_import=0\n";
}
}
int main(int argc,char** argv){try{Synthetic();if(failed)return 1;for(int i=1;i<argc;++i){const std::string option(argv[i]);if(option=="--self-test")continue;
    if(option=="--source-map"&&i+1<argc){Original(argv[++i]);continue;}throw std::runtime_error("Usage: [--self-test] [--source-map owner-trainyard.mpk]");}return 0;
}catch(const CompiledCatalogError& error){std::cerr<<"Compiled catalog offset "<<error.Offset()<<": "<<error.what()<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<"Compiled catalog: "<<error.what()<<'\n';return 1;}}

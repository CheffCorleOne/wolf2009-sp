// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceDefinitions.h"
#include "CompiledDeclarationCatalog.h"
#include "Archive.h"
#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
namespace {
using namespace wolf2009;
typedef std::vector<unsigned char> Bytes;
unsigned checks=0,groups=0,failed=0;
void Check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class F>void Reject(F test,const char* reason){bool refused=false;try{test();}catch(const std::exception&){refused=true;}Check(refused,reason);}
template<class F>void Group(const char* name,F test){++groups;try{test();}catch(const std::exception& error){++failed;std::cerr<<name<<": "<<error.what()<<'\n';}}
Bytes Data(const std::string& text){return Bytes(text.begin(),text.end());}
std::uint64_t Hash(const Bytes& bytes){std::uint64_t value=UINT64_C(14695981039346656037);for(auto c:bytes){value^=c;value*=UINT64_C(1099511628211);}return value;}
void Word(Bytes& bytes,std::uint32_t value){for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<unsigned char>(value>>shift));}
void Text(Bytes& bytes,const std::string& text){bytes.insert(bytes.end(),text.begin(),text.end());}
std::vector<CompiledCatalogEntry> EntriesWithPools(const std::vector<std::string>& texts,const char* id,
    const std::vector<std::uint32_t>& first,const std::vector<std::uint32_t>& second){
    Bytes bytes;for(const auto* pool:{&first,&second}){Word(bytes,std::uint32_t(pool->size()));for(const auto word:*pool)Word(bytes,word);}
    Text(bytes,"1\nown/source.def\n3\n"+std::to_string(texts.size())+"\n");std::uint32_t raw=700;
    for(const auto& text:texts){Text(bytes,std::to_string(raw++)+"\n"+std::to_string(text.size())+"\n");Text(bytes,text);}Text(bytes,"0\n");
    const auto document=CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1);CompiledCatalogSource source;
    source.loadIdentity=id;source.path=std::string("owner/")+id;source.role=CompiledCatalogRole::Explicit;source.epoch=23;
    source.byteIdentityKnown=true;source.bytes=document.sourceBytes;source.fingerprint=document.sourceFingerprint;
    CompiledDeclarationCatalog catalog;catalog.AddDocument(document,source);std::vector<CompiledCatalogEntry> out;
    for(std::size_t i=0;i<catalog.EntryCount();++i)out.push_back(catalog.Entry(i));return out;
}
std::vector<CompiledCatalogEntry> Entries(const std::vector<std::string>& texts,const char* id="own-load",std::uint32_t secondPoolWord=0xffffffff){
    return EntriesWithPools(texts,id,{0x12345678,0x7fc01234},{secondPoolWord});
}
void Import(SourceDefinitions& definitions,const std::vector<std::string>& texts,const char* id="own-load"){for(const auto& entry:Entries(texts,id))definitions.AddCompiledRecord(entry);}
void Plain(SourceDefinitions& definitions,const std::string& text,const char* member="own/plain.def"){
    SourceDefinitionArchiveIdentity archive;archive.name="own-plain";archive.bytes=65536;archive.fingerprint=123;definitions.AddMember(archive,member,Data(text));
}
const SourceResolvedLiteral& Field(const ResolvedSourceDefinition& result,const char* key){const auto* value=result.Find(key);Check(value!=NULL,"Expected resolved field missing");return *value;}
std::string Scalar(const ResolvedSourceDefinition& result,const char* key,SourceLiteralEncoding encoding){const auto& field=Field(result,key);
    Check(field.encoding==encoding&&field.value.size()==1,"Resolved value has wrong encoding/token count");return field.value[0].text;}
void Words(const ResolvedSourceDefinition& result,const char* key,SourceLiteralEncoding encoding,const std::vector<std::uint32_t>& words){const auto& field=Field(result,key);
    Check(field.encoding==encoding&&field.numericWords==words,"Qualified numeric encoding/words differ");
    Check(field.value.empty()&&!field.gameplayImplemented&&field.origins[0].data.numericWords==words,"Numeric value synthesized tokens/lost raw-fact words or acquired gameplay support");}
void Synthetic(){
    Group("compiled strings integers raw tuples and provenance",[]{
        const std::string text=R"DECL(/*prefix*/ entityDef "Typed" {
"str" 0 0 "A \"quote\" \\back\q"
"space" 0 0 "line one
line two	last"
"bare" 0 0 literal
"minimum" 4 1 -2147483648
"maximum" 4 1 +2147483647
"zero" 4 1 0
"negative" 4 1 -17
"overflow" 4 1 2147483648
"underflow" 4 1 -2147483649
"quotedint" 4 1 "12"
"floatword" 1 1 0
"unknown" -7 8 "raw"
} /*tail*/)DECL";
        auto entries=Entries({text});const auto original=entries[0];SourceDefinitions d;d.AddCompiledRecord(original);entries.clear();
        const auto r=d.Resolve(SourceDefinitionKind::EntityDef,"typed");Check(d.MemberCount()==1&&d.DefinitionCount()==1&&!r.gameplaySupported,"Compiled import promoted runtime support/counts");
        Check(Scalar(r,"str",SourceLiteralEncoding::CompiledString)=="A \"quote\" \\back\\q"&&Scalar(r,"bare",SourceLiteralEncoding::CompiledString)=="literal","Compiled string decoded metadata or escaped bytes incorrectly");
        Check(Scalar(r,"space",SourceLiteralEncoding::CompiledString)=="line one\nline two\tlast","Compiled quoted whitespace was normalized");
        for(const auto& pair:std::vector<std::pair<const char*,const char*>>{{"minimum","-2147483648"},{"maximum","+2147483647"},{"zero","0"},{"negative","-17"}})
            Check(Scalar(r,pair.first,SourceLiteralEncoding::CompiledSignedInteger)==pair.second,"Signed int32 boundary/spelling changed");
        for(const auto* key:{"overflow","underflow","quotedint","unknown"}){const auto& field=Field(r,key);Check(field.encoding==SourceLiteralEncoding::CompiledOpaque&&field.value.empty()&&!field.gameplayImplemented,"Unsupported typed value was converted/hidden");}
        Check(Field(r,"floatword").encoding==SourceLiteralEncoding::CompiledFloat32&&Field(r,"floatword").value.empty()&&Field(r,"floatword").numericWords==std::vector<std::uint32_t>({0xffffffff}),"Proven float-pool scalar word was not retained as DATA");
        const auto& field=Field(r,"str");const auto& fact=field.origins[0];const auto owner=fact.origin.member.compiledRecord;
        Check(fact.data.encoding==SourceLiteralEncoding::CompiledString&&fact.data.rawValueType==0&&fact.data.rawValueAux==0&&fact.data.values.size()==3&&fact.data.values[0].text=="0"&&fact.data.values[1].text=="0"&&fact.data.values[2].quoted,"Raw tuple metadata was stripped from source fact");
        Check(owner&&owner->source->loadIdentity=="own-load"&&owner->source->epoch==23&&owner->Group().sourceFile=="own/source.def"&&owner->Record().rawId==700&&
            owner->document->pools[0].words==std::vector<std::uint32_t>({0x12345678,0x7fc01234})&&owner->document->pools[1].words[0]==0xffffffff,"Compiled provenance/prefix pools lost ownership");
        Check(fact.origin.member.bytes==text.size()&&fact.origin.member.fingerprint==Hash(Data(text))&&fact.origin.member.archive.bytes==owner->document->sourceBytes&&
            fact.origin.member.archive.fingerprint==owner->document->sourceFingerprint&&fact.origin.member.name.find("compiled/own-load/")==0,"Record identity was confused with source-file label or packet identity");
        Check(fact.origin.begin<fact.origin.end&&fact.origin.end<=text.size()&&text.substr(fact.origin.begin,fact.origin.end-fact.origin.begin)==fact.data.rawText&&
            owner->Record().textSpan.begin+fact.data.values[2].begin<owner->Record().textSpan.end,"Record-local source spans escaped the packet payload");
    });
    Group("opaque child barriers and typed inheritance",[]{
        SourceDefinitions d;Import(d,{"entityDef a { \"v\" 0 0 \"same\" }","entityDef b { \"v\" 0 0 \"same\" }",
            "entityDef equal { \"inherit\" 0 0 \"a\" \"inherit\" 0 0 \"b\" }",
            "entityDef different { \"v\" 4 1 7 }","entityDef conflict { \"inherit\" 0 0 \"a\" \"inherit\" 0 0 \"different\" }",
            "entityDef barrier { \"inherit\" 0 0 \"a\" \"v\" 9 1 0 }",
            "entityDef override { \"inherit\" 0 0 \"a\" \"inherit\" 0 0 \"different\" \"v\" 0 0 \"own\" }",
            "entityDef missing { \"inherit\" 0 0 \"absent\" \"v\" 0 0 \"own\" }",
            "entityDef cycleA { \"inherit\" 0 0 \"cycleB\" }","entityDef cycleB { \"inherit\" 0 0 \"cycleA\" \"v\" 0 0 \"own\" }"});
        const auto equal=d.Resolve(SourceDefinitionKind::EntityDef,"equal");Check(Scalar(equal,"v",SourceLiteralEncoding::CompiledString)=="same"&&Field(equal,"v").origins.size()==2&&equal.inheritance.size()==2,"Equal typed inheritance lost origins/parent indices");
        Reject([&]{d.Resolve(SourceDefinitionKind::EntityDef,"conflict");},"Different typed parents silently chose a value");
        const auto barrier=d.Resolve(SourceDefinitionKind::EntityDef,"barrier");Check(Field(barrier,"v").encoding==SourceLiteralEncoding::CompiledOpaque&&Field(barrier,"v").value.empty()&&Field(barrier,"v").origins[0].origin.declarationName=="barrier","Opaque child exposed a qualified parent value");
        Check(Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"override"),"v",SourceLiteralEncoding::CompiledString)=="own","Explicit typed child override failed");
        Reject([&]{d.Resolve(SourceDefinitionKind::EntityDef,"missing");},"Own compiled value hid missing parent");Reject([&]{d.Resolve(SourceDefinitionKind::EntityDef,"cycleB");},"Own compiled value hid an inheritance cycle");
        const auto& raw=Field(barrier,"v");Check(raw.rawValueType==9&&raw.rawValueAux==1&&raw.origins[0].data.values[2].text=="0","Opaque tuple metadata/value fact was lost");
    });
    Group("profile separation and unchanged model grammar",[]{
        const auto compiled=Entries({"entityDef e { \"key\" 0 0 \"value\" }"});SourceDefinitions typed,plain;
        typed.AddCompiledRecord(compiled[0]);Plain(plain,"entityDef e { \"key\" 0 0 \"value\" }");
        Check(Scalar(typed.Resolve(SourceDefinitionKind::EntityDef,"e"),"key",SourceLiteralEncoding::CompiledString)=="value"&&
            Scalar(plain.Resolve(SourceDefinitionKind::EntityDef,"e"),"key",SourceLiteralEncoding::PlainText)=="0","Plain/compiled input profiles were silently conflated");
        const auto notTyped=Entries({"entityDef plainBody { key value }"},"plain-body");Reject([&]{typed.AddCompiledRecord(notTyped[0]);},"Plain entity body was accepted as a compiled tuple");
        SourceDefinitions mixed;Plain(mixed,"entityDef a { key value }");Import(mixed,{"entityDef b { key 0 0 value }","entityDef child { inherit 0 0 a inherit 0 0 b }",
            "entityDef replaced { inherit 0 0 a inherit 0 0 b key 0 0 own }"});
        Reject([&]{mixed.Resolve(SourceDefinitionKind::EntityDef,"child");},"Plain and compiled strings acquired an unqualified value-equivalence policy");
        Check(Scalar(mixed.Resolve(SourceDefinitionKind::EntityDef,"replaced"),"key",SourceLiteralEncoding::CompiledString)=="own","Explicit mixed-profile replacement failed");
        SourceDefinitions models;Import(models,{"model base { mesh models/own/body.md5r\n anim idle models/own/idle.md5anim\n anim open models/own/open.md5anim { frame 1 unknown }\n}","model child { inherit base skeleton models/own/rig.hkx }"});
        const auto r=models.ResolveModelAnimations("child");Check(Scalar(r.model,"mesh",SourceLiteralEncoding::PlainText)=="models/own/body.md5r"&&
            r.Find("idle")->shape==SourceAnimationReferenceShape::SingleLiteralNoTail&&r.Find("open")->shape==SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock&&
            r.Find("idle")->origins[0].origin.member.compiledRecord&&!r.gameplaySupported,"Compiled model grammar was interpreted as entity tuples or runtime actions");
    });
    Group("default declaration names remain literal after compiled imports",[]{
        const auto entries=Entries({R"DECL(entityDef "Dir\Part.DEF" { v 0 0 backslash })DECL",
            R"DECL(entityDef "dir/part.hkx" { v 0 0 slash })DECL"},"literal-names");
        SourceDefinitions implicit,explicitOld(SourceDefinitionLimits(),SourceDefinitionNamePolicy::AsciiCaseFoldLiteralV1);
        for(auto* d:{&implicit,&explicitOld}){for(const auto& entry:entries)d->AddCompiledRecord(entry);
            Check(d->DefinitionCount()==2&&Scalar(d->Resolve(SourceDefinitionKind::EntityDef,"DIR\\PART.def"),"v",SourceLiteralEncoding::CompiledString)=="backslash"&&
                Scalar(d->Resolve(SourceDefinitionKind::EntityDef,"DIR/PART.HKX"),"v",SourceLiteralEncoding::CompiledString)=="slash","Compiled import changed the default declaration-name policy");
            Reject([&]{d->Resolve(SourceDefinitionKind::EntityDef,"dir/part.def");},"Default policy folded declaration-name slashes");
            Reject([&]{d->Resolve(SourceDefinitionKind::EntityDef,"dir\\part");},"Default policy removed a declaration extension");}
        Reject([]{SourceDefinitions invalid(SourceDefinitionLimits(),static_cast<SourceDefinitionNamePolicy>(99));},"Unknown declaration-name policy was accepted");
    });
    Group("canonical declaration parents preserve raw names and field keys",[]{
        SourceDefinitions d(SourceDefinitionLimits(),SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
        Import(d,{R"DECL(entityDef "Root\Parent.entitydef" {
"value.path" 0 0 dotted
"value/part" 0 0 slash
"value\part" 0 0 backslash
asset 0 0 "Models/Own/Clip.MD5ANIM"
})DECL",R"DECL(entityDef "Root/Child.entitydef" { inherit 0 0 "ROOT/PARENT.Def" own 4 1 7 })DECL"},"canonical-parents");
        const auto r=d.Resolve(SourceDefinitionKind::EntityDef,"ROOT\\CHILD.hkx");
        Check(r.selected.declarationName=="Root/Child.entitydef"&&r.selected.rawDeclarationName=="\"Root/Child.entitydef\""&&
            r.declarations.size()==2&&r.inheritance.size()==1,"Canonical lookup replaced source declaration spelling/provenance");
        Check(r.inheritance[0].parent.data.values[2].text=="ROOT/PARENT.Def"&&r.inheritance[0].resolved.declarationName=="Root\\Parent.entitydef"&&
            !r.inheritance[0].explicitMapContext,"Canonical parent lookup rewrote the parent token or invented MAPDEF context");
        Check(Scalar(r,"VALUE.PATH",SourceLiteralEncoding::CompiledString)=="dotted"&&Scalar(r,"value/part",SourceLiteralEncoding::CompiledString)=="slash"&&
            Scalar(r,"value\\part",SourceLiteralEncoding::CompiledString)=="backslash"&&r.Find("value")==nullptr,"Declaration-name policy leaked into field-key lookup");
        Check(Scalar(r,"asset",SourceLiteralEncoding::CompiledString)=="Models/Own/Clip.MD5ANIM"&&
            Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"root/child"),"own",SourceLiteralEncoding::CompiledSignedInteger)=="7","Canonical lookup rewrote an asset value");
        const auto& fact=Field(r,"value.path").origins[0];const auto owner=fact.origin.member.compiledRecord;
        Check(owner&&fact.origin.declarationName=="Root\\Parent.entitydef"&&
            std::string(owner->Record().text.begin()+fact.origin.begin,owner->Record().text.begin()+fact.origin.end)==fact.data.rawText,
            "Canonical lookup lost owning record-relative item spans");
    });
    Group("canonical collisions refuse instead of selecting a winner",[]{
        SourceDefinitions d(SourceDefinitionLimits(),SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
        Import(d,{R"DECL(entityDef "Dir\Thing.DEF" { v 0 0 first })DECL"},"first-canonical");
        const auto collisions=Entries({"entityDef DIR/THING.md5anim { v 0 0 other }"},"later-canonical");
        Reject([&]{d.AddCompiledRecord(collisions[0]);},"Different compiled aliases sharing a canonical key chose a winner");
        Reject([&]{Plain(d,"entityDef dir/thing.hkx { v first }","own/collision.def");},"Mixed plain/compiled canonical duplicate was accepted");
        Check(d.MemberCount()==1&&d.DefinitionCount()==1&&Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"DIR/THING.any"),"v",SourceLiteralEncoding::CompiledString)=="first",
            "Rejected canonical aliases mutated prior declarations");
        Reject([&]{Plain(d,"entityDef fresh.one { v one } entityDef FRESH.two { v two }","own/transaction.def");},"Canonical duplicate inside one member partially committed");
        Check(d.MemberCount()==1&&d.DefinitionCount()==1,"Failed within-member canonical transaction consumed catalog entries");
        Plain(d,"entityDef fresh.three { v third }","own/transaction.def");Check(d.MemberCount()==2&&Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"fresh.four"),"v",SourceLiteralEncoding::PlainText)=="third",
            "Canonical rejection consumed a member identity or later valid import");
        SourceDefinitions types(SourceDefinitionLimits(),SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
        Import(types,{"future.Type asset/a.one {}","future/Type asset/a.two {}","future.Other asset/a.three {}",
            "entityDef asset/a.four {}","model asset/a.five {}"},"type-namespaces");
        Check(types.DefinitionCount()==5,"Declaration-name canonicalization leaked into declaration types");
        const auto sameType=Entries({R"DECL(FUTURE.TYPE "ASSET\A.six" {})DECL"},"duplicate-type");
        Reject([&]{types.AddCompiledRecord(sameType[0]);},"Other-kind type names did not fold ASCII case");
        Check(types.DefinitionCount()==5&&types.MemberCount()==5,"Canonical type collision changed prior namespace entries");
    });
    Group("last dot anywhere matches the catalog including directory dots",[]{
        const auto entries=Entries({"entityDef Models.v1/Body { v 0 0 directory }","entityDef .hidden { v 0 0 emptyKey }",
            R"DECL(entityDef "Folder\Clip.part" { v 0 0 suffix })DECL"},"last-dot");
        CompiledDeclarationCatalog catalog;catalog.AddDocument(*entries[0].document,*entries[0].source);
        SourceDefinitions d(SourceDefinitionLimits(),SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);for(const auto& e:entries)d.AddCompiledRecord(e);
        struct Example {const char* query;const char* raw;const char* value;};const Example examples[]={
            {"MODELS.other/Any","Models.v1/Body","directory"},{".anything",".hidden","emptyKey"},{"FOLDER/CLIP.any","Folder\\Clip.part","suffix"}};
        for(const auto& e:examples){const auto lookup=catalog.Lookup("ENTITYDEF",e.query,CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1);
            Check(lookup.matches.size()==1&&lookup.matches[0].name==e.raw,"Independent catalog canonical-name control differs");
            const auto r=d.Resolve(SourceDefinitionKind::EntityDef,e.query);Check(r.selected.declarationName==e.raw&&Scalar(r,"v",SourceLiteralEncoding::CompiledString)==e.value,
                "SourceDefinitions canonical profile differs from catalog last-dot/slash behavior");}
        Reject([&]{d.Resolve(SourceDefinitionKind::EntityDef,"Models.v1/Body.hkx");},"Last-dot profile was changed to basename-only extension stripping");
        Reject([&]{d.Resolve(SourceDefinitionKind::EntityDef,"");},"Canonical empty key bypassed the nonempty raw query gate");
        const auto colliding=Entries({"entityDef MODELS.v2/Other { v 0 0 ignored }"},"directory-collision");
        Reject([&]{d.AddCompiledRecord(colliding[0]);},"Dot-directory aliases did not share the exact catalog canonical key");
        Check(d.DefinitionCount()==3,"Dot-directory collision mutated canonical declarations");
    });
    Group("canonical model closure keeps logical names and MAPDEF exact",[]{
        SourceDefinitions d(SourceDefinitionLimits(),SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
        Import(d,{R"DECL(model "Models\Base.model" {
mesh Models/Own/Body.MD5R
anim Open.md5anim models/own/open.md5anim
anim open.hkx models/own/open.hkx
anim "Folder\Walk01.hkx" "Models\Own\Walk.md5anim"
})DECL",R"DECL(model Models/Child.def { inherit "MODELS/BASE.other"
anim OPEN.md5anim models/own/replaced.md5anim { frame 1 sound own }
})DECL",R"DECL(model "Context\Map.model" { skin Own/Skin })DECL",
            "model Models/Mapped.def { inherit MAPDEF }","model mapdef { skin Ordinary/Skin }",
            "model Models/Lower.def { inherit mapdef }","model Models/NotPlaceholder.def { inherit MAPDEF.ext }"},"model-canonical");
        const auto r=d.ResolveModelAnimations("MODELS\\CHILD.hkx");
        Check(r.references.size()==3&&r.Find("open")==nullptr&&r.Find("OPEN.MD5ANIM")&&r.Find("open.hkx")&&r.Find("folder\\walk01.hkx")&&r.Find("folder/walk01.hkx")==nullptr,
            "Canonical declaration policy stemmed or slash-folded logical animation names");
        Check(r.Find("open.md5anim")->shape==SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock&&
            r.Find("open.md5anim")->assetReference.text=="models/own/replaced.md5anim"&&r.Find("open.hkx")->assetReference.text=="models/own/open.hkx"&&
            r.Find("folder\\walk01.hkx")->assetReference.text=="Models\\Own\\Walk.md5anim","Canonical model merge inferred an asset-extension bridge");
        Check(r.model.declarations.size()==2&&r.model.declarations[0].declarationName=="Models\\Base.model"&&
            Scalar(r.model,"mesh",SourceLiteralEncoding::PlainText)=="Models/Own/Body.MD5R","Animation dependency lookup lost raw source names/mesh value");
        Reject([&]{d.ResolveModelAnimations("models/mapped.hkx");},"Exact MAPDEF did not require caller context under canonical lookup");
        SourceDefinitionContext context;context.mapModelDefinition="CONTEXT/MAP.any";const auto mapped=d.ResolveModelAnimations("models/mapped.hkx",context);
        Check(mapped.model.inheritance.size()==1&&mapped.model.inheritance[0].explicitMapContext&&
            Scalar(mapped.model,"skin",SourceLiteralEncoding::PlainText)=="Own/Skin","Explicit MAPDEF context missed canonical dependency lookup");
        for(const auto* name:{"models/lower.hkx","models/notplaceholder.hkx"})Check(Scalar(d.ResolveModelAnimations(name,context).model,"skin",SourceLiteralEncoding::PlainText)=="Ordinary/Skin",
            "Canonical lookup expanded the exact MAPDEF placeholder rule");
        Import(d,{R"DECL(entityDef "Nodes/A.def" { inherit 0 0 "NODES\B.hkx" v 0 0 own })DECL",
            "entityDef Nodes/B.def { inherit 0 0 nodes/a.other }"},"canonical-cycle");
        Reject([&]{d.Resolve(SourceDefinitionKind::EntityDef,"nodes/a.hkx");},"Canonical parent cycle escaped the full closure gate");
    });
    Group("opaque equality is scoped to the owning document pools",[]{
        SourceDefinitions separate;const auto a=Entries({"entityDef a { v 9 3 0 }"},"pool-a",0x3f800000),b=Entries({"entityDef b { v 9 3 0 }"},"pool-b",0x40000000);
        separate.AddCompiledRecord(a[0]);separate.AddCompiledRecord(b[0]);Import(separate,{"entityDef child { inherit 0 0 a inherit 0 0 b }","entityDef explicitChild { inherit 0 0 a inherit 0 0 b v 0 0 own }"},"children");
        Check(a[0].document->pools[1].words[0]!=b[0].document->pools[1].words[0],"Different-pool control fixture is not distinct");
        Reject([&]{separate.Resolve(SourceDefinitionKind::EntityDef,"child");},"Equal opaque indices from different source pools were coalesced");
        Check(Scalar(separate.Resolve(SourceDefinitionKind::EntityDef,"explicitChild"),"v",SourceLiteralEncoding::CompiledString)=="own","Explicit child failed to replace unqualified pool-dependent parents");
        SourceDefinitions same;Import(same,{"entityDef a { v 9 3 0 }","entityDef b { v 9 3 0 }","entityDef child { inherit 0 0 a inherit 0 0 b }"},"same-pool");
        const auto resolved=same.Resolve(SourceDefinitionKind::EntityDef,"child");const auto& opaque=Field(resolved,"v");
        Check(opaque.encoding==SourceLiteralEncoding::CompiledOpaque&&opaque.value.empty()&&opaque.origins.size()==2&&
            opaque.origins[0].origin.member.compiledRecord->document==opaque.origins[1].origin.member.compiledRecord->document,"Identical same-document opaque facts lost their source identity");
    });
    Group("owning numeric words inline bits and checked pool spans",[]{
        const auto entries=EntriesWithPools({"entityDef numeric {\n f 1 1 0\n fs 1 3 1\n is 2 4 0\n i 2 1 0\n"
            " inline 3 1 3212836864\n signedBits 3 1 -1082130432\n negativeZero 3 1 -2147483648\n nan 3 1 2143289345\n infinity 3 1 2139095040\n allBits 3 1 4294967295\n"
            " integer 4 1 -2147483648\n notBool 4 1 2\n countZero 1 0 9999\n countBig 1 256 9999\n inlineArray 3 2 0\n unknown 9 3 0\n tooBig 3 1 4294967296\n tooNegative 3 1 -2147483649\n hex 3 1 0x3f800000\n}"},
            "numeric-bits",{0xffffffff,0x80000000,0,0x7fffffff},{0x3f800000,0x80000000,0x7fc00001,0x7f800000,0xff800000});
        ResolvedSourceDefinition r;{SourceDefinitions d;d.AddCompiledRecord(entries[0]);r=d.Resolve(SourceDefinitionKind::EntityDef,"numeric");}
        Words(r,"f",SourceLiteralEncoding::CompiledFloat32,{0x3f800000});Words(r,"fs",SourceLiteralEncoding::CompiledFloatArray,{0x80000000,0x7fc00001,0x7f800000});
        Words(r,"is",SourceLiteralEncoding::CompiledIntegerArray,{0xffffffff,0x80000000,0,0x7fffffff});Words(r,"i",SourceLiteralEncoding::CompiledIntegerArray,{0xffffffff});
        Words(r,"inline",SourceLiteralEncoding::CompiledFloat32,{0xbf800000});Words(r,"signedBits",SourceLiteralEncoding::CompiledFloat32,{0xbf800000});
        Words(r,"negativeZero",SourceLiteralEncoding::CompiledFloat32,{0x80000000});Words(r,"nan",SourceLiteralEncoding::CompiledFloat32,{0x7fc00001});
        Words(r,"infinity",SourceLiteralEncoding::CompiledFloat32,{0x7f800000});Words(r,"allBits",SourceLiteralEncoding::CompiledFloat32,{0xffffffff});
        Check(Scalar(r,"integer",SourceLiteralEncoding::CompiledSignedInteger)=="-2147483648"&&Field(r,"integer").numericWords==std::vector<std::uint32_t>({0x80000000})&&
            Scalar(r,"notBool",SourceLiteralEncoding::CompiledSignedInteger)=="2"&&Field(r,"notBool").numericWords[0]==2,"Signed integer token/bit profile changed or was coerced to bool");
        for(const auto* key:{"countZero","countBig","inlineArray","unknown","tooBig","tooNegative","hex"})Check(Field(r,key).encoding==SourceLiteralEncoding::CompiledOpaque&&Field(r,key).value.empty()&&Field(r,key).numericWords.empty(),"Unsupported count/kind/inline word spelling was guessed");
        const auto& fact=Field(r,"fs").origins[0];Check(fact.data.rawValueType==1&&fact.data.rawValueAux==3&&fact.data.values.size()==3&&fact.data.values[2].text=="1"&&
            fact.origin.member.compiledRecord->document->pools[1].words[1]==fact.data.numericWords[0],"Numeric materialization replaced raw index/count/owning pool provenance");
        auto& changed=r.literalFields.at("fs").numericWords;changed[0]=7;Check(fact.data.numericWords[0]==0x80000000&&entries[0].document->pools[1].words[1]==0x80000000,"Resolved numeric words alias fact/pool storage");
        std::vector<std::uint32_t> maximum(255);for(std::size_t i=0;i<maximum.size();++i)maximum[i]=std::uint32_t(i);
        SourceDefinitions full;const auto limit=EntriesWithPools({"entityDef maxCount { f 1 255 0 i 2 255 0 }"},"max-count",maximum,maximum);full.AddCompiledRecord(limit[0]);const auto max=full.Resolve(SourceDefinitionKind::EntityDef,"maxCount");
        Words(max,"f",SourceLiteralEncoding::CompiledFloatArray,maximum);Words(max,"i",SourceLiteralEncoding::CompiledIntegerArray,maximum);
    });
    Group("numeric inheritance compares decoded words not serialized indices",[]{
        SourceDefinitions d;const auto same=EntriesWithPools({"entityDef a { v 1 2 0 }","entityDef b { v 1 2 2 }","entityDef joined { inherit 0 0 a inherit 0 0 b }"},"alias-pool",{1},{0x3f800000,0x40000000,0x3f800000,0x40000000});
        for(const auto& e:same)d.AddCompiledRecord(e);const auto alias=d.Resolve(SourceDefinitionKind::EntityDef,"joined");Words(alias,"v",SourceLiteralEncoding::CompiledFloatArray,{0x3f800000,0x40000000});
        Check(Field(alias,"v").origins.size()==2&&Field(alias,"v").origins[0].data.values[2].text!=Field(alias,"v").origins[1].data.values[2].text,"Aliased indices did not coalesce with both raw provenances");
        for(const auto& e:EntriesWithPools({"entityDef c { v 1 2 0 }"},"equal-other-pool",{}, {0x3f800000,0x40000000}))d.AddCompiledRecord(e);
        Import(d,{"entityDef equalDocs { inherit 0 0 a inherit 0 0 c }"},"equal-child");const auto equal=d.Resolve(SourceDefinitionKind::EntityDef,"equalDocs");Check(Field(equal,"v").origins.size()==2,"Equal decoded numeric words across documents did not coalesce");
        for(const auto& e:EntriesWithPools({"entityDef unequal { v 1 2 0 }"},"unequal-pool",{}, {0x3f800000,0x40400000}))d.AddCompiledRecord(e);
        Import(d,{"entityDef conflict { inherit 0 0 a inherit 0 0 unequal }","entityDef replaced { inherit 0 0 a inherit 0 0 unequal v 4 1 7 }"},"numeric-children");
        Reject([&]{d.Resolve(SourceDefinitionKind::EntityDef,"conflict");},"Different decoded words behind equal pool indices coalesced");Check(Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"replaced"),"v",SourceLiteralEncoding::CompiledSignedInteger)=="7","Explicit numeric child override failed");
        SourceDefinitions ints;Import(ints,{"entityDef a { v 4 1 +1 }","entityDef b { v 4 1 01 }","entityDef c { inherit 0 0 a inherit 0 0 b }"},"integer-spelling");
        const auto integer=ints.Resolve(SourceDefinitionKind::EntityDef,"c");Check(Field(integer,"v").numericWords==std::vector<std::uint32_t>({1})&&Field(integer,"v").origins.size()==2&&Field(integer,"v").value.size()==1,"Known signed integers compared spelling instead of admitted word identity");
    });
    Group("malformed admitted pool references refuse atomically",[]{
        SourceDefinitions d;Plain(d,"entityDef keep { key original }");
        for(const auto* tuple:{"1 1 -1","1 1 abc","1 1 \"0\"","1 1 2147483648","1 1 1","1 2 0","2 1 -1","2 1 2","2 3 0"}){
            const auto bad=Entries({std::string("entityDef bad { inherit 0 0 keep key ")+tuple+" }"},"bad-pool");
            Reject([&]{d.AddCompiledRecord(bad[0]);},"Malformed admitted numeric reference became opaque/fell back to parent");
            Check(d.MemberCount()==1&&d.DefinitionCount()==1&&Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"keep"),"key",SourceLiteralEncoding::PlainText)=="original","Failed numeric import changed prior state");
        }
        const auto good=Entries({"entityDef valid { v 1 1 0 }"},"bad-pool");d.AddCompiledRecord(good[0]);Words(d.Resolve(SourceDefinitionKind::EntityDef,"valid"),"v",SourceLiteralEncoding::CompiledFloat32,{0xffffffff});
    });
    Group("numeric vector retention and result budgets",[]{
        std::string known="entityDef values {\n",unknown="entityDef values {\n";for(unsigned i=0;i<64;++i){known+="k"+std::to_string(i)+" 1 255 0\n";unknown+="k"+std::to_string(i)+" 9 255 0\n";}known+="}";unknown+="}";
        const auto words=std::vector<std::uint32_t>(255,0x3f800000);const auto a=EntriesWithPools({known},"known-budget",{},words),b=EntriesWithPools({unknown},"opaque-budget",{},words);
        SourceDefinitionLimits cap;cap.storedBytes=160u<<10;SourceDefinitions opaque(cap),numeric(cap);opaque.AddCompiledRecord(b[0]);
        Check(opaque.DefinitionCount()==1,"Opaque retained-budget control failed");Reject([&]{numeric.AddCompiledRecord(a[0]);},"Materialized numeric vectors escaped parse retained budget");Check(numeric.MemberCount()==0,"Rejected vector allocations committed a partial import");
        cap=SourceDefinitionLimits();cap.resultBytes=192u<<10;SourceDefinitions opaqueResult(cap),numericResult(cap);opaqueResult.AddCompiledRecord(b[0]);numericResult.AddCompiledRecord(a[0]);
        Check(opaqueResult.Resolve(SourceDefinitionKind::EntityDef,"values").literalFields.size()==64,"Opaque result-budget control failed");Reject([&]{numericResult.Resolve(SourceDefinitionKind::EntityDef,"values");},"Result/fact numeric word copies escaped output budget");
    });
    Group("malformed tuples and transactional import",[]{
        SourceDefinitions d;Plain(d,"entityDef keep { key original }");const auto members=d.MemberCount(),definitions=d.DefinitionCount();
        const auto unchanged=[&](){Check(d.MemberCount()==members&&d.DefinitionCount()==definitions&&Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"keep"),"key",SourceLiteralEncoding::PlainText)=="original","Failed import changed prior definitions or counts");};
        for(const auto* body:{"key","key 0","key 0 0","key \"0\" 0 value","key 2147483648 0 value","key 0 -2147483649 value","key 0 0 { nested }",
            "inherit 4 1 1","inherit 1 1 0","key 0 0 a key 0 0 b","key 0 0 a tail"}){
            const auto bad=Entries({std::string("entityDef bad { ")+body+" }"},"bad-tuples");Reject([&]{d.AddCompiledRecord(bad[0]);},"Malformed tuple/unqualified inherit admitted");unchanged();}
        const auto good=Entries({"entityDef imported { key 0 0 value }"},"good");d.AddCompiledRecord(good[0]);const auto count=d.MemberCount();
        Reject([&]{d.AddCompiledRecord(good[0]);},"Duplicate compiled member was imported twice");Check(d.MemberCount()==count&&Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"imported"),"key",SourceLiteralEncoding::CompiledString)=="value","Duplicate rejection mutated an existing compiled declaration");
        const auto duplicate=Entries({"entityDef IMPORTED { key 0 0 other }"},"another-load");Reject([&]{d.AddCompiledRecord(duplicate[0]);},"Case-fold duplicate declaration bypassed transaction");
        Check(d.MemberCount()==count,"Duplicate declaration consumed a member slot");
    });
    Group("forged entry selectors headers and spans",[]{
        const auto original=Entries({"entityDef candidate { key 0 0 value }"})[0];SourceDefinitions d;Plain(d,"entityDef keep { key original }");
        const std::vector<std::function<void(CompiledCatalogEntry&)>> forge{
            [](CompiledCatalogEntry& x){x.document.reset();},[](CompiledCatalogEntry& x){x.source.reset();},[](CompiledCatalogEntry& x){x.section=2;},
            [](CompiledCatalogEntry& x){x.group=99;},[](CompiledCatalogEntry& x){x.record=99;},[](CompiledCatalogEntry& x){x.type="model";},[](CompiledCatalogEntry& x){x.name="different";},
            [](CompiledCatalogEntry& x){++x.typeSpan.begin;},[](CompiledCatalogEntry& x){--x.nameSpan.end;},[](CompiledCatalogEntry& x){++x.bodySpan.begin;},
            [](CompiledCatalogEntry& x){++x.bodySpan.end;},[](CompiledCatalogEntry& x){--x.envelopeSpan.end;},
            [](CompiledCatalogEntry& x){auto doc=std::make_shared<CompiledDeclarations>(*x.document);++doc->sections[0].groups[0].records[0].textSpan.end;x.document=doc;},
            [](CompiledCatalogEntry& x){x.document=std::make_shared<const CompiledDeclarations>(*x.document);},
            [](CompiledCatalogEntry& x){x.source=std::make_shared<const CompiledCatalogSource>(*x.source);},
            [](CompiledCatalogEntry& x){auto source=std::make_shared<CompiledCatalogSource>(*x.source);source->fingerprint^=1;x.source=source;},
            [](CompiledCatalogEntry& x){++x.loadOrdinal;},[](CompiledCatalogEntry& x){++x.documentOrdinal;},[](CompiledCatalogEntry& x){++x.catalogOrdinal;}};
        std::size_t forgeIndex=0;
        for(const auto& mutate:forge){auto entry=original;mutate(entry);const auto reason=std::string("Forged compiled entry case ")+std::to_string(forgeIndex++)+" was admitted";Reject([&]{d.AddCompiledRecord(entry);},reason.c_str());
            Check(d.MemberCount()==1&&d.DefinitionCount()==1&&Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"keep"),"key",SourceLiteralEncoding::PlainText)=="original","Rejected forged entry changed prior catalog state");}
        d.AddCompiledRecord(original);Check(d.MemberCount()==2&&Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"candidate"),"key",SourceLiteralEncoding::CompiledString)=="value","Forged import consumed budgets/identity of subsequent valid record");
    });
    Group("owning compiled documents and retained result budgets",[]{
        ResolvedSourceDefinition retained;{const auto entries=Entries({"entityDef retained { key 0 0 value }"});SourceDefinitions d;d.AddCompiledRecord(entries[0]);retained=d.Resolve(SourceDefinitionKind::EntityDef,"retained");}
        const auto owner=Field(retained,"key").origins[0].origin.member.compiledRecord;Check(owner&&owner->source->epoch==23&&owner->document->pools[0].words[0]==0x12345678&&owner->Record().text==Data("entityDef retained { key 0 0 value }"),"Resolved compiled owner/pools died with catalog/frontend/input");
        const auto large=Entries({"entityDef first { key 0 0 value }","entityDef second { key 0 0 value }",std::string("future unused { \"")+std::string(32768,'x')+"\" }"},"large-document");
        SourceDefinitionLimits low;low.storedBytes=64u<<10;SourceDefinitions constrained(low);Plain(constrained,"entityDef keep { key original }");
        Reject([&]{constrained.AddCompiledRecord(large[0]);},"Small selected record retained a large unchecked document outside storedBytes");Check(constrained.MemberCount()==1,"Rejected large-document retention committed input metadata");
        constrained.AddCompiledRecord(Entries({"entityDef small { key 0 0 value }"},"small-document")[0]);Check(constrained.MemberCount()==2,"Rejected retention consumed subsequent valid import budget");
        SourceDefinitionLimits shared;shared.storedBytes=192u<<10;SourceDefinitions deduplicated(shared);deduplicated.AddCompiledRecord(large[0]);deduplicated.AddCompiledRecord(large[1]);
        Check(deduplicated.MemberCount()==2&&Scalar(deduplicated.Resolve(SourceDefinitionKind::EntityDef,"second"),"key",SourceLiteralEncoding::CompiledString)=="value","Shared source document was charged per record instead of once");
        SourceDefinitionLimits result;result.resultBytes=32u<<10;SourceDefinitions capped(result);capped.AddCompiledRecord(large[0]);Reject([&]{capped.Resolve(SourceDefinitionKind::EntityDef,"first");},"Resolved provenance retained a whole large document outside result cap");
        Check(capped.MemberCount()==1&&capped.DefinitionCount()==1,"Failed bounded result changed the frontend catalog");
    });
    std::cout<<"WOLF2009_COMPILED_DEFINITIONS_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed="<<failed<<'\n';
}
void Original(const char* path){
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open selected trainyard archive");const auto archive=Archive::Scan(input);std::size_t chunk=0,entry=0,matches=0;
    for(std::size_t c=0;c<archive.Chunks().size();++c)if(archive.Chunks()[c].type=="DECL")for(std::size_t e=0;e<archive.Chunks()[c].entries.size();++e)
        if(archive.Chunks()[c].entries[e].name=="maps/game/trainyard/trainyard"){chunk=c;entry=e;++matches;}
    Check(matches==1,"Selected trainyard DECL packet missing/ambiguous");const auto bytes=archive.ReadEntry(input,chunk,entry);
    const auto document=CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1);Check(document.sourceBytes==2112750&&document.sourceFingerprint==UINT64_C(0xc8d044282ad8a99e),"Selected source packet identity differs");
    CompiledCatalogSource source;source.loadIdentity="selected-trainyard";source.path="owner/trainyard/DECL/maps/game/trainyard/trainyard";source.role=CompiledCatalogRole::Map;
    source.byteIdentityKnown=true;source.bytes=document.sourceBytes;source.fingerprint=document.sourceFingerprint;source.epoch=0;
    SourceDefinitionLimits limits;limits.members=2048;SourceDefinitions d(limits);std::size_t entities=0,models=0;
    {CompiledDeclarationCatalog catalog;catalog.AddDocument(document,source);for(std::size_t i=0;i<catalog.EntryCount();++i){const auto record=catalog.Entry(i);
        if(record.type=="entityDef"){d.AddCompiledRecord(record);++entities;}else if(record.type=="model"){d.AddCompiledRecord(record);++models;}}}
    Check(entities==974&&models==31&&d.MemberCount()==1005&&d.DefinitionCount()==1005,"Selected compiled entity/model import counts differ");
    const auto container=d.Resolve(SourceDefinitionKind::EntityDef,"container_locker_foot_01");Check(!container.gameplaySupported,"Compiled source frontend claimed container gameplay");
    Check(Scalar(container,"spawnclass",SourceLiteralEncoding::CompiledString)=="rvAnimatedContainer"&&Scalar(container,"model",SourceLiteralEncoding::CompiledString)=="model_env_locker_foot_01_anim","Actual container class/model literals changed");
    for(const auto* key:{"noclipmodel","usable","persist","applyHilightTemplate"})Check(Scalar(container,key,SourceLiteralEncoding::CompiledSignedInteger)=="1","Actual container signed flag type/value differs");
    Check(Scalar(container,"hide_finish",SourceLiteralEncoding::CompiledSignedInteger)=="0"&&Scalar(container,"giveDelay",SourceLiteralEncoding::CompiledSignedInteger)=="1200"&&
        Scalar(container,"health",SourceLiteralEncoding::CompiledSignedInteger)=="10000","Actual container signed delay/flag/health differs");
    const auto& origin=Field(container,"giveDelay").origins[0];const auto owner=origin.origin.member.compiledRecord;
    Check(owner&&owner->source->loadIdentity=="selected-trainyard"&&owner->source->epoch==0&&owner->source->byteIdentityKnown&&owner->Record().rawId==1682&&owner->Record().textSpan.begin==1710675&&owner->Record().textSpan.end==1711184&&
        owner->document->pools[1].words.size()==1467&&origin.data.values.size()==3&&origin.data.values[0].text=="4"&&origin.data.values[1].text=="1","Actual compiled source owner/raw tuple/packet span lost");
    Check(owner->Record().textSpan.begin+origin.origin.begin<owner->Record().textSpan.end&&std::string(owner->Record().text.begin()+origin.origin.begin,owner->Record().text.begin()+origin.origin.end)==origin.data.rawText,"Actual item-relative span was confused with packet coordinates");
    const auto model=d.ResolveModelAnimations("model_env_locker_foot_01_anim");std::size_t pure=0,blocks=0;for(const auto& reference:model.references){pure+=reference.second.shape==SourceAnimationReferenceShape::SingleLiteralNoTail;blocks+=reference.second.shape==SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock;}
    Check(model.references.size()==6&&pure==4&&blocks==2&&model.Find("open")->assetReference.text=="models/props/locker_foot_01_anim_open.md5anim"&&
        model.Find("open")->origins[0].origin.member.compiledRecord&&!model.gameplaySupported,"Actual compiled model reference facts changed/promoted an extension bridge");
    const SourceDefinition* weapon=nullptr;for(std::size_t i=0;i<d.DefinitionCount();++i)if(d.Definition(i).nameToken.text=="weapon_mp40")weapon=&d.Definition(i);
    Check(weapon&&weapon->member.compiledRecord,"Selected MP40 declaration/provenance missing");const auto weaponOwner=weapon->member.compiledRecord;
    Check(weaponOwner->Record().rawId==5659&&weaponOwner->Record().textSpan.begin==2101473&&weaponOwner->Record().textSpan.end==2104607&&
        weaponOwner->Group().sourceFile=="def/weapons/mp40/mp40.def","Selected MP40 record identity differs");
    const auto mp40=d.Resolve(SourceDefinitionKind::EntityDef,"weapon_mp40");
    struct PoolExample {const char* key;std::size_t index;std::uint32_t words[3];};
    const PoolExample examples[]={
        {"flashViewOffset",431,{0x42820000,0,0}},{"hip_viewoffset",35,{0,0,0}},{"hip_viewangles",35,{0,0,0}},
        {"aim_viewoffset",434,{0x3f800000,0x3c23d70a,0}},{"aim_viewangles",437,{0xbd4ccccd,0x3db851ec,0}},
        {"PivotJointOffset",440,{0xc1c80000,0,0}}};
    for(const auto& example:examples){Words(mp40,example.key,SourceLiteralEncoding::CompiledFloatArray,
            std::vector<std::uint32_t>(example.words,example.words+3));const auto& value=Field(mp40,example.key);const auto& fact=value.origins[0];
        Check(value.rawValueType==1&&value.rawValueAux==3&&fact.data.values[2].text==std::to_string(example.index)&&
            fact.origin.member.compiledRecord->document==weaponOwner->document,"Selected MP40 raw pool index/count/owner differs");
        const auto& pool=weaponOwner->document->pools[1];const auto packetBegin=pool.dataSpan.begin+4*example.index;
        Check(packetBegin>=pool.dataSpan.begin&&packetBegin<=pool.dataSpan.end&&12<=pool.dataSpan.end-packetBegin,"Selected MP40 owning pool packet span differs");
        for(std::size_t n=0;n<3;++n){std::uint32_t word=0;for(unsigned byte=0;byte<4;++byte)word|=std::uint32_t(bytes[packetBegin+4*n+byte])<<(8*byte);
            Check(word==value.numericWords[n]&&word==pool.words[example.index+n],"Selected MP40 materialized words differ from fresh packet bytes");}
    }
    Words(mp40,"m_AccuracyCrouching",SourceLiteralEncoding::CompiledFloat32,{1022739087});
    Words(mp40,"m_AccuracyStanding",SourceLiteralEncoding::CompiledFloat32,{1024416809});
    Words(mp40,"m_AccuracyAimingModifier",SourceLiteralEncoding::CompiledFloat32,{1053609165});
    std::cout<<"WOLF2009_COMPILED_DEFINITIONS_ORIGINAL_OK imports="<<d.DefinitionCount()<<" entitydefs="<<entities<<" models="<<models
        <<" container=1 model_refs=6 pure=4 blocks=2 mp40_float_arrays=6 mp40_inline_floats=3 owning_provenance=1 all_parent_closures_claimed=0 runtime_admission=0\n";
}
}
int main(int argc,char** argv){try{Synthetic();if(failed)return 1;for(int i=1;i<argc;++i){const std::string option(argv[i]);if(option=="--self-test")continue;
    if(option=="--source-map"&&i+1<argc){Original(argv[++i]);continue;}throw std::runtime_error("Usage: [--self-test] [--source-map owner-trainyard.mpk]");}return 0;
}catch(const std::exception& error){std::cerr<<"Compiled definitions: "<<error.what()<<'\n';return 1;}}

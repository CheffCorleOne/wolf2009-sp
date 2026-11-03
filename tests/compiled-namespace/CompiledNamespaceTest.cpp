// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarationNamespace.h"
#include "SourceDefinitions.h"
#include "Archive.h"
#include "ZipArchive.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
namespace {
using namespace wolf2009;
typedef std::vector<unsigned char> Bytes;
typedef CompiledNamespaceReaderMode Mode;
typedef CompiledNamespaceDecisionKind Decision;
const CompiledNamespacePolicy policy=CompiledNamespacePolicy::OrdinaryTextOwnersV1;
unsigned checks=0,groups=0,failed=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void Reject(F f,const char* why){bool refused=false;try{f();}catch(const std::exception&){refused=true;}Check(refused,why);}
template<class F>void Group(const char* name,F f){++groups;try{f();}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}}
void Word(Bytes& b,std::uint32_t v){for(unsigned shift=0;shift<32;shift+=8)b.push_back(static_cast<unsigned char>(v>>shift));}
void Text(Bytes& b,const std::string& s){b.insert(b.end(),s.begin(),s.end());}
struct InputGroup {std::string owner;std::vector<std::string> records;};
Bytes Packet(const std::vector<InputGroup>& first,const std::vector<InputGroup>& second={},const std::vector<std::uint32_t>& floats={}){
    Bytes b;Word(b,0);Word(b,std::uint32_t(floats.size()));for(auto w:floats)Word(b,w);
    for(const auto* section:{&first,&second}){Text(b,std::to_string(section->size())+"\n");
        for(const auto& g:*section){Text(b,g.owner+"\n3\n"+std::to_string(g.records.size())+"\n");
            for(const auto& r:g.records){Text(b,"17\n"+std::to_string(r.size()+1)+"\n"+r+"\n");}}}
    return b;
}
void Add(CompiledDeclarationCatalog& c,const Bytes& bytes,const char* id,CompiledCatalogRole role=CompiledCatalogRole::Explicit){
    const auto d=CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1);CompiledCatalogSource source;
    source.loadIdentity=id;source.path=std::string("selected/")+id;source.role=role;source.byteIdentityKnown=true;source.bytes=d.sourceBytes;source.fingerprint=d.sourceFingerprint;
    c.AddDocument(d,source);
}
std::unique_ptr<CompiledDeclarationCatalog> Catalog(const std::vector<InputGroup>& first,const std::vector<InputGroup>& second={},const char* id="one"){
    std::unique_ptr<CompiledDeclarationCatalog> c(new CompiledDeclarationCatalog);Add(*c,Packet(first,second),id);return c;
}
CompiledCatalogEntry Find(const CompiledDeclarationNamespace& n,const char* type,const char* name){const auto r=n.Lookup(type,name);
    Check(r.status==CompiledCatalogLookupStatus::Unique&&r.matches.size()==1,"Selected typed key not unique");return r.matches[0];}
std::string Body(const CompiledCatalogEntry& e){return std::string(e.Record().text.begin(),e.Record().text.end());}
void Synthetic(){
    Group("actual type and canonical names preserve all raw facts",[]{
        auto c=Catalog({{"own/file.def",{"entityDef A/B.ext { v 4 1 1 }","model A/B.ext { mesh own/model }","future.Type a/b.ext {}"}}});
        CompiledDeclarationNamespace n(policy);n.ApplySection(*c,0,0,Mode::Ordinary);
        Check(n.SelectedCount()==3&&n.DecisionCount()==3&&n.ApplicationCount()==1,"Raw IDs/default group type collapsed typed keys");
        Check(Find(n,"ENTITYDEF","a\\b.any").type=="entityDef"&&Find(n,"MoDeL","A/B").Group().defaultTypeId==3&&
            Find(n,"future.type","A/B.any").Record().rawId==17,"Canonical typed lookup changed metadata");
        Check(n.Lookup("future/Type","a/b.ext").status==CompiledCatalogLookupStatus::Missing,"Type keys were slash folded");
        Check(n.SelectedState(0).seen08&&!n.SelectedState(0).protected20&&n.Application(0).accepted==3,"Accepted flags/application counts differ");
    });
    Group("per group reset duplicate and ordinary owner conflict",[]{
        auto c=Catalog({{"Own/A.def",{"entityDef x { v 0 0 first }","entityDef X.ext { v 0 0 duplicate }"}},
            {"own/b.def",{"entityDef x { v 0 0 conflict }"}},{"OWN/A.DEF",{}},{"own/a.def",{"entityDef x { v 0 0 reloaded }"}}});
        CompiledDeclarationNamespace n(policy);n.ApplySection(*c,0,0,Mode::Ordinary);
        Check(n.SelectedCount()==1&&n.DecisionCount()==4,"Reload/skip changed namespace cardinality");
        const Decision kinds[]={Decision::AcceptedNew,Decision::SkippedDuplicate,Decision::SkippedOwnerConflict,Decision::AcceptedReplacement};
        for(std::size_t i=0;i<4;++i)Check(n.Decision(i).kind==kinds[i],"Owner/seen gate selected the wrong branch");
        Check(Body(n.Selected(0)).find("reloaded")!=std::string::npos&&n.SelectedState(0).ownerLabel=="Own/A.def"&&
            n.Decision(3).previousSeen08==false&&n.Decision(2).previousSeen08&&n.Decision(3).previous.IsUnmodified(),"Reload did not preserve owner identity/previous provenance");
        Check(n.Application(0).accepted==1&&n.Application(0).replaced==1&&n.Application(0).skipped==2,"Decision application totals differ");
    });
    Group("protected gate dominates owner and model exception stays sticky",[]{
        auto first=Catalog({{"own/a.def",{"entityDef x { a 4 1 1 }","model x { mesh first }"}}},{},"first");
        auto second=Catalog({{"OWN/A.DEF",{"entityDef x { a 4 1 2 }","MODEL x { mesh second }"}}},{},"second");
        auto conflict=Catalog({{"own/b.def",{"model x { mesh otherOwner }"}}},{},"conflict");
        CompiledDeclarationNamespace n(policy);n.ApplySection(*first,0,0,Mode::ProtectAccepted);n.ApplySection(*second,0,0,Mode::Ordinary);
        Check(n.Decision(2).kind==Decision::SkippedProtected&&!n.Decision(2).previousSeen08&&n.Decision(2).resultProtected20,
            "Same-owner reset incorrectly cleared protected20");
        Check(n.Decision(3).kind==Decision::AcceptedReplacement&&n.Decision(3).model34ProtectionBypass&&n.SelectedState(1).protected20&&
            Find(n,"entityDef","x").source->loadIdentity=="first"&&Find(n,"model","x").source->loadIdentity=="second","Model bypass or sticky protected bit differs");
        n.ApplySection(*conflict,0,0,Mode::Ordinary);Check(n.Decision(4).kind==Decision::SkippedOwnerConflict&&n.Decision(4).model34ProtectionBypass,
            "Model exception bypassed ordinary owner policy");
    });
    Group("owner comparisons fold case only and retain extensions",[]{
        auto c=Catalog({{"DEF\\Source.DEF",{"entityDef x {}"}},{"def/source.def",{"entityDef X {}"}},
            {"def\\source.other",{"entityDef X {}"}},{"def\\source.def",{"entityDef X { replacement 0 0 yes }"}}});
        CompiledDeclarationNamespace n(policy);n.ApplySection(*c,0,0,Mode::Ordinary);
        Check(n.Decision(1).kind==Decision::SkippedOwnerConflict&&n.Decision(2).kind==Decision::SkippedOwnerConflict&&
            n.Decision(3).kind==Decision::AcceptedReplacement,"Owner filename used declaration slash/stem normalization");
    });
    Group("empty groups reset every declaration of only that owner",[]{
        auto c=Catalog({{"own/a.def",{"entityDef a {}","model a {}"}},{"own/b.def",{"entityDef b {}"}}},{{"OWN/A.DEF",{}}});
        CompiledDeclarationNamespace n(policy);n.ApplySection(*c,0,0,Mode::Ordinary);n.ApplySection(*c,0,1,Mode::Ordinary);
        Check(n.SelectedCount()==3&&!n.SelectedState(0).seen08&&!n.SelectedState(1).seen08&&n.SelectedState(2).seen08&&n.DecisionCount()==3,
            "Empty group reset dropped absent records or reset another owner");
        Check(n.Application(1).decisionBegin==3&&n.Application(1).decisionEnd==3,"Empty section/group created invented records");
    });
    Group("last dot anywhere canonical collisions and literal type namespaces",[]{
        auto c=Catalog({{"own/a.def",{"entityDef Models.v1/Body {}","ENTITYDEF MODELS.v2/Other {}","model.v1 .hidden {}","model/v1 .other {}"}}});
        CompiledDeclarationNamespace n(policy);n.ApplySection(*c,0,0,Mode::Ordinary);
        Check(n.SelectedCount()==3&&n.Decision(1).kind==Decision::SkippedDuplicate&&Find(n,"ENTITYDEF","models.any/thing").name=="Models.v1/Body",
            "Canonical collision changed last-dot-anywhere behavior");
        Check(Find(n,"model.v1",".anything").name==".hidden"&&Find(n,"model/v1",".anything").name==".other"&&
            n.Lookup("model",".anything").status==CompiledCatalogLookupStatus::Missing,"Type stems or canonical empty keys conflated namespaces");
        Reject([&]{n.Lookup("entityDef","");},"Empty raw lookup name accepted");
    });
    Group("section rollback restores selected flags decisions and input identity",[]{
        CompiledNamespaceLimits limits;limits.decisions=2;CompiledDeclarationNamespace n(policy,limits);
        auto initial=Catalog({{"own/a.def",{"model x { mesh first }"}}},{},"initial");n.ApplySection(*initial,0,0,Mode::Ordinary);
        auto bad=Catalog({{"own/a.def",{"model x { mesh second }","entityDef y {}"}}},{},"candidate");
        Reject([&]{n.ApplySection(*bad,0,0,Mode::Ordinary);},"Section exceeded decision limit without refusal");
        Check(n.ApplicationCount()==1&&n.DecisionCount()==1&&n.SelectedCount()==1&&n.SelectedState(0).seen08&&
            n.Selected(0).source->loadIdentity=="initial","Failed section leaked an accepted replacement/flag reset");
        auto valid=Catalog({{"own/a.def",{"model x { mesh valid }"}}},{},"candidate");n.ApplySection(*valid,0,0,Mode::Ordinary);
        Check(n.DecisionCount()==2&&Body(n.Selected(0)).find("valid")!=std::string::npos,"Failed application consumed a load identity");
    });
    Group("bounded work owners selected records and retained pools",[]{
        auto two=Catalog({{"own/a.def",{"model a {}"}},{"own/b.def",{"model b {}"}}});
        for(int which=0;which<3;++which){CompiledNamespaceLimits limits;if(which==0)limits.owners=1;else if(which==1)limits.selected=1;else limits.work=1;
            CompiledDeclarationNamespace n(policy,limits);Reject([&]{n.ApplySection(*two,0,0,Mode::Ordinary);},"Configured namespace limit was ignored");
            Check(n.SelectedCount()==0&&n.ApplicationCount()==0&&n.DecisionCount()==0,"Budget refusal partially committed namespace data");}
        CompiledNamespaceLimits limits;limits.retainedBytes=20000;CompiledDeclarationNamespace n(policy,limits);CompiledDeclarationCatalog large;
        Add(large,Packet({{"own/a.def",{"model a {}"}}},{},std::vector<std::uint32_t>(10000,0x3f800000)),"large-pool");
        Reject([&]{n.ApplySection(large,0,0,Mode::Ordinary);},"Retained owning pool escaped namespace storage budget");
        Check(n.ApplicationCount()==0,"Retained pool refusal published an application");
        Reject([]{CompiledNamespaceLimits l;l.keyBytes=0;CompiledDeclarationNamespace n(policy,l);},"Zero namespace key budget accepted");
        Reject([]{CompiledDeclarationNamespace n(static_cast<CompiledNamespacePolicy>(9));},"Unknown namespace policy accepted");
    });
    Group("explicit selection and repeated immutable applications",[]{
        auto c=Catalog({{"own/a.def",{"entityDef x {}"}}});CompiledDeclarationNamespace n(policy);
        for(Mode mode:{Mode::Unspecified,static_cast<Mode>(9)})Reject([&]{n.ApplySection(*c,0,0,mode);},"Missing/unknown reader mode accepted");
        Reject([&]{n.ApplySection(*c,1,0,Mode::Ordinary);},"Missing load unit accepted");Reject([&]{n.ApplySection(*c,0,2,Mode::Ordinary);},"Third section accepted");
        n.ApplySection(*c,0,0,Mode::Ordinary);n.ApplySection(*c,0,0,Mode::Ordinary);
        Check(n.ApplicationCount()==2&&n.Decision(1).kind==Decision::AcceptedReplacement,"Immutable same-owner section reload was treated as duplicate");
        auto reopened=Catalog({{"own/a.def",{"entityDef x { different 4 1 1 }"}}});
        Reject([&]{n.ApplySection(*reopened,0,0,Mode::Ordinary);},"Same load identity admitted a different immutable packet");
        Check(n.ApplicationCount()==2&&n.DecisionCount()==2,"Refused identity collision changed prior applications");
    });
    Group("copied records decisions and pools outlive catalogs",[]{
        CompiledDeclarationNamespace n(policy);std::weak_ptr<const CompiledDeclarations> weak;
        {CompiledDeclarationCatalog c;Add(c,Packet({{"own/a.def",{"entityDef x { v 1 1 0 }"}}},{},{0x3f800000}),"old");weak=c.Document(0);n.ApplySection(c,0,0,Mode::Ordinary);}
        Check(!weak.expired()&&n.Selected(0).IsUnmodified()&&n.Selected(0).document->pools[1].words[0]==0x3f800000,"Namespace borrowed input catalog/pools");
        auto copy=n.Selected(0);copy.name="forged";Check(!copy.IsUnmodified()&&n.Selected(0).IsUnmodified()&&n.Selected(0).name=="x","Mutable returned copy altered internal sealed entries");
        auto decision=n.Decision(0);decision.incoming.type="forged";Check(n.Decision(0).incoming.IsUnmodified(),"Mutable decision altered internal provenance");
        Check(n.SelectedEntries().size()==1&&n.Application(0).document==n.Selected(0).document,"Owning collection/application snapshots differ");
        Reject([&]{n.Selected(1);},"Selected out-of-range accepted");Reject([&]{n.SelectedState(1);},"Selected state out-of-range accepted");
        Reject([&]{n.Decision(1);},"Decision out-of-range accepted");Reject([&]{n.Application(1);},"Application out-of-range accepted");
    });
    Group("query allocation budgets and application cap",[]{
        CompiledNamespaceLimits limits;limits.resultBytes=1;limits.applications=1;auto c=Catalog({});CompiledDeclarationNamespace empty(policy,limits);
        empty.ApplySection(*c,0,0,Mode::Ordinary);Reject([&]{empty.ApplySection(*c,0,1,Mode::Ordinary);},"Application cap ignored on an empty section");
        Reject([&]{empty.Application(0);},"Application result allocation budget ignored");
        limits.resultBytes=2048;auto many=Catalog({{"own/a.def",{"entityDef a {}","entityDef b {}","entityDef c {}","entityDef d {}"}}});
        CompiledDeclarationNamespace n(policy,limits);n.ApplySection(*many,0,0,Mode::Ordinary);
        Reject([&]{n.SelectedEntries();},"Aggregate selected result ignored its allocation budget");
        Check(n.SelectedCount()==4&&n.DecisionCount()==4,"Query refusal changed retained namespace");
    });
    std::cout<<"WOLF2009_COMPILED_NAMESPACE_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed="<<failed<<'\n';
}
Bytes ReadGlobal(const std::string& base){std::ifstream f(base+"/assets.pk4",std::ios::binary);Check(bool(f),"Cannot open selected assets.pk4");
    const auto z=ZipArchive::Scan(f);std::size_t index=0,matches=0;for(std::size_t i=0;i<z.Entries().size();++i)if(z.Entries()[i].name=="maps/game/global/global.decls"){index=i;++matches;}
    Check(matches==1,"Selected global member missing/ambiguous");return z.ReadEntry(f,index);}
Bytes ReadMap(const std::string& base){std::ifstream f(base+"/maps/game/trainyard/trainyard.mpk",std::ios::binary);Check(bool(f),"Cannot open selected trainyard.mpk");
    const auto a=Archive::Scan(f);std::size_t chunk=0,entry=0,matches=0;for(std::size_t c=0;c<a.Chunks().size();++c)if(a.Chunks()[c].type=="DECL")
        for(std::size_t e=0;e<a.Chunks()[c].entries.size();++e)if(a.Chunks()[c].entries[e].name=="maps/game/trainyard/trainyard"){chunk=c;entry=e;++matches;}
    Check(matches==1,"Selected map DECL missing/ambiguous");return a.ReadEntry(f,chunk,entry);}
const SourceResolvedLiteral& Field(const ResolvedSourceDefinition& r,const char* key){const auto* f=r.Find(key);Check(f!=NULL,"Selected field missing");return *f;}
void Original(const std::string& base){CompiledDeclarationCatalog catalog;Add(catalog,ReadGlobal(base),"global",CompiledCatalogRole::Global);Add(catalog,ReadMap(base),"map",CompiledCatalogRole::Map);
    Check(catalog.EntryCount()==6515&&catalog.Document(0)->sourceFingerprint==UINT64_C(0xd79bab8fa3531c10)&&
        catalog.Document(1)->sourceFingerprint==UINT64_C(0xc8d044282ad8a99e),"Selected original packet identities differ");
    CompiledDeclarationNamespace protectedSet(policy),ordinarySet(policy);
    for(std::size_t section=0;section<2;++section){protectedSet.ApplySection(catalog,0,section,Mode::ProtectAccepted);ordinarySet.ApplySection(catalog,0,section,Mode::Ordinary);}
    for(std::size_t section=0;section<2;++section){protectedSet.ApplySection(catalog,1,section,Mode::Ordinary);ordinarySet.ApplySection(catalog,1,section,Mode::Ordinary);}
    for(auto* n:{&protectedSet,&ordinarySet})Check(n->SelectedCount()==4890&&n->DecisionCount()==6515&&n->ApplicationCount()==4,"Selected original namespace cardinalities differ");
    std::size_t protectedSkips=0,ordinaryReplacements=0,models=0,entities=0;for(std::size_t i=0;i<protectedSet.DecisionCount();++i){
        protectedSkips+=protectedSet.Decision(i).kind==Decision::SkippedProtected;ordinaryReplacements+=ordinarySet.Decision(i).kind==Decision::AcceptedReplacement;}
    Check(protectedSkips==1621&&ordinaryReplacements==1625,"Selected original protected/model overlap decisions differ");
    SourceDefinitionLimits limits;limits.members=2000;SourceDefinitions guarded(limits,SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1),ordinary(limits,SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
    for(std::size_t i=0;i<protectedSet.SelectedCount();++i){const auto e=protectedSet.Selected(i);if(e.type=="entityDef"||e.type=="model"){
        guarded.AddCompiledRecord(e);models+=e.type=="model";entities+=e.type=="entityDef";}}
    for(const auto& e:ordinarySet.SelectedEntries())if(e.type=="entityDef"||e.type=="model")ordinary.AddCompiledRecord(e);
    Check(entities==1555&&models==48&&guarded.DefinitionCount()==1603&&ordinary.DefinitionCount()==1603,"Effective original frontend import counts differ");
    std::size_t entityClosures=0,missingClosures=0;
    for(std::size_t i=0;i<guarded.DefinitionCount();++i){const auto& definition=guarded.Definition(i);if(definition.kind==SourceDefinitionKind::EntityDef){
        const char* missingParent=definition.nameToken.text=="tesla_prong_arcs_conductance"?"tesla_prong_arcs":
            definition.nameToken.text=="trigger_tutorial"?"trigger_multi":NULL;
        if(missingParent){Check(protectedSet.Lookup("entityDef",missingParent).status==CompiledCatalogLookupStatus::Missing,
                "Expected selected-parent absence changed; requalify this dataset's closure instead of silently defaulting");
            bool refused=false;try{guarded.Resolve(SourceDefinitionKind::EntityDef,definition.nameToken.text);}
            catch(const std::runtime_error& error){refused=std::string(error.what())=="Selected source declaration/parent is missing";}
            Check(refused,"Missing original selected parent did not remain a literal resolution barrier");++missingClosures;continue;}
        ResolvedSourceDefinition result;
        try{result=guarded.Resolve(SourceDefinitionKind::EntityDef,definition.nameToken.text);}
        catch(const std::exception& error){throw std::runtime_error("Entity closure "+definition.nameToken.text+": "+error.what());}
        Check(result.selected.member.compiledRecord&&result.selected.member.compiledRecord->IsUnmodified()&&!result.gameplaySupported,
            "Effective entity dependency closure lost its selected provenance or claimed gameplay");++entityClosures;}}
    Check(entityClosures==1553&&missingClosures==2,"Selected entity closure coverage differs from the qualified input set");
    const auto a=guarded.Resolve(SourceDefinitionKind::EntityDef,"STREAMTECH.def"),b=ordinary.Resolve(SourceDefinitionKind::EntityDef,"streamtech");
    Check(Field(a,"map_heap_size").encoding==SourceLiteralEncoding::CompiledSignedInteger&&Field(a,"map_heap_size").numericWords==std::vector<std::uint32_t>({140})&&
        Field(b,"map_heap_size").encoding==SourceLiteralEncoding::CompiledString&&Field(b,"map_heap_size").value[0].text=="140","Typed global/map conflict was coerced or chose an implicit winner");
    Check(a.selected.member.compiledRecord->source->role==CompiledCatalogRole::Global&&b.selected.member.compiledRecord->source->role==CompiledCatalogRole::Map,
        "Explicit plans lost their actual winning provenance");
    const auto locker=guarded.Resolve(SourceDefinitionKind::EntityDef,"CONTAINER_LOCKER_FOOT_01.def");
    Check(Field(locker,"spawnclass").value[0].text=="rvAnimatedContainer"&&!locker.gameplaySupported&&
        Field(locker,"giveDelay").numericWords==std::vector<std::uint32_t>({1200}),"Effective container type/default data changed");
    const auto refs=guarded.ResolveModelAnimations("MODEL_ENV_LOCKER_FOOT_01_ANIM.model");Check(refs.references.size()==6&&refs.Find("open")&&
        refs.Find("open")->assetReference.text=="models/props/locker_foot_01_anim_open.md5anim"&&!refs.gameplaySupported,"Effective model animation facts changed");
    std::cout<<"WOLF2009_COMPILED_NAMESPACE_ORIGINAL_OK selected="<<protectedSet.SelectedCount()<<" decisions="<<protectedSet.DecisionCount()
        <<" entitydefs="<<entities<<" models="<<models<<" protected_skips="<<protectedSkips<<" ordinary_replacements="<<ordinaryReplacements
        <<" entity_closures="<<entityClosures<<" missing_parent_refusals="<<missingClosures<<" explicit_plans=2 typed_conflict_retained=1 retail_schedule_claim=0 native_registry_import=0 gameplay=0\n";
}
}
int main(int argc,char** argv){try{Synthetic();if(failed)return 1;for(int i=1;i<argc;++i){const std::string option=argv[i];if(option=="--self-test")continue;
    if(option=="--source-base"&&i+1<argc){Original(argv[++i]);continue;}throw std::runtime_error("Usage: [--self-test] [--source-base owner-SP/base]");}return 0;
}catch(const std::exception& e){std::cerr<<"Compiled namespace: "<<e.what()<<'\n';return 1;}}

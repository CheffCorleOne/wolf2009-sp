// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceDefinitions.h"
#include "ZipArchive.h"
#include "Archive.h"
#include "Entities.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace wolf2009;
using Bytes=std::vector<unsigned char>;
unsigned checks=0,groups=0,failed=0;
void Check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void Reject(F run,const char* reason){bool no=false;try{run();}catch(const std::exception&){no=true;}Check(no,reason);}
template<class F> void Group(const char* name,F run){++groups;try{run();}catch(const std::exception& e){++failed;std::cerr<<name<<": "<<e.what()<<'\n';}}
Bytes Data(const std::string& text){return Bytes(text.begin(),text.end());}
std::uint64_t Hash(const Bytes& bytes){std::uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=b;h*=1099511628211ull;}return h;}
SourceDefinitionArchiveIdentity Archive(){SourceDefinitionArchiveIdentity a;a.name="own-source-catalog";a.bytes=65536;a.fingerprint=123;return a;}
void Add(SourceDefinitions& definitions,const std::string& text,const char* member="def/own.def"){definitions.AddMember(Archive(),member,Data(text));}
std::string Scalar(const ResolvedSourceDefinition& result,const char* key){const auto* f=result.Find(key);Check(f&&f->value.size()==1,"Missing/non-scalar resolved literal");return f->value[0].text;}
void Synthetic(){
    Group("literal merge and provenance",[]{
        SourceDefinitions d;
        const std::string base="/* own source */ entityDef BaSe { \"Model\" \"body\" \"Flag\" \"1\" \"editor note\" \"a\\\\b\\\"c\" }";
        Add(d,base,"def/base.def");Add(d,"entityDef Child { inherit BASE Flag 2 unknown_literal \"kept\" }","def/child.def");
        const auto r=d.Resolve(SourceDefinitionKind::EntityDef,"cHiLd");
        Check(Scalar(r,"MODEL")=="body"&&Scalar(r,"flag")=="2"&&Scalar(r,"unknown_literal")=="kept","Literal child override/lookup failed");
        Check(!r.gameplaySupported&&r.declarations.size()==2&&r.inheritance.size()==1,"Resolver claimed gameplay or lost inheritance closure");
        const auto* f=r.Find("model");Check(f->origins.size()==1&&f->origins[0].origin.member.name=="def/base.def"&&
            f->origins[0].origin.member.archive.fingerprint==123&&f->origins[0].origin.member.fingerprint==Hash(Data(base)),"Archive/member provenance lost");
        Check(f->origins[0].data.key.raw=="\"Model\""&&f->origins[0].data.values[0].raw=="\"body\""&&
            f->origins[0].origin.declarationName=="BaSe"&&!f->gameplayImplemented,"Raw spellings/capability boundary lost");
        Check(Scalar(r,"editor note")=="a\\b\"c","Explicit quote/backslash token policy changed");
        Add(d,"entityDef \"Raw Child\" { inherit BASE }","def/raw-name.def");
        Check(d.Resolve(SourceDefinitionKind::EntityDef,"raw child").selected.rawDeclarationName=="\"Raw Child\"",
              "Resolved provenance discarded original quoted declaration name");
        Add(d,"entityDef whitespace { note \"line one\nline two\tvalue\" }","def/whitespace.def");
        Check(Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"whitespace"),"note")=="line one\nline two\tvalue",
              "Quoted source whitespace was discarded or interpreted");
    });
    Group("model opaque commands and other declaration kinds",[]{
        SourceDefinitions d;
        Add(d,"model Body {\n mesh models/own/body.md5r\n offset ( 0 1 2 )\n anim idle models/own/idle.md5anim\n { frame 7 trigger thing { nested 1 } }\n mystery value { inside \"preserved\" }\n}\n"
              "skin other { \"from\" \"to\" }\n table curve { { 0, 1 } }\n");
        const auto r=d.Resolve(SourceDefinitionKind::Model,"body");
        Check(Scalar(r,"mesh")=="models/own/body.md5r"&&r.Find("offset")->value.size()==5,"Known model literals lost");
        Check(r.opaqueFacts.size()==2&&!r.Find("anim")&&!r.gameplaySupported,"Opaque animation/unknown commands were interpreted");
        Check(r.opaqueFacts[0].data.rawText.find("frame 7 trigger thing { nested 1 }")!=std::string::npos,
              "Nested frame-command body was dropped or flattened into fields");
        Check(d.DefinitionCount()==3&&d.Definition(1).kind==SourceDefinitionKind::Other&&
            d.Definition(1).rawText.find("\"from\" \"to\"")!=std::string::npos,"Other named declarations disappeared");
        Reject([&]{d.Resolve(SourceDefinitionKind::Other,"other");},"Opaque kind resolved as supported data");
    });
    Group("exact MAPDEF and explicit context",[]{
        SourceDefinitions d;Add(d,"model mapdef { skin literal_parent } model context { skeleton context_skel } "
            "model upper { inherit MAPDEF mesh body } model lower { inherit mapdef mesh other } "
            "model childSkeleton { inherit MAPDEF skeleton authored_child_skel }");
        Reject([&]{d.Resolve(SourceDefinitionKind::Model,"upper");},"Literal MAPDEF inferred a context from a similarly named model");
        Check(Scalar(d.Resolve(SourceDefinitionKind::Model,"lower"),"skin")=="literal_parent","Non-placeholder parent was rewritten as MAPDEF");
        SourceDefinitionContext c;c.mapModelDefinition="context";const auto r=d.Resolve(SourceDefinitionKind::Model,"upper",c);
        Check(Scalar(r,"skeleton")=="context_skel"&&r.inheritance[0].explicitMapContext&&
            r.inheritance[0].parent.data.values[0].text=="MAPDEF"&&r.inheritance[0].resolved.declarationName=="context",
            "Explicit MAPDEF resolution/provenance failed");
        const auto child=d.Resolve(SourceDefinitionKind::Model,"childSkeleton",c);
        Check(Scalar(child,"skeleton")=="authored_child_skel"&&!child.gameplaySupported,
              "Explicit resolver policy lost child-over-context precedence or claimed retail skeleton support");
        c.mapModelDefinition="missing";Reject([&]{d.Resolve(SourceDefinitionKind::Model,"upper",c);},"Missing MAPDEF context target accepted");
        c.policy=static_cast<SourceDefinitionPolicy>(99);Reject([&]{d.Resolve(SourceDefinitionKind::Model,"lower",c);},"Unknown lookup/merge policy accepted");
    });
    Group("conflicts, cycles and missing dependencies",[]{
        SourceDefinitions d;Add(d,"model a { mesh first } model b { mesh second } "
            "model conflict { inherit a inherit b } model override { inherit a inherit b mesh own } "
            "model c { inherit d } model d { inherit c } model missing { inherit absent mesh own }");
        Reject([&]{d.Resolve(SourceDefinitionKind::Model,"conflict");},"Distinct inherited values silently chose a precedence");
        Check(Scalar(d.Resolve(SourceDefinitionKind::Model,"override"),"mesh")=="own","Explicit child override did not resolve inherited conflict");
        Reject([&]{d.Resolve(SourceDefinitionKind::Model,"c");},"Inheritance cycle accepted");
        Reject([&]{d.Resolve(SourceDefinitionKind::Model,"missing");},"Child override hid missing dependency");
        SourceDefinitions equal;Add(equal,"model a { mesh same } model b { mesh same } model child { inherit a inherit b }");
        const auto r=equal.Resolve(SourceDefinitionKind::Model,"child");
        Check(r.Find("mesh")->origins.size()==2,"Identical inherited values lost independent provenance");
        SourceDefinitions diamond;Add(diamond,"model root { mesh same } model a { inherit root } model b { inherit root } model child { inherit a inherit b }");
        const auto both=diamond.Resolve(SourceDefinitionKind::Model,"child");
        Check(both.Find("mesh")->origins.size()==1&&both.inheritance.size()==4&&both.declarations.size()==4,"Diamond traversal duplicated provenance or mistook sharing for a cycle");
    });
    Group("transactional indexing and malformed input",[]{
        SourceDefinitions d;Add(d,"entityDef known { key value }");
        const auto memberCount=d.MemberCount(),definitionCount=d.DefinitionCount();
        for(const auto* text:{"model x {", "model x { mesh \"unterminated }", "/* unterminated", "}", "model { }",
                              "model x { inherit { bad } }", "entityDef x { key }", "entityDef x { Key a key b }",
                              "entityDef KNOWN { key other }"}) {
            Reject([&]{Add(d,text,"def/rejected.def");},"Malformed/duplicate declaration admitted");
            Check(d.MemberCount()==memberCount&&d.DefinitionCount()==definitionCount&&
                Scalar(d.Resolve(SourceDefinitionKind::EntityDef,"known"),"key")=="value","Failed AddMember changed existing catalog");
        }
        Reject([&]{Add(d,"model other {}","DEF/OWN.DEF");},"Duplicate member under explicit lookup policy accepted");
        auto archive=Archive();++archive.fingerprint;
        Reject([&]{d.AddMember(archive,"def/new.def",Data("model other {}"));},"Inconsistent supplied archive identity accepted");
        Check(d.MemberCount()==1,"Rejected archive/member identity changed catalog");
    });
    Group("bounded parsing and resolution",[]{
        SourceDefinitionLimits l;l.sourceBytesPerMember=1;SourceDefinitions small(l);
        Reject([&]{Add(small,"model x {}");},"Source member byte limit ignored");
        l=SourceDefinitionLimits();l.tokenBytes=3;SourceDefinitions token(l);
        Reject([&]{Add(token,"model x {}");},"Token length limit ignored");
        l=SourceDefinitionLimits();l.nesting=1;SourceDefinitions nested(l);
        Reject([&]{Add(nested,"other x { nested {} }");},"Nested opaque data escaped depth budget");
        l=SourceDefinitionLimits();l.inheritanceDepth=1;SourceDefinitions depth(l);Add(depth,"model a {} model b { inherit a }");
        Reject([&]{depth.Resolve(SourceDefinitionKind::Model,"b");},"Inheritance depth limit ignored");
        l=SourceDefinitionLimits();l.resolutionWork=1;SourceDefinitions work(l);Add(work,"model a { mesh x }");
        Reject([&]{work.Resolve(SourceDefinitionKind::Model,"a");},"Resolution work limit ignored");
        l=SourceDefinitionLimits();l.resultBytes=1;SourceDefinitions result(l);Add(result,"model a { mesh x }");
        Reject([&]{result.Resolve(SourceDefinitionKind::Model,"a");},"Result provenance byte limit ignored");
        l=SourceDefinitionLimits();l.resultFacts=1;SourceDefinitions facts(l);Add(facts,"model a { mesh x skin y }");
        Reject([&]{facts.Resolve(SourceDefinitionKind::Model,"a");},"Result fact count limit ignored");
        l=SourceDefinitionLimits();l.tokens=4;SourceDefinitions total(l);Add(total,"model a {}");
        Reject([&]{Add(total,"model b {}","def/second.def");},"Cumulative token budget ignored");
        Check(total.MemberCount()==1,"Cumulative-budget refusal changed prior member");
        l=SourceDefinitionLimits();l.resolutionBytes=4096;SourceDefinitions memory(l);
        Add(memory,"entityDef longkey { \""+std::string(1024,'k')+"\" value }");
        Reject([&]{memory.Resolve(SourceDefinitionKind::EntityDef,"longkey");},"Repeated resolution key metadata escaped memory cap");
    });
    Group("retained long identities and index metadata budget",[]{
        SourceDefinitionLimits l;l.storedBytes=32768;SourceDefinitions d(l);
        Add(d,"model keep { mesh body }");const auto before=d.Resolve(SourceDefinitionKind::Model,"keep");
        auto archive=Archive();archive.name.assign(4096,'a');const std::string member(4096,'b');
        std::string many;for(int i=0;i<6;++i)many+="model d"+std::to_string(i)+" {}\n";
        Reject([&]{d.AddMember(archive,member,Data(many));},"Repeated long archive/member identities escaped retained storage cap");
        Check(d.MemberCount()==1&&d.DefinitionCount()==1&&Scalar(d.Resolve(SourceDefinitionKind::Model,"keep"),"mesh")=="body"&&
            d.Resolve(SourceDefinitionKind::Model,"keep").selected.member.fingerprint==before.selected.member.fingerprint,
            "Retained metadata budget rejection changed the prior catalog");
        Add(d,"model later { mesh accepted }","def/later.def");
        Check(d.MemberCount()==2&&Scalar(d.Resolve(SourceDefinitionKind::Model,"later"),"mesh")=="accepted",
              "Rejected retained metadata consumed the next valid transaction's budget");
    });
    Group("resolved output owns its provenance",[]{
        ResolvedSourceDefinition result;
        {SourceDefinitions temporary;Add(temporary,"model child { mesh \"models/own/body\"\n anim a clip { frame 1 unknown }\n }");result=temporary.Resolve(SourceDefinitionKind::Model,"child");}
        Check(Scalar(result,"mesh")=="models/own/body"&&result.Find("mesh")->origins[0].data.values[0].raw=="\"models/own/body\""&&
            result.opaqueFacts[0].data.rawText.find("frame 1 unknown")!=std::string::npos,"Resolved literals/opaque provenance borrowed catalog lifetime");
    });
    std::cout<<"WOLF2009_SOURCE_DEFINITIONS_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed="<<failed<<'\n';
}
std::string MapContext(const char* path) {
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open owner source map context");
    const auto archive=Archive::Scan(input);std::string resource;
    for(const auto& chunk:archive.Chunks())if(chunk.type=="PROC")for(const auto& entry:chunk.entries){Check(resource.empty(),"Ambiguous source-map PROC resource");resource=entry.name;}
    Check(!resource.empty(),"Missing source-map PROC resource");std::size_t matches=0;Bytes bytes;
    for(std::size_t c=0;c<archive.Chunks().size();++c)if(archive.Chunks()[c].type=="ENTS")
        for(std::size_t e=0;e<archive.Chunks()[c].entries.size();++e)if(archive.Chunks()[c].entries[e].name==resource){
            Check(archive.Chunks()[c].entries[e].size<=64u<<20,"Source-map ENTS budget");bytes=archive.ReadEntry(input,c,e);++matches;}
    Check(matches==1,"Same-resource source-map ENTS missing/ambiguous");
    const auto entities=DecodeEntities4(bytes);Check(!entities.entities.empty(),"Missing source worldspawn");
    const auto* classname=entities.entities[0].Find("classname");const auto* field=entities.entities[0].Find("mapSpecificDef");
    Check(classname&&classname->kind==EntityProperty::Text&&classname->text=="worldspawn"&&
        field&&field->kind==EntityProperty::Text&&field->sourceType==0&&!field->text.empty(),"Missing literal worldspawn mapSpecificDef context");
    std::cout<<"SOURCE_MAP_CONTEXT map="<<resource<<" entity=0 key="<<field->key<<" sourceType="<<field->sourceType
        <<" value="<<field->text<<" ents_bytes="<<bytes.size()<<" ents_fingerprint="<<std::hex<<Hash(bytes)<<std::dec<<'\n';
    return field->text; // explicit caller input; resolver never opens/invents a map context
}
void Original(const char* path,const char* mapPath){
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open owner source catalog");
    input.seekg(0,std::ios::end);const auto size=input.tellg();Check(size>0&&size<=64*1024*1024,"Owner source catalog byte budget");input.seekg(0);
    SourceDefinitionArchiveIdentity archive;archive.name="owner-selected-assets";archive.bytes=static_cast<std::size_t>(size);archive.fingerprint=14695981039346656037ull;
    char buffer[65536];while(input){input.read(buffer,sizeof(buffer));for(std::streamsize i=0;i<input.gcount();++i){archive.fingerprint^=static_cast<unsigned char>(buffer[i]);archive.fingerprint*=1099511628211ull;}}
    input.clear();input.seekg(0);const auto zip=ZipArchive::Scan(input);SourceDefinitions definitions;
    const char* members[]={"def/maps/trainyard.def","def/ai/allies/faction_kreisau.def","def/ai/allies/_allies_base.def","def/ai/actor.def",
        "def/ai/civilians/civ_male_04.def","def/ai/civilians/_civ_male_base.def","def/_humanoid_male.def","skins/allies.skin"};
    for(const auto* name:members){std::size_t at=0,matches=0;for(std::size_t n=0;n<zip.Entries().size();++n)if(zip.Entries()[n].name==name){at=n;++matches;}
        Check(matches==1,"Selected exact source ZIP member missing/ambiguous");const auto bytes=zip.ReadEntry(input,at);definitions.AddMember(archive,name,bytes);
        std::cout<<"SOURCE_MEMBER name="<<name<<" bytes="<<bytes.size()<<" crc="<<zip.Entries()[at].crc<<" fingerprint="<<std::hex<<Hash(bytes)<<std::dec<<'\n';}
    const auto entity=definitions.Resolve(SourceDefinitionKind::EntityDef,"trainyard_greeter");
    Check(Scalar(entity,"model")=="model_civilian_male_04"&&Scalar(entity,"skin")=="resist_fighter_B"&&
        Scalar(entity,"detailPack")=="streampacks/allies_kreisau_agt02.spk","Selected real entity inheritance changed literal model/skin/detail-pack");
    Check(entity.Find("model")->origins[0].origin.member.name=="def/ai/allies/faction_kreisau.def","Real selected model lost member provenance");
    Reject([&]{definitions.Resolve(SourceDefinitionKind::Model,Scalar(entity,"model"));},"Real MAPDEF inferred a map context");
    SourceDefinitionContext context;context.mapModelDefinition=mapPath?MapContext(mapPath):"model_trainyard_base";
    Check(context.mapModelDefinition=="model_trainyard_base","Selected source proof requires the explicit trainyard model context");
    const auto model=definitions.Resolve(SourceDefinitionKind::Model,Scalar(entity,"model"),context);
    Check(Scalar(model,"mesh")=="models/characters/civilians/civ_male_04/anims/body_cm04.md5r"&&
        model.Find("mesh")->origins[0].origin.member.name=="def/ai/civilians/civ_male_04.def","Real body mesh literal/provenance changed");
    bool mapEdge=false;for(const auto& edge:model.inheritance)mapEdge=mapEdge||(edge.explicitMapContext&&edge.parent.data.values[0].text=="MAPDEF"&&edge.resolved.declarationName=="model_trainyard_base");
    Check(mapEdge&&!model.opaqueFacts.empty()&&!entity.gameplaySupported&&!model.gameplaySupported,"Real model lost explicit context/opaque commands or claimed runtime support");
    Check(Scalar(model,"skeleton")=="models/characters/_humanoid_male/empty_male_skel.hkx"&&
        model.Find("skeleton")->origins[0].origin.member.name=="def/_humanoid_male.def",
        "Literal child-over-context skeleton policy changed; retail MAPDEF semantics remain unqualified");
    std::cout<<"WOLF2009_SOURCE_DEFINITIONS_ORIGINAL_OK members="<<definitions.MemberCount()<<" declarations="<<definitions.DefinitionCount()
        <<" entity_parents="<<entity.declarations.size()<<" model_parents="<<model.declarations.size()<<" opaque_model_facts="<<model.opaqueFacts.size()
        <<" model="<<Scalar(entity,"model")<<" skin="<<Scalar(entity,"skin")<<" mesh="<<Scalar(model,"mesh")
        <<" literal_skeleton="<<Scalar(model,"skeleton")<<" retail_MAPDEF_skeleton=unqualified gameplaySupported=0 archive_fingerprint="<<std::hex<<archive.fingerprint<<std::dec<<'\n';
}
}
int main(int argc,char** argv){try{Synthetic();if(failed)return 1;const char* catalog=NULL;const char* map=NULL;
    for(int i=1;i<argc;++i){const std::string option(argv[i]);if(option=="--self-test")continue;
        Check(i+1<argc,"Missing source option value");
        if(option=="--source-catalog"){Check(!catalog,"Duplicate source catalog option");catalog=argv[++i];}
        else if(option=="--source-map"){Check(!map,"Duplicate source map option");map=argv[++i];}
        else throw std::runtime_error("Usage: [--self-test] [--source-catalog owner-assets.pk4 [--source-map owner-map.mpk]]");}
    Check(!map||catalog,"Source map context requires a selected source catalog");if(catalog)Original(catalog,map);return 0;
}catch(const std::exception& e){std::cerr<<"Source definitions: "<<e.what()<<'\n';return 1;}}

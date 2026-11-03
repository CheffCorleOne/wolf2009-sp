// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceEntityRuntime.h"
#include "CompiledDeclarationCatalog.h"
#include "Archive.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace wolf2009;
typedef std::vector<unsigned char> Bytes;
unsigned checks=0;
void Check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class F>void Reject(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,why);}
void Word(Bytes& b,std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(static_cast<unsigned char>(v>>(8*i)));}
void Text(Bytes& b,const std::string& t){b.insert(b.end(),t.begin(),t.end());}
std::uint32_t Bits(float v){std::uint32_t word;std::memcpy(&word,&v,4);return word;}
EntityProperty TextValue(const char* key,const char* value){EntityProperty p;p.key=key;p.text=value;return p;}
EntityProperty IntValue(const char* key,std::int32_t value){EntityProperty p;p.key=key;p.kind=EntityProperty::Integer;p.integer=value;return p;}
EntityProperty Numbers(const char* key,const std::vector<float>& values){EntityProperty p;p.key=key;p.kind=EntityProperty::Floats;p.numbers=values;return p;}
Bytes Entities(const std::vector<EntityRecord>& records){
    std::vector<std::string> keys,values;std::map<std::string,std::size_t> keyIndex,valueIndex;std::vector<std::uint32_t> keyRefs,valueRefs,floatWords;
    Bytes body;for(const auto& r:records){Word(body,std::uint32_t(r.properties.size()));for(const auto& p:r.properties){
        auto k=keyIndex.find(p.key);if(k==keyIndex.end()){k=keyIndex.emplace(p.key,keys.size()).first;keys.push_back(p.key);keyRefs.push_back(0);}++keyRefs[k->second];Word(body,std::uint32_t(k->second));
        if(p.kind==EntityProperty::Text){auto v=valueIndex.find(p.text);if(v==valueIndex.end()){v=valueIndex.emplace(p.text,values.size()).first;values.push_back(p.text);valueRefs.push_back(0);}++valueRefs[v->second];Word(body,0);Word(body,1);Word(body,std::uint32_t(v->second));}
        else if(p.kind==EntityProperty::Integer){Word(body,4);Word(body,1);Word(body,std::uint32_t(p.integer));}
        else{Word(body,1);Word(body,std::uint32_t(p.numbers.size()));Word(body,std::uint32_t(floatWords.size()));for(auto v:p.numbers)floatWords.push_back(Bits(v));}
    }}
    Bytes keyBytes,valueBytes;for(const auto& k:keys){Text(keyBytes,k);keyBytes.push_back(0);}for(const auto& v:values){Text(valueBytes,v);valueBytes.push_back(0);}
    Bytes out;Text(out,"Version 4\nbinary\nentities "+std::to_string(records.size())+" "+std::to_string(body.size())+"\nkeys "+std::to_string(keys.size())+" "+std::to_string(keyBytes.size())+
        "\nvalues "+std::to_string(values.size())+" "+std::to_string(valueBytes.size())+"\nintValues 0\nfloatValues "+std::to_string(floatWords.size())+"\n");
    for(auto word:floatWords)Word(out,word);out.insert(out.end(),keyBytes.begin(),keyBytes.end());while(out.size()%4)out.push_back(0);for(auto ref:keyRefs)Word(out,ref);
    out.insert(out.end(),valueBytes.begin(),valueBytes.end());while(out.size()%4)out.push_back(0);for(auto ref:valueRefs)Word(out,ref);out.insert(out.end(),body.begin(),body.end());out.push_back('\n');return out;
}
void Definitions(SourceDefinitions& definitions,const std::vector<std::string>& records){
    Bytes data;Word(data,0);Word(data,0);Text(data,"1\nown/class.def\n3\n"+std::to_string(records.size())+"\n");unsigned id=0;
    for(const auto& record:records){Text(data,std::to_string(id++)+"\n"+std::to_string(record.size())+"\n");Text(data,record);}Text(data,"0\n");
    const auto document=CompiledDeclarations::Read(data,CompiledDeclFormat::TwoSectionLfTextV1);CompiledCatalogSource source;source.loadIdentity="own-classes";source.path="own/classes";source.role=CompiledCatalogRole::Explicit;
    CompiledDeclarationCatalog catalog;catalog.AddDocument(document,source);for(std::size_t i=0;i<catalog.EntryCount();++i)definitions.AddCompiledRecord(catalog.Entry(i));
}
const char* Path="wolf2009/selected/ENTS/maps/own/map";
EntityRecord Instance(){EntityRecord e;e.properties={TextValue("classname","own_container"),TextValue("name","own_source_container"),Numbers("origin",{12,24,36}),Numbers("rotation",{0,1,0,-1,0,0,0,0,1})};return e;}
std::shared_ptr<const SourceEntityPlan> Prepare(const Bytes& bytes,const SourceDefinitions& definitions){return PrepareSourceEntity(bytes,Path,0,definitions,SourceEntityPolicy::AnimatedContainerV1);}
void Synthetic(){
    SourceDefinitions definitions;Definitions(definitions,{"entityDef own_container { spawnclass 0 0 rvAnimatedContainer model 0 0 own_model usable 4 1 1 persist 4 1 1 giveDelay 4 1 1200 unknown_active 9 1 17 }"});
    auto e=Instance();e.properties.push_back(IntValue("usable",0));const auto bytes=Entities({e});auto plan=Prepare(bytes,definitions);
    Check(plan->Classname()=="own_container"&&plan->Name()=="own_source_container"&&plan->Spawnclass()=="rvAnimatedContainer"&&plan->ModelDefinition()=="own_model","Typed source class selection differs");
    std::int32_t integer=-1;const auto usable=plan->Find("Usable");Check(usable.Integer(integer)&&integer==0&&usable.instance&&usable.declaration&&usable.declaration->numericWords[0]==1,"Instance override lost default facts or effective typed integer");
    std::string text="untouched";Check(!usable.Text(text)&&text=="untouched","Typed integer was converted to text");
    const auto unknown=plan->Find("unknown_active");Check(unknown.declaration&&unknown.declaration->encoding==SourceLiteralEncoding::CompiledOpaque,"Unimplemented source effect was silently discarded");
    std::vector<float> numbers;Check(plan->Find("origin").Floats(numbers)&&numbers==std::vector<float>({12,24,36}),"Source placement words changed");
    Check(plan->Identity().resourcePath==Path&&plan->Identity().sourceBytes==bytes.size()&&plan->Identity().sourceFingerprint&&plan->Identity().entityIndex==0,"ENTS byte/selection identity lost");
    auto replacement=e;replacement.properties.push_back(TextValue("model","instance_model"));auto overridden=Prepare(Entities({replacement}),definitions);Check(overridden->ModelDefinition()=="instance_model"&&overridden->Find("model").declaration->value[0].text=="own_model","Model instance override lost declaration provenance");
    auto invalid=e;invalid.properties.push_back(IntValue("model",7));Reject([&]{Prepare(Entities({invalid}),definitions);},"Wrong typed override exposed the parent's model");
    SourceDefinitions unknownClass;Definitions(unknownClass,{"entityDef own_container { spawnclass 0 0 rvUnknown model 0 0 own_model }"});Reject([&]{Prepare(bytes,unknownClass);},"Unknown class became a generic source visual");
    Reject([&]{PrepareSourceEntity(bytes,Path,1,definitions,SourceEntityPolicy::AnimatedContainerV1);},"Out-of-range entity selected");
    Reject([&]{PrepareSourceEntity(bytes,Path,0,definitions,SourceEntityPolicy::Unspecified);},"Unspecified source entity policy admitted");
    for(const auto* path:{"wolf2009/a/ENTS/../map","wolf2009/a/MODL/maps/own/map","wolf2009//ENTS/maps/own/map","wolf2009/a/ENTS/","/wolf2009/a/ENTS/map"})Reject([&]{PrepareSourceEntity(bytes,path,0,definitions,SourceEntityPolicy::AnimatedContainerV1);},"Invalid source path admitted");
    auto truncated=bytes;truncated.resize(bytes.size()-5);Reject([&]{Prepare(truncated,definitions);},"Truncated ENTS source admitted");
    SourceEntityLimits low;low.effectiveFields=2;Reject([&]{PrepareSourceEntity(bytes,Path,0,definitions,SourceEntityPolicy::AnimatedContainerV1,SourceDefinitionContext(),low);},"Effective source field budget bypassed");
    std::shared_ptr<const SourceEntityPlan> owned;{SourceDefinitions transient;Definitions(transient,{"entityDef own_container { spawnclass 0 0 rvAnimatedContainer model 0 0 own_model }"});owned=Prepare(Entities({Instance()}),transient);}
    Check(owned->Defaults().Find("model")->origins[0].origin.member.compiledRecord&&owned->Instance().Find("name")->text=="own_source_container","Source facts died with transient frontend/input");
    const auto verifyPlacement=[&](const std::vector<float>& matrix){auto entity=Instance();entity.properties.back()=Numbers("rotation",matrix);auto p=Prepare(Entities({entity}),definitions);
        const auto placement=PrepareSourceEntityPlacement(*p,SourceEntityPlacementPolicy::NativeAffinePlacementV1);Check(std::equal(matrix.begin(),matrix.end(),placement.authoredAxis.begin()),"Placement normalized away authored matrix values");
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){double dot=0,composed=0;for(unsigned k=0;k<3;++k){dot+=double(placement.physicsAxis[3*i+k])*placement.physicsAxis[3*j+k];composed+=double(placement.visualToPhysics[3*i+k])*placement.physicsAxis[3*k+j];}
            Check(std::abs(dot-(i==j?1:0))<1e-6&&std::abs(composed-matrix[3*i+j])<2e-6,"Rigid physics/affine visual reconstruction differs from authored matrix");}
        return placement;};
    Check(!verifyPlacement({0,1,0,-1,0,0,0,0,1}).authoredNonUnitAxes,"Rigid placement acquired scale");
    const auto uniform=verifyPlacement({0,2,0,-2,0,0,0,0,2});Check(uniform.authoredNonUnitAxes&&uniform.authoredRowLengths[0]==2&&uniform.authoredRowLengths[2]==2,"Uniform authored scale lost");
    const auto affine=verifyPlacement({0.002807f,-0.997242f,0,0.997242f,0.002807f,0,0,0,1});
    Check(affine.authoredNonUnitAxes&&std::abs(affine.authoredRowLengths[0]-0.9972459243891351)<1e-12&&affine.authoredRowLengths[2]==1,"Nonuniform authored XY scale was treated as float rounding");
    verifyPlacement({1.0000001f,0,0,0.0000001f,1,0,0,0,1});
    const auto badPlacement=[&](const std::vector<float>& matrix){auto entity=Instance();entity.properties.back()=Numbers("rotation",matrix);const auto p=Prepare(Entities({entity}),definitions);Reject([&]{PrepareSourceEntityPlacement(*p,SourceEntityPlacementPolicy::NativeAffinePlacementV1);},"Unsupported affine placement was silently normalized");};
    badPlacement({1,0,0,.1f,1,0,0,0,1});badPlacement({1,0,0,0,1,0,0,0,-1});badPlacement({0,0,0,0,1,0,0,0,1});
    badPlacement({.000001f,0,0,0,1,0,0,0,1});badPlacement({100000,0,0,0,1,0,0,0,1});
    Reject([&]{PrepareSourceEntityPlacement(*plan,SourceEntityPlacementPolicy::Unspecified);},"Unspecified affine profile accepted");
    auto angle=Instance();angle.properties.back()=Numbers("angle",{90});auto anglePlan=Prepare(Entities({angle}),definitions);Reject([&]{PrepareSourceEntityPlacement(*anglePlan,SourceEntityPlacementPolicy::NativeAffinePlacementV1);},"Nonzero angle-only placement gained an inferred convention");
    auto nearEntity=Instance();nearEntity.properties.back()=Numbers("rotation",{0.002807f,-0.997242f,0,0.997242f,0.002807f,0,0,0,1});const auto nearPlan=Prepare(Entities({nearEntity}),definitions);
    const auto near=PrepareSourceEntityPlacement(*nearPlan,SourceEntityPlacementPolicy::NativeNearUnitPlacementV1);
    Check(near.placementApproximated&&near.maximumScaleError<0.005&&std::abs(near.matrixDifferenceNorm-affine.matrixDifferenceNorm)<1e-12,"Native near-unit profile omitted its measured approximation");
    const double point[]={11,-23,47};double squaredDelta=0,squaredRadius=0;for(unsigned i=0;i<3;++i){squaredRadius+=point[i]*point[i];double delta=0;for(unsigned j=0;j<3;++j)delta+=point[j]*(near.authoredAxis[3*j+i]-near.physicsAxis[3*j+i]);squaredDelta+=delta*delta;}
    Check(std::sqrt(squaredDelta)<=std::sqrt(squaredRadius)*near.matrixDifferenceNorm,"Reported near-unit displacement bound is not conservative");
    for(const auto scale:{1.006f,0.994f,2.0f}){auto outOfProfile=Instance();outOfProfile.properties.back()=Numbers("rotation",{scale,0,0,0,1,0,0,0,1});const auto p=Prepare(Entities({outOfProfile}),definitions);
        Reject([&]{PrepareSourceEntityPlacement(*p,SourceEntityPlacementPolicy::NativeNearUnitPlacementV1);},"Native near-unit half-percent scale limit was weakened");}
    std::cout<<"WOLF2009_SOURCE_ENTITY_SELECTION_SYNTHETIC checks="<<checks<<" native_spawn_claimed=0\n";
}
Bytes Read(const Archive& archive,std::ifstream& file,const char* type,const char* name){std::size_t chunk=0,entry=0,count=0;
    for(std::size_t c=0;c<archive.Chunks().size();++c)if(archive.Chunks()[c].type==type)for(std::size_t e=0;e<archive.Chunks()[c].entries.size();++e)if(archive.Chunks()[c].entries[e].name==name){chunk=c;entry=e;++count;}
    Check(count==1,"Selected exact source resource missing/ambiguous");return archive.ReadEntry(file,chunk,entry);
}
void Original(const char* path){
    std::ifstream file(path,std::ios::binary);Check(bool(file),"Cannot read selected original archive");const auto archive=Archive::Scan(file);
    const auto ents=Read(archive,file,"ENTS","maps/game/trainyard/trainyard"),decl=Read(archive,file,"DECL","maps/game/trainyard/trainyard");
    const auto document=CompiledDeclarations::Read(decl,CompiledDeclFormat::TwoSectionLfTextV1);CompiledCatalogSource source;source.loadIdentity="selected-trainyard";source.path="owner/DECL/maps/game/trainyard/trainyard";source.role=CompiledCatalogRole::Map;
    CompiledDeclarationCatalog catalog;catalog.AddDocument(document,source);SourceDefinitionLimits limits;limits.members=2048;SourceDefinitions definitions(limits,SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
    for(std::size_t i=0;i<catalog.EntryCount();++i)if(catalog.Entry(i).type=="entityDef"||catalog.Entry(i).type=="model")definitions.AddCompiledRecord(catalog.Entry(i));
    const auto entities=DecodeEntities4(ents);std::size_t found=0;for(std::size_t i=0;i<entities.entities.size();++i){const auto* kind=entities.entities[i].Find("classname");if(!kind||kind->text!="container_locker_foot_01")continue;
        const auto plan=PrepareSourceEntity(ents,"wolf2009/selected/ENTS/maps/game/trainyard/trainyard",i,definitions,SourceEntityPolicy::AnimatedContainerV1);
        Check(plan->Spawnclass()=="rvAnimatedContainer"&&plan->ModelDefinition()=="model_env_locker_foot_01_anim","Actual class was conflated with env Searchable");
        std::int32_t value=-1;Check(plan->Find("giveDelay").Integer(value)&&value==1200&&plan->Find("persist").Integer(value)&&value==1,"Actual active timer/persist source facts lost");
        std::string ragdoll;Check(plan->Find("ragdoll").Text(ragdoll)&&ragdoll=="locker_foot_01_anim"&&plan->Find("def_afcore").Text(ragdoll)&&ragdoll=="afcore_footLocker","Actual deferred AF facts lost");
        Check(plan->Find("origin").instance&&plan->Find("rotation").instance,"Actual source placement facts missing");
        std::cout<<"WOLF2009_SOURCE_ENTITY_SELECTED index="<<i<<" name="<<plan->Name()<<" fields="<<plan->Keys().size()<<" native_spawn_claimed=0\n";
        if(!found){std::cout<<"WOLF2009_SOURCE_ENTITY_FIELD_KEYS";for(const auto& key:plan->Keys())std::cout<<' '<<key;std::cout<<'\n';}
        if(found<2){std::vector<float> matrix;Check(plan->Find("rotation").Floats(matrix)&&matrix.size()==9,"Actual source rotation words unavailable");
            const auto placement=PrepareSourceEntityPlacement(*plan,SourceEntityPlacementPolicy::NativeAffinePlacementV1);
            Check(placement.authoredNonUnitAxes==(i==921),"Actual source affine/rigid placement classification differs");
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){double value=0;for(unsigned k=0;k<3;++k)value+=double(placement.visualToPhysics[3*row+k])*placement.physicsAxis[3*k+col];Check(std::abs(value-matrix[3*row+col])<2e-6,"Actual source matrix did not survive native affine decomposition");}
            std::cout.precision(12);std::cout<<"WOLF2009_SOURCE_ENTITY_ROTATION index="<<i;for(const auto value:matrix)std::cout<<' '<<value;
            const auto native=PrepareSourceEntityPlacement(*plan,SourceEntityPlacementPolicy::NativeNearUnitPlacementV1);Check(native.placementApproximated==(i==921)&&native.maximumScaleError<=0.005,"Actual source near-unit profile silently lost its approximation");
            std::cout<<" row_lengths="<<placement.authoredRowLengths[0]<<','<<placement.authoredRowLengths[1]<<','<<placement.authoredRowLengths[2]<<" authoredNonUnitAxes="<<placement.authoredNonUnitAxes<<" native_matrix_error="<<native.matrixDifferenceNorm<<'\n';}++found;
    }Check(found==5,"Actual selected container count differs");std::cout<<"WOLF2009_SOURCE_ENTITY_SELECTION_ORIGINAL containers="<<found<<" owning_facts=1 native_spawn_claimed=0\n";
}
}
int main(int argc,char** argv){try{Synthetic();for(int i=1;i<argc;++i){const std::string arg(argv[i]);if(arg=="--self-test")continue;if(arg=="--source-map"&&i+1<argc){Original(argv[++i]);continue;}throw std::runtime_error("Usage: --self-test [--source-map selected-trainyard.mpk]");}return 0;
}catch(const std::exception& error){std::cerr<<"Source entity runtime: "<<error.what()<<'\n';return 1;}}

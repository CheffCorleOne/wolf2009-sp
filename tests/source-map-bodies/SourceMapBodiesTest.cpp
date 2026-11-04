// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceMapBodies.h"
#include "Archive.h"
#include "HavokTestFixture.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace wolf2009;
using namespace wolf2009_test;
unsigned checks=0,refusals=0;
void Check(bool value,const char* why){++checks;Need(value,why);}
void Refuse(const std::function<void()>& operation){bool caught=false;
    try{operation();}catch(const std::runtime_error&){caught=true;}++refusals;Check(caught,"Expected body-plan refusal");}
void AppendU(Bytes& bytes,std::uint32_t value){const auto at=bytes.size();bytes.resize(at+4);U(bytes,at,value);}
void Align4(Bytes& bytes){while(bytes.size()%4)bytes.push_back(0);}
Bytes NamesFile(const std::vector<std::string>& names){
    std::size_t stringBytes=0;for(const auto& name:names)stringBytes+=name.size()+1;
    const std::string header="Version 4\nbinary\nentities "+std::to_string(names.size())+" "+std::to_string(names.size()*20)+
        "\nkeys 1 5\nvalues "+std::to_string(names.size())+" "+std::to_string(stringBytes)+"\nintValues 0\nfloatValues 0\n";
    Bytes out(header.begin(),header.end());const char key[]="name";out.insert(out.end(),key,key+5);Align4(out);AppendU(out,std::uint32_t(names.size()));
    for(const auto& name:names){out.insert(out.end(),name.begin(),name.end());out.push_back(0);}Align4(out);
    for(std::size_t i=0;i<names.size();++i)AppendU(out,1);
    for(std::size_t i=0;i<names.size();++i){AppendU(out,1);AppendU(out,0);AppendU(out,0);AppendU(out,1);AppendU(out,std::uint32_t(i));}
    out.push_back('\n');return out;
}
HavokFixture Pack(){
    HavokFixture f(16384);f.types[8192]="hkpBoxShape";
    const std::string names[]={"maps/game/authored","platform","models/resource","gfx/effect","Mystery","","","CaseName","models/entity_named"};
    for(unsigned i=0;i<9;++i){const std::uint32_t at=64+i*512;f.types[at]="hkpRigidBody";f.links[at+16]=8192;
        U(f.data,at+44,i==1?0:std::uint32_t(0x12345600+i));
        if(i!=5){f.links[at+112]=9000+i*128;Text(f.data,9000+i*128,names[i]);}
        for(unsigned row=0;row<3;++row)F(f.data,at+224+row*16+row*4,1);
        F(f.data,at+272,float(i)*.0254f);F(f.data,at+284,1);F(f.data,at+332,1);F(f.data,at+348,1);
    }return f;
}
void Synthetic(){
    auto bytes=Pack().File();auto ents=NamesFile({"world_named","platform","casename","models/entity_named"});
    auto plan=DecodeSourceMapBodies(bytes,ents,"maps/game/authored");
    Check(plan.inventory.bodies.size()==9&&plan.entities.entities.size()==4,"Owning decoded counts");
    Check(plan.selectedWorldBody==0&&plan.bindings[0].role==SourceMapBodyRole::SelectedWorld,"Explicit selected world");
    Check(plan.entityBodies==2&&plan.resourceNameBodies==2&&plan.otherUnboundBodies==4,"World/entity/resource/unknown separation");
    Check(plan.bindings[1].role==SourceMapBodyRole::ExactEntityName&&plan.bindings[1].entityIndex==1&&plan.entityBodyIndices[1]==1,"Exact entity instance mapping");
    Check(plan.bindings[8].role==SourceMapBodyRole::ExactEntityName&&plan.bindings[8].entityIndex==3,"Entity exact match precedes resource prefix");
    Check(plan.bindings[7].role==SourceMapBodyRole::OtherUnbound&&plan.entityBodyIndices[2]==std::size_t(-1),"Case mismatch is never guessed");
    Check(plan.bindings[2].role==SourceMapBodyRole::ResourceNameUnbound&&plan.bindings[3].role==SourceMapBodyRole::ResourceNameUnbound,"Resource names stay uninstantiated");
    Check(plan.inventory.bodies[1].rawFilterInfo==0,"Raw zero filter is retained");
    Check(!plan.inventory.bodies[5].hasName&&plan.inventory.bodies[6].hasName&&plan.inventory.bodies[6].rawName.empty(),"Absent and explicitly empty names differ");
    Check(plan.inventory.bodies[1].rootShapeOffset==plan.inventory.bodies[2].rootShapeOffset,"Shared shapes preserve distinct body ownership");
    Check(plan.inventory.bodies[1].rawName=="platform"&&plan.inventory.bodies[4].rawName=="Mystery","Raw name bytes preserved");
    Check(plan.inventory.bodies[1].sourceBodyOffset==576&&plan.inventory.bodies[1].rootShapeType=="hkpBoxShape"&&plan.inventory.bodies[1].motionStateFinite,"Checked body metadata");
    const auto beforePose=plan.inventory.bodies[1].rawMotionStateWords;std::uint32_t bits=0;const float position=.0254f;std::memcpy(&bits,&position,4);
    Check(beforePose[12]==bits,"Body translation binary32 words preserved without ENTS reinterpretation");
    const auto source=bytes,entitySource=ents;bytes.clear();ents.clear();
    Check(plan.inventory.source==source&&plan.entsSource==entitySource&&plan.inventory.sourceBytes==source.size()&&plan.entsBytes==entitySource.size(),"Sources owned after caller buffers expire");
    Check(plan.inventory.sourceFingerprint&&plan.entsFingerprint,"Both source identities retained");
    auto inv=plan.inventory;auto doc=plan.entities;
    Refuse([&]{BindSourceMapBodies(inv,doc,"MAPS/game/authored");});
    Refuse([&]{BindSourceMapBodies(inv,doc,"");});
    auto duplicateBody=inv;duplicateBody.bodies[4].rawName="platform";
    Refuse([&]{BindSourceMapBodies(duplicateBody,doc,"maps/game/authored");});
    auto duplicateEntity=doc;duplicateEntity.entities.push_back(doc.entities[1]);
    Refuse([&]{BindSourceMapBodies(inv,duplicateEntity,"maps/game/authored");});
    auto duplicateWorld=inv;duplicateWorld.bodies[4].rawName="maps/game/authored";
    Refuse([&]{BindSourceMapBodies(duplicateWorld,doc,"maps/game/authored");});
    auto duplicateOffset=inv;duplicateOffset.bodies[4].sourceBodyOffset=duplicateOffset.bodies[3].sourceBodyOffset;
    Refuse([&]{BindSourceMapBodies(duplicateOffset,doc,"maps/game/authored");});
    auto duplicateKey=doc;auto property=duplicateKey.entities[1].properties[0];property.key="NAME";duplicateKey.entities[1].properties.push_back(property);
    Refuse([&]{BindSourceMapBodies(inv,duplicateKey,"maps/game/authored");});
    auto numericName=doc;numericName.entities[1].properties[0].kind=EntityProperty::Integer;
    Refuse([&]{BindSourceMapBodies(inv,numericName,"maps/game/authored");});
    auto badIdentity=inv;badIdentity.sourceFingerprint^=1;
    Refuse([&]{BindSourceMapBodies(badIdentity,doc,"maps/game/authored");});
    auto unknownDuplicates=inv;unknownDuplicates.bodies[4].rawName="models/resource";
    auto unknownDoc=doc;unknownDoc.entities.push_back(doc.entities[0]);
    const auto unresolved=BindSourceMapBodies(unknownDuplicates,unknownDoc,"maps/game/authored");
    Check(unresolved.ambiguousUnboundBodyNames==std::vector<std::string>{"models/resource"}&&
        unresolved.ambiguousUnboundEntityNames==std::vector<std::string>{"world_named"},"Unrelated name ambiguity stays explicit");
    auto nameControl=inv;nameControl.bodies[4].rawName="models\\resource";nameControl.bodies[7].rawName="MODELS/UPPER";
    const auto noRewrite=BindSourceMapBodies(nameControl,doc,"maps/game/authored");
    Check(noRewrite.bindings[4].role==SourceMapBodyRole::OtherUnbound&&noRewrite.bindings[7].role==SourceMapBodyRole::OtherUnbound,"No slash/case normalization or role invention");
    auto nonAscii=doc;nonAscii.entities[0].properties[0].text="opaque_\xc3\xa9";
    Check(BindSourceMapBodies(inv,nonAscii,"maps/game/authored").entities.entities[0].properties[0].text=="opaque_\xc3\xa9","Unmatched entity name bytes remain owned");
    auto noShape=Pack();noShape.links.erase(64+16);Refuse([&]{ReadSourceMapBodyInventory(noShape.File());});
    auto nonfinite=Pack();U(nonfinite.data,64+224,0x7fc00000u);
    const auto unknownPose=ReadSourceMapBodyInventory(nonfinite.File());
    Check(!unknownPose.bodies[0].motionStateFinite&&unknownPose.bodies[0].rawMotionStateWords[0]==0x7fc00000u,"Nonfinite pose remains explicitly unqualified/raw");
    SourceMapBodiesLimits limits;limits.bodies=1;Refuse([&]{ReadSourceMapBodyInventory(source,limits);});
    limits=SourceMapBodiesLimits();limits.hkxsBytes=208;Refuse([&]{ReadSourceMapBodyInventory(source,limits);});
    limits=SourceMapBodiesLimits();limits.nameBytes=1;Refuse([&]{BindSourceMapBodies(inv,doc,"maps/game/authored",limits);});
    limits=SourceMapBodiesLimits();limits.entities.entities=1;Refuse([&]{BindSourceMapBodies(inv,doc,"maps/game/authored",limits);});
    limits=SourceMapBodiesLimits();limits.entities.properties=1;Refuse([&]{BindSourceMapBodies(inv,doc,"maps/game/authored",limits);});
    Check(plan.inventory.bodies.size()==9&&plan.bindings[1].entityIndex==1&&plan.inventory.bodies[1].rawMotionStateWords==beforePose,"Rejected plans never mutate earlier owning result");
    std::cout<<"SOURCE_MAP_BODIES_SYNTHETIC_OK checks="<<checks<<" refusals="<<refusals<<'\n';
}
void Original(const std::string& path){
    std::ifstream input(path.c_str(),std::ios::binary);Need(bool(input),"Cannot read owner-supplied MPK");
    const auto archive=Archive::Scan(input);Bytes hkxs,ents;std::string world;std::size_t hkxsCount=0,entsCount=0;
    for(std::size_t c=0;c<archive.Chunks().size();++c){const auto& chunk=archive.Chunks()[c];
        for(std::size_t e=0;e<chunk.entries.size();++e)if(chunk.type=="HKXS"){++hkxsCount;hkxs=archive.ReadEntry(input,c,e);world=chunk.entries[e].name;}
        else if(chunk.type=="ENTS"){++entsCount;ents=archive.ReadEntry(input,c,e);}}
    Check(hkxsCount==1&&entsCount==1,"Expected one exact map HKXS/ENTS resource");
    const auto plan=DecodeSourceMapBodies(hkxs,ents,world);
    Check(plan.inventory.bodies[plan.selectedWorldBody].rawName==world,"Exact original world selected");
    Check(plan.inventory.bodies.size()==1+plan.entityBodies+plan.resourceNameBodies+plan.otherUnboundBodies,"Every original body classified once");
    for(std::size_t i=0;i<plan.bindings.size();++i)if(plan.bindings[i].role==SourceMapBodyRole::ExactEntityName){
        const auto& body=plan.inventory.bodies[i];const auto entity=plan.bindings[i].entityIndex;
        Check(entity<plan.entities.entities.size()&&plan.entities.entities[entity].Find("name")&&
            plan.entities.entities[entity].Find("name")->text==body.rawName&&plan.entityBodyIndices[entity]==i,"Original exact-name reciprocal body binding");}
    if(world=="maps/game/trainyard/trainyard"){
        Check(plan.inventory.bodies.size()==535&&plan.entities.entities.size()==2238,"Original trainyard counts");
        Check(plan.entityBodies==227&&plan.resourceNameBodies==307&&plan.otherUnboundBodies==0,"Trainyard instance/resource partition");
        const auto& wagonEntity=plan.entities.entities.at(2129);
        Check(wagonEntity.Find("name")&&wagonEntity.Find("name")->text=="starting_boxcar_model"&&
            wagonEntity.Find("classname")&&wagonEntity.Find("classname")->text=="func_mover","Original wagon entity identity");
        Check(plan.entityBodyIndices[2129]==std::size_t(-1),"Wagon has no invented direct-name instance body");
        unsigned wagon=0;for(std::size_t i=0;i<plan.inventory.bodies.size();++i)if(plan.inventory.bodies[i].rawName=="models/vehicles/box_car_a"){
            ++wagon;const auto& body=plan.inventory.bodies[i];
            Check(plan.bindings[i].role==SourceMapBodyRole::ResourceNameUnbound&&body.rawFilterInfo==0&&
                body.rootShapeType=="hkpMoppBvTreeShape"&&body.motionStateFinite,"Wagon resource template stays uninstantiated/raw filter0");
            float pose[32];for(unsigned j=0;j<32;++j)std::memcpy(&pose[j],&body.rawMotionStateWords[j],4);
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
                Check(std::fabs(pose[row*4+col]-(row==col?1.f:0.f))<=.000001f,"Original wagon template identity basis");
            for(unsigned j=0;j<4;++j){Check(std::fabs(pose[12+j]-(j==3?1.f:0.f))<=.000001f,"Original wagon template local origin");
                Check(std::fabs(pose[16+j]-pose[20+j])<=.000001f,"Original wagon template stationary swept centre");
                Check(std::fabs(pose[24+j]-(j==3?1.f:0.f))<=.000001f&&std::fabs(pose[28+j]-(j==3?1.f:0.f))<=.000001f,"Original wagon template swept identity quaternion");}
            std::cout<<"SOURCE_MAP_BODIES_WAGON_TEMPLATE body_offset="<<body.sourceBodyOffset<<" root_offset="<<body.rootShapeOffset
                <<" raw_filter="<<body.rawFilterInfo<<" identity_basis=1 local_origin=0,0,0,1 centre="<<pose[16]<<','<<pose[17]<<','<<pose[18]<<','<<pose[19]
                <<" entity_model="<<(wagonEntity.Find("model")?wagonEntity.Find("model")->text:std::string())<<" native_binding_inferred=0\n";
        }
        Check(wagon==1,"Exactly one original wagon resource template");
    }
    std::cout<<"SOURCE_MAP_BODIES_ORIGINAL_OK world="<<world<<" bodies="<<plan.inventory.bodies.size()<<" entities="<<plan.entities.entities.size()
        <<" exact_instances="<<plan.entityBodies<<" resource_names_unbound="<<plan.resourceNameBodies<<" other_unbound="<<plan.otherUnboundBodies
        <<" checks="<<checks<<" geometry_admitted=0 runtime_activation_inferred=0\n";
}
}
int main(int argc,char** argv){try{
    if(argc==1||(argc==2&&std::string(argv[1])=="--self-test")){Synthetic();return 0;}
    if(argc==3&&std::string(argv[1])=="--source-map"){Original(argv[2]);return 0;}
    throw std::runtime_error("usage: wolf2009-source-map-bodies [--self-test | --source-map OWNER_MPK]");
}catch(const std::exception& error){std::cerr<<"SOURCE_MAP_BODIES_FAIL checks="<<checks<<" reason="<<error.what()<<'\n';return 1;}}

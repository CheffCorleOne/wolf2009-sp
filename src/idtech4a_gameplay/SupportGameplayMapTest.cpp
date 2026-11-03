// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupportGameplayMap.h"
#include "MapPhysics.h"
#include "../../tests/HavokTestFixture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
using namespace wolf2009;
using wolf2009_test::Bytes;
unsigned checks=0,groups=0,failed=0;
const char mapName[]="wolf2009/synthetic/PROC/maps/synthetic/support";
void Check(bool ok,const char* reason) { ++checks;if(!ok)throw std::runtime_error(reason); }
template<class F> void Reject(F run,const char* reason) {
    bool refused=false;try{run();}catch(const std::exception&){refused=true;}Check(refused,reason);
}
template<class F> void Group(const char* name,F run) {
    ++groups;try{run();}catch(const std::exception& e){++failed;std::cerr<<"Support gameplay "<<name<<": "<<e.what()<<'\n';}
}
std::uint64_t Fingerprint(const Bytes& bytes) {
    std::uint64_t value=14695981039346656037ull;
    for(auto b:bytes){value^=b;value*=1099511628211ull;}return value;
}
struct Pack : wolf2009_test::HavokFixture {
    enum : std::uint32_t { Body=64,Box=576,BodyName=800 };
    Pack():HavokFixture(4096) {
        types[Body]="hkpRigidBody";types[Box]="hkpBoxShape";
        wolf2009_test::Text(data,BodyName,"maps/synthetic/support");
        links[Body+112]=BodyName;links[Body+16]=Box;
        for(int k=0;k<3;++k)wolf2009_test::F(data,Body+224+k*20,1);
        wolf2009_test::F(data,Body+332,1);wolf2009_test::F(data,Body+348,1);
        wolf2009_test::U(data,Body+44,3);
        wolf2009_test::F(data,Box+32,.4064f);
        wolf2009_test::F(data,Box+36,.8128f);
        wolf2009_test::F(data,Box+40,.2032f);
    }
};
void Word(Bytes& bytes,std::uint32_t value) {
    for(int k=0;k<4;++k)bytes.push_back(static_cast<unsigned char>(value>>(8*k)));
}
void String(Bytes& bytes,const std::string& value) {
    bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);
}
void Align4(Bytes& bytes) { while(bytes.size()%4)bytes.push_back(0); }
EntityProperty Text(const char* key,const std::string& text) {
    EntityProperty out;out.key=key;out.text=text;return out;
}
EntityRecord Start(const char* name,float x=0,float y=0,float z=32) {
    EntityRecord out;out.properties={Text("classname","info_player_start"),Text("name",name)};
    EntityProperty origin;origin.key="origin";origin.kind=EntityProperty::Floats;origin.numbers={x,y,z};
    out.properties.push_back(origin);
    EntityProperty angle;angle.key="angle";angle.kind=EntityProperty::Integer;angle.integer=90;
    out.properties.push_back(angle);return out;
}
// Independent own Version4 writer, using only the public entity value kinds.
// No original bytes or extracted entity records are embedded in this fixture.
Bytes Entities(const std::vector<EntityRecord>& entities) {
    std::vector<std::string> keys,values;
    std::map<std::string,std::size_t> keyIndex,valueIndex;
    std::vector<std::uint32_t> keyRefs,valueRefs;
    Bytes numbers,records,keyBytes,valueBytes;
    for(const auto& entity:entities) {
        Word(records,static_cast<std::uint32_t>(entity.properties.size()));
        for(const auto& property:entity.properties) {
            auto key=keyIndex.emplace(property.key,keys.size());
            if(key.second){keys.push_back(property.key);keyRefs.push_back(0);}
            ++keyRefs[key.first->second];Word(records,static_cast<std::uint32_t>(key.first->second));
            if(property.kind==EntityProperty::Text) {
                auto value=valueIndex.emplace(property.text,values.size());
                if(value.second){values.push_back(property.text);valueRefs.push_back(0);}
                ++valueRefs[value.first->second];Word(records,0);Word(records,1);
                Word(records,static_cast<std::uint32_t>(value.first->second));
            }else if(property.kind==EntityProperty::Floats) {
                Word(records,1);Word(records,static_cast<std::uint32_t>(property.numbers.size()));
                Word(records,static_cast<std::uint32_t>(numbers.size()/4));
                for(float number:property.numbers){std::uint32_t bits;std::memcpy(&bits,&number,4);Word(numbers,bits);}
            }else {
                Word(records,4);Word(records,1);Word(records,static_cast<std::uint32_t>(property.integer));
            }
        }
    }
    for(const auto& key:keys)String(keyBytes,key);
    for(const auto& value:values)String(valueBytes,value);
    std::ostringstream header;
    header<<"Version 4\nbinary\nentities "<<entities.size()<<' '<<records.size()<<"\nkeys "<<keys.size()<<' '
          <<keyBytes.size()<<"\nvalues "<<values.size()<<' '<<valueBytes.size()<<"\nintValues 0\nfloatValues "
          <<numbers.size()/4<<'\n';
    const auto text=header.str();Bytes bytes(text.begin(),text.end());
    bytes.insert(bytes.end(),numbers.begin(),numbers.end());bytes.insert(bytes.end(),keyBytes.begin(),keyBytes.end());Align4(bytes);
    for(auto count:keyRefs)Word(bytes,count);
    bytes.insert(bytes.end(),valueBytes.begin(),valueBytes.end());Align4(bytes);
    for(auto count:valueRefs)Word(bytes,count);
    bytes.insert(bytes.end(),records.begin(),records.end());bytes.push_back('\n');return bytes;
}
std::vector<EntityRecord> OwnEntities() {
    EntityRecord world;world.properties={Text("classname","worldspawn"),Text("name","world")};
    EntityRecord extra;extra.properties={Text("classname","func_static"),Text("name","unbound_prop")};
    return {world,Start("source_start"),extra};
}
SupportCollisionWorld World() {
    SupportCollisionWorld world;world.sourceFingerprint=1234;world.sourceBytes=4096;
    world.selectedBodyName="maps/synthetic/support";world.sourceBodyOffset=Pack::Body;world.rootShapeOffset=Pack::Box;
    world.rootRawFilterInfo=3;world.rootRawFilterKnown=true;
    world.selectedGeometryComplete=world.selectedHierarchyComplete=world.selectedContentsComplete=true;
    world.selectedChildFiltersComplete=world.selectedContactMetadataComplete=world.selectedCoverageComplete=true;
    world.allSourceRigidBodiesSelected=true;
    SupportCollisionShape box;box.kind=SupportCollisionKind::Box;box.radius=.5;
    box.source.sourceShapeOffset=Pack::Box;box.source.sourceRegion=123;box.source.occurrence=1;
    for(int i=0;i<8;++i)box.corePoints.push_back({i&1?16.0:-16.0,i&2?32.0:-32.0,i&4?8.0:-8.0});
    box.declaredCorePoints=8;box.filter.inheritedRawContents=3;box.filter.inheritedContentsKnown=true;
    world.totalCorePoints=8;world.shapes.push_back(box);return world;
}
std::string Audit(const SupportCollisionWorld& world) {
    std::ostringstream out;out<<std::setprecision(17)<<world.sourceFingerprint<<' '<<world.sourceBytes<<' '
        <<world.selectedBodyName<<' '<<world.sourceBodyOffset<<' '<<world.rootShapeOffset<<' '
        <<world.rootRawFilterInfo<<' '<<world.rootRawFilterKnown<<' '<<world.selectedGeometryComplete<<' '
        <<world.selectedHierarchyComplete<<' '<<world.selectedContentsComplete<<' '<<world.selectedChildFiltersComplete<<' '
        <<world.selectedContactMetadataComplete<<' '<<world.selectedCoverageComplete<<' '<<world.allSourceRigidBodiesSelected<<' '
        <<world.otherSourceRigidBodies<<' '<<world.nativeRunnable<<' '<<world.entityBindingsResolved<<' '<<world.totalCorePoints;
    for(const auto& shape:world.shapes) {
        out<<' '<<int(shape.kind)<<' '<<shape.source.sourceShapeOffset<<' '<<shape.source.sourceRegion<<' '
           <<shape.source.occurrence<<' '<<shape.radius<<' '<<shape.declaredCorePoints<<' '
           <<shape.filter.inheritedRawContents<<' '<<shape.filter.inheritedContentsKnown<<' '
           <<int(shape.filter.contentsProvenance)<<' '<<shape.triangle.contactMetadataQualified;
        for(auto p:shape.corePoints)out<<' '<<p.x<<' '<<p.y<<' '<<p.z;
        for(auto a:shape.placement.axis)out<<' '<<a.x<<' '<<a.y<<' '<<a.z;
        out<<' '<<shape.placement.translation.x<<' '<<shape.placement.translation.y<<' '<<shape.placement.translation.z;
    }
    return out.str();
}
void RefuseWorld(SupportCollisionWorld world,const char* reason) {
    const auto before=Audit(world);
    Reject([&]{AdmitSupportGameplayWorld(world,SupportGameplayContentsPolicy::DeclaredDoom3BitsV1);},reason);
    Check(before==Audit(world),"Rejected admission mutated its source world");
}
void CheckFiles(const SupportGameplayMap& plan) {
    Check(plan.mapText.find("brushDef")==std::string::npos&&plan.mapText.find("patchDef")==std::string::npos,
          "Source world was substituted with native brushes/patches");
    Check(std::count(plan.mapText.begin(),plan.mapText.end(),'{')==2&&
          std::count(plan.mapText.begin(),plan.mapText.end(),'}')==2,"Source map has unexpected entity/geometry blocks");
    Check(plan.mapText.find("\"classname\" \"worldspawn\"")!=std::string::npos&&
          plan.mapText.find("\"classname\" \"info_player_start\"")!=std::string::npos,"Native source-map lifecycle entities absent");
    Check(plan.mapText.find("\"wolf2009_support_identity\" \""+plan.identity+"\"")!=std::string::npos,
          "Generated map is not bound to its support snapshot");
    Check(plan.cmText.find("CM \"1.00\"\n0\ncollisionModel \"worldMap\"")==0,
          "Native world CM header/model identity changed");
    Check(plan.cmText.find("vertices { 0 }")!=std::string::npos&&
          plan.cmText.find("edges { 1 ( 0 0 ) 0 0 }")!=std::string::npos&&
          plan.cmText.find("nodes { ( -1 0 ) }")!=std::string::npos&&
          plan.cmText.find("polygons { }")!=std::string::npos&&plan.cmText.find("brushes { }")!=std::string::npos,
          "Native CM contains unexpected collision geometry");
    Check(std::count(plan.cmText.begin(),plan.cmText.end(),'{')==6&&
          plan.cmText.find("wolf2009/collision/")==std::string::npos,"Native CM has additional models/material geometry");
    Check(!plan.world.nativeRunnable&&!plan.world.entityBindingsResolved,
          "Source reader falsely certified a campaign/native-runnable world");
}
std::string ExpectedIdentity(const SupportGameplayMap& plan,const Bytes& hkxs,const Bytes& ents) {
    std::ostringstream out;out<<"support1-doom3bits1:"<<std::hex<<plan.mountEpoch<<':'<<Fingerprint(hkxs)<<':'<<hkxs.size()<<':'
        <<Fingerprint(ents)<<':'<<ents.size()<<':'<<plan.world.sourceBodyOffset<<':'<<plan.world.rootShapeOffset;
    return out.str();
}
void RefusePrepare(const Bytes& hkxs,const Bytes& ents,const std::string& name,std::uint64_t epoch,
                   const SupportGameplayMap& admitted,const char* reason) {
    const auto beforeH=hkxs,beforeE=ents;auto output=admitted;
    Reject([&]{output=PrepareSupportGameplayMap(hkxs,ents,name,epoch);},reason);
    Check(hkxs==beforeH&&ents==beforeE,"Rejected preparation mutated its input snapshots");
    Check(output.identity==admitted.identity&&output.mapText==admitted.mapText&&output.cmText==admitted.cmText&&
          Audit(output.world)==Audit(admitted.world),"Rejected preparation replaced a previously admitted output");
}
void Synthetic() {
    Group("declared contents and immutable admission",[]{
        const auto world=World();const auto before=Audit(world);
        const auto response=AdmitSupportGameplayWorld(world,SupportGameplayContentsPolicy::DeclaredDoom3BitsV1);
        Check(response.size()==1&&response[0].contents==3&&response[0].material=="wolf2009/collision/solid_opaque",
              "Source contents3/material3 were replaced with fixture SOLID1");
        Check(before==Audit(world),"Admission mutated source geometry/metadata");
        Reject([&]{AdmitSupportGameplayWorld(world,static_cast<SupportGameplayContentsPolicy>(99));},"Unknown policy admitted");
        auto other=World();other.shapes[0].filter.inheritedRawContents=8;
        const auto player=AdmitSupportGameplayWorld(other,SupportGameplayContentsPolicy::DeclaredDoom3BitsV1);
        Check(player[0].contents==8&&player[0].material=="wolf2009/collision/playerclip","Declared bits were flattened into SOLID");
        other=World();other.shapes[0].kind=SupportCollisionKind::Sphere;
        other.shapes[0].corePoints={{0,0,0}};other.shapes[0].radius=4;
        Check(AdmitSupportGameplayWorld(other,SupportGameplayContentsPolicy::DeclaredDoom3BitsV1)[0].contents==3,
              "Exact sphere support profile refused");
        other.shapes[0].kind=SupportCollisionKind::Capsule;other.shapes[0].corePoints.push_back({0,0,8});
        Check(AdmitSupportGameplayWorld(other,SupportGameplayContentsPolicy::DeclaredDoom3BitsV1)[0].contents==3,
              "Exact capsule support profile refused");
    });
    Group("unknown contents and metadata",[]{
        for(std::uint32_t bits:{0u,256u,0x80000003u,4096u}) {
            auto world=World();world.shapes[0].filter.inheritedRawContents=bits;
            RefuseWorld(world,"Unknown/unbound-trigger contents admitted");
        }
        auto world=World();world.shapes[0].filter.inheritedContentsKnown=false;
        RefuseWorld(world,"Unknown source contents admitted");
        world=World();world.shapes[0].kind=SupportCollisionKind::Triangle;
        world.shapes[0].triangle.contactMetadataQualified=true;
        world.shapes[0].filter.contentsProvenance=SupportCollisionContentsProvenance::TriangleOccurrenceValueUnmapped;
        RefuseWorld(world,"Triangle raw child material reinterpreted as gameplay contents");
        world=World();world.shapes[0].kind=SupportCollisionKind::Triangle;
        RefuseWorld(world,"Unqualified triangle response admitted");
        world=World();world.shapes[0].kind=SupportCollisionKind::Cylinder;
        RefuseWorld(world,"Cylinder envelope promoted to native retail response");
    });
    Group("coverage, other bodies and source identities",[]{
        typedef bool SupportCollisionWorld::* Flag;
        const Flag flags[]={&SupportCollisionWorld::selectedGeometryComplete,&SupportCollisionWorld::selectedHierarchyComplete,
            &SupportCollisionWorld::selectedContentsComplete,&SupportCollisionWorld::selectedChildFiltersComplete,
            &SupportCollisionWorld::selectedContactMetadataComplete,&SupportCollisionWorld::selectedCoverageComplete,
            &SupportCollisionWorld::allSourceRigidBodiesSelected};
        for(auto flag:flags){auto world=World();world.*flag=false;RefuseWorld(world,"Incomplete source coverage/body selection admitted");}
        auto world=World();world.otherSourceRigidBodies=1;RefuseWorld(world,"Additional source rigid body silently omitted");
        world=World();world.shapes[0].source.occurrence=0;RefuseWorld(world,"Missing source occurrence admitted");
        world=World();world.shapes.push_back(world.shapes[0]);RefuseWorld(world,"Duplicate source occurrence admitted");
        world=World();world.shapes.clear();RefuseWorld(world,"Empty source world admitted");
        world=World();world.shapes[0].radius=-1;RefuseWorld(world,"Invalid source geometry admitted");
    });
    Group("own checked HKXS and ENTS prepare",[]{
        const auto hkxs=Pack().File(),ents=Entities(OwnEntities());const auto beforeH=hkxs,beforeE=ents;
        const auto plan=PrepareSupportGameplayMap(hkxs,ents,mapName,7);
        Check(plan.world.shapes.size()==1&&plan.responses.size()==1&&plan.world.totalCorePoints==8,
              "Own exact support core disappeared during preparation");
        Check(plan.responses[0].contents==3&&plan.responses[0].material=="wolf2009/collision/solid_opaque",
              "Prepared response lost source mask/material");
        Check(plan.world.rootRawFilterKnown&&plan.world.rootRawFilterInfo==3&&plan.world.allSourceRigidBodiesSelected,
              "Source root/body facts lost");
        Check(plan.canonicalMapName=="wolf2009/synthetic/proc/maps/synthetic/support"&&
              plan.mountLabel=="synthetic"&&plan.mountEpoch==7,"Canonical source/mount binding changed");
        Check(plan.sourceEntities==3&&plan.unimplementedEntities==1,"Unimplemented source entities were hidden");
        Check(plan.mapText.find("\"origin\" \"0 0 32\"")!=std::string::npos&&
              plan.mapText.find("\"name\" \"source_start\"")!=std::string::npos,"Authored source start changed");
        CheckFiles(plan);Check(hkxs==beforeH&&ents==beforeE,"Preparation mutated source bytes");
    });
    Group("snapshot identity and owning output",[]{
        auto pack=Pack();auto hkxs=pack.File(),ents=Entities(OwnEntities());
        const auto plan=PrepareSupportGameplayMap(hkxs,ents,mapName,7);
        Check(plan.identity==ExpectedIdentity(plan,hkxs,ents),"Identity omitted its versioned policy/epoch/HKXS/ENTS/body binding");
        Check(PrepareSupportGameplayMap(hkxs,ents,mapName,8).identity!=plan.identity,"Mount epoch did not invalidate identity");
        wolf2009_test::F(pack.data,Pack::Box+32,.4318f);
        Check(PrepareSupportGameplayMap(pack.File(),ents,mapName,7).identity!=plan.identity,"HKXS change did not invalidate identity");
        auto changed=OwnEntities();changed[1]=Start("other_start");
        Check(PrepareSupportGameplayMap(hkxs,Entities(changed),mapName,7).identity!=plan.identity,"ENTS change did not invalidate identity");
        hkxs.clear();ents.clear();CheckFiles(plan);
        Check(plan.world.shapes[0].corePoints.size()==8&&plan.responses[0].contents==3,"Prepared output borrowed source-buffer lifetime");
    });
    Group("prepare fail closed and retains caller output",[]{
        const auto hkxs=Pack().File(),ents=Entities(OwnEntities());const auto good=PrepareSupportGameplayMap(hkxs,ents,mapName,7);
        RefusePrepare(hkxs,ents,mapName,0,good,"Zero mount epoch admitted");
        RefusePrepare(hkxs,ents,"wolf2009/synthetic/PROC/maps/synthetic/support.map",7,good,"Ambiguous map extension admitted");
        RefusePrepare(hkxs,ents,"wolf2009/other/PROC/maps/synthetic/different",7,good,"Wrong selected source body admitted");
        RefusePrepare(Bytes(),ents,mapName,7,good,"Malformed HKXS admitted");
        RefusePrepare(hkxs,Bytes(),mapName,7,good,"Malformed ENTS admitted");
        auto pack=Pack();wolf2009_test::U(pack.data,Pack::Body+44,256);
        RefusePrepare(pack.File(),ents,mapName,7,good,"Unknown decoded source contents admitted");
        pack=Pack();pack.types[Pack::Box]="hkpCylinderShape";
        RefusePrepare(pack.File(),ents,mapName,7,good,"Unresolved decoded cylinder admitted");
        pack=Pack();pack.types[1024]="hkpRigidBody";
        RefusePrepare(pack.File(),ents,mapName,7,good,"Decoded additional rigid body silently omitted");
        pack=Pack();pack.types[Pack::Box]="hkpUnobservedShape";
        RefusePrepare(pack.File(),ents,mapName,7,good,"Unknown decoded shape/coverage silently omitted");
    });
    Group("ambiguous authored starts require explicit selection",[]{
        const auto hkxs=Pack().File();auto entities=OwnEntities();entities.push_back(Start("second_start",10,20,40));
        const auto ents=Entities(entities),single=Entities(OwnEntities());const auto good=PrepareSupportGameplayMap(hkxs,single,mapName,7);
        RefusePrepare(hkxs,ents,mapName,7,good,"Ambiguous primary source starts selected automatically");
        const auto selected=PrepareSupportGameplayMap(hkxs,ents,mapName,7,"second_start");
        Check(selected.mapText.find("\"origin\" \"10 20 40\"")!=std::string::npos,
              "Explicit authored start selection changed coordinates");
        entities.erase(entities.begin()+1);
        entities.erase(entities.end()-1);
        RefusePrepare(hkxs,Entities(entities),mapName,7,good,"Missing source start silently synthesized");
    });
    std::cout<<"WOLF2009_SUPPORT_GAMEPLAY_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed="<<failed<<'\n';
}
void Original(const char* path) {
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open owner-supplied source map");
    const auto archive=Archive::Scan(input);std::string resource;
    for(const auto& chunk:archive.Chunks())if(chunk.type=="PROC")for(const auto& entry:chunk.entries) {
        Check(resource.empty(),"Owner source package has ambiguous PROC resources");resource=entry.name;
    }
    Check(!resource.empty(),"Owner source package has no PROC resource");
    const auto hkxs=ReadMapResource(input,archive,resource,"HKXS",64u<<20);
    const auto ents=ReadMapResource(input,archive,resource,"ENTS",64u<<20);
    const auto plan=PrepareSupportGameplayMap(hkxs,ents,"wolf2009/source/PROC/"+resource,1);
    CheckFiles(plan);Check(plan.identity==ExpectedIdentity(plan,hkxs,ents),"Original snapshot identity mismatch");
    Check(plan.world.sourceFingerprint==Fingerprint(hkxs)&&plan.world.sourceBytes==hkxs.size(),"Original source-byte identity lost");
    if(resource=="maps/game/empty/empty"||resource=="maps/game/empty/soundtest") {
        Check(plan.world.shapes.size()==10&&plan.world.totalCorePoints==80&&plan.responses.size()==10,
              "Original empty world shape count changed");
        Check(plan.world.rootRawFilterKnown&&plan.world.rootRawFilterInfo==3&&plan.world.otherSourceRigidBodies==0,
              "Original empty selected body/filter facts changed");
        for(std::size_t i=0;i<plan.responses.size();++i) {
            Check(plan.world.shapes[i].kind==SupportCollisionKind::Box&&plan.world.shapes[i].radius==0,
                  "Original empty exact box/radius changed");
            Check(plan.responses[i].contents==3&&plan.responses[i].material=="wolf2009/collision/solid_opaque",
                  "Original empty raw3 was flattened to fixture1");
        }
    }
    std::cout<<"WOLF2009_SUPPORT_GAMEPLAY_SOURCE_OK map="<<resource<<" shapes="<<plan.world.shapes.size()
        <<" responses="<<plan.responses.size()<<" sourceEntities="<<plan.sourceEntities
        <<" unimplementedEntities="<<plan.unimplementedEntities<<" nativeVertices=0 nativeBrushes=0"
        <<" rootRaw="<<plan.world.rootRawFilterInfo<<" policy=DeclaredDoom3BitsV1"
        <<" nativeRunnable="<<plan.world.nativeRunnable<<" identity="<<plan.identity<<'\n';
}
} // namespace
int main(int argc,char** argv) {
    try {
        Synthetic();if(failed)return 1;
        for(int i=1;i<argc;++i) {
            if(std::string(argv[i])=="--self-test")continue;
            Check(std::string(argv[i])=="--source-map"&&i+1<argc,"Usage: [--self-test] [--source-map owner-supplied.mpk]");
            Original(argv[++i]);
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"Support gameplay map: "<<e.what()<<'\n';return 1;}
}

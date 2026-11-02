// SPDX-License-Identifier: GPL-3.0-or-later
#include "StaticCollision.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(double a,double b){return std::abs(a-b)<1e-5;}
template<class F>void Reject(F function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}Need(failed,"Malformed collision input/query was accepted");}
void U(Bytes& b,std::size_t p,std::uint32_t value){Need(p+4<=b.size(),"Synthetic writer range");for(int i=0;i<4;++i)b[p+i]=static_cast<unsigned char>(value>>(8*i));}
void F(Bytes& b,std::size_t p,float value){std::uint32_t bits;std::memcpy(&bits,&value,4);U(b,p,bits);}
void Text(Bytes& b,std::size_t p,const std::string& value){Need(p+value.size()<b.size(),"Synthetic string writer range");for(std::size_t i=0;i<value.size();++i)b[p+i]=static_cast<unsigned char>(value[i]);b[p+value.size()]=0;}
std::size_t Align(std::size_t n){return (n+15)/16*16;}
// Entirely synthetic packfile: a translated box, through a list and MOPP
// child link. No game bytes, Havok SDK structures or code are fixtures.
struct Fixture {
    Bytes data;
    std::vector<std::pair<std::uint32_t,std::uint32_t> > links;
    std::map<std::uint32_t,std::string> types;
    Fixture():data(1792,0){
        types[0]="hkRootLevelContainer";types[64]="hkpRigidBody";
        types[576]="hkpMoppBvTreeShape";types[640]="hkpListShape";
        types[736]="hkpConvexTranslateShape";types[800]="hkpBoxShape";
        Text(data,880,"maps/game/synthetic/test");
        Link(64+112,880);Link(64+16,576);Link(576+52,640);
        Link(640+24,704);U(data,640+28,1);Link(704,736);Link(736+24,800);
        F(data,736+40,-.0254f);F(data,800+32,.0508f);F(data,800+36,.0508f);F(data,800+40,.0254f);
        Identity(64);
    }
    void Identity(std::size_t body){for(int i=0;i<3;++i)F(data,body+224+i*20,1);F(data,body+320+12,1);F(data,body+336+12,1);}
    void Link(std::uint32_t from,std::uint32_t to){for(std::size_t i=0;i<links.size();++i)if(links[i].first==from){links[i].second=to;return;}links.push_back(std::make_pair(from,to));}
    void Storage(){
        Link(576+52,960);types[960]="hkpStorageExtendedMeshShape";
        F(data,960+96,1);F(data,960+100,1);F(data,960+104,1);
        Link(960+160,1184);U(data,960+164,1);
        Link(1184+16,1248);U(data,1184+20,1);F(data,1184+44,1);Link(1248,736);
    }
    Bytes File(bool global=false)const{
        Bytes classes(16,0);std::map<std::string,std::uint32_t> names;
        for(std::map<std::uint32_t,std::string>::const_iterator i=types.begin();i!=types.end();++i)if(!names.count(i->second)){
            const std::size_t p=classes.size();names[i->second]=static_cast<std::uint32_t>(p);classes.resize(p+i->second.size()+1);Text(classes,p,i->second);
        }
        classes.resize(Align(classes.size()),255);
        const std::size_t absolute=208+classes.size(),local=data.size();
        const std::size_t linkEnd=Align(local+links.size()*(global?12:8));
        const std::size_t globalStart=global?local:linkEnd,virtualStart=linkEnd;
        const std::size_t end=Align(virtualStart+types.size()*12);
        Bytes bytes(absolute+end,255);for(std::size_t i=0;i<208;++i)bytes[i]=0;
        U(bytes,0,0x57e0e057);U(bytes,4,0x10c0c010);U(bytes,12,5);
        bytes[16]=4;bytes[17]=1;bytes[18]=0;bytes[19]=1;
        U(bytes,20,3);U(bytes,24,2);U(bytes,28,0);U(bytes,32,0);U(bytes,36,names["hkRootLevelContainer"]);
        Text(bytes,40,"Havok-5.6.0-r1");
        const char* tags[]={"__classnames__","__types__","__data__"};
        for(int i=0;i<3;++i){const std::size_t h=64+i*48;Text(bytes,h,tags[i]);U(bytes,h+20,static_cast<std::uint32_t>(i==0?208:absolute));
            for(int k=0;k<6;++k)U(bytes,h+24+k*4,static_cast<std::uint32_t>(i==0?classes.size():i==1?0:k==0?local:k==1?globalStart:k==2?virtualStart:end));}
        std::copy(classes.begin(),classes.end(),bytes.begin()+208);std::copy(data.begin(),data.end(),bytes.begin()+absolute);
        std::size_t p=absolute+local;
        for(std::size_t i=0;i<links.size();++i){U(bytes,p,links[i].first);if(global)U(bytes,p+4,2);U(bytes,p+(global?8:4),links[i].second);p+=global?12:8;}
        p=absolute+virtualStart;
        for(std::map<std::uint32_t,std::string>::const_iterator i=types.begin();i!=types.end();++i){U(bytes,p,i->first);U(bytes,p+4,0);U(bytes,p+8,names[i->second]);p+=12;}
        return bytes;
    }
};
StaticCollisionScene Decode(const Fixture& f,const StaticCollisionLimits& limits=StaticCollisionLimits()){
    return DecodeStaticCollision(f.File(),"maps/game/synthetic/test",limits);
}
StaticCollisionBox Box(CollisionVec3 lo,CollisionVec3 hi){StaticCollisionBox b;b.minimum=lo;b.maximum=hi;return b;}
}
int RunStaticCollisionTests(){int passed=0;try{
    Fixture fixture;StaticCollisionScene s=Decode(fixture);
    Need(s.boxes.size()==1&&s.coverageComplete&&s.identityBodyPoseVerified&&!s.physicsFiltersApplied&&s.visitedShapes==4,"Synthetic shape graph/coverage failed");
    Need(Near(s.boxes[0].minimum.x,-2)&&Near(s.boxes[0].minimum.z,-2)&&Near(s.boxes[0].maximum.z,0),"Metre-to-game-unit box conversion failed");++passed;
    s=DecodeStaticCollision(fixture.File(true),"MAPS\\GAME\\SYNTHETIC\\TEST");Need(s.boxes.size()==1,"Global fixups or explicit normalized body selection failed");++passed;
    Fixture shared=fixture;U(shared.data,640+28,2);shared.Link(720,736);s=Decode(shared);Need(s.boxes.size()==2&&s.visitedShapes==6,"Shared shape instances mistaken for graph cycles");++passed;
    Fixture unknown=fixture;unknown.types[800]="syntheticUnsupportedShape";s=Decode(unknown);Need(!s.coverageComplete&&s.boxes.empty()&&s.unsupported["syntheticUnsupportedShape"]==1,"Unsupported shape silently treated as complete");++passed;
    Fixture storage=fixture;storage.Storage();s=Decode(storage);Need(s.coverageComplete&&s.boxes.size()==1,"Identity storage placement failed");
    F(storage.data,1184+48,1);s=Decode(storage);Need(!s.coverageComplete&&s.boxes.empty()&&s.omittedTransformedSubparts==1,"Nonidentity storage placement silently used identity");++passed;
    storage=fixture;storage.Storage();storage.Link(960+148,1088);U(storage.data,960+152,1);U(storage.data,1088+16,7);s=Decode(storage);
    Need(!s.coverageComplete&&s.boxes.size()==1&&s.omittedTriangles==7,"Omitted triangle coverage lost");++passed;
    Fixture rounded=fixture;F(rounded.data,800+16,.01f);s=Decode(rounded);Need(!s.coverageComplete&&s.boxes.empty(),"Unverified rounded box treated as exact AABB");++passed;
    Fixture pose=fixture;F(pose.data,64+224,0);Reject([&]{Decode(pose);});pose=fixture;F(pose.data,64+272,1);Reject([&]{Decode(pose);});pose=fixture;F(pose.data,64+320,.5f);Reject([&]{Decode(pose);});++passed;
    Reject([&]{DecodeStaticCollision(fixture.File(),"maps/game/other");});Fixture duplicate=fixture;duplicate.types[1280]="hkpRigidBody";duplicate.Link(1280+112,880);Reject([&]{Decode(duplicate);});++passed;
    Fixture unnamed=fixture;unnamed.types[1280]="hkpRigidBody";s=Decode(unnamed);Need(s.otherRigidBodies==1&&s.boxes.size()==1,"Unselected unnamed body changed main-body selection");++passed;
    Fixture cycle=fixture;cycle.Link(736+24,640);Reject([&]{Decode(cycle);});Fixture missing=fixture;missing.links.erase(missing.links.begin()+1);Reject([&]{Decode(missing);});++passed;
    Fixture bad=fixture;F(bad.data,800+32,-1);Reject([&]{Decode(bad);});bad=fixture;F(bad.data,736+32,(std::numeric_limits<float>::quiet_NaN)());Reject([&]{Decode(bad);});bad=fixture;bad.Link(704,0xfffffff0u);Reject([&]{Decode(bad);});++passed;
    Bytes bytes=fixture.File();bytes.pop_back();Reject([&]{DecodeStaticCollision(bytes,"maps/game/synthetic/test");});bytes=fixture.File();U(bytes,12,6);Reject([&]{DecodeStaticCollision(bytes,"maps/game/synthetic/test");});bytes=fixture.File();U(bytes,160+24,0xffffffffu);Reject([&]{DecodeStaticCollision(bytes,"maps/game/synthetic/test");});++passed;
    StaticCollisionLimits limits;limits.sourceBytes=1;Reject([&]{Decode(fixture,limits);});limits=StaticCollisionLimits();limits.objects=2;Reject([&]{Decode(fixture,limits);});limits=StaticCollisionLimits();limits.fixups=2;Reject([&]{Decode(fixture,limits);});++passed;
    limits=StaticCollisionLimits();limits.shapeVisits=3;Reject([&]{Decode(fixture,limits);});limits=StaticCollisionLimits();limits.nesting=3;Reject([&]{Decode(fixture,limits);});limits=StaticCollisionLimits();limits.transforms=0;Reject([&]{Decode(fixture,limits);});limits=StaticCollisionLimits();limits.boxes=0;Reject([&]{Decode(fixture,limits);});++passed;
    limits=StaticCollisionLimits();limits.compoundEntries=0;Reject([&]{Decode(fixture,limits);});limits=StaticCollisionLimits();limits.coordinateGameUnits=1;Reject([&]{Decode(fixture,limits);});limits.coordinateGameUnits=(std::numeric_limits<double>::quiet_NaN)();Reject([&]{Decode(fixture,limits);});++passed;
    Fixture huge=fixture;U(huge.data,640+28,0xffffffffu);limits=StaticCollisionLimits();limits.compoundEntries=(std::numeric_limits<std::size_t>::max)();Reject([&]{Decode(huge,limits);});++passed;
    Fixture longType=fixture;longType.types[800]=std::string(257,'x');Reject([&]{Decode(longType);});bytes=fixture.File();U(bytes,36,0xffffffffu);Reject([&]{DecodeStaticCollision(bytes,"maps/game/synthetic/test");});++passed;
    StaticCollisionScene world;world.coverageComplete=true;world.boxes.push_back(Box({-100,-100,-10},{100,100,0}));world.boxes.push_back(Box({50,-100,0},{60,100,100}));
    StaticCollisionHit hit=RaycastStatic(world,{0,0,72},{0,0,-100});Need(hit.hit&&Near(hit.fraction,.72)&&hit.normal.z==1&&hit.boxIndex==0&&!hit.startedInside&&hit.coverageComplete,"Analytical floor ray failed");++passed;
    hit=RaycastStatic(world,{0,0,36},{100,0,0});Need(hit.hit&&Near(hit.fraction,.5)&&hit.normal.x==-1&&hit.boxIndex==1,"Analytical wall ray failed");++passed;
    hit=SweepStaticBox(world,{0,0,36},{16,16,36},{100,0,0});Need(hit.hit&&Near(hit.fraction,.34)&&hit.normal.x==-1&&hit.boxIndex==1,"Player AABB sweep or tangential floor contact failed");++passed;
    hit=SweepStaticBox(world,{0,0,36},{16,16,36},{0,0,-5});Need(hit.hit&&!hit.startedInside&&hit.fraction==0&&hit.normal.z==1,"Inward movement from floor contact failed");
    hit=SweepStaticBox(world,{0,0,36},{16,16,36},{0,0,5});Need(!hit.hit,"Outward movement from floor contact was blocked");++passed;
    hit=RaycastStatic(world,{0,0,-5},{0,0,1});Need(hit.hit&&hit.startedInside&&hit.fraction==0,"Initial penetration not reported");
    hit=RaycastStatic(world,{0,0,200},{0,0,0});Need(!hit.hit,"Stationary exterior point reported a collision");++passed;
    world.coverageComplete=false;hit=RaycastStatic(world,{0,0,200},{0,0,10});Need(!hit.hit&&!hit.coverageComplete,"Partial-scene miss incorrectly claims clear coverage");
    hit=RaycastStatic(world,{0,0,72},{0,0,-100});Need(hit.hit&&!hit.coverageComplete,"Partial-scene hit lost coverage status");++passed;
    Reject([&]{SweepStaticBox(world,{0,0,0},{-1,1,1},{1,0,0});});Reject([&]{RaycastStatic(world,{(std::numeric_limits<double>::infinity)(),0,0},{1,0,0});});world.boxes[0].maximum.z=-20;Reject([&]{RaycastStatic(world,{0,0,10},{0,0,-20});});++passed;
    std::cout<<"Static collision self-test: "<<passed<<" passed, 0 failed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Static collision self-test failed after "<<passed<<" checks: "<<e.what()<<'\n';return 1;}}
}

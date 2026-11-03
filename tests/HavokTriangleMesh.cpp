// SPDX-License-Identifier: GPL-3.0-or-later
#include "HavokTriangleMesh.h"
#include "HavokTestFixture.h"
#include "MapPhysics.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace wolf2009;
using namespace wolf2009_test;
template<class Fn> void Reject(Fn f) {bool caught=false;try{f();}catch(const std::exception &){caught=true;}Need(caught,"Malformed triangle input accepted");}
bool Near(double a,double b){return std::abs(a-b)<.00001;}
struct Fixture : HavokFixture {
    Fixture() {
        types[64]="hkpStorageExtendedMeshShape";
        types[1024]="hkpStorageExtendedMeshShapeMeshSubpartStorage";
        types[1600]="hkpStorageExtendedMeshShapeMeshSubpartStorage";
        for(int k=0;k<4;++k)F(data,64+96+k*4,1);
        links[64+148]=512;U(data,64+152,2);links[64+192]=768;U(data,64+196,2);
        links[768]=1024;links[772]=1600;
        Record(512,2,16);Record(576,1,8);
        Array(1024,8,1120,3);Array(1024,32,1216,4);
        Array(1024,44,1248,1);Array(1024,56,1280,1);Array(1024,68,1320,2);
        Array(1600,8,1720,3);Array(1600,20,1800,4);
        Vertex(1120,0,0,0,0x7fc01234u);Vertex(1136,.0254f,0,0,0x80000000u);Vertex(1152,0,.0254f,0,0x12345678u);
        Vertex(1720,0,0,.0254f,0);Vertex(1736,.0254f,0,.0254f,0);Vertex(1752,0,.0254f,.0254f,0);
        U(data,1216,0);U(data,1220,1);U(data,1224,2);U(data,1228,0xdeadbeefu);
        data[1800]=2;data[1802]=1;data[1804]=0;data[1806]=0xef;data[1807]=0xbe;
        data[1248]=255;U(data,1280,0x12345678u);data[1320]=0xff;data[1321]=0xff;data[1322]=0x34;data[1323]=0x12;
    }
    void Record(std::uint32_t at,std::uint32_t type,std::uint32_t stride) {
        U(data,at,256);U(data,at+8,65536);U(data,at+16,1);U(data,at+24,16);
        U(data,at+28,3);U(data,at+52,stride);U(data,at+56,type);U(data,at+60,0x13579bdfu);
    }
    void Array(std::uint32_t owner,std::uint32_t field,std::uint32_t target,std::uint32_t count) {
        links[owner+field]=target;U(data,owner+field+4,count);U(data,owner+field+8,0xc0000000u|count);
    }
    void Vertex(std::uint32_t at,float x,float y,float z,std::uint32_t w) {
        F(data,at,x);F(data,at+4,y);F(data,at+8,z);U(data,at+12,w);
    }
};
HavokTriangleMeshDocument Decode(const Fixture &f,const HavokTriangleMeshLimits &l=HavokTriangleMeshLimits()) {
    return DecodeHavokTriangleMeshes(f.File(),{},l);
}
void Synthetic() {
    Fixture f;const auto result=Decode(f);
    Need(result.completeBufferCoverage&&result.storages.size()==1&&result.declaredSubparts==2&&
         result.decodedSubparts==2&&result.decodedTriangles==2&&result.decodedVertices==6,
         "Triangle buffer counts/coverage incorrect");
    const auto &a=result.storages[0].subparts[0],&b=result.storages[0].subparts[1];
    Need(a.indexBytes==4&&b.indexBytes==2&&a.triangles[0].indices[2]==2&&b.triangles[0].indices[0]==2,
         "Stored u16/u32 index triples changed order");
    Need(a.triangles[0].strideTail==Bytes({0xef,0xbe,0xad,0xde})&&b.triangles[0].strideTail==Bytes({0xef,0xbe}),
         "Triangle stride tails lost/interpreted as vertex indices");
    Need(a.vertices[0].rawWBits==0x7fc01234u&&a.vertices[1].rawWBits==0x80000000u&&
         Near(a.vertices[1].unscaledGamePosition.x,1)&&Near(b.vertices[0].unscaledGamePosition.z,1),
         "Finite xyz/raw W bits or unit conversion changed");
    Need(a.rawRecord[60]==0xdf&&a.arrays[3].bytes[0]==255&&a.arrays[4].bytes==Bytes({0x78,0x56,0x34,0x12})&&
         a.arrays[5].bytes==Bytes({0xff,0xff,0x34,0x12})&&a.arrays[2].rawCapacityFlags==0xc0000004u,
         "Raw record/material metadata/capacity flags changed");
    Need(Decode(f).storages[0].subparts[0].sourceRegion==a.sourceRegion&&a.sourceRegion!=b.sourceRegion,
         "Triangle provenance/occurrences unstable");
    auto changed=f;changed.data[3000]=1;
    Need(Decode(changed).storages[0].subparts[0].sourceRegion!=a.sourceRegion,"Changed source reused triangle fingerprint");
    auto extra=f;extra.links[512+4]=1248;
    Need(Decode(extra).storages[0].subparts[0].recordFixups[0].targetDataOffset==1248,
         "Raw triangle-record fixup not retained");
    auto scaled=f;F(scaled.data,64+96,2);const auto scale=Decode(scaled);
    Need(!scale.completeBufferCoverage&&!scale.storages[0].identityScale&&
         scale.storages[0].subparts[0].buffersDecoded&&Near(scale.storages[0].subparts[0].vertices[1].unscaledGamePosition.x,1),
         "Nonidentity storage scale silently applied/qualified");
    scaled=f;U(scaled.data,64+108,0x7fc00000u);const auto invalidScale=Decode(scaled);
    Need(!invalidScale.storages[0].finiteScale&&!invalidScale.completeBufferCoverage&&
         invalidScale.storages[0].rawScaleBits[3]==0x7fc00000u,"Unknown scale W discarded/qualified");
    auto unknown=f;U(unknown.data,512+56,3);const auto unknownType=Decode(unknown);
    Need(!unknownType.completeBufferCoverage&&!unknownType.storages[0].subparts[0].buffersDecoded&&
         unknownType.storages[0].subparts[0].arrays[2].bytes.size()==16&&
         unknownType.storages[0].subparts[0].vertices.size()==3,"Unknown index type inferred or raw buffers discarded");
    unknown=f;U(unknown.data,512+24,12);const auto unknownStride=Decode(unknown);
    Need(!unknownStride.completeBufferCoverage&&unknownStride.storages[0].subparts[0].vertices.empty()&&
         unknownStride.storages[0].subparts[0].arrays[0].bytes.size()==48,"Unknown vertex stride inferred");
    unknown=f;U(unknown.data,512+52,4);Need(!Decode(unknown).completeBufferCoverage,"Strip-like stride guessed as a triangle list");
    auto tight=f;U(tight.data,512+52,12);U(tight.data,1024+36,3);
    Need(Decode(tight).storages[0].subparts[0].triangles[0].strideTail.empty(),"Tight triangle triple invented padding");
    auto bad=f;U(bad.data,1216,3);Reject([&]{Decode(bad);});
    bad=f;bad.data[1800]=3;Reject([&]{Decode(bad);});
    bad=f;F(bad.data,1120,std::numeric_limits<float>::infinity());Reject([&]{Decode(bad);});
    bad=f;U(bad.data,1024+12,4);Reject([&]{Decode(bad);});
    bad=f;U(bad.data,1024+36,3);Reject([&]{Decode(bad);});
    bad=f;U(bad.data,64+196,1);Reject([&]{Decode(bad);});
    bad=f;bad.links.erase(1024+32);Reject([&]{Decode(bad);});
    bad=f;bad.links[1024+8]=4080;Reject([&]{Decode(bad);});
    bad=f;bad.types[1024]="hkpRigidBody";Reject([&]{Decode(bad);});
    Reject([&]{DecodeHavokTriangleMeshes(f.File(),{64,64});});
    Reject([&]{DecodeHavokTriangleMeshes(f.File(),{1024});});
    HavokTriangleMeshLimits limits;limits.totalVertices=5;Reject([&]{Decode(f,limits);});
    limits=HavokTriangleMeshLimits();limits.totalTriangles=1;Reject([&]{Decode(f,limits);});
    limits=HavokTriangleMeshLimits();limits.decodedBytes=64;Reject([&]{Decode(f,limits);});
    limits=HavokTriangleMeshLimits();limits.arrayElements=3;Reject([&]{Decode(f,limits);});
    limits=HavokTriangleMeshLimits();limits.subparts=1;Reject([&]{Decode(f,limits);});
    std::cout << "WOLF2009_HAVOK_TRIANGLE_SYNTHETIC_OK\n";
}
void Original(const char *file) {
    std::ifstream input(file,std::ios::binary);Need(bool(input),"Cannot open map package");
    const auto archive=Archive::Scan(input);std::string name;
    for(const auto &chunk:archive.Chunks())if(chunk.type=="PROC")for(const auto &entry:chunk.entries) {
        Need(name.empty(),"Ambiguous map resource");name=entry.name;
    }
    const auto bytes=ReadMapResource(input,archive,name,"HKXS",64u<<20);
    const auto all=DecodeHavokTriangleMeshes(bytes);
    CollisionVec3 lo={1e30,1e30,1e30},hi={-1e30,-1e30,-1e30};
    std::size_t u16=0,u32=0,tails=0,materialBytes=0;
    for(const auto &storage:all.storages)for(const auto &part:storage.subparts) {
        u16+=part.indexBytes==2;u32+=part.indexBytes==4;
        materialBytes+=part.arrays[3].bytes.size()+part.arrays[4].bytes.size()+part.arrays[5].bytes.size();
        for(const auto &t:part.triangles)tails+=t.strideTail.size();
        for(const auto &v:part.vertices) {
            const auto p=v.unscaledGamePosition;
            lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
            hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
        }
    }
    std::cout << "WOLF2009_HAVOK_TRIANGLE_REAL_OK map=" << name << " storages=" << all.storages.size()
        << " subparts=" << all.declaredSubparts << " decodedSubparts=" << all.decodedSubparts
        << " identityQualified=" << all.qualifiedIdentitySubparts << " vertices=" << all.decodedVertices
        << " triangles=" << all.decodedTriangles << " declaredTriangles=" << all.declaredTriangles
        << " u16Parts=" << u16 << " u32Parts=" << u32 << " strideTailBytes=" << tails
        << " rawMaterialBytes=" << materialBytes << " decodedBytes=" << all.decodedBytes
        << " bufferCoverage=" << all.completeBufferCoverage << '\n';
    if(all.decodedVertices)std::cout << std::setprecision(10) << "UnscaledSourceVertexBounds "
        << lo.x << ' ' << lo.y << ' ' << lo.z << " / " << hi.x << ' ' << hi.y << ' ' << hi.z << '\n';
    // Explicit storage offsets measured in the read-only reference profile.
    // These are test selectors, never automatic world/body selection policy.
    if(name=="maps/game/trainyard/trainyard"||name=="maps/game/mtw/mtw_ware") {
        const bool train=name=="maps/game/trainyard/trainyard";
        const auto selected=DecodeHavokTriangleMeshes(bytes,{train?360832u:45360u});
        Need(selected.completeBufferCoverage&&selected.decodedTriangles==(train?14238u:1650u)&&
             selected.declaredSubparts==(train?2919u:247u)&&selected.decodedVertices==(train?17983u:1740u),
             "Measured named-map triangle profile changed");
        CollisionVec3 selectedLo={1e30,1e30,1e30},selectedHi={-1e30,-1e30,-1e30};
        for(const auto &storage:selected.storages)for(const auto &part:storage.subparts)for(const auto &v:part.vertices) {
            const auto p=v.unscaledGamePosition;
            selectedLo.x=std::min(selectedLo.x,p.x);selectedLo.y=std::min(selectedLo.y,p.y);selectedLo.z=std::min(selectedLo.z,p.z);
            selectedHi.x=std::max(selectedHi.x,p.x);selectedHi.y=std::max(selectedHi.y,p.y);selectedHi.z=std::max(selectedHi.z,p.z);
        }
        if(!train)Need(Near(selectedLo.x,-4127.999853900099)&&Near(selectedLo.y,-4271.9998697596275)&&
             Near(selectedLo.z,-207.99999161968083)&&Near(selectedHi.x,-2792.7548866572342)&&
             Near(selectedHi.y,-2192.7516666922984)&&Near(selectedHi.z,184.99999534426712),
             "Warehouse triangle bounds differ from independent checked metadata probe");
        std::cout << "SelectedReferenceStorage offset=" << (train?360832:45360) << " subparts=" << selected.declaredSubparts
            << " vertices=" << selected.decodedVertices << " triangles=" << selected.decodedTriangles << '\n';
        std::cout << "SelectedReferenceUnscaledBounds " << selectedLo.x << ' ' << selectedLo.y << ' ' << selectedLo.z
            << " / " << selectedHi.x << ' ' << selectedHi.y << ' ' << selectedHi.z << '\n';
    }
}
}
int main(int argc,char **argv) {
    try {
        Synthetic();const bool survey=argc>1&&std::string(argv[1])=="--survey";int failures=0;
        for(int i=survey?2:1;i<argc;++i) {
            try {Original(argv[i]);}
            catch(const std::exception &e){if(!survey)throw;++failures;std::cout << "WOLF2009_HAVOK_TRIANGLE_REAL_ERROR argument=" << i-1 << " reason=" << e.what() << '\n';}
            if(survey)std::cerr << "Triangle buffer survey " << i-1 << '/' << argc-2 << '\n';
        }
        return failures?1:0;
    }catch(const std::exception &e){std::cerr << "Havok triangle test: " << e.what() << '\n';return 1;}
}

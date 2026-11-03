// SPDX-License-Identifier: GPL-3.0-or-later
#include "HavokTriangleQuery.h"
#include "MapPhysics.h"
#include "WorldTriangleGeometry.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace wolf2009;
int checks=0;
void Need(bool value,const char* text) { if (!value) throw std::runtime_error(text); ++checks; }
bool Near(double a,double b) { return std::abs(a-b)<1e-6; }
void U16(unsigned char* p,std::uint32_t value) { p[0]=value; p[1]=value>>8; }
void U32(unsigned char* p,std::uint32_t value) { for (int i=0;i<4;++i) p[i]=value>>(8*i); }
std::uint32_t Bits(float value) { std::uint32_t word; std::memcpy(&word,&value,4); return word; }
void F32(unsigned char* p,float value) { U32(p,Bits(value)); }
void Array(HavokTriangleRawArray& a,unsigned field,unsigned width,unsigned count) {
    a.fieldOffset=field; a.elementBytes=width; a.count=count; a.hasPointer=count!=0;
    a.bytes.assign(std::size_t(width)*count,0); a.targetDataOffset=1000+field*16;
}
struct Fixture {
    HavokTriangleStorage storage;
    HavokTriangleSubpart part;
    Fixture() {
        storage.sourceStorageOffset=64; part.sourceStorageOffset=64; part.sourceRecordOffset=512;
        storage.rawScaleBits={{Bits(1),Bits(1),Bits(1),Bits(1)}}; storage.rawHeader[184]=6;
        U32(part.rawRecord.data()+16,4); U32(part.rawRecord.data()+24,16); U32(part.rawRecord.data()+28,6);
        U32(part.rawRecord.data()+52,6); part.rawRecord[56]=1;
        part.rawRecord[1]=1; U16(part.rawRecord.data()+2,4); U16(part.rawRecord.data()+8,1);
        const unsigned fields[]={8,20,32,44,56,68},widths[]={16,2,4,1,4,2},counts[]={6,12,0,4,3,0};
        for (int i=0;i<6;++i) Array(part.arrays[i],fields[i],widths[i],counts[i]);
        const float xyz[6][3]={{0,0,0},{1,0,0},{0,1,0},{2,0,0},{0,2,0},{2,2,0}};
        for (int v=0;v<6;++v) for (int axis=0;axis<3;++axis)
            F32(part.arrays[0].bytes.data()+v*16+axis*4,xyz[v][axis]*.0254f);
        for (int row=0;row<4;++row) for (int v=0;v<3;++v)
            U16(part.arrays[1].bytes.data()+row*6+v*2,row==3 ? v+3 : v);
        U32(part.arrays[4].bytes.data(),0x80000009u);
        U32(part.arrays[4].bytes.data()+4,0x12345678u);
        U32(part.arrays[4].bytes.data()+8,8);
        // Native finish-loaded rebinding uses owned arrays, not these stale
        // serialized pointer words. They must not become host addresses.
        for (int field : {4,12,20,48}) U32(part.rawRecord.data()+field,0xdeadbeefu);
    }
    HavokTriangleQueryResult Query(std::size_t index=0) const {
        return ResolveHavokTriangleQuery(storage,part,index);
    }
};
bool Hit(const HavokTriangleQueryResult& t,double x,double y,bool back=false) {
    return RayCastHavokTriangleSegment(t,{x,y,back?-1.0:1.0},{x,y,back?1.0:-1.0}).status==HavokTriangleRayStatus::Hit;
}
void Synthetic() {
    Fixture f; const auto raw=f.part.rawRecord; const auto bytes=f.part.arrays[1].bytes;
    const auto q=f.Query();
    Need(q.geometryStatus==HavokTriangleQueryStatus::Resolved && q.materialStatus==HavokTriangleQueryStatus::Resolved &&
         q.rawCollisionFilterInfo==0x80000009u && q.flatNativeSurfaceCandidate,"Default independent query profile");
    Need(q.indices==std::array<std::uint32_t,3>{{0,1,2}} && Near(q.positions[1].x,1),"Native triangle indices/units");
    const auto front=RayCastHavokTriangleSegment(q,{.2,.2,1},{.2,.2,-1});
    const auto back=RayCastHavokTriangleSegment(q,{.2,.2,-1},{.2,.2,1});
    Need(front.status==HavokTriangleRayStatus::Hit && back.status==HavokTriangleRayStatus::Hit &&
         Near(front.fraction,.5) && Near(front.normal.z,1) && Near(back.normal.z,-1),"Two-sided ray/normal orientation");
    Need(f.part.rawRecord==raw && f.part.arrays[1].bytes==bytes,"Query mutated retained source data");
    for (const CollisionVec3& p : {CollisionVec3(.4,0),CollisionVec3(.4,.6),CollisionVec3(0,.4)})
        Need(Hit(q,p.x,p.y) && Hit(q,p.x,p.y,true),"One of the three triangle edges was rejected");
    for (const CollisionVec3& p : {CollisionVec3(.4,-.00005),CollisionVec3(.4,.60005),CollisionVec3(-.00005,.4)})
        Need(Hit(q,p.x,p.y),"Source relative edge epsilon was lost");
    for (const CollisionVec3& p : {CollisionVec3(.4,-.0002),CollisionVec3(.4,.6002),CollisionVec3(-.0002,.4)})
        Need(!Hit(q,p.x,p.y),"Point beyond source relative edge epsilon was accepted");
    Need(RayCastHavokTriangleSegment(q,{.2,.2,0},{.2,.2,1}).status==HavokTriangleRayStatus::Hit,
         "Source t0 endpoint was rejected");
    Need(RayCastHavokTriangleSegment(q,{.2,.2,0},{.2,.2,-1}).normal.z==-1,
         "Source on-plane normal sign rule was changed");
    Need(RayCastHavokTriangleSegment(q,{.2,.2,1},{.2,.2,0}).status==HavokTriangleRayStatus::Miss &&
         RayCastHavokTriangleSegment(q,{.2,.2,0},{.3,.3,0}).status==HavokTriangleRayStatus::Miss,
         "Source to-plane endpoint or coplanar rule was changed");
    HavokTriangleRayOptions options; options.maximumFraction=.5;
    Need(RayCastHavokTriangleSegment(q,{.2,.2,1},{.2,.2,-1},options).status==HavokTriangleRayStatus::Miss,
         "Maximum hit fraction must be strict");
    options.maximumFraction=.5001;
    Need(RayCastHavokTriangleSegment(q,{.2,.2,1},{.2,.2,-1},options).status==HavokTriangleRayStatus::Hit,
         "Fraction below maximum was rejected");
    auto degenerate=q; degenerate.positions[2]=degenerate.positions[1];
    Need(!Hit(degenerate,.2,.2),"Degenerate source triangle produced a ray hit");
    auto duplicate=f;
    F32(duplicate.part.arrays[0].bytes.data()+16,1.234f);
    F32(duplicate.part.arrays[0].bytes.data()+20,5.678f);
    F32(duplicate.part.arrays[0].bytes.data()+24,9.1011f);
    std::memcpy(duplicate.part.arrays[0].bytes.data()+32,duplicate.part.arrays[0].bytes.data()+16,16);
    Need(duplicate.Query().geometryStatus==HavokTriangleQueryStatus::Resolved &&
         !duplicate.Query().flatNativeSurfaceCandidate,"Duplicate non-axis points became a finite surface through FMA residue");
    auto scaled=f; scaled.storage.rawScaleBits[0]=Bits(2);
    Need(Near(scaled.Query().positions[1].x,2),"Source storage scale missing");
    f.part.rawRecord[57]=1;
    Need(f.Query(0).indices==std::array<std::uint32_t,3>{{0,1,2}} &&
         f.Query(1).indices==std::array<std::uint32_t,3>{{0,2,1}},"Alternating source index order");
    f.part.rawRecord[57]=2;
    Need(f.Query(2).indexSelectors==std::array<std::int32_t,3>{{0,3,4}} &&
         f.Query(2).indices==std::array<std::uint32_t,3>{{0,3,4}},"Flip byte was normalized to boolean/parity");
    f.part.rawRecord[57]=255;
    Need(f.Query(2).signedFlip==-1 && f.Query(2).indices==std::array<std::uint32_t,3>{{0,3,4}},
         "Signed flip mask was lost");
    Need(f.Query(3).geometryStatus==HavokTriangleQueryStatus::Invalid,"Flip read outside retained index array");
    f=Fixture(); f.part.rawRecord[1]=2; U16(f.part.rawRecord.data()+8,2);
    Array(f.part.arrays[5],68,2,4); U16(f.part.arrays[5].bytes.data(),1);
    Need(f.Query().rawCollisionFilterInfo==0x12345678u && f.Query().materialIndex==1,"UInt16 material indices");
    f.part.rawRecord[1]=0;
    Need(f.Query().effectiveMaterialIndexType==1 && f.Query().rawCollisionFilterInfo==0x80000009u,
         "Legacy finish-loaded material index upgrade");
    f.part.rawRecord[1]=3;
    Need(f.Query().materialStatus==HavokTriangleQueryStatus::Unsupported && Hit(f.Query(),.2,.2),
         "Unknown material blocked known geometry/ray");
    options=HavokTriangleRayOptions(); options.requireAnyRawFilterBits=true; options.rawFilterMask=8;
    Need(RayCastHavokTriangleSegment(f.Query(),{.2,.2,1},{.2,.2,-1},options).status==HavokTriangleRayStatus::Unavailable,
         "Requested filter guessed unknown material");
    Need(RayCastHavokTriangleSegment(q,{.2,.2,1},{.2,.2,-1},options).status==HavokTriangleRayStatus::Hit,
         "Caller raw-filter bit policy rejected matching word");
    options.rawFilterMask=16;
    Need(RayCastHavokTriangleSegment(q,{.2,.2,1},{.2,.2,-1},options).status==HavokTriangleRayStatus::FilteredOut,
         "Caller raw-filter bit policy ignored");
    f=Fixture(); Array(f.part.arrays[3],44,1,0);
    Need(f.Query().nullMaterialIndexBase && f.Query().rawCollisionFilterInfo==0 &&
         f.Query().materialStatus==HavokTriangleQueryStatus::Resolved,"Null material-index base must return raw zero");
    f=Fixture(); U16(f.part.rawRecord.data()+8,0); f.part.arrays[3].bytes[0]=2;
    Need(f.Query(3).rawCollisionFilterInfo==8,"Zero material-index stride is a constant binding");
    U16(f.part.rawRecord.data()+2,0xfffcu);
    Need(f.Query().materialStatus==HavokTriangleQueryStatus::Invalid && Hit(f.Query(),.2,.2),
         "Negative material stride escaped bounds or blocked independent ray");
    f=Fixture(); U32(f.storage.rawHeader.data()+176,4); U32(f.part.rawRecord.data()+60,0xfffffffeu);
    Need(f.Query(2).weldingIndex==0 && f.Query(2).weldingStatus==HavokTriangleQueryStatus::Unavailable,
         "Signed triangle offset/welding index");
    U32(f.part.rawRecord.data()+60,99);
    Need(f.Query().weldingStatus==HavokTriangleQueryStatus::Invalid && Hit(f.Query(),.2,.2),
         "Welding range silently accepted or changed the isolated ray");
    f=Fixture(); U32(f.storage.rawHeader.data()+176,4); f.storage.rawHeader[184]=0;
    Need(!f.Query().flatNativeSurfaceCandidate && Hit(f.Query(),.2,.2),"Welding contact policy leaked into leaf ray");
    f=Fixture(); F32(f.storage.rawHeader.data()+188,.0254f); F32(f.part.rawRecord.data()+40,.0254f);
    Need(!f.Query().flatNativeSurfaceCandidate && Near(f.Query().radiusGameUnits,1) &&
         Near(f.Query().extrusionGameUnits.z,1) && Hit(f.Query(),.2,.2) && !Hit(f.Query(),1.2,.1),
         "Radius/extrusion was lost or incorrectly inflated the original flat ray");
    U32(f.storage.rawHeader.data()+188,0x7fc00000u); U32(f.part.rawRecord.data()+40,0x7fc00000u);
    Need(!f.Query().radiusFinite && !f.Query().extrusionFinite && Hit(f.Query(),.2,.2),
         "Ignored nonfinite shape metadata changed finite triangle ray geometry");
    f=Fixture(); f.part.arrays[1].bytes.pop_back();
    Need(f.Query().geometryStatus==HavokTriangleQueryStatus::Invalid,"Truncated retained index array");
    f=Fixture(); U32(f.part.rawRecord.data()+60,0x80000000u);
    for (std::size_t invalid : {std::numeric_limits<std::size_t>::max(),
                               std::size_t(1)<<(sizeof(std::size_t)*8-1),std::size_t(4)})
        Need(f.Query(invalid).geometryStatus==HavokTriangleQueryStatus::Invalid,"Invalid index reached signed arithmetic");
    options=HavokTriangleRayOptions(); options.maximumFraction=std::numeric_limits<double>::infinity();
    Need(RayCastHavokTriangleSegment(q,{.2,.2,1},{.2,.2,-1},options).status==HavokTriangleRayStatus::InvalidInput,
         "Nonfinite ray fraction accepted");
    std::cout << "WOLF2009_HAVOK_TRIANGLE_QUERY_SYNTHETIC_OK checks=" << checks << '\n';
}
void Original(const char* path) {
    std::ifstream input(path,std::ios::binary); Need(bool(input),"Cannot open owner map");
    const auto archive=Archive::Scan(input); std::string name;
    for (const auto& chunk:archive.Chunks()) if (chunk.type=="PROC") for (const auto& entry:chunk.entries) {
        Need(name.empty(),"Ambiguous owner map resource"); name=entry.name;
    }
    Need(!name.empty(),"Owner map PROC missing");
    const auto source=ReadMapResource(input,archive,name,"HKXS",64u<<20);
    const auto world=DecodeWorldCollisionGeometry(source,name);
    const auto placed=DecodeWorldTriangleGeometry(source,world);
    std::size_t triangles=0,materials=0,opaqueWelding=0,invalidWelding=0,flat=0,nonzeroRadius=0,extruded=0;
    std::size_t rayPairs=0,degenerate=0,materialUnknown=0;
    for (const auto& occurrence:placed.parts) {
        const auto& storage=placed.buffers.storages[occurrence.storageIndex];
        const auto& part=storage.subparts[occurrence.subpartIndex];
        for (std::size_t i=0;i<part.declaredTriangles;++i) {
            const auto q=ResolveHavokTriangleQuery(storage,part,i); ++triangles;
            Need(q.geometryStatus==HavokTriangleQueryStatus::Resolved,"Owner triangle geometry query unresolved");
            if (q.materialStatus==HavokTriangleQueryStatus::Resolved) ++materials; else ++materialUnknown;
            if (q.weldingStatus==HavokTriangleQueryStatus::Unavailable) ++opaqueWelding;
            if (q.weldingStatus==HavokTriangleQueryStatus::Invalid) ++invalidWelding;
            if (q.flatNativeSurfaceCandidate) ++flat;
            if (!q.radiusFinite || q.radiusGameUnits!=0) ++nonzeroRadius;
            if (!q.extrusionFinite || q.extrusionGameUnits.x || q.extrusionGameUnits.y || q.extrusionGameUnits.z) ++extruded;
            const auto& a=q.positions[0]; const auto& b=q.positions[1]; const auto& c=q.positions[2];
            const CollisionVec3 u(b.x-a.x,b.y-a.y,b.z-a.z),v(c.x-a.x,c.y-a.y,c.z-a.z);
            // More precise independent area reference. This avoids creating
            // a false normal for coincident/collinear points through FMA.
            CollisionVec3 n(double(static_cast<long double>(u.y)*v.z-static_cast<long double>(u.z)*v.y),
                            double(static_cast<long double>(u.z)*v.x-static_cast<long double>(u.x)*v.z),
                            double(static_cast<long double>(u.x)*v.y-static_cast<long double>(u.y)*v.x));
            const double length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
            if (length==0) { ++degenerate; continue; }
            n={n.x/length,n.y/length,n.z/length};
            const CollisionVec3 center((a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3);
            const CollisionVec3 from(center.x+n.x*16,center.y+n.y*16,center.z+n.z*16);
            const CollisionVec3 to(center.x-n.x*16,center.y-n.y*16,center.z-n.z*16);
            const auto front=RayCastHavokTriangleSegment(q,from,to),back=RayCastHavokTriangleSegment(q,to,from);
            if (front.status!=HavokTriangleRayStatus::Hit || back.status!=HavokTriangleRayStatus::Hit ||
                !Near(front.fraction,.5) || !Near(back.fraction,.5)) {
                std::cerr.precision(17);
                std::cerr << "TRIANGLE_QUERY_RAY_CASE map=" << name << " storage=" << q.sourceStorageOffset
                          << " subpart=" << part.subpartIndex << " triangle=" << i << " rawflip=" << unsigned(q.rawFlipByte)
                          << " front=" << int(front.status) << " back=" << int(back.status)
                          << " fraction=" << front.fraction << ',' << back.fraction << " normalLength=" << length
                          << " a=(" << a.x << ',' << a.y << ',' << a.z << ") b=(" << b.x << ',' << b.y << ',' << b.z
                          << ") c=(" << c.x << ',' << c.y << ',' << c.z << ")\n";
            }
            Need(front.status==HavokTriangleRayStatus::Hit && back.status==HavokTriangleRayStatus::Hit &&
                 Near(front.fraction,.5) && Near(back.fraction,.5),"Owner finite two-sided centroid ray");
            ++rayPairs;
        }
    }
    Need(triangles==placed.reportedTriangles && placed.allReportedOccurrencesPlaced,"Selected-world triangle query scope");
    std::cout << "WOLF2009_HAVOK_TRIANGLE_QUERY_REAL_OK map=" << name << " triangles=" << triangles
              << " childFilterWords=" << materials << " unknownChildFilter=" << materialUnknown
              << " opaqueWelding=" << opaqueWelding << " invalidWelding=" << invalidWelding
              << " flatSurfaceCandidates=" << flat << " nonzeroRadius=" << nonzeroRadius
              << " extruded=" << extruded << " rayPairs=" << rayPairs << " degenerate=" << degenerate
              << " sweepPolicyVerified=0 worldContentsMappingVerified=0\n";
}
}
int main(int argc,char** argv) {
    try { Synthetic(); for (int i=1;i<argc;++i) Original(argv[i]); return 0; }
    catch (const std::exception& e) { std::cerr << "Havok triangle query: " << e.what() << '\n'; return 1; }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic virtual-cylinder format and cache checks, without retail assets.
#include "HavokPrimitiveSupport.h"
#include "HavokTestFixture.h"
#include "MapPhysics.h"
#include "SupportCollisionWorld.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
using namespace wolf2009;
using namespace wolf2009_test;
const auto profile=HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope;
int checks=0;
void Check(bool value,const char* message) { Need(value,message); ++checks; }
template<class F> void Reject(F f) {
    bool caught=false; try { f(); } catch (const std::exception&) { caught=true; }
    Check(caught,"Invalid cylinder/profile/cache accepted");
}
std::uint32_t Bits(float value) { std::uint32_t bits; std::memcpy(&bits,&value,4); return bits; }
bool Near(double a,double b,double tolerance=1e-6) { return std::abs(a-b)<=tolerance; }
struct Fixture : HavokFixture {
    Fixture() {
        types[64]="hkpRigidBody"; types[576]="hkpListShape";
        types[1024]="hkpCylinderShape"; types[1152]="hkpConvexTranslateShape";
        links[64+16]=576; links[64+112]=768; Text(data,768,"maps/synthetic/cylinder-envelope");
        U(data,64+44,3);
        for (int r=0;r<3;++r) for (int c=0;c<4;++c) F(data,64+224+r*16+c*4,r==c?1.f:0.f);
        F(data,64+332,1); F(data,64+348,1);
        links[576+24]=864; U(data,576+28,2); links[864]=1024; links[880]=1152;
        U(data,868,3); U(data,884,8); links[1152+24]=1024;
        F(data,1152+16,.05f); F(data,1152+32,.254f);
        F(data,1024+16,.05f); F(data,1024+20,1);
        U(data,1024+24,0x7fc00123u); // unused heightfield factor remains opaque
        F(data,1024+40,-1); F(data,1024+56,1);
        F(data,1024+64,1); F(data,1024+84,1);
        U(data,1024+44,0x7fc01234u); U(data,1024+92,0x7f800000u);
    }
};
struct Data {
    std::vector<unsigned char> bytes;
    WorldCollisionGeometry world;
    WorldTriangleGeometry triangles;
    HavokPrimitiveSupportDocument primitives;
    explicit Data(const Fixture& fixture=Fixture(),bool optIn=true) {
        bytes=fixture.File(); world=DecodeWorldCollisionGeometry(bytes,"maps/synthetic/cylinder-envelope");
        triangles=DecodeWorldTriangleGeometry(bytes,world);
        const std::vector<HavokPrimitiveSelection> selected={{1024,HavokPrimitiveKind::Cylinder}};
        primitives=optIn ? DecodeHavokPrimitiveSupport(bytes,selected,profile) : DecodeHavokPrimitiveSupport(bytes,selected);
    }
    SupportCollisionWorld Build(const SupportCollisionWorldLimits& limits=SupportCollisionWorldLimits()) const {
        return BuildSupportCollisionWorld(world,triangles,primitives,limits);
    }
};
void Synthetic() {
    Data data; Data legacy(Fixture(),false); const auto& record=data.primitives.shapes[0];
    Check(legacy.primitives.cylinderProfile==HavokCylinderGeometryProfile::Unresolved &&
          legacy.primitives.shapes[0].status==HavokPrimitiveSupportStatus::Unresolved &&
          legacy.primitives.shapes[0].corePoints.empty() && !legacy.Build().selectedGeometryComplete,
          "Old/default API silently enabled cylinder geometry");
    Check(data.primitives.sourceFingerprint==legacy.primitives.sourceFingerprint &&
          record.sourceRegion==legacy.primitives.shapes[0].sourceRegion &&
          data.primitives.cylinderProfile==profile && record.cylinder.profile==profile,
          "Geometry interpretation corrupted source identity or omitted its profile key");
    Check(record.status==HavokPrimitiveSupportStatus::Resolved && record.radiusKnown && record.radiusFinite &&
          record.corePoints.size()==32 && record.virtualVertexIds.size()==32 && Near(record.radiusGameUnits,.05f/.0254),
          "Explicit cylinder envelope/radius16 profile incomplete");
    Check(record.rawHeaderBytes==96 && record.cylinder.rawCylinderRadiusBits==Bits(1) &&
          record.cylinder.rawBaseRadiusFactorBits==0x7fc00123u && record.rawHeader[47]==0x7f && record.rawHeader[95]==0x7f,
          "Unused factor/W payloads discarded or interpreted as point/radius data");
    Check(record.cylinder.quantizationKBits==0x402bfe8du && record.cylinder.virtualVertexStepBits==0x3ebe8495u &&
          record.cylinder.observedRoundThresholdBits==0x3efae14eu && record.cylinder.hasRuntimeThresholdObservation &&
          !record.cylinder.matchesNativeSupportSelection,"Cylinder profile lost the observed algorithm distinction");
    for (std::size_t n=0;n<32;++n) {
        const auto id=record.virtualVertexIds[n]; Check(id==(n/2)*16+n%2,"Native virtual ID ordering changed");
        Check(Near(record.corePoints[n].z*.0254,id&128?-1:1),"Cylinder endpoint ID bit lost");
        Check((record.corePoints[n].x>0)==bool(id&32) && (record.corePoints[n].y>0)==bool(id&64),
              "Cylinder perpendicular sign bits swapped");
    }
    Check(Bits(float(record.corePoints[14].x*.0254))==0x3f7b87bau &&
          Bits(float(record.corePoints[14].y*.0254))==0x3e3e8495u &&
          Bits(float(record.corePoints[15].x*.0254))==0x3f54697fu &&
          Bits(float(record.corePoints[15].y*.0254))==0x3f0ee370u,
          "Verified nonuniform virtual ring replaced by guessed angles/analytic geometry");
    // Independent authored cylinder replay observed native ID0x71 at21deg;
    // another reachable ID0x70 has a larger projection. Keep this profile fact.
    const double angle=21*3.14159265358979323846/180;
    const auto projection=[&](std::size_t n) { return .0254*(std::cos(angle)*record.corePoints[n].x+
                                                           std::sin(angle)*record.corePoints[n].y); };
    Check(projection(14)-projection(15)>.0093 && projection(14)-projection(15)<.0094,
          "Envelope test accidentally asserted native max-dot selection equivalence");
    const auto support=data.Build();
    Check(support.shapes.size()==2 && support.selectedGeometryComplete && support.selectedHierarchyComplete &&
          !support.nativeRunnable && !support.entityBindingsResolved,"Explicit cylinder join lost selected geometry or claimed native world");
    Check(support.shapes[0].kind==SupportCollisionKind::Cylinder && support.shapes[1].kind==SupportCollisionKind::Cylinder &&
          support.shapes[0].cylinder.profile==profile && !support.shapes[0].cylinder.matchesNativeSupportSelection &&
          support.shapes[0].source.occurrence!=support.shapes[1].source.occurrence &&
          Near(support.shapes[1].worldBounds.minimum.x-support.shapes[0].worldBounds.minimum.x,10),
          "Shared cylinder occurrences, placement or semantic metadata collapsed");
    Check(support.sourceOmissions[0].resolution==SupportCollisionOmissionResolution::ImportedPrimitiveSupport &&
          support.shapes[0].filter.inheritedRawContents==3 && support.shapes[1].filter.inheritedRawContents==8,
          "Cylinder source join/filter provenance lost");
    auto bad=data; bad.primitives.cylinderProfile=HavokCylinderGeometryProfile::Unresolved; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].cylinder.profile=HavokCylinderGeometryProfile::Unresolved; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].cylinder.quantizationKBits^=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].cylinder.virtualVertexStepBits^=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].cylinder.observedRoundThresholdBits^=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].cylinder.matchesNativeSupportSelection=true; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].cylinder.rawCylinderRadiusBits^=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].cylinder.rawBaseRadiusFactorBits^=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].virtualVertexIds[0]=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].corePoints[0].x=std::nextafter(record.corePoints[0].x,0.0); Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].rawHeader[64]^=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[0].radiusGameUnits=1/.0254; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes.push_back(record); Reject([&]{bad.Build();});
    Fixture stage; F(stage.data,1024+48,9.584259033203125f); F(stage.data,1024+64,-.4038020670413971f);
    F(stage.data,1024+80,7.058307647705078f); F(stage.data,1024+20,1.8998898267745972f);
    const Data staged(stage);
    Check(Bits(float(staged.primitives.shapes[0].corePoints[14].x*.0254))==0x41353502u,
          "Cylinder float32 stages fused/reassociated into a different source point");
    const double regrouped=(-.4038020670413971*double(.9825397729873657f)+
        7.058307647705078*double(.18605263531208038f))*1.8998898267745972+9.584259033203125;
    Check(Bits(float(regrouped))==0x41353503u,"Float-stage witness no longer distinguishes regrouped arithmetic");
    Fixture zero; F(zero.data,1024+20,0); F(zero.data,1024+16,0); F(zero.data,1152+16,0);
    for (int k=0;k<3;++k) { F(zero.data,1024+32+k*4,float(k)); F(zero.data,1024+48+k*4,float(k)); }
    const Data degenerate(zero); const auto collapsed=degenerate.Build();
    Check(collapsed.shapes.size()==2 && collapsed.selectedGeometryComplete && collapsed.shapes[0].corePoints.size()==32 &&
          collapsed.shapes[0].radius==0 && collapsed.shapes[0].corePoints[0].z==collapsed.shapes[0].corePoints[31].z,
          "Degenerate rounded core/duplicate virtual IDs were normalized or rejected");
    Fixture mismatch; F(mismatch.data,1152+16,1); const auto wrongRadius=Data(mismatch).Build();
    Check(wrongRadius.shapes.size()==1 && !wrongRadius.selectedGeometryComplete && !wrongRadius.selectedHierarchyComplete,
          "Wrapper was compared with cylinder radius20 instead of convex radius16");
    bad=data; bad.world.omissions[1].hasWrapperRadiusMetadata=false;
    Check(!bad.Build().selectedGeometryComplete && !bad.Build().selectedHierarchyComplete,"Unknown cylinder hierarchy inferred");
    for (std::size_t field:{16u,20u,32u,36u,40u,48u,52u,56u,64u,68u,72u,80u,84u,88u}) {
        Fixture malformed; U(malformed.data,1024+field,0x7fc00000u); Reject([&]{Data test(malformed);});
    }
    for (std::size_t field:{16u,20u}) { Fixture malformed; F(malformed.data,1024+field,-1); Reject([&]{Data test(malformed);}); }
    Reject([&]{BuildHavokCylinderVirtualCore(record.rawHeader,HavokCylinderGeometryProfile::Unresolved);});
    Reject([&]{DecodeHavokPrimitiveSupport(data.bytes,{{1024,HavokPrimitiveKind::Cylinder}},static_cast<HavokCylinderGeometryProfile>(99));});
    HavokPrimitiveSupportLimits limits; limits.decodedBytes=data.primitives.decodedBytes-1;
    Reject([&]{DecodeHavokPrimitiveSupport(data.bytes,{{1024,HavokPrimitiveKind::Cylinder}},profile,limits);});
    limits=HavokPrimitiveSupportLimits(); limits.coordinateGameUnits=2;
    Reject([&]{BuildHavokCylinderVirtualCore(record.rawHeader,profile,limits);});
    limits=HavokPrimitiveSupportLimits(); limits.maximumRadiusGameUnits=.5;
    Reject([&]{BuildHavokCylinderVirtualCore(record.rawHeader,profile,limits);});
    SupportCollisionWorldLimits worldLimits; worldLimits.corePointsPerShape=31; Reject([&]{data.Build(worldLimits);});
    worldLimits=SupportCollisionWorldLimits(); worldLimits.totalCorePoints=63; Reject([&]{data.Build(worldLimits);});
    std::cout << "WOLF2009_CYLINDER_VIRTUAL_SYNTHETIC_OK checks=" << checks << '\n';
}
void Original(const char* path) {
    std::ifstream input(path,std::ios::binary); Check(bool(input),"Cannot open supplied cylinder map");
    const auto archive=Archive::Scan(input); std::string name;
    for (const auto& chunk:archive.Chunks()) if (chunk.type=="PROC") for (const auto& entry:chunk.entries) {
        Check(name.empty(),"Ambiguous source cylinder map"); name=entry.name;
    }
    const auto bytes=ReadMapResource(input,archive,name,"HKXS",64u<<20);
    const auto source=DecodeWorldCollisionGeometry(bytes,name); const auto triangles=DecodeWorldTriangleGeometry(bytes,source);
    std::map<std::uint32_t,HavokPrimitiveKind> unique; std::size_t expectedCylinders=0;
    for (const auto& o:source.omissions) {
        HavokPrimitiveKind kind;
        if (o.type=="hkpSphereShape") kind=HavokPrimitiveKind::Sphere;
        else if (o.type=="hkpCapsuleShape") kind=HavokPrimitiveKind::Capsule;
        else if (o.type=="hkpCylinderShape") { kind=HavokPrimitiveKind::Cylinder; ++expectedCylinders; }
        else continue;
        const auto inserted=unique.emplace(o.sourceShapeOffset,kind);
        Check(inserted.second || inserted.first->second==kind,"Source cylinder object kind changed");
    }
    std::vector<HavokPrimitiveSelection> selection;
    for (const auto& entry:unique) selection.emplace_back(entry.first,entry.second);
    const auto primitives=DecodeHavokPrimitiveSupport(bytes,selection,profile);
    const auto world=BuildSupportCollisionWorld(source,triangles,primitives);
    std::size_t cylinders=0,remaining=0;
    for (const auto& shape:world.shapes) if (shape.kind==SupportCollisionKind::Cylinder) {
        ++cylinders;
        Check(shape.cylinder.profile==profile && !shape.cylinder.matchesNativeSupportSelection && shape.corePoints.size()==32,
              "Source cylinder profile/algorithm fact missing");
    }
    for (const auto& o:world.sourceOmissions) if (o.source.type=="hkpCylinderShape" &&
        o.resolution==SupportCollisionOmissionResolution::Unresolved) ++remaining;
    Check(cylinders+remaining==expectedCylinders && !world.nativeRunnable && !world.entityBindingsResolved,
          "Source cylinder occurrence disappeared or geometry became a native scene");
    std::cout << "WOLF2009_CYLINDER_VIRTUAL_SOURCE_OK map=" << name << " supportShapes=" << world.shapes.size()
        << " corePoints=" << world.totalCorePoints << " cylinderOccurrences=" << expectedCylinders << " importedCylinders=" << cylinders
        << " unresolvedCylinders=" << remaining << " geometryComplete=" << world.selectedGeometryComplete
        << " hierarchyComplete=" << world.selectedHierarchyComplete << " contentsComplete=" << world.selectedContentsComplete
        << " coverageComplete=" << world.selectedCoverageComplete << " profile=RetailDefault16VirtualEnvelope"
        << " matchesNativeSupportSelection=0 nativeRunnable=0\n";
    for (const auto& unresolved:world.unresolved) std::cout << "Unresolved " << unresolved.first << ' ' << unresolved.second << '\n';
}
} // namespace
int main(int argc,char** argv) {
    try {
        Synthetic();
        for (int i=1;i<argc;i+=2) { Check(std::string(argv[i])=="--source-map" && i+1<argc,"Usage: --source-map owner.mpk"); Original(argv[i+1]); }
        return 0;
    } catch (const std::exception& e) { std::cerr << "Cylinder virtual test failed: " << e.what() << '\n'; return 1; }
}

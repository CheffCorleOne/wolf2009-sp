// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupportCollisionWorld.h"
#include "HavokTestFixture.h"
#include "MapPhysics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace wolf2009;
int checks=0;
void Check(bool b,const char* message) { if (!b) throw std::runtime_error(message); ++checks; }
bool Near(double a,double b) { return std::abs(a-b)<1e-6; }
template<class F> void Reject(F f) {
    bool rejected=false; try { f(); } catch (const std::exception&) { rejected=true; }
    Check(rejected,"Malformed support world accepted");
}
void U16(unsigned char* p,std::uint32_t n) { p[0]=n; p[1]=n>>8; }
void U32(unsigned char* p,std::uint32_t n) { for (int k=0;k<4;++k) p[k]=n>>(8*k); }
std::uint32_t Bits(float f) { std::uint32_t word; std::memcpy(&word,&f,4); return word; }
void F32(unsigned char* p,float f) { U32(p,Bits(f)); }
void Array(HavokTriangleRawArray& a,unsigned field,unsigned width,unsigned count) {
    a.fieldOffset=field; a.elementBytes=width; a.count=count; a.hasPointer=count!=0;
    a.targetDataOffset=2000+field*8; a.bytes.assign(std::size_t(width)*count,0);
}
struct Fixture {
    WorldCollisionGeometry world;
    HavokTriangleMeshDocument buffers;
    Fixture() {
        world.sourceBytes=buffers.sourceBytes=8192; world.sourceFingerprint=buffers.sourceFingerprint=123;
        world.selectedBodyName="maps/synthetic/support"; world.sourceBodyOffset=64; world.rootShapeOffset=576;
        world.identityBodyPoseVerified=world.geometryComplete=world.contentsComplete=world.coverageComplete=true;
    }
    WorldCollisionPrimitive& Box(std::uint64_t occurrence=1) {
        WorldCollisionPrimitive p; p.occurrence=occurrence;
        p.local.sourceShapeOffset=1024; p.local.sourceRegion=100+occurrence;
        p.local.rawContents=0x80000003u; p.local.contentsKnown=true; p.local.radius=.5;
        for (int b=0;b<8;++b) p.local.vertices.push_back({b&1?1.0:-1.0,b&2?2.0:-2.0,b&4?3.0:-3.0});
        p.supportArrayComplete=true;
        p.placement.axis[0]={0,1,0}; p.placement.axis[1]={-1,0,0}; p.placement.translation={10,20,30};
        WorldCollisionTransformRecord transform; transform.local=p.placement; p.transforms.push_back(transform);
        p.world=p.local;
        for (auto& v:p.world.vertices) v=TransformCollisionPoint(p.placement,v);
        world.primitives.push_back(p); return world.primitives.back();
    }
    void Missing(const std::string& type,std::uint32_t shape,std::uint64_t occurrence,
                 const CollisionRigidTransform& placement,std::size_t sub=static_cast<std::size_t>(-1),
                 std::size_t triangles=0,bool known=true,std::uint32_t contents=0x80000003u) {
        WorldCollisionOmission o; o.type=type; o.sourceShapeOffset=shape; o.occurrence=occurrence;
        o.placement=placement; o.sourceSubpartIndex=sub; o.triangles=triangles; o.contentsKnown=known; o.rawContents=contents;
        world.omissions.push_back(o); ++world.unsupported[type]; world.geometryComplete=world.coverageComplete=false;
    }
    void Triangle(std::uint64_t occurrence=2) {
        if (buffers.storages.empty()) {
            HavokTriangleStorage storage; storage.sourceStorageOffset=576; storage.identityScale=storage.finiteScale=true;
            storage.rawScaleBits={{Bits(1),Bits(1),Bits(1),Bits(1)}};
            storage.rawHeader[184]=6; F32(storage.rawHeader.data()+188,.00254f);
            HavokTriangleSubpart sub; sub.sourceStorageOffset=576; sub.sourceRecordOffset=704;
            sub.sourceOwnedStorageOffset=1200; sub.sourceRegion=777; sub.declaredTriangles=1; sub.declaredVertices=3;
            sub.buffersDecoded=sub.identityScaleBufferProfile=sub.finiteExtrusion=true;
            U32(sub.rawRecord.data()+16,1); U32(sub.rawRecord.data()+24,16); U32(sub.rawRecord.data()+28,3);
            U32(sub.rawRecord.data()+52,6); sub.rawRecord[56]=1; sub.rawRecord[1]=1;
            U16(sub.rawRecord.data()+2,4); U16(sub.rawRecord.data()+8,1);
            const unsigned fields[]={8,20,32,44,56,68},widths[]={16,2,4,1,4,2},counts[]={3,3,0,1,1,0};
            for (int k=0;k<6;++k) Array(sub.arrays[k],fields[k],widths[k],counts[k]);
            const float xyz[3][3]={{0,0,0},{.0508f,0,0},{0,.0762f,0}};
            for (int v=0;v<3;++v) {
                HavokTriangleVertex vertex;
                vertex.unscaledGamePosition={double(xyz[v][0])/.0254,double(xyz[v][1])/.0254,double(xyz[v][2])/.0254};
                sub.vertices.push_back(vertex);
                for (int k=0;k<3;++k) F32(sub.arrays[0].bytes.data()+v*16+k*4,xyz[v][k]);
                U16(sub.arrays[1].bytes.data()+v*2,v);
            }
            U32(sub.arrays[4].bytes.data(),0x12345678u);
            HavokTriangle tri; tri.indices[0]=0; tri.indices[1]=1; tri.indices[2]=2; sub.triangles.push_back(tri);
            storage.subparts.push_back(sub); buffers.storages.push_back(storage);
        }
        CollisionRigidTransform placement; placement.translation={double(occurrence)*10,4,6};
        Missing("storage-triangle-subpart",576,occurrence,placement,0,1,false,0);
    }
    WorldTriangleGeometry Triangles() const { return PlaceWorldTriangleGeometry(world,buffers); }
    SupportCollisionWorld Build(const SupportCollisionWorldLimits& limits=SupportCollisionWorldLimits()) const {
        return BuildSupportCollisionWorld(world,Triangles(),limits);
    }
};
struct PrimitiveFixture : wolf2009_test::HavokFixture {
    PrimitiveFixture() {
        using namespace wolf2009_test;
        types[64]="hkpRigidBody"; types[576]="hkpListShape";
        types[1024]="hkpSphereShape"; types[1088]="hkpConvexTranslateShape";
        types[1152]="hkpCapsuleShape"; types[1280]="hkpCylinderShape";
        types[1408]="hkpConvexTransformShape";
        links[64+16]=576; links[64+112]=768; Text(data,768,"maps/synthetic/primitives");
        U(data,64+44,0x80000003u);
        for (int r=0;r<3;++r) for (int c=0;c<4;++c) F(data,64+224+r*16+c*4,r==c?1.0f:0.0f);
        F(data,64+320+12,1); F(data,64+336+12,1);
        links[576+24]=864; U(data,576+28,4);
        const unsigned children[]={1024,1088,1280,1408};
        for (int i=0;i<4;++i) { links[864+i*16]=children[i]; U(data,864+i*16+4,3); }
        F(data,1024+16,.0254f); F(data,1280+16,.0508f);
        F(data,1088+16,.0127f); links[1088+24]=1152; F(data,1088+32,.254f);
        F(data,1152+16,.0127f); F(data,1152+48+8,.0508f);
        U(data,1152+32+12,0x7fc01234u); U(data,1152+48+12,0x80000000u);
        F(data,1408+16,.0127f); links[1408+24]=1088;
        F(data,1408+32+4,1); F(data,1408+48,-1); F(data,1408+64+8,1);
        F(data,1408+80,.762f);
    }
};
struct PrimitiveCase {
    WorldCollisionGeometry world;
    WorldTriangleGeometry triangles;
    HavokPrimitiveSupportDocument primitives;
    SupportCollisionWorld Build(const SupportCollisionWorldLimits& limits=SupportCollisionWorldLimits()) const {
        return BuildSupportCollisionWorld(world,triangles,primitives,limits);
    }
};
PrimitiveCase PrimitiveData(const PrimitiveFixture& fixture=PrimitiveFixture()) {
    const auto bytes=fixture.File(); PrimitiveCase out;
    out.world=DecodeWorldCollisionGeometry(bytes,"maps/synthetic/primitives");
    out.triangles=DecodeWorldTriangleGeometry(bytes,out.world);
    out.primitives=DecodeHavokPrimitiveSupport(bytes,{{1024,HavokPrimitiveKind::Sphere},
        {1152,HavokPrimitiveKind::Capsule},{1280,HavokPrimitiveKind::Cylinder}});
    return out;
}
void PrimitiveSynthetic() {
    auto data=PrimitiveData();
    Check(data.world.omissions.size()==4 && data.world.omissions[0].hasWrapperRadiusMetadata &&
          data.world.omissions[0].wrapperRadiusCount==0 && data.world.omissions[1].wrapperRadiusCount==1 &&
          data.world.omissions[3].wrapperRadiusCount==2 &&
          Near(data.world.omissions[3].minimumWrapperRadius,.5) && Near(data.world.omissions[3].maximumWrapperRadius,.5),
          "World omissions did not retain the full typed wrapper-radius summary");
    Check(data.world.rootRawFilterKnown && data.world.rootRawFilterInfo==0x80000003u,
          "Selected rigid-body root filter source word was not retained");
    const auto legacy=BuildSupportCollisionWorld(data.world,data.triangles);
    Check(legacy.shapes.empty() && !legacy.selectedGeometryComplete && legacy.unresolved.at("hkpCapsuleShape")==2,
          "Legacy overload changed its primitive omission behavior");
    const auto result=data.Build();
    Check(result.shapes.size()==3 && result.totalCorePoints==5 && !result.selectedGeometryComplete &&
          result.selectedHierarchyComplete && result.unresolved.at("hkpCylinderShape")==1 &&
          !result.unresolved.count("hkpSphereShape") && !result.unresolved.count("hkpCapsuleShape"),
          "Exact primitive joins lost geometry or enabled unresolved cylinder support");
    Check(result.rootRawFilterKnown && result.rootRawFilterInfo==0x80000003u &&
          result.shapes[0].filter.inheritedRawContents==3 && !result.nativeRunnable && !result.entityBindingsResolved,
          "Raw root filter was conflated with inherited contents or runtime bindings");
    Check(result.shapes[0].kind==SupportCollisionKind::Sphere && result.shapes[0].corePoints.size()==1 &&
          Near(result.shapes[0].radius,1) && Near(result.shapes[0].worldBounds.minimum.x,-1),
          "Sphere support core/radius/rounded bounds changed during import");
    const auto& a=result.shapes[1]; const auto& b=result.shapes[2];
    Check(a.kind==SupportCollisionKind::Capsule && b.kind==SupportCollisionKind::Capsule &&
          a.corePoints.size()==2 && b.corePoints.size()==2 && Near(a.radius,.5) && Near(b.radius,.5) &&
          a.source.sourceShapeOffset==b.source.sourceShapeOffset && a.source.occurrence!=b.source.occurrence &&
          a.source.sourceRegion!=b.source.sourceRegion && Near(a.worldBounds.minimum.x,9.5) &&
          Near(b.worldBounds.minimum.x,29.5) && Near(b.worldBounds.minimum.y,9.5),
          "Shared capsule placements were merged, transformed twice or had their radii added");
    Check(result.sourceOmissions[0].resolution==SupportCollisionOmissionResolution::ImportedPrimitiveSupport &&
          result.sourceOmissions[1].shapeCount==1 && result.sourceOmissions[2].resolution==SupportCollisionOmissionResolution::Unresolved &&
          result.sourceOmissions[3].firstShape==2,
          "Primitive source omission resolution/ranges were lost");

    auto bad=data; bad.world.omissions[1].hasWrapperRadiusMetadata=false;
    const auto unknown=bad.Build();
    Check(unknown.shapes.size()==2 && !unknown.selectedHierarchyComplete && !unknown.selectedGeometryComplete &&
          unknown.unresolved.at("unknown-primitive-wrapper-radii")==1,
          "Manual/legacy omission acquired an assumed radius hierarchy");
    PrimitiveFixture mismatch;
    wolf2009_test::F(mismatch.data,1088+16,.0254f);
    const auto mismatched=PrimitiveData(mismatch).Build();
    Check(mismatched.shapes.size()==1 && !mismatched.selectedHierarchyComplete &&
          mismatched.unresolved.at("unverified-wrapper-radius")==2,
          "A wrapper/leaf radius mismatch was ignored or accumulated into geometry");
    auto rawZero=PrimitiveFixture(); wolf2009_test::U(rawZero.data,64+44,0);
    const auto zero=PrimitiveData(rawZero).Build();
    Check(zero.rootRawFilterKnown && zero.rootRawFilterInfo==0 && zero.shapes[0].filter.inheritedRawContents==3,
          "Raw root zero was changed into an unknown/default SOLID policy");
    auto degenerate=PrimitiveFixture();
    wolf2009_test::F(degenerate.data,1152+48+8,0);
    const auto repeated=PrimitiveData(degenerate).Build();
    const auto& ends=repeated.shapes[1].corePoints;
    Check(ends.size()==2 && Near(ends[0].x,ends[1].x) && Near(ends[0].y,ends[1].y) && Near(ends[0].z,ends[1].z),
          "Degenerate capsule endpoints were deduplicated during the join");
    bad=data; bad.world.omissions[1].contentsKnown=false;
    Check(!bad.Build().selectedContentsComplete && bad.Build().shapes.size()==3,
          "Unknown primitive inherited contents were replaced or suppressed geometry");
    bad=data; bad.world.omissions[1].maximumWrapperRadius=2; bad.world.omissions[1].minimumWrapperRadius=3;
    Reject([&]{bad.Build();});
    bad=data; bad.world.omissions[0].minimumWrapperRadius=bad.world.omissions[0].maximumWrapperRadius=1;
    Reject([&]{bad.Build();});
    bad=data; bad.primitives.sourceFingerprint++; Reject([&]{bad.Build();});
    bad=data; bad.primitives.sourceBytes++; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes.push_back(bad.primitives.shapes[0]); Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes.erase(bad.primitives.shapes.begin()+1); Reject([&]{bad.Build();});
    bad=data; bad.world.omissions.erase(bad.world.omissions.begin()+2); Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[1].corePoints[0].x++; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[1].radiusGameUnits+=.25; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[1].rawHeader[16]^=1; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[1].sourceRegion++; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[1].sourceType="hkpSphereShape"; Reject([&]{bad.Build();});
    bad=data; bad.primitives.shapes[2].status=HavokPrimitiveSupportStatus::Resolved; Reject([&]{bad.Build();});
    SupportCollisionWorldLimits limits;
    limits.shapes=2; Reject([&]{data.Build(limits);});
    limits=SupportCollisionWorldLimits(); limits.totalCorePoints=4; Reject([&]{data.Build(limits);});
    limits=SupportCollisionWorldLimits(); limits.corePointsPerShape=1; Reject([&]{data.Build(limits);});
}
void Synthetic() {
    Fixture f; f.Box(); const auto box=f.Build();
    Check(box.shapes.size()==1 && box.selectedGeometryComplete && box.selectedCoverageComplete &&
          !box.nativeRunnable && !box.entityBindingsResolved,"Source selection became a runnable/bound physics world");
    const auto& b=box.shapes[0];
    Check(b.corePoints.size()==8 && b.radius==.5 && Near(b.worldBounds.minimum.x,7.5) &&
          Near(b.worldBounds.maximum.y,21.5) && Near(b.worldBounds.minimum.z,26.5),"Box radius/rigid placement/AABB wrong");
    Check(b.worldBounds.minimum.x<=7.5 && b.worldBounds.maximum.y>=21.5 &&
          b.filter.inheritedRawContents==0x80000003u && b.filter.inheritedContentsKnown && !b.filter.childFilterKnown,
          "Rounded AABB is not conservative or source contents/filter conflated");
    auto point=f;
    for (auto& v:point.world.primitives[0].local.vertices) v={0,0,0};
    for (auto& v:point.world.primitives[0].world.vertices) v=point.world.primitives[0].placement.translation;
    const auto roundedPoint=point.Build();
    Check(roundedPoint.selectedGeometryComplete && roundedPoint.shapes[0].corePoints.size()==8 &&
          Near(roundedPoint.shapes[0].worldBounds.minimum.x,9.5) &&
          Near(roundedPoint.shapes[0].worldBounds.maximum.z,30.5),"Zero-dimensional rounded box core rejected");
    f.world.otherSourceRigidBodies=3; const auto selected=f.Build();
    Check(selected.selectedCoverageComplete && !selected.allSourceRigidBodiesSelected && selected.otherSourceRigidBodies==3,
          "Selected-body coverage generalized to all rigid bodies");
    f=Fixture(); auto& hull=f.Box(); hull.kind=WorldCollisionPrimitiveKind::ConvexVertices;
    hull.local.vertices.resize(1); hull.world.vertices.resize(1); hull.additionalSupportVertices={{100,0,0},{100,0,0},{-100,0,0}};
    hull.volumeProfileVerified=hull.sourcePlanesConsistent=false;
    ConvexContactPlane wrong; wrong.normal={1,0,0}; wrong.distance=1000000; hull.local.planes.push_back(wrong);
    f.Missing("unverified-source-plane-residual",1024,1,hull.placement);
    f.Missing("unverified-lower-dimensional-convex",1024,1,hull.placement);
    const auto rawPlanes=f.world.primitives[0].local.planes;
    const auto support=f.Build();
    Check(support.selectedGeometryComplete && support.selectedCoverageComplete && support.shapes.size()==1 &&
          support.shapes[0].declaredCorePoints==1 && support.shapes[0].corePoints.size()==4 &&
          Near(support.shapes[0].worldBounds.maximum.y,120.5),"Padded/degenerate support core blocked by independent ray planes");
    Check(support.sourceOmissions[0].resolution==SupportCollisionOmissionResolution::IndependentRayPlaneDiagnostic &&
          support.sourceOmissions[1].resolution==SupportCollisionOmissionResolution::IndependentVolumeDiagnostic &&
          f.world.primitives[0].local.planes[0].distance==rawPlanes[0].distance,"Ray plane diagnostics mutated or lost");
    f=Fixture(); f.Box(1); f.Box(2); f.world.primitives[1].placement.translation.x+=50;
    f.world.primitives[1].transforms[0].local=f.world.primitives[1].placement;
    for (std::size_t i=0;i<8;++i) f.world.primitives[1].world.vertices[i]=
        TransformCollisionPoint(f.world.primitives[1].placement,f.world.primitives[1].local.vertices[i]);
    const auto repeated=f.Build();
    Check(repeated.shapes.size()==2 && repeated.shapes[0].source.sourceShapeOffset==repeated.shapes[1].source.sourceShapeOffset &&
          repeated.shapes[0].source.occurrence!=repeated.shapes[1].source.occurrence &&
          repeated.shapes[0].worldBounds.minimum.x!=repeated.shapes[1].worldBounds.minimum.x,
          "Shared primitive occurrences were deduplicated");
    auto bad=f; bad.world.primitives[1]=bad.world.primitives[0]; Reject([&]{bad.Build();});
    bad=f; bad.world.primitives[0].world.rawContents=4; Reject([&]{bad.Build();});
    bad=f; bad.world.primitives[0].world.vertices[0].x+=1; Reject([&]{bad.Build();});
    bad=f; bad.world.primitives[0].transforms[0].local.translation.x+=1; Reject([&]{bad.Build();});
    bad=f; bad.world.primitives[0].placement.axis[0].x=2; Reject([&]{bad.Build();});
    bad=f; bad.world.primitives[0].additionalSupportVertices={{std::numeric_limits<double>::quiet_NaN(),0,0}};
    Reject([&]{bad.Build();});
    bad=f; bad.world.primitives[0].supportArrayComplete=false;
    Check(!bad.Build().selectedGeometryComplete && bad.Build().shapes.size()==1,"Incomplete source lanes imported");
    bad=f; bad.world.primitives[0].radiusHierarchyVerified=false;
    Check(!bad.Build().selectedHierarchyComplete && !bad.Build().selectedGeometryComplete,"Unknown radius hierarchy ignored");
    bad=f; bad.world.primitives[0].local.contentsKnown=bad.world.primitives[0].world.contentsKnown=false;
    Check(bad.Build().selectedGeometryComplete && !bad.Build().selectedContentsComplete,"Unknown raw contents guessed");
    f=Fixture(); f.Triangle(); f.Triangle(3); const auto triangles=f.Build();
    Check(triangles.shapes.size()==2 && triangles.importedTriangles==2 && triangles.expectedTriangles==2 &&
          triangles.selectedGeometryComplete && triangles.selectedChildFiltersComplete &&
          !triangles.selectedContentsComplete && !triangles.selectedCoverageComplete && !triangles.nativeRunnable,
          "Triangle support/contents coverage semantics wrong");
    Check(triangles.shapes[0].corePoints.size()==3 && Near(triangles.shapes[0].radius,.1) &&
          triangles.shapes[0].filter.rawChildFilterInfo==0x12345678u &&
          triangles.shapes[0].filter.childFilterKnown && !triangles.shapes[0].filter.inheritedContentsKnown &&
          triangles.shapes[0].source.sourceRegion!=triangles.shapes[1].source.sourceRegion &&
          triangles.shapes[0].source.occurrence!=triangles.shapes[1].source.occurrence &&
          triangles.shapes[0].worldBounds.minimum.x!=triangles.shapes[1].worldBounds.minimum.x,
          "Triangle radius/raw filter/provenance/duplicate placement lost");
    bad=f; auto placed=bad.Triangles(); placed.parts.push_back(placed.parts[0]);
    Reject([&]{BuildSupportCollisionWorld(bad.world,placed);});
    placed=bad.Triangles(); placed.parts.pop_back(); Reject([&]{BuildSupportCollisionWorld(bad.world,placed);});
    placed=bad.Triangles(); placed.sourceBodyOffset++; Reject([&]{BuildSupportCollisionWorld(bad.world,placed);});
    placed=bad.Triangles(); placed.buffers.sourceFingerprint++; Reject([&]{BuildSupportCollisionWorld(bad.world,placed);});
    placed=bad.Triangles(); placed.parts[0].placement.translation.x++;
    Reject([&]{BuildSupportCollisionWorld(bad.world,placed);});
    placed=bad.Triangles(); placed.parts[0].worldVertices[0].x++;
    Reject([&]{BuildSupportCollisionWorld(bad.world,placed);});
    placed=bad.Triangles(); placed.parts[0].positionsPlaced=false;
    const auto unplaced=BuildSupportCollisionWorld(bad.world,placed);
    Check(!unplaced.selectedGeometryComplete && !unplaced.selectedChildFiltersComplete &&
          !unplaced.selectedContactMetadataComplete,"Unvisited triangle metadata advertised as complete");
    bad=f; bad.world.omissions.push_back(bad.world.omissions[0]); Reject([&]{bad.Build();});
    bad=f; auto& sub=bad.buffers.storages[0].subparts[0];
    sub.rawRecord[1]=3; const auto material=bad.Build();
    Check(material.shapes.size()==2 && material.selectedGeometryComplete && !material.selectedChildFiltersComplete &&
          !material.shapes[0].filter.childFilterKnown,"Unknown child material suppressed geometry or became a known filter");
    bad=f; bad.buffers.storages[0].rawHeader[184]=0; const auto welded=bad.Build();
    Check(welded.selectedGeometryComplete && !welded.selectedContactMetadataComplete &&
          !welded.shapes[0].triangle.contactMetadataQualified,"Unknown welding policy discarded or silently enabled");
    bad=f; U32(bad.buffers.storages[0].rawHeader.data()+176,1); const auto opaque=bad.Build();
    Check(opaque.selectedGeometryComplete && !opaque.selectedContactMetadataComplete &&
          opaque.shapes[0].triangle.weldingStatus==HavokTriangleQueryStatus::Unavailable,
          "Unavailable welding word blocked support or was fabricated");
    bad=f; auto& degenerate=bad.buffers.storages[0].subparts[0];
    std::copy(degenerate.arrays[0].bytes.begin()+16,degenerate.arrays[0].bytes.begin()+32,degenerate.arrays[0].bytes.begin()+32);
    degenerate.vertices[2]=degenerate.vertices[1]; const auto roundedLine=bad.Build();
    Check(roundedLine.selectedGeometryComplete && roundedLine.shapes[0].corePoints.size()==3 &&
          Near(roundedLine.shapes[0].radius,.1),"Degenerate rounded triangle core was rejected as a flat surface");
    bad=f; auto& extruded=bad.buffers.storages[0].subparts[0];
    F32(extruded.rawRecord.data()+40,.0254f); extruded.sourceExtrusion.z=1;
    const auto extrusion=bad.Build();
    Check(extrusion.shapes.empty() && !extrusion.selectedGeometryComplete &&
          extrusion.unresolved.count("unsupported-triangle-extrusion"),"Unproven nonzero-extrusion support enabled");
    f=Fixture(); f.Box(); f.Missing("hkpSphereShape",2048,3,CollisionRigidTransform());
    const auto partial=f.Build();
    Check(partial.shapes.size()==1 && !partial.selectedGeometryComplete && !partial.selectedCoverageComplete &&
          !partial.nativeRunnable && partial.sourceOmissions[0].source.type=="hkpSphereShape" &&
          partial.sourceOmissions[0].resolution==SupportCollisionOmissionResolution::Unresolved,
          "Unknown source shape disappeared or partial world became runnable");
    f=Fixture(); f.Box(); f.world.geometryComplete=false;
    Check(!f.Build().selectedGeometryComplete,"Unexplained incomplete source traversal became complete");
    f=Fixture(); f.Box(); SupportCollisionWorldLimits limits; limits.shapes=0; Reject([&]{f.Build(limits);});
    limits=SupportCollisionWorldLimits(); limits.totalCorePoints=7; Reject([&]{f.Build(limits);});
    limits=SupportCollisionWorldLimits(); limits.corePointsPerShape=7; Reject([&]{f.Build(limits);});
    limits=SupportCollisionWorldLimits(); limits.maximumRadiusGameUnits=.1; Reject([&]{f.Build(limits);});
    limits=SupportCollisionWorldLimits(); limits.coordinateGameUnits=30; Reject([&]{f.Build(limits);});
    PrimitiveSynthetic();
    std::cout << "WOLF2009_SUPPORT_COLLISION_SYNTHETIC_OK checks=" << checks << '\n';
}
void Original(const char* path,bool withPrimitives) {
    std::ifstream input(path,std::ios::binary); Check(bool(input),"Cannot open supplied support-world package");
    const auto archive=Archive::Scan(input); std::string name;
    for (const auto& chunk:archive.Chunks()) if (chunk.type=="PROC") for (const auto& entry:chunk.entries) {
        Check(name.empty(),"Ambiguous support-world resource"); name=entry.name;
    }
    const auto bytes=ReadMapResource(input,archive,name,"HKXS",64u<<20);
    const auto source=DecodeWorldCollisionGeometry(bytes,name);
    const auto triangles=DecodeWorldTriangleGeometry(bytes,source);
    HavokPrimitiveSupportDocument primitives;
    if (withPrimitives) {
        std::map<std::uint32_t,HavokPrimitiveKind> unique;
        for (const auto& o:source.omissions) {
            if (o.type=="hkpSphereShape") unique.emplace(o.sourceShapeOffset,HavokPrimitiveKind::Sphere);
            else if (o.type=="hkpCapsuleShape") unique.emplace(o.sourceShapeOffset,HavokPrimitiveKind::Capsule);
            else if (o.type=="hkpCylinderShape") unique.emplace(o.sourceShapeOffset,HavokPrimitiveKind::Cylinder);
        }
        std::vector<HavokPrimitiveSelection> selection;
        for (const auto& entry:unique) selection.push_back({entry.first,entry.second});
        primitives=DecodeHavokPrimitiveSupport(bytes,selection);
    }
    const auto support=withPrimitives ? BuildSupportCollisionWorld(source,triangles,primitives) :
                                      BuildSupportCollisionWorld(source,triangles);
    std::size_t boxes=0,hulls=0,tri=0,spheres=0,capsules=0,additional=0,unknownContents=0,unknownPrimitiveContents=0,
                unknownFilter=0,unqualifiedWelding=0;
    for (const auto& shape:support.shapes) {
        boxes+=shape.kind==SupportCollisionKind::Box; hulls+=shape.kind==SupportCollisionKind::ConvexVertices;
        tri+=shape.kind==SupportCollisionKind::Triangle;
        spheres+=shape.kind==SupportCollisionKind::Sphere; capsules+=shape.kind==SupportCollisionKind::Capsule;
        additional+=shape.corePoints.size()-shape.declaredCorePoints;
        unknownContents+=!shape.filter.inheritedContentsKnown;
        unknownPrimitiveContents+=shape.kind!=SupportCollisionKind::Triangle && !shape.filter.inheritedContentsKnown;
        unknownFilter+=shape.kind==SupportCollisionKind::Triangle && !shape.filter.childFilterKnown;
        unqualifiedWelding+=shape.kind==SupportCollisionKind::Triangle && !shape.triangle.contactMetadataQualified;
    }
    Check(boxes+hulls+tri+spheres+capsules==support.shapes.size() && tri==support.importedTriangles &&
          !support.nativeRunnable && !support.entityBindingsResolved,"Support source corpus invariant failed");
    std::cout << "WOLF2009_SUPPORT_COLLISION_REAL_OK map=" << name << " shapes=" << support.shapes.size()
        << " boxes=" << boxes << " hulls=" << hulls << " triangles=" << tri << " corePoints=" << support.totalCorePoints
        << " spheres=" << spheres << " capsules=" << capsules << " primitivePhase=" << withPrimitives
        << " additionalLanes=" << additional << " unknownInheritedContents=" << unknownContents
        << " unknownPrimitiveContents=" << unknownPrimitiveContents << " rootRawFilterInfo=" << support.rootRawFilterInfo
        << " rootRawFilterKnown=" << support.rootRawFilterKnown << " hierarchyComplete=" << support.selectedHierarchyComplete
        << " unknownChildFilter=" << unknownFilter << " unqualifiedWelding=" << unqualifiedWelding
        << " geometryComplete=" << support.selectedGeometryComplete << " contentsComplete=" << support.selectedContentsComplete
        << " coverageComplete=" << support.selectedCoverageComplete << " otherBodies=" << support.otherSourceRigidBodies
        << " nativeRunnable=" << support.nativeRunnable << '\n';
    for (const auto& item:support.unresolved) std::cout << "Unresolved " << item.first << ' ' << item.second << '\n';
}
} // namespace
int main(int argc,char** argv) {
    try {
        Synthetic();
        const bool withPrimitives=argc>1 && std::string(argv[1])=="--with-primitives";
        for (int i=withPrimitives?2:1;i<argc;++i) Original(argv[i],withPrimitives);
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "Support collision test failed: " << e.what() << '\n'; return 1; }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupportCollisionWorld.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace wolf2009 {
namespace {
const std::size_t none = static_cast<std::size_t>(-1);
using PrimitiveKey = std::pair<std::uint32_t,std::uint64_t>;
using PartKey = std::tuple<std::uint32_t,std::uint64_t,std::size_t>;
void Need(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Add(std::size_t& total,std::size_t count,std::size_t maximum,const char* message) {
    Need(total <= maximum && count <= maximum-total,message); total += count;
}
void Count(std::map<std::string,std::size_t>& counts,const std::string& name,std::size_t n=1) {
    Add(counts[name],n,(std::numeric_limits<std::size_t>::max)(),"Support diagnostic count overflow");
}
bool Finite(CollisionVec3 p,double bound) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x) <= bound && std::abs(p.y) <= bound && std::abs(p.z) <= bound;
}
double Dot(CollisionVec3 a,CollisionVec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
bool Near(double a,double b) {
    return std::isfinite(a) && std::isfinite(b) && std::abs(a-b) <=
        32*std::numeric_limits<double>::epsilon()*std::max(1.0,std::max(std::abs(a),std::abs(b)));
}
bool Same(CollisionVec3 a,CollisionVec3 b) { return Near(a.x,b.x) && Near(a.y,b.y) && Near(a.z,b.z); }
bool Same(const CollisionRigidTransform& a,const CollisionRigidTransform& b) {
    return Same(a.translation,b.translation) && Same(a.axis[0],b.axis[0]) &&
        Same(a.axis[1],b.axis[1]) && Same(a.axis[2],b.axis[2]);
}
void Rigid(const CollisionRigidTransform& t,double bound) {
    Need(Finite(t.translation,bound),"Support placement translation exceeds budget");
    for (int i=0;i<3;++i) for (int j=0;j<3;++j)
        Need(Finite(t.axis[i],2) && std::abs(Dot(t.axis[i],t.axis[j])-(i==j?1:0)) <= .00002,
             "Support placement is scaled/nonorthonormal");
    const auto& a=t.axis[0]; const auto& b=t.axis[1]; const auto& c=t.axis[2];
    const double det=a.x*(b.y*c.z-b.z*c.y)-b.x*(a.y*c.z-a.z*c.y)+c.x*(a.y*b.z-a.z*b.y);
    Need(std::abs(det-1) <= .00002,"Support placement is reflected/improper");
}
std::uint64_t Mix(std::uint64_t hash,std::uint64_t value) {
    for (int k=0;k<8;++k) { hash ^= static_cast<unsigned char>(value>>(8*k)); hash *= 1099511628211ull; }
    return hash;
}
std::uint32_t RawWord(const HavokPrimitiveSupport& shape,std::size_t at) {
    Need(at <= shape.rawHeaderBytes && 4 <= shape.rawHeaderBytes-at,"Primitive raw field outside header");
    std::uint32_t word=0;
    for (int k=0;k<4;++k) word|=std::uint32_t(shape.rawHeader[at+k])<<(8*k);
    return word;
}
double RawFloat(const HavokPrimitiveSupport& shape,std::size_t at) {
    const auto bits=RawWord(shape,at); float value;
    static_assert(sizeof(value)==4 && std::numeric_limits<float>::is_iec559,"IEEE32 float required");
    std::memcpy(&value,&bits,4); return value;
}
bool PrimitiveType(const std::string& type) {
    return type=="hkpSphereShape" || type=="hkpCapsuleShape" || type=="hkpCylinderShape";
}
bool SameCylinderMetadata(const HavokCylinderGeometryMetadata& a,const HavokCylinderGeometryMetadata& b) {
    return a.profile==b.profile && a.rawCylinderRadiusBits==b.rawCylinderRadiusBits &&
        a.rawBaseRadiusFactorBits==b.rawBaseRadiusFactorBits && a.quantizationKBits==b.quantizationKBits &&
        a.virtualVertexStepBits==b.virtualVertexStepBits && a.observedRoundThresholdBits==b.observedRoundThresholdBits &&
        a.hasRuntimeThresholdObservation==b.hasRuntimeThresholdObservation &&
        a.matchesNativeSupportSelection==b.matchesNativeSupportSelection;
}
SupportCollisionIdentity Identity(const WorldCollisionPrimitive& p) {
    SupportCollisionIdentity out;
    out.sourceShapeOffset=p.local.sourceShapeOffset; out.sourceRegion=p.local.sourceRegion;
    out.occurrence=p.occurrence; return out;
}
struct Importer {
    const WorldCollisionGeometry& source;
    const WorldTriangleGeometry& triangles;
    const SupportCollisionWorldLimits& limits;
    SupportCollisionWorld out;
    std::map<PrimitiveKey,std::size_t> primitives,primitiveShapes;
    std::map<PartKey,std::size_t> parts;
    std::set<PartKey> joinedParts;
    std::set<std::tuple<std::string,std::uint32_t,std::uint64_t,std::size_t>> omissionIdentities;
    std::map<std::string,std::size_t> omissionCounts;
    const HavokPrimitiveSupportDocument* primitiveDocument;
    std::map<std::uint32_t,std::size_t> primitiveRecords;
    std::set<std::uint32_t> joinedPrimitiveRecords;

    Importer(const WorldCollisionGeometry& w,const WorldTriangleGeometry& t,
             const SupportCollisionWorldLimits& l,const HavokPrimitiveSupportDocument* p=NULL)
        :source(w),triangles(t),limits(l),primitiveDocument(p) {
        Need(l.shapes && l.corePointsPerShape && l.totalCorePoints && l.sourceRecords &&
             std::isfinite(l.coordinateGameUnits) && l.coordinateGameUnits > 0 &&
             std::isfinite(l.maximumRadiusGameUnits) && l.maximumRadiusGameUnits >= 0,
             "Invalid support world limits");
        Need(w.sourceBytes && w.identityBodyPoseVerified && !w.selectedBodyName.empty(),
             "Support import requires a selected source body with a verified pose");
        Need(t.buffers.sourceBytes==w.sourceBytes && t.buffers.sourceFingerprint==w.sourceFingerprint &&
             t.sourceBodyOffset==w.sourceBodyOffset && t.sourceBodyName==w.selectedBodyName,
             "Support world/triangle source identity mismatch");
        Need(w.primitives.size() <= l.sourceRecords && w.omissions.size() <= l.sourceRecords &&
             t.parts.size() <= l.sourceRecords && t.buffers.storages.size() <= l.sourceRecords,
             "Support source record budget exceeded");
        out.sourceFingerprint=w.sourceFingerprint; out.sourceBytes=w.sourceBytes;
        out.selectedBodyName=w.selectedBodyName; out.sourceBodyOffset=w.sourceBodyOffset;
        out.rootShapeOffset=w.rootShapeOffset; out.otherSourceRigidBodies=w.otherSourceRigidBodies;
        out.rootRawFilterInfo=w.rootRawFilterInfo; out.rootRawFilterKnown=w.rootRawFilterKnown;
        out.allSourceRigidBodiesSelected=w.otherSourceRigidBodies==0;
        out.sourceUnsupported=w.unsupported;
        if (p) {
            Need(p->cylinderProfile==HavokCylinderGeometryProfile::Unresolved ||
                 p->cylinderProfile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope,
                 "Unknown primitive document cylinder interpretation profile");
            Need(p->sourceBytes==w.sourceBytes && p->sourceFingerprint==w.sourceFingerprint,
                 "Support world/primitive document source identity mismatch");
            Need(p->shapes.size() <= l.sourceRecords,"Support primitive document record budget exceeded");
            for (std::size_t i=0;i<p->shapes.size();++i) {
                const auto& record=p->shapes[i];
                Need(primitiveRecords.emplace(record.sourceShapeOffset,i).second,
                     "Duplicate support primitive document source offset");
                CheckPrimitiveRecord(record);
            }
        }
        std::set<std::uint32_t> storageOffsets;
        std::size_t subparts=0;
        for (const auto& storage:t.buffers.storages) {
            Need(storageOffsets.insert(storage.sourceStorageOffset).second,"Duplicate support triangle storage identity");
            Add(subparts,storage.subparts.size(),l.sourceRecords,"Support subpart budget exceeded");
        }
        for (std::size_t i=0;i<t.parts.size();++i) {
            const auto& part=t.parts[i];
            Need(part.storageIndex < t.buffers.storages.size(),"Support triangle storage index out of range");
            const auto& storage=t.buffers.storages[part.storageIndex];
            Need(part.subpartIndex < storage.subparts.size(),"Support triangle subpart index out of range");
            const auto& sub=storage.subparts[part.subpartIndex];
            Need(sub.sourceStorageOffset==storage.sourceStorageOffset && sub.subpartIndex==part.subpartIndex &&
                 part.occurrence,"Support triangle part identity mismatch");
            auto region=Mix(Mix(sub.sourceRegion,w.sourceBodyOffset),part.occurrence);
            if (!region) region=1;
            Need(part.sourceRegion==region,"Support triangle occurrence region mismatch");
            Need(parts.emplace(PartKey(storage.sourceStorageOffset,part.occurrence,part.subpartIndex),i).second,
                 "Duplicate support triangle occurrence");
            Rigid(part.placement,l.coordinateGameUnits);
        }
    }
    void Append(SupportCollisionShape shape) {
        Need(out.shapes.size() < limits.shapes,"Support shape budget exceeded");
        Need(!shape.corePoints.empty() && shape.corePoints.size() <= limits.corePointsPerShape,
             "Support core point count outside budget");
        Add(out.totalCorePoints,shape.corePoints.size(),limits.totalCorePoints,"Support core point budget exceeded");
        Need(std::isfinite(shape.radius) && shape.radius >= 0 && shape.radius <= limits.maximumRadiusGameUnits,
             "Support radius outside budget");
        Rigid(shape.placement,limits.coordinateGameUnits);
        CollisionVec3 lo,hi;
        bool first=true;
        for (const auto& p:shape.corePoints) {
            Need(Finite(p,limits.coordinateGameUnits),"Support core point outside budget");
            const auto world=TransformCollisionPoint(shape.placement,p);
            Need(Finite(world,limits.coordinateGameUnits),"Placed support core point outside budget");
            if (first) { lo=hi=world; first=false; }
            lo.x=std::min(lo.x,world.x); lo.y=std::min(lo.y,world.y); lo.z=std::min(lo.z,world.z);
            hi.x=std::max(hi.x,world.x); hi.y=std::max(hi.y,world.y); hi.z=std::max(hi.z,world.z);
        }
        const double inf=std::numeric_limits<double>::infinity(),r=shape.radius;
        shape.worldBounds.minimum={std::nextafter(lo.x-r,-inf),std::nextafter(lo.y-r,-inf),std::nextafter(lo.z-r,-inf)};
        shape.worldBounds.maximum={std::nextafter(hi.x+r,inf),std::nextafter(hi.y+r,inf),std::nextafter(hi.z+r,inf)};
        Need(Finite(shape.worldBounds.minimum,limits.coordinateGameUnits) &&
             Finite(shape.worldBounds.maximum,limits.coordinateGameUnits),"Rounded support bounds outside budget");
        out.shapes.push_back(std::move(shape));
    }
    void CheckPrimitiveRecord(const HavokPrimitiveSupport& p) const {
        const char* type=NULL; std::size_t bytes=0,points=0;
        switch (p.kind) {
        case HavokPrimitiveKind::Sphere: type="hkpSphereShape"; bytes=32; points=1; break;
        case HavokPrimitiveKind::Capsule: type="hkpCapsuleShape"; bytes=64; points=2; break;
        case HavokPrimitiveKind::Cylinder: type="hkpCylinderShape"; bytes=96; break;
        default: throw std::runtime_error("Unknown support primitive document kind");
        }
        auto region=Mix(Mix(source.sourceFingerprint,p.sourceShapeOffset),static_cast<std::uint64_t>(p.kind));
        if (!region) region=1;
        Need(p.sourceType==type && p.rawHeaderBytes==bytes && p.sourceRegion==region,
             "Support primitive document type/header/identity mismatch");
        Need(p.rawRadiusBits==RawWord(p,16) && p.radiusFinite==std::isfinite(RawFloat(p,16)),
             "Support primitive raw radius cache mismatch");
        if (!points) {
            Need(p.cylinder.rawCylinderRadiusBits==RawWord(p,20) &&
                 p.cylinder.rawBaseRadiusFactorBits==RawWord(p,24) && primitiveDocument &&
                 p.cylinder.profile==primitiveDocument->cylinderProfile,
                 "Cylinder raw parameters/document profile cache mismatch");
            if (p.cylinder.profile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope) {
                Need(limits.corePointsPerShape>=32,"Cylinder virtual-core point budget exceeded");
                HavokPrimitiveSupportLimits geometry;
                geometry.coordinateGameUnits=limits.coordinateGameUnits;
                geometry.maximumRadiusGameUnits=limits.maximumRadiusGameUnits;
                const auto expected=BuildHavokCylinderVirtualCore(p.rawHeader,p.cylinder.profile,geometry);
                Need(p.status==HavokPrimitiveSupportStatus::Resolved && p.radiusKnown && p.radiusFinite &&
                     p.radiusGameUnits==expected.radiusGameUnits && p.corePoints.size()==32 && p.virtualVertexIds.size()==32 &&
                     SameCylinderMetadata(p.cylinder,expected.metadata),"Cylinder virtual geometry/profile cache mismatch");
                for (std::size_t i=0;i<32;++i)
                    Need(p.virtualVertexIds[i]==expected.vertexIds[i] && p.corePoints[i].x==expected.corePoints[i].x &&
                         p.corePoints[i].y==expected.corePoints[i].y && p.corePoints[i].z==expected.corePoints[i].z,
                         "Cylinder raw float32 virtual vertex/cache mismatch");
                return;
            }
            Need(p.status==HavokPrimitiveSupportStatus::Unresolved && !p.radiusKnown && p.corePoints.empty(),
                 "Unverified cylinder support geometry was enabled");
            HavokCylinderGeometryMetadata unresolved;
            unresolved.rawCylinderRadiusBits=RawWord(p,20); unresolved.rawBaseRadiusFactorBits=RawWord(p,24);
            Need(SameCylinderMetadata(p.cylinder,unresolved) && p.virtualVertexIds.empty() && p.radiusGameUnits==0,
                 "Unresolved cylinder retained an incompatible geometry cache");
            return;
        }
        Need(SameCylinderMetadata(p.cylinder,HavokCylinderGeometryMetadata()) && p.virtualVertexIds.empty(),
             "Non-cylinder primitive retained a cylinder geometry interpretation");
        const auto radius=RawFloat(p,16)/.0254;
        Need(p.status==HavokPrimitiveSupportStatus::Resolved && p.radiusKnown && p.radiusFinite &&
             std::isfinite(radius) && radius>=0 && radius<=limits.maximumRadiusGameUnits &&
             Near(p.radiusGameUnits,radius) && p.corePoints.size()==points && points<=limits.corePointsPerShape,
             "Support primitive resolved radius/core profile mismatch");
        for (std::size_t i=0;i<points;++i) {
            CollisionVec3 expected;
            if (p.kind==HavokPrimitiveKind::Capsule) {
                const auto at=32+i*16;
                expected={RawFloat(p,at)/.0254,RawFloat(p,at+4)/.0254,RawFloat(p,at+8)/.0254};
            }
            Need(Finite(expected,limits.coordinateGameUnits) && Same(expected,p.corePoints[i]),
                 "Support primitive raw XYZ/core cache mismatch");
        }
    }
    void Primitive(std::size_t index) {
        const auto& p=source.primitives[index];
        const PrimitiveKey key(p.local.sourceShapeOffset,p.occurrence);
        Need(p.occurrence && p.local.sourceRegion && primitives.emplace(key,index).second,
             "Duplicate/empty support primitive occurrence identity");
        Rigid(p.placement,limits.coordinateGameUnits);
        Need(p.local.sourceShapeOffset==p.world.sourceShapeOffset && p.local.sourceRegion==p.world.sourceRegion &&
             p.local.rawContents==p.world.rawContents && p.local.contentsKnown==p.world.contentsKnown &&
             p.local.radius==p.world.radius && p.local.vertices.size()==p.world.vertices.size(),
             "Support local/world primitive identity mismatch");
        for (std::size_t i=0;i<p.local.vertices.size();++i)
            Need(Same(TransformCollisionPoint(p.placement,p.local.vertices[i]),p.world.vertices[i]),
                 "Support local/world vertex placement mismatch");
        if (!p.local.contentsKnown) {
            out.selectedContentsComplete=false; Count(out.unresolved,"unknown-primitive-contents");
        }
        if (!p.supportArrayComplete || p.local.vertices.empty()) {
            out.selectedGeometryComplete=false; Count(out.unresolved,"unavailable-complete-support-array"); return;
        }
        bool radiusHierarchy=p.radiusHierarchyVerified;
        CollisionRigidTransform combined;
        for (const auto& wrapper:p.transforms) {
            Rigid(wrapper.local,limits.coordinateGameUnits);
            combined=ComposeCollisionTransforms(combined,wrapper.local);
            if (wrapper.hasRadius) {
                Need(std::isfinite(wrapper.radius) && wrapper.radius >= 0 &&
                     wrapper.radius <= limits.maximumRadiusGameUnits,"Support wrapper radius outside budget");
                radiusHierarchy=radiusHierarchy && std::abs(wrapper.radius-p.local.radius) <= 1e-7/.0254;
            }
        }
        Need(Same(combined,p.placement),"Support primitive transform chain/placement mismatch");
        if (!radiusHierarchy) {
            out.selectedGeometryComplete=out.selectedHierarchyComplete=false;
            Count(out.unresolved,"unverified-wrapper-radius"); return;
        }
        SupportCollisionShape shape;
        if (p.kind==WorldCollisionPrimitiveKind::Box) shape.kind=SupportCollisionKind::Box;
        else if (p.kind==WorldCollisionPrimitiveKind::ConvexVertices) shape.kind=SupportCollisionKind::ConvexVertices;
        else { out.selectedGeometryComplete=false; Count(out.unresolved,"unknown-primitive-kind"); return; }
        shape.source=Identity(p); shape.placement=p.placement; shape.radius=p.local.radius;
        shape.declaredCorePoints=p.local.vertices.size(); shape.corePoints=p.local.vertices;
        Need(p.additionalSupportVertices.size() <= limits.corePointsPerShape &&
             shape.corePoints.size() <= limits.corePointsPerShape-p.additionalSupportVertices.size(),
             "Support primitive lane budget exceeded");
        shape.corePoints.insert(shape.corePoints.end(),p.additionalSupportVertices.begin(),p.additionalSupportVertices.end());
        shape.filter.inheritedRawContents=p.local.rawContents; shape.filter.inheritedContentsKnown=p.local.contentsKnown;
        primitiveShapes.emplace(key,out.shapes.size()); Append(std::move(shape));
    }
    void TrianglePart(SupportCollisionOmission& record) {
        const auto& o=record.source;
        const PartKey key(o.sourceShapeOffset,o.occurrence,o.sourceSubpartIndex);
        const auto found=parts.find(key);
        Need(found!=parts.end() && joinedParts.insert(key).second,"Missing/duplicate support triangle source join");
        const auto& part=triangles.parts[found->second];
        const auto& storage=triangles.buffers.storages[part.storageIndex];
        const auto& sub=storage.subparts[part.subpartIndex];
        Need(sub.sourceStorageOffset==o.sourceShapeOffset && sub.subpartIndex==o.sourceSubpartIndex &&
             sub.declaredTriangles==o.triangles && Same(part.placement,o.placement),
             "Support triangle graph/buffer occurrence mismatch");
        Add(out.expectedTriangles,o.triangles,limits.shapes,"Support triangle count budget exceeded");
        if (!part.positionsPlaced || !part.extrusionPlaced) {
            out.selectedGeometryComplete=false;
            if (o.triangles) {
                out.selectedContentsComplete=out.selectedChildFiltersComplete=out.selectedContactMetadataComplete=false;
            }
            Count(out.unresolved,"unplaced-triangle-subpart"); return;
        }
        Need(part.worldVertices.size()==sub.vertices.size(),"Support placed triangle vertex cache mismatch");
        record.firstShape=out.shapes.size();
        bool complete=true;
        for (std::size_t i=0;i<o.triangles;++i) {
            const auto q=ResolveHavokTriangleQuery(storage,sub,i,limits.triangleQuery);
            if (q.materialStatus!=HavokTriangleQueryStatus::Resolved) {
                out.selectedChildFiltersComplete=false; Count(out.unresolved,"unknown-triangle-child-filter");
            }
            // The material word and this inherited occurrence value have no
            // established mapping to gameplay contents. Do not substitute one.
            out.selectedContentsComplete=false; Count(out.unresolved,"unmapped-triangle-contents");
            const bool contact=q.weldingType==6 && q.weldingStatus==HavokTriangleQueryStatus::Resolved;
            if (!contact) {
                out.selectedContactMetadataComplete=false; Count(out.unresolved,"unqualified-triangle-welding");
            }
            if (q.geometryStatus!=HavokTriangleQueryStatus::Resolved || !q.radiusFinite || q.radiusGameUnits<0 ||
                !q.extrusionFinite || q.extrusionGameUnits.x!=0 || q.extrusionGameUnits.y!=0 || q.extrusionGameUnits.z!=0) {
                out.selectedGeometryComplete=false; complete=false;
                Count(out.unresolved,q.geometryStatus!=HavokTriangleQueryStatus::Resolved ? "unavailable-triangle-core" :
                      !q.radiusFinite || q.radiusGameUnits<0 ? "unavailable-triangle-radius" : "unsupported-triangle-extrusion");
                continue;
            }
            SupportCollisionShape shape;
            shape.kind=SupportCollisionKind::Triangle; shape.placement=part.placement;
            shape.source.sourceShapeOffset=o.sourceShapeOffset; shape.source.sourceRecordOffset=q.sourceRecordOffset;
            shape.source.sourceRegion=part.sourceRegion; shape.source.occurrence=part.occurrence;
            shape.source.sourceSubpartIndex=part.subpartIndex; shape.source.triangleIndex=i;
            shape.corePoints.assign(q.positions.begin(),q.positions.end()); shape.declaredCorePoints=3; shape.radius=q.radiusGameUnits;
            for (int k=0;k<3;++k) {
                Need(q.indices[k] < part.worldVertices.size() &&
                     Same(TransformCollisionPoint(part.placement,q.positions[k]),part.worldVertices[q.indices[k]]),
                     "Support raw/placed triangle vertex mismatch");
            }
            shape.filter.inheritedRawContents=o.rawContents; shape.filter.inheritedContentsKnown=o.contentsKnown;
            shape.filter.contentsProvenance=SupportCollisionContentsProvenance::TriangleOccurrenceValueUnmapped;
            shape.filter.rawChildFilterInfo=q.rawCollisionFilterInfo; shape.filter.childFilterStatus=q.materialStatus;
            shape.filter.childFilterKnown=q.materialStatus==HavokTriangleQueryStatus::Resolved;
            auto& meta=shape.triangle;
            meta.rawIndexType=q.rawIndexType; meta.rawFlipByte=q.rawFlipByte; meta.weldingType=q.weldingType;
            meta.rawTriangleOffset=q.rawTriangleOffset; meta.rawRadiusBits=q.rawRadiusBits;
            meta.weldingIndex=q.weldingIndex; meta.weldingArrayCount=q.weldingArrayCount; meta.weldingInfo=q.weldingInfo;
            meta.weldingStatus=q.weldingStatus; meta.rawExtrusionBits=q.rawExtrusionBits; meta.contactMetadataQualified=contact;
            Append(std::move(shape)); ++out.importedTriangles;
        }
        record.shapeCount=out.shapes.size()-record.firstShape;
        if (complete) record.resolution=SupportCollisionOmissionResolution::ImportedTriangleSubpart;
    }
    void PrimitiveOmission(SupportCollisionOmission& record) {
        const auto& o=record.source;
        const auto found=primitiveRecords.find(o.sourceShapeOffset);
        Need(found!=primitiveRecords.end(),"Missing support primitive document source join");
        const auto& p=primitiveDocument->shapes[found->second];
        Need(p.sourceType==o.type && o.sourceSubpartIndex==none && !o.triangles &&
             primitives.find(PrimitiveKey(o.sourceShapeOffset,o.occurrence))==primitives.end(),
             "Support primitive omission/source identity mismatch");
        joinedPrimitiveRecords.insert(o.sourceShapeOffset);
        if (p.kind==HavokPrimitiveKind::Cylinder && p.status!=HavokPrimitiveSupportStatus::Resolved) return;
        if (!o.hasWrapperRadiusMetadata) {
            out.selectedGeometryComplete=out.selectedHierarchyComplete=false;
            Count(out.unresolved,"unknown-primitive-wrapper-radii"); return;
        }
        Need(o.wrapperRadiusCount<=limits.sourceRecords && std::isfinite(o.minimumWrapperRadius) &&
             std::isfinite(o.maximumWrapperRadius) && o.minimumWrapperRadius>=0 &&
             o.minimumWrapperRadius<=o.maximumWrapperRadius && o.maximumWrapperRadius<=limits.maximumRadiusGameUnits,
             "Primitive wrapper radius metadata outside budget");
        if (!o.wrapperRadiusCount) {
            Need(o.minimumWrapperRadius==0 && o.maximumWrapperRadius==0,
                 "Empty primitive wrapper radius summary is inconsistent");
        } else if (std::abs(o.minimumWrapperRadius-p.radiusGameUnits)>1e-7/.0254 ||
                   std::abs(o.maximumWrapperRadius-p.radiusGameUnits)>1e-7/.0254) {
            out.selectedGeometryComplete=out.selectedHierarchyComplete=false;
            Count(out.unresolved,"unverified-wrapper-radius"); return;
        }
        Need(out.shapes.size()<limits.shapes && out.totalCorePoints<=limits.totalCorePoints &&
             p.corePoints.size()<=limits.totalCorePoints-out.totalCorePoints,
             "Primitive support output budget exceeded");
        SupportCollisionShape shape;
        shape.kind=p.kind==HavokPrimitiveKind::Sphere ? SupportCollisionKind::Sphere :
            p.kind==HavokPrimitiveKind::Capsule ? SupportCollisionKind::Capsule : SupportCollisionKind::Cylinder;
        shape.source.sourceShapeOffset=o.sourceShapeOffset; shape.source.occurrence=o.occurrence;
        shape.source.sourceRegion=Mix(Mix(p.sourceRegion,source.sourceBodyOffset),o.occurrence);
        if (!shape.source.sourceRegion) shape.source.sourceRegion=1;
        shape.placement=o.placement; shape.radius=p.radiusGameUnits;
        shape.declaredCorePoints=p.corePoints.size(); shape.corePoints=p.corePoints;
        shape.cylinder=p.cylinder;
        shape.filter.inheritedRawContents=o.rawContents; shape.filter.inheritedContentsKnown=o.contentsKnown;
        if (!o.contentsKnown) { out.selectedContentsComplete=false; Count(out.unresolved,"unknown-primitive-contents"); }
        record.firstShape=out.shapes.size(); Append(std::move(shape)); record.shapeCount=1;
        record.resolution=SupportCollisionOmissionResolution::ImportedPrimitiveSupport;
    }
    void Omission(const WorldCollisionOmission& sourceRecord) {
        SupportCollisionOmission record; record.source=sourceRecord;
        const auto& o=record.source;
        Need(o.occurrence,"Empty support omission occurrence identity");
        Need(omissionIdentities.emplace(o.type,o.sourceShapeOffset,o.occurrence,o.sourceSubpartIndex).second,
             "Duplicate support omission occurrence identity");
        Rigid(o.placement,limits.coordinateGameUnits); Count(omissionCounts,o.type);
        if (o.type=="storage-triangle-subpart") TrianglePart(record);
        else if (primitiveDocument && PrimitiveType(o.type)) PrimitiveOmission(record);
        else if (o.type=="unverified-source-plane-residual" || o.type=="unverified-lower-dimensional-convex") {
            const PrimitiveKey key(o.sourceShapeOffset,o.occurrence);
            const auto found=primitives.find(key);
            Need(found!=primitives.end(),"Support diagnostic lacks its exact primitive occurrence");
            const auto& primitive=source.primitives[found->second];
            Need(o.sourceSubpartIndex==none && Same(o.placement,primitive.placement) &&
                 o.rawContents==primitive.local.rawContents && o.contentsKnown==primitive.local.contentsKnown,
                 "Support diagnostic/primitive source join mismatch");
            const auto shape=primitiveShapes.find(key);
            if (shape!=primitiveShapes.end()) {
                record.firstShape=shape->second; record.shapeCount=1;
                record.resolution=o.type=="unverified-source-plane-residual" ?
                    SupportCollisionOmissionResolution::IndependentRayPlaneDiagnostic :
                    SupportCollisionOmissionResolution::IndependentVolumeDiagnostic;
            }
        }
        if (record.resolution==SupportCollisionOmissionResolution::Unresolved) {
            out.selectedGeometryComplete=false; Count(out.unresolved,o.type);
            if (!o.contentsKnown) out.selectedContentsComplete=false;
            if (o.type=="unverified-wrapper-radius" || o.type=="scaled-storage-mesh") out.selectedHierarchyComplete=false;
        }
        out.sourceOmissions.push_back(std::move(record));
    }
    SupportCollisionWorld Run() {
        for (std::size_t i=0;i<source.primitives.size();++i) Primitive(i);
        for (const auto& record:source.omissions) Omission(record);
        Need(joinedParts.size()==parts.size(),"Support triangle part has no selected source occurrence");
        Need(joinedPrimitiveRecords.size()==primitiveRecords.size(),
             "Support primitive document object has no selected source occurrence");
        Need(out.expectedTriangles==triangles.reportedTriangles,"Support reported triangle count mismatch");
        for (const auto& diagnostic:source.unsupported) {
            const auto matched=omissionCounts[diagnostic.first];
            Need(diagnostic.second>=matched,"Support source diagnostic counts disagree");
            const auto remaining=diagnostic.second-matched;
            if (!remaining) continue;
            if (diagnostic.first=="unverified-storage-contents") {
                out.selectedContentsComplete=false; Count(out.unresolved,diagnostic.first,remaining);
            } else if (diagnostic.first!="unverified-source-plane-residual" &&
                       diagnostic.first!="unverified-lower-dimensional-convex") {
                out.selectedGeometryComplete=false; Count(out.unresolved,diagnostic.first,remaining);
            }
        }
        // A caller cannot erase all diagnostics of an incomplete traversal and
        // thereby turn it into a complete support world.
        if (!source.geometryComplete && source.omissions.empty() && source.unsupported.empty()) {
            out.selectedGeometryComplete=false; Count(out.unresolved,"unexplained-incomplete-source-geometry");
        }
        if (!source.contentsComplete) out.selectedContentsComplete=false;
        out.selectedCoverageComplete=out.selectedGeometryComplete && out.selectedHierarchyComplete &&
            out.selectedContentsComplete && out.selectedChildFiltersComplete && out.selectedContactMetadataComplete;
        return std::move(out);
    }
};
} // namespace
SupportCollisionWorld BuildSupportCollisionWorld(const WorldCollisionGeometry& world,
    const WorldTriangleGeometry& triangles,const SupportCollisionWorldLimits& limits) {
    return Importer(world,triangles,limits).Run();
}
SupportCollisionWorld BuildSupportCollisionWorld(const WorldCollisionGeometry& world,
    const WorldTriangleGeometry& triangles,const HavokPrimitiveSupportDocument& primitives,
    const SupportCollisionWorldLimits& limits) {
    return Importer(world,triangles,limits,&primitives).Run();
}
} // namespace wolf2009

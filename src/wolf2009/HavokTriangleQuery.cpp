// SPDX-License-Identifier: GPL-3.0-or-later
#include "HavokTriangleQuery.h"
#include <cmath>
#include <cstring>

namespace wolf2009 {
namespace {
struct Problem { HavokTriangleQueryStatus status; const char* reason; };
void Need(bool value, const char* reason) {
    if (!value) throw Problem{HavokTriangleQueryStatus::Invalid,reason};
}
void Known(bool value, const char* reason) {
    if (!value) throw Problem{HavokTriangleQueryStatus::Unsupported,reason};
}
std::uint32_t U32(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
           std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
std::uint16_t U16(const unsigned char* p) {
    return std::uint16_t(p[0]) | std::uint16_t(p[1]) << 8;
}
std::int32_t Signed16(std::uint32_t word) {
    const std::uint32_t low = word & 65535u;
    return low < 32768u ? std::int32_t(low) : std::int32_t(low) - 65536;
}
std::int64_t Signed32(std::uint32_t word) {
    return word < 0x80000000u ? std::int64_t(word) : std::int64_t(word) - 4294967296LL;
}
float F32(std::uint32_t word) {
    static_assert(sizeof(float) == 4, "32-bit source floats required");
    float value; std::memcpy(&value,&word,4); return value;
}
bool Finite(const CollisionVec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Bounded(const CollisionVec3& v, double limit) {
    return Finite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit;
}
CollisionVec3 Sub(const CollisionVec3& a,const CollisionVec3& b) {
    return {a.x-b.x,a.y-b.y,a.z-b.z};
}
CollisionVec3 Cross(const CollisionVec3& a,const CollisionVec3& b) {
    // Separate products match the source mul/sub predicates. FMA contraction
    // can otherwise make Cross(v,v) a tiny nonzero vector for exact duplicates.
    volatile double yz=a.y*b.z, zy=a.z*b.y, zx=a.z*b.x;
    volatile double xz=a.x*b.z, xy=a.x*b.y, yx=a.y*b.x;
    return {yz-zy,zx-xz,xy-yx};
}
double Dot(const CollisionVec3& a,const CollisionVec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
void Array(const HavokTriangleRawArray& a,std::size_t width,const HavokTriangleQueryLimits& limits) {
    Need(a.elementBytes == width,"retained array element width mismatch");
    Need(a.count <= limits.rawArrayBytes/width,"retained array exceeds query budget");
    Need(a.bytes.size() == std::size_t(a.count)*width,"retained array count/bytes mismatch");
    Need(!a.count || a.hasPointer,"retained nonempty array has no pointer fixup");
}
std::uint32_t Read(const HavokTriangleRawArray& a,std::int64_t offset,std::size_t width) {
    Need(offset >= 0 && std::uint64_t(offset) <= a.bytes.size() &&
         width <= a.bytes.size()-std::size_t(offset),"triangle query outside retained array");
    const unsigned char* p = a.bytes.data()+std::size_t(offset);
    return width == 1 ? *p : width == 2 ? U16(p) : U32(p);
}
void Geometry(const HavokTriangleStorage& storage,const HavokTriangleSubpart& part,
              const HavokTriangleQueryLimits& limits,HavokTriangleQueryResult& out) {
    const unsigned char* record = part.rawRecord.data();
    const std::uint32_t vertices = U32(record+28);
    Known(U32(record+24) == 16,"vertex storage profile is not 16-byte XYZ/W");
    Known(out.rawIndexType == 1 || out.rawIndexType == 2,"unverified triangle index type");
    Need(vertices <= limits.vertices,"triangle vertex query budget exceeded");
    Array(part.arrays[0],16,limits);
    Need(part.arrays[0].count == vertices,"record and retained vertex counts differ");
    const std::size_t width = out.rawIndexType == 1 ? 2 : 4;
    const HavokTriangleRawArray& indices = part.arrays[out.rawIndexType == 1 ? 1 : 2];
    Array(indices,width,limits);
    const std::int64_t stride = Signed32(U32(record+52));
    const std::uint32_t mask = std::uint32_t(out.signedFlip) & std::uint32_t(out.triangleIndex);
    out.indexSelectors = {{0,Signed16(mask+1),Signed16((mask^1u)+1)}};
    const float scale[3] = {F32(storage.rawScaleBits[0]),F32(storage.rawScaleBits[1]),F32(storage.rawScaleBits[2])};
    for (int axis=0;axis<3;++axis) Need(std::isfinite(scale[axis]),"nonfinite source triangle scale");
    for (int corner=0;corner<3;++corner) {
        const std::int64_t at = std::int64_t(out.triangleIndex)*stride + out.indexSelectors[corner]*std::int64_t(width);
        out.indices[corner] = Read(indices,at,width);
        Need(out.indices[corner] < vertices,"triangle vertex index outside retained vertices");
        double xyz[3];
        for (int axis=0;axis<3;++axis) {
            const float source = F32(Read(part.arrays[0],std::int64_t(out.indices[corner])*16+axis*4,4));
            const float scaled = source*scale[axis];
            xyz[axis] = double(scaled)/.0254;
        }
        out.positions[corner] = {xyz[0],xyz[1],xyz[2]};
        Need(Bounded(out.positions[corner],limits.coordinateGameUnits),"triangle XYZ is nonfinite/outside query budget");
    }
    out.geometryStatus = HavokTriangleQueryStatus::Resolved;
}
void Material(const HavokTriangleSubpart& part,const HavokTriangleQueryLimits& limits,
              HavokTriangleQueryResult& out) {
    // The base finish-loaded constructor upgrades the legacy zero enum to
    // UINT8 before StorageExtendedMesh rebinds the owned buffer pointers.
    out.effectiveMaterialIndexType = out.rawMaterialIndexType ? out.rawMaterialIndexType : 1;
    Known(out.effectiveMaterialIndexType == 1 || out.effectiveMaterialIndexType == 2,
          "unverified material-index storage type");
    const std::size_t width = out.effectiveMaterialIndexType == 1 ? 1 : 2;
    const HavokTriangleRawArray& indices = part.arrays[out.effectiveMaterialIndexType == 1 ? 3 : 5];
    Array(indices,width,limits);
    if (!indices.hasPointer) {
        out.nullMaterialIndexBase = true;
        out.rawCollisionFilterInfo = 0;
        out.materialStatus = HavokTriangleQueryStatus::Resolved;
        return;
    }
    const HavokTriangleRawArray& materials = part.arrays[4];
    Array(materials,4,limits);
    out.materialIndex = Read(indices,std::int64_t(out.triangleIndex)*out.materialIndexByteStride,width);
    out.rawCollisionFilterInfo = Read(materials,std::int64_t(out.materialIndex)*out.materialByteStride,4);
    out.materialStatus = HavokTriangleQueryStatus::Resolved;
}
}
HavokTriangleQueryResult ResolveHavokTriangleQuery(
    const HavokTriangleStorage& storage,const HavokTriangleSubpart& part,
    std::size_t triangleIndex,const HavokTriangleQueryLimits& limits) {
    HavokTriangleQueryResult out;
    out.sourceStorageOffset = storage.sourceStorageOffset; out.sourceRecordOffset = part.sourceRecordOffset;
    out.sourceRegion = part.sourceRegion; out.triangleIndex = triangleIndex;
    out.rawIndexType = part.rawRecord[56]; out.rawFlipByte = part.rawRecord[57];
    out.signedFlip = out.rawFlipByte < 128 ? out.rawFlipByte : int(out.rawFlipByte)-256;
    out.rawTriangleOffset = U32(part.rawRecord.data()+60);
    if (part.sourceStorageOffset != storage.sourceStorageOffset ||
        U32(part.rawRecord.data()+16) > limits.triangles ||
        triangleIndex >= U32(part.rawRecord.data()+16)) {
        out.geometryStatus = out.materialStatus = HavokTriangleQueryStatus::Invalid;
        out.geometryReason = out.materialReason = "triangle/source identity or declared range mismatch";
        return out;
    }
    out.weldingIndex = Signed32(out.rawTriangleOffset)+std::int64_t(triangleIndex);
    out.weldingArrayCount = U32(storage.rawHeader.data()+176);
    out.weldingType = storage.rawHeader[184];
    out.rawRadiusBits = U32(storage.rawHeader.data()+188);
    out.radiusGameUnits = double(F32(out.rawRadiusBits))/.0254;
    out.radiusFinite = std::isfinite(out.radiusGameUnits) && std::abs(out.radiusGameUnits) <= limits.coordinateGameUnits;
    for (int i=0;i<4;++i) out.rawExtrusionBits[i] = U32(part.rawRecord.data()+32+i*4);
    out.extrusionGameUnits = {double(F32(out.rawExtrusionBits[0]))/.0254,
                             double(F32(out.rawExtrusionBits[1]))/.0254,
                             double(F32(out.rawExtrusionBits[2]))/.0254};
    out.extrusionFinite = Bounded(out.extrusionGameUnits,limits.coordinateGameUnits);
    out.rawMaterialIndexType = part.rawRecord[1];
    out.materialByteStride = Signed16(U16(part.rawRecord.data()+2));
    out.materialIndexByteStride = U16(part.rawRecord.data()+8);
    if (!out.weldingArrayCount) out.weldingStatus = HavokTriangleQueryStatus::Resolved;
    else if (out.weldingArrayCount > limits.triangles || out.weldingIndex < 0 ||
             std::uint64_t(out.weldingIndex) >= out.weldingArrayCount) {
        out.weldingStatus = HavokTriangleQueryStatus::Invalid;
        out.weldingReason = "welding index/count outside retained storage declaration";
    } else out.weldingReason = "nonempty welding payload is not retained; selected word is unavailable";
    try { Geometry(storage,part,limits,out); }
    catch (const Problem& p) { out.geometryStatus=p.status; out.geometryReason=p.reason; }
    try { Material(part,limits,out); }
    catch (const Problem& p) { out.materialStatus=p.status; out.materialReason=p.reason; }
    const CollisionVec3 normal=Cross(Sub(out.positions[1],out.positions[0]),Sub(out.positions[2],out.positions[0]));
    const double areaSquared=Dot(normal,normal);
    out.flatNativeSurfaceCandidate = out.geometryStatus == HavokTriangleQueryStatus::Resolved &&
        std::isfinite(areaSquared) && areaSquared > 0 &&
        out.radiusFinite && out.radiusGameUnits == 0 && out.extrusionFinite &&
        out.extrusionGameUnits.x == 0 && out.extrusionGameUnits.y == 0 && out.extrusionGameUnits.z == 0 &&
        out.weldingType == 6 && out.weldingStatus != HavokTriangleQueryStatus::Invalid;
    return out;
}
HavokTriangleRayResult RayCastHavokTriangleSegment(
    const HavokTriangleQueryResult& triangle,const CollisionVec3& from,const CollisionVec3& to,
    const HavokTriangleRayOptions& options) {
    HavokTriangleRayResult out; out.fraction=options.maximumFraction;
    if (triangle.geometryStatus != HavokTriangleQueryStatus::Resolved) {
        out.reason="triangle geometry is unavailable"; return out;
    }
    if (!Finite(from) || !Finite(to) || !std::isfinite(options.maximumFraction) ||
        options.maximumFraction < 0 || options.maximumFraction > 1) {
        out.status=HavokTriangleRayStatus::InvalidInput; out.reason="invalid finite segment/fraction"; return out;
    }
    if (options.requireAnyRawFilterBits) {
        if (triangle.materialStatus != HavokTriangleQueryStatus::Resolved) {
            out.reason="requested raw-filter policy requires a resolved child filter word"; return out;
        }
        if (!(triangle.rawCollisionFilterInfo & options.rawFilterMask)) {
            out.status=HavokTriangleRayStatus::FilteredOut; return out;
        }
    }
    const auto& v=triangle.positions;
    if (!Finite(v[0]) || !Finite(v[1]) || !Finite(v[2])) {
        out.status=HavokTriangleRayStatus::InvalidInput; out.reason="nonfinite triangle vertices"; return out;
    }
    const CollisionVec3 n=Cross(Sub(v[1],v[0]),Sub(v[2],v[0]));
    const double n2=Dot(n,n), a=Dot(Sub(from,v[0]),n), b=Dot(Sub(to,v[0]),n);
    if (!std::isfinite(n2) || !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(a-b)) {
        out.status=HavokTriangleRayStatus::InvalidInput; out.reason="segment arithmetic outside finite range"; return out;
    }
    out.status=HavokTriangleRayStatus::Miss;
    if (n2 == 0 || !((a < 0 && b > 0) || (a > 0 && b < 0) || (a == 0 && b != 0))) return out;
    const double fraction=a/(a-b);
    if (!(fraction < options.maximumFraction)) return out;
    const CollisionVec3 delta=Sub(to,from);
    const CollisionVec3 point(from.x+fraction*delta.x,from.y+fraction*delta.y,from.z+fraction*delta.z);
    if (!Finite(delta) || !Finite(point)) {
        out.status=HavokTriangleRayStatus::InvalidInput; out.reason="nonfinite segment witness"; return out;
    }
    // Independently expressed plane/edge test with the measured dimensionless
    // source tolerance. Its scaling follows |cross(edge1,edge2)| squared.
    const double edgeMinimum=-9.999999747378752e-5*n2;
    for (int i=0;i<3;++i) {
        const double edge=Dot(Cross(Sub(v[i],point),Sub(v[(i+1)%3],point)),n);
        if (!std::isfinite(edge)) {
            out.status=HavokTriangleRayStatus::InvalidInput; out.reason="nonfinite triangle edge predicate"; return out;
        }
        if (edge < edgeMinimum) return out;
    }
    const double sign=a > 0 ? 1 : -1, inverse=sign/std::sqrt(n2);
    out.normal={n.x*inverse,n.y*inverse,n.z*inverse}; out.position=point;
    out.fraction=fraction; out.status=HavokTriangleRayStatus::Hit; return out;
}
} // namespace wolf2009

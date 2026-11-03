// SPDX-License-Identifier: GPL-3.0-or-later
// Independent checked file-format reading. No Havok SDK code or runtime.
#include "HavokTriangleMesh.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
void Need(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
double Float(std::uint32_t bits) {
    float value;static_assert(sizeof(value)==4,"IEEE32 float required");
    std::memcpy(&value,&bits,4);return value;
}
bool Finite(CollisionVec3 p,double bound) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::abs(p.x)<=bound&&std::abs(p.y)<=bound&&std::abs(p.z)<=bound;
}
std::uint64_t Mix(std::uint64_t hash,std::uint64_t value) {
    for(int k=0;k<8;++k){hash^=static_cast<unsigned char>(value>>(k*8));hash*=1099511628211ull;}
    return hash;
}
struct Reader : detail::HavokPack {
    const HavokTriangleMeshLimits &limits;
    HavokTriangleMeshDocument result;
    std::size_t ownedVertices=0,ownedIndexElements=0,totalTriangles=0,totalSubparts=0;
    std::uint64_t fingerprint=14695981039346656037ull;
    Reader(const std::vector<unsigned char> &bytes,const HavokTriangleMeshLimits &l)
        :HavokPack(bytes,l.sourceBytes,l.objects,l.fixups),limits(l) {
        Need(l.storageShapes&&l.subparts&&l.maximumIndexStride>=6&&l.coordinateGameUnits>0&&
             std::isfinite(l.coordinateGameUnits),"Invalid triangle reader limits");
        for(const auto c:bytes){fingerprint^=c;fingerprint*=1099511628211ull;}
    }
    void Budget(std::size_t count,std::size_t stride) {
        Need(stride&&count<=(limits.decodedBytes-result.decodedBytes)/stride,
             "Triangle decoded-memory budget exceeded");
        result.decodedBytes+=count*stride;
    }
    void Add(std::size_t &total,std::size_t count,std::size_t maximum,const char *message) {
        Need(total<=maximum&&count<=maximum-total,message);total+=count;
    }
    template<std::size_t N> void Raw(std::array<unsigned char,N> &out,std::uint32_t offset) {
        DataRange(offset,N);std::copy(bytes.begin()+sections[2].absolute+offset,
            bytes.begin()+sections[2].absolute+offset+N,out.begin());
    }
    std::vector<HavokTriangleFixup> Fixups(std::uint32_t object,std::size_t length) {
        std::vector<HavokTriangleFixup> out;
        const auto begin=pointers.lower_bound(object);
        auto end=begin;std::size_t count=0;
        while(end!=pointers.end()&&end->first-object<length){++end;++count;}
        Budget(count,sizeof(HavokTriangleFixup));out.reserve(count);
        for(auto i=begin;i!=end;++i) {
            HavokTriangleFixup f;f.fieldOffset=i->first-object;f.targetDataOffset=i->second;out.push_back(f);
        }
        return out;
    }
    HavokTriangleRawArray Array(std::uint32_t owner,std::uint32_t field,std::uint32_t stride) {
        HavokTriangleRawArray out;
        out.fieldOffset=field;out.rawPointerWord=DU(owner+field);out.count=DU(owner+field+4);
        out.rawCapacityFlags=DU(owner+field+8);out.elementBytes=stride;
        Need(out.count<=limits.arrayElements,"Triangle raw-array element budget exceeded");
        const auto pointer=pointers.find(owner+field);
        out.hasPointer=pointer!=pointers.end();if(out.hasPointer)out.targetDataOffset=pointer->second;
        if(!out.count)return out;
        Need(out.hasPointer,"Triangle owned-array pointer fixup missing");
        ArrayRange(out.targetDataOffset,out.count,stride);Budget(out.count,stride);
        const auto absolute=sections[2].absolute+std::size_t(out.targetDataOffset);
        out.bytes.assign(bytes.begin()+absolute,bytes.begin()+absolute+std::size_t(out.count)*stride);
        return out;
    }
    static std::uint32_t Index(const std::vector<unsigned char> &bytes,std::size_t at,std::size_t width) {
        std::uint32_t value=0;for(std::size_t k=0;k<width;++k)value|=std::uint32_t(bytes[at+k])<<(k*8);
        return value;
    }
    void Unknown(HavokTriangleSubpart &part,const char *reason) {
        // Fixed reader-owned diagnostic text; never unbounded source strings.
        Budget(1,sizeof(std::string));
        Budget(std::strlen(reason)+1,1);part.unsupportedProfiles.push_back(reason);
    }
    HavokTriangleSubpart Subpart(std::uint32_t storage,std::uint32_t record,std::uint32_t owner,
                                std::size_t index,bool identity) {
        Need(Type(owner)=="hkpStorageExtendedMeshShapeMeshSubpartStorage",
             "Triangle owned buffer has unexpected type");
        DataRange(owner,80);DataRange(record,64);
        HavokTriangleSubpart part;
        part.sourceStorageOffset=storage;part.sourceRecordOffset=record;
        part.sourceOwnedStorageOffset=owner;part.subpartIndex=index;
        part.sourceRegion=Mix(Mix(Mix(fingerprint,storage),index),owner);
        if(!part.sourceRegion)part.sourceRegion=1;
        Raw(part.rawRecord,record);Raw(part.rawOwnedStorageHeader,owner);
        part.recordFixups=Fixups(record,64);part.ownedStorageFixups=Fixups(owner,80);
        part.declaredTriangles=DU(record+16);part.vertexStride=DU(record+24);
        part.declaredVertices=DU(record+28);part.indexStride=DU(record+52);part.rawIndexType=DU(record+56);
        Need(part.declaredTriangles<=limits.trianglesPerSubpart&&part.declaredVertices<=limits.verticesPerSubpart,
             "Triangle subpart vertex/triangle budget exceeded");
        Add(totalTriangles,part.declaredTriangles,limits.totalTriangles,"Triangle aggregate count budget exceeded");
        Add(result.declaredTriangles,part.declaredTriangles,limits.totalTriangles,"Triangle declared count overflow");
        const auto ownedVertexCount=DU(owner+12);
        Need(ownedVertexCount<=limits.verticesPerSubpart,"Owned triangle vertex budget exceeded");
        Add(ownedVertices,ownedVertexCount,limits.totalVertices,"Triangle aggregate vertex budget exceeded");
        Add(ownedIndexElements,DU(owner+24),limits.arrayElements,"Triangle aggregate index budget exceeded");
        Add(ownedIndexElements,DU(owner+36),limits.arrayElements,"Triangle aggregate index budget exceeded");
        const std::uint32_t fields[6]={8,20,32,44,56,68},widths[6]={16,2,4,1,4,2};
        for(int k=0;k<6;++k)part.arrays[k]=Array(owner,fields[k],widths[k]);
        const auto &vertices=part.arrays[0];
        if(part.vertexStride!=16)Unknown(part,"unverified-vertex-stride");
        if(part.rawIndexType==1)part.indexBytes=2;
        else if(part.rawIndexType==2)part.indexBytes=4;
        else Unknown(part,"unverified-index-type");
        bool supported=part.vertexStride==16&&part.indexBytes;
        if(part.indexBytes) {
            Need(part.indexStride<=limits.maximumIndexStride,"Triangle index-stride budget exceeded");
            if(part.indexStride<3*part.indexBytes||part.indexStride%part.indexBytes) {
                Unknown(part,"unverified-index-stride");supported=false;
            }
        }
        if(!identity)Unknown(part,"unverified-storage-scale");
        if(part.vertexStride==16) {
            Need(part.declaredVertices==vertices.count,"Triangle record/owned vertex counts disagree");
            Budget(vertices.count,sizeof(HavokTriangleVertex));part.vertices.reserve(vertices.count);
        }
        for(std::size_t i=0;part.vertexStride==16&&i<vertices.count;++i) {
            const auto at=i*16;
            HavokTriangleVertex v;
            v.unscaledGamePosition={Float(Index(vertices.bytes,at,4))/.0254,
                Float(Index(vertices.bytes,at+4,4))/.0254,Float(Index(vertices.bytes,at+8,4))/.0254};
            Need(Finite(v.unscaledGamePosition,limits.coordinateGameUnits),
                 "Triangle xyz is nonfinite/outside coordinate budget");
            v.rawWBits=Index(vertices.bytes,at+12,4);part.vertices.push_back(v);
        }
        if(!supported)return part;
        const auto selected=part.indexBytes==2?1:2,inactive=part.indexBytes==2?2:1;
        const auto &indices=part.arrays[selected];
        const std::size_t indexLength=std::size_t(indices.count)*part.indexBytes;
        Need(part.declaredTriangles<=indexLength/part.indexStride&&
             std::size_t(part.declaredTriangles)*part.indexStride==indexLength,
             "Triangle record/owned index byte counts disagree");
        if(part.arrays[inactive].count)Unknown(part,"additional-inactive-index-buffer");
        Budget(part.declaredTriangles,sizeof(HavokTriangle));part.triangles.reserve(part.declaredTriangles);
        const auto tailBytes=part.indexStride-3*part.indexBytes;
        if(tailBytes)Budget(part.declaredTriangles,tailBytes);
        for(std::size_t i=0;i<part.declaredTriangles;++i) {
            const auto at=i*part.indexStride;HavokTriangle triangle;
            for(int k=0;k<3;++k) {
                triangle.indices[k]=Index(indices.bytes,at+k*part.indexBytes,part.indexBytes);
                Need(triangle.indices[k]<part.vertices.size(),"Triangle index exceeds vertex count");
            }
            triangle.strideTail.assign(indices.bytes.begin()+at+3*part.indexBytes,
                indices.bytes.begin()+at+part.indexStride);
            part.triangles.push_back(std::move(triangle));
        }
        part.buffersDecoded=true;
        part.identityScaleBufferProfile=identity&&part.arrays[inactive].count==0;
        return part;
    }
    HavokTriangleStorage Storage(std::uint32_t object) {
        Need(Type(object)=="hkpStorageExtendedMeshShape","Selected triangle storage offset has unexpected type");
        HavokTriangleStorage storage;storage.sourceStorageOffset=object;Raw(storage.rawHeader,object);
        for(int k=0;k<4;++k)storage.rawScaleBits[k]=DU(object+96+k*4);
        storage.sourceScale={Float(storage.rawScaleBits[0]),Float(storage.rawScaleBits[1]),Float(storage.rawScaleBits[2])};
        storage.finiteScale=Finite(storage.sourceScale,(std::numeric_limits<double>::max)())&&
            std::isfinite(Float(storage.rawScaleBits[3]));
        storage.identityScale=storage.rawScaleBits[0]==0x3f800000u&&storage.rawScaleBits[1]==0x3f800000u&&
            storage.rawScaleBits[2]==0x3f800000u&&storage.rawScaleBits[3]==0x3f800000u;
        const auto count=DU(object+152),ownedCount=DU(object+196);
        Need(count==ownedCount,"Triangle record/owned subpart counts disagree");
        Add(totalSubparts,count,limits.subparts,"Triangle subpart aggregate budget exceeded");
        result.declaredSubparts+=count;
        if(!count)return storage;
        const auto records=Pointer(object+148),owned=Pointer(object+192);
        ArrayRange(records,count,64);ArrayRange(owned,count,4);
        Budget(count,sizeof(HavokTriangleSubpart));storage.subparts.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            auto part=Subpart(object,records+static_cast<std::uint32_t>(i*64),Pointer(owned+i*4),i,storage.identityScale);
            if(part.buffersDecoded) {
                ++result.decodedSubparts;result.decodedTriangles+=part.triangles.size();
            }
            result.decodedVertices+=part.vertices.size();
            if(part.identityScaleBufferProfile)++result.qualifiedIdentitySubparts;
            else result.completeBufferCoverage=false;
            storage.subparts.push_back(std::move(part));
        }
        return storage;
    }
    HavokTriangleMeshDocument Decode(std::vector<std::uint32_t> selected) {
        if(selected.empty())for(const auto &o:objects)if(o.second=="hkpStorageExtendedMeshShape")selected.push_back(o.first);
        Need(selected.size()<=limits.storageShapes,"Triangle storage-object budget exceeded");
        std::set<std::uint32_t> unique;
        for(const auto offset:selected)Need(unique.insert(offset).second,"Duplicate triangle storage selection");
        Budget(selected.size(),sizeof(HavokTriangleStorage));result.storages.reserve(selected.size());
        for(const auto offset:selected)result.storages.push_back(Storage(offset));
        return std::move(result);
    }
};
}
HavokTriangleMeshDocument DecodeHavokTriangleMeshes(const std::vector<unsigned char> &hkxs,
    const std::vector<std::uint32_t> &selectedStorageOffsets,const HavokTriangleMeshLimits &limits) {
    return Reader(hkxs,limits).Decode(selectedStorageOffsets);
}
} // namespace wolf2009

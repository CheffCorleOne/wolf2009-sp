// SPDX-License-Identifier: GPL-3.0-or-later
// Independent portable geometry reader, sharing HavokPack's checked fixups and
// the already measured model primitive layouts. No Havok SDK implementation.
#include "WorldCollisionGeometry.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
const double units = 1/.0254;
void Need(bool b,const char *message) {if(!b) throw std::runtime_error(message);}
CollisionVec3 Add(CollisionVec3 a,CollisionVec3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
CollisionVec3 Mul(CollisionVec3 a,double k) {return {a.x*k,a.y*k,a.z*k};}
double Dot(CollisionVec3 a,CollisionVec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
CollisionVec3 Cross(CollisionVec3 a,CollisionVec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double Component(CollisionVec3 a,int i) {return i==0?a.x:i==1?a.y:a.z;}
CollisionVec3 Rotate(const CollisionRigidTransform &t,CollisionVec3 p) {
    return Add(Add(Mul(t.axis[0],p.x),Mul(t.axis[1],p.y)),Mul(t.axis[2],p.z));
}
bool Finite(CollisionVec3 p,double maximum) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::abs(p.x)<=maximum&&std::abs(p.y)<=maximum&&std::abs(p.z)<=maximum;
}
std::string Fold(std::string s) {
    for(char &c:s) {if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(c=='\\')c='/';}
    return s;
}
void Rigid(const CollisionRigidTransform &t,double bound) {
    Need(Finite(t.translation,bound),"World collision translation exceeds budget");
    for(int i=0;i<3;++i) for(int j=0;j<3;++j)
        Need(Finite(t.axis[i],2)&&std::abs(Dot(t.axis[i],t.axis[j])-(i==j?1:0))<=.00002,
             "World collision transform is scaled/nonorthonormal");
    Need(std::abs(Dot(t.axis[0],Cross(t.axis[1],t.axis[2]))-1)<=.00002,
         "World collision transform is reflected/improper");
}
std::uint64_t Mix(std::uint64_t hash,std::uint64_t value) {
    for(int i=0;i<8;++i) {hash^=static_cast<unsigned char>(value>>(i*8));hash*=1099511628211ull;}
    return hash;
}
double FloatHalfUlp(double value) {
    const float f=static_cast<float>(value),infinity=std::numeric_limits<float>::infinity();
    const double above=std::nextafter(f,infinity),below=std::nextafter(f,-infinity);
    return .5*std::max(std::abs(above-f),std::abs(double(f)-below));
}
double PlaneFloat32ErrorBound(const ConvexContactPlane &plane,CollisionVec3 v) {
    // Half-ULP intervals bound independent source-coordinate/coefficient
    // rounding. gamma(7) additionally bounds a float32 three-product dot plus
    // plane offset. This is a numeric bound, not a geometric inflation.
    double inputError=FloatHalfUlp(plane.distance/units)*units;
    double magnitude=std::abs(plane.distance)+inputError;
    for(int k=0;k<3;++k) {
        const double n=Component(plane.normal,k),x=Component(v,k);
        const double dn=FloatHalfUlp(n),dx=FloatHalfUlp(x/units)*units;
        inputError+=std::abs(n)*dx+std::abs(x)*dn+dn*dx;
        magnitude+=(std::abs(n)+dn)*(std::abs(x)+dx);
    }
    const double epsilon=std::numeric_limits<float>::epsilon();
    return inputError+(7*epsilon/(1-7*epsilon))*magnitude;
}
struct Reader : detail::HavokPack {
    const WorldCollisionGeometryLimits &limits;
    WorldCollisionGeometry result;
    std::vector<std::uint32_t> stack;
    std::vector<WorldCollisionTransformRecord> transforms;
    std::uint64_t fingerprint=14695981039346656037ull,occurrence=0;
    std::size_t vertices=0,planes=0,planeVertexChecks=0,supportVertices=0;
    Reader(const std::vector<unsigned char> &b,const WorldCollisionGeometryLimits &l)
        :HavokPack(b,l.graph.sourceBytes,l.graph.objects,l.graph.fixups),limits(l) {
        Need(l.primitives&&l.omissions&&l.verticesPerHull>=4&&l.planesPerHull>=4&&
             l.totalVertices>=4&&l.totalPlanes>=4&&l.totalSupportVertices>=4&&l.graph.coordinateGameUnits>0&&
             std::isfinite(l.graph.coordinateGameUnits)&&l.maximumRadiusGameUnits>=0&&
             std::isfinite(l.maximumRadiusGameUnits)&&l.minimumPlaneToleranceGameUnits>=0&&
             std::isfinite(l.minimumPlaneToleranceGameUnits),"Invalid world geometry limits");
        for(const auto c:b) {fingerprint^=c;fingerprint*=1099511628211ull;}
        result.sourceFingerprint=fingerprint;result.sourceBytes=b.size();
    }
    CollisionVec3 V(std::size_t p) const {return {F(p),F(p+4),F(p+8)};}
    void Count(std::map<std::string,std::size_t> &map,const std::string &key,std::size_t count=1) {
        Need(count<=(std::numeric_limits<std::size_t>::max)()-map[key],"World collision count overflow");
        map[key]+=count;
    }
    void Missing(const std::string &type,std::uint32_t object,const CollisionRigidTransform &t,
                 std::uint32_t contents,bool known,std::size_t sub=static_cast<std::size_t>(-1),
                 std::size_t triangles=0) {
        Need(result.omissions.size()<limits.omissions,"World collision omission budget exceeded");
        WorldCollisionOmission o;
        o.type=type;o.sourceShapeOffset=object;o.sourceSubpartIndex=sub;o.triangles=triangles;
        o.occurrence=occurrence;o.placement=t;o.rawContents=contents;o.contentsKnown=known;
        o.hasWrapperRadiusMetadata=true;
        for(const auto &wrapper:transforms) if(wrapper.hasRadius) {
            if(!o.wrapperRadiusCount)o.minimumWrapperRadius=o.maximumWrapperRadius=wrapper.radius;
            else {
                o.minimumWrapperRadius=std::min(o.minimumWrapperRadius,wrapper.radius);
                o.maximumWrapperRadius=std::max(o.maximumWrapperRadius,wrapper.radius);
            }
            ++o.wrapperRadiusCount;
        }
        result.omissions.push_back(o);Count(result.unsupported,type);result.geometryComplete=false;
        Need(triangles<=(std::numeric_limits<std::size_t>::max)()-result.omittedTriangles,
             "World omitted triangle count overflow");
        result.omittedTriangles+=triangles;
    }
    void TransformVisit() {
        Need(++result.transformVisits<=limits.graph.transforms,"World collision transform budget exceeded");
    }
    void IdentityBody(std::uint32_t body) {
        DataRange(body,512);
        for(int row=0;row<3;++row) for(int col=0;col<4;++col)
            Need(std::abs(F(body+224+row*16+col*4)-(row==col?1.:0.))<=1e-6,
                 "Unverified nonidentity world-body matrix");
        if(limits.namedLocalBodyShapeFrame){
            for(int k=0;k<3;++k)Need(std::abs(F(body+272+k*4))<=1e-6,"Nonlocal resource body origin");
            const double padding=F(body+284);
            Need(padding==0||padding==1,"Resource body origin padding differs");
            // Swept centres contain COM, not the model origin. The inventory
            // owns those raw values; this explicit decoder returns local shape
            // coordinates and never pretends to restore source dynamics.
            result.identityBodyPoseVerified=true;return;
        }
        for(std::size_t p=272;p<=304;p+=16) for(int k=0;k<4;++k)
            Need(std::abs(F(body+p+k*4))<=1e-6,"Unverified nonzero world-body translation");
        for(std::size_t p=320;p<=336;p+=16) for(int k=0;k<4;++k)
            Need(std::abs(F(body+p+k*4)-(k==3?1.:0.))<=1e-6,
                 "Unverified swept world-body rotation");
        result.identityBodyPoseVerified=true;
    }
    double Radius(std::uint32_t object) const {
        const auto r=F(object+16)*units;
        Need(r>=0&&r<=limits.maximumRadiusGameUnits,"World convex radius exceeds budget");
        return r;
    }
    void Store(ConvexContactHull local,WorldCollisionPrimitiveKind kind,
               const CollisionRigidTransform &placement,std::uint64_t path,
               std::vector<CollisionVec3> additionalSupport={}) {
        Need(result.primitives.size()<limits.primitives,"World primitive count budget exceeded");
        Need(local.vertices.size()<=limits.totalVertices-vertices&&
             local.planes.size()<=limits.totalPlanes-planes,"World hull aggregate budget exceeded");
        Need(local.vertices.size()<=limits.totalSupportVertices-supportVertices&&
             additionalSupport.size()<=limits.totalSupportVertices-supportVertices-local.vertices.size(),
             "World support vertex aggregate budget exceeded");
        supportVertices+=local.vertices.size()+additionalSupport.size();
        Need(local.planes.empty()||local.vertices.size()<=
             (limits.planeVertexChecks-planeVertexChecks)/local.planes.size(),
             "World plane/vertex validation work budget exceeded");
        planeVertexChecks+=local.vertices.size()*local.planes.size();
        vertices+=local.vertices.size();planes+=local.planes.size();
        local.sourceRegion=Mix(Mix(fingerprint,result.sourceBodyOffset),path);
        if(!local.sourceRegion)local.sourceRegion=1;
        WorldCollisionPrimitive p;
        p.kind=kind;p.local=std::move(local);p.world=p.local;p.placement=placement;
        p.additionalSupportVertices=std::move(additionalSupport);p.supportArrayComplete=true;
        p.transforms=transforms;p.occurrence=occurrence;
        p.volumeProfileVerified=p.local.vertices.size()>=4&&p.local.planes.size()>=4;
        if(!p.volumeProfileVerified)
            Missing("unverified-lower-dimensional-convex",p.local.sourceShapeOffset,placement,
                    p.local.rawContents,p.local.contentsKnown);
        for(const auto &wrapper:transforms) if(wrapper.hasRadius&&
            std::abs(wrapper.radius-p.local.radius)>1e-7*units) p.radiusHierarchyVerified=false;
        if(!p.radiusHierarchyVerified)
            Missing("unverified-wrapper-radius",p.local.sourceShapeOffset,placement,
                    p.local.rawContents,p.local.contentsKnown);
        for(auto &v:p.world.vertices) {
            v=TransformCollisionPoint(placement,v);
            Need(Finite(v,limits.graph.coordinateGameUnits),"World hull vertex exceeds budget");
        }
        for(auto &plane:p.world.planes) {
            plane.normal=Rotate(placement,plane.normal);
            plane.distance-=Dot(plane.normal,placement.translation);
            Need(std::isfinite(plane.distance)&&std::abs(plane.distance)<=limits.graph.coordinateGameUnits,
                 "World hull plane exceeds budget");
        }
        // Source coordinates and plane equations are float32. Preserve them
        // exactly; this bound only validates their finite rounding residual.
        for(const auto &plane:p.local.planes) for(const auto &v:p.local.vertices) {
            const auto value=Dot(plane.normal,v)+plane.distance;
            const double tolerance=limits.minimumPlaneToleranceGameUnits+
                PlaneFloat32ErrorBound(plane,v);
            if(value>p.maximumPlaneVertexResidual) {
                p.maximumPlaneVertexResidual=value;p.float32ErrorBoundAtMaximumResidual=tolerance;
            }
            p.maximumPlaneVertexBoundExcess=std::max(p.maximumPlaneVertexBoundExcess,value-tolerance);
            if(value>tolerance)p.sourcePlanesConsistent=false;
        }
        if(!p.sourcePlanesConsistent)
            Missing("unverified-source-plane-residual",p.local.sourceShapeOffset,placement,
                    p.local.rawContents,p.local.contentsKnown);
        result.primitives.push_back(std::move(p));
    }
    ConvexContactHull Base(std::uint32_t object,std::uint32_t contents,bool known) const {
        ConvexContactHull h;
        h.sourceShapeOffset=object;h.rawContents=contents;h.contentsKnown=known;h.radius=Radius(object);
        return h;
    }
    void Box(std::uint32_t object,const CollisionRigidTransform &t,std::uint32_t contents,
             bool known,std::uint64_t path) {
        DataRange(object,48);
        Need(limits.verticesPerHull>=8&&limits.planesPerHull>=6,"World box budget too small");
        const auto half=Mul(V(object+32),units);
        Need(Finite(half,limits.graph.coordinateGameUnits)&&half.x>=0&&half.y>=0&&half.z>=0,
             "Invalid world box extents");
        auto h=Base(object,contents,known);
        for(int bits=0;bits<8;++bits)
            h.vertices.push_back({half.x*(bits&1?1:-1),half.y*(bits&2?1:-1),half.z*(bits&4?1:-1)});
        for(int k=0;k<3;++k) for(int sign=-1;sign<=1;sign+=2) {
            ConvexContactPlane p;
            p.normal={k==0?double(sign):0,k==1?double(sign):0,k==2?double(sign):0};
            p.distance=-Component(half,k);h.planes.push_back(p);
        }
        Store(std::move(h),WorldCollisionPrimitiveKind::Box,t,path);
    }
    void Hull(std::uint32_t object,const CollisionRigidTransform &t,std::uint32_t contents,
              bool known,std::uint64_t path) {
        DataRange(object,96);
        const std::size_t blocks=DU(object+68),count=DU(object+76),planeCount=DU(object+84);
        if(!(count<=limits.verticesPerHull&&
             blocks<=limits.verticesPerHull/4+(limits.verticesPerHull%4!=0)&&
             count/4+(count%4!=0)<=blocks))
            throw std::runtime_error("Invalid world convex vertex/block counts: shape="+
                std::to_string(object)+" count="+std::to_string(count)+" blocks="+std::to_string(blocks));
        if(planeCount>limits.planesPerHull)
            throw std::runtime_error("World convex plane budget exceeded: shape="+
                std::to_string(object)+" planes="+std::to_string(planeCount));
        const auto vertexArray=blocks?Pointer(object+64):0,planeArray=planeCount?Pointer(object+80):0;
        if(blocks)ArrayRange(vertexArray,blocks,48);
        if(planeCount)ArrayRange(planeArray,planeCount,16);
        auto h=Base(object,contents,known);
        for(std::size_t i=0;i<count;++i) {
            const auto block=vertexArray+(i/4)*48,lane=(i%4)*4;
            const CollisionVec3 v={F(block+lane)*units,F(block+16+lane)*units,F(block+32+lane)*units};
            Need(Finite(v,limits.graph.coordinateGameUnits),"Local world hull vertex exceeds budget");
            h.vertices.push_back(v);
        }
        std::vector<CollisionVec3> additionalSupport;
        Need(blocks<=(std::numeric_limits<std::size_t>::max)()/4,"World support lane count overflow");
        const std::size_t supportCount=blocks*4;
        Need(supportCount<=limits.totalSupportVertices-supportVertices,
             "World support vertex aggregate budget exceeded");
        for(std::size_t i=count;i<supportCount;++i) {
            const auto block=vertexArray+(i/4)*48,lane=(i%4)*4;
            const CollisionVec3 v={F(block+lane)*units,F(block+16+lane)*units,F(block+32+lane)*units};
            Need(Finite(v,limits.graph.coordinateGameUnits),"Additional world support vertex exceeds budget");
            additionalSupport.push_back(v);
        }
        for(std::size_t i=0;i<planeCount;++i) {
            const auto at=planeArray+i*16;
            ConvexContactPlane p;p.normal=V(at);p.distance=F(at+12)*units;
            Need(std::abs(Dot(p.normal,p.normal)-1)<=.00002,"World convex plane normal is not unit");
            Need(std::abs(p.distance)<=limits.graph.coordinateGameUnits,"Local world plane exceeds budget");
            h.planes.push_back(p);
        }
        Store(std::move(h),WorldCollisionPrimitiveKind::ConvexVertices,t,path,std::move(additionalSupport));
    }
    bool StorageContents(std::uint32_t object,std::size_t parts,std::size_t index,
                         std::size_t sub,std::size_t children,std::uint32_t childArray,
                         std::vector<std::uint32_t> &flags) const {
        const auto a=pointers.find(object+204);
        if(a==pointers.end()||DU(object+208)!=parts)return false;
        ArrayRange(a->second,parts,4);const auto storage=Pointer(a->second+index*4);
        if(Type(storage)!="hkpStorageExtendedMeshShapeShapeSubpartStorage")return false;
        DataRange(storage,56);const auto materials=DU(storage+36);
        Need(materials<=limits.graph.compoundEntries,"World material table budget exceeded");
        const bool explicitStrides=DU(sub)==0x00040101u&&DU(sub+8)==(materials<<16|1u);
        // The observed zero-stride profile owns exactly one shape, index and
        // uint32 material. Only element zero is addressed, so both strides are
        // immaterial. This does not infer broadcasting for a larger array.
        const bool singleton=children==1&&materials==1&&DU(sub)==0x00000101u&&
            DU(sub+8)==0x00010000u&&DU(sub+4)==0&&DU(sub+12)==0&&
            pointers.find(sub+4)==pointers.end()&&pointers.find(sub+12)==pointers.end();
        if((!explicitStrides&&!singleton)||materials==0||materials>65535||
           DU(storage+12)!=children||DU(storage+24)!=children||DU(storage+48)!=0)return false;
        const auto shapes=Pointer(storage+8),indices=Pointer(storage+20),table=Pointer(storage+32);
        ArrayRange(shapes,children,4);ArrayRange(indices,children,1);ArrayRange(table,materials,4);
        for(std::size_t i=0;i<children;++i) {
            Need(Pointer(shapes+i*4)==Pointer(childArray+i*4),"World material/shape instances disagree");
            const auto material=bytes[sections[2].absolute+indices+i];
            Need(material<materials,"World material index exceeds table");
            flags.push_back(DU(table+material*4));
        }
        return true;
    }
    void Storage(std::uint32_t object,const CollisionRigidTransform &parent,std::uint32_t contents,
                 bool known,std::uint64_t path) {
        DataRange(object,212);
        for(int k=0;k<3;++k) if(std::abs(F(object+96+k*4)-1)>1e-6) {
            Missing("scaled-storage-mesh",object,parent,contents,known);return;
        }
        const auto triangles=DU(object+152),parts=DU(object+164);
        Need(triangles<=limits.graph.compoundEntries&&parts<=limits.graph.compoundEntries,
             "World storage subpart count exceeds budget");
        if(triangles) {
            const auto array=Pointer(object+148);ArrayRange(array,triangles,64);
            for(std::size_t i=0;i<triangles;++i)
                Missing("storage-triangle-subpart",object,parent,0,false,i,DU(array+i*64+16));
        }
        if(!parts)return;
        const auto array=Pointer(object+160);ArrayRange(array,parts,64);
        for(std::size_t i=0;i<parts;++i) {
            TransformVisit();const auto sub=array+i*64;
            const auto children=DU(sub+20);
            Need(children<=limits.graph.compoundEntries,"World storage child count exceeds budget");
            WorldCollisionTransformRecord record;
            record.kind=WorldCollisionTransformKind::StorageQuaternion;
            record.sourceObjectOffset=object;record.sourceSubpartIndex=i;
            const double x=F(sub+32),y=F(sub+36),z=F(sub+40),w=F(sub+44);
            record.quaternion[0]=x;record.quaternion[1]=y;record.quaternion[2]=z;record.quaternion[3]=w;
            Need(std::abs(x*x+y*y+z*z+w*w-1)<=.00001,"Nonunit storage placement quaternion");
            record.local.axis[0]={1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)};
            record.local.axis[1]={2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)};
            record.local.axis[2]={2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)};
            record.local.translation=Mul(V(sub+48),units);
            Rigid(record.local,limits.graph.coordinateGameUnits);
            const auto combined=ComposeCollisionTransforms(parent,record.local);
            Rigid(combined,limits.graph.coordinateGameUnits);
            if(!children)continue;
            const auto childArray=Pointer(sub+16);ArrayRange(childArray,children,4);
            std::vector<std::uint32_t> flags;
            const bool verified=StorageContents(object,parts,i,sub,children,childArray,flags);
            if(!verified) {result.contentsComplete=false;Count(result.unsupported,"unverified-storage-contents");}
            transforms.push_back(record);
            for(std::size_t j=0;j<children;++j)
                Shape(Pointer(childArray+j*4),combined,verified?flags[j]:0,verified,
                      Mix(Mix(path,i),j));
            transforms.pop_back();
        }
    }
    void Shape(std::uint32_t object,const CollisionRigidTransform &t,std::uint32_t contents,
               bool known,std::uint64_t path) {
        Need(++result.visitedShapes<=limits.graph.shapeVisits,"World shape visit budget exceeded");
        Need(stack.size()<limits.graph.nesting&&std::find(stack.begin(),stack.end(),object)==stack.end(),
             "World shape graph cycle/nesting limit");
        stack.push_back(object);++occurrence;path=Mix(path,object);
        const auto type=Type(object);Count(result.shapeOccurrences,type);
        if(!known)result.contentsComplete=false;
        if(type=="hkpMoppBvTreeShape") Shape(Pointer(object+52),t,contents,known,Mix(path,0));
        else if(type=="hkpListShape") {
            const auto count=DU(object+28);
            Need(count<=limits.graph.compoundEntries,"World list child budget exceeded");
            if(count) {
                const auto array=Pointer(object+24);ArrayRange(array,count,16);
                for(std::size_t i=0;i<count;++i) {
                    const auto flags=DU(array+i*16+4);
                    Shape(Pointer(array+i*16),t,flags,flags!=0,Mix(path,i));
                }
            }
        } else if(type=="hkpConvexTranslateShape"||type=="hkpConvexTransformShape") {
            TransformVisit();WorldCollisionTransformRecord record;
            record.sourceObjectOffset=object;record.radius=Radius(object);record.hasRadius=true;
            if(type=="hkpConvexTranslateShape") {
                DataRange(object,48);record.kind=WorldCollisionTransformKind::Translate;
                record.local.translation=Mul(V(object+32),units);
            } else {
                DataRange(object,96);record.kind=WorldCollisionTransformKind::ConvexMatrix;
                for(int k=0;k<3;++k) {
                    record.local.axis[k]=V(object+32+k*16);
                    Need(std::abs(F(object+44+k*16))<=1e-7,"Unverified convex matrix padding");
                }
                record.local.translation=Mul(V(object+80),units);
                Need(std::abs(F(object+92))<=1e-7,"Unverified convex translation padding");
            }
            Rigid(record.local,limits.graph.coordinateGameUnits);
            const auto combined=ComposeCollisionTransforms(t,record.local);
            Rigid(combined,limits.graph.coordinateGameUnits);
            transforms.push_back(record);
            Shape(Pointer(object+24),combined,contents,known,Mix(path,0));
            transforms.pop_back();
        } else if(type=="hkpBoxShape") Box(object,t,contents,known,path);
        else if(type=="hkpConvexVerticesShape") Hull(object,t,contents,known,path);
        else if(type=="hkpStorageExtendedMeshShape") Storage(object,t,contents,known,path);
        else Missing(type,object,t,contents,known);
        stack.pop_back();
    }
    WorldCollisionGeometry Decode(const std::string &name) {
        Need(!name.empty()&&name.size()<=4096,"Invalid world body name");
        std::size_t matches=0,bodies=0;
        for(const auto &object:objects) if(object.second=="hkpRigidBody") {
            ++bodies;DataRange(object.first,116);
            const auto pointer=pointers.find(object.first+112);
            if(pointer==pointers.end())continue;
            const auto bodyName=DataString(pointer->second);
            if(Fold(bodyName)==Fold(name)) {
                ++matches;result.sourceBodyOffset=object.first;result.selectedBodyName=bodyName;
            }
        }
        Need(matches==1,"Expected one exact named world rigid body");
        result.otherSourceRigidBodies=bodies-1;IdentityBody(result.sourceBodyOffset);
        result.rootShapeOffset=Pointer(result.sourceBodyOffset+16);result.geometryComplete=true;
        const auto contents=DU(result.sourceBodyOffset+44);
        result.rootRawFilterInfo=contents;result.rootRawFilterKnown=true;
        Shape(result.rootShapeOffset,CollisionRigidTransform(),contents,contents!=0,0);
        result.coverageComplete=result.geometryComplete&&result.contentsComplete;
        return std::move(result);
    }
};
}
CollisionVec3 TransformCollisionPoint(const CollisionRigidTransform &t,CollisionVec3 p) {
    return Add(t.translation,Rotate(t,p));
}
CollisionRigidTransform ComposeCollisionTransforms(const CollisionRigidTransform &parent,
                                                   const CollisionRigidTransform &child) {
    CollisionRigidTransform t;
    for(int i=0;i<3;++i)t.axis[i]=Rotate(parent,child.axis[i]);
    t.translation=TransformCollisionPoint(parent,child.translation);
    return t;
}
WorldCollisionGeometry DecodeWorldCollisionGeometry(const std::vector<unsigned char> &hkxs,
    const std::string &name,const WorldCollisionGeometryLimits &limits) {
    return Reader(hkxs,limits).Decode(name);
}
} // namespace wolf2009

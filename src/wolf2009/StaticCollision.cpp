// SPDX-License-Identifier: GPL-3.0-or-later
// Independent format observations and elementary AABB mathematics; no Havok
// implementation, SDK types, MOPP interpreter or original game code is used.
#include "StaticCollision.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>
namespace wolf2009 {
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double Axis(const CollisionVec3& v,int i){return i==0?v.x:i==1?v.y:v.z;}
CollisionVec3 Add(CollisionVec3 a,CollisionVec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CollisionVec3 Sub(CollisionVec3 a,CollisionVec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CollisionVec3 Mul(CollisionVec3 a,double n){return {a.x*n,a.y*n,a.z*n};}
bool Finite(CollisionVec3 v,double limit){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::abs(v.x)<=limit&&std::abs(v.y)<=limit&&std::abs(v.z)<=limit;}
std::string Name(std::string s){Need(!s.empty()&&s.size()<=4096&&s.find('\0')==std::string::npos,"Collision body name is empty/invalid");for(std::size_t i=0;i<s.size();++i){if(s[i]>='A'&&s[i]<='Z')s[i]=char(s[i]-'A'+'a');if(s[i]=='\\')s[i]='/';}return s;}
struct Section{std::uint32_t absolute,local,global,virtuals,exports,imports,end;};
struct Reader{
    const std::vector<unsigned char>& bytes;const StaticCollisionLimits& limits;Section sections[3];
    std::map<std::uint32_t,std::uint32_t> pointers;std::map<std::uint32_t,std::string> objects;
    StaticCollisionScene result;std::vector<std::uint32_t> stack;std::size_t transformVisits=0,fixupCount=0;
    Reader(const std::vector<unsigned char>& value,const StaticCollisionLimits& bounds):bytes(value),limits(bounds){}
    void Range(std::size_t pos,std::size_t size)const{Need(pos<=bytes.size()&&size<=bytes.size()-pos,"HKXS file range is truncated");}
    void DataRange(std::size_t pos,std::size_t size)const{Need(pos<=sections[2].local&&size<=sections[2].local-pos,"HKXS data range exceeds section");}
    void ArrayRange(std::size_t pos,std::size_t count,std::size_t stride)const{Need(stride&&count<=sections[2].local/stride,"HKXS array byte size exceeds data section");DataRange(pos,count*stride);}
    std::uint32_t U(std::size_t pos)const{Range(pos,4);return std::uint32_t(bytes[pos])|(std::uint32_t(bytes[pos+1])<<8)|(std::uint32_t(bytes[pos+2])<<16)|(std::uint32_t(bytes[pos+3])<<24);}
    std::uint32_t DU(std::size_t pos)const{DataRange(pos,4);return U(sections[2].absolute+pos);}
    double F(std::size_t pos)const{const std::uint32_t bits=DU(pos);float value;static_assert(sizeof(float)==4,"IEEE32 float required");std::memcpy(&value,&bits,4);Need(std::isfinite(value),"Non-finite collision coordinate");return value;}
    CollisionVec3 V(std::size_t pos)const{return {F(pos),F(pos+4),F(pos+8)};}
    std::string String(std::size_t absolute,std::size_t end)const{
        Need(absolute<end&&end<=bytes.size(),"HKXS string range invalid");std::string text;
        while(absolute<end&&bytes[absolute]){Need(text.size()<4096,"HKXS string limit exceeded");Need(bytes[absolute]>=32&&bytes[absolute]<=126,"Non-printable HKXS type/name string");text+=char(bytes[absolute++]);}
        Need(absolute<end,"Unterminated HKXS string");return text;
    }
    std::uint32_t Pointer(std::size_t source)const{DataRange(source,4);const std::map<std::uint32_t,std::uint32_t>::const_iterator i=pointers.find(static_cast<std::uint32_t>(source));Need(i!=pointers.end(),"Required HKXS pointer fixup missing");return i->second;}
    std::string Type(std::uint32_t object)const{const std::map<std::uint32_t,std::string>::const_iterator i=objects.find(object);return i==objects.end()?"untyped-shape":i->second;}
    void Diagnostic(const std::string& message){if(result.diagnostics.size()<32)result.diagnostics.push_back(message);}
    void Unsupported(const std::string& type,std::size_t count=1){Need(count<=(std::numeric_limits<std::size_t>::max)()-result.unsupported[type],"Unsupported shape count overflow");result.unsupported[type]+=count;result.coverageComplete=false;}
    void Padding(std::size_t begin,std::size_t end)const{for(std::size_t p=begin;p<end;++p)Need(bytes[p]==255,"Unexpected HKXS fixup padding");}
    void Fixups(std::uint32_t begin,std::uint32_t end,bool global){
        const std::size_t stride=global?12:8,absolute=sections[2].absolute;std::size_t p=absolute+begin,stop=absolute+end;
        while(p+stride<=stop){const std::uint32_t source=U(p);if(source==0xffffffffu){Padding(p,stop);return;}
            Need(++fixupCount<=limits.fixups,"HKXS fixup count limit exceeded");DataRange(source,4);
            const std::uint32_t section=global?U(p+4):2,destination=U(p+(global?8:4));Need(section<3&&destination<sections[section].local,"HKXS pointer destination outside section");
            // The measured selected shape graph only has data-section pointers.
            if(section==2)Need(pointers.insert(std::make_pair(source,destination)).second,"Duplicate/ambiguous HKXS pointer source");
            p+=stride;
        }Padding(p,stop);
    }
    void Header(){
        Need(bytes.size()>=208&&bytes.size()<=limits.sourceBytes,"HKXS byte limit/header size invalid");
        Need(U(0)==0x57e0e057&&U(4)==0x10c0c010,"Expected Havok packfile magic");
        Need(U(12)==5&&bytes[16]==4&&bytes[17]==1&&bytes[18]==0&&bytes[19]==1,"Unsupported HKXS version/layout profile");
        Need(String(40,56)=="Havok-5.6.0-r1"&&U(20)==3,"Unsupported Havok contents version or section count");
        const char* names[3]={"__classnames__","__types__","__data__"};
        for(std::size_t i=0;i<3;++i){const std::size_t h=64+i*48;Need(String(h,h+20)==names[i],"Unexpected HKXS section order/name");Section& s=sections[i];
            s.absolute=U(h+20);s.local=U(h+24);s.global=U(h+28);s.virtuals=U(h+32);s.exports=U(h+36);s.imports=U(h+40);s.end=U(h+44);
            Need(s.local<=s.global&&s.global<=s.virtuals&&s.virtuals<=s.exports&&s.exports<=s.imports&&s.imports<=s.end,"HKXS section offsets are not monotonic");Range(s.absolute,s.end);
        }
        Need(sections[0].absolute>=208&&sections[0].absolute+std::size_t(sections[0].end)<=sections[2].absolute&&sections[1].end==0,"Unsupported overlapping/reflection HKXS sections");
        Need(sections[2].absolute+std::size_t(sections[2].end)==bytes.size()&&sections[2].exports==sections[2].imports&&sections[2].imports==sections[2].end,"HKXS imports/exports/trailing data unsupported");
        Fixups(sections[2].local,sections[2].global,false);Fixups(sections[2].global,sections[2].virtuals,true);
        std::size_t p=sections[2].absolute+std::size_t(sections[2].virtuals),end=sections[2].absolute+std::size_t(sections[2].exports);
        while(p+12<=end){const std::uint32_t object=U(p);if(object==0xffffffffu){Padding(p,end);break;}
            Need(objects.size()<limits.objects,"HKXS typed object limit exceeded");DataRange(object,4);const std::uint32_t section=U(p+4),name=U(p+8);Need(section==0&&name<sections[0].local,"HKXS class fixup is outside class-name section");
            const std::string type=String(sections[0].absolute+name,sections[0].absolute+std::size_t(sections[0].local));Need(!type.empty()&&type.size()<=256,"HKXS class-name size limit exceeded");Need(objects.insert(std::make_pair(object,type)).second,"Duplicate HKXS typed object");p+=12;
        }Padding(p,end);
        Need(U(24)==2&&U(32)==0&&Type(U(28))=="hkRootLevelContainer"&&U(36)<sections[0].local,"Unsupported HKXS root container");
        Need(String(sections[0].absolute+U(36),sections[0].absolute+std::size_t(sections[0].local))=="hkRootLevelContainer","HKXS root class-name contradicts object type");
    }
    void IdentityBody(std::uint32_t body){
        DataRange(body,512);
        // Measured main-level-body layout on empty and mtw_ware: the transform
        // matrix is at +224, followed by translation/swept centres/quaternions.
        // Unsupported poses fail explicitly rather than silently using identity.
        for(int row=0;row<3;++row)for(int column=0;column<4;++column)Need(std::abs(F(body+224+row*16+column*4)-(row==column?1.0:0.0))<=1e-6,"Non-identity/unverified level rigid-body matrix is unsupported");
        for(std::size_t offset=272;offset<=304;offset+=16)for(int k=0;k<4;++k)Need(std::abs(F(body+offset+k*4))<=1e-6,"Nonzero/unverified level rigid-body translation is unsupported");
        for(std::size_t offset=320;offset<=336;offset+=16)for(int k=0;k<4;++k)Need(std::abs(F(body+offset+k*4)-(k==3?1.0:0.0))<=1e-6,"Non-identity/unverified swept rigid-body rotation is unsupported");
        result.identityBodyPoseVerified=true;
    }
    void TransformBudget(){Need(++transformVisits<=limits.transforms,"Collision transform budget exceeded");}
    void Shape(std::uint32_t object,CollisionVec3 translation){
        Need(++result.visitedShapes<=limits.shapeVisits,"Collision shape visit budget exceeded");Need(stack.size()<limits.nesting,"Collision graph nesting limit exceeded");
        Need(std::find(stack.begin(),stack.end(),object)==stack.end(),"Cycle in collision shape graph");stack.push_back(object);
        const std::string type=Type(object);
        if(type=="hkpMoppBvTreeShape")Shape(Pointer(object+52),translation);
        else if(type=="hkpListShape"){
            const std::uint32_t count=DU(object+28);Need(count<=limits.compoundEntries,"Collision list entry limit exceeded");
            if(count){const std::uint32_t array=Pointer(object+24);ArrayRange(array,count,16);for(std::uint32_t i=0;i<count;++i)Shape(Pointer(array+std::size_t(i)*16),translation);}
        }else if(type=="hkpConvexTranslateShape"){
            TransformBudget();const CollisionVec3 t=Add(translation,V(object+32));Need(Finite(Mul(t,1.0/.0254),limits.coordinateGameUnits),"Collision translation exceeds coordinate limit");Shape(Pointer(object+24),t);
        }else if(type=="hkpBoxShape"){
            const CollisionVec3 half=V(object+32);Need(half.x>=0&&half.y>=0&&half.z>=0,"Negative collision box half-extents");
            if(std::abs(F(object+16))>1e-7)Unsupported("box-with-unverified-convex-radius");
            else{Need(result.boxes.size()<limits.boxes,"Collision box count limit exceeded");StaticCollisionBox box;box.minimum=Mul(Sub(translation,half),1.0/.0254);box.maximum=Mul(Add(translation,half),1.0/.0254);box.sourceShapeOffset=object;
                Need(Finite(box.minimum,limits.coordinateGameUnits)&&Finite(box.maximum,limits.coordinateGameUnits),"Collision bounds exceed coordinate limit");result.boxes.push_back(box);}
        }else if(type=="hkpStorageExtendedMeshShape")Storage(object,translation);
        else Unsupported(type);
        stack.pop_back();
    }
    void Storage(std::uint32_t object,CollisionVec3 parentTranslation){
        for(int k=0;k<3;++k)if(std::abs(F(object+96+k*4)-1)>1e-6){Unsupported("scaled-storage-mesh");Diagnostic("StorageExtendedMesh scaling is unsupported");return;}
        const std::uint32_t triangles=DU(object+152);Need(triangles<=limits.compoundEntries,"Collision triangle-subpart limit exceeded");
        if(triangles){const std::uint32_t array=Pointer(object+148);ArrayRange(array,triangles,64);for(std::uint32_t i=0;i<triangles;++i){const std::size_t n=DU(array+std::size_t(i)*64+16);Need(n<=(std::numeric_limits<std::size_t>::max)()-result.omittedTriangles,"Omitted triangle count overflow");result.omittedTriangles+=n;}Unsupported("storage-triangle-subpart",triangles);}
        const std::uint32_t count=DU(object+164);Need(count<=limits.compoundEntries,"Collision placed-shape subpart limit exceeded");
        if(count){const std::uint32_t array=Pointer(object+160);ArrayRange(array,count,64);for(std::uint32_t i=0;i<count;++i){
            TransformBudget();const std::size_t sub=array+std::size_t(i)*64;const CollisionVec3 quaternion=V(sub+32),t=V(sub+48);const double w=F(sub+44);
            const bool identity=std::abs(quaternion.x)<=1e-6&&std::abs(quaternion.y)<=1e-6&&std::abs(quaternion.z)<=1e-6&&std::abs(std::abs(w)-1)<=1e-6&&std::abs(t.x)<=1e-6&&std::abs(t.y)<=1e-6&&std::abs(t.z)<=1e-6;
            const std::uint32_t children=DU(sub+20);Need(children<=limits.compoundEntries,"Collision placed child count limit exceeded");
            if(!identity){++result.omittedTransformedSubparts;Unsupported("transformed-shape-subpart");continue;}
            if(children){const std::uint32_t pointersArray=Pointer(sub+16);ArrayRange(pointersArray,children,4);for(std::uint32_t j=0;j<children;++j)Shape(Pointer(pointersArray+std::size_t(j)*4),parentTranslation);}
        }}
    }
    StaticCollisionScene Decode(const std::string& expected){
        Header();const std::string wanted=Name(expected);std::uint32_t selected=0;std::size_t matches=0,bodies=0;
        for(std::map<std::uint32_t,std::string>::const_iterator i=objects.begin();i!=objects.end();++i)if(i->second=="hkpRigidBody"){
            ++bodies;DataRange(i->first,116);
            const std::map<std::uint32_t,std::uint32_t>::const_iterator namePointer=pointers.find(i->first+112);
            // An unnamed, unselected body cannot satisfy the explicit request.
            // Count it but do not invent a name or use its transform/shape.
            if(namePointer==pointers.end())continue;
            const std::uint32_t name=namePointer->second;const std::string bodyName=String(sections[2].absolute+name,sections[2].absolute+std::size_t(sections[2].local));
            if(bodyName.empty())continue;
            if(Name(bodyName)==wanted){selected=i->first;result.selectedBodyName=bodyName;++matches;}
        }
        Need(matches==1,"Expected a unique explicitly named level rigid body");result.otherRigidBodies=bodies-1;IdentityBody(selected);result.coverageComplete=true;
        Shape(Pointer(selected+16),CollisionVec3());
        if(!result.coverageComplete)Diagnostic("Partial geometric coverage: misses do not establish a clear passage");
        Diagnostic("Queries omit original collision-filter masks, motion and unselected rigid bodies; game physics is not runtime-verified");return result;
    }
};
bool Intersect(const StaticCollisionBox& box,CollisionVec3 origin,CollisionVec3 delta,StaticCollisionHit& hit){
    const double epsilon=1e-8;bool inside=true;for(int i=0;i<3;++i)inside=inside&&Axis(origin,i)>Axis(box.minimum,i)+epsilon&&Axis(origin,i)<Axis(box.maximum,i)-epsilon;
    if(inside){hit.hit=true;hit.startedInside=true;hit.fraction=0;return true;}
    double enter=0,leave=1;CollisionVec3 normal;bool hasNormal=false;
    for(int i=0;i<3;++i){const double position=Axis(origin,i),movement=Axis(delta,i),lo=Axis(box.minimum,i),hi=Axis(box.maximum,i);
        if(std::abs(movement)<1e-12){if(position<=lo+epsilon||position>=hi-epsilon)return false;continue;}
        double a=(lo-position)/movement,b=(hi-position)/movement;double sign=-1;if(a>b){std::swap(a,b);sign=1;}
        if(a>enter+epsilon||(!hasNormal&&std::abs(a-enter)<=epsilon)){enter=(std::max)(0.0,a);normal=CollisionVec3(i==0?sign:0,i==1?sign:0,i==2?sign:0);hasNormal=true;}
        leave=(std::min)(leave,b);if(enter>leave+epsilon)return false;
    }
    if(!hasNormal||leave<=epsilon||enter<0||enter>1)return false;
    hit.hit=true;hit.fraction=enter;hit.normal=normal;return true;
}
StaticCollisionHit Query(const StaticCollisionScene& scene,CollisionVec3 origin,CollisionVec3 half,CollisionVec3 delta){
    Need(Finite(origin,1e12)&&Finite(half,1e12)&&Finite(delta,1e12)&&half.x>=0&&half.y>=0&&half.z>=0,"Invalid collision query coordinates/extents");
    StaticCollisionHit best;best.coverageComplete=scene.coverageComplete;
    for(std::size_t i=0;i<scene.boxes.size();++i){const StaticCollisionBox& original=scene.boxes[i];
        Need(Finite(original.minimum,1e12)&&Finite(original.maximum,1e12)&&original.minimum.x<=original.maximum.x&&original.minimum.y<=original.maximum.y&&original.minimum.z<=original.maximum.z,"Invalid collision scene box");
        StaticCollisionBox expanded=original;expanded.minimum=Sub(expanded.minimum,half);expanded.maximum=Add(expanded.maximum,half);StaticCollisionHit candidate;
        if(Intersect(expanded,origin,delta,candidate)&&(!best.hit||candidate.fraction<best.fraction)){candidate.boxIndex=i;candidate.coverageComplete=scene.coverageComplete;best=candidate;}
    }return best;
}
}
StaticCollisionScene DecodeStaticCollision(const std::vector<unsigned char>& bytes,const std::string& bodyName,const StaticCollisionLimits& limits){Need(std::isfinite(limits.coordinateGameUnits)&&limits.coordinateGameUnits>0,"Invalid collision coordinate budget");Reader reader(bytes,limits);return reader.Decode(bodyName);}
StaticCollisionHit RaycastStatic(const StaticCollisionScene& scene,CollisionVec3 origin,CollisionVec3 delta){return Query(scene,origin,CollisionVec3(),delta);}
StaticCollisionHit SweepStaticBox(const StaticCollisionScene& scene,CollisionVec3 center,CollisionVec3 half,CollisionVec3 delta){return Query(scene,center,half,delta);}
}

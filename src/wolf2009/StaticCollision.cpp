// SPDX-License-Identifier: GPL-3.0-or-later
// Independent format observations and elementary AABB mathematics; no Havok
// implementation, SDK types, MOPP interpreter or original game code is used.
#include "StaticCollision.h"
#include "HavokPack.h"
#include "ModelCollision.h"
#include "ModelConvexCollision.h"
#include "ModelPrimitiveCollision.h"
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
struct Reader : detail::HavokPack {
    const StaticCollisionLimits& limits;
    StaticCollisionScene result;std::vector<std::uint32_t> stack;std::size_t transformVisits=0;
    bool modelLocalOnly=false,geometryComplete=true;std::map<std::string,std::size_t> unsupportedGeometry;
    const ModelConvexCollisionLimits* convexLimits=nullptr;
    std::vector<ConvexContactHull> modelHulls;
    std::size_t hullVertices=0,hullPlanes=0;
    std::uint64_t sourceRegion=0;
    bool primitiveMode = false;
    std::vector<ModelCollisionPrimitive> modelPrimitives;
    Reader(const std::vector<unsigned char>& value,const StaticCollisionLimits& bounds)
        : HavokPack(value,bounds.sourceBytes,bounds.objects,bounds.fixups), limits(bounds) {}
    CollisionVec3 V(std::size_t pos)const{return {F(pos),F(pos+4),F(pos+8)};}
    void Diagnostic(const std::string& message){if(result.diagnostics.size()<32)result.diagnostics.push_back(message);}
    void Unsupported(const std::string& type,std::size_t count=1,bool geometric=true){Need(count<=(std::numeric_limits<std::size_t>::max)()-result.unsupported[type],"Unsupported shape count overflow");result.unsupported[type]+=count;result.coverageComplete=false;if(geometric){Need(count<=(std::numeric_limits<std::size_t>::max)()-unsupportedGeometry[type],"Unsupported geometry count overflow");unsupportedGeometry[type]+=count;geometryComplete=false;}}
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
    void LocalModelBody(std::uint32_t body){
        DataRange(body,512);
        // Asset-body transform is identity, origin (0,0,0,1). The following
        // two vectors are measured COM/swept-centre slots, not placements.
        for(int row=0;row<3;++row)for(int column=0;column<4;++column)Need(std::abs(F(body+224+row*16+column*4)-(row==column?1.0:0.0))<=1e-6,"Non-identity/unverified model-body matrix is unsupported");
        for(int k=0;k<4;++k)Need(std::abs(F(body+272+k*4)-(k==3?1.0:0.0))<=1e-6,"Nonzero/unverified model-body origin is unsupported");
        const CollisionVec3 first=V(body+288),second=V(body+304);
        Need(Finite(Mul(first,1.0/.0254),limits.coordinateGameUnits)&&Finite(Mul(second,1.0/.0254),limits.coordinateGameUnits),"Model COM exceeds coordinate budget");
        Need(std::abs(first.x-second.x)<=1e-6&&std::abs(first.y-second.y)<=1e-6&&std::abs(first.z-second.z)<=1e-6&&std::abs(F(body+300))<=1e-6&&std::abs(F(body+316))<=1e-6,"Unverified model swept-centre state is unsupported");
        for(std::size_t offset=320;offset<=336;offset+=16)for(int k=0;k<4;++k)Need(std::abs(F(body+offset+k*4)-(k==3?1.0:0.0))<=1e-6,"Non-identity/unverified model swept rotation is unsupported");
    }
    void TransformBudget(){Need(++transformVisits<=limits.transforms,"Collision transform budget exceeded");}
    void StorePrimitive(ConvexContactHull &hull, ModelPrimitiveKind kind, std::uint32_t child) {
        if (primitiveMode) {
            ModelCollisionPrimitive p;
            p.geometry = hull;
            p.kind = kind;
            p.sourceChildShapeOffset = child;
            modelPrimitives.push_back(p);
        } else
            modelHulls.push_back(hull);
    }
    void BoxHull(std::uint32_t source, std::uint32_t child, CollisionVec3 translation,
                 const CollisionVec3 *axes, std::uint32_t contents, bool known, bool transformed) {
        DataRange(child, 48);
        const CollisionVec3 half = V(child + 32);
        Need(half.x >= 0 && half.y >= 0 && half.z >= 0, "Negative model primitive box half extents");
        Need(modelPrimitives.size() < convexLimits->hulls && convexLimits->verticesPerHull >= 8 &&
                 convexLimits->planesPerHull >= 6,
             "Model box primitive budget exceeded");
        Need(hullVertices <= convexLimits->totalVertices &&
                 8 <= convexLimits->totalVertices - hullVertices &&
                 hullPlanes <= convexLimits->totalPlanes && 6 <= convexLimits->totalPlanes - hullPlanes,
             "Model box aggregate budget exceeded");
        ConvexContactHull hull;
        hull.sourceShapeOffset = source;
        hull.sourceRegion = sourceRegion ^ (std::uint64_t(result.visitedShapes) * 1099511628211ull);
        if (!hull.sourceRegion)
            hull.sourceRegion = 1;
        hull.rawContents = contents;
        hull.contentsKnown = known;
        hull.radius = F(child + 16) / .0254;
        Need(hull.radius >= 0 && hull.radius <= convexLimits->maximumRadiusGameUnits,
             "Model box radius exceeds budget");
        for (int mask = 0; mask < 8; ++mask) {
            CollisionVec3 p = translation;
            for (int k = 0; k < 3; ++k)
                p = Add(p, Mul(axes[k], Axis(half, k) * ((mask & (1 << k)) ? 1 : -1)));
            p = Mul(p, 1.0 / .0254);
            Need(Finite(p, limits.coordinateGameUnits), "Model box vertex exceeds coordinate budget");
            hull.vertices.push_back(p);
        }
        for (int k = 0; k < 3; ++k)
            for (int sign = -1; sign <= 1; sign += 2) {
                ConvexContactPlane plane;
                plane.normal = Mul(axes[k], sign);
                plane.distance = (-plane.normal.x * translation.x - plane.normal.y * translation.y -
                                  plane.normal.z * translation.z - Axis(half, k)) /
                                 .0254;
                Need(std::isfinite(plane.distance) &&
                         std::abs(plane.distance) <= limits.coordinateGameUnits,
                     "Model box plane exceeds coordinate budget");
                for (std::size_t j = 0; j < hull.vertices.size(); ++j) {
                    const CollisionVec3 p = hull.vertices[j];
                    Need(plane.normal.x * p.x + plane.normal.y * p.y + plane.normal.z * p.z +
                                 plane.distance <=
                             .0001,
                         "Model box basis/vertex planes disagree");
                }
                hull.planes.push_back(plane);
            }
        hullVertices += 8;
        hullPlanes += 6;
        StorePrimitive(
            hull, transformed ? ModelPrimitiveKind::TransformedBox : ModelPrimitiveKind::AxisBox, child);
    }
    void TransformedBoxHull(std::uint32_t object, CollisionVec3 parent, std::uint32_t contents,
                            bool known) {
        DataRange(object, 96);
        TransformBudget();
        const std::uint32_t child = Pointer(object + 24);
        if (Type(child) != "hkpBoxShape") {
            Unsupported("convex-transform-child-" + Type(child));
            return;
        }
        Need(++result.visitedShapes <= limits.shapeVisits,
             "Collision transformed box child visit budget exceeded");
        CollisionVec3 axes[3] = {V(object + 32), V(object + 48), V(object + 64)};
        for (int i = 0; i < 3; ++i) {
            Need(std::abs(F(object + 44 + i * 16)) <= 1e-7, "Unverified convex-transform axis padding");
            for (int j = 0; j < 3; ++j) {
                const double dot = axes[i].x * axes[j].x + axes[i].y * axes[j].y + axes[i].z * axes[j].z;
                Need(std::abs(dot - (i == j ? 1 : 0)) <= .00001,
                     "Nonorthonormal/scaled convex transform unsupported");
            }
        }
        const double determinant = axes[0].x * (axes[1].y * axes[2].z - axes[1].z * axes[2].y) -
                                   axes[1].x * (axes[0].y * axes[2].z - axes[0].z * axes[2].y) +
                                   axes[2].x * (axes[0].y * axes[1].z - axes[0].z * axes[1].y);
        Need(std::abs(determinant - 1) <= .00001, "Reflected/improper convex transform unsupported");
        Need(std::abs(F(object + 92)) <= 1e-7, "Unverified convex-transform translation padding");
        Need(std::abs(F(object + 16) - F(child + 16)) <= 1e-7,
             "Convex-transform wrapper/child radius differs");
        const CollisionVec3 t = Add(parent, V(object + 80));
        Need(Finite(Mul(t, 1.0 / .0254), limits.coordinateGameUnits),
             "Model transformed box translation exceeds budget");
        BoxHull(object, child, t, axes, contents, known, true);
    }
    void ConvexHull(std::uint32_t object, CollisionVec3 translation, std::uint32_t contents,
                    bool known) {
        DataRange(object, 96);
        const std::uint32_t blocks = DU(object + 68), count = DU(object + 76), planes = DU(object + 84);
        Need((primitiveMode ? modelPrimitives.size() : modelHulls.size()) < convexLimits->hulls,
             "Model convex hull count limit exceeded");
        Need(count >= 4 && count <= convexLimits->verticesPerHull && blocks > 0 &&
                 blocks <= convexLimits->verticesPerHull / 4 + (convexLimits->verticesPerHull % 4 != 0),
             "Model convex vertex/block limit exceeded");
        const std::uint32_t vertices = Pointer(object + 64), planeArray = Pointer(object + 80);
        ArrayRange(vertices, blocks, 48);
        Need(count / 4 + (count % 4 != 0) <= blocks, "Model convex block/vertex counts contradict");
        Need(planes >= 4 && planes <= convexLimits->planesPerHull,
             "Model convex plane count limit exceeded");
        ArrayRange(planeArray, planes, 16);
        Need(hullVertices <= convexLimits->totalVertices &&
                 count <= convexLimits->totalVertices - hullVertices &&
                 hullPlanes <= convexLimits->totalPlanes &&
                 planes <= convexLimits->totalPlanes - hullPlanes,
             "Model convex aggregate budget exceeded");
        ConvexContactHull hull;
        hull.sourceShapeOffset = object;
        // A shared typed shape can occur through multiple graph placements.
        hull.sourceRegion = sourceRegion ^ (std::uint64_t(result.visitedShapes) * 1099511628211ull);
        if (!hull.sourceRegion)
            hull.sourceRegion = 1;
        hull.rawContents = contents;
        hull.contentsKnown = known;
        hull.radius = F(object + 16) / .0254;
        Need(hull.radius >= 0 && hull.radius <= convexLimits->maximumRadiusGameUnits,
             "Model convex radius exceeds limit");
        const CollisionVec3 t = Mul(translation, 1.0 / .0254);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t block = vertices + std::size_t(i / 4) * 48, lane = (i % 4) * 4;
            const CollisionVec3 v =
                Add(Mul({F(block + lane), F(block + 16 + lane), F(block + 32 + lane)}, 1.0 / .0254), t);
            Need(Finite(v, limits.coordinateGameUnits), "Model convex vertex exceeds coordinate budget");
            hull.vertices.push_back(v);
        }
        for (std::uint32_t i = 0; i < planes; ++i) {
            const std::size_t p = planeArray + std::size_t(i) * 16;
            ConvexContactPlane plane;
            plane.normal = V(p);
            const double n = plane.normal.x * plane.normal.x + plane.normal.y * plane.normal.y +
                             plane.normal.z * plane.normal.z;
            Need(std::abs(n - 1) <= .00002, "Model convex plane normal is not unit length");
            plane.distance =
                F(p + 12) / .0254 - plane.normal.x * t.x - plane.normal.y * t.y - plane.normal.z * t.z;
            Need(std::isfinite(plane.distance) && std::abs(plane.distance) <= limits.coordinateGameUnits,
                 "Model convex plane exceeds coordinate budget");
            for (std::size_t j = 0; j < hull.vertices.size(); ++j) {
                const CollisionVec3 v = hull.vertices[j];
                Need(plane.normal.x * v.x + plane.normal.y * v.y + plane.normal.z * v.z +
                             plane.distance <=
                         .0001,
                     "Model convex vertex contradicts source plane");
            }
            hull.planes.push_back(plane);
        }
        hullVertices += count;
        hullPlanes += planes;
        StorePrimitive(hull, ModelPrimitiveKind::ConvexHull, object);
    }
    void Shape(std::uint32_t object,CollisionVec3 translation,std::uint32_t contents,bool known){
        Need(++result.visitedShapes<=limits.shapeVisits,"Collision shape visit budget exceeded");Need(stack.size()<limits.nesting,"Collision graph nesting limit exceeded");
        Need(std::find(stack.begin(),stack.end(),object)==stack.end(),"Cycle in collision shape graph");stack.push_back(object);
        const std::string type=Type(object);
        if(type=="hkpMoppBvTreeShape")Shape(Pointer(object+52),translation,contents,known);
        else if(type=="hkpListShape"){
            const std::uint32_t count=DU(object+28);Need(count<=limits.compoundEntries,"Collision list entry limit exceeded");
            if(count){const std::uint32_t array=Pointer(object+24);ArrayRange(array,count,16);for(std::uint32_t i=0;i<count;++i){
                const std::size_t child=array+std::size_t(i)*16;const std::uint32_t raw=DU(child+4);
                // Empty's ten entries explicitly store 3 (solid|opaque).
                // Asset-model entries may store zero until entity setup. Do
                // not invent inheritance or treat those as known nonsolid.
                if(!raw){result.contentsComplete=false;Unsupported("unverified-list-child-contents",1,false);}
                Shape(Pointer(child),translation,raw,raw!=0);
            }}
        }else if(type=="hkpConvexTranslateShape"){
            TransformBudget();const CollisionVec3 t=Add(translation,V(object+32));Need(Finite(Mul(t,1.0/.0254),limits.coordinateGameUnits),"Collision translation exceeds coordinate limit");Shape(Pointer(object+24),t,contents,known);
        }else if(type=="hkpConvexVerticesShape"&&convexLimits)ConvexHull(object,translation,contents,known);
        else if (type == "hkpConvexTransformShape" && primitiveMode)
            TransformedBoxHull(object, translation, contents, known);
        else if (type == "hkpBoxShape" && primitiveMode) {
            const CollisionVec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            BoxHull(object, object, translation, axes, contents, known, false);
        } else if (type == "hkpBoxShape" && !convexLimits) {
            const CollisionVec3 half=V(object+32);Need(half.x>=0&&half.y>=0&&half.z>=0,"Negative collision box half-extents");
            if(std::abs(F(object+16))>1e-7)Unsupported("box-with-unverified-convex-radius");
            else{Need(result.boxes.size()<limits.boxes,"Collision box count limit exceeded");StaticCollisionBox box;box.minimum=Mul(Sub(translation,half),1.0/.0254);box.maximum=Mul(Add(translation,half),1.0/.0254);box.sourceShapeOffset=object;
                box.contents=contents;box.contentsKnown=known;
                if(!known){result.contentsComplete=false;result.coverageComplete=false;}
                Need(Finite(box.minimum,limits.coordinateGameUnits)&&Finite(box.maximum,limits.coordinateGameUnits),"Collision bounds exceed coordinate limit");result.boxes.push_back(box);}
        } else if (type == "hkpStorageExtendedMeshShape" && !modelLocalOnly)
            Storage(object, translation);
        else Unsupported(type);
        stack.pop_back();
    }
    bool StorageContents(std::uint32_t object,std::uint32_t parts,std::uint32_t index,std::size_t sub,std::uint32_t children,std::uint32_t childArray,std::vector<std::uint32_t>& flags){
        // Measured 5.6 profile: U8 material index per child, U32 contents table.
        // Verify the separately stored shape array too: material i must follow
        // that exact child instance, not a shared shape's object address.
        const std::map<std::uint32_t,std::uint32_t>::const_iterator a=pointers.find(object+204);
        if(a==pointers.end()||DU(object+208)!=parts)return false;
        ArrayRange(a->second,parts,4);const std::uint32_t storage=Pointer(a->second+std::size_t(index)*4);
        if(Type(storage)!="hkpStorageExtendedMeshShapeShapeSubpartStorage")return false;
        DataRange(storage,56);const std::uint32_t materials=DU(storage+36);
        Need(materials<=limits.compoundEntries,"Collision material table limit exceeded");
        if(DU(sub)!=0x00040101u||materials==0||materials>65535||DU(sub+8)!=(materials<<16|1u)||DU(storage+12)!=children||DU(storage+24)!=children||DU(storage+48)!=0)return false;
        const std::uint32_t shapes=Pointer(storage+8),indices=Pointer(storage+20),table=Pointer(storage+32);
        ArrayRange(shapes,children,4);ArrayRange(indices,children,1);ArrayRange(table,materials,4);flags.reserve(children);
        for(std::uint32_t i=0;i<children;++i){
            Need(Pointer(shapes+std::size_t(i)*4)==Pointer(childArray+std::size_t(i)*4),"Storage contents/shape instance arrays disagree");
            const std::uint32_t material=bytes[sections[2].absolute+std::size_t(indices)+i];Need(material<materials,"Storage collision material index exceeds table");flags.push_back(DU(table+std::size_t(material)*4));
        }return true;
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
            if(children){const std::uint32_t pointersArray=Pointer(sub+16);ArrayRange(pointersArray,children,4);std::vector<std::uint32_t> flags;
                const bool known=StorageContents(object,count,i,sub,children,pointersArray,flags);
                if(!known){result.contentsComplete=false;Unsupported("unverified-storage-contents",1,false);Diagnostic("Unknown storage contents layout: boxes remain conservatively included");}
                for(std::uint32_t j=0;j<children;++j)Shape(Pointer(pointersArray+std::size_t(j)*4),parentTranslation,known?flags[j]:0,known);
            }
        }}
    }
    std::uint32_t SelectBody(const std::string& expected){
        const std::string wanted=Name(expected);std::uint32_t selected=0;std::size_t matches=0,bodies=0;
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
        Need(matches==1,"Expected a unique explicitly named collision rigid body");result.otherRigidBodies=bodies-1;return selected;
    }
    StaticCollisionScene Decode(const std::string& expected){
        const std::uint32_t selected=SelectBody(expected);IdentityBody(selected);result.coverageComplete=true;
        const std::uint32_t contents=DU(selected+44);const bool known=contents!=0;
        if(!known){result.contentsComplete=false;Unsupported("unverified-body-contents",1,false);}
        Shape(Pointer(selected+16),CollisionVec3(),contents,known);
        if(!result.coverageComplete)Diagnostic("Partial geometric coverage: misses do not establish a clear passage");
        Diagnostic("Queries apply observed contents bits; entity/group filters, motion and unselected rigid bodies remain unsupported");return result;
    }
    ModelCollision DecodeModel(const std::string& expected){
        Need(Name(expected).compare(0,7,"models/")==0,"Model collision requires an explicit models/... body name");modelLocalOnly=true;
        const std::uint32_t selected=SelectBody(expected);LocalModelBody(selected);result.coverageComplete=true;
        const std::uint32_t root=Pointer(selected+16),contents=DU(selected+44);const bool known=contents!=0;
        if(!known){result.contentsComplete=false;Unsupported("unverified-body-contents",1,false);}
        Shape(root,CollisionVec3(),contents,known);
        ModelCollision model;model.selectedBodyName=result.selectedBodyName;model.sourceBodyOffset=selected;model.rootShapeOffset=root;model.localBoxes.swap(result.boxes);model.localPoseVerified=true;model.geometryComplete=geometryComplete;model.contentsComplete=result.contentsComplete;model.visitedShapes=result.visitedShapes;model.unsupportedGeometry.swap(unsupportedGeometry);model.diagnostics.swap(result.diagnostics);
        model.diagnostics.push_back("Local model subset only: runtime entity pose, contents qualification and world clearance require explicit caller evidence");
        if(!model.geometryComplete)model.diagnostics.push_back("Unsupported model shapes omitted; no complete wagon/world walking coverage");
        if(!model.contentsComplete)model.diagnostics.push_back("Raw model contents remain unknown; runtime query qualification must not invent contents bits");return model;
    }
    ModelConvexCollision DecodeConvexModel(const std::string &expected,
                                           const ModelConvexCollisionLimits &bounds) {
        Need(Name(expected).compare(0, 7, "models/") == 0,
             "Convex model collision requires an explicit models/... body name");
        modelLocalOnly = true;
        convexLimits = &bounds;
        const std::uint32_t selected = SelectBody(expected);
        LocalModelBody(selected);
        result.coverageComplete = true;
        // Stable provenance guard over the actual bounded file and selected body.
        sourceRegion = 14695981039346656037ull;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            sourceRegion ^= bytes[i];
            sourceRegion *= 1099511628211ull;
        }
        sourceRegion ^= selected;
        sourceRegion *= 1099511628211ull;
        if (!sourceRegion)
            sourceRegion = 1;
        const std::uint32_t root = Pointer(selected + 16), contents = DU(selected + 44);
        const bool known = contents != 0;
        if (!known) {
            result.contentsComplete = false;
            Unsupported("unverified-body-contents", 1, false);
        }
        Shape(root, CollisionVec3(), contents, known);
        ModelConvexCollision model;
        model.selectedBodyName = result.selectedBodyName;
        model.sourceBodyOffset = selected;
        model.rootShapeOffset = root;
        model.localHulls.swap(modelHulls);
        model.localPoseVerified = true;
        model.geometryComplete = geometryComplete;
        model.contentsComplete = result.contentsComplete;
        model.visitedShapes = result.visitedShapes;
        model.unsupportedGeometry.swap(unsupportedGeometry);
        model.diagnostics.swap(result.diagnostics);
        model.diagnostics.push_back("Convex-only local subset: omitted shapes, runtime entity state and "
                                    "scene clearance remain unverified");
        return model;
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
StaticCollisionHit Query(const StaticCollisionScene& scene,CollisionVec3 origin,CollisionVec3 half,CollisionVec3 delta,std::uint32_t mask){
    Need(Finite(origin,1e12)&&Finite(half,1e12)&&Finite(delta,1e12)&&half.x>=0&&half.y>=0&&half.z>=0,"Invalid collision query coordinates/extents");
    StaticCollisionHit best;best.coverageComplete=scene.coverageComplete&&scene.contentsComplete;best.contentsMaskApplied=true;
    for(std::size_t i=0;i<scene.boxes.size();++i){const StaticCollisionBox& original=scene.boxes[i];
        Need(Finite(original.minimum,1e12)&&Finite(original.maximum,1e12)&&original.minimum.x<=original.maximum.x&&original.minimum.y<=original.maximum.y&&original.minimum.z<=original.maximum.z,"Invalid collision scene box");
        if(!original.contentsKnown)best.coverageComplete=false;
        else if(!(original.contents&mask))continue;
        StaticCollisionBox expanded=original;expanded.minimum=Sub(expanded.minimum,half);expanded.maximum=Add(expanded.maximum,half);StaticCollisionHit candidate;
        if(Intersect(expanded,origin,delta,candidate)&&(!best.hit||candidate.fraction<best.fraction)){candidate.boxIndex=i;candidate.contents=original.contents;candidate.hitContentsKnown=original.contentsKnown;candidate.contentsMaskApplied=true;candidate.coverageComplete=best.coverageComplete;best=candidate;}
    }return best;
}
}
StaticCollisionScene DecodeStaticCollision(const std::vector<unsigned char>& bytes,const std::string& bodyName,const StaticCollisionLimits& limits){Need(std::isfinite(limits.coordinateGameUnits)&&limits.coordinateGameUnits>0,"Invalid collision coordinate budget");Reader reader(bytes,limits);return reader.Decode(bodyName);}
ModelCollision DecodeModelCollision(const std::vector<unsigned char>& bytes,const std::string& bodyName,const StaticCollisionLimits& limits){Need(std::isfinite(limits.coordinateGameUnits)&&limits.coordinateGameUnits>0,"Invalid model collision coordinate budget");Reader reader(bytes,limits);return reader.DecodeModel(bodyName);}
ModelConvexCollision DecodeModelConvexCollision(const std::vector<unsigned char> &bytes,
                                                const std::string &bodyName,
                                                const ModelConvexCollisionLimits &limits) {
    Need(std::isfinite(limits.graph.coordinateGameUnits) && limits.graph.coordinateGameUnits > 0 &&
             std::isfinite(limits.maximumRadiusGameUnits) && limits.maximumRadiusGameUnits >= 0,
         "Invalid model convex limits");
    Reader reader(bytes, limits.graph);
    return reader.DecodeConvexModel(bodyName, limits);
}
ModelPrimitiveCollision DecodeModelPrimitiveCollision(const std::vector<unsigned char> &bytes,
                                                      const std::string &name,
                                                      const ModelConvexCollisionLimits &limits) {
    Need(std::isfinite(limits.graph.coordinateGameUnits) && limits.graph.coordinateGameUnits > 0 &&
             std::isfinite(limits.maximumRadiusGameUnits) && limits.maximumRadiusGameUnits >= 0,
         "Invalid model primitive limits");
    Reader reader(bytes, limits.graph);
    reader.primitiveMode = true;
    ModelConvexCollision common = reader.DecodeConvexModel(name, limits);
    ModelPrimitiveCollision model;
    model.selectedBodyName = common.selectedBodyName;
    model.sourceBodyOffset = common.sourceBodyOffset;
    model.rootShapeOffset = common.rootShapeOffset;
    model.localPrimitives.swap(reader.modelPrimitives);
    model.localPoseVerified = common.localPoseVerified;
    model.geometryComplete = common.geometryComplete;
    model.contentsComplete = common.contentsComplete;
    model.visitedShapes = common.visitedShapes;
    model.unsupportedGeometry.swap(common.unsupportedGeometry);
    model.diagnostics.swap(common.diagnostics);
    model.diagnostics.push_back("Primitive geometry only: coincident faces do not identify a unique "
                                "original contact shape or establish runtime contents");
    return model;
}
StaticCollisionHit RaycastStatic(const StaticCollisionScene& scene,CollisionVec3 origin,CollisionVec3 delta,std::uint32_t mask){return Query(scene,origin,CollisionVec3(),delta,mask);}
StaticCollisionHit SweepStaticBox(const StaticCollisionScene& scene,CollisionVec3 center,CollisionVec3 half,CollisionVec3 delta,std::uint32_t mask){return Query(scene,center,half,delta,mask);}
}

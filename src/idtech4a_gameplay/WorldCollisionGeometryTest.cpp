// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldCollisionGeometry.h"
#include "MapPhysics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace wolf2009;
using Bytes=std::vector<unsigned char>;
void Check(bool b,const char *s) {if(!b)throw std::runtime_error(s);}
void U(Bytes &b,std::size_t at,std::uint32_t n) {
    Check(at+4<=b.size(),"Fixture writer range");
    for(int k=0;k<4;++k)b[at+k]=static_cast<unsigned char>(n>>(8*k));
}
void F(Bytes &b,std::size_t at,float f) {std::uint32_t u;std::memcpy(&u,&f,4);U(b,at,u);}
void Text(Bytes &b,std::size_t at,const std::string &s) {
    Check(at+s.size()<b.size(),"Fixture text range");std::copy(s.begin(),s.end(),b.begin()+at);b[at+s.size()]=0;
}
std::size_t Align(std::size_t n) {return (n+15)/16*16;}
template<class Fn> void Reject(Fn f) {bool caught=false;try{f();}catch(const std::exception &){caught=true;}Check(caught,"Malformed world geometry accepted");}
bool Near(double a,double b,double e=.00002) {return std::abs(a-b)<=e;}
struct Fixture {
    Bytes data=Bytes(8192,0);
    std::map<std::uint32_t,std::uint32_t> links;
    std::map<std::uint32_t,std::string> types;
    Fixture() {
        types[0]="hkRootLevelContainer";types[64]="hkpRigidBody";
        types[576]="hkpStorageExtendedMeshShape";
        types[1408]="hkpConvexTransformShape";types[2048]="hkpBoxShape";
        types[2176]="hkpConvexTranslateShape";types[2432]="hkpConvexVerticesShape";
        types[4096]="hkpStorageExtendedMeshShapeShapeSubpartStorage";
        types[4160]="hkpStorageExtendedMeshShapeShapeSubpartStorage";
        Text(data,800,"maps/synthetic/world");links[64+112]=800;links[64+16]=576;
        for(int i=0;i<3;++i)F(data,64+224+i*20,1);
        F(data,64+332,1);F(data,64+348,1);U(data,64+44,3);
        for(int k=0;k<3;++k)F(data,576+96+k*4,1);
        links[576+160]=1024;U(data,576+164,2);
        links[576+204]=4000;U(data,576+208,2);links[4000]=4096;links[4004]=4160;
        const float q=std::sqrt(.5f);
        F(data,1024+40,q);F(data,1024+44,q);
        F(data,1024+48,.254f);F(data,1024+52,.508f);F(data,1024+56,.762f);
        F(data,1088+44,1);F(data,1088+48,-.254f);
        links[1300]=1408;links[1304]=2176;links[1320]=2048;
        Materials(1024,4096,1300,2,4300,4320,2);
        Materials(1088,4160,1320,1,4340,4360,1);
        data[4300]=0;data[4301]=1;U(data,4320,3);U(data,4324,8);U(data,4360,16);
        links[1408+24]=2048;
        F(data,1408+36,1);F(data,1408+48,-1);F(data,1408+72,1);
        F(data,1408+80,.0254f);F(data,1408+84,.0508f);F(data,1408+88,.0762f);
        F(data,1408+16,.00127f);F(data,2048+16,.00127f);
        F(data,2048+32,.0508f);F(data,2048+36,.0762f);F(data,2048+40,.1016f);
        links[2176+24]=2432;F(data,2176+40,.254f);F(data,2176+16,.00254f);
        F(data,2432+16,.00254f);links[2432+64]=2800;U(data,2432+68,2);U(data,2432+76,8);
        links[2432+80]=3000;U(data,2432+84,6);
        for(int i=0;i<8;++i)for(int k=0;k<3;++k)
            F(data,2800+(i/4)*48+k*16+(i%4)*4,(i&(1<<k))?.0254f:-.0254f);
        for(int k=0;k<3;++k)for(int sign=0;sign<2;++sign) {
            F(data,3000+(k*2+sign)*16+k*4,sign?1.f:-1.f);
            F(data,3000+(k*2+sign)*16+12,-.0254f);
        }
    }
    void Materials(std::uint32_t sub,std::uint32_t storage,std::uint32_t children,
                   std::uint32_t count,std::uint32_t indices,std::uint32_t table,std::uint32_t materials) {
        U(data,sub,0x00040101u);U(data,sub+8,(materials<<16)|1u);links[sub+16]=children;U(data,sub+20,count);
        links[storage+8]=children;U(data,storage+12,count);links[storage+20]=indices;U(data,storage+24,count);
        links[storage+32]=table;U(data,storage+36,materials);
    }
    Bytes File() const {
        Bytes classes(16,0);std::map<std::string,std::uint32_t> names;
        for(const auto &t:types)if(!names.count(t.second)) {
            const auto at=classes.size();names[t.second]=static_cast<std::uint32_t>(at);
            classes.resize(at+t.second.size()+1);Text(classes,at,t.second);
        }
        classes.resize(Align(classes.size()),255);
        const auto absolute=208+classes.size(),local=data.size(),global=Align(local+links.size()*8),
                   end=Align(global+types.size()*12);
        Bytes out(absolute+end,255);std::fill(out.begin(),out.begin()+208,0);
        U(out,0,0x57e0e057);U(out,4,0x10c0c010);U(out,12,5);out[16]=4;out[17]=1;out[19]=1;
        U(out,20,3);U(out,24,2);U(out,36,names["hkRootLevelContainer"]);Text(out,40,"Havok-5.6.0-r1");
        const char *tags[]={"__classnames__","__types__","__data__"};
        for(int i=0;i<3;++i) {
            const auto h=64+i*48;Text(out,h,tags[i]);U(out,h+20,static_cast<std::uint32_t>(i==0?208:absolute));
            for(int k=0;k<6;++k)U(out,h+24+k*4,static_cast<std::uint32_t>(i==0?classes.size():
                i==1?0:k==0?local:k<=2?global:end));
        }
        std::copy(classes.begin(),classes.end(),out.begin()+208);std::copy(data.begin(),data.end(),out.begin()+absolute);
        auto at=absolute+local;
        for(const auto &link:links){U(out,at,link.first);U(out,at+4,link.second);at+=8;}
        at=absolute+global;
        for(const auto &t:types){U(out,at,t.first);U(out,at+4,0);U(out,at+8,names[t.second]);at+=12;}
        return out;
    }
};
WorldCollisionGeometry Decode(const Fixture &f,const WorldCollisionGeometryLimits &l=WorldCollisionGeometryLimits()) {
    return DecodeWorldCollisionGeometry(f.File(),"maps/synthetic/world",l);
}
void Synthetic() {
    Fixture f;const auto scene=Decode(f);
    Check(scene.coverageComplete&&scene.identityBodyPoseVerified&&scene.primitives.size()==3&&scene.omissions.empty(),
          "World hierarchy coverage incorrect");
    const auto &box=scene.primitives[0],&hull=scene.primitives[1],&shared=scene.primitives[2];
    Check(box.kind==WorldCollisionPrimitiveKind::Box&&box.transforms.size()==2&&Near(box.local.radius,.05)&&
          Near(box.world.radius,.05)&&box.radiusHierarchyVerified,"Rounded transformed box lost data");
    Check(Near(box.world.vertices[0].x,10)&&Near(box.world.vertices[0].y,24)&&Near(box.world.vertices[0].z,29),
          "Nested quaternion/matrix translation composition incorrect");
    Check(Near(hull.world.vertices[0].x,11)&&Near(hull.world.vertices[0].y,19)&&Near(hull.world.vertices[0].z,39)&&
          Near(hull.local.radius,.1)&&hull.local.rawContents==8,"Translated/rotated hull geometry or contents incorrect");
    Check(shared.local.sourceShapeOffset==box.local.sourceShapeOffset&&shared.occurrence!=box.occurrence&&
          shared.world.sourceRegion!=box.world.sourceRegion&&shared.local.rawContents==16&&
          Near(shared.world.vertices[0].x,-12),"Shared shape occurrences collapsed");
    Check(box.supportArrayComplete&&hull.supportArrayComplete&&box.additionalSupportVertices.empty()&&
          hull.additionalSupportVertices.empty(),"Complete box/hull support arrays not retained");
    auto lanes=f;U(lanes.data,2432+76,5);
    const auto padded=Decode(lanes);
    Check(padded.primitives[1].local.vertices.size()==5&&
          padded.primitives[1].additionalSupportVertices.size()==3&&
          padded.primitives[1].supportArrayComplete&&
          Near(padded.primitives[1].additionalSupportVertices[2].x,1),
          "Partial-block support lanes lost or logical vertices changed");
    lanes=f;U(lanes.data,2432+68,3);
    F(lanes.data,2896,.254f);
    const auto extraBlock=Decode(lanes);
    Check(extraBlock.primitives[1].local.vertices.size()==8&&
          extraBlock.primitives[1].additionalSupportVertices.size()==4&&
          Near(extraBlock.primitives[1].additionalSupportVertices[0].x,10)&&
          extraBlock.primitives[1].sourcePlanesConsistent,
          "Extra full support block lost, truncated, or substituted into raw ray-plane validation");
    F(lanes.data,2896,std::numeric_limits<float>::quiet_NaN());Reject([&]{Decode(lanes);});
    F(lanes.data,2896,1000000);Reject([&]{Decode(lanes);});
    for(const auto &p:scene.primitives)for(const auto &plane:p.world.planes)for(const auto &v:p.world.vertices)
        Check(plane.normal.x*v.x+plane.normal.y*v.y+plane.normal.z*v.z+plane.distance<.0001,
              "Transformed plane does not contain original transformed vertices");
    Check(Decode(f).primitives[0].world.sourceRegion==box.world.sourceRegion,"Source occurrence is unstable");
    auto roundoff=f;F(roundoff.data,3000+12,std::nextafter(-.0254f,0.f));
    const auto rounded=Decode(roundoff);
    Check(rounded.coverageComplete&&rounded.primitives[1].maximumPlaneVertexResidual>0&&
          rounded.primitives[1].maximumPlaneVertexResidual<=rounded.primitives[1].float32ErrorBoundAtMaximumResidual&&
          rounded.primitives[1].maximumPlaneVertexBoundExcess==0,
          "One-ULP source roundoff treated as unknown geometry");
    auto changed=f;changed.data[7000]=1;
    Check(Decode(changed).primitives[0].world.sourceRegion!=box.world.sourceRegion,"Changed bytes reused source fingerprint");
    auto partial=f;partial.links[576+148]=1200;U(partial.data,576+152,1);U(partial.data,1216,7);
    const auto omitted=Decode(partial);
    Check(!omitted.geometryComplete&&!omitted.coverageComplete&&omitted.primitives.size()==3&&
          omitted.omittedTriangles==7&&omitted.omissions[0].sourceSubpartIndex==0,"Triangle omission silently discarded");
    partial=f;U(partial.data,1024,0);const auto unknown=Decode(partial);
    Check(unknown.geometryComplete&&!unknown.contentsComplete&&!unknown.coverageComplete&&
          !unknown.primitives[0].world.contentsKnown,"Unknown storage contents inferred");
    partial=f;F(partial.data,1408+16,.00508f);const auto radius=Decode(partial);
    Check(!radius.geometryComplete&&!radius.primitives[0].radiusHierarchyVerified&&
          Near(radius.primitives[0].world.radius,.05)&&Near(radius.primitives[0].transforms[1].radius,.2),
          "Wrapper radius mismatch was silently dropped/applied twice");
    partial=f;F(partial.data,576+96,2);const auto scaled=Decode(partial);
    Check(!scaled.geometryComplete&&scaled.primitives.empty()&&scaled.unsupported.count("scaled-storage-mesh"),
          "Unsupported scaling treated as exact rigid placement");
    partial=f;U(partial.data,2432+76,3);U(partial.data,2432+84,3);
    const auto lowDimension=Decode(partial);
    Check(!lowDimension.coverageComplete&&!lowDimension.primitives[1].volumeProfileVerified&&
          lowDimension.primitives[1].local.vertices.size()==3&&lowDimension.primitives[1].local.planes.size()==3,
          "Lower-dimensional raw hull discarded or qualified as a volume");
    auto bad=f;F(bad.data,1024+44,2);Reject([&]{Decode(bad);});
    bad=f;F(bad.data,1408+36,2);Reject([&]{Decode(bad);});
    bad=f;F(bad.data,1408+72,-1);Reject([&]{Decode(bad);});
    bad=f;F(bad.data,2048+32,-1);Reject([&]{Decode(bad);});
    bad=f;F(bad.data,3000+12,100);const auto contradictory=Decode(bad);
    Check(!contradictory.coverageComplete&&!contradictory.primitives[1].sourcePlanesConsistent&&
          contradictory.primitives[1].local.planes[0].distance>3900,
          "Contradictory raw planes silently repaired/qualified");
    bad=f;U(bad.data,2432+68,1);Reject([&]{Decode(bad);});
    bad=f;bad.links.erase(2432+64);Reject([&]{Decode(bad);});
    bad=f;bad.links[2176+24]=2176;Reject([&]{Decode(bad);});
    bad=f;F(bad.data,64+272,1);Reject([&]{Decode(bad);});
    bad=f;bad.data[4301]=2;Reject([&]{Decode(bad);});
    bad=f;F(bad.data,2048+16,std::numeric_limits<float>::quiet_NaN());Reject([&]{Decode(bad);});
    WorldCollisionGeometryLimits limits;limits.primitives=2;Reject([&]{Decode(f,limits);});
    limits=WorldCollisionGeometryLimits();limits.totalVertices=8;Reject([&]{Decode(f,limits);});
    limits=WorldCollisionGeometryLimits();limits.graph.transforms=2;Reject([&]{Decode(f,limits);});
    limits=WorldCollisionGeometryLimits();limits.planeVertexChecks=1;Reject([&]{Decode(f,limits);});
    limits=WorldCollisionGeometryLimits();limits.totalSupportVertices=23;Reject([&]{Decode(f,limits);});
    std::cout << "WOLF2009_WORLD_COLLISION_GEOMETRY_SYNTHETIC_OK\n";
}
struct Bounds {
    CollisionVec3 lo={1e30,1e30,1e30},hi={-1e30,-1e30,-1e30};
    void Add(CollisionVec3 p,double r=0) {
        lo.x=std::min(lo.x,p.x-r);lo.y=std::min(lo.y,p.y-r);lo.z=std::min(lo.z,p.z-r);
        hi.x=std::max(hi.x,p.x+r);hi.y=std::max(hi.y,p.y+r);hi.z=std::max(hi.z,p.z+r);
    }
};
void Original(const char *path,bool allowMissingLegacy=false) {
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Cannot open supplied world package");
    const auto archive=Archive::Scan(input);std::string name;
    for(const auto &chunk:archive.Chunks())if(chunk.type=="PROC")for(const auto &entry:chunk.entries) {
        Check(name.empty(),"Ambiguous world resource");name=entry.name;
    }
    const auto bytes=ReadMapResource(input,archive,name,"HKXS",64u<<20);
    const auto scene=DecodeWorldCollisionGeometry(bytes,name);
    StaticCollisionScene legacy;
    bool legacyAvailable=true;
    try {legacy=DecodeStaticCollision(bytes,name);}
    catch(const std::exception &error) {
        if(!allowMissingLegacy)throw;
        legacyAvailable=false;
        std::cout << "LegacySubsetUnavailable map=" << name << " reason=" << error.what() << '\n';
    }
    std::size_t boxes=0,hulls=0,rounded=0,transformed=0,unverifiedPlanes=0,unverifiedRadii=0,lowDimension=0;
    double maximumResidual=0,maximumResidualBound=0,maximumBoundExcess=0;
    Bounds vertices,roundedEnclosure;
    std::map<std::uint32_t,std::vector<const WorldCollisionPrimitive *>> bySource;
    for(const auto &p:scene.primitives) {
        boxes+=p.kind==WorldCollisionPrimitiveKind::Box;hulls+=p.kind==WorldCollisionPrimitiveKind::ConvexVertices;
        rounded+=p.world.radius>0;transformed+=!p.transforms.empty();
        unverifiedPlanes+=!p.sourcePlanesConsistent;unverifiedRadii+=!p.radiusHierarchyVerified;
        lowDimension+=!p.volumeProfileVerified;
        if(p.maximumPlaneVertexResidual>maximumResidual) {
            maximumResidual=p.maximumPlaneVertexResidual;
            maximumResidualBound=p.float32ErrorBoundAtMaximumResidual;
        }
        maximumBoundExcess=std::max(maximumBoundExcess,p.maximumPlaneVertexBoundExcess);
        for(const auto &v:p.world.vertices){vertices.Add(v);roundedEnclosure.Add(v,p.world.radius);}
        bySource[p.world.sourceShapeOffset].push_back(&p);
    }
    // Retain every already decoded exact axis-box occurrence. This checks the
    // generalized graph against the independently tested legacy subset.
    for(const auto &old:legacy.boxes) {
        bool found=false;
        for(const auto *candidate:bySource[old.sourceShapeOffset]) {
            if(candidate->world.rawContents!=old.contents||candidate->world.radius!=0)continue;
            Bounds b;for(const auto &v:candidate->world.vertices)b.Add(v);
            if(Near(b.lo.x,old.minimum.x,.001)&&Near(b.lo.y,old.minimum.y,.001)&&Near(b.lo.z,old.minimum.z,.001)&&
               Near(b.hi.x,old.maximum.x,.001)&&Near(b.hi.y,old.maximum.y,.001)&&Near(b.hi.z,old.maximum.z,.001)) {
                found=true;break;
            }
        }
        Check(found,"General world reader lost an exact legacy box occurrence");
    }
    std::cout << std::setprecision(10) << "WOLF2009_WORLD_COLLISION_GEOMETRY_REAL_OK map=" << name
        << " primitives=" << scene.primitives.size() << " boxes=" << boxes << " hulls=" << hulls
        << " rounded=" << rounded << " transformed=" << transformed << " omissions=" << scene.omissions.size()
        << " omittedTriangles=" << scene.omittedTriangles << " legacyAvailable=" << legacyAvailable
        << " legacyBoxesPreserved=" << legacy.boxes.size()
        << " geometryComplete=" << scene.geometryComplete << " contentsComplete=" << scene.contentsComplete
        << " coverageComplete=" << scene.coverageComplete << " unverifiedPlanes=" << unverifiedPlanes
        << " unverifiedRadii=" << unverifiedRadii << " lowDimension=" << lowDimension << '\n'
        << "PlaneFloat32Residual maximum=" << maximumResidual << " boundAtMaximum=" << maximumResidualBound
        << " maximumBoundExcess=" << maximumBoundExcess << '\n'
        << "CoreVertexBounds " << vertices.lo.x << ' ' << vertices.lo.y << ' ' << vertices.lo.z << " / "
        << vertices.hi.x << ' ' << vertices.hi.y << ' ' << vertices.hi.z << '\n'
        << "RoundedEnclosureBounds " << roundedEnclosure.lo.x << ' ' << roundedEnclosure.lo.y << ' ' << roundedEnclosure.lo.z
        << " / " << roundedEnclosure.hi.x << ' ' << roundedEnclosure.hi.y << ' ' << roundedEnclosure.hi.z << '\n';
    for(const auto &u:scene.unsupported)std::cout << "Omission " << u.first << ' ' << u.second << '\n';
}
}
int main(int argc,char **argv) {
    try {
        Synthetic();const bool survey=argc>1&&std::string(argv[1])=="--survey";
        int failures=0;
        for(int i=survey?2:1;i<argc;++i) {
            try {Original(argv[i],survey);}
            catch(const std::exception &error) {
                if(!survey)throw;
                ++failures;std::cout << "WOLF2009_WORLD_COLLISION_GEOMETRY_REAL_ERROR argument=" << i-1
                    << " reason=" << error.what() << '\n';
            }
            if(survey)std::cerr << "World geometry survey " << i-1 << '/' << argc-2 << '\n';
        }
        return failures?1:0;
    }
    catch(const std::exception &e){std::cerr << "World geometry test: " << e.what() << '\n';return 1;}
}

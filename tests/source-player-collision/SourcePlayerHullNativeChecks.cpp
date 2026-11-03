// SPDX-License-Identifier: GPL-3.0-or-later
// QA runs inside the normal GPL game module after map/CM initialization. All
// fixture geometry is independently authored; source asset hulls are exercised
// separately by the production source-player spawn/physics checks.
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineSourcePlayerHull.h"
#include "cm/CollisionModel.h"
#include "cm/CollisionModel_traceBits.h"
#include <cmath>
#include <climits>
#include <cstdio>
#include <stdexcept>

namespace {
int checks=0;
void Need(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
bool Near(float a,float b,float error=.001f){return std::fabs(a-b)<=error;}
void NativeTrace(const char* label,cmHandle_t model,const idTraceModel* hull,
    const idVec3& start,const idVec3& end,float expected) {
    trace_t t;
    collisionModelManager->Translation(&t,start,end,hull,mat3_default,1,model,vec3_origin,mat3_default);
    Need(std::isfinite(t.fraction)&&Near(t.fraction,expected),label);
    Need(t.endpos.Compare(start+(end-start)*t.fraction,.003f),"Wide hull trace end position");
    if(t.fraction<1){Need(t.c.material!=NULL,"Wide hull native trace material");Need(Near(t.c.normal.LengthSqr(),1,.001f),"Wide hull native trace unit normal");}
    common->Printf("WOLF2009_PLAYER_HULL_TRACE case=%s fraction=%.7f expected=%.7f\n",label,t.fraction,expected);
}
void Run() {
    Need(MAX_TRACEMODEL_VERTS==64&&MAX_TRACEMODEL_EDGES==160&&MAX_TRACEMODEL_POLYS==96,"Shared wide trace-model ABI limits");
    Need(collisionModelManager!=NULL,"Native collision manager absent");
    idCMTraceBits<161> cache;cache.Clear();
    const int bits[]={31,32,63,64,95,96,127,128,143,144,159,160};
    for(unsigned i=0;i<sizeof(bits)/sizeof(bits[0]);++i){cache.Set(bits[i]);Need(cache.Get(bits[i])==1,"Native CM cache boundary");}
    for(unsigned i=0;i<sizeof(bits)/sizeof(bits[0]);++i){cache.Set(bits[i],0);Need(cache.Get(bits[i])==0,"Native CM cache clear boundary");}
    float xyz[150];
    for(int z=0;z<2;++z)for(int i=0;i<24;++i){const int at=3*(z*24+i);const double a=i*6.2831853071795864769/24;
        xyz[at]=float(13*std::cos(a));xyz[at+1]=float(13*std::sin(a));xyz[at+2]=z?57.0f:13.0f;}
    xyz[144]=xyz[145]=xyz[146]=xyz[147]=xyz[148]=0;xyz[149]=70;
    idTraceModel hull;idStr reason;
    Need(Wolf2009_BuildSourcePlayerTraceModel(xyz,50,hull,reason),reason.c_str());
    Need(hull.type==TRM_CUSTOM&&hull.numVerts==50&&hull.numEdges==144&&hull.numPolys==96&&hull.isConvex,"Actual native wide hull geometry");
    for(int i=0;i<50;++i)for(int j=0;j<3;++j)Need(hull.verts[i][j]==xyz[3*i+j],"Native supports remain exact");
    idTraceModel copy=hull;Need(copy.Compare(hull),"Native custom hull equality");
    copy.verts[63].Set(999,999,999);copy.edges[160].normal.Set(1,2,3);
    Need(copy.Compare(hull),"Unused native trace storage is excluded from cache identity");
    copy=hull;copy.edges[1].v[0]=copy.edges[1].v[1];Need(!copy.Compare(hull),"Custom hull edge topology is part of cache identity");
    copy=hull;copy.polys[95].dist+=.1f;Need(!copy.Compare(hull),"Custom hull high plane is part of cache identity");
    copy=hull;copy.polys[95].edges[0]*=-1;Need(!copy.Compare(hull),"Custom hull oriented face is part of cache identity");
    copy=hull;copy.isConvex=false;Need(!copy.Compare(hull),"Custom hull convexity is part of cache identity");
    copy=hull;Need(!Wolf2009_BuildSourcePlayerTraceModel(xyz,65,copy,reason)&&copy.Compare(hull),"Native hull failure is transactional");
    idTraceModel cylinder,cone,polygon;
    const idBounds smallBounds(idVec3(-1,-1,-1),idVec3(1,1,1));
    cylinder.SetupCylinder(smallBounds,INT_MAX);cone.SetupCone(smallBounds,INT_MAX);
    Need(cylinder.numVerts==32&&cylinder.numEdges==48&&cylinder.polys[16].numEdges==16,"Cylinder ring respects polygon edge capacity");
    Need(cone.numVerts==17&&cone.numEdges==32&&cone.polys[16].numEdges==16,"Cone ring respects polygon edge capacity");
    idVec3 ring[65];for(int i=0;i<65;++i){const double a=i*6.2831853071795864769/65;ring[i].Set(float(std::cos(a)),float(std::sin(a)),0);}
    polygon.SetupPolygon(ring,65);Need(polygon.numVerts==16&&polygon.polys[0].numEdges==16,"Polygon ring respects fixed face storage");
    const idTraceModel floor(idBounds(idVec3(-256,-256,-16),idVec3(256,256,0)));
    cmHandle_t model=collisionModelManager->SetupTrmModel(floor,NULL);
    NativeTrace("wide-to-floor",model,&hull,idVec3(0,0,100),idVec3(0,0,-100),(100-CM_CLIP_EPSILON)/200);
    NativeTrace("wide-floor-clear",model,&hull,idVec3(0,0,2),idVec3(32,0,2),1);
    Need((collisionModelManager->Contents(idVec3(0,0,-1),&hull,mat3_default,1,model,vec3_origin,mat3_default)&1)!=0,"Native wide hull penetration");
    Need(collisionModelManager->Contents(idVec3(0,0,2),&hull,mat3_default,1,model,vec3_origin,mat3_default)==0,"Native wide hull clearance");
    contactInfo_t contacts[16];idVec6 direction;direction.Zero();direction[2]=-1;
    const int contactCount=collisionModelManager->Contacts(contacts,16,idVec3(0,0,CM_CLIP_EPSILON),direction,1,&hull,mat3_default,1,model,vec3_origin,mat3_default);
    Need(contactCount>0&&contactCount<=16,"Native wide hull floor contacts");
    for(int i=0;i<contactCount;++i)Need(contacts[i].normal.Compare(idVec3(0,0,1),.001f),"Native wide hull contact orientation");
    model=collisionModelManager->SetupTrmModel(hull,NULL);
    const idTraceModel box(smallBounds);
    NativeTrace("point-to-wide",model,NULL,idVec3(0,0,100),idVec3(0,0,50),(30-CM_CLIP_EPSILON)/50);
    NativeTrace("box-to-wide",model,&box,idVec3(40,0,35),idVec3(0,0,35),(26-CM_CLIP_EPSILON)/40);
    NativeTrace("wide-to-wide",model,&hull,idVec3(50,0,0),idVec3(0,0,0),(24-CM_CLIP_EPSILON)/50);
    Need((collisionModelManager->Contents(idVec3(0,0,35),&box,mat3_default,1,model,vec3_origin,mat3_default)&1)!=0,"Legacy box inside wide model");
    Need(collisionModelManager->Contents(idVec3(40,0,35),&box,mat3_default,1,model,vec3_origin,mat3_default)==0,"Legacy box outside wide model");
    trace_t rotation;const idRotation turn(idVec3(20,0,35),idVec3(0,0,1),90);
    collisionModelManager->Rotation(&rotation,idVec3(20,20,35),turn,&box,mat3_default,1,model,vec3_origin,mat3_default);
    Need(std::isfinite(rotation.fraction)&&rotation.fraction>0&&rotation.fraction<1,"Native rotation contacts wide hull");
    common->Printf("WOLF2009_PLAYER_HULL_NATIVE PASS checks=%d vertices=%d edges=%d faces=%d contacts=%d rotation=%.7f\n",
        checks,hull.numVerts,hull.numEdges,hull.numPolys,contactCount,rotation.fraction);
}
}
extern "C" __attribute__((visibility("default"))) int Wolf2009_RunSourcePlayerHullNativeChecks(void) {
    try {checks=0;Run();return 1;}
    catch(const std::exception& e){common->Warning("WOLF2009_PLAYER_HULL_NATIVE FAIL checks=%d reason=%s",checks,e.what());return 0;}
}

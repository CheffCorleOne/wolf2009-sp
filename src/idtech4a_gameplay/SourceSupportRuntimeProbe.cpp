// SPDX-License-Identifier: GPL-3.0-or-later
#include "idlib/precompiled.h"
#include "SourceSupportRuntimeProbe.h"
#include "EngineRoundedCollision.h"
#include "framework/Wolf2009Streams.h"
#include "cm/CollisionModel.h"
#include "renderer/Material.h"
#include <stdio.h>

namespace {
bool attempted=false,passed=false;
bool Near(float a,float b,float tolerance=.002f) {return idMath::Fabs(a-b)<=tolerance;}
bool Finite(float value) {return value==value&&value>=-10000000.0f&&value<=10000000.0f;}
bool Finite(const idVec3& value) {return Finite(value.x)&&Finite(value.y)&&Finite(value.z);}
bool Bounds(const idBounds& bounds) {
    return Finite(bounds[0])&&Finite(bounds[1])&&bounds[0].x<bounds[1].x&&
        bounds[0].y<bounds[1].y&&bounds[0].z<bounds[1].z;
}
bool Metadata(idBounds& sidecar) {
    int contents=-1,nativeContents=-1;idBounds native;
    sidecar.Clear();native.Clear();
    const WolfCMStatus status=Wolf2009_CMMetadata(0,sidecar,contents);
    const bool publicBounds=collisionModelManager->GetModelBounds(0,native);
    const bool publicContents=collisionModelManager->GetModelContents(0,nativeContents);
    const bool valid=status==WolfCM_Complete&&contents==3&&publicBounds&&publicContents&&nativeContents==3&&
        Bounds(sidecar)&&native.Compare(sidecar,.0001f)&&
        sidecar[0].Compare(idVec3(-192,-192,-8),.01f)&&sidecar[1].Compare(idVec3(192,192,320),.01f);
    common->Printf("WOLF2009_SOURCE_SUPPORT_METADATA status=%d contents=%d public_contents=%d "
        "bounds=(%.5f,%.5f,%.5f)-(%.5f,%.5f,%.5f) valid=%d\n",int(status),contents,nativeContents,
        sidecar[0].x,sidecar[0].y,sidecar[0].z,sidecar[1].x,sidecar[1].y,sidecar[1].z,valid);
    return valid;
}
bool Contact(const contactInfo_t& contact) {
    return contact.type==CONTACT_WOLF2009_SUPPORT&&contact.contents==3&&contact.material&&
        !idStr::Cmp(contact.material->GetName(),"wolf2009/collision/solid_opaque")&&
        contact.material->GetContentFlags()==3&&contact.modelFeature>0&&contact.trmFeature==-1&&
        Finite(contact.point)&&Finite(contact.normal)&&Near(contact.normal.LengthSqr(),1,.001f)&&
        Near(contact.dist,contact.point*contact.normal,.01f);
}
bool Trace(const char* label,const idVec3& start,const idVec3& end,const idTraceModel* actor,
           int mask,float expected,const idVec3& normal,int* feature=NULL) {
    trace_t trace;
    collisionModelManager->Translation(&trace,start,end,actor,mat3_default,mask,0,vec3_origin,mat3_default);
    const bool hit=expected<1,actualHit=Finite(trace.fraction)&&trace.fraction>=0&&trace.fraction<1;
    const bool valid=Finite(trace.fraction)&&Near(trace.fraction,expected,.00005f)&&
        Finite(trace.endpos)&&trace.endpos.Compare(start+(end-start)*expected,.015f)&&
        trace.endAxis.Compare(mat3_default,.0001f)&&
        (hit?(actualHit&&Contact(trace.c)&&trace.c.normal.Compare(normal,.003f)):trace.fraction==1);
    if(feature)*feature=actualHit?trace.c.modelFeature:0;
    const idVec3 n=actualHit?trace.c.normal:vec3_origin;
    const int contents=actualHit?trace.c.contents:0;
    common->Printf("WOLF2009_SOURCE_SUPPORT_TRACE case=%s actor=%s mask=%d fraction=%.8f expected=%.8f "
        "end=(%.5f,%.5f,%.5f) normal=(%.5f,%.5f,%.5f) contents=%d feature=%d valid=%d\n",
        label,actor?"actual_trm":"declared_geometry_point",mask,trace.fraction,expected,
        trace.endpos.x,trace.endpos.y,trace.endpos.z,n.x,n.y,n.z,contents,actualHit?trace.c.modelFeature:0,valid);
    return valid;
}
bool MapAndCache(const char* mapName,int originalFeature,const idBounds& originalBounds,
                 const idVec3& wallStart,const idVec3& wallEnd,float wallFraction) {
    idMapFile sameMapFile;
    idStr path(mapName);path+=".map";
    if(!sameMapFile.Parse(path,true)) {
        common->Printf("WOLF2009_SOURCE_SUPPORT_MAP parsed=0\n");return false;
    }
    bool structure=sameMapFile.GetNumEntities()==2&&sameMapFile.GetGeometryCRC()==0&&
        idStr::Icmp(sameMapFile.GetName(),mapName)==0;
    for(int i=0;i<sameMapFile.GetNumEntities();++i)
        structure=structure&&sameMapFile.GetEntity(i)->GetNumPrimitives()==0;
    const idMapEntity* world=sameMapFile.GetNumEntities()>0?sameMapFile.GetEntity(0):NULL;
    const idMapEntity* start=sameMapFile.GetNumEntities()>1?sameMapFile.GetEntity(1):NULL;
    const idStr identity(world?world->epairs.GetString("wolf2009_support_identity",""):"");
    idVec3 authoredStart=vec3_origin;
    const bool origin=start&&start->epairs.GetVector("origin","0 0 0",authoredStart);
    structure=structure&&world&&start&&!identity.IsEmpty()&&
        !idStr::Icmp(world->epairs.GetString("classname"),"worldspawn")&&
        !idStr::Icmp(start->epairs.GetString("classname"),"info_player_start")&&
        origin&&authoredStart.Compare(vec3_origin,.0001f);
    common->Printf("WOLF2009_SOURCE_SUPPORT_MAP parsed=1 entities=%d primitives=0 crc=%u "
        "identity=%s authored_start=(%.4f,%.4f,%.4f) valid=%d\n",sameMapFile.GetNumEntities(),
        sameMapFile.GetGeometryCRC(),identity.c_str(),authoredStart.x,authoredStart.y,authoredStart.z,structure);
    if(!structure)return false;
    const WolfCMStatus before=Wolf2009_CMSourceMapCached(0,sameMapFile.GetName(),identity.c_str());
    if(before!=WolfCM_Complete)return false;
    // With the identical generated name/time/identity this MUST retain the
    // live binding. Rebuilding it would allocate a different feature range.
    collisionModelManager->LoadMap(&sameMapFile);
    idBounds after;int contents=-1;
    const WolfCMStatus metadata=Wolf2009_CMMetadata(0,after,contents);
    int feature=0;
    const bool trace=Trace("cached-world",wallStart,wallEnd,NULL,1,wallFraction,idVec3(-1,0,0),&feature);
    const bool cached=metadata==WolfCM_Complete&&contents==3&&after.Compare(originalBounds,.0001f)&&
        !idStr::Cmp(collisionModelManager->GetModelName(0),"worldMap")&&trace&&feature==originalFeature;
    // Intentional failure uses the DIRECT status API. The native wrapper
    // raises common->Error on Invalid and must never receive this bad identity.
    const WolfCMStatus wrong=Wolf2009_CMSourceMapCached(0,sameMapFile.GetName(),"wrong");
    const WolfCMStatus retained=Wolf2009_CMSourceMapCached(0,sameMapFile.GetName(),identity.c_str());
    idBounds retainedBounds;int retainedContents=-1;
    const WolfCMStatus retainedMetadata=Wolf2009_CMMetadata(0,retainedBounds,retainedContents);
    const bool valid=cached&&wrong==WolfCM_Invalid&&retained==WolfCM_Complete&&
        retainedMetadata==WolfCM_Complete&&retainedContents==3&&retainedBounds.Compare(originalBounds,.0001f);
    common->Printf("WOLF2009_SOURCE_SUPPORT_CACHE repeat_loadmap=1 original_feature=%d feature=%d "
        "metadata_status=%d wrong_identity_status=%d retained_status=%d stable=%d valid=%d\n",
        originalFeature,feature,int(metadata),int(wrong),int(retained),cached,valid);
    return valid;
}
} // namespace

bool Wolf2009_RunSourceSupportProbe(const char* canonicalMapName) {
    if(!canonicalMapName||!collisionModelManager||!fileSystem)return false;
    const idStr name(canonicalMapName);
    const int labelEnd=name.Find('/',9);
    if(name.Icmpn("wolf2009/",9)||labelEnd<=9||
       idStr::Icmp(name.Mid(labelEnd,name.Length()-labelEnd).c_str(),"/PROC/maps/game/empty/empty")) {
        common->Printf("WOLF2009_SOURCE_SUPPORT_PROBE refused_profile=%s\n",canonicalMapName);return false;
    }
    attempted=true;passed=false;
    const idStr label=name.Mid(9,labelEnd-9);
    const bool actualWorld=!idStr::Cmp(collisionModelManager->GetModelName(0),"worldMap");
    idBounds bounds;
    if(!actualWorld||!Metadata(bounds))return false;
    // Zero is the valid main-world handle, not a failed standalone model load.
    if(collisionModelManager->LoadModel("worldMap",false)!=0)return false;
    const float clip=CM_CLIP_EPSILON;
    const idVec3 wallStart(0,0,48),wallEnd(256,0,48),left(-1,0,0),up(0,0,1);
    const float wallFraction=(184-clip)/256;
    int feature=0;
    if(!Trace("wall-point",wallStart,wallEnd,NULL,1,wallFraction,left,&feature))return false;
    if(!Trace("floor-point",idVec3(0,0,32),idVec3(0,0,-32),NULL,1,(32-clip)/64,up))return false;
    const idTraceModel playerBox(idBounds(idVec3(-16,-16,0),idVec3(16,16,72)));
    if(!Trace("wall-actor",idVec3(0,0,8),idVec3(256,0,8),&playerBox,1,(168-clip)/256,left))return false;
    if(!Trace("floor-actor",idVec3(0,0,32),idVec3(0,0,-32),&playerBox,1,(32-clip)/64,up))return false;
    if(!Trace("wall-opaque-bit",wallStart,wallEnd,NULL,2,wallFraction,left))return false;
    if(!Trace("wall-mask8-excluded",wallStart,wallEnd,NULL,8,1,vec3_origin))return false;
    if(!MapAndCache(canonicalMapName,feature,bounds,wallStart,wallEnd,wallFraction))return false;
    const idStr cmPath=name+".cm";
    const bool classified=Wolf2009_IsSourceCollisionPath(cmPath.c_str());
    const bool preparing=Wolf2009_CMSourceMapPreparing();
    idFile* denied=Wolf2009_OpenSourceCollisionPath(cmPath.c_str());
    const bool outsideRefused=classified&&!preparing&&denied==NULL;
    if(denied)fileSystem->CloseFile(denied);
    common->Printf("WOLF2009_SOURCE_SUPPORT_CM_ACCESS preparing=%d recognized=%d outside_refused=%d\n",
        preparing,classified,outsideRefused);
    if(!outsideRefused)return false;
    unsigned long long before=0,after=0;
    const bool knownBefore=Wolf2009_MountEpoch(label.c_str(),before);
    if(!knownBefore||!before)return false;
    const bool removed=Wolf2009_Unmount(label.c_str());
    const bool knownAfter=Wolf2009_MountEpoch(label.c_str(),after);
    int retainedFeature=0;
    const bool retainedTrace=Trace("after-refused-unmount",wallStart,wallEnd,NULL,1,wallFraction,left,&retainedFeature);
    idBounds retainedBounds;int retainedContents=-1;
    const WolfCMStatus retained=Wolf2009_CMMetadata(0,retainedBounds,retainedContents);
    const bool lease=!removed&&knownAfter&&after==before&&retainedTrace&&retainedFeature==feature&&
        retained==WolfCM_Complete&&retainedContents==3&&retainedBounds.Compare(bounds,.0001f);
    common->Printf("WOLF2009_SOURCE_SUPPORT_LEASE label=%s unmounted=%d before=%llu after=%llu "
        "feature_stable=%d valid=%d\n",label.c_str(),removed,before,after,retainedFeature==feature,lease);
    if(!lease)return false;
    passed=true;
    common->Printf("WOLF2009_ACTUAL_SOURCE_SUPPORT_QA_OK source=%s policy=DeclaredDoom3BitsV1 "
        "point_query=declared_geometry original_runtime_filter=unqualified retail_ray=unqualified\n",canonicalMapName);
    return true;
}
bool Wolf2009_CheckSourceSupportReleased() {
    if(!attempted)return true;
    idBounds bounds;int contents=-1;
    const WolfCMStatus metadata=Wolf2009_CMMetadata(0,bounds,contents);
    const bool lifetime=Wolf2009_CMSourceLifetimeOK();
    const bool valid=lifetime&&metadata==WolfCM_NoSidecar;
    // common has shut down: use the process-local stdout capture directly.
    fprintf(stdout,"WOLF2009_SOURCE_SUPPORT_LIFETIME SourceLifetime=%d metadata_status=%d probe_passed=%d valid=%d\n",
        lifetime,int(metadata),passed,valid);fflush(stdout);
    attempted=false;passed=false;return valid;
}

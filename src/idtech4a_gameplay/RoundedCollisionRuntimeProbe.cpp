// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored QA geometry, analytic expectations and native GPL
// game API input. No retail map substitution, OS input or custom controller.
#include "idlib/precompiled.h"
#include "RoundedCollisionRuntimeProbe.h"
#include "EngineRoundedCollision.h"
#include "cm/CollisionModel.h"
#include "renderer/Material.h"
#include <math.h>
#include <stdio.h>

namespace {
const char fixtureName[]="wolf2009/_qa/rounded-support";
const char fixturePath[]="wolf2009/_qa/rounded-support.cm";
const char surfaceName[]="rounded_collision_player_surface";
cmHandle_t fixtureHandle=-1;
bool Near(float a,float b,float tolerance=.002f) { return idMath::Fabs(a-b)<=tolerance; }
bool Finite(float a) { return a==a&&a>=-10000000.0f&&a<=10000000.0f; }
bool Finite(const idVec3& p) { return Finite(p.x)&&Finite(p.y)&&Finite(p.z); }
bool Contact(const contactInfo_t& c) {
    return c.type==CONTACT_WOLF2009_SUPPORT&&c.contents==1&&c.material&&
        !idStr::Cmp(c.material->GetName(),"wolf2009/collision/solid")&&
        c.modelFeature>0&&c.trmFeature==-1&&Finite(c.point)&&Finite(c.normal)&&
        Near(c.normal.LengthSqr(),1,.001f)&&Near(c.dist,c.point*c.normal,.01f);
}
class FixtureEnabled {
  public:
    FixtureEnabled() { Wolf2009_EnableRoundedCollisionFixture(true); }
    ~FixtureEnabled() { Wolf2009_EnableRoundedCollisionFixture(false); }
};
WolfCMQuery Query(cmHandle_t handle,const idTraceModel* shape,const idMat3& actorAxis,
                  const idVec3& modelOrigin,const idMat3& modelAxis) {
    WolfCMQuery q;q.handle=handle;q.contentMask=1;q.trm=shape;q.trmAxis=&actorAxis;
    q.modelOrigin=&modelOrigin;q.modelAxis=&modelAxis;q.clipEpsilon=CM_CLIP_EPSILON;
    return q;
}
bool Trace(const char* label,cmHandle_t handle,const idVec3& start,const idVec3& end,
           const idTraceModel* shape,float expected,const idVec3& normal,int* feature=NULL,
           const idMat3& actorAxis=mat3_default,const idVec3& modelOrigin=vec3_origin,
           const idMat3& modelAxis=mat3_default,int mask=1) {
    trace_t t;
    collisionModelManager->Translation(&t,start,end,shape,actorAxis,mask,handle,modelOrigin,modelAxis);
    const bool hit=expected<1;
    const bool actualHit=Finite(t.fraction)&&t.fraction>=0&&t.fraction<1;
    const bool valid=Finite(t.fraction)&&Near(t.fraction,expected,.0003f)&&
        t.endpos.Compare(start+(end-start)*expected,.05f)&&t.endAxis.Compare(actorAxis,.0001f)&&
        (!hit||(actualHit&&Contact(t.c)&&t.c.normal.Compare(normal,.003f)));
    if(feature&&hit)*feature=actualHit?t.c.modelFeature:0;
    const idVec3 n=actualHit?t.c.normal:vec3_origin;
    common->Printf("WOLF2009_ROUNDED_TRACE case=%s fraction=%.7f expected=%.7f "
        "end=(%.5f,%.5f,%.5f) normal=(%.5f,%.5f,%.5f) feature=%d valid=%d\n",
        label,t.fraction,expected,t.endpos.x,t.endpos.y,t.endpos.z,n.x,n.y,n.z,actualHit?t.c.modelFeature:0,valid);
    return valid;
}
bool Positions(cmHandle_t handle,const idTraceModel& box) {
    const idVec3 penetrating(0,-64,2),clear(0,-64,4.25f);
    const int overlap=collisionModelManager->Contents(penetrating,&box,mat3_default,1,handle,vec3_origin,mat3_default);
    const int separated=collisionModelManager->Contents(clear,&box,mat3_default,1,handle,vec3_origin,mat3_default);
    const int excluded=collisionModelManager->Contents(penetrating,&box,mat3_default,8,handle,vec3_origin,mat3_default);
    const int sphere=collisionModelManager->Contents(idVec3(150,0,48),NULL,mat3_default,1,handle,vec3_origin,mat3_default);
    trace_t zero,rotationZero;
    collisionModelManager->Translation(&zero,penetrating,penetrating,&box,mat3_default,1,handle,vec3_origin,mat3_default);
    const idRotation none(vec3_origin,idVec3(0,0,1),0);
    collisionModelManager->Rotation(&rotationZero,penetrating,none,&box,mat3_default,1,handle,vec3_origin,mat3_default);
    trace_t direct;
    const WolfCMQuery q=Query(handle,&box,mat3_default,vec3_origin,mat3_default);
    const WolfCMStatus status=Wolf2009_CMPosition(direct,clear,q);
    const bool valid=overlap==1&&separated==0&&excluded==0&&sphere==1&&zero.fraction==0&&
        rotationZero.fraction==0&&Contact(zero.c)&&Contact(rotationZero.c)&&
        status==WolfCM_Complete&&direct.fraction==1&&direct.c.contents==0;
    common->Printf("WOLF2009_ROUNDED_POSITION overlap=%d clear=%d excluded=%d sphere=%d zero=%.1f "
        "rotation_zero=%.1f direct_status=%d valid=%d\n",overlap,separated,excluded,sphere,
        zero.fraction,rotationZero.fraction,int(status),valid);
    return valid;
}
bool Contacts(cmHandle_t handle,const idTraceModel& box,int floorFeature,int sphereFeature) {
    contactInfo_t contacts[8];idVec6 dir;dir.Zero();dir[2]=-1;
    const int floor=collisionModelManager->Contacts(contacts,8,idVec3(0,-64,4.25f),dir,1,
        &box,mat3_default,1,handle,vec3_origin,mat3_default);
    if(floor<0||floor>8)return false;
    bool floorValid=floor>0;
    for(int i=0;i<floor;++i)floorValid=floorValid&&Contact(contacts[i])&&
        contacts[i].modelFeature==floorFeature&&contacts[i].normal.Compare(idVec3(0,0,1),.003f)&&
        Near(contacts[i].point.z,0,.01f);
    // Non-normalized linear direction deliberately crosses both colliders.
    // Earliest-hit-only collection would return the sphere and lose the floor.
    dir.Zero();dir[0]=128;dir[2]=-96;
    const int both=collisionModelManager->Contacts(contacts,8,idVec3(96,0,80),dir,1,
        NULL,mat3_default,1,handle,vec3_origin,mat3_default);
    if(both<0||both>8)return false;
    bool foundFloor=false,foundSphere=false,qualified=true;
    for(int i=0;i<both;++i) {
        qualified=qualified&&Contact(contacts[i]);
        foundFloor=foundFloor||contacts[i].modelFeature==floorFeature;
        foundSphere=foundSphere||contacts[i].modelFeature==sphereFeature;
    }
    const int capped=collisionModelManager->Contacts(contacts,1,idVec3(96,0,80),dir,1,
        NULL,mat3_default,1,handle,vec3_origin,mat3_default);
    const int zero=collisionModelManager->Contacts(NULL,0,idVec3(96,0,80),dir,1,
        NULL,mat3_default,1,handle,vec3_origin,mat3_default);
    const bool valid=floorValid&&both==2&&foundFloor&&foundSphere&&qualified&&capped==1&&zero==0;
    common->Printf("WOLF2009_ROUNDED_CONTACTS floor=%d both=%d floor_feature=%d sphere_feature=%d "
        "capped=%d capacity_zero=%d valid=%d\n",floor,both,foundFloor,foundSphere,capped,zero,valid);
    return valid;
}
bool PivotRotation(cmHandle_t handle,int sphereFeature) {
    const idVec3 pivot(160,-80,48),start(80,-80,48),axis(0,0,1);
    const idRotation positive(pivot,axis,180),negative(pivot,axis,-180);
    const idVec3 end=positive*start;
    // The chord y=-80 misses the sphere, while the positive native pivot arc
    // goes through its centre at half the rotation. Negative180 takes the other arc.
    if(!Trace("pivot-chord-control",handle,start,end,NULL,1,vec3_origin))return false;
    const double pi=3.14159265358979323846;
    const float expected=static_cast<float>((pi*.5-2*asin((32+CM_CLIP_EPSILON)/160.0))/pi);
    trace_t hit,uncertified,miss;
    collisionModelManager->Rotation(&hit,start,positive,NULL,mat3_default,1,handle,vec3_origin,mat3_default);
    const WolfCMQuery q=Query(handle,NULL,mat3_default,vec3_origin,mat3_default);
    const WolfCMStatus unproved=Wolf2009_CMRotation(uncertified,start,negative,q);
    // This geometrically clear broad negative arc still lacks a complete
    // independent no-hit certificate. Test the typed refusal explicitly;
    // never turn Bullet's OutsideInterval into a successful native miss.
    // A separate far, small negative arc exercises a certified public miss.
    const idVec3 smallPivot(-64,-80,48),smallStart(-72,-80,48);
    const idRotation smallNegative(smallPivot,axis,-180);
    collisionModelManager->Rotation(&miss,smallStart,smallNegative,NULL,mat3_default,1,handle,vec3_origin,mat3_default);
    const idTraceModel spinningBox(idBounds(idVec3(-1,-2,-3),idVec3(1,2,3)));
    const idMat3 spinningAxis=idRotation(vec3_origin,idVec3(0,1,0),30).ToMat3();
    trace_t boxMiss;
    collisionModelManager->Rotation(&boxMiss,smallStart,smallNegative,&spinningBox,spinningAxis,1,handle,vec3_origin,mat3_default);
    const idRotation at(pivot,axis,180*expected);
    const idVec3 expectedPosition=at*start;
    idVec3 expectedNormal=expectedPosition-idVec3(160,0,48);expectedNormal.Normalize();
    const bool valid=Near(hit.fraction,expected,.0015f)&&hit.endpos.Compare(expectedPosition,.08f)&&
        Contact(hit.c)&&hit.c.modelFeature==sphereFeature&&hit.c.normal.Compare(expectedNormal,.004f)&&
        Near((hit.c.point-idVec3(160,0,48)).Length(),32,.02f)&&
        hit.endAxis.Compare(mat3_default,.0001f)&&
        unproved==WolfCM_Indeterminate&&miss.fraction==1&&miss.endpos.Compare(smallNegative*smallStart,.002f)&&
        miss.endAxis.Compare(mat3_default,.0001f)&&boxMiss.fraction==1&&
        boxMiss.endpos.Compare(smallNegative*smallStart,.002f)&&
        boxMiss.endAxis.Compare(spinningAxis*smallNegative.ToMat3(),.0001f);
    common->Printf("WOLF2009_ROUNDED_ROTATION positive=%.7f expected=%.7f negative_small=%.7f "
        "uncertified_negative_status=%d end=(%.5f,%.5f,%.5f) valid=%d\n",hit.fraction,expected,miss.fraction,int(unproved),
        hit.endpos.x,hit.endpos.y,hit.endpos.z,valid);
    return valid;
}
bool ExpectedFailures(cmHandle_t handle,const idTraceModel& box) {
    trace_t trace;memset(&trace,0,sizeof(trace));trace.fraction=.375f;
    const idVec3 largeOrigin(8388608.0f,0,0);
    WolfCMQuery q=Query(handle,NULL,mat3_default,largeOrigin,mat3_default);
    const WolfCMStatus projection=Wolf2009_CMTranslation(trace,
        idVec3(8388708.0f,7.110555648803711f,48),idVec3(8388758.0f,7.110555648803711f,48),q);
    // Expected failures use DIRECT APIs. The public CM wrapper deliberately
    // raises common->Error for these statuses and must not turn them into clear.
    idVec6 tiny;tiny.Zero();tiny[0]=2e-19f;tiny[3]=1e-23f;
    q=Query(handle,NULL,mat3_default,vec3_origin,mat3_default);
    int count=77;
    const WolfCMStatus angular=Wolf2009_CMContacts(NULL,0,count,idVec3(0,0,48),tiny,1e22f,q);
    idMat3 reflected=mat3_default;reflected[0]=-reflected[0];
    q=Query(handle,&box,reflected,vec3_origin,mat3_default);
    const WolfCMStatus invalid=Wolf2009_CMTranslation(trace,idVec3(0,-64,32),idVec3(0,-64,-32),q);
    const bool valid=projection==WolfCM_Indeterminate&&angular==WolfCM_Indeterminate&&
        count==0&&invalid==WolfCM_Invalid;
    common->Printf("WOLF2009_ROUNDED_EXPECTED_FAILURES float_projection=%d tiny_angular=%d "
        "angular_count=%d invalid_frame=%d not_usable=%d\n",int(projection),int(angular),count,int(invalid),valid);
    return valid;
}
void Frames(int count,int forward=0) {
    usercmd_t commands[MAX_ASYNC_CLIENTS];memset(commands,0,sizeof(commands));
    commands[0].forwardmove=forward;commands[0].buttons=BUTTON_RUN;
    for(int i=0;i<count;++i)game->RunFrame(commands);
}
class PlayerSetup {
    idEntity* player;
    idVec3 origin;
    idEntity* surface;
  public:
    PlayerSetup(idEntity* p):player(p),surface(NULL) {gameEdit->EntityGetOrigin(player,origin);}
    void Surface(idEntity* p) {surface=p;}
    void Restore() {
        if(!gameEdit)return;
        if(player&&gameEdit->FindEntity("player1")==player)gameEdit->EntitySetOrigin(player,origin);
        if(surface&&gameEdit->FindEntity(surfaceName)==surface)gameEdit->EntityDelete(surface);
        player=surface=NULL;
    }
    ~PlayerSetup() {try{Restore();}catch(...){}}
};
bool Player(cmHandle_t handle) {
    if(!game||!gameEdit||!gameEdit->PlayerIsValid())return false;
    idEntity* player=gameEdit->FindEntity("player1");
    const idDict* definition=player?gameEdit->EntityGetSpawnArgs(player):NULL;
    if(!definition||idStr::Icmp(definition->GetString("spawnclass"),"idPlayer")||gameEdit->FindEntity(surfaceName))return false;
    PlayerSetup restore(player);
    idDict args;args.Set("classname","wolf2009_static");args.Set("name",surfaceName);
    args.Set("origin","1000 0 0");args.Set("clipmodel",fixtureName);
    idEntity* surface=NULL;gameEdit->SpawnEntityDef(args,&surface);if(!surface)return false;
    restore.Surface(surface);
    // Setup warps occur before measured input. Existing friction/gravity settle
    // residual velocity from the preceding tests; there is no per-frame correction.
    gameEdit->EntitySetOrigin(player,idVec3(960,0,4));Frames(30);
    idVec3 settled;gameEdit->EntityGetOrigin(player,settled);
    gameEdit->EntitySetOrigin(player,idVec3(960,0,settled.z));Frames(30);
    gameEdit->EntityGetOrigin(player,settled);
    Frames(18,127);idVec3 walked;gameEdit->EntityGetOrigin(player,walked);
    Frames(150,127);idVec3 stopped;gameEdit->EntityGetOrigin(player,stopped);
    const idTraceModel actor(idBounds(idVec3(-16,-16,0),idVec3(16,16,72)));
    const int contents=collisionModelManager->Contents(stopped,&actor,mat3_default,1,handle,idVec3(1000,0,0),mat3_default);
    const bool floor=Finite(settled)&&Near(settled.x,960,.2f)&&Near(settled.y,0,.2f)&&settled.z>0&&settled.z<.5f;
    const bool moving=walked.x>settled.x+25&&walked.x<1060&&Near(walked.y,settled.y,.3f)&&Near(walked.z,settled.z,.06f);
    // Sphere front128, actor half-width16, native query skin.25, translated1000.
    const float expected=1000+128-16-CM_CLIP_EPSILON;
    const bool wall=Near(stopped.x,expected,.35f)&&Near(stopped.y,settled.y,.3f)&&
        Near(stopped.z,settled.z,.06f)&&contents==0;
    common->Printf("WOLF2009_ROUNDED_PLAYER settled=(%.5f,%.5f,%.5f) walked=(%.5f,%.5f,%.5f) "
        "stopped=(%.5f,%.5f,%.5f) expected_x=%.5f contents=%d floor=%d moving=%d wall=%d\n",
        settled.x,settled.y,settled.z,walked.x,walked.y,walked.z,stopped.x,stopped.y,stopped.z,expected,contents,floor,moving,wall);
    restore.Restore();
    return floor&&moving&&wall;
}
} // namespace

bool Wolf2009_RunRoundedCollisionFixtureProbe() {
    if(!collisionModelManager||!game||!gameEdit)return false;
    cmHandle_t handle;
    {FixtureEnabled enabled;handle=collisionModelManager->LoadModel(fixtureName,true);}
    if(handle<=0||idStr::Cmp(collisionModelManager->GetModelName(handle),fixtureName))return false;
    fixtureHandle=handle;
    idBounds bounds,directBounds;int contents=0,directContents=0;
    bounds.Clear();directBounds.Clear();
    const bool model=collisionModelManager->LoadModel(fixtureName,true)==handle&&
        Wolf2009_CMModelReady(handle,fixtureName)==WolfCM_Complete&&
        !Wolf2009_IsRoundedCollisionFixturePath(fixturePath)&&Wolf2009_OpenRoundedCollisionFixturePath(fixturePath)==NULL&&
        collisionModelManager->GetModelBounds(handle,bounds)&&collisionModelManager->GetModelContents(handle,contents)&&
        Wolf2009_CMMetadata(handle,directBounds,directContents)==WolfCM_Complete&&contents==1&&directContents==1&&
        bounds[0].Compare(idVec3(-258,-258,-18),.0001f)&&bounds[1].Compare(idVec3(258,258,80),.0001f)&&
        directBounds.Compare(bounds,.0001f);
    common->Printf("WOLF2009_ROUNDED_MODEL handle=%d cached_after_stream_close=%d contents=%d "
        "bounds=(%.4f,%.4f,%.4f)/(%.4f,%.4f,%.4f) valid=%d\n",int(handle),model,contents,
        bounds[0].x,bounds[0].y,bounds[0].z,bounds[1].x,bounds[1].y,bounds[1].z,model);
    if(!model)return false;
    const float clip=CM_CLIP_EPSILON;const idVec3 up(0,0,1),left(-1,0,0);
    int floorFeature=0,sphereFeature=0;
    if(!Trace("floor-point",handle,idVec3(-64,0,32),idVec3(-64,0,-32),NULL,(32-clip)/64,up,&floorFeature))return false;
    if(!Trace("sphere-point",handle,idVec3(96,0,48),idVec3(224,0,48),NULL,(32-clip)/128,left,&sphereFeature))return false;
    if(floorFeature<=0||sphereFeature<=0||floorFeature==sphereFeature)return false;
    const idTraceModel box(idBounds(idVec3(-4,-4,-4),idVec3(4,4,4)));
    if(!Trace("floor-box",handle,idVec3(0,-64,32),idVec3(0,-64,-32),&box,(28-clip)/64,up))return false;
    if(!Trace("sphere-box",handle,idVec3(96,0,48),idVec3(224,0,48),&box,(28-clip)/128,left))return false;
    if(!Trace("sphere-filter-excluded",handle,idVec3(96,0,48),idVec3(224,0,48),NULL,1,vec3_origin,NULL,
              mat3_default,vec3_origin,mat3_default,8))return false;
    // An octahedron's right support vertex is (8,0,0); its bounds would also
    // contain the nonexistent (8,8,0) corner. Analytic sphere TOI distinguishes them.
    idTraceModel octahedron;octahedron.SetupOctahedron(idBounds(idVec3(-8,-8,-8),idVec3(8,8,8)));
    const double radius=32+clip,dx=sqrt(radius*radius-64);
    const idVec3 octaNormal(static_cast<float>(-dx/radius),static_cast<float>(-8/radius),0);
    if(!Trace("actual-octahedron-not-bounds",handle,idVec3(96,-8,48),idVec3(224,-8,48),&octahedron,
              static_cast<float>((152-dx-96)/128),octaNormal))return false;
    const idTraceModel asymmetric(idBounds(idVec3(-8,-2,-4),idVec3(8,2,4)));
    const idMat3 actorAxis=idRotation(vec3_origin,idVec3(0,1,0),90).ToMat3();
    if(!Trace("rotated-actor",handle,idVec3(-64,0,32),idVec3(-64,0,-32),&asymmetric,(24-clip)/64,up,NULL,actorAxis))return false;
    const idVec3 modelOrigin(700,200,10);
    const idMat3 modelAxis=idRotation(vec3_origin,idVec3(0,0,1),90).ToMat3();
    if(!Trace("transformed-model-once",handle,idVec3(96,0,48)*modelAxis+modelOrigin,
              idVec3(224,0,48)*modelAxis+modelOrigin,NULL,(32-clip)/128,left*modelAxis,NULL,
              mat3_default,modelOrigin,modelAxis))return false;
    if(!Positions(handle,box)||!Contacts(handle,box,floorFeature,sphereFeature)||
       !PivotRotation(handle,sphereFeature)||!ExpectedFailures(handle,box))return false;
    common->Printf("WOLF2009_NATIVE_ROUNDED_QUERIES_QA_OK\n");
    if(!Player(handle))return false;
    common->Printf("WOLF2009_ACTUAL_NATIVE_ROUNDED_COLLISION_QA_OK\n");
    return true;
}
bool Wolf2009_CheckRoundedCollisionFixtureReleased() {
    if(fixtureHandle<0)return true;
    idBounds bounds;int contents=0;
    const bool absent=Wolf2009_CMMetadata(fixtureHandle,bounds,contents)==WolfCM_NoSidecar;
    const bool nativeInvalid=collisionModelManager&&!collisionModelManager->GetModelBounds(fixtureHandle,bounds);
    const bool released=absent&&nativeInvalid;
    fprintf(stdout,"WOLF2009_ROUNDED_COLLISION_LIFETIME released=%d native_invalid=%d sidecar_absent=%d\n",
        released,nativeInvalid,absent);fflush(stdout);fixtureHandle=-1;return released;
}

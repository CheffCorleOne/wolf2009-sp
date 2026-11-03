// SPDX-License-Identifier: GPL-3.0-or-later
// Native boundary for one authored QA model. Queries remain in the shared
// owning support index/Bullet geometry backend; GPL idClip/player are reused.
#include "SupportCollisionIndex.h"
#include "EngineRoundedCollision.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "cm/CollisionModel.h"
#include "framework/DeclManager.h"
#include "renderer/Material.h"

namespace {
using namespace wolf2009;
const char fixtureName[]="wolf2009/_qa/rounded-support";
const char fixturePath[]="wolf2009/_qa/rounded-support.cm";
const char fixtureCM[]=
    "CM \"1.00\"\n0\ncollisionModel \"wolf2009/_qa/rounded-support\" {\n"
    "vertices { 0 }\nedges { 1 ( 0 0 ) 0 0 }\n"
    "nodes { ( -1 0 ) }\npolygons { }\nbrushes { }\n}\n";
void Need(bool ok,const char* reason) { if(!ok)throw std::runtime_error(reason); }
CollisionVec3 Vec(const idVec3& v) { return {v.x,v.y,v.z}; }
bool Finite(CollisionVec3 v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
        std::abs(v.x)<=10000000&&std::abs(v.y)<=10000000&&std::abs(v.z)<=10000000;
}
CollisionVec3 Add(CollisionVec3 a,CollisionVec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
CollisionVec3 Sub(CollisionVec3 a,CollisionVec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
CollisionVec3 Rotate(const CollisionRigidTransform& t,CollisionVec3 p) {
    return {t.axis[0].x*p.x+t.axis[1].x*p.y+t.axis[2].x*p.z,
            t.axis[0].y*p.x+t.axis[1].y*p.y+t.axis[2].y*p.z,
            t.axis[0].z*p.x+t.axis[1].z*p.y+t.axis[2].z*p.z};
}
CollisionRigidTransform Pose(const idVec3& origin,const idMat3& axis) {
    CollisionRigidTransform p;p.translation=Vec(origin);
    for(int k=0;k<3;++k)p.axis[k]=Vec(axis[k]);
    return p;
}
idVec3 Native(CollisionVec3 v) {
    Need(Finite(v),"Rounded native vector is outside the finite coordinate profile");
    return idVec3(static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z));
}
float BoundFloat(double value,bool upper) {
    Need(std::isfinite(value)&&std::abs(value)<=10000000,"Rounded native bound is invalid");
    float out=static_cast<float>(value);
    if((upper&&double(out)<value)||(!upper&&double(out)>value))
        out=std::nextafter(out,upper?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity());
    return out;
}
SupportCollisionWorld FixtureWorld() {
    SupportCollisionWorld world;
    world.sourceFingerprint=0x52534d4151544f57ull; // authored fixture identity, not HKXS provenance
    world.selectedBodyName=fixtureName;world.rootRawFilterInfo=1;world.rootRawFilterKnown=true;
    world.selectedGeometryComplete=world.selectedHierarchyComplete=world.selectedContentsComplete=true;
    world.selectedChildFiltersComplete=world.selectedContactMetadataComplete=world.selectedCoverageComplete=true;
    world.allSourceRigidBodiesSelected=true;
    for(int i=0;i<2;++i) {
        SupportCollisionShape shape;
        shape.source.sourceShapeOffset=i+1;shape.source.sourceRegion=0x51520000ull+i+1;
        shape.source.occurrence=i+1;
        shape.filter.inheritedRawContents=1;shape.filter.inheritedContentsKnown=true;
        if(!i) {
            shape.kind=SupportCollisionKind::Box;shape.radius=2;
            for(int bits=0;bits<8;++bits)
                shape.corePoints.push_back({bits&1?256.0:-256.0,bits&2?256.0:-256.0,bits&4?-2.0:-16.0});
        }else {
            shape.kind=SupportCollisionKind::Sphere;shape.radius=32;
            shape.corePoints.push_back({160,0,48});
        }
        shape.declaredCorePoints=shape.corePoints.size();world.totalCorePoints+=shape.corePoints.size();
        world.shapes.push_back(std::move(shape));
    }
    return world;
}
struct Binding {
    const SupportCollisionWorld world;
    const SupportCollisionIndex index;
    const idMaterial* material;
    const std::uint64_t generation;
    const int firstFeature;
    idBounds bounds;
    Binding(const idMaterial* m,std::uint64_t g,int feature)
        :world(FixtureWorld()),index(world),material(m),generation(g),firstFeature(feature) {
        bool first=true;
        for(const auto& shape:world.shapes) {
            RoundedConvexShape physical;physical.corePoints=shape.corePoints;physical.radius=shape.radius;
            const auto b=RoundedConvexBounds(physical,shape.placement);
            Need(b.valid,"Authored rounded fixture has invalid canonical bounds");
            const double lo[]={b.minimum.x,b.minimum.y,b.minimum.z},hi[]={b.maximum.x,b.maximum.y,b.maximum.z};
            for(int k=0;k<3;++k) {
                const float l=BoundFloat(lo[k],false),h=BoundFloat(hi[k],true);
                if(first){bounds[0][k]=l;bounds[1][k]=h;}
                else {bounds[0][k]=std::min(bounds[0][k],l);bounds[1][k]=std::max(bounds[1][k],h);}
            }
            first=false;
        }
    }
};
struct Registry {
    bool enabled=false;
    std::uint64_t generation=1,nextFeature=1;
    std::map<int,std::unique_ptr<Binding>> bindings;
    std::string diagnostic;
};
Registry& State() { static Registry state;return state; }
WolfCMStatus Error(WolfCMStatus status,const char* message) { State().diagnostic=message;return status; }
Binding* Lookup(int handle) {
    const auto i=State().bindings.find(handle);
    if(i==State().bindings.end())return NULL;
    Need(i->second->generation==State().generation,"Stale rounded collision model generation");
    return i->second.get();
}
struct QueryWorld {
    const Binding& binding;
    std::unique_ptr<SupportCollisionIndex> transformed;
    const SupportCollisionIndex* index;
    QueryWorld(const Binding& b,const WolfCMQuery& q):binding(b),index(&b.index) {
        Need(q.trmAxis&&q.modelOrigin&&q.modelAxis,"Rounded query requires actor/model frame pointers");
        Need(std::isfinite(q.clipEpsilon)&&q.clipEpsilon>=0&&q.clipEpsilon<=1000,
             "Rounded query clearance is invalid");
        CollisionRigidTransform canonical;
        Need(CanonicalRoundedConvexPose(Pose(*q.modelOrigin,*q.modelAxis),canonical),
             "Rounded model pose is not a proper finite rigid frame");
        bool identity=canonical.translation.x==0&&canonical.translation.y==0&&canonical.translation.z==0;
        for(int i=0;i<3;++i) {
            const double values[]={canonical.axis[i].x,canonical.axis[i].y,canonical.axis[i].z};
            for(int j=0;j<3;++j)identity=identity&&values[j]==(i==j?1.0:0.0);
        }
        if(identity)return;
        auto placed=b.world;
        for(auto& shape:placed.shapes)shape.placement=ComposeCollisionTransforms(canonical,shape.placement);
        transformed.reset(new SupportCollisionIndex(placed));index=transformed.get();
    }
};
RoundedConvexShape Actor(const WolfCMQuery& q) {
    RoundedConvexShape actor;
    if(!q.trm)actor.corePoints.push_back({0,0,0}); // explicit fixture point profile
    else {
        Need(q.trm->type!=TRM_INVALID&&q.trm->isConvex&&q.trm->numVerts>0&&q.trm->numVerts<=MAX_TRACEMODEL_VERTS,
             "Rounded adapter requires an actual convex trace-model vertex set");
        actor.corePoints.reserve(q.trm->numVerts);
        for(int i=0;i<q.trm->numVerts;++i) {
            const auto p=Vec(q.trm->verts[i]);Need(Finite(p),"Nonfinite/out-of-range actor trace vertex");
            actor.corePoints.push_back(p);
        }
    }
    return actor;
}
struct FixtureFilter : SupportCollisionQueryFilter {
    std::uint32_t mask;
    std::uint64_t only;
    explicit FixtureFilter(int value,std::uint64_t occurrence=0):mask(static_cast<std::uint32_t>(value)),only(occurrence) {}
    SupportFilterDecision Select(const SupportCollisionIdentity& id,const SupportCollisionFilter& filter) const override {
        if(only&&id.occurrence!=only)return SupportFilterDecision::Exclude;
        if(!filter.inheritedContentsKnown||filter.inheritedRawContents!=1)return SupportFilterDecision::Unknown;
        return (mask&1)?SupportFilterDecision::Include:SupportFilterDecision::Exclude;
    }
};
SupportCollisionQueryLimits Limits(const WolfCMQuery& q,bool position=false) {
    SupportCollisionQueryLimits limits;
    limits.geometry.targetSeparationGameUnits=position?0:q.clipEpsilon;
    limits.clearancePolicy=SupportClearancePolicy::NonBlockingPhysical;
    return limits;
}
WolfCMStatus QueryStatus(const SupportCollisionQueryResult& result,const char* operation) {
    if(result.selectedBodyQueryComplete&&(result.status==SupportCollisionQueryStatus::Separated||
       result.status==SupportCollisionQueryStatus::Hit||result.status==SupportCollisionQueryStatus::InitialOverlap)) {
        if(result.status==SupportCollisionQueryStatus::Separated)return WolfCM_Complete;
        if(result.hasCandidate&&result.responseMetadataQualified)return WolfCM_Complete;
    }
    std::ostringstream reason;
    reason<<operation<<": index status "<<static_cast<int>(result.status)<<", "
          <<RoundedConvexStatusName(result.candidate.status)<<"/"<<RoundedConvexReasonName(result.candidate.reason)
          <<", uncertain="<<result.uncertainQueries<<", filters="<<result.unknownFilters;
    if(result.uncertainQueries)reason<<", first uncertain shape "<<result.firstUncertainShapeIndex<<": "
        <<RoundedConvexStatusName(result.firstUncertain.status)<<"/"<<RoundedConvexReasonName(result.firstUncertain.reason);
    State().diagnostic=reason.str();
    return result.status==SupportCollisionQueryStatus::Invalid?WolfCM_Invalid:WolfCM_Indeterminate;
}
WolfCMStatus FloatStopStatus(const QueryWorld& world,const RoundedConvexShape& actor,
                            const FixtureFilter& filter,const trace_s& trace,const WolfCMQuery& q) {
    // Rounding fraction down is insufficient: float start + fraction*delta
    // can round past the certified TOI at large but admitted coordinates.
    // Check the actual native output frame, using physical radii/zero skin.
    const auto atStop=world.index->Overlap(actor,Pose(trace.endpos,trace.endAxis),filter,Limits(q,true));
    if(atStop.selectedBodyQueryComplete&&atStop.status==SupportCollisionQueryStatus::Separated)
        return WolfCM_Complete;
    return Error(WolfCM_Indeterminate,"Native float stop overlaps physical geometry or lacks a definite separation proof");
}
void EmptyTrace(trace_s& out,const idVec3& end,const idMat3& axis) {
    std::memset(&out,0,sizeof(out));out.fraction=1;out.endpos=end;out.endAxis=axis;
    out.c.type=CONTACT_NONE;out.c.modelFeature=out.c.trmFeature=-1;out.c.entityNum=-1;
}
void Contact(trace_s& out,const SupportCollisionQueryResult& result,const Binding& binding) {
    Need(result.shapeIndex<binding.world.shapes.size(),"Rounded hit has an invalid source feature");
    const auto& source=binding.world.shapes[result.shapeIndex].source;
    Need(source.sourceRegion==result.source.sourceRegion&&source.occurrence==result.source.occurrence&&
         source.sourceShapeOffset==result.source.sourceShapeOffset,"Rounded hit/source feature identity mismatch");
    const auto& hit=result.candidate;
    Need(hit.hasWitnesses&&hit.hasNormal&&hit.hasDistance&&Finite(hit.pointOnB)&&Finite(hit.normalBtoA),
         "Rounded contact lacks a proven physical witness/normal");
    const double n=hit.normalBtoA.x*hit.normalBtoA.x+hit.normalBtoA.y*hit.normalBtoA.y+hit.normalBtoA.z*hit.normalBtoA.z;
    Need(std::abs(n-1)<.00002,"Rounded contact normal is not unit length");
    out.c.type=CONTACT_WOLF2009_SUPPORT;out.c.point=Native(hit.pointOnB);out.c.normal=Native(hit.normalBtoA);
    out.c.dist=out.c.point*out.c.normal;out.c.contents=1;out.c.material=binding.material;
    out.c.modelFeature=binding.firstFeature+static_cast<int>(result.shapeIndex);out.c.trmFeature=-1;
}
double ExactFraction(const SupportCollisionQueryResult& result) {
    if(result.status==SupportCollisionQueryStatus::InitialOverlap)return 0;
    Need(result.candidate.hasFraction&&std::isfinite(result.candidate.fraction)&&
         result.candidate.fraction>=0&&result.candidate.fraction<=1,"Invalid rounded time of impact");
    return result.candidate.fraction;
}
float Fraction(double value) {
    Need(std::isfinite(value)&&value>=0&&value<=1,"Invalid rounded native fraction");
    float out=static_cast<float>(value);
    if(double(out)>value||out==1)out=std::nextafter(out,0.0f);
    return out;
}
class FixtureFile : public idFile {
    const std::string text;
    std::size_t position=0;
  public:
    FixtureFile():text(fixtureCM) {}
    const char* GetName() override { return fixturePath; }
    const char* GetFullPath() override { return fixturePath; }
    int Length() override { return static_cast<int>(text.size()); }
    int Tell() override { return static_cast<int>(position); }
    ID_TIME_T Timestamp() override { return 0; }
    int Read(void* data,int count) override {
        if(!data||count<=0)return 0;
        const auto n=std::min(std::size_t(count),text.size()-position);
        if(n)std::memcpy(data,text.data()+position,n);position+=n;return static_cast<int>(n);
    }
    int Write(const void*,int) override { return 0; }
    void ForceFlush() override {}
    void Flush() override {}
    int Seek(long offset,fsOrigin_t origin) override {
        const long long base=origin==FS_SEEK_SET?0:origin==FS_SEEK_CUR?static_cast<long long>(position):
            origin==FS_SEEK_END?static_cast<long long>(text.size()):-1;
        if(base<0||offset < -base||offset>static_cast<long long>(text.size())-base)return -1;
        position=static_cast<std::size_t>(base+offset);return 0;
    }
};
} // namespace

void Wolf2009_EnableRoundedCollisionFixture(bool enabled) { State().enabled=enabled; }
bool Wolf2009_IsRoundedCollisionFixturePath(const char* path) {
    return State().enabled&&path&&idStr::Cmp(path,fixturePath)==0;
}
idFile* Wolf2009_OpenRoundedCollisionFixturePath(const char* path) {
    if(!Wolf2009_IsRoundedCollisionFixturePath(path))return NULL;
    try {return new FixtureFile();}
    catch(const std::exception& e){Error(WolfCM_Invalid,e.what());return NULL;}
    catch(...){Error(WolfCM_Invalid,"Rounded fixture file allocation failed");return NULL;}
}
WolfCMStatus Wolf2009_CMModelReady(int handle,const char* actualName) {
    try {
        if(!actualName)return Error(WolfCM_Invalid,"Missing native collision model name");
        auto* existing=Lookup(handle);
        if(idStr::Cmp(actualName,fixtureName)!=0)
            return existing?Error(WolfCM_Invalid,"Rounded model handle reused without FreeMap"):WolfCM_NoSidecar;
        if(existing)return WolfCM_Complete;
        Need(State().enabled&&handle>=0&&State().generation,"Rounded fixture attachment is disabled/invalid");
        Need(State().nextFeature<=std::uint64_t(INT_MAX)-1,"Rounded source feature IDs exhausted");
        const auto* material=declManager->FindMaterial("wolf2009/collision/solid");
        Need(material&&material->GetState()!=DS_DEFAULTED&&(material->GetContentFlags()&CONTENTS_SOLID),
             "Authored rounded fixture solid material is unavailable");
        std::unique_ptr<Binding> binding(new Binding(material,State().generation,static_cast<int>(State().nextFeature)));
        State().bindings.emplace(handle,std::move(binding));State().nextFeature+=2;
        common->Printf("WOLF2009_ROUNDED_MODEL_READY handle=%d generation=%llu shapes=2\n",handle,
                       static_cast<unsigned long long>(State().generation));
        State().diagnostic.clear();return WolfCM_Complete;
    }catch(const std::exception& e){return Error(WolfCM_Invalid,e.what());}
    catch(...){return Error(WolfCM_Invalid,"Rounded collision attachment failed");}
}
void Wolf2009_CMFreeMap() {
    State().bindings.clear();
    if(State().generation)++State().generation; // zero means exhausted; never silently reuse IDs
}
WolfCMStatus Wolf2009_CMMetadata(int handle,idBounds& bounds,int& contents) {
    try {
        const auto* b=Lookup(handle);if(!b)return WolfCM_NoSidecar;
        bounds=b->bounds;contents=1;return WolfCM_Complete;
    }catch(const std::exception& e){return Error(WolfCM_Invalid,e.what());}
    catch(...){return Error(WolfCM_Invalid,"Rounded metadata failed");}
}
WolfCMStatus Wolf2009_CMTranslation(trace_s& out,const idVec3& start,const idVec3& end,const WolfCMQuery& q) {
    try {
        const auto* b=Lookup(q.handle);if(!b)return WolfCM_NoSidecar;
        if(start==end)return Wolf2009_CMPosition(out,start,q);
        Need(Finite(Vec(start))&&Finite(Vec(end)),"Rounded translation endpoints are invalid");
        QueryWorld world(*b,q);const auto actor=Actor(q);const FixtureFilter filter(q.contentMask);
        const auto result=world.index->Cast(actor,Pose(start,*q.trmAxis),Pose(end,*q.trmAxis),filter,Limits(q));
        const auto status=QueryStatus(result,"Translation");if(status!=WolfCM_Complete)return status;
        trace_s trace;EmptyTrace(trace,end,*q.trmAxis);
        if(result.status!=SupportCollisionQueryStatus::Separated) {
            Contact(trace,result,*b);trace.fraction=Fraction(ExactFraction(result));trace.endpos=start+trace.fraction*(end-start);
            if(trace.fraction>0) {
                const auto stop=FloatStopStatus(world,actor,filter,trace,q);
                if(stop!=WolfCM_Complete)return stop;
            }
        }
        out=trace;return WolfCM_Complete;
    }catch(const std::exception& e){return Error(WolfCM_Invalid,e.what());}
    catch(...){return Error(WolfCM_Invalid,"Rounded translation failed");}
}
WolfCMStatus Wolf2009_CMPosition(trace_s& out,const idVec3& start,const WolfCMQuery& q) {
    try {
        const auto* b=Lookup(q.handle);if(!b)return WolfCM_NoSidecar;
        Need(Finite(Vec(start)),"Rounded position is invalid");
        QueryWorld world(*b,q);const auto actor=Actor(q);const FixtureFilter filter(q.contentMask);
        const auto result=world.index->Overlap(actor,Pose(start,*q.trmAxis),filter,Limits(q,true));
        const auto status=QueryStatus(result,"Position");if(status!=WolfCM_Complete)return status;
        trace_s trace;EmptyTrace(trace,start,*q.trmAxis);
        if(result.status!=SupportCollisionQueryStatus::Separated) {Contact(trace,result,*b);trace.fraction=0;}
        out=trace;return WolfCM_Complete;
    }catch(const std::exception& e){return Error(WolfCM_Invalid,e.what());}
    catch(...){return Error(WolfCM_Invalid,"Rounded position test failed");}
}
WolfCMStatus Wolf2009_CMContacts(trace_s* out,int capacity,int& count,const idVec3& start,
                                const idVec6& dir,float depth,const WolfCMQuery& q) {
    count=0;
    try {
        const auto* b=Lookup(q.handle);if(!b)return WolfCM_NoSidecar;
        Need(capacity>=0&&capacity<=4096&&(!capacity||out),"Rounded contacts capacity/output is invalid");
        Need(Finite(Vec(start))&&std::isfinite(depth)&&depth>=0,"Rounded contact start/depth is invalid");
        for(int k=0;k<6;++k)Need(std::isfinite(dir[k]),"Nonfinite rounded contact direction");
        if(dir[3]!=0||dir[4]!=0||dir[5]!=0)
            return Error(WolfCM_Indeterminate,"Rotational contact collection is unsupported");
        if(depth==0||(dir[0]==0&&dir[1]==0&&dir[2]==0))
            return Error(WolfCM_Indeterminate,"Rounded contact collection requires a positive linear sweep");
        const idVec3 end=start+dir.SubVec3(0)*depth;
        Need(Finite(Vec(end)),"Rounded contact endpoint is outside the finite profile");
        QueryWorld world(*b,q);const auto actor=Actor(q);std::vector<trace_s> contacts;
        contacts.reserve(b->world.shapes.size());
        for(const auto& shape:b->world.shapes) {
            const FixtureFilter filter(q.contentMask,shape.source.occurrence);
            const auto result=world.index->Cast(actor,Pose(start,*q.trmAxis),Pose(end,*q.trmAxis),filter,Limits(q));
            const auto status=QueryStatus(result,"Contacts");if(status!=WolfCM_Complete)return status;
            if(result.status==SupportCollisionQueryStatus::Separated)continue;
            trace_s trace;EmptyTrace(trace,start,*q.trmAxis);Contact(trace,result,*b);
            trace.fraction=Fraction(ExactFraction(result));trace.endpos=start+trace.fraction*(end-start);
            contacts.push_back(trace);
        }
        const auto written=std::min(std::size_t(capacity),contacts.size());
        for(std::size_t i=0;i<written;++i)out[i]=contacts[i];
        count=static_cast<int>(written);return WolfCM_Complete;
    }catch(const std::exception& e){return Error(WolfCM_Invalid,e.what());}
    catch(...){return Error(WolfCM_Invalid,"Rounded contact collection failed");}
}
WolfCMStatus Wolf2009_CMRotation(trace_s& out,const idVec3& start,const idRotation& rotation,const WolfCMQuery& q) {
    try {
        const auto* b=Lookup(q.handle);if(!b)return WolfCM_NoSidecar;
        const double angle=rotation.GetAngle();
        Need(std::isfinite(angle)&&std::abs(angle)<=360&&Finite(Vec(start))&&Finite(Vec(rotation.GetOrigin())),
             "Rounded rotation is outside the finite one-turn profile");
        if(angle==0)return Wolf2009_CMPosition(out,start,q);
        const auto axis=Vec(rotation.GetVec());
        Need(Finite(axis)&&std::abs(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z-1)<=.00002,
             "Rounded rotation axis must be a finite unit vector");
        QueryWorld world(*b,q);const auto physicalActor=Actor(q);auto actor=physicalActor;
        CollisionRigidTransform canonicalActor;
        Need(CanonicalRoundedConvexPose(Pose(vec3_origin,*q.trmAxis),canonicalActor),
             "Rounded actor rotation frame is invalid");
        const auto offset=Sub(Vec(start),Vec(rotation.GetOrigin()));
        for(auto& point:actor.corePoints) {
            point=Add(Rotate(canonicalActor,point),offset);
            Need(Finite(point),"Rounded pivot-relative actor core exceeds the coordinate profile");
        }
        const int segments=static_cast<int>(std::ceil(std::abs(angle)/90));
        const FixtureFilter filter(q.contentMask);
        const auto limits=Limits(q);
        for(int i=0;i<segments;++i) {
            const idRotation fromRotation(rotation.GetOrigin(),rotation.GetVec(),static_cast<float>(angle*i/segments));
            const idRotation toRotation(rotation.GetOrigin(),rotation.GetVec(),static_cast<float>(angle*(i+1)/segments));
            // The shape is expressed around the actual pivot. Fixed origins
            // plus <=90-degree shortest-slerp segments preserve the arc and sign.
            const auto from=Pose(rotation.GetOrigin(),fromRotation.ToMat3());
            const auto to=Pose(rotation.GetOrigin(),toRotation.ToMat3());
            const auto result=world.index->Cast(actor,from,to,filter,limits);
            std::ostringstream operation;operation<<"Rotation angle="<<angle<<" segment="<<i;
            const auto status=QueryStatus(result,operation.str().c_str());if(status!=WolfCM_Complete)return status;
            if(result.status==SupportCollisionQueryStatus::Separated)continue;
            trace_s trace;EmptyTrace(trace,start,*q.trmAxis);Contact(trace,result,*b);
            trace.fraction=Fraction((i+ExactFraction(result))/segments);
            const idRotation completed(rotation.GetOrigin(),rotation.GetVec(),static_cast<float>(angle*trace.fraction));
            completed.RotatePoint(trace.endpos);
            trace.endAxis=q.trm?*q.trmAxis*completed.ToMat3():mat3_default;
            if(trace.fraction>0) {
                const auto stop=FloatStopStatus(world,physicalActor,filter,trace,q);
                if(stop!=WolfCM_Complete)return stop;
            }
            out=trace;return WolfCM_Complete;
        }
        trace_s trace;EmptyTrace(trace,start,*q.trmAxis);
        rotation.RotatePoint(trace.endpos);
        trace.endAxis=q.trm?*q.trmAxis*rotation.ToMat3():mat3_default;
        const auto stop=FloatStopStatus(world,physicalActor,filter,trace,q);
        if(stop!=WolfCM_Complete)return stop;
        out=trace;return WolfCM_Complete;
    }catch(const std::exception& e){return Error(WolfCM_Invalid,e.what());}
    catch(...){return Error(WolfCM_Invalid,"Rounded rotation failed");}
}
const char* Wolf2009_CMLastDiagnostic() { return State().diagnostic.c_str(); }

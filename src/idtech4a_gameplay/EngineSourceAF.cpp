// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "game/physics/Physics_AF.h"
#include "EngineSourceAF.h"
#include "SourceRagdoll.h"
#include "SourceEntityRuntime.h"
#include "SourceDeclarationSetRuntime.h"
#include "EngineAnimationSet.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "SourceActorBinding.h"

namespace {
const double Units=1.0/.0254;
void Need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
bool Refuse(idStr& reason,const char* why){reason=why;return false;}
bool Finite(const idVec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&v.LengthSqr()<=1e14f;}
bool Rigid(const idMat3& a){for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(!std::isfinite(a[i]*a[j])||idMath::Fabs(a[i]*a[j]-(i==j?1.f:0.f))>.00002f)return false;return idMath::Fabs(a.Determinant()-1)<.00002f;}
idMat3 Canonical(const idMat3& axis){Need(Rigid(axis),"Native source AF basis is improper/scaled");idQuat q=axis.ToQuat();q.Normalize();const auto a=q.ToMat3();Need(Rigid(a),"Native source AF canonical basis invalid");return a;}
idVec3 V(wolf2009::CollisionVec3 v){idVec3 a(float(v.x),float(v.y),float(v.z));Need(Finite(a),"Native source AF vector exceeds finite domain");return a;}
idMat3 A(const wolf2009::CollisionRigidTransform& t){idMat3 a;for(int i=0;i<3;++i)a[i]=V(t.axis[i]);return Canonical(a);}
struct Pose {idVec3 p;idMat3 a;Pose():p(vec3_origin),a(mat3_identity){}Pose(const idVec3& x,const idMat3& y):p(x),a(y){}};
Pose Compose(const Pose& parent,const Pose& child){return Pose(parent.p+child.p*parent.a,Canonical(child.a*parent.a));}
Pose Inverse(const Pose& pose){const auto a=pose.a.Transpose();return Pose(-pose.p*a,a);}
Pose SourcePose(const wolf2009::CollisionRigidTransform& pose){return Pose(V(pose.translation),A(pose));}
std::string Text(const wolf2009::ResolvedSourceDefinition& def,const char* key,const char* fallback=NULL){
    const auto* field=def.Find(key);if(!field){Need(fallback!=NULL,"Required source AF field is absent");return fallback;}
    wolf2009::SourceEntityValue v;v.declaration=field;std::string result;Need(v.Text(result)&&result.size()<=4096,"Source AF text field is unqualified/excessive");return result;
}
std::string Text(const wolf2009::SourceEntityPlan& plan,const char* key){std::string s;Need(plan.Find(key).Text(s)&&!s.empty()&&s.size()<=4096,"Required source AF entity text is absent/unqualified");return s;}
bool Flag(const wolf2009::ResolvedSourceDefinition& def,const char* key,bool fallback){
    const auto* field=def.Find(key);if(!field)return fallback;wolf2009::SourceEntityValue v;v.declaration=field;std::int32_t n=0;Need(v.Integer(n)&&(n==0||n==1),"Source AF flag is not literal zero/one");return n!=0;
}
std::vector<float> Numbers(const wolf2009::ResolvedSourceDefinition& def,const char* key,std::size_t n){
    wolf2009::SourceEntityValue v;v.declaration=def.Find(key);std::vector<float> values;Need(v.declaration&&v.Floats(values)&&values.size()==n,"Source AF numeric vector is absent/unqualified");return values;
}
bool Zero(wolf2009::CollisionVec3 a){return a.x==0&&a.y==0&&a.z==0;}
bool Identity(const wolf2009::CollisionRigidTransform& p){for(int i=0;i<3;++i)for(int k=0;k<3;++k){const auto& a=p.axis[i];const double c=k==0?a.x:k==1?a.y:a.z;if(c!=(i==k?1.:0.))return false;}return true;}
std::string FoldResource(std::string text){for(auto& c:text){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c+=32;}return text;}
std::pair<std::string,std::string> Resource(const std::string& path,const char* family){
    Need(path.size()<=8192&&path.find("wolf2009/")==0&&path.find("..") == std::string::npos&&path.find('\\')==std::string::npos,"Source AF resource path is outside typed VFS");
    const auto slash=path.find('/',9);Need(slash!=std::string::npos&&slash>9,"Source AF typed mount is absent");const auto typeEnd=path.find('/',slash+1);
    Need(typeEnd!=std::string::npos&&FoldResource(path.substr(slash+1,typeEnd-slash-1))==family&&typeEnd+1<path.size(),"Source AF resource family/name mismatch");
    return {FoldResource(path.substr(9,slash-9)),FoldResource(path.substr(typeEnd+1))};
}
void Close(idFile* file){if(file)fileSystem->CloseFile(file);}
struct Part {
    wolf2009::ResolvedSourceDefinition definition;
    std::string jointName;
    int joint=-1;
    bool fixed=false;
    Pose authoredOffset; // owning source facts; RestDeltaV1 deliberately uses rest binding
};
struct PreparedData {
    wolf2009::SourceRagdoll source;
    wolf2009::ResolvedSourceDefinition core;
    std::vector<Part> parts;
    std::vector<Pose> restJoints;
    wolf2009::SourceRagdollNativeBox hull;
    std::string resource,reference,nativeModel,restClip;
    std::size_t dynamicBody=0,fixedBody=0;
    int nativeContents=0,nativeMask=0;
};
float Outward(double value){const float f=float(value);Need(std::isfinite(f)&&f>0,"Native AF hull float is invalid");return double(f)<value?std::nextafter(f,(std::numeric_limits<float>::infinity)()):f;}
float OutwardNonnegative(double value){const float f=float(value);Need(std::isfinite(f)&&f>=0,"Native AF error bound is invalid");return double(f)<value?std::nextafter(f,(std::numeric_limits<float>::infinity)()):f;}
float RoundingExcess(const idVec3& expanded,wolf2009::CollisionVec3 core,double radius){
    double squared=0;for(int i=0;i<3;++i){const double c=i==0?core.x:i==1?core.y:core.z;const auto d=std::nextafter(double(expanded[i])-c,(std::numeric_limits<double>::infinity)());
        squared=std::nextafter(squared+std::nextafter(d*d,(std::numeric_limits<double>::infinity)()),(std::numeric_limits<double>::infinity)());}
    const auto length=std::nextafter(std::sqrt(squared),(std::numeric_limits<double>::infinity)());return OutwardNonnegative(std::nextafter(std::max(0.,length-radius),(std::numeric_limits<double>::infinity)()));
}
float HingeAngle(const idVec3& axis,const idVec3& reference,const idVec3& shaft,const wolf2009::SourceRagdollHinge& h){
    Need(Finite(axis)&&Finite(reference)&&Finite(shaft),"Native AF hinge angle pose invalid");
    float angle=float(std::atan2(double(axis*reference.Cross(shaft)),double(reference*shaft))*180/idMath::PI);
    if(angle<h.minimumAngle*180/idMath::PI-1e-3&&angle+360<=h.maximumAngle*180/idMath::PI+1e-3)angle+=360;return angle;
}
std::vector<Pose> JointMods(const PreparedData& data,const Pose& asset,const Pose& moving,const std::vector<Pose>& bodyToJoint,const std::vector<Pose>& fixedJoints){
    std::vector<Pose> mods;mods.reserve(data.parts.size());const Pose inverse=Inverse(asset);
    for(std::size_t i=0;i<data.parts.size();++i){const Pose world=data.parts[i].fixed?fixedJoints[i]:Compose(moving,bodyToJoint[i]);const Pose local=Compose(inverse,world);Need(Finite(local.p),"Native AF model-space joint position invalid");mods.push_back(local);}return mods;
}
std::shared_ptr<const PreparedData> Prepare(const WolfSourceDeclarationSet& set,const wolf2009::SourceEntityPlan& plan,const WolfSourceActorBinding& binding,const WolfSourceAFRequest& q){
    Need(q.profile==WolfSourceAF_FixedRootWorldHingeNativeConservativeV1,"Explicit native source AF compatibility profile required");
    Need(q.exactResourcePath&&q.authoredRagdollReference&&q.exactRootVariant&&q.restAnimation&&q.expectedBytes&&q.expectedBytes<=64u*1024u*1024u&&q.expectedFingerprint,"Explicit source AF resource/identity/rest selection required");
    Need(q.nativeContents>0&&q.nativeClipMask>0,"Explicit native AF contents/mask policy required");
    const std::string path=q.exactResourcePath;const auto resource=Resource(path,"hkxr");
    const auto modelResource=Resource(binding.SourcePlan().ResourceRequest().resources.modelResource,"modl");
    Need(resource.first==modelResource.first,"Source AF must use the explicitly selected model mount");
    auto referenceKey=FoldResource(q.authoredRagdollReference);Need(referenceKey.size()>4&&referenceKey.compare(referenceKey.size()-4,4,".hkx")==0,"Native AF V1 requires an explicit authored .HKX reference");referenceKey.resize(referenceKey.size()-4);
    Need(resource.second==referenceKey,"Native AF V1 authored .HKX reference does not match the explicit HKXR key");
    idStr validation;Need(binding.MatchesNative(validation),validation.c_str());
    std::unique_ptr<PreparedData> p(new PreparedData);p->resource=path;p->reference=q.authoredRagdollReference;p->restClip=q.restAnimation;p->nativeModel=binding.NativeModelDefinition();p->nativeContents=q.nativeContents;p->nativeMask=q.nativeClipMask;
    p->core=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,Text(plan,"def_afcore"),binding.SourcePlan().Context());
    Need(p->core.opaqueFacts.empty(),"Source AF core has opaque fields");Need(Text(p->core,"havok_ragdoll")==p->reference,"Source AF authored resource differs from explicit request");
    Need(Text(p->core,"check_ragdoll_match")==Text(plan,"ragdoll"),"Source AF ragdoll tag differs from core check_ragdoll_match");
    const auto prefix=Text(p->core,"prefix");Need(!prefix.empty(),"Source AF part prefix is empty");
    std::unique_ptr<idFile,void(*)(idFile*)> file(fileSystem->OpenFileRead(q.exactResourcePath),Close);Need(bool(file),"Exact source AF HKXR resource is unavailable");
    Need(file->Length()>0&&std::uint64_t(file->Length())==q.expectedBytes,"Source AF resource byte identity differs");
    std::vector<unsigned char> bytes(std::size_t(file->Length()));Need(file->Read(bytes.data(),int(bytes.size()))==int(bytes.size()),"Source AF resource read failed");
    p->source=wolf2009::DecodeSourceRagdoll(bytes,q.exactRootVariant);Need(p->source.sourceFingerprint==q.expectedFingerprint,"Source AF resource fingerprint differs");
    Need(p->source.complete&&p->source.bodies.size()==2&&p->source.hinges.size()==1&&p->source.skeleton.joints.size()==2&&p->source.boneToBody.size()==2,"Native AF V1 requires complete two-body/one-hinge source");
    Need(p->source.boneToBody[0]>=0&&p->source.boneToBody[1]>=0&&p->source.boneToBody[0]!=p->source.boneToBody[1],"Native AF V1 requires complete unique source bone mapping");
    const auto* model=Wolf2009_FindRegisteredSourceAnimationSet(p->nativeModel.c_str());Need(model&&model->NumJoints()==2,"Native AF V1 requires explicit complete two-joint visual model");
    const auto* rest=binding.FindClip(q.restAnimation);const auto* afReference=binding.SourcePlan().Model().Find("af_pose");
    Need(rest&&afReference&&afReference->hasLiteralAssetReference&&afReference->assetReference.text==rest->authoredReference,"Native AF rest clip does not match authored model af_pose asset");
    const int restNumber=model->GetSpecificAnim(q.restAnimation);const idAnim* animation=restNumber>0?model->GetAnim(restNumber):NULL;
    Need(animation&&animation->NumAnims()==1&&animation->MD5Anim(0)&&animation->MD5Anim(0)->IsSourceAnimationV2()&&animation->MD5Anim(0)->NumJoints()==2,"Native AF rest animation is not exact SourceV2 full pose");
    idJointQuat joints[2];const int indices[2]={0,1};animation->MD5Anim(0)->GetSingleFrame(0,joints,indices,2);
    for(int i=0;i<2;++i){Pose pose(joints[i].t,Canonical(joints[i].q.ToMat3()));Need(Finite(pose.p),"Native source AF rest joint is invalid");const int parent=model->GetJoint(i)->parentNum;Need(i?parent==0:parent==INVALID_JOINT,"Native AF V1 requires checked ordered two-joint tree");if(parent>=0)pose=Compose(p->restJoints[std::size_t(parent)],pose);p->restJoints.push_back(pose);}
    std::set<int> mapped;std::set<std::string> partNames;
    for(std::size_t i=0;i<2;++i){const auto& b=p->source.bodies[i];Part part;
        const auto name=prefix+"_"+b.name;partNames.insert(name);part.definition=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,name,binding.SourcePlan().Context());
        Need(part.definition.opaqueFacts.empty(),"Source AF part has opaque fields");part.jointName=Text(part.definition,"jointName");
        const auto* joint=model->FindJoint(part.jointName.c_str());Need(joint&&idStr::Cmp(model->GetJointName(joint->num),part.jointName.c_str())==0&&mapped.insert(joint->num).second,"Source AF part joint has no exact unique visual binding");part.joint=joint->num;
        part.fixed=Flag(part.definition,"keyframed",false);Need(Flag(part.definition,"useRagdollTranslation",false),"NativeRestDeltaV1 requires declared useRagdollTranslation1");
        const auto position=Numbers(part.definition,"jointOffsetPos",3),axis=Numbers(part.definition,"jointOffsetAxis",9);part.authoredOffset.p=idVec3(position[0],position[1],position[2])*float(Units);
        for(int r=0;r<3;++r)part.authoredOffset.a[r]=idVec3(axis[std::size_t(r*3)],axis[std::size_t(r*3+1)],axis[std::size_t(r*3+2)]);part.authoredOffset.a=Canonical(part.authoredOffset.a);
        const std::set<std::string> fields={"classname","jointname","keyframed","useragdolltranslation","jointoffsetpos","jointoffsetaxis","damagezone","damagescale","damagenonlethal","materialtype","clipmask"};
        for(const auto& f:part.definition.literalFields)Need(fields.count(f.first)!=0,"Source AF part has an unsupported field");
        Need(Zero(b.linearVelocity)&&Zero(b.angularVelocity),"Native AF V1 does not admit serialized initial velocity");
        Need(b.friction>=0&&b.restitution>=0&&b.restitution<=1&&b.linearDamping>=0&&b.angularDamping>=0&&b.linearDamping<=100&&b.angularDamping<=100,"Native AF source material/damping domain unsupported");
        if(part.fixed)p->fixedBody=i;else p->dynamicBody=i;p->parts.push_back(std::move(part));
    }
    Need(p->parts[0].fixed!=p->parts[1].fixed,"Native AF V1 requires exactly one fixed root and one dynamic body");
    Need(p->parts[p->fixedBody].joint==0&&p->parts[p->dynamicBody].joint==1,"Native AF V1 requires fixed root/dynamic child visual binding");
    // Source skeleton names and visual names intentionally differ. The source
    // bone -> body map and the resolved body -> part -> visual joint edges are
    // both mandatory; counts or coincident array order never establish identity.
    for(std::size_t bone=0;bone<p->source.skeleton.joints.size();++bone){const auto body=std::size_t(p->source.boneToBody[bone]);
        const auto& sourceJoint=p->source.skeleton.joints[bone];Need(sourceJoint.name==p->source.bodies[body].name,"Source AF skeleton name differs from its explicitly mapped body");
        const int parent=sourceJoint.parent;const int visualParent=model->GetJoint(p->parts[body].joint)->parentNum;
        const int mappedParent=parent<0?INVALID_JOINT:p->parts[std::size_t(p->source.boneToBody[std::size_t(parent)])].joint;
        Need(visualParent==mappedParent,"Source AF mapped source/visual parent topology differs");
    }
    for(const auto& field:p->core.literalFields){if(field.first=="classname"||field.first=="havok_ragdoll"||field.first=="check_ragdoll_match"||field.first=="prefix")continue;
        Need(field.first.find("def_")==0,"Source AF core has an unsupported field");const auto target=Text(p->core,field.first.c_str());const auto resolved=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,target,binding.SourcePlan().Context());
        bool selected=false;for(const auto& part:p->parts)if(resolved.selected.declarationName==part.definition.selected.declarationName)selected=true;Need(selected,"Source AF core def reference is outside selected parts");}
    const auto& h=p->source.hinges[0];Need((h.body[0]==p->dynamicBody&&h.body[1]==p->fixedBody),"Native AF V1 requires source hinge bodyA dynamic/bodyB fixed");
    Need(!h.motor.enabled&&(!h.motor.present||h.motor.type=="hkpPositionConstraintMotor"),"Native AF V1 refuses enabled/unqualified motors");
    Need(h.limitEnabled&&h.limitAxis==0&&std::abs(h.minimumAngle)<=2*idMath::PI&&std::abs(h.maximumAngle)<=2*idMath::PI&&h.maximumAngle>h.minimumAngle&&h.maximumAngle-h.minimumAngle<=idMath::PI+1e-6,"Native AF hinge limit profile unsupported");
    Need(!h.frictionEnabled||(h.frictionAxis==0&&h.frictionAxes==1&&h.maxFrictionTorque<=1e6),"Native AF hinge friction profile unsupported");
    const auto& dynamic=p->source.bodies[p->dynamicBody];Need(dynamic.inverseMass>0,"Native AF dynamic body has no finite mass");
    p->hull=wolf2009::BuildSourceRagdollNativeBox(dynamic,wolf2009::SourceRagdollHullPolicy::NativeConservativeHullV1);
    Need(Identity(p->hull.local),"Native AF V1 dynamic box cannot carry rotated local wrapper");
    const auto& center=dynamic.centerOfMassLocal;const auto& boxCenter=p->hull.local.translation;
    Need(center.x==boxCenter.x&&center.y==boxCenter.y&&center.z==boxCenter.z,"Native AF V1 source COM differs from the single-box center");
    return std::shared_ptr<const PreparedData>(p.release());
}

// GPL idAFConstraint_HingeFriction::Add supplies the Jacobian convention. Here
// a regular auxiliary row uses an explicit source torque cap, not its native
// coefficient*constraint-force rule. Evolve integrates force through dt, so its
// angular row bounds have units kg*gameUnits^2/s^2 = Nm*Units^2.
class SourceTorqueFriction : public idAFConstraint {
    idAFConstraint_Hinge* hinge;float torque;
  public:
    SourceTorqueFriction(idAFConstraint_Hinge* h,float cap):hinge(h),torque(cap){type=CONSTRAINT_FRICTION;name="sourceTorqueCap";body1=h->GetBody1();body2=h->GetBody2();InitSize(1);fl.allowPrimary=false;fl.frameConstraint=false;}
    void Evaluate(float) override {const idVec3 axis=hinge->GetAxis();J1.Zero();J1.SubVec6(0).SubVec3(1)=axis;if(body2){J2.Zero();J2.SubVec6(0).SubVec3(1)=-axis;}c1.Zero();c2.Zero();lo[0]=-torque;hi[0]=torque;}
    void ApplyFriction(float) override {}
};
}

struct WolfSourceAFPrepared::Impl {std::shared_ptr<const PreparedData> data;};
WolfSourceAFPrepared::WolfSourceAFPrepared(Impl* p):impl(p){}
WolfSourceAFPrepared::~WolfSourceAFPrepared(){delete impl;}
WolfSourceAFPrepared* Wolf2009_PrepareSourceAF(const WolfSourceDeclarationSet& set,const wolf2009::SourceEntityPlan& plan,const WolfSourceActorBinding& binding,const WolfSourceAFRequest& request,idStr& reason){
    reason.Clear();try{std::unique_ptr<WolfSourceAFPrepared::Impl> impl(new WolfSourceAFPrepared::Impl);impl->data=Prepare(set,plan,binding,request);auto* result=new WolfSourceAFPrepared(impl.get());impl.release();return result;}catch(const std::exception& e){reason=e.what();return NULL;}
}
struct WolfSourceAF::Impl {
    std::shared_ptr<const PreparedData> data;
    idAnimatedEntity* owner;
    idAnimator* animator;
    const idDeclModelDef* model;
    std::unique_ptr<idPhysics_AF> physics;
    idAFConstraint_Hinge* hinge=NULL;
    Pose assetWorld;
    idVec3 hingeAxisWorld,referenceShaftWorld,movingShaftLocal;
    std::vector<Pose> bodyToJoint,fixedJoints;
    WolfSourceAFState state;
    Impl(std::shared_ptr<const PreparedData> p,idAnimatedEntity& e):data(std::move(p)),owner(&e),animator(e.GetAnimator()),model(animator?animator->ModelDef():NULL){}
    bool Valid()const{return animator&&owner->GetAnimator()==animator&&animator->ModelDef()==model&&model&&idStr::Cmp(model->GetName(),data->nativeModel.c_str())==0&&animator->NumJoints()==2&&!animator->RemoveOrigin();}
    Pose JointWorld(int joint){idVec3 p;idMat3 a;Need(animator->GetJointTransform(jointHandle_t(joint),gameLocal.time,p,a),"Native AF cannot snapshot current visual joint");Need(Finite(p),"Native AF current visual joint invalid");return Compose(assetWorld,Pose(p,Canonical(a)));}
    void CommitMods(const std::vector<Pose>& mods){for(std::size_t i=0;i<mods.size();++i){const auto joint=jointHandle_t(data->parts[i].joint);animator->SetJointPos(joint,JOINTMOD_WORLD_OVERRIDE,mods[i].p);animator->SetJointAxis(joint,JOINTMOD_WORLD_OVERRIDE,mods[i].a);}state.jointMods=int(mods.size());owner->UpdateVisuals();}
    void Mods(){
        Need(state.active&&physics&&Valid(),"Native AF pose owner/model changed");
        const auto& p=*data;const auto* body=physics->GetBody(0);const Pose moving(body->GetWorldOrigin(),Canonical(body->GetWorldAxis()));
        CommitMods(JointMods(p,assetWorld,moving,bodyToJoint,fixedJoints));
    }
};
WolfSourceAF::WolfSourceAF(Impl* p):impl(p){}
WolfSourceAF::~WolfSourceAF(){if(impl){if(impl->state.active&&impl->owner->GetPhysics()==impl->physics.get()){impl->owner->SetPhysics(NULL);impl->owner->GetPhysics()->SetOrigin(impl->assetWorld.p);impl->owner->GetPhysics()->SetAxis(impl->assetWorld.a);impl->animator->ClearAllJoints();}delete impl;}}
WolfSourceAF* WolfSourceAF::Bind(const WolfSourceAFPrepared& prepared,idAnimatedEntity& owner,idStr& reason){reason.Clear();try{Need(prepared.impl&&prepared.impl->data,"Native AF prepared data absent");std::unique_ptr<Impl> impl(new Impl(prepared.impl->data,owner));Need(impl->Valid(),"Native AF owner differs from prepared visual model");auto* result=new WolfSourceAF(impl.get());impl.release();return result;}catch(const std::exception& e){reason=e.what();return NULL;}}
bool WolfSourceAF::Active()const{return impl&&impl->state.active;}
bool WolfSourceAF::Activate(idStr& reason){
    reason.Clear();try{Need(impl&&impl->Valid()&&!impl->state.active,"Native AF activation requires unchanged inactive bound owner");auto& s=*impl;const auto& p=*s.data;
        Need(!s.owner->GetBindMaster()&&gameLocal.time>=0,"Native AF V1 refuses bound owner or invalid clock");s.assetWorld=Pose(s.owner->GetPhysics()->GetOrigin(),Canonical(s.owner->GetPhysics()->GetAxis()));Need(Finite(s.assetWorld.p),"Native AF asset placement invalid");
        std::vector<Pose> joints;for(const auto& part:p.parts)joints.push_back(s.JointWorld(part.joint));
        std::vector<Pose> bodyPoses;for(std::size_t i=0;i<2;++i){const auto restJointWorld=Compose(s.assetWorld,p.restJoints[std::size_t(p.parts[i].joint)]);const auto delta=Compose(joints[i],Inverse(restJointWorld));bodyPoses.push_back(Compose(delta,Compose(s.assetWorld,SourcePose(p.source.bodies[i].pose))));}
        const auto& sourceBody=p.source.bodies[p.dynamicBody];const auto& sourceHinge=p.source.hinges[0];
        const Pose fixedFrame=Compose(bodyPoses[p.fixedBody],SourcePose(sourceHinge.localFrame[1]));const Pose movingFrame=Compose(bodyPoses[p.dynamicBody],SourcePose(sourceHinge.localFrame[0]));
        const idVec3 pivot=fixedFrame.p;const idVec3 hingeAxis=fixedFrame.a[0];Need(Finite(pivot)&&Rigid(fixedFrame.a),"Native AF world hinge frame invalid");
        Need((movingFrame.a[0]-hingeAxis).LengthSqr()<=.00002f*.00002f,"Native AF source hinge A/B axes disagree at handoff");
        const Pose com=Compose(bodyPoses[p.dynamicBody],Pose(V(sourceBody.centerOfMassLocal),mat3_identity));
        const idVec3 half(Outward(p.hull.halfExtents.x),Outward(p.hull.halfExtents.y),Outward(p.hull.halfExtents.z));idTraceModel trm(idBounds(-half,half));
        std::unique_ptr<idClipModel> clip(new idClipModel(trm));clip->SetContents(p.nativeContents);
        const double mass=1/sourceBody.inverseMass,volume=8.*double(half.x)*half.y*half.z;Need(std::isfinite(mass)&&mass>0&&mass<1e6&&std::isfinite(volume)&&volume>0,"Native AF hull mass/volume invalid");
        const float density=float(mass/volume);Need(std::isfinite(density)&&density>0,"Native AF hull density is not finite positive float");
        float nativeMass=0;idVec3 nativeCenter;idMat3 nativeInertia;clip->GetMassProperties(density,nativeMass,nativeCenter,nativeInertia);
        Need(std::isfinite(nativeMass)&&nativeMass>0&&std::isfinite(1/nativeMass)&&Finite(nativeCenter)&&nativeCenter.Compare(vec3_origin,1e-4f)&&nativeInertia.IsDiagonal(1e-3f),"Native AF hull mass properties require a fallback/COM shift");
        for(int r=0;r<3;++r)for(int c=0;c<3;++c)Need(std::isfinite(nativeInertia[r][c])&&(r!=c||(nativeInertia[r][c]>0&&std::isfinite(1/nativeInertia[r][c]))),"Native AF hull inertia is invalid");
        std::unique_ptr<idPhysics_AF> physics(new idPhysics_AF);physics->SetSelf(s.owner);physics->SetGravity(gameLocal.GetGravity());physics->SetSelfCollision(false);physics->SetComeToRest(true);physics->SetTimeScale(1);
        std::unique_ptr<idAFBody> body(new idAFBody(sourceBody.name.c_str(),clip.get(),density));clip.release();body->SetWorldOrigin(com.p);body->SetWorldAxis(com.a);body->SetClipMask(p.nativeMask);body->SetFriction(0,0,float(sourceBody.friction));body->SetBouncyness(float(sourceBody.restitution));physics->AddBody(body.get());body.release();
        std::unique_ptr<idAFConstraint_Hinge> hinge(new idAFConstraint_Hinge(sourceHinge.name.c_str(),physics->GetBody(0),NULL));hinge->SetAnchor(pivot);hinge->SetAxis(hingeAxis);
        const auto& reference=fixedFrame.a[1];const auto midpoint=wolf2009::SourceRagdollHingeLimitCenter({hingeAxis.x,hingeAxis.y,hingeAxis.z},{reference.x,reference.y,reference.z},sourceHinge.minimumAngle,sourceHinge.maximumAngle);
        const idVec3 centerAxis=V(midpoint);hinge->SetPhysics(physics.get());hinge->SetLimit(centerAxis,float((sourceHinge.maximumAngle-sourceHinge.minimumAngle)*180/idMath::PI),movingFrame.a[1]);
        auto* hingePtr=hinge.get();physics->AddConstraint(hinge.get());hinge.release();const double torque=sourceHinge.frictionEnabled?sourceHinge.maxFrictionTorque*Units*Units:0;
        Need(std::isfinite(torque)&&torque<=1e12,"Native AF torque cap invalid");if(torque>0){std::unique_ptr<SourceTorqueFriction> friction(new SourceTorqueFriction(hingePtr,float(torque)));physics->AddConstraint(friction.get());friction.release();}
        std::vector<Pose> bodyToJoint(2),fixedJoints=joints;bodyToJoint[p.dynamicBody]=Compose(Inverse(com),joints[p.dynamicBody]);
        // NativeRestDeltaV1 keeps the authored source fixed-body hinge as the
        // world pivot. SetAnchor derives the moving local anchor from that
        // pivot/current rest-delta body pose. The source disagreement is exposed.
        const float residual=(movingFrame.p-pivot).Length();
        Need(wolf2009::SourceRagdollNativeAnchorRetargetAllowed(residual),"NativeRestDeltaV1 anchor retarget exceeds explicit half-game-unit budget");
        WolfSourceAFState state;state.active=true;state.rootCollisionExcluded=true;state.sourceBodies=2;state.nativeBodies=1;state.hinges=1;state.mass=float(mass);state.frictionTorqueGameUnits=float(torque);state.handoffAnchorResidual=residual;
        state.maximumRoundingExcess=RoundingExcess(half,p.source.bodies[p.dynamicBody].boxes[0].halfExtents,p.hull.sourceRadius);state.hingeAngleDegrees=HingeAngle(hingeAxis,fixedFrame.a[1],movingFrame.a[1],sourceHinge);
        state.initialHingeAngleDegrees=state.hingeAngleDegrees;state.minimumHingeAngleDegrees=float(sourceHinge.minimumAngle*180/idMath::PI);state.maximumHingeAngleDegrees=float(sourceHinge.maximumAngle*180/idMath::PI);
        for(int i=0;i<3;++i){state.hingeAxisWorld[i]=hingeAxis[i];state.hingeAnchorWorld[i]=pivot[i];state.assetOriginWorld[i]=s.assetWorld.p[i];}
        const auto initialMods=JointMods(p,s.assetWorld,com,bodyToJoint,fixedJoints);const auto movingShaft=A(sourceHinge.localFrame[0])[1];
        s.bodyToJoint.swap(bodyToJoint);s.fixedJoints.swap(fixedJoints);s.physics=std::move(physics);s.hinge=hingePtr;s.state=state;
        s.hingeAxisWorld=hingeAxis;s.referenceShaftWorld=fixedFrame.a[1];s.movingShaftLocal=movingShaft;
        s.owner->SetPhysics(s.physics.get());s.animator->ClearAllAnims(gameLocal.time,0);s.owner->BecomeActive(TH_PHYSICS|TH_ANIMATE);s.CommitMods(initialMods);return true;
    }catch(const std::exception& e){reason=e.what();return false;}
}
bool WolfSourceAF::Step(int timeStepMs,idStr& reason){reason.Clear();try{Need(impl&&impl->state.active&&impl->Valid()&&impl->owner->GetPhysics()==impl->physics.get(),"Native AF Step owner/state changed");Need(timeStepMs>=0&&timeStepMs<=1000,"Native AF timestep outside explicit domain");
        const auto& b=impl->data->source.bodies[impl->data->dynamicBody];const double dt=timeStepMs*.001;
        impl->physics->GetBody(0)->SetFriction(float(1-std::exp(-b.linearDamping*dt)),float(1-std::exp(-b.angularDamping*dt)),float(b.friction));
        if(timeStepMs)impl->owner->RunPhysics();impl->Mods();impl->state.atRest=impl->physics->IsAtRest();
        const idVec3 shaft=impl->movingShaftLocal*impl->physics->GetBody(0)->GetWorldAxis();
        impl->state.hingeAngleDegrees=HingeAngle(impl->hingeAxisWorld,impl->referenceShaftWorld,shaft,impl->data->source.hinges[0]);
        const idVec3 anchor=impl->hinge->GetAnchor();Need(Finite(anchor),"Native AF moving hinge anchor invalid");for(int i=0;i<3;++i)impl->state.hingeAnchorWorld[i]=anchor[i];return true;
    }catch(const std::exception& e){reason=e.what();return false;}}
bool WolfSourceAF::ApplyJointMods(idStr& reason){reason.Clear();try{Need(impl,"Native AF is absent");impl->Mods();return true;}catch(const std::exception& e){reason=e.what();return false;}}
bool WolfSourceAF::GetPhysicsToVisualTransform(idVec3& origin,idMat3& axis)const{if(!impl||!impl->state.active||!impl->physics)return false;axis=impl->assetWorld.a*impl->physics->GetAxis().Transpose();origin=(impl->assetWorld.p-impl->physics->GetOrigin())*impl->assetWorld.a.Transpose();return true;}
bool WolfSourceAF::Query(WolfSourceAFState& out)const{if(!impl)return false;out=impl->state;return true;}

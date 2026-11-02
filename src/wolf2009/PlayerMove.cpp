// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerMove.h"
#include <algorithm>
#include <cmath>
#include <exception>
namespace wolf2009 {
namespace {
bool Finite(double x) { return std::isfinite(x); }
bool Finite(CollisionVec3 v) { return Finite(v.x)&&Finite(v.y)&&Finite(v.z); }
CollisionVec3 Add(CollisionVec3 a,CollisionVec3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
CollisionVec3 Scale(CollisionVec3 a,double s) {return {a.x*s,a.y*s,a.z*s};}
double Dot(CollisionVec3 a,CollisionVec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
CollisionVec3 Center(CollisionVec3 feet,const PlayerMoveParameters& p) {return Add(feet,{0,0,p.height*.5});}
CollisionVec3 Half(const PlayerMoveParameters& p) {return {p.halfWidth,p.halfWidth,p.height*.5};}
bool ParametersValid(const PlayerMoveParameters& p) {
    return Finite(p.halfWidth)&&p.halfWidth>0&&p.halfWidth<=10000&&
        Finite(p.height)&&p.height>0&&p.height<=10000&&
        Finite(p.speed)&&p.speed>=0&&p.speed<=10000&&
        Finite(p.gravity)&&p.gravity>=0&&p.gravity<=100000&&
        Finite(p.terminalFallSpeed)&&p.terminalFallSpeed>0&&p.terminalFallSpeed<=100000&&
        Finite(p.fixedSeconds)&&p.fixedSeconds>0&&p.fixedSeconds<=.05&&
        p.slideIterations>0&&p.slideIterations<=16;
}
PlayerMoveStatus SceneStatus(const StaticCollisionScene& scene) {
    if(!scene.coverageComplete)return PlayerMoveStatus::PartialGeometry;
    if(!scene.identityBodyPoseVerified)return PlayerMoveStatus::UnverifiedBodyPose;
    if(scene.otherRigidBodies)return PlayerMoveStatus::UnselectedBodies;
    if(scene.boxes.empty())return PlayerMoveStatus::NoGeometry;
    return PlayerMoveStatus::Initialized;
}
bool FeetValid(CollisionVec3 v) {
    return Finite(v)&&std::abs(v.x)<=1e10&&std::abs(v.y)<=1e10&&std::abs(v.z)<=1e10;
}
// A tiny downward query detects contact only, without accepting a positive gap
// as ground or changing the feet coordinate. It does not act as a ground snap.
bool Grounded(const StaticCollisionScene& scene,CollisionVec3 feet,const PlayerMoveParameters& p) {
    const StaticCollisionHit hit=SweepStaticBox(scene,Center(feet,p),Half(p),{0,0,-.0001});
    return hit.hit&&!hit.startedInside&&hit.fraction<=1e-6&&hit.normal.z>.99;
}
CollisionVec3 RemoveInward(CollisionVec3 v,CollisionVec3 normal) {
    const double into=Dot(v,normal);return into<0?Add(v,Scale(normal,-into)):v;
}
}
PlayerMoveResult InitializePlayerMove(const StaticCollisionScene& scene,CollisionVec3 feet,const PlayerMoveParameters& p) {
    PlayerMoveResult out;out.state.feet=feet;
    if(!ParametersValid(p)){out.status=PlayerMoveStatus::InvalidParameters;return out;}
    out.status=SceneStatus(scene);if(out.status!=PlayerMoveStatus::Initialized)return out;
    if(!FeetValid(feet)){out.status=PlayerMoveStatus::InvalidState;return out;}
    try {
        const StaticCollisionHit overlap=SweepStaticBox(scene,Center(feet,p),Half(p),{});
        if(overlap.startedInside){out.status=PlayerMoveStatus::OverlappingGeometry;return out;}
        out.state.grounded=Grounded(scene,feet,p);out.state.initialized=true;
    }catch(const std::exception&){out.status=PlayerMoveStatus::MalformedGeometry;}
    return out;
}
PlayerMoveResult StepPlayerMove(const StaticCollisionScene& scene,const PlayerMoveState& state,CollisionVec3 input,const PlayerMoveParameters& p) {
    PlayerMoveResult out;out.state=state;
    if(!ParametersValid(p)){out.status=PlayerMoveStatus::InvalidParameters;return out;}
    out.status=SceneStatus(scene);if(out.status!=PlayerMoveStatus::Initialized)return out;
    if(!state.initialized||!FeetValid(state.feet)||!Finite(state.velocity)||
       std::abs(state.velocity.x)>100000||std::abs(state.velocity.y)>100000||std::abs(state.velocity.z)>100000){out.status=PlayerMoveStatus::InvalidState;return out;}
    if(!Finite(input)||std::abs(input.x)>1000000||std::abs(input.y)>1000000||input.z!=0){out.status=PlayerMoveStatus::InvalidInput;return out;}
    try {
        if(SweepStaticBox(scene,Center(state.feet,p),Half(p),{}).startedInside){out.status=PlayerMoveStatus::OverlappingGeometry;return out;}
        PlayerMoveState next=state;
        const double length=std::sqrt(input.x*input.x+input.y*input.y);
        if(length>1){input.x/=length;input.y/=length;}
        next.velocity.x=input.x*p.speed;next.velocity.y=input.y*p.speed;
        next.velocity.z=(std::max)(-p.terminalFallSpeed,state.velocity.z-p.gravity*p.fixedSeconds);
        next.grounded=false;
        CollisionVec3 remaining=Scale(next.velocity,p.fixedSeconds);
        for(std::size_t i=0;i<p.slideIterations;++i){
            if(Dot(remaining,remaining)<=1e-20){remaining={};break;}
            const StaticCollisionHit hit=SweepStaticBox(scene,Center(next.feet,p),Half(p),remaining);
            if(hit.startedInside){out.status=PlayerMoveStatus::OverlappingGeometry;return out;}
            if(!hit.hit){next.feet=Add(next.feet,remaining);remaining={};break;}
            next.feet=Add(next.feet,Scale(remaining,hit.fraction));
            ++out.contacts;
            if(hit.normal.z>.99)next.grounded=true;
            remaining=RemoveInward(Scale(remaining,1-hit.fraction),hit.normal);
            next.velocity=RemoveInward(next.velocity,hit.normal);
        }
        out.movementClipped=Dot(remaining,remaining)>1e-20;
        if(!FeetValid(next.feet)||!Finite(next.velocity)){out.status=PlayerMoveStatus::InvalidState;return out;}
        // The complete scene has already been validated by the sweeps. A final
        // contact query also keeps ground status when gravity is set to zero.
        next.grounded=Grounded(scene,next.feet,p);
        out.state=next;out.status=PlayerMoveStatus::Advanced;
    }catch(const std::exception&){out.status=PlayerMoveStatus::MalformedGeometry;}
    return out;
}
const char* PlayerMoveStatusText(PlayerMoveStatus status) {
    switch(status){
    case PlayerMoveStatus::Initialized:return "Player initialized at authored feet without snapping";
    case PlayerMoveStatus::Advanced:return "Fixed prototype walking step completed";
    case PlayerMoveStatus::PartialGeometry:return "Movement refused: collision geometry is incomplete";
    case PlayerMoveStatus::UnverifiedBodyPose:return "Movement refused: body pose is unverified";
    case PlayerMoveStatus::UnselectedBodies:return "Movement refused: other rigid bodies were omitted";
    case PlayerMoveStatus::NoGeometry:return "Movement refused: no collision boxes";
    case PlayerMoveStatus::InvalidParameters:return "Movement refused: invalid prototype parameters";
    case PlayerMoveStatus::InvalidState:return "Movement refused: invalid player state";
    case PlayerMoveStatus::InvalidInput:return "Movement refused: expected finite world XY input";
    case PlayerMoveStatus::MalformedGeometry:return "Movement refused: malformed collision geometry";
    case PlayerMoveStatus::OverlappingGeometry:return "Movement refused: player box overlaps geometry";
    }return "Unknown player movement status";
}
}

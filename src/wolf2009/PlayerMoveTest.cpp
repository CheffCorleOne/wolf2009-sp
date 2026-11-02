// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerMove.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=1e-7){return std::abs(a-b)<=tolerance;}
StaticCollisionBox Box(CollisionVec3 lo,CollisionVec3 hi){StaticCollisionBox b;b.minimum=lo;b.maximum=hi;return b;}
StaticCollisionScene World(){StaticCollisionScene s;s.coverageComplete=true;s.identityBodyPoseVerified=true;s.boxes.push_back(Box({-1000,-1000,-20},{1000,1000,0}));return s;}
PlayerMoveState Init(const StaticCollisionScene& s,CollisionVec3 feet,const PlayerMoveParameters& p=PlayerMoveParameters()){
    const PlayerMoveResult r=InitializePlayerMove(s,feet,p);Need(r.Succeeded(),"Synthetic start refused");return r.state;
}
PlayerMoveState Steps(const StaticCollisionScene& s,PlayerMoveState state,CollisionVec3 input,int count,const PlayerMoveParameters& p=PlayerMoveParameters()){
    for(int i=0;i<count;++i){const PlayerMoveResult r=StepPlayerMove(s,state,input,p);Need(r.Succeeded(),"Synthetic step refused");state=r.state;}return state;
}
void Unchanged(const PlayerMoveResult& r,const PlayerMoveState& s){Need(r.state.feet.x==s.feet.x&&r.state.feet.y==s.feet.y&&r.state.feet.z==s.feet.z&&r.state.velocity.x==s.velocity.x&&r.state.velocity.y==s.velocity.y&&r.state.velocity.z==s.velocity.z&&r.state.initialized==s.initialized&&r.state.grounded==s.grounded,"Failed step changed input state");}
}
int RunPlayerMoveTests(){int passed=0;try{
    StaticCollisionScene world=World();PlayerMoveState start=Init(world,{0,0,0});
    Need(start.initialized&&start.grounded&&start.feet.z==0,"Floor start was moved or ungrounded");++passed;
    PlayerMoveState state=Steps(world,start,{},600);Need(state.grounded&&state.feet.z==0&&state.velocity.z==0,"Resting player drifted through floor");++passed;
    state=Steps(world,start,{1,0,0},60);Need(Near(state.feet.x,100)&&state.feet.z==0&&state.grounded,"Walking distance or floor contact incorrect");++passed;
    state=Steps(world,start,{1,1,0},60);Need(Near(state.feet.x,100/std::sqrt(2.0))&&Near(state.feet.y,state.feet.x),"Diagonal input exceeds speed");++passed;
    state=Steps(world,start,{.5,0,0},60);Need(Near(state.feet.x,50),"Analog magnitude discarded");++passed;
    state=Steps(world,Init(world,{0,0,100}),{},120);Need(state.grounded&&Near(state.feet.z,0)&&Near(state.velocity.z,0),"Gravity did not land on floor");++passed;
    PlayerMoveState air=Init(world,{0,0,100});const PlayerMoveResult fall=StepPlayerMove(world,air,{});Need(fall.Succeeded()&&Near(fall.state.velocity.z,-800.0/60)&&Near(fall.state.feet.z,100-800.0/3600)&&!fall.state.grounded,"Fixed gravity step incorrect");++passed;
    StaticCollisionScene wall=world;wall.boxes.push_back(Box({50,-1000,0},{50.01,1000,200}));
    state=Steps(wall,start,{1,0,0},180);Need(Near(state.feet.x,34)&&state.feet.z==0&&state.grounded,"Wall did not stop AABB at contact");++passed;
    state=Steps(wall,Init(wall,{34,0,0}),{1,1,0},60);Need(Near(state.feet.x,34)&&state.feet.y>60&&state.grounded,"Wall sliding failed at boundary");++passed;
    state=Steps(wall,Init(wall,{34,0,0}),{-1,0,0},10);Need(state.feet.x<20,"Movement away from wall blocked");++passed;
    StaticCollisionScene corner=wall;corner.boxes.push_back(Box({-1000,50,0},{1000,60,200}));
    state=Steps(corner,start,{1,1,0},180);Need(Near(state.feet.x,34)&&Near(state.feet.y,34)&&state.grounded,"Corner leaked or floor contact lost");++passed;
    PlayerMoveParameters fast;fast.speed=10000;state=Steps(wall,start,{1,0,0},1,fast);Need(Near(state.feet.x,34),"Sweep tunneled through thin wall at high speed");++passed;
    StaticCollisionScene ceiling=world;ceiling.boxes.push_back(Box({-100,-100,100},{100,100,110}));state=Init(ceiling,{0,0,0});state.velocity.z=3000;
    PlayerMoveResult r=StepPlayerMove(ceiling,state,{});Need(r.Succeeded()&&Near(r.state.feet.z,100-72)&&r.state.velocity.z==0&&!r.state.grounded,"Ceiling did not stop rising box");++passed;
    StaticCollisionScene ledge=World();ledge.boxes[0].maximum.x=0;state=Steps(ledge,Init(ledge,{-30,0,0}),{1,0,0},30);Need(state.feet.x>16&&state.feet.z<0&&!state.grounded,"Walking off support did not fall");++passed;
    r=InitializePlayerMove(wall,{40,0,0});Need(r.status==PlayerMoveStatus::OverlappingGeometry&&!r.state.initialized&&r.state.feet.x==40,"Overlapped start was adjusted or accepted");++passed;
    state=start;state.feet.x=40;r=StepPlayerMove(wall,state,{});Need(r.status==PlayerMoveStatus::OverlappingGeometry,"Mutated overlapped state accepted");Unchanged(r,state);++passed;
    StaticCollisionScene unknown=world;unknown.coverageComplete=false;r=StepPlayerMove(unknown,start,{1,0,0});Need(r.status==PlayerMoveStatus::PartialGeometry,"Partial scene treated as clear");Unchanged(r,start);
    Need(InitializePlayerMove(unknown,{0,0,0}).status==PlayerMoveStatus::PartialGeometry,"Initialized partial scene");++passed;
    unknown=world;unknown.identityBodyPoseVerified=false;r=StepPlayerMove(unknown,start,{});Need(r.status==PlayerMoveStatus::UnverifiedBodyPose,"Unverified scene accepted");Unchanged(r,start);
    unknown=world;unknown.otherRigidBodies=1;r=StepPlayerMove(unknown,start,{});Need(r.status==PlayerMoveStatus::UnselectedBodies,"Omitted bodies treated as clear");Unchanged(r,start);++passed;
    unknown=world;unknown.boxes.clear();r=StepPlayerMove(unknown,start,{});Need(r.status==PlayerMoveStatus::NoGeometry,"Empty scene treated as free");Unchanged(r,start);++passed;
    unknown=world;unknown.boxes[0].maximum.x=-2000;r=StepPlayerMove(unknown,start,{});Need(r.status==PlayerMoveStatus::MalformedGeometry,"Malformed bounds accepted");Unchanged(r,start);++passed;
    PlayerMoveParameters bad;bad.fixedSeconds=.1;r=StepPlayerMove(world,start,{},bad);Need(r.status==PlayerMoveStatus::InvalidParameters,"Variable/oversized step accepted");Unchanged(r,start);
    bad=PlayerMoveParameters();bad.height=0;Need(InitializePlayerMove(world,{0,0,0},bad).status==PlayerMoveStatus::InvalidParameters,"Zero height accepted");
    bad=PlayerMoveParameters();bad.slideIterations=0;Need(InitializePlayerMove(world,{0,0,0},bad).status==PlayerMoveStatus::InvalidParameters,"Zero iteration budget accepted");++passed;
    r=StepPlayerMove(world,start,{0,0,1});Need(r.status==PlayerMoveStatus::InvalidInput,"Vertical wish input accepted");Unchanged(r,start);
    r=StepPlayerMove(world,start,{(std::numeric_limits<double>::quiet_NaN)(),0,0});Need(r.status==PlayerMoveStatus::InvalidInput,"NaN input accepted");Unchanged(r,start);++passed;
    state=start;state.initialized=false;r=StepPlayerMove(world,state,{});Need(r.status==PlayerMoveStatus::InvalidState,"Uninitialized state accepted");Unchanged(r,state);
    Need(InitializePlayerMove(world,{0,0,(std::numeric_limits<double>::infinity)()}).status==PlayerMoveStatus::InvalidState,"Infinite start accepted");++passed;
    PlayerMoveParameters limited;limited.slideIterations=1;r=StepPlayerMove(wall,start,{1,0,0},limited);Need(r.Succeeded()&&r.movementClipped&&r.state.feet.x==0&&r.state.feet.z==0,"Iteration limit did not safely discard remainder");++passed;
    PlayerMoveParameters noGravity;noGravity.gravity=0;state=Steps(world,start,{1,0,0},60,noGravity);Need(state.grounded&&state.feet.z==0,"Zero-gravity tangent walk lost exact ground contact");++passed;
    std::cout<<"Player move self-test: "<<passed<<" passed, 0 failed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Player move self-test failed after "<<passed<<" checks: "<<e.what()<<'\n';return 1;}}
}

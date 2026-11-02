// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerSession.h"
#include "MapPhysics.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void Need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
wolf2009::PlayerStartCandidate Candidate(const std::string& name){wolf2009::PlayerStartCandidate c;c.placement.name=name;c.placement.hasOrigin=true;c.placement.origin[0]=0;c.placement.origin[1]=0;c.placement.origin[2]=0;c.kind=wolf2009::PlayerStartKind::Respawn;return c;}
wolf2009::StaticCollisionScene Collision(){wolf2009::StaticCollisionScene s;s.coverageComplete=s.identityBodyPoseVerified=true;wolf2009::StaticCollisionBox floor;floor.minimum={-200,-200,-10};floor.maximum={200,200,0};s.boxes.push_back(floor);wolf2009::StaticCollisionBox wall;wall.minimum={184,-200,0};wall.maximum={194,200,200};wall.sourceShapeOffset=1;s.boxes.push_back(wall);return s;}
int Tests(){
    unsigned passed=0;try{
        auto collision=Collision();const std::vector<wolf2009::PlayerStartCandidate> starts{Candidate("editor_respawn")};wolf2009android::PlayerSession session;std::string reason;
        Need(!session.Start(starts,"",&collision,true,reason)&&!session.Selected(),"Unnamed implicit start was accepted");++passed;
        Need(session.Start(starts,"editor_respawn",NULL,false,reason)&&session.State().feet.z==0&&session.Eye().z==64&&!session.Walking(),"Inspection altered authored origin or requires collision");++passed;
        Need(session.Advance(NULL,{1,0,0},1)==0&&session.State().feet.x==0,"Inspection fabricated player motion");++passed;
        auto partial=collision;partial.coverageComplete=false;Need(!session.Start(starts,"editor_respawn",&partial,true,reason)&&session.Selected()&&!session.Walking()&&session.State().feet.x==0,"Rejected walking replaced inspection session");++passed;
        Need(session.Start(starts,"editor_respawn",&collision,true,reason)&&session.Walking()&&session.State().grounded,"Complete-scene walking did not initialize on authored ground");++passed;
        Need(session.Advance(&collision,{1,0,0},100)==6&&std::abs(session.State().feet.x-10)<1e-7,"Frame cap permits unbounded physics catchup");++passed;
        for(unsigned i=0;i<180;i++)session.Advance(&collision,{1,0,0},1.0/60);
        Need(std::abs(session.State().feet.x-168)<.01&&std::abs(session.State().feet.z)<1e-7,"Player did not stop at expanded wall or fell through ground");++passed;
        auto duplicate=starts;duplicate.push_back(starts[0]);const double before=session.State().feet.x;Need(!session.Start(duplicate,"editor_respawn",&collision,true,reason)&&session.State().feet.x==before,"Ambiguous selection mutated active player");++passed;
        session.Pause();Need(session.Advance(&collision,{0,1,0},0)==0,"Pause retained an old fixed-step remainder");++passed;
        session.Clear();Need(!session.Selected()&&!session.Walking(),"Cleared session remains active");++passed;
        auto headed=starts;headed[0].placement.hasAngle=true;headed[0].placement.angle=0;Need(session.Start(headed,"editor_respawn",NULL,false,reason)&&session.HasInitialHeading()&&std::abs(-std::cos(session.InitialAwayYaw())-1)<1e-9,"Authored yaw0 does not look +X");++passed;
        headed[0].placement.angle=90;Need(session.Start(headed,"editor_respawn",NULL,false,reason)&&std::abs(-std::sin(session.InitialAwayYaw())-1)<1e-9&&std::abs(std::cos(session.InitialAwayYaw()))<1e-9,"Authored yaw90 does not look +Y");++passed;
        Need(session.Start(starts,"editor_respawn",NULL,false,reason)&&session.HasInitialHeading()&&std::abs(-std::cos(session.InitialAwayYaw())-1)<1e-9,"Absent angle and rotation did not use runtime default yaw0");++passed;
        headed[0].placement.hasAngle=false;headed[0].placement.hasRotation=true;Need(session.Start(headed,"editor_respawn",NULL,false,reason)&&!session.HasInitialHeading(),"Unverified rotation-only heading replaced manual view");++passed;
        std::cout<<"Android player session self-test: "<<passed<<" passed, 0 failed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Player session test failed after "<<passed<<": "<<e.what()<<'\n';return 1;}
}
int Audit(const char* path,const char* requested){
    std::ifstream input(path,std::ios::binary);Need(bool(input),"Cannot read MPK");const auto archive=wolf2009::Archive::Scan(input);std::string name;for(const auto& chunk:archive.Chunks())if(chunk.type=="PROC")for(const auto& e:chunk.entries)name=e.name;
    const auto starts=wolf2009::ReadMapPlayerStarts(input,archive,name);const auto collision=wolf2009::ReadMapStaticCollision(input,archive,name);
    wolf2009android::PlayerSession session;std::string reason;const bool accepted=session.Start(starts,requested,&collision,true,reason);
    std::cout<<"accepted="<<accepted<<" reason="<<reason<<" starts="<<starts.size()<<" boxes="<<collision.boxes.size()<<" complete="<<collision.coverageComplete<<'\n';
    if(accepted){for(unsigned i=0;i<180;i++)session.Advance(&collision,{1,0,0},1.0/60);std::cout<<"feet="<<session.State().feet.x<<","<<session.State().feet.y<<","<<session.State().feet.z<<" grounded="<<session.State().grounded<<'\n';}
    return 0;
}
}
int main(int argc,char** argv){try{return argc==3?Audit(argv[1],argv[2]):Tests();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

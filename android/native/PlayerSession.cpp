// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerSession.h"
#include <algorithm>
#include <cmath>
namespace wolf2009android {
bool PlayerSession::Start(const std::vector<wolf2009::PlayerStartCandidate>& candidates,
    const std::string& requestedName,const wolf2009::StaticCollisionScene* collision,
    bool walkingRequested,std::string& reason){
    if(requestedName.empty()){reason="Choose an explicitly named ENTS start";return false;}
    const wolf2009::PlayerStartSelection choice=wolf2009::SelectDiagnosticPlayerStart(candidates,requestedName);
    if(!choice.selected){reason=wolf2009::PlayerStartSelectionReasonText(choice.reason);return false;}
    const wolf2009::PlayerStartCandidate& candidate=candidates[choice.candidateIndex];
    if(!candidate.placement.hasOrigin){reason="Selected start has no authored origin";return false;}
    for(unsigned i=0;i<3;i++)if(!std::isfinite(candidate.placement.origin[i])||std::abs(candidate.placement.origin[i])>10000000){reason="Authored origin is outside supported coordinate range";return false;}
    wolf2009::PlayerMoveState next;next.feet={candidate.placement.origin[0],candidate.placement.origin[1],candidate.placement.origin[2]};
    if(walkingRequested){
        if(!collision){reason="No eligible HKXS collision resource";return false;}
        const wolf2009::PlayerMoveResult initialized=wolf2009::InitializePlayerMove(*collision,next.feet,parameters_);
        if(!initialized.Succeeded()){reason=wolf2009::PlayerMoveStatusText(initialized.status);return false;}
        next=initialized.state;
    }
    bool heading=false;double degrees=0;
    if(candidate.placement.hasAngle&&std::isfinite(candidate.placement.angle)){heading=true;degrees=candidate.placement.angle;}
    else if(!candidate.placement.hasAngle&&!candidate.placement.hasRotation)heading=true;
    // PC runtime confirms empty's absent angle/rotation defaults to yaw0.
    // Rotation-only headings remain manual. yaw0 looks +X, yaw90 looks +Y.
    candidate_=candidate;state_=next;selected_=true;walking_=walkingRequested;accumulator_=0;
    hasHeading_=heading;awayYaw_=(std::remainder(degrees,360.0)+180.0)*3.14159265358979323846/180.0;
    status_=walking_?"prototype AABB walking":"inspection only; movement disabled";reason=status_;return true;
}
bool PlayerSession::Pulse(const wolf2009::StaticCollisionScene* collision,wolf2009::CollisionVec3 input){
    if(!walking_||!collision)return false;
    const wolf2009::PlayerMoveResult step=wolf2009::StepPlayerMove(*collision,state_,input,parameters_);
    if(!step.Succeeded()){status_=wolf2009::PlayerMoveStatusText(step.status);walking_=false;accumulator_=0;return false;}
    state_=step.state;return true;
}
std::size_t PlayerSession::Advance(const wolf2009::StaticCollisionScene* collision,wolf2009::CollisionVec3 input,double elapsedSeconds){
    if(!walking_||!std::isfinite(elapsedSeconds)||elapsedSeconds<0){accumulator_=0;return 0;}
    accumulator_+=std::min(.1,elapsedSeconds);std::size_t steps=0;
    while(accumulator_+1e-10>=parameters_.fixedSeconds&&steps<6){
        accumulator_-=parameters_.fixedSeconds;
        if(!Pulse(collision,input))break;++steps;
    }
    return steps;
}
void PlayerSession::Clear(){selected_=walking_=hasHeading_=false;awayYaw_=0;candidate_=wolf2009::PlayerStartCandidate();state_=wolf2009::PlayerMoveState();accumulator_=0;status_.clear();}
}

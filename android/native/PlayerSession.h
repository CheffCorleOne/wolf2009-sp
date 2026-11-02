// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ANDROID_PLAYER_SESSION_H
#define WOLF2009_ANDROID_PLAYER_SESSION_H
#include "PlayerStart.h"
#include "PlayerMove.h"
#include <string>
#include <vector>
namespace wolf2009android {
// Render-thread owned session. Start is explicit, transactional and never snaps.
// The 64-unit eye offset is the original runtime's standing view-height default;
// bob, spawn offsets and the original character controller are not reproduced.
class PlayerSession {
public:
    bool Start(const std::vector<wolf2009::PlayerStartCandidate>& candidates,
               const std::string& requestedName,const wolf2009::StaticCollisionScene* collision,
               bool walkingRequested,std::string& reason);
    std::size_t Advance(const wolf2009::StaticCollisionScene* collision,
                        wolf2009::CollisionVec3 worldXYInput,double elapsedSeconds);
    bool Pulse(const wolf2009::StaticCollisionScene* collision,wolf2009::CollisionVec3 worldXYInput);
    void Pause(){accumulator_=0;}
    void Clear();
    bool Selected()const{return selected_;}
    bool Walking()const{return walking_;}
    bool HasInitialHeading()const{return hasHeading_;}
    double InitialAwayYaw()const{return awayYaw_;}
    const wolf2009::PlayerStartCandidate& Candidate()const{return candidate_;}
    const wolf2009::PlayerMoveState& State()const{return state_;}
    wolf2009::CollisionVec3 Eye()const{return {state_.feet.x,state_.feet.y,state_.feet.z+64};}
    const std::string& Status()const{return status_;}
private:
    bool selected_=false,walking_=false;
    bool hasHeading_=false;
    double awayYaw_=0;
    wolf2009::PlayerStartCandidate candidate_;
    wolf2009::PlayerMoveState state_;
    wolf2009::PlayerMoveParameters parameters_;
    double accumulator_=0;
    std::string status_;
};
}
#endif

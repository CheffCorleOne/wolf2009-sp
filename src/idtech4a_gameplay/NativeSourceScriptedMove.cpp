// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceScriptedMove.h"

namespace {
void Need(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
bool Live(idEntity* entity) {
    return entity && entity->entityNumber >= 0 && entity->entityNumber < MAX_GENTITIES &&
           gameLocal.entities[entity->entityNumber] == entity;
}
const char* const AasName = "aas28";
const unsigned MaxHops = 256;
}

int Wolf2009_SourceGoToTravelFlags(WolfSourceGoToTravelPolicy policy) {
    switch (policy) {
    case WolfSourceGoToTravelPolicy::WalkAirV1: return TFL_WALK | TFL_AIR;
    }
    return 0;
}

bool WolfSourceScriptedMove::Begin(const WolfSourceScriptedMoveRequest& request, idStr& reason) {
    reason.Clear();
    try {
        Need(Live(request.actor), "Scripted move actor is not a live native entity");
        // +0x46c: the previous scripted command is cleared before the new one.
        const unsigned begins = state_.begins + 1, stops = state_.stops;
        state_ = WolfSourceScriptedMoveState();
        state_.begins = begins;
        state_.stops = stops;
        actor_ = request.actor;
        goal_ = request.goal;
        face_ = request.face;
        const idVec3 origin = request.actor->GetPhysics()->GetOrigin();
        for (int i = 0; i < 3; ++i) state_.beginOrigin[i] = origin[i];
        // MoveToEntity(null) stops movement and fails; ScriptedMove then stops.
        Need(request.goal && Live(request.goal), "MoveToEntity has no goal entity; the scripted move stops");
        Need(!request.ignoreClusters, "GoToAndFaceTarget mIgnoreClusters has no qualified travel-flag mapping");
        const idVec3 goal = request.goal->GetPhysics()->GetOrigin();
        for (int i = 0; i < 3; ++i) state_.goal[i] = goal[i];
        float range = request.range > 0.f ? request.range : 8.f;
        float inner = range - 2.f;
        if (!(inner > 0.f)) inner = 8.f;
        const float dx = origin.x - goal.x, dy = origin.y - goal.y;
        if (inner * inner > dx * dx + dy * dy && 70.f > std::fabs(goal.z - origin.z)) {
            // The original posts `reached` on the goal and stops; this profile
            // has no native receiver for that event.
            state_.reachedOnBegin = true;
            throw std::runtime_error("Goal already within range: the original posts `reached`, which has no admitted native receiver");
        }
        // StartMove (+0x520, command 6): resets move status and arms motion.
        state_.moveCommand = 6;
        state_.moveStatus = 0;
        state_.moving = true;
        state_.travelFlags = Wolf2009_SourceGoToTravelFlags(request.travel);
        const idAAS* aas = gameLocal.GetAAS(AasName);
        if (!aas) {
            state_.moveStatus = 1;
            throw std::runtime_error("Native aas28 is not loaded; no route sensor exists for the move");
        }
        const idAASSettings* settings = aas->GetSettings();
        Need(settings && settings->numBoundingBoxes == 1, "Native aas28 hull settings differ from the qualified profile");
        const idBounds& hull = settings->boundingBoxes[0];
        idVec3 point = origin + idVec3(0, 0, 1);
        int area = aas->PointReachableAreaNum(point, hull, AREA_REACHABLE_WALK);
        const int goalArea = aas->PointReachableAreaNum(goal, hull, AREA_REACHABLE_WALK);
        state_.startArea = area;
        state_.goalArea = goalArea;
        if (area <= 0 || goalArea <= 0) {
            state_.moveStatus = 1;
            throw std::runtime_error("Actor or goal is outside the reachable native AAS areas");
        }
        std::set<int> visited;
        state_.routeAreas.push_back(area);
        while (area != goalArea) {
            int travelTime = 0;
            idReachability* reach = nullptr;
            if (state_.routeHops >= MaxHops || !visited.insert(area).second ||
                !aas->RouteToGoalArea(area, point, goalArea, state_.travelFlags, travelTime, &reach) || !reach ||
                reach->fromAreaNum != area || reach->toAreaNum <= 0 || reach->toAreaNum == area ||
                (reach->travelType & ~state_.travelFlags)) {
                state_.moveStatus = 1;
                throw std::runtime_error("Native AAS has no bounded route under the explicit travel policy");
            }
            if (!state_.routeHops) state_.routeTravelTime = travelTime;
            area = reach->toAreaNum;
            point = reach->end;
            state_.routeAreas.push_back(area);
            ++state_.routeHops;
        }
        // ScriptedMove success: scripted-move flag, style bits, face target.
        state_.scriptedMove = true;
        state_.styleBits = wolf2009::sourceai::MovementStyleBits(request.movement, request.ignoreClusters, request.endInCrouch);
        return true;
    } catch (const std::exception& error) {
        reason = error.what();
        state_.failure = error.what();
        state_.scriptedMove = state_.moving = false;
        return false;
    }
}

void WolfSourceScriptedMove::Stop() {
    if (state_.scriptedMove || state_.moving) ++state_.stops;
    state_.scriptedMove = state_.moving = false;
    state_.moveStartPending = false;
    state_.legs = WolfSourceLegState::Idle;
    goal_ = nullptr;
    face_ = nullptr;
}

bool WolfSourceScriptedMove::UpdateLegs() {
    if (!state_.moving || !Live(actor_.GetEntity())) return false;
    if (state_.legs != WolfSourceLegState::Idle) return false;
    state_.legs = WolfSourceLegState::Moving;
    state_.moveStartPending = true;
    ++state_.legTransitions;
    return true;
}

void WolfSourceScriptedMove::RefuseTranslation() {
    if (!state_.moving || state_.translationRefusals) return;
    ++state_.translationRefusals;
    state_.translationReason = "Move update +0x4dc translation (rvAASRouteSensor::HandleAnimTranslation) needs decoded source "
                               "root motion; not implemented, the actor keeps its origin";
}

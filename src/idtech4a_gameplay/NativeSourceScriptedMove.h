// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_SCRIPTED_MOVE_H
#define WOLF2009_NATIVE_SOURCE_SCRIPTED_MOVE_H
// SourceAIScriptedMoveV1: the native half of GoToAndFaceTarget (type 243) on
// the GPL idAAS router. It records the scripted move command exactly as the
// original's ScriptedMove 0x201ac2b0 / MoveToEntity 0x2019a720 / StartMove
// 0x2019d620 set it, plans the walk route on the loaded aas28 file, and runs
// the leg state transition that arms the move start (0x20166d00). Per-frame
// translation (move update +0x4dc, rvAASRouteSensor::HandleAnimTranslation)
// needs source root motion that is not decoded; it is an explicit refusal.
// Native game headers must precede this header.
#include "SourceAIScriptedMove.h"
#include <cstdint>
#include <string>
#include <vector>

// Explicit travel-flag policy for this profile. The original's GoTo travel
// mask is not recovered; this profile uses the mask that the native trainyard
// route check (run 016093956a80) qualified: TFL_WALK | TFL_AIR. mIgnoreClusters
// true is refused rather than mapped to another mask.
enum class WolfSourceGoToTravelPolicy { WalkAirV1 };
int Wolf2009_SourceGoToTravelFlags(WolfSourceGoToTravelPolicy);

struct WolfSourceScriptedMoveRequest {
    idEntity* actor = nullptr;
    idEntity* goal = nullptr;          // null: MoveToEntity fails, scripted stop
    idEntity* face = nullptr;
    float range = 0;                   // resolved mRange (distance kind)
    wolf2009::sourceai::Movement movement = wolf2009::sourceai::Movement::Walk;
    bool ignoreClusters = false, endInCrouch = false;
    WolfSourceGoToTravelPolicy travel = WolfSourceGoToTravelPolicy::WalkAirV1;
};

enum class WolfSourceLegState { Idle, Moving };

struct WolfSourceScriptedMoveState {
    bool scriptedMove = false;        // ai+0x954 bit 0x20
    bool moving = false;              // ai+0x11b0 bit 0x2
    bool reachedOnBegin = false;      // MoveToEntity posted `reached` instead
    std::uint8_t styleBits = 0;       // ai+0x11b5
    std::uint32_t moveCommand = 0;    // ai+0x11c0 (6: move to entity)
    std::uint32_t moveStatus = 0;     // ai+0x11c4 (0 after StartMove; 1: no route)
    int travelFlags = 0, startArea = 0, goalArea = 0, routeTravelTime = 0;
    unsigned routeHops = 0, begins = 0, stops = 0, legTransitions = 0, translationRefusals = 0;
    float goal[3] = {0, 0, 0}, beginOrigin[3] = {0, 0, 0};
    std::vector<int> routeAreas;
    WolfSourceLegState legs = WolfSourceLegState::Idle;
    bool moveStartPending = false;    // ai+0x11b7 bit 0x10
    std::string failure, translationReason;
};

class WolfSourceScriptedMove {
  public:
    // ScriptedMove + MoveToEntity + StartMove on the native AAS. False is the
    // original's scripted stop (+0x45c); the reason says which step refused.
    bool Begin(const WolfSourceScriptedMoveRequest&, idStr& reason);
    // The original Exit: +0x45c then +0x460(1). Clears the move and legs.
    void Stop();
    // One leg state step after the brain tick. Returns true on the frame the
    // legs leave idle for the move, which sets the pending move start.
    bool UpdateLegs();
    void ClearMoveStartPending() { state_.moveStartPending = false; }
    // Translation that has no admitted source is refused; recorded once per move.
    void RefuseTranslation(const std::string& reason);
    const WolfSourceScriptedMoveState& State() const { return state_; }
    bool Active() const { return state_.scriptedMove; }
    idEntity* Face() const { return face_.GetEntity(); }
  private:
    WolfSourceScriptedMoveState state_;
    idEntityPtr<idEntity> actor_, goal_, face_;
};
#endif

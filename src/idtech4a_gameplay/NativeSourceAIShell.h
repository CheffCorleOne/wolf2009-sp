// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_AI_SHELL_H
#define WOLF2009_NATIVE_SOURCE_AI_SHELL_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "NativeSourceBrainActions.h"
#include "NativeSourceAIScriptedAnim.h"

class idAnimatedEntity;
class idEntity;
class idStr;
class WolfNativeSourceRegistry;
class WolfSourceDeclarationSet;
class WolfSourceDirector;
namespace wolf2009 { struct BrainTaskUse; struct TrainDoorUseEvent; }

// Optional greeter network host (SourceAIShellGreeterRouteV1). The shell
// keeps the greeter's identity, wake, relay take, Use/Activate and pending-key
// latch; the host runs its brain network once per greeter Think and supplies
// the native body that the shell's head and accessories bind to. The host
// must outlive the shell. HostedThink throws to stop the greeter with a reason.
class WolfSourceAIBrainHost {
  public:
    virtual ~WolfSourceAIBrainHost() {}
    virtual idAnimatedEntity* HostedBody() const = 0;
    // The arrival relay event, observed before the shell delivers its Use.
    virtual void HostedRelay(const wolf2009::TrainDoorUseEvent&, idEntity* activator) = 0;
    virtual void HostedThink(wolf2009::BrainTaskUse& use) = 0;
};

struct WolfSourceAIActorState {
    std::string name, classname, brainResource, pendingKey, reason, poseReason, relayReason;
    std::string lastActivator;
    std::uint64_t levelGeneration = 0;
    unsigned spawnId = 0, brainTicks = 0, rootRefusals = 0, wakes = 0, activations = 0;
    unsigned useNotifications = 0, rejectedUses = 0, stopRoot = 0, stopNode = 0, stopType = 0;
    unsigned relayDeliveries = 0;
    int wakeTimeMs = -1, firstBrainTimeMs = -1, lastPlayerUseTimeMs = -1;
    bool hidden = false, noUnhideOnUse = false, brainBound = false, pendingUse = false;
    bool activated = false, thinking = false, hosted = false;
    // SourceInterrogatorChainV1 (held stepper over the actor's own network) and
    // the recovered idAI script fields that any network may write.
    std::string chainProfile, modelAnimationsReason;
    unsigned brainTransitions = 0, brainProbes = 0, holds = 0, heldSourceNode = 0;
    bool brainFailed = false;
    WolfSourceAIScriptState script;
    std::vector<WolfSourceBrainActionReceipt> actions;
    std::vector<WolfSourceVoiceStreamReceipt> voice;
    // SourceAIScriptedAnimV1: admitted body clips, the native animator state
    // and every Animate task of this actor's own network (node id, state).
    bool animationAdmitted = false;
    std::vector<std::string> admittedClips;
    WolfSourceAIScriptedAnimState animation;
    std::vector<std::pair<unsigned, WolfSourceAnimateTask>> animateTasks;
};
// Native owners of the execution scene's frame-command trigger targets.
struct WolfSourceAIExecutionTargetState {
    std::string name, classname, lastActivator, effect;
    unsigned activations = 0;
    int lastTimeMs = -1;
    bool live = false;
};

// Exact, map-owned trainyard actor profile. Events run on the game thread and
// always use the captured native entity generations. Stop receipts report
// selection at the first unsupported task, without entering it.
class WolfSourceAIShell {
  public:
    WolfSourceAIShell();
    ~WolfSourceAIShell();
    // Before Spawn: hand the greeter's network to an external host.
    bool HostGreeter(WolfSourceAIBrainHost*, idStr&);
    bool Spawn(const WolfSourceDeclarationSet&, const std::string& exactEntities,
               const std::string& materialArchive, std::uint64_t generation,
               WolfSourceDirector*, std::shared_ptr<WolfNativeSourceRegistry>, idStr&);
    bool Activate(const std::string& target, idEntity* activator, idStr&);
    bool Use(const std::string& target, const std::string& key, idEntity* activator, idStr&);
    bool Query(const std::string& name, WolfSourceAIActorState&, idStr&) const;
    unsigned Count() const;
    idEntity* Body(const std::string& name) const;
    idEntity* Head(const std::string& name) const;
    unsigned AccessoryCount(const std::string& name) const;
    idEntity* Accessory(const std::string& name, unsigned index) const;
    // SourceAIScriptedAnimV1 execution-scene targets and QA entry points. The
    // QA calls use the same native handlers as the network.
    bool ExecutionTarget(const std::string& name, WolfSourceAIExecutionTargetState&) const;
    idEntity* ExecutionTargetEntity(const std::string& name) const;
    bool QAAnimate(const std::string& actor, const std::string& clip, WolfSourceAnimateTask&, std::string& effect);
    bool QAAnimFinished(const std::string& actor, const std::string& clip, WolfSourceAnimateTask&);
    bool QAKill(const std::string& actor, const std::string& deathOverride, std::string& effect);
    WolfSourceAIShell(const WolfSourceAIShell&) = delete;
    WolfSourceAIShell& operator=(const WolfSourceAIShell&) = delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

int Wolf2009_LiveSourceAIShells();
int Wolf2009_LiveSourceAIActors();
int Wolf2009_LiveSourceAIBrains();
int Wolf2009_LiveSourceAIPendingUses();
int Wolf2009_LiveSourceAIExecutionTargets();
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAIN_DOOR_SIGNAL_H
#define WOLF2009_TRAIN_DOOR_SIGNAL_H

#include "LinearMover.h"
#include <cstdint>

namespace wolf2009 {

struct TrainDoorSignalLimits {
    LinearMoverLimits brain;
    std::size_t entities = 200000, entityNameBytes = 64u * 1024u * 1024u;
};

struct TrainDoorUnsupportedOutput {
    std::uint32_t outputToken = 0, destinationNodeId = 0, destinationNodeType = 0;
    std::uint32_t modeToken = 0, selectorToken = 0;
};

struct TrainDoorSignalPlan {
    std::string brainResource, signalEntityName, ownerEntityName, ownerClassname, useKey;
    std::string voiceDefinition, actorAnimation, targetEntityName, targetClassname;
    // A reference for preloading only. No Speak output is dispatched here.
    std::vector<std::string> referencedSpeechLines;
    std::uint32_t onUsedNodeId = 0, voiceNodeId = 0, actorStartNodeId = 0;
    // Observed serialization fields, not decoded bit masks or engine caller flags.
    std::uint32_t onUsedRawFlags = 0, voiceRawFlags = 0, actorRawFlags = 0;
    std::vector<TrainDoorUnsupportedOutput> unsupportedOutputs;
    std::size_t unexecutedNodeCount = 0;
};

// Exact observed OnUsed(type3)->ScriptedVOStreamIn(type19)->
// AnimOverrideMoveStart(type59) profile. Source is an explicit target_brainuse
// ENTS name, not an assumed elapsed-time event. Resolve its single target/key,
// the owner's brain and triggerStartMoveEnt from ENTS; never hardcode map names.
// Unknown selected-node flags/fields/branches fail. Actor outputs are reported
// without importing another node type's port/completion semantics.
TrainDoorSignalPlan DecodeTrainDoorSignalH1023(
    const std::vector<unsigned char> &data, const std::string &brainResource,
    const std::string &signalEntityName, const EntityDocument &entities,
    const TrainDoorSignalLimits &limits = TrainDoorSignalLimits());

struct TrainDoorUseEvent {
    std::string sourceEntityName, ownerEntityName, useKey;
    std::uint64_t levelGeneration = 0, eventId = 0;
};

struct TrainDoorCommand {
    enum Kind { None, PrepareVoiceResources, StartActorAnimation, ActivateTarget } kind = None;
    std::string ownerEntityName, resourceName, targetEntityName;
    std::uint64_t levelGeneration = 0, eventId = 0;
    std::uint32_t sourceNodeId = 0;
};

enum class TrainDoorSignalStatus {
    AwaitingUse,
    VoicePreparationRequested,
    ActorStartRequested,
    TargetActivationRequested,
    CompleteWithoutTarget,
    WrongEvent,
    AlreadyAccepted,
    WrongCallback,
    ResourceNotReady,
    ActorStartNotCommitted
};

struct TrainDoorSignalResult {
    TrainDoorSignalStatus status = TrainDoorSignalStatus::AwaitingUse;
    TrainDoorCommand command;
};

// Type19 readiness as seen by the signal runtime: the provider's once-only
// consumable Ready result. Native hosts adapt their whole-bank provider here.
class TrainDoorVoiceReadiness {
  public:
    virtual ~TrainDoorVoiceReadiness() {}
    // True at most once, only for an observed completed whole-bank request.
    virtual bool ConsumeReady() = 0;
};

class TrainDoorSignalRuntime {
  public:
    TrainDoorSignalRuntime(const TrainDoorSignalPlan &plan, std::uint64_t levelGeneration);
    TrainDoorSignalResult OnUsed(const TrainDoorUseEvent &event);
    // Native route: asks the provider itself. ResourceNotReady leaves the
    // runtime waiting at the type19 node; only a consumed Ready advances it.
    TrainDoorSignalResult ConsumeVoiceReadiness(const TrainDoorCommand &request, TrainDoorVoiceReadiness &provider);
    // Legacy portable/test-host entry with a caller-supplied boolean. The
    // native greeter route does not use it; a boolean is not readiness.
    TrainDoorSignalResult VoiceResourcesReady(const TrainDoorCommand &request, bool ready);
    // Call only after the actual actor-animation subsystem has committed this
    // requested start boundary. Construction, rendering, timers and frame sampling
    // cannot acknowledge it. False/wrong/replayed acknowledgements emit no action.
    TrainDoorSignalResult ActorStartCommitted(const TrainDoorCommand &request, bool committed);
    // No scene mutations occur here; each returned command is emitted once. The
    // level owner validates current entity lifetimes and applies it transactionally
    // (e.g. stage a runtime copy and commit it with the scene only on success).
    // Reset requires a fresh runtime with the new generation. Later BRAI outputs
    // remain unsupported even after target activation has been requested.
    TrainDoorSignalStatus Status() const { return status_; }

  private:
    TrainDoorSignalPlan plan_;
    std::uint64_t generation_ = 0, eventId_ = 0;
    TrainDoorSignalStatus status_ = TrainDoorSignalStatus::AwaitingUse;
    TrainDoorCommand pending_;
};

int RunTrainDoorSignalTests();

} // namespace wolf2009
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAIN_ARRIVAL_H
#define WOLF2009_TRAIN_ARRIVAL_H
#include "BrainStartup.h"
#include <map>
namespace wolf2009 {
struct TrainArrivalLimits {
    LinearMoverLimits brain;
    std::size_t registryEntities = 200000, registryStringBytes = 64u * 1024u * 1024u;
};
struct LevelEntityState {
    std::string name, classname, model, brain;
    bool alive = true, hidden = false, hiddenKnown = false, initiallyHidden = false;
    bool staticVisibilityProfileKnown = false;
};
// Minimal live level registry, initialized from explicit ENTS properties.
// Hide is known only for an observed numeric0/1 property; no entity-def default
// or visibility is invented. A shown func_static also needs the observed plain
// placement/material property profile, with no unhandled activation properties.
// Names are exact; case-fold collisions are rejected.
class LevelEntityRegistry {
  public:
    explicit LevelEntityRegistry(const EntityDocument &entities,
                                 const TrainArrivalLimits &limits = TrainArrivalLimits());
    const LevelEntityState *Find(const std::string &name) const;
    std::size_t Size() const {
        return entities_.size();
    }
    // For a staged level reset; references to old entries must be reacquired.
    void Swap(LevelEntityRegistry &other) noexcept {
        entities_.swap(other.entities_);
    }

  private:
    std::map<std::string, LevelEntityState> entities_;
    friend class TrainArrivalRuntime;
};
struct TrainArrivalTarget {
    std::uint32_t nodeId = 0;
    std::string name, classname, model;
};
struct TrainArrivalPlan {
    std::string brainResource;
    std::uint32_t onSpawnNodeId = 0, moveNodeId = 0;
    bool hasRemoval = false;
    TrainArrivalTarget removal;
    std::vector<TrainArrivalTarget> shows;
    std::uint32_t unsupportedNodeId = 0, unsupportedNodeType = 0;
    std::string unsupportedTargetName, unsupportedTargetClassname, unsupportedUseKey;
    std::size_t unexecutedNodeCount = 0;
};
// Observed profiles only: direct verified startup, then MoveToEnt completion ->
// optional RemoveTarget -> exactly three UseTarget of initially hidden
// func_static entities. The direct-three-show bypass was also causally tested.
// Follow graph IDs, never record order/names of a particular map. Unknown edge
// fields/branches/intermediate delays throw. Later actions are reported, not run.
TrainArrivalPlan DecodeTrainArrivalH1023(const std::vector<unsigned char> &data,
                                         const std::string &brainResource,
                                         const LevelEntityRegistry &entities,
                                         const TrainArrivalLimits &limits = TrainArrivalLimits());
struct TrainArrivalMoveStamp {
    std::string brainResource, ownerEntityName;
    std::uint32_t moveNodeId = 0;
    std::uint64_t levelGeneration = 0;
    bool spawnDelivered = false, active = false, arrived = false;
};
enum class TrainArrivalStatus {
    NoCompletion,
    Applied,
    AlreadyApplied,
    WrongSource,
    UnconfirmedSpawn,
    InvalidTransition,
    RegistryConflict
};
struct TrainArrivalChange {
    enum Kind { RemoveEntity, ShowEntity } kind = RemoveEntity;
    std::string name;
    std::uint32_t sourceNodeId = 0;
    bool wasAlive = false, wasHidden = false, alive = false, hidden = false;
};
struct TrainArrivalResult {
    TrainArrivalStatus status = TrainArrivalStatus::NoCompletion;
    std::vector<TrainArrivalChange> changes;
    std::uint32_t unsupportedNodeId = 0, unsupportedNodeType = 0;
};
class TrainArrivalRuntime {
  public:
    TrainArrivalRuntime(const TrainArrivalPlan &plan, const std::string &ownerEntityName,
                        std::uint64_t levelGeneration);
    // Called by the level owner ONLY after a successful movement commit, using
    // snapshots from that same owner. Requires active->arrived, an already
    // delivered spawn event and exact source/generation. No elapsed-time guess
    // or zero-length arrival at spawn generates completion. Registry edits and
    // returned renderer deltas are atomic; allocation/validation failure leaves
    // the registry and one-shot guard unchanged. Rebuild both on level reset.
    TrainArrivalResult ObserveCommittedMove(const TrainArrivalMoveStamp &before,
                                            const TrainArrivalMoveStamp &after,
                                            LevelEntityRegistry &entities);
    bool Applied() const {
        return applied_;
    }

  private:
    TrainArrivalPlan plan_;
    std::string owner_;
    std::uint64_t generation_;
    bool applied_ = false;
};
const char *TrainArrivalStatusText(TrainArrivalStatus status);
int RunTrainArrivalTests();
} // namespace wolf2009
#endif

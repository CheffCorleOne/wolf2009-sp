// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_TRAINYARD_EXIT_H
#define WOLF2009_NATIVE_SOURCE_TRAINYARD_EXIT_H
// Game-private C++11 owner for the measured trainyard exit slice
// (MeasuredTrainyardExitV1): native source trigger volumes from their HKXS
// boxes, shake_train's arming of trigger_once_1, the movetrain Wait ->
// RemoveTarget of the wagon-bound doorway blocker, and the multi-target
// rvTarget_BRAINUse that exit_train_trigger activates. Every unimplemented
// node, field or target is an explicit refusal receipt.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
class idEntity;class idStr;class WolfSourceDeclarationSet;class WolfSourceDirector;
class WolfSourceGreeterRoute;class WolfNativeSourceRegistry;struct WolfSourceEvent;
class WolfSourceAIShell;
namespace wolf2009 {struct SourceMapBodyPlan;}

enum class WolfSourceTriggerPhase { Dormant,Live,Fired };
struct WolfSourceTriggerState {
    std::string name;
    unsigned entityIndex=0,spawnId=0;
    WolfSourceTriggerPhase phase=WolfSourceTriggerPhase::Dormant;
    bool triggerFirst=false,alive=false;
    unsigned arms=0,fires=0,dormantTouches=0,lateTouches=0,refusedTouches=0,activationRefusals=0;
    int armTimeMs=-1,fireTimeMs=-1;
    std::string armActivator,fireActivator;
    // Decoded HKXS box in game units (identity body basis).
    float origin[3]={0,0,0},mins[3]={0,0,0},maxs[3]={0,0,0};
};
// One delivery attempt to one target. outcome: delivered, refused,
// implementation-absent, stale-skipped, target-expired.
struct WolfSourceTargetReceipt {
    std::string source,target,operation,outcome,reason,key,actor;
    int timeMs=-1;
};
enum class WolfSourceExitStage { Prepared,Waiting,Held,Failed };
struct WolfSourceTrainyardExitState {
    std::uint64_t levelGeneration=0;
    unsigned thinkFrames=0;
    // shake_train: 1 OnSpawn -> 30 Wait -> 19 UseTarget -> 31 (refused).
    WolfSourceExitStage shake=WolfSourceExitStage::Prepared;
    int shakeStartMs=-1,shakeUseMs=-1;std::int32_t shakeWaitMs=0;
    unsigned shakeUses=0,shakeStopNode=0,shakeStopType=0;
    std::string shakeUseOutcome,shakeStopReason,shakeWaitRaw;
    // movetrain: 6 UseTarget (director) -> 7 Wait -> 8 RemoveTarget -> 9 (refused).
    WolfSourceExitStage tail=WolfSourceExitStage::Prepared;
    int waitStartMs=-1,removalTimeMs=-1;std::int32_t waitMs=0;
    unsigned removals=0,consoleNode=0,consoleType=0;
    std::string waitRaw,removalOutcome,consoleCommand,consoleReason,tailReason;
    bool blockerAlive=false,blockerBound=false;
    float blockerMins[3]={0,0,0},blockerMaxs[3]={0,0,0};
    WolfSourceTriggerState exitTrigger,armedTrigger,dormantTrigger;
    unsigned relayActivations=0,greeterUses=0;
    std::vector<WolfSourceTargetReceipt> receipts;
    std::string relayName,relayKey,reason;
};
class WolfSourceTrainyardExit {
  public:
    ~WolfSourceTrainyardExit();
    // Map assembly after the selected greeter owner spawned and before wagon OnSpawn.
    bool Spawn(std::shared_ptr<WolfNativeSourceRegistry>,idEntity* wagon,WolfSourceGreeterRoute*,WolfSourceAIShell*,idStr&);
    // After the director started: delivers shake_train OnSpawn and starts Think.
    bool Start(WolfSourceDirector*,idStr&);
    bool Query(WolfSourceTrainyardExitState&,idStr&)const;
    // Effect-free registry admission of a (replayed) source event.
    int Inspect(const WolfSourceEvent&,idStr&)const;
    idEntity* Blocker()const;
    idEntity* Trigger(const std::string& name)const;
    idEntity* Relay()const;
    WolfSourceTrainyardExit(const WolfSourceTrainyardExit&)=delete;
    WolfSourceTrainyardExit& operator=(const WolfSourceTrainyardExit&)=delete;
    struct Impl;
  private:
    std::shared_ptr<Impl> impl;
    explicit WolfSourceTrainyardExit(std::shared_ptr<Impl>);
    friend std::unique_ptr<WolfSourceTrainyardExit> Wolf2009_PrepareSourceTrainyardExit(const WolfSourceDeclarationSet&,
        const wolf2009::SourceMapBodyPlan&,const std::vector<unsigned char>&,const std::vector<unsigned char>&,idStr&);
};
// Data-only: ENTS/decl/HKXS/BRAI qualification. plan must outlive the owner.
std::unique_ptr<WolfSourceTrainyardExit> Wolf2009_PrepareSourceTrainyardExit(const WolfSourceDeclarationSet&,
    const wolf2009::SourceMapBodyPlan&,const std::vector<unsigned char>& movetrainBrain,
    const std::vector<unsigned char>& shakeTrainBrain,idStr&);
const char* Wolf2009_SourceTriggerPhaseText(WolfSourceTriggerPhase);
const char* Wolf2009_SourceExitStageText(WolfSourceExitStage);
int Wolf2009_LiveSourceTrainyardExits();
int Wolf2009_LiveSourceTriggers();
#endif

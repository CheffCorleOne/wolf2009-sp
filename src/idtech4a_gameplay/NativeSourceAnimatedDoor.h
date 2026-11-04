// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_ANIMATED_DOOR_H
#define WOLF2009_NATIVE_SOURCE_ANIMATED_DOOR_H
// Game-private C++11 owner. No API8 export/layout change. Native game headers
// must precede this header in game translation units.
#include "Entities.h"
#include "SourceActorBinding.h"
#include "SourceEntityRuntime.h"
#include "TrainDoorSignal.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class idEntity;
class idAnimatedEntity;
class idStr;
class WolfSourceDeclarationSet;
class WolfSourceActorBinding;

enum WolfSourceAnimatedDoorPolicy {
    WolfSourceAnimatedDoor_Unspecified=0,
    // Qualified func_animate: fixed two-frame idle/end poses, one full-pose
    // source move, external actor-start activation, no collision/Use routing.
    WolfSourceAnimatedDoor_NativeControlledBoundSequenceV1=1
};
// Issued by the retained scripted-actor OWNER, never assembled from a caller's
// observed clip/time. Query before Start reports committed=false and no command.
// A committed record is immutable and comes only from actual native PlayOnce
// plus first-pose evaluation. Native identity is captured by that owner once;
// queries must not find/rebind a replacement by its name.
struct WolfSourceActorStartEvidence {
    idAnimatedEntity* nativeActor=NULL;
    std::string sourceName;
    std::uint64_t levelGeneration=0;
    unsigned nativeSpawnId=0;
    bool committed=false,actualPoseEvaluated=false;
    wolf2009::TrainDoorCommand command;
    unsigned commandSerial=0;
    int committedTimeMs=-1;
};
struct WolfSourceActorStartBinding {
    const void* context=NULL;
    // All callbacks run on the game thread; retain/release must not throw.
    // Preflight/query are effect-free and cannot acknowledge or start anything.
    void (*retain)(const void*)=NULL;
    void (*release)(const void*)=NULL;
    bool (*preflight)(const void*,idStr&)=NULL;
    bool (*query)(const void*,WolfSourceActorStartEvidence&,idStr&)=NULL;
};
struct WolfSourceAnimatedDoorRequest {
    WolfSourceAnimatedDoorPolicy policy=WolfSourceAnimatedDoor_Unspecified;
    std::string exactEntitiesPath;
    std::size_t entityIndex=0;
    std::uint64_t levelGeneration=0;
    bool expectedEntitiesIdentityKnown=false;
    std::uint64_t expectedEntitiesBytes=0,expectedEntitiesFingerprint=0;
    wolf2009::SourceDefinitionContext context;
    wolf2009::actor::ResourceSelection resources;
    wolf2009::TrainDoorSignalPlan signal;
    WolfSourceActorStartBinding actorStart;
};
struct WolfSourceAnimatedDoorFacts {
    wolf2009::SourceEntityIdentity identity;
    wolf2009::EntityRecord instance,parentInstance;
    wolf2009::ResolvedSourceDefinition defaults;
    std::size_t parentEntityIndex=0;
    std::string name,classname,model,bindName,initialClip,moveClip,endClip;
    int rawEditorFrame=0;
    // The authored fact is retained. Qualified constant end clips are held at
    // source frame0; this is not a general native func_animate cycle evaluator.
    bool sourceLoopLastAnimation=false,sourceNoClipModel=false;
};
enum WolfSourceAnimatedDoorPhase {
    WolfSourceAnimatedDoor_Prepared=0,WolfSourceAnimatedDoor_Idle=1,
    WolfSourceAnimatedDoor_Moving=2,WolfSourceAnimatedDoor_Completed=3
};
struct WolfSourceAnimatedDoorState {
    WolfSourceAnimatedDoorPhase phase=WolfSourceAnimatedDoor_Prepared;
    std::uint64_t levelGeneration=0,eventId=0;
    std::uint32_t sourceNodeId=0;
    unsigned activations=0,duplicateActivations=0,completions=0,consumedCompletions=0;
    unsigned doorCommandSerial=0,actorCommandSerial=0,actorNativeSpawnId=0;
    int activationTimeMs=-1,nativeEndTimeMs=-1,completionTimeMs=-1;
    int sourceFrames=0,sourceBones=0;
    double endpointPaletteError=0,endPoseTransitionError=0;
    bool nativeBound=false,sourceNoClipModel=false,qualifiedConstantEndPose=false;
};
struct WolfSourceAnimatedDoorCompletion {
    std::uint64_t levelGeneration=0,eventId=0;
    std::uint32_t sourceNodeId=0;
    std::string entityName,moveClip,endClip;
    int nativeEndTimeMs=-1,observedTimeMs=-1;
};

class WolfSourceAnimatedDoor {
  public:
    ~WolfSourceAnimatedDoor();
    const WolfSourceAnimatedDoorFacts& Facts() const;
    const WolfSourceActorBinding& Binding() const;
    idAnimatedEntity* Entity() const;
    // Parent must be the live exact named ENTS bind owner, at its authored
    // initial rigid world pose. Assembly precedes mover OnSpawn and actor Start.
    // The actor authority must already own its original spawned inactive actor.
    // Native Bind preserves the child's authored world pose; no preview/warp.
    bool Spawn(idEntity* exactBindParent,idStr& reason);
    // Synchronous with the retained owner's genuine actor PlayOnce commit.
    // Correlates full actor-owner Start command with this ActivateTarget,
    // original captured native SpawnId, actual pose/serial and game-clock start.
    // A caller-supplied ACK, observed clip/time or same-name replacement cannot
    // authorize activation. The request's retained owner is the sole authority.
    // Duplicate accepted event is acknowledged without replaying either clip.
    bool Activate(const wolf2009::TrainDoorCommand&,idStr& reason);
    // Call from ordinary map Think. The native controlled entity owns playback
    // and final hold; this owner observes it, verifies actual endpoint palette,
    // then advances the actual animator to the qualified constant end clip.
    bool Tick(idStr& reason);
    bool Query(WolfSourceAnimatedDoorState&,idStr& reason) const;
    bool TakeCompletion(bool& pending,WolfSourceAnimatedDoorCompletion&,idStr& reason);
    WolfSourceAnimatedDoor(const WolfSourceAnimatedDoor&)=delete;
    WolfSourceAnimatedDoor& operator=(const WolfSourceAnimatedDoor&)=delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceAnimatedDoor(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceAnimatedDoor> Wolf2009_PrepareSourceAnimatedDoor(
        const WolfSourceAnimatedDoorRequest&,const WolfSourceDeclarationSet&,idStr&);
};
// Exact ENTS bytes, selected compiled class/model closure, source geometry and
// all three original clips are qualified before allocating a native entity.
// Retains the readonly ENTS and existing actor capability/mount/model owners.
// Destroy this owner (native child first) before releasing its real parent,
// source declarations/material owner and selected resource mounts.
std::unique_ptr<WolfSourceAnimatedDoor> Wolf2009_PrepareSourceAnimatedDoor(
    const WolfSourceAnimatedDoorRequest&,const WolfSourceDeclarationSet&,idStr& reason);
#endif

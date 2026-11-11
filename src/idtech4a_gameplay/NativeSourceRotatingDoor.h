// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_ROTATING_DOOR_H
#define WOLF2009_NATIVE_SOURCE_ROTATING_DOOR_H
// SourceRotatingDoorOldV1, stage T1 (docs/TRAPDOOR.md section 4): the mover
// seam's second policy, WolfSourceMover_NativeRotatingDoorOldV1. One native
// rvRotatingDoor stand-in for trainyard ENTS 2136 sewer_trapdoor, derived from
// GPL idMover so it owns the same idPhysics_Parametric the original uses.
// Prepare reads and qualifies data only; Spawn allocates the entity at map
// assembly; DeliverPostSpawn performs the one DeferredPostSpawnActivateV1
// (self activate). Motion is angular extrapolation in the physics; the open
// and close completions are observed from the physics state in the door's
// own Think, never from a timer alone.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
class idEntity;class idStr;class WolfSourceDeclarationSet;class WolfNativeSourceRegistry;
namespace wolf2009 {struct SourceMapBodyPlan;}

struct WolfSourceRotatingDoorRequest {
    // Mover seam policy (NativeSourceMover.h) and the named door profile.
    int moverPolicy=0;
    std::string profile;                 // "SourceRotatingDoorOldV1"
    std::string exactEntities,sourceMapName;
};
struct WolfSourceRotatingDoorState {
    std::string profile,name,sourceClass,nativeModel,sndOpen,sndClose,triggerClosed,doorState,failure;
    std::string portalReceipt,soundReceipts,keyReceipts,contentsProfile,pushProfile,activateRefusal;
    unsigned entityIndex=0,renderTriangles=0,postSpawnDeliveries=0,duplicatePostSpawns=0,activations=0,ignoredActivations=0,
        moveStarts=0,openCompletions=0,closeCompletions=0,thinkFrames=0,openThinkFrames=0,velocityResets=0,genericActivateRefusals=0,
        triggerClosedAbsent=0,blockedFrames=0,physicsReachedAngEvents=0;
    std::uint64_t levelGeneration=0;
    int spawnTimeMs=-1,moveStartTimeMs=-1,moveEndTimeMs=-1,durationMs=0,openCompletionTimeMs=-1,closeCompletionTimeMs=-1,physicsEndTimeMs=-1;
    float origin[3]={0,0,0},localAngles[3]={0,0,0},target=0,delta=0,speed=0;
    float closedBounds[6]={0,0,0,0,0,0},bounds[6]={0,0,0,0,0,0},expectedOpenBounds[6]={0,0,0,0,0,0},modelBounds[6]={0,0,0,0,0,0};
    // Trace down through the hole at (-256 576) from z 330 to 290, at assembly
    // (before the post-spawn activation) and on Query.
    float closedTraceFraction=-1,traceFraction=-1;int closedTraceEntity=-1,traceEntity=-1;
    int entityNumber=-1,contents=0,clipMask=0;
    bool spawned=false,thinking=false,physicsActive=false,physicsAtRest=false,pusher=false,clipIsTraceModel=false;
};
class WolfSourceRotatingDoor {
  public:
    ~WolfSourceRotatingDoor();
    // Map assembly: allocates the native entity, records the closed clip
    // bounds and the hole trace, and binds the exact source name.
    bool Spawn(std::shared_ptr<WolfNativeSourceRegistry>,idStr&);
    // DeferredPostSpawnActivateV1: start_open -> one self activate. Repeated
    // deliveries are counted and refused without effect.
    bool DeliverPostSpawn(idStr&);
    bool Query(WolfSourceRotatingDoorState&,idStr&)const;
    idEntity* Entity()const;
    WolfSourceRotatingDoor(const WolfSourceRotatingDoor&)=delete;
    WolfSourceRotatingDoor& operator=(const WolfSourceRotatingDoor&)=delete;
  private:
    struct Impl;std::unique_ptr<Impl> impl;
    explicit WolfSourceRotatingDoor(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceRotatingDoor> Wolf2009_PrepareSourceRotatingDoor(const WolfSourceDeclarationSet&,
        const wolf2009::SourceMapBodyPlan&,const WolfSourceRotatingDoorRequest&,idStr&);
};
std::unique_ptr<WolfSourceRotatingDoor> Wolf2009_PrepareSourceRotatingDoor(const WolfSourceDeclarationSet&,
    const wolf2009::SourceMapBodyPlan&,const WolfSourceRotatingDoorRequest&,idStr&);
int Wolf2009_LiveSourceRotatingDoors();
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_MOVER_H
#define WOLF2009_NATIVE_SOURCE_MOVER_H
// Game-private C++98 seam. No new GetGameAPI/API8 entry or STL boundary.
class idEntity;
class idStr;
enum WolfSourceMoverPolicy {
    WolfSourceMover_Unspecified=0,
    WolfSourceMover_NativeDirectLinearPusherV1=1
};
enum WolfSourceMoverPlacementPolicy {
    WolfSourceMoverPlacement_Unspecified=0,
    WolfSourceMoverPlacement_AuthoredUnboundWorldV1=1,
    // The caller has independently established the world pose. Raw bind and
    // authored placement are retained; this does not resolve a source parent.
    WolfSourceMoverPlacement_CallerEstablishedWorldV1=2
};
enum WolfSourceMoverVisualPolicy {
    WolfSourceMoverVisual_Unspecified=0,
    WolfSourceMoverVisual_QualifiedModelV1=1,
    WolfSourceMoverVisual_NoneDiagnosticV1=2
};
enum WolfSourceMoverDataPolicy {
    WolfSourceMoverData_Unspecified=0,
    WolfSourceMoverData_TypedSourceVfsV1=1,
    // Only accepted in an explicitly compiled native QA build.
    WolfSourceMoverData_OwnLooseFixtureV1=2
};
struct WolfSourceMoverCollisionBinding {
    // A non-world native CM alias qualified for this exact source selection.
    // The owner must keep its source lease/registration alive. Native movers
    // use compound CM geometry as the pusher, not a fabricated trace/bounds box.
    const char* nativeModel;
    const void* context;
    void (*retain)(const void*);
    void (*release)(const void*);
    bool (*preflight)(const void*,idStr&);
    int contents,clipMask;
    WolfSourceMoverCollisionBinding():nativeModel(0),context(0),retain(0),release(0),
        preflight(0),contents(0),clipMask(0){}
};
struct WolfSourceMoverRequest {
    WolfSourceMoverPolicy policy;
    WolfSourceMoverPlacementPolicy placementPolicy;
    WolfSourceMoverVisualPolicy visualPolicy;
    WolfSourceMoverDataPolicy dataPolicy;
    const char *exactEntitiesPath,*exactBrainPath,*nativeVisualModel,*nativeSkin;
    unsigned entityIndex;
    float worldOrigin[3],worldAxis[9];
    unsigned long long expectedEntitiesBytes,expectedEntitiesFingerprint;
    unsigned long long expectedBrainBytes,expectedBrainFingerprint;
    WolfSourceMoverCollisionBinding collision;
    WolfSourceMoverRequest():policy(WolfSourceMover_Unspecified),
        placementPolicy(WolfSourceMoverPlacement_Unspecified),
        visualPolicy(WolfSourceMoverVisual_Unspecified),dataPolicy(WolfSourceMoverData_Unspecified),
        exactEntitiesPath(0),exactBrainPath(0),nativeVisualModel(0),nativeSkin(0),entityIndex(0),
        expectedEntitiesBytes(0),expectedEntitiesFingerprint(0),
        expectedBrainBytes(0),expectedBrainFingerprint(0){
        for(unsigned i=0;i<3;++i)worldOrigin[i]=0;
        for(unsigned i=0;i<9;++i)worldAxis[i]=(i%4==0)?1.0f:0.0f;
    }
};
class WolfSourceMoverPrepared {
  public:
    ~WolfSourceMoverPrepared();
  private:
    struct Impl;Impl* impl;
    explicit WolfSourceMoverPrepared(Impl*);
    WolfSourceMoverPrepared(const WolfSourceMoverPrepared&);
    WolfSourceMoverPrepared& operator=(const WolfSourceMoverPrepared&);
    friend WolfSourceMoverPrepared* Wolf2009_PrepareSourceMover(const WolfSourceMoverRequest&,idStr&);
    friend bool Wolf2009_SpawnPreparedSourceMover(const WolfSourceMoverPrepared&,idEntity*&,idStr&);
};
// Preparation reads and owns exact ENTS/BRAI data and resolves the existing
// qualified direct OnSpawn->MoveToEnt profile. It allocates no native entity
// and executes no spawn event. All collision/render bindings are preflighted.
WolfSourceMoverPrepared* Wolf2009_PrepareSourceMover(const WolfSourceMoverRequest&,idStr&);
bool Wolf2009_SpawnPreparedSourceMover(const WolfSourceMoverPrepared&,idEntity*&,idStr&);
bool Wolf2009_SpawnSourceMover(const WolfSourceMoverRequest&,idEntity*&,idStr&);
// Level owner delivers OnSpawn only after map assembly. Repeated deliveries
// are acknowledged without replaying movement or resetting its clock.
bool Wolf2009_DeliverSourceMoverOnSpawn(idEntity*,idStr&);
enum WolfSourceMoverPhase {
    WolfSourceMover_Prepared=0,WolfSourceMover_Moving=1,
    WolfSourceMover_Arrived=2,WolfSourceMover_Unsupported=3
};
struct WolfSourceMoverState {
    WolfSourceMoverPhase phase;
    const char *sourceName,*sourceClass,*sourceModel,*sourceBrain,*authoredBind,*authoredSkin,
        *destinationName,*nativeModel,*nativeSkin,*nativeCollisionModel,*failure;
    unsigned entityIndex,onSpawnNode,moveNode,unexecutedNodes,moveOutputRecords;
    unsigned spawnDeliveries,duplicateSpawnDeliveries,arrivals,consumedArrivals,blockedFrames;
    unsigned long long entitiesBytes,entitiesFingerprint,brainBytes,brainFingerprint;
    float origin[3],start[3],destination[3],velocity[3],sourceSpeed;
    int startTime,nativeEndTime,lastBlockingEntity;
    bool nativePusher,collisionIsTraceModel,callerEstablishedWorld,visualDiagnostic,
        typedSourceResources,moverWorldSweepImplemented,retailClockImplemented;
    WolfSourceMoverState():phase(WolfSourceMover_Prepared),sourceName(0),sourceClass(0),
        sourceModel(0),sourceBrain(0),authoredBind(0),authoredSkin(0),destinationName(0),nativeModel(0),nativeSkin(0),
        nativeCollisionModel(0),failure(0),entityIndex(0),onSpawnNode(0),moveNode(0),
        unexecutedNodes(0),moveOutputRecords(0),spawnDeliveries(0),duplicateSpawnDeliveries(0),
        arrivals(0),consumedArrivals(0),blockedFrames(0),entitiesBytes(0),entitiesFingerprint(0),
        brainBytes(0),brainFingerprint(0),sourceSpeed(0),startTime(-1),nativeEndTime(-1),
        lastBlockingEntity(-1),nativePusher(false),collisionIsTraceModel(false),
        callerEstablishedWorld(false),visualDiagnostic(false),typedSourceResources(false),
        moverWorldSweepImplemented(false),retailClockImplemented(false){
        for(unsigned i=0;i<3;++i)origin[i]=start[i]=destination[i]=velocity[i]=0;
    }
};
// String pointers remain valid until this native entity is removed.
bool Wolf2009_QuerySourceMover(const idEntity*,WolfSourceMoverState&,idStr&);
// Exactly-once arrival handoff. It reports the retained source move node;
// it does not execute or silently discard its later graph outputs.
bool Wolf2009_TakeSourceMoverArrival(idEntity*,bool& pending,unsigned& sourceMoveNode,idStr&);
bool Wolf2009_IsSourceMover(const idEntity*);
int Wolf2009_LiveSourceMovers();
#endif

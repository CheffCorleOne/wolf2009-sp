// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_GREETER_ROUTE_H
#define WOLF2009_NATIVE_SOURCE_GREETER_ROUTE_H
// Game-private C++11 owner for the measured trainyard relay -> greeter route.
// It consumes the arrival director's own-key relay request, delivers the
// greeter brain's exact OnUsed node, and enters type19 ScriptedVOStreamIn
// through WolfScriptedVoiceReadinessRequest. Only the provider's consumed Ready
// advances node 29, to node 35; node 10 locomotion and the door are refused.
#include "TrainDoorSignal.h"
#include "NativeSourceScriptedVoiceReadiness.h"
#include <cstdint>
#include <memory>
#include <string>
class idEntity;class idStr;class WolfSourceDeclarationSet;class WolfSourceDirector;
class WolfNativeSourceRegistry;class WolfSourceScriptedActor;class WolfSourceAnimatedDoor;

struct WolfSourceGreeterRouteRequest {
    // Only "MeasuredTrainyardGreeterRouteV1" is admitted.
    std::string profile;
    // Typed map ENTS of the running source map; BRAI/door resources use its mount.
    std::string exactEntities,sourceMapName,materialArchive;
    // Explicit greeter detail MODL and global SKEL resources. No mount search.
    std::string greeterModel,greeterRig;
    std::string voiceReadinessProfile,voiceLocale;
};
enum class WolfSourceGreeterRoutePhase {
    Prepared,WaitingForRelay,WaitingForVoiceReadiness,
    // Node 29 Ready consumed, node 35 entered; node 10 has no native handler.
    HeldAtUnsupportedNode,
    Refused,Cancelled,Failed
};
const char* Wolf2009_SourceGreeterRoutePhaseText(WolfSourceGreeterRoutePhase);
struct WolfSourceGreeterRouteState {
    WolfSourceGreeterRoutePhase phase=WolfSourceGreeterRoutePhase::Prepared;
    wolf2009::TrainDoorSignalStatus signal=wolf2009::TrainDoorSignalStatus::AwaitingUse;
    WolfScriptedVoiceReadinessStatus voice=WolfScriptedVoiceReadinessStatus::Pending;
    std::uint64_t levelGeneration=0,relayEventId=0;
    std::uint32_t onUsedNode=0,voiceNode=0,actorStartNode=0;
    unsigned thinkFrames=0,relayTakes=0,onUsedDeliveries=0,refusals=0;
    unsigned voiceRequests=0,voiceQueries=0,voiceReleases=0,cancellations=0;
    unsigned readyConsumptions=0,node35ProbeRefusals=0,bankMembers=0;
    std::uint32_t stopNode=0,stopNodeType=0;std::uint64_t bankGeneration=0;
    unsigned greeterSpawnId=0,doorSpawnId=0,wagonSpawnId=0;
    int relayTimeMs=-1,onUsedTimeMs=-1,voiceRequestTimeMs=-1,readyTimeMs=-1;
    // Raw source facts: incoming upstream key, relay's own forwarded key and
    // the greeter OnUsed key are retained separately; none is case-folded.
    std::string relayName,greeterName,doorName,incomingUseKey,forwardedUseKey,onUsedKey;
    std::string voiceDefinition,actorAnimation,voiceProfile,voiceReason,reason,storedMoveStartAnimation,stopReason;
    bool actorSpawned=false,doorBound=false,node35Entered=false,voiceBacked=false;
    // idAI receiver Use (static 0x201ac870): CanUseKey over the greeter's
    // OnUsed roots, then the single pending-key latch, then base Use/activate.
    unsigned keyedUses=0,keyedUseRefusals=0,activateRefusals=0,playerBookkeepingOmissions=0;
    unsigned latchedRoot=0,latchedDestination=0,latchTransitionRefusals=0;
    int latchTimeMs=-1;
    bool latchPending=false,latchConsumed=false;
    std::string latchedKey,latchActorName,latchTransitionReason,activateReason;
};
// One idAI Use delivery as the relay observed it. canUseKey/latched report
// the established network notify; activateRefused and the omitted local-player
// bookkeeping are explicit refusals, not performed effects.
struct WolfSourceAIUseReceipt {
    std::string key,reason;
    unsigned root=0,destination=0;
    bool canUseKey=false,latched=false,activateRefused=false,playerBookkeepingOmitted=false;
};
class WolfSourceGreeterRoute {
  public:
    ~WolfSourceGreeterRoute();
    // Map assembly after the source wagon exists and before its OnSpawn: the
    // inactive greeter and the door bound at the wagon's authored pose.
    bool Spawn(std::shared_ptr<WolfNativeSourceRegistry>,idEntity* wagon,idStr&);
    // Starts the ordinary Think owner. The director must outlive this route.
    bool Start(WolfSourceDirector*,idStr&);
    // Effect-free observation; validates owned native lifetimes.
    bool Query(WolfSourceGreeterRouteState&,idStr&)const;
    // Effect-free admission predicate shared by the Think delivery path.
    bool Inspect(const wolf2009::TrainDoorUseEvent&,idEntity* originalActor,idStr&)const;
    // Ends the route before completion and releases any readiness request.
    bool Cancel(idStr&);
    // Keyed Use on the greeter (idAI receiver). noUnhideOnUse is absent from
    // the qualified definition and instance, so the established path notifies
    // the brain: CanUseKey over the OnUsed roots, then the pending latch. The
    // next Think evaluates the latched root once; its destination has no
    // native handler and is refused while the latch stays pending. idAI
    // activate/unhide is refused explicitly. False when the greeter owner or
    // route is terminal/stale or no root accepts the key.
    bool ReceiveKeyedUse(const std::string& key,idEntity* actor,WolfSourceAIUseReceipt&,idStr&);
    WolfSourceScriptedActor* Actor()const;
    WolfSourceAnimatedDoor* Door()const;
    WolfSourceGreeterRoute(const WolfSourceGreeterRoute&)=delete;
    WolfSourceGreeterRoute& operator=(const WolfSourceGreeterRoute&)=delete;
  private:
    struct Impl;std::shared_ptr<Impl> impl;
    explicit WolfSourceGreeterRoute(std::shared_ptr<Impl>);
    friend std::unique_ptr<WolfSourceGreeterRoute> Wolf2009_PrepareSourceGreeterRoute(
        const WolfSourceDeclarationSet&,const WolfSourceGreeterRouteRequest&,idStr&);
};
// Data-only preparation: exact ENTS/BRAI identity, decoded signal profile and
// actor/door requests. Creates no native entity. The declaration set must
// outlive the route.
std::unique_ptr<WolfSourceGreeterRoute> Wolf2009_PrepareSourceGreeterRoute(
    const WolfSourceDeclarationSet&,const WolfSourceGreeterRouteRequest&,idStr&);
int Wolf2009_LiveSourceGreeterRoutes();
#endif

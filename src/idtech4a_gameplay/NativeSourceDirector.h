// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_DIRECTOR_H
#define WOLF2009_NATIVE_SOURCE_DIRECTOR_H
#include "NativeSourceRegistry.h"
#include "TrainDoorSignal.h"
#include <vector>
class idEntity;class idStr;
enum class WolfSourceDirectorPhase { Prepared,WaitingForArrival,PartialArrival,Unsupported };
struct WolfSourceDirectorRequest {
    std::string profile,brainResource,ownerName;
    std::uint64_t entitiesBytes=0,entitiesFingerprint=0;
    std::vector<unsigned char> brainBytes;
};
struct WolfSourceDirectorState {
    WolfSourceDirectorPhase phase=WolfSourceDirectorPhase::Prepared;
    std::uint64_t levelGeneration=0,arrivalEventId=0;
    unsigned thinkFrames=0,arrivals=0,shown=0,removed=0,relayEvents=0;
    unsigned unsupportedNode=0,unsupportedType=0;
    WolfSourceDelivery removal=WolfSourceDelivery::Ready,relay=WolfSourceDelivery::Ready;
    std::string rawIncomingUseKey,authoredRelayUseKey,forwardedRelayUseKey,reason;
};
// Ordinary native Think owner. It observes the actual mover's committed
// transition, consumes its arrival once and delivers exact source node events.
// The explicit measured profile permits the causally tested show prefix when
// quake has no implementation, reporting a PARTIAL arrival and zero removals.
// It does not implement the later Wait/cleanup/console or actor graph branches.
class WolfSourceDirector {
  public:
    ~WolfSourceDirector();
    bool BindRelay(idEntity*,idStr&);
    bool Start(idEntity* mover,idStr&);
    bool Query(WolfSourceDirectorState&,idStr&)const;
    // Qualified own-key relay receipt only, never an actor-start ACK. The
    // consumer must validate its captured native greeter generation before
    // delivering OnUsed. A returned request does not instantiate that target.
    bool TakeRelayRequest(wolf2009::TrainDoorUseEvent&,bool& pending,idStr&);
    // Original incoming actor remains generation checked until handoff. This
    // pointer is for synchronous engine-thread delivery, not delayed storage.
    bool TakeRelayRequest(wolf2009::TrainDoorUseEvent&,idEntity*& originalActor,bool& pending,idStr&);
  private:
    struct Impl;std::shared_ptr<Impl> impl;
    explicit WolfSourceDirector(std::shared_ptr<Impl>);
    friend std::unique_ptr<WolfSourceDirector> Wolf2009_PrepareSourceDirector(
        const WolfSourceDirectorRequest&,const wolf2009::EntityDocument&,
        std::shared_ptr<WolfNativeSourceRegistry>,idStr&);
};
std::unique_ptr<WolfSourceDirector> Wolf2009_PrepareSourceDirector(
    const WolfSourceDirectorRequest&,const wolf2009::EntityDocument&,
    std::shared_ptr<WolfNativeSourceRegistry>,idStr&);
int Wolf2009_LiveSourceDirectors();
#endif

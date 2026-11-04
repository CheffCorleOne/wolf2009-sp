// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceDirectorChecks.h"
#include "NativeSourceRegistry.h"
#include "NativeSourceDirector.h"
#include "NativeSourceMap.h"
#include "NativeSourceMover.h"
#include <memory>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
namespace {
unsigned checks=0;
void Need(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){Need(ok,reason.IsEmpty()?fallback:reason.c_str());}
wolf2009::EntityRecord Record(const char* name,const char* classname){wolf2009::EntityRecord out;
    wolf2009::EntityProperty a,b;a.key="name";a.text=name;b.key="classname";b.text=classname;out.properties={a,b};return out;}
struct Own {
    std::vector<idEntityPtr<idEntity>> entities;
    ~Own(){for(auto& entity:entities)if(entity.GetEntity())delete entity.GetEntity();}
    idEntity* Spawn(const char* name,int slot=-1){Need(!gameLocal.FindEntity(name),"Own source-registry native name already occupied");
        idDict args;args.Set("classname","wolf2009_source_registry_own");args.Set("name",name);args.Set("noclipmodel","1");args.Set("networkSync","0");
        if(slot>=0)args.SetInt("spawn_entnum",slot);std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idEntity::Type,&args));Need(bool(entity),"Own source-registry Spawn failed");
        idEntityPtr<idEntity> retained;retained=entity.get();entities.push_back(retained);return entity.release();}
};
class ShowReceiver : public WolfSourceUseReceiver {
  public:
    unsigned uses=0;bool throws=false;
    bool Check(idEntity*,idEntity*,const WolfSourceEvent& event,idStr& reason)const override{if(event.useKey!="exact_key"){reason="Own receiver exact key rejected";return false;}reason.Clear();return true;}
    bool Use(idEntity* target,idEntity*,const WolfSourceEvent&,idStr&)override{++uses;if(throws)throw std::runtime_error("Own receiver intentional exception");target->Show();return true;}
};
class PreflightReceiver : public WolfSourceUseReceiver {
  public:
    WolfNativeSourceRegistry* registry=NULL;bool reenter=true,throws=false;
    mutable bool inside=false;mutable unsigned checks=0;
    mutable WolfSourceDelivery nestedInspect=WolfSourceDelivery::Ready,nestedDeliver=WolfSourceDelivery::Ready;
    unsigned uses=0;
    bool Check(idEntity*,idEntity*,const WolfSourceEvent& event,idStr& reason)const override{
        ++checks;if(throws)throw std::runtime_error("Own preflight intentional exception");
        // Bound this fixture's own nesting so a regressed guard fails an oracle
        // instead of crashing the QA process with unbounded recursion.
        if(reenter&&!inside){inside=true;idStr nestedReason;nestedInspect=registry->Inspect(event,nestedReason);nestedDeliver=registry->Deliver(event,nestedReason);inside=false;}
        reason.Clear();return true;
    }
    bool Use(idEntity* target,idEntity*,const WolfSourceEvent&,idStr&)override{++uses;target->Show();return true;}
};
WolfSourceEvent Event(std::uint64_t generation,unsigned node,const char* target){WolfSourceEvent out;out.levelGeneration=generation;out.eventId=1;out.sourceNodeId=node;
    out.sourceName="source_registry_own_owner";out.targetName=target;out.useKey="exact_key";return out;}
void Frame(){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);}
}
bool Wolf2009_RunSourceRegistryChecks(idStr& reason){reason.Clear();checks=0;try{
    Own own;wolf2009::EntityDocument doc;doc.entities={Record("source_registry_own_owner","func_mover"),Record("source_registry_own_show","func_static"),
        Record("source_registry_own_missing","func_earthquake"),Record("source_registry_own_remove","func_static"),Record("source_registry_own_throw","func_static")};
    WolfNativeSourceRegistry registry(doc,7001);auto receiver=std::make_shared<ShowReceiver>();auto throwing=std::make_shared<ShowReceiver>();throwing->throws=true;
    auto* source=own.Spawn("source_registry_own_owner");auto* target=own.Spawn("source_registry_own_show");target->Hide();
    auto* removed=own.Spawn("source_registry_own_remove");auto* throwingEntity=own.Spawn("source_registry_own_throw");
    registry.Bind("source_registry_own_owner","func_mover",source,std::shared_ptr<WolfSourceUseReceiver>());
    registry.Bind("source_registry_own_show","func_static",target,receiver);
    registry.Bind("source_registry_own_remove","func_static",removed,std::shared_ptr<WolfSourceUseReceiver>(),true);
    registry.Bind("source_registry_own_throw","func_static",throwingEntity,throwing);
    auto event=Event(7000,10,"source_registry_own_show");
    Need(registry.Deliver(event,reason)==WolfSourceDelivery::WrongGeneration&&registry.DeliveryCount()==0&&receiver->uses==0&&target->IsHidden(),"Wrong generation caused native effects");
    event.levelGeneration=7001;Need(registry.Inspect(event,reason)==WolfSourceDelivery::Ready&&receiver->uses==0&&registry.DeliveryCount()==0,"Source registry preflight caused effects");
    Need(registry.Deliver(event,reason)==WolfSourceDelivery::Applied&&receiver->uses==1&&!target->IsHidden(),"Qualified native Use did not commit");
    Need(registry.Deliver(event,reason)==WolfSourceDelivery::AlreadyDelivered&&receiver->uses==1,"Duplicate source event replayed");
    auto altered=event;altered.useKey="different";Need(registry.Deliver(altered,reason)==WolfSourceDelivery::Refused&&receiver->uses==1,"Reused event identity accepted different payload");
    auto missing=Event(7001,11,"source_registry_own_missing");missing.kind=WolfSourceEvent::RemoveTarget;missing.useKey.clear();
    Need(registry.Deliver(missing,reason)==WolfSourceDelivery::ImplementationAbsent&&registry.LastDelivery()->outcome==WolfSourceDelivery::ImplementationAbsent,
        "Unimplemented quake was reported removed");
    auto absent=Event(7001,12,"source_registry_own_ABSENT");Need(registry.Deliver(absent,reason)==WolfSourceDelivery::SourceAbsent,"Missing source target was confused with an unimplemented target");
    auto wrongKey=Event(7001,13,"source_registry_own_show");wrongKey.useKey="EXACT_KEY";
    Need(registry.Deliver(wrongKey,reason)==WolfSourceDelivery::UnsupportedOperation&&receiver->uses==1,"Source use keys were silently folded");
    auto remove=Event(7001,14,"source_registry_own_remove");remove.kind=WolfSourceEvent::RemoveTarget;remove.useKey.clear();
    const int oldId=gameLocal.GetSpawnId(removed),slot=removed->entityNumber;
    Need(registry.Deliver(remove,reason)==WolfSourceDelivery::Applied&&!registry.NativeEntity(remove.targetName),"Qualified exact native removal did not delete target");
    auto* replacement=own.Spawn("source_registry_own_remove",slot);Need(gameLocal.GetSpawnId(replacement)!=oldId,"Own registry test did not reuse native slot generation");
    auto reused=remove;reused.sourceNodeId=15;Need(registry.Deliver(reused,reason)==WolfSourceDelivery::ExpiredNativeTarget&&gameLocal.entities[slot]==replacement,
        "Source registry rebound an expired name/slot and removed replacement");
    bool rebound=false;try{registry.Bind(remove.targetName,"func_static",replacement,receiver);}catch(const std::exception&){rebound=true;}Need(rebound,"Source registry allowed rebinding after dispatch");
    // External deletion has no registry removed flag. Only the captured native
    // spawn generation can prevent a Use from reaching this same-name object.
    const int oldTargetId=gameLocal.GetSpawnId(target),targetSlot=target->entityNumber;delete target;
    auto* replacementTarget=own.Spawn("source_registry_own_show",targetSlot);replacementTarget->Hide();auto staleUse=event;staleUse.sourceNodeId=20;
    Need(gameLocal.GetSpawnId(replacementTarget)!=oldTargetId&&registry.Deliver(staleUse,reason)==WolfSourceDelivery::ExpiredNativeTarget&&
        receiver->uses==1&&replacementTarget->IsHidden(),"Externally deleted Use target rebound to a replacement generation");
    auto failure=Event(7001,16,"source_registry_own_throw");bool threw=false;try{registry.Deliver(failure,reason);}catch(const std::exception&){threw=true;}
    Need(threw&&throwing->uses==1&&registry.LastDelivery()->outcome==WolfSourceDelivery::Refused,"Callback exception lost source delivery guard");
    Need(registry.Deliver(failure,reason)==WolfSourceDelivery::AlreadyDelivered&&throwing->uses==1,"Failed callback was replayed");
    throwing->throws=false;auto later=failure;later.sourceNodeId=17;Need(registry.Deliver(later,reason)==WolfSourceDelivery::Applied&&throwing->uses==2,"Callback exception left registry busy");
    const int oldSourceId=gameLocal.GetSpawnId(source),sourceSlot=source->entityNumber;delete source;
    auto* replacementSource=own.Spawn("source_registry_own_owner",sourceSlot);auto staleOwner=failure;staleOwner.sourceNodeId=21;
    Need(gameLocal.GetSpawnId(replacementSource)!=oldSourceId&&registry.Deliver(staleOwner,reason)==WolfSourceDelivery::ExpiredNativeTarget&&throwing->uses==2,
        "Deleted event source rebound to a replacement generation");
    {
        // A new map-owned registry captures current native generations. Both
        // entry points must protect callbacks before the native effect phase.
        WolfNativeSourceRegistry guarded(doc,7002);auto preflight=std::make_shared<PreflightReceiver>();preflight->registry=&guarded;
        guarded.Bind("source_registry_own_owner","func_mover",replacementSource,std::shared_ptr<WolfSourceUseReceiver>());
        guarded.Bind("source_registry_own_show","func_static",replacementTarget,preflight);
        auto checked=Event(7002,30,"source_registry_own_show");
        Need(guarded.Inspect(checked,reason)==WolfSourceDelivery::Ready&&preflight->checks==1&&preflight->nestedInspect==WolfSourceDelivery::Refused&&
            preflight->nestedDeliver==WolfSourceDelivery::Refused&&guarded.DeliveryCount()==0&&preflight->uses==0&&replacementTarget->IsHidden(),"Preflight Check reentered Inspect/Deliver or caused native effects");
        Need(guarded.Deliver(checked,reason)==WolfSourceDelivery::Applied&&preflight->checks==2&&preflight->nestedInspect==WolfSourceDelivery::Refused&&
            preflight->nestedDeliver==WolfSourceDelivery::Refused&&preflight->uses==1&&!replacementTarget->IsHidden(),"Deliver preflight callback bypassed recursion guard");
        preflight->reenter=false;
        for(unsigned mode=0;mode<2;++mode){checked.sourceNodeId=31+mode;preflight->throws=true;bool checkThrew=false;
            try{if(mode)guarded.Deliver(checked,reason);else guarded.Inspect(checked,reason);}catch(const std::exception&){checkThrew=true;}
            preflight->throws=false;Need(checkThrew&&guarded.Inspect(checked,reason)==WolfSourceDelivery::Ready,"Preflight exception did not restore a clean Inspect scope");
            Need(guarded.Deliver(checked,reason)==WolfSourceDelivery::Applied&&preflight->uses==mode+2,"Preflight exception left Deliver busy or replayed effects");}
    }
    common->Printf("WOLF2009_SOURCE_REGISTRY_QA_OK checks=%u exact_names=1 no_key_fold=1 no_rebind=1 missing_is_not_removed=1 duplicate_guard=1 callback_exception_guard=1 own_fixture=1\n",checks);reason.Clear();return true;
}catch(const std::exception& error){reason=error.what();common->Warning("WOLF2009_SOURCE_REGISTRY_QA_FAIL checks=%u reason=%s",checks,error.what());return false;}}

bool Wolf2009_RunTrainyardDirectorChecks(WolfSourceMapRuntime& map,idStr& reason){reason.Clear();checks=0;try{
    WolfSourceMapState mapState;Need(map.Query(mapState,reason)&&mapState.spawned&&mapState.sourceStatics==3&&mapState.sourceRelays==1&&mapState.sourceDirectors==1,"Explicit arrival map slice is not assembled");
    auto* director=map.Director();Need(director&&Wolf2009_LiveSourceDirectors()==1,"Ordinary source director Think is missing");
    WolfSourceDirectorState before;Need(director->Query(before,reason)&&before.phase==WolfSourceDirectorPhase::WaitingForArrival&&before.arrivals==0,"Source director did not start at movement boundary");
    const char* names[]={"layer12_func_static_2506","layer12_func_static_2233","layer12_func_static_2224"};idEntityPtr<idEntity> statics[3];
    for(unsigned i=0;i<3;++i){statics[i]=gameLocal.FindEntity(names[i]);Need(statics[i].GetEntity()&&statics[i].GetEntity()->IsHidden()&&statics[i].GetEntity()->GetPhysics()->GetContents()==0,"Source boxcar was visible/solid before arrival");}
    // Query is deliberately effect-free. Only ordinary RunFrame/Think advances.
    for(unsigned i=0;i<3;++i){WolfSourceDirectorState queried;Need(director->Query(queried,reason)&&queried.thinkFrames==before.thinkFrames&&queried.arrivals==0,"Query advanced the source director");}
    unsigned frames=0;WolfSourceDirectorState state=before;
    while(state.phase==WolfSourceDirectorPhase::WaitingForArrival&&frames<1600){Frame();++frames;NeedReason(director->Query(state,reason),reason,"Source director expired");}
    common->Printf("WOLF2009_SOURCE_DIRECTOR_STATE phase=%d frames=%u thinks=%u arrivals=%u shown=%u removed=%u removal=%s relay=%s reason=%s\n",
        int(state.phase),frames,state.thinkFrames,state.arrivals,state.shown,state.removed,Wolf2009_SourceDeliveryText(state.removal),Wolf2009_SourceDeliveryText(state.relay),state.reason.c_str());
    Need(state.phase==WolfSourceDirectorPhase::PartialArrival&&state.arrivals==1&&state.shown==3&&state.removed==0&&state.removal==WolfSourceDelivery::ImplementationAbsent,"Actual mover arrival did not apply measured partial prefix");
    Need(state.relay==WolfSourceDelivery::Applied&&state.relayEvents==1&&state.rawIncomingUseKey=="train_Stop"&&state.authoredRelayUseKey=="train_stop"&&state.forwardedRelayUseKey=="train_stop",
        "Qualified relay did not preserve incoming key and forward its own literal key");
    for(unsigned i=0;i<3;++i)Need(statics[i].GetEntity()&&!statics[i].GetEntity()->IsHidden()&&statics[i].GetEntity()->GetPhysics()->GetContents()==CONTENTS_SOLID,"Original static Show did not commit native visibility/collision");
    WolfSourceMoverState mover;Need(Wolf2009_QuerySourceMover(map.PrimaryMover(),mover,reason)&&mover.phase==WolfSourceMover_Arrived&&mover.arrivals==1&&mover.consumedArrivals==1,"Director did not consume native arrival exactly once");
    wolf2009::TrainDoorUseEvent relay;bool pending=false;idEntity* originalActor=NULL;
    Need(director->TakeRelayRequest(relay,originalActor,pending,reason)&&pending&&originalActor==map.PrimaryMover()&&relay.sourceEntityName=="train_greeter_brainuse_1"&&
        relay.ownerEntityName=="train_greeter"&&relay.useKey=="train_stop"&&relay.levelGeneration==state.levelGeneration&&relay.eventId==state.arrivalEventId,
        "Qualified relay handoff lost exact source/target/key/generation/original actor");
    Need(director->TakeRelayRequest(relay,originalActor,pending,reason)&&!pending&&!originalActor,"Source relay handoff was consumed twice");
    for(unsigned i=0;i<8;++i)Frame();WolfSourceDirectorState after;Need(director->Query(after,reason)&&after.arrivals==1&&after.shown==3&&after.removed==0&&after.thinkFrames==state.thinkFrames,"Terminal source prefix replayed from ordinary Think");
    NeedReason(map.Query(mapState,reason),reason,"Source map capability/lifetime changed");
    common->Printf("WOLF2009_SOURCE_DIRECTOR_QA_OK checks=%u frames=%u ordinary_think=1 source_static_show=3 removed_quake=0 relay_own_key_request=1 actor_start_ack=0 committed_arrival_once=1 query_effects=0 partial_prefix=1 full_campaign=0\n",checks,frames);reason.Clear();return true;
}catch(const std::exception& error){reason=error.what();common->Warning("WOLF2009_SOURCE_DIRECTOR_QA_FAIL checks=%u reason=%s",checks,error.what());return false;}}

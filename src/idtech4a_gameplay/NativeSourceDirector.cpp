// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceDirector.h"
#include "NativeSourceMover.h"
#include "NativeSourceRelay.h"
#include "TrainArrival.h"
#include <limits>
#include <stdexcept>
#include <utility>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
namespace {
void Need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){Need(ok,reason.IsEmpty()?fallback:reason.c_str());}
std::uint64_t Hash(const std::vector<unsigned char>& bytes){std::uint64_t out=14695981039346656037ull;for(unsigned char c:bytes){out^=c;out*=1099511628211ull;}return out;}
class RelayReceiver : public WolfSourceUseReceiver {
  public:
    std::unique_ptr<WolfSourceBrainUseRelay> route;idEntityPtr<idEntity> originalActor;
    bool Check(idEntity* entity,idEntity* source,const WolfSourceEvent& event,idStr& reason)const override{
        const char* why="";
        if(!route||!entity||!source||!route->CanAccept(event,why)){reason=why[0]?why:"Source relay native owner/route is absent";return false;}
        reason.Clear();return true;
    }
    bool Use(idEntity* entity,idEntity* source,const WolfSourceEvent& event,idStr& reason)override{
        if(!Check(entity,source,event,reason))return false;
        const char* why="";if(!route->Accept(event,why)){reason=why;return false;}
        originalActor=source;return true;
    }
};
int liveDirectors=0;
class DirectorThinkHost {
  public:
    virtual ~DirectorThinkHost(){}
    virtual void Tick()=0;
    virtual bool Active()const=0;
};
const std::shared_ptr<DirectorThinkHost>* pendingThink=NULL;
struct ThinkContext {
    explicit ThinkContext(const std::shared_ptr<DirectorThinkHost>& value){Need(!pendingThink,"Recursive director Spawn");pendingThink=&value;}
    ~ThinkContext(){pendingThink=NULL;}
};
}
struct WolfSourceDirector::Impl : DirectorThinkHost {
    WolfSourceDirectorRequest request;std::shared_ptr<WolfNativeSourceRegistry> registry;
    wolf2009::LevelEntityRegistry source;wolf2009::TrainArrivalPlan plan;wolf2009::TrainArrivalKeyedUseTail keyedUseTail;
    std::unique_ptr<wolf2009::TrainArrivalRuntime> validation;
    std::shared_ptr<RelayReceiver> relay;
    idEntityPtr<idEntity> mover,nativeThink;WolfSourceMoverState before;
    WolfSourceDirectorState state;bool relayBound=false;
    Impl(const wolf2009::EntityDocument& doc):source(doc){}
    wolf2009::TrainArrivalMoveStamp Stamp(const WolfSourceMoverState& value)const{
        wolf2009::TrainArrivalMoveStamp out;out.brainResource=request.brainResource;out.ownerEntityName=request.ownerName;
        out.moveNodeId=value.moveNode;out.levelGeneration=registry->Generation();out.spawnDelivered=value.spawnDeliveries==1;
        out.active=value.phase==WolfSourceMover_Moving;out.arrived=value.phase==WolfSourceMover_Arrived;return out;
    }
    WolfSourceEvent Event(WolfSourceEvent::Kind kind,unsigned node,const std::string& target,const std::string& key="")const{
        WolfSourceEvent out;out.kind=kind;out.sourceName=request.ownerName;out.targetName=target;out.useKey=key;
        out.sourceNodeId=node;out.levelGeneration=registry->Generation();out.eventId=state.arrivalEventId;return out;
    }
    void CheckMover(const WolfSourceMoverState& value)const{
        Need(value.sourceName&&request.ownerName==value.sourceName&&value.moveNode==plan.moveNodeId&&value.onSpawnNode==plan.onSpawnNodeId,
            "Director mover source owner/node changed");
        Need(value.entitiesBytes==request.entitiesBytes&&value.entitiesFingerprint==request.entitiesFingerprint&&
            value.brainBytes==request.brainBytes.size()&&value.brainFingerprint==Hash(request.brainBytes),"Director mover source identity differs");
        Need(value.phase!=WolfSourceMover_Unsupported&&value.spawnDeliveries==1,"Director mover is unsupported or OnSpawn was not delivered");
    }
    bool Active()const override{return state.phase==WolfSourceDirectorPhase::WaitingForArrival;}
    void Tick() override{
        if(state.phase!=WolfSourceDirectorPhase::WaitingForArrival)return;
        try{
            ++state.thinkFrames;idStr why;WolfSourceMoverState current;auto* entity=mover.GetEntity();
            Need(entity&&registry->NativeEntity(request.ownerName)==entity,"Director captured mover generation expired");
            NeedReason(Wolf2009_QuerySourceMover(entity,current,why),why,"Director mover query failed");CheckMover(current);
            if(current.phase!=WolfSourceMover_Arrived){Need(current.phase==WolfSourceMover_Moving,"Director mover stopped without committed arrival");before=current;return;}
            Need(before.phase==WolfSourceMover_Moving&&current.arrivals==1&&current.consumedArrivals==0,"Director arrival transition was already consumed or differs");
            // Validate the original selected source prefix in a scratch DATA
            // registry. Its four candidate deltas are not native effect ACKs.
            auto validationCopy=*validation;auto sourceCopy=source;
            const auto expected=validationCopy.ObserveCommittedMove(Stamp(before),Stamp(current),sourceCopy);
            Need(expected.status==wolf2009::TrainArrivalStatus::Applied&&expected.changes.size()==4,"Director source arrival plan refused transition");
            state.arrivalEventId=1; // One committed movement in this explicit generation/profile.
            const auto removal=Event(WolfSourceEvent::RemoveTarget,plan.removal.nodeId,plan.removal.name);
            const auto removalReady=registry->Inspect(removal,why);
            Need(removalReady==WolfSourceDelivery::Ready||removalReady==WolfSourceDelivery::ImplementationAbsent,
                "Director removal target has an invalid native generation/profile");
            for(const auto& show:plan.shows)NeedReason(registry->Inspect(Event(WolfSourceEvent::UseTarget,show.nodeId,show.name),why)==WolfSourceDelivery::Ready,
                why,"Director static target is not ready");
            bool pending=false;unsigned node=0;Need(Wolf2009_TakeSourceMoverArrival(entity,pending,node,why)&&pending&&node==plan.moveNodeId,
                "Director failed to consume the actual committed mover arrival");
            ++state.arrivals;state.arrivalTimeMs=gameLocal.time;state.removal=registry->Deliver(removal,why);
            Need(state.removal==removalReady||state.removal==WolfSourceDelivery::Applied,"Director removal changed after admission");
            if(state.removal==WolfSourceDelivery::Applied)++state.removed;
            for(const auto& show:plan.shows){const auto outcome=registry->Deliver(Event(WolfSourceEvent::UseTarget,show.nodeId,show.name),why);
                NeedReason(outcome==WolfSourceDelivery::Applied,why,"Native source static show failed");++state.shown;}
            state.relay=registry->Deliver(Event(WolfSourceEvent::UseTarget,keyedUseTail.sourceNodeId,keyedUseTail.targetName,keyedUseTail.useKey),why);
            if(state.relay==WolfSourceDelivery::Applied){++state.relayEvents;state.forwardedRelayUseKey=relay->route->AuthoredKey();}
            else NeedReason(state.relay==WolfSourceDelivery::UnsupportedOperation||state.relay==WolfSourceDelivery::ImplementationAbsent,
                why,"Director relay delivery failed");
            state.reason=why.c_str();state.phase=WolfSourceDirectorPhase::PartialArrival;
            // Discard the scratch registry: especially its predicted removal
            // must never become a statement about an unimplemented native quake.
            gameLocal.Printf("WOLF2009_SOURCE_ARRIVAL generation=%llu event=%llu shows=%u removals=%u removal_status=%s relay_status=%s raw_key=%s relay_key=%s forwarded_key=%s actor_start_ack=0 full_prefix=0 later_nodes=unsupported full_campaign=0\n",
                static_cast<unsigned long long>(state.levelGeneration),static_cast<unsigned long long>(state.arrivalEventId),state.shown,state.removed,
                Wolf2009_SourceDeliveryText(state.removal),Wolf2009_SourceDeliveryText(state.relay),state.rawIncomingUseKey.c_str(),state.authoredRelayUseKey.c_str(),state.forwardedRelayUseKey.c_str());
        }catch(const std::exception& error){state.phase=WolfSourceDirectorPhase::Unsupported;state.reason=error.what();gameLocal.Warning("WOLF2009_SOURCE_DIRECTOR_UNSUPPORTED reason=%s",error.what());}
    }
};
// Only a weak reference goes into the native Think entity. The map owner
// destroys it before releasing registry/collision/render/animation owners.
class idWolfSourceDirectorThink : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceDirectorThink);
    idWolfSourceDirectorThink(){Need(pendingThink&&bool(*pendingThink),"Source director requires its map owner");host=*pendingThink;++liveDirectors;}
    ~idWolfSourceDirectorThink(){host.reset();--liveDirectors;}
    void Spawn(){fl.networkSync=false;fl.neverDormant=true;cinematic=true;BecomeActive(TH_THINK);}
    void Think()override{const auto owner=host.lock();if(!owner){BecomeInactive(TH_THINK);return;}owner->Tick();if(!owner->Active())BecomeInactive(TH_THINK);}
    void Save(idSaveGame*)const{gameLocal.Error("Source director save state is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Source director restore is unimplemented");}
  private:std::weak_ptr<DirectorThinkHost> host;
};
CLASS_DECLARATION(idEntity,idWolfSourceDirectorThink)
END_CLASS
WolfSourceDirector::WolfSourceDirector(std::shared_ptr<Impl> value):impl(std::move(value)){}
WolfSourceDirector::~WolfSourceDirector(){if(auto* entity=impl->nativeThink.GetEntity())delete entity;}
std::unique_ptr<WolfSourceDirector> Wolf2009_PrepareSourceDirector(const WolfSourceDirectorRequest& request,
    const wolf2009::EntityDocument& document,std::shared_ptr<WolfNativeSourceRegistry> registry,idStr& reason){reason.Clear();try{
    Need(registry&&registry->Generation()&&request.profile=="MeasuredTrainyardArrivalV1"&&request.brainResource=="brains/trainyard/movetrain"&&
        request.ownerName=="starting_boxcar_model"&&request.entitiesBytes&&request.brainBytes.size()<=65536,"Explicit measured native arrival request required");
    std::shared_ptr<WolfSourceDirector::Impl> out(new WolfSourceDirector::Impl(document));out->request=request;out->registry=std::move(registry);
    out->plan=wolf2009::DecodeTrainArrivalH1023(request.brainBytes,request.brainResource,out->source);
    // The generic arrival decoder retains an opaque continuation. A source
    // target/key projection is not execution authority for the next action.
    out->keyedUseTail=wolf2009::QualifyTrainArrivalKeyedUseTail(out->plan);
    const auto& plan=out->plan;
    Need(plan.hasRemoval&&plan.removal.name=="layer3_func_earthquake_2"&&plan.removal.classname=="func_earthquake"&&
        plan.shows.size()==3&&plan.shows[0].name=="layer12_func_static_2506"&&plan.shows[1].name=="layer12_func_static_2233"&&plan.shows[2].name=="layer12_func_static_2224"&&
        plan.unsupportedNodeType==68&&plan.unsupportedTargetName=="train_greeter_brainuse_1"&&plan.unsupportedTargetClassname=="target_brainuse"&&plan.unsupportedUseKey=="train_Stop",
        "Source arrival differs from measured trainyard prefix");
    for(const auto& show:plan.shows)Need(show.model=="models/vehicles/box_car.md5r","Measured arrival static source model differs");
    out->relay.reset(new RelayReceiver);out->relay->route=Wolf2009_PrepareSourceBrainUseRelay(WolfSourceRelayPolicy::MeasuredTrainyardOwnLiteralV1,
        document,2031,request.ownerName,out->keyedUseTail.sourceNodeId,out->keyedUseTail.useKey,out->registry->Generation());
    out->validation.reset(new wolf2009::TrainArrivalRuntime(plan,request.ownerName,out->registry->Generation()));
    out->state.levelGeneration=out->registry->Generation();out->state.unsupportedNode=plan.unsupportedNodeId;out->state.unsupportedType=plan.unsupportedNodeType;
    out->state.rawIncomingUseKey=out->relay->route->SourceIncomingKey();out->state.authoredRelayUseKey=out->relay->route->AuthoredKey();
    return std::unique_ptr<WolfSourceDirector>(new WolfSourceDirector(std::move(out)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceDirector>();}}
bool WolfSourceDirector::BindRelay(idEntity* entity,idStr& reason){reason.Clear();try{
    Need(impl->state.phase==WolfSourceDirectorPhase::Prepared&&!impl->relayBound,"Director relay already bound/started");
    impl->registry->Bind(impl->relay->route->SignalName(),"target_brainuse",entity,impl->relay);impl->relayBound=true;return true;
}catch(const std::exception& error){reason=error.what();return false;}}
bool WolfSourceDirector::Start(idEntity* mover,idStr& reason){reason.Clear();try{
    Need(impl->state.phase==WolfSourceDirectorPhase::Prepared&&!impl->nativeThink.GetEntity()&&impl->relayBound,"Director start is not prepared");
    Need(!gameLocal.isMultiplayer&&!gameLocal.isClient&&mover&&impl->registry->NativeEntity(impl->request.ownerName)==mover,"Director requires its registered local SP mover");
    WolfSourceMoverState current;NeedReason(Wolf2009_QuerySourceMover(mover,current,reason),reason,"Director native mover is unavailable");impl->CheckMover(current);
    Need(current.phase==WolfSourceMover_Moving&&current.arrivals==0&&current.consumedArrivals==0,"Director must start before actual movement completion");
    const std::string name="wolf2009_source_director_"+std::to_string(impl->registry->Generation());Need(!gameLocal.FindEntity(name.c_str()),"Native source director name is occupied");
    idDict args;args.Set("classname","wolf2009_source_director");args.Set("name",name.c_str());args.Set("noclipmodel","1");args.Set("networkSync","0");
    const std::shared_ptr<DirectorThinkHost> host=impl;ThinkContext context(host);
    std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourceDirectorThink::Type,&args));
    Need(entity&&entity->GetType()==&idWolfSourceDirectorThink::Type,"Native source director allocation failed");
    impl->before=current;impl->mover=mover;impl->nativeThink=entity.release();impl->state.phase=WolfSourceDirectorPhase::WaitingForArrival;return true;
}catch(const std::exception& error){reason=error.what();return false;}}
bool WolfSourceDirector::Query(WolfSourceDirectorState& out,idStr& reason)const{reason.Clear();
    if(impl->state.phase!=WolfSourceDirectorPhase::Prepared&&(!impl->nativeThink.GetEntity()||!impl->mover.GetEntity())){reason="Native source director/mover lifetime expired";return false;}
    out=impl->state;return true;
}
bool WolfSourceDirector::TakeRelayRequest(wolf2009::TrainDoorUseEvent& out,bool& pending,idStr& reason){pending=false;reason.Clear();
    idEntity* ignored=NULL;return TakeRelayRequest(out,ignored,pending,reason);
}
bool WolfSourceDirector::TakeRelayRequest(wolf2009::TrainDoorUseEvent& out,idEntity*& originalActor,bool& pending,idStr& reason){originalActor=NULL;pending=false;reason.Clear();try{
    if(!impl->relayBound||!impl->registry->NativeEntity(impl->relay->route->SignalName())){reason="Source relay generation expired";return false;}
    if(impl->state.relayEvents){auto* source=impl->relay->originalActor.GetEntity();
        if(!source||source!=impl->mover.GetEntity()||impl->request.ownerName!=source->GetName()){reason="Source relay original actor generation expired";return false;}
        if(impl->relay->route->Take(out)){originalActor=source;pending=true;}}
    return true;
}catch(const std::exception& error){reason=error.what();return false;}
}
int Wolf2009_LiveSourceDirectors(){return liveDirectors;}

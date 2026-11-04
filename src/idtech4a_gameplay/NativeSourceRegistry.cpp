// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceRegistry.h"
#include "TrainArrival.h"
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
namespace {
void Need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
WolfSourceDelivery Refuse(WolfSourceDelivery result,const char* text,idStr& reason){reason=text;return result;}
void Name(const std::string& name){Need(!name.empty()&&name.size()<=1024,"Source registry name budget");
    for(unsigned char c:name)Need(c>=32&&c<127,"Source registry name byte");}
bool SameIdentity(const WolfSourceEvent& a,const WolfSourceEvent& b){return a.levelGeneration==b.levelGeneration&&a.eventId==b.eventId&&a.sourceNodeId==b.sourceNodeId;}
bool SamePayload(const WolfSourceEvent& a,const WolfSourceEvent& b){return SameIdentity(a,b)&&a.kind==b.kind&&a.sourceName==b.sourceName&&a.targetName==b.targetName&&a.useKey==b.useKey;}
}
struct WolfNativeSourceRegistry::Impl {
    struct Entry {
        std::string classname;idEntityPtr<idEntity> native;
        std::shared_ptr<WolfSourceUseReceiver> receiver;
        bool bound=false,removable=false,removed=false;
    };
    std::uint64_t generation;std::map<std::string,Entry> entries;
    std::vector<WolfSourceDeliveryRecord> deliveries;bool busy=false;
    explicit Impl(std::uint64_t value):generation(value){deliveries.reserve(1024);}
    WolfSourceDelivery Inspect(const WolfSourceEvent& event,idStr& reason)const{
        reason.Clear();
        if(event.levelGeneration!=generation||!event.eventId||!event.sourceNodeId)
            return Refuse(WolfSourceDelivery::WrongGeneration,"Source event generation/identity differs",reason);
        for(const auto& record:deliveries)if(SameIdentity(record.event,event))
            return Refuse(SamePayload(record.event,event)?WolfSourceDelivery::AlreadyDelivered:WolfSourceDelivery::Refused,
                SamePayload(record.event,event)?"Source event was already attempted":"Source event identity has a different payload",reason);
        const auto source=entries.find(event.sourceName),target=entries.find(event.targetName);
        if(source==entries.end()||target==entries.end())return Refuse(WolfSourceDelivery::SourceAbsent,"Exact source event name is absent from ENTS",reason);
        if(source==target)return Refuse(WolfSourceDelivery::UnsupportedOperation,"Self-targeted source event is not admitted",reason);
        if(!source->second.bound||!target->second.bound)return Refuse(WolfSourceDelivery::ImplementationAbsent,"Source entity exists but its native implementation is absent",reason);
        auto* owner=source->second.native.GetEntity();auto* entity=target->second.native.GetEntity();
        if(!owner||!entity||source->second.removed||target->second.removed)
            return Refuse(WolfSourceDelivery::ExpiredNativeTarget,"Captured native entity generation expired",reason);
        if(event.sourceName!=owner->GetName()||event.targetName!=entity->GetName())
            return Refuse(WolfSourceDelivery::Refused,"Captured native source name changed",reason);
        if(event.kind==WolfSourceEvent::RemoveTarget){
            if(!target->second.removable)return Refuse(WolfSourceDelivery::UnsupportedOperation,"Native target does not admit source removal",reason);
            return WolfSourceDelivery::Ready;
        }
        if(event.kind!=WolfSourceEvent::UseTarget||!target->second.receiver)
            return Refuse(WolfSourceDelivery::UnsupportedOperation,"Native target has no qualified source Use receiver",reason);
        if(!target->second.receiver->Check(entity,owner,event,reason))return WolfSourceDelivery::UnsupportedOperation;
        return WolfSourceDelivery::Ready;
    }
};
WolfNativeSourceRegistry::WolfNativeSourceRegistry(const wolf2009::EntityDocument& document,std::uint64_t generation):impl(new Impl(generation)){
    Need(generation!=0,"Source registry requires nonzero map generation");
    // Reuse the source registry's bounded unique-name/property qualification;
    // its initial alive flags are DATA, never a claim of native instantiation.
    wolf2009::LevelEntityRegistry qualified(document);
    for(const auto& entity:document.entities){const auto* name=entity.Find("name");if(!name||name->text.empty())continue;
        const auto* source=qualified.Find(name->text);Need(source,"Source registry qualification differs");
        Impl::Entry entry;entry.classname=source->classname;impl->entries.emplace(source->name,std::move(entry));}
}
WolfNativeSourceRegistry::~WolfNativeSourceRegistry(){}
void WolfNativeSourceRegistry::Bind(const std::string& name,const std::string& classname,idEntity* entity,std::shared_ptr<WolfSourceUseReceiver> receiver,bool removal){
    Need(!impl->busy&&impl->deliveries.empty(),"Source registry bindings are sealed after dispatch");Name(name);Name(classname);
    auto found=impl->entries.find(name);Need(found!=impl->entries.end()&&found->second.classname==classname,"Native source binding differs from exact ENTS name/class");
    Need(!found->second.bound&&entity&&name==entity->GetName()&&entity->entityNumber>=MAX_CLIENTS&&entity->entityNumber<MAX_GENTITIES&&gameLocal.entities[entity->entityNumber]==entity,
        "Source registry native binding is occupied/invalid");
    for(const auto& entry:impl->entries)Need(!entry.second.bound||entry.second.native.GetEntity()!=entity,"Native entity already belongs to another source name");
    found->second.native=entity;found->second.receiver=std::move(receiver);found->second.removable=removal;found->second.bound=true;
}
WolfSourceDelivery WolfNativeSourceRegistry::Inspect(const WolfSourceEvent& event,idStr& reason)const{
    if(impl->busy)return Refuse(WolfSourceDelivery::Refused,"Recursive source registry dispatch",reason);
    return impl->Inspect(event,reason);
}
WolfSourceDelivery WolfNativeSourceRegistry::Deliver(const WolfSourceEvent& event,idStr& reason){
    if(impl->busy)return Refuse(WolfSourceDelivery::Refused,"Recursive source registry dispatch",reason);
    Name(event.sourceName);Name(event.targetName);if(!event.useKey.empty())Name(event.useKey);
    const auto inspected=impl->Inspect(event,reason);
    if(inspected==WolfSourceDelivery::AlreadyDelivered||inspected==WolfSourceDelivery::WrongGeneration)return inspected;
    for(const auto& record:impl->deliveries)if(SameIdentity(record.event,event))return inspected;
    Need(impl->deliveries.size()<1024,"Source event delivery budget exhausted");
    WolfSourceDeliveryRecord record;record.event=event;record.outcome=inspected;record.reason=reason.c_str();
    // All event/string storage is committed before native effects. A callback
    // failure remains an attempted event and cannot be replayed into a slot.
    impl->deliveries.push_back(std::move(record));
    if(inspected!=WolfSourceDelivery::Ready)return inspected;
    auto& stored=impl->deliveries.back();stored.outcome=WolfSourceDelivery::Refused;impl->busy=true;
    try{
        auto& target=impl->entries.find(event.targetName)->second;
        if(event.kind==WolfSourceEvent::RemoveTarget){delete target.native.GetEntity();target.removed=true;stored.outcome=WolfSourceDelivery::Applied;}
        else if(target.receiver->Use(target.native.GetEntity(),impl->entries.find(event.sourceName)->second.native.GetEntity(),stored.event,reason))
            stored.outcome=WolfSourceDelivery::Applied;
        if(stored.outcome!=WolfSourceDelivery::Applied)stored.reason=reason.c_str();
        impl->busy=false;return stored.outcome;
    }catch(...){impl->busy=false;throw;}
}
idEntity* WolfNativeSourceRegistry::NativeEntity(const std::string& name)const{const auto found=impl->entries.find(name);return found==impl->entries.end()||found->second.removed?NULL:found->second.native.GetEntity();}
std::uint64_t WolfNativeSourceRegistry::Generation()const{return impl->generation;}
const WolfSourceDeliveryRecord* WolfNativeSourceRegistry::LastDelivery()const{return impl->deliveries.empty()?NULL:&impl->deliveries.back();}
unsigned WolfNativeSourceRegistry::DeliveryCount()const{return unsigned(impl->deliveries.size());}
const char* Wolf2009_SourceDeliveryText(WolfSourceDelivery value){switch(value){
    case WolfSourceDelivery::Ready:return "ready";case WolfSourceDelivery::Applied:return "applied";
    case WolfSourceDelivery::AlreadyDelivered:return "already-delivered";case WolfSourceDelivery::SourceAbsent:return "source-absent";
    case WolfSourceDelivery::ImplementationAbsent:return "implementation-absent";case WolfSourceDelivery::ExpiredNativeTarget:return "native-generation-expired";
    case WolfSourceDelivery::WrongGeneration:return "wrong-generation";case WolfSourceDelivery::UnsupportedOperation:return "unsupported-operation";
    case WolfSourceDelivery::Refused:return "refused";}return "unknown";}

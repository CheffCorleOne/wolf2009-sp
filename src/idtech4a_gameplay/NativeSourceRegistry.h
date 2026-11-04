// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_REGISTRY_H
#define WOLF2009_NATIVE_SOURCE_REGISTRY_H
#include "Entities.h"
#include <cstdint>
#include <memory>
#include <string>
class idEntity;class idStr;

struct WolfSourceEvent {
    enum Kind { UseTarget, RemoveTarget } kind=UseTarget;
    std::string sourceName,targetName,useKey;
    std::uint64_t levelGeneration=0,eventId=0;
    std::uint32_t sourceNodeId=0;
};
enum class WolfSourceDelivery {
    Ready,Applied,AlreadyDelivered,SourceAbsent,ImplementationAbsent,
    ExpiredNativeTarget,WrongGeneration,UnsupportedOperation,Refused
};
// Explicit, owning native receiver. A source classname alone never registers
// generic Activate. Check must be effect-free; Use runs on the game thread.
class WolfSourceUseReceiver {
  public:
    virtual ~WolfSourceUseReceiver(){}
    virtual bool Check(idEntity* target,idEntity* source,const WolfSourceEvent&,idStr&)const=0;
    virtual bool Use(idEntity* target,idEntity* source,const WolfSourceEvent&,idStr&)=0;
};
struct WolfSourceDeliveryRecord {
    WolfSourceEvent event;
    WolfSourceDelivery outcome=WolfSourceDelivery::Refused;
    std::string reason;
};
// Map-owned exact source-name registry. Each binding captures idEntityPtr's
// spawn generation ONCE. Expired slots/names are never rebound by FindEntity.
// Source presence, native implementation and successful effects are distinct.
class WolfNativeSourceRegistry {
  public:
    WolfNativeSourceRegistry(const wolf2009::EntityDocument&,std::uint64_t levelGeneration);
    ~WolfNativeSourceRegistry();
    void Bind(const std::string& exactName,const std::string& exactClass,idEntity*,
        std::shared_ptr<WolfSourceUseReceiver>,bool allowRemoval=false);
    WolfSourceDelivery Inspect(const WolfSourceEvent&,idStr&)const;
    // Every attempted identity is remembered before native Use/remove effects.
    // Replays do not repeat effects, including a Use which failed after starting.
    // This does not promise rollback of arbitrary receiver side effects.
    // Inspect and Deliver both guard the complete preflight callback scope;
    // recursive entry refuses, and exceptions restore the guard.
    WolfSourceDelivery Deliver(const WolfSourceEvent&,idStr&);
    idEntity* NativeEntity(const std::string& exactName)const;
    std::uint64_t Generation()const;
    const WolfSourceDeliveryRecord* LastDelivery()const;
    unsigned DeliveryCount()const;
    WolfNativeSourceRegistry(const WolfNativeSourceRegistry&)=delete;
    WolfNativeSourceRegistry& operator=(const WolfNativeSourceRegistry&)=delete;
  private:
    struct Impl;std::unique_ptr<Impl> impl;
};
const char* Wolf2009_SourceDeliveryText(WolfSourceDelivery);
#endif

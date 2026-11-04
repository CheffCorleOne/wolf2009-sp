// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_RELAY_H
#define WOLF2009_NATIVE_SOURCE_RELAY_H
// Portable game-private data/one-shot seam. It dispatches no native event and
// supplies no actor-start ACK. The native map separately qualifies the selected
// compiled rvTarget_BRAINUse class and captures the sender/relay generations.
#include "NativeSourceRegistry.h"
#include "TrainDoorSignal.h"
#include <set>
#include <stdexcept>
#include <utility>

enum class WolfSourceRelayPolicy { Unspecified,MeasuredTrainyardOwnLiteralV1 };
class WolfSourceBrainUseRelay {
  public:
    const std::string& SignalName()const{return signal_;}
    const std::string& TargetName()const{return target_;}
    const std::string& AuthoredKey()const{return ownKey_;}
    const std::string& SourceIncomingKey()const{return sourceIncoming_;}
    const std::string& AcceptedIncomingKey()const{return acceptedIncoming_;}
    bool CanAccept(const WolfSourceEvent& event,const char*& reason)const{
        reason="";
        if(event.kind!=WolfSourceEvent::UseTarget||event.levelGeneration!=generation_||!event.eventId||event.sourceNodeId!=node_||
           event.sourceName!=owner_||event.targetName!=signal_){reason="Source relay event source/node/generation differs";return false;}
        if(accepted_){reason="Source relay request was already accepted";return false;}
        if(event.useKey.size()>1024){reason="Source relay incoming key budget";return false;}
        for(unsigned char c:event.useKey)if(c<32||c>=127){reason="Source relay incoming key byte";return false;}
        // Qualified rvTarget_BRAINUse ignores this incoming value. In particular,
        // forwarding own train_stop is not case-folding incoming train_Stop.
        return true;
    }
    bool Accept(const WolfSourceEvent& event,const char*& reason){
        if(!CanAccept(event,reason))return false;
        wolf2009::TrainDoorUseEvent prepared;prepared.sourceEntityName=signal_;prepared.ownerEntityName=target_;prepared.useKey=ownKey_;
        prepared.levelGeneration=generation_;prepared.eventId=event.eventId;std::string incoming=event.useKey;
        pending_=std::move(prepared);acceptedIncoming_=std::move(incoming);accepted_=available_=true;return true;
    }
    bool Take(wolf2009::TrainDoorUseEvent& out){if(!available_)return false;
        out=pending_;available_=false;return true;}
    WolfSourceBrainUseRelay(const WolfSourceBrainUseRelay&)=delete;
    WolfSourceBrainUseRelay& operator=(const WolfSourceBrainUseRelay&)=delete;
  private:
    std::string signal_,target_,ownKey_,owner_,sourceIncoming_,acceptedIncoming_;
    std::uint64_t generation_=0;std::uint32_t node_=0;bool accepted_=false,available_=false;
    wolf2009::TrainDoorUseEvent pending_;
    WolfSourceBrainUseRelay(){}
    friend std::unique_ptr<WolfSourceBrainUseRelay> Wolf2009_PrepareSourceBrainUseRelay(
        WolfSourceRelayPolicy,const wolf2009::EntityDocument&,std::size_t,
        const std::string&,std::uint32_t,const std::string&,std::uint64_t);
};
namespace wolfSourceRelayDetail {
inline void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
inline std::string Fold(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
inline std::string Text(const wolf2009::EntityRecord& entity,const char* key){const auto* value=entity.Find(key);
    Need(value&&value->kind==wolf2009::EntityProperty::Text&&value->text.size()<=1024,"Source relay literal text absent/unqualified");return value->text;}
inline std::size_t Exact(const wolf2009::EntityDocument& document,const std::string& name){
    std::size_t found=0,matches=0,properties=0;const auto folded=Fold(name);
    for(std::size_t i=0;i<document.entities.size();++i){const auto& entity=document.entities[i];Need(entity.properties.size()<=2000000-properties,"Source relay lookup work budget");properties+=entity.properties.size();
        const auto* value=entity.Find("name");if(!value)continue;Need(value->kind==wolf2009::EntityProperty::Text&&value->text.size()<=1024,"Source relay entity name is unqualified");
        if(Fold(value->text)!=folded)continue;Need(value->text==name,"Source relay name collides in native namespace");found=i;++matches;}
    Need(matches==1,"Source relay/target name absent or ambiguous");return found;
}
}
inline std::unique_ptr<WolfSourceBrainUseRelay> Wolf2009_PrepareSourceBrainUseRelay(
    WolfSourceRelayPolicy policy,const wolf2009::EntityDocument& document,std::size_t index,
    const std::string& sourceOwner,std::uint32_t sourceNode,const std::string& sourceIncoming,std::uint64_t generation){
    using wolfSourceRelayDetail::Need;using wolfSourceRelayDetail::Text;using wolfSourceRelayDetail::Exact;using wolfSourceRelayDetail::Fold;
    Need(policy==WolfSourceRelayPolicy::MeasuredTrainyardOwnLiteralV1&&document.entities.size()<=200000&&index==2031&&index<document.entities.size()&&
        sourceOwner=="starting_boxcar_model"&&sourceNode!=0&&sourceIncoming=="train_Stop"&&generation!=0,"Explicit measured source relay request required");
    const auto& source=document.entities[index];const std::set<std::string> allowed={"classname","name","origin","rotation","target","usekey"};std::set<std::string> seen;
    Need(source.properties.size()==6,"Measured source relay property profile differs");
    for(const auto& field:source.properties){Need(field.key.size()<=1024,"Source relay property key budget");const auto key=Fold(field.key);Need(allowed.count(key)&&seen.insert(key).second,"Unadmitted/duplicate source relay property");}
    const auto name=Text(source,"name"),target=Text(source,"target"),key=Text(source,"useKey");
    Need(Text(source,"classname")=="target_brainuse"&&name=="train_greeter_brainuse_1"&&target=="train_greeter"&&key=="train_stop"&&name!=target,
        "Measured source relay class/target/own key differs; recursive/other targets are unsupported");
    Need(Exact(document,name)==index,"Source relay selected identity differs");const auto targetIndex=Exact(document,target);
    Need(targetIndex==2034&&Text(document.entities[targetIndex],"classname")=="trainyard_greeter","Source relay target is not the qualified original greeter");
    std::unique_ptr<WolfSourceBrainUseRelay> out(new WolfSourceBrainUseRelay);out->signal_=name;out->target_=target;out->ownKey_=key;out->owner_=sourceOwner;
    out->sourceIncoming_=sourceIncoming;out->node_=sourceNode;out->generation_=generation;return out;
}
#endif

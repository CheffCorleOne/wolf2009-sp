// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceRelay.h"
#include <functional>
#include <iostream>
namespace {
unsigned checks=0;
void Need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
wolf2009::EntityProperty Text(const char* key,const char* value){wolf2009::EntityProperty out;out.key=key;out.text=value;return out;}
wolf2009::EntityProperty Floats(const char* key,std::initializer_list<float> value){wolf2009::EntityProperty out;out.key=key;out.kind=wolf2009::EntityProperty::Floats;out.numbers=value;return out;}
wolf2009::EntityDocument Own(){
    // Independently authored sparse data. No original ENTS/BRAI or game assets.
    wolf2009::EntityDocument out;out.entities.resize(2035);
    out.entities[2031].properties={Text("classname","target_brainuse"),Text("name","train_greeter_brainuse_1"),Text("target","train_greeter"),
        Text("useKey","train_stop"),Floats("origin",{0,0,0}),Floats("rotation",{1,0,0,0,1,0,0,0,1})};
    out.entities[2034].properties={Text("name","train_greeter"),Text("classname","trainyard_greeter")};return out;
}
std::unique_ptr<WolfSourceBrainUseRelay> Prepare(const wolf2009::EntityDocument& document){
    return Wolf2009_PrepareSourceBrainUseRelay(WolfSourceRelayPolicy::MeasuredTrainyardOwnLiteralV1,document,2031,"starting_boxcar_model",6,"train_Stop",19);
}
WolfSourceEvent Event(){WolfSourceEvent out;out.sourceName="starting_boxcar_model";out.targetName="train_greeter_brainuse_1";out.useKey="train_Stop";
    out.sourceNodeId=6;out.eventId=701;out.levelGeneration=19;return out;}
void Rejects(const std::function<void()>& action){bool refused=false;try{action();}catch(const std::exception&){refused=true;}Need(refused,"Unqualified relay source was accepted");}
void Replace(wolf2009::EntityRecord& entity,const char* key,const char* value){for(auto& field:entity.properties)if(field.key==key){field.text=value;return;}throw std::runtime_error("Own fixture property absent");}
}
int main(){try{
    const auto source=Own();const auto event=Event();const char* why="";
    for(const char* input:{"train_Stop","a_completely_different_input","TRAIN_STOP",""}){
        auto relay=Prepare(source);auto incoming=event;incoming.useKey=input;wolf2009::TrainDoorUseEvent output;
        Need(!relay->Take(output)&&relay->SourceIncomingKey()=="train_Stop"&&relay->AuthoredKey()=="train_stop","Preparation dispatched or normalized raw source keys");
        Need(relay->CanAccept(incoming,why)&&relay->Accept(incoming,why)&&relay->AcceptedIncomingKey()==input,"Qualified relay did not retain exact accepted input");
        Need(relay->Take(output)&&output.sourceEntityName==event.targetName&&output.ownerEntityName=="train_greeter"&&output.useKey=="train_stop"&&
            output.levelGeneration==event.levelGeneration&&output.eventId==event.eventId,"Relay forwarded incoming/case-folded data instead of own key");
        Need(!relay->Accept(incoming,why)&&!relay->Take(output),"Consumed source relay request replayed");
    }
    auto relay=Prepare(source);wolf2009::TrainDoorUseEvent output;
    for(unsigned n=0;n<7;++n){auto wrong=event;
        if(n==0)++wrong.levelGeneration;if(n==1)wrong.eventId=0;if(n==2)++wrong.sourceNodeId;if(n==3)wrong.sourceName="another_mover";
        if(n==4)wrong.targetName="another_relay";if(n==5)wrong.kind=WolfSourceEvent::RemoveTarget;if(n==6)wrong.useKey=std::string(1025,'x');
        Need(!relay->CanAccept(wrong,why)&&!relay->Accept(wrong,why)&&!relay->Take(output),"Wrong generation/source/node/event mutated relay request");}
    Need(relay->Accept(event,why)&&relay->Take(output)&&output.useKey=="train_stop","Rejected input consumed the genuine later event");
    {auto mutated=Own();mutated.entities[2031].properties.clear();Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();mutated.entities[2034].properties.clear();Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();mutated.entities.push_back(mutated.entities[2034]);Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();mutated.entities.push_back(mutated.entities[2034]);Replace(mutated.entities.back(),"name","TRAIN_GREETER");Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();Replace(mutated.entities[2031],"target","train_greeter_brainuse_1");Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();mutated.entities[2031].properties.push_back(Text("target1","another_target"));Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();Replace(mutated.entities[2034],"classname","target_brainuse");Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();Replace(mutated.entities[2031],"useKey","Train_Stop");Rejects([&]{Prepare(mutated);});}
    {auto mutated=Own();mutated.entities.push_back(mutated.entities[2031]);Rejects([&]{Prepare(mutated);});}
    Rejects([&]{Wolf2009_PrepareSourceBrainUseRelay(WolfSourceRelayPolicy::Unspecified,source,2031,"starting_boxcar_model",6,"train_Stop",19);});
    Rejects([&]{Wolf2009_PrepareSourceBrainUseRelay(WolfSourceRelayPolicy::MeasuredTrainyardOwnLiteralV1,source,2030,"starting_boxcar_model",6,"train_Stop",19);});
    Need(source.entities[2031].Find("useKey")->text=="train_stop"&&event.useKey=="train_Stop","Relay changed original owning source data");
    std::cout<<"SOURCE_RELAY_PORTABLE_PASS checks="<<checks<<" own_key=1 no_casefold=1 once=1 missing_duplicate_recursive_refused=1 actor_ack=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"SOURCE_RELAY_PORTABLE_FAIL checks="<<checks<<" reason="<<error.what()<<'\n';return 1;}}

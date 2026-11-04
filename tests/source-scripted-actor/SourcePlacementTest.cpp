// SPDX-License-Identifier: GPL-3.0-or-later
// Data-only production preflight: no native engine, model or shader startup.
#include "NativeSourceScriptedActor.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>
namespace {
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
wolf2009::EntityProperty* Find(wolf2009::EntityRecord& source,const char* key){for(auto& field:source.properties)if(field.key==key)return &field;throw std::runtime_error("Placement fixture field absent");}
bool Same(const wolf2009::EntityRecord& a,const wolf2009::EntityRecord& b){if(a.properties.size()!=b.properties.size())return false;
    for(std::size_t i=0;i<a.properties.size();++i){const auto& x=a.properties[i];const auto& y=b.properties[i];
        if(x.key!=y.key||x.kind!=y.kind||x.text!=y.text||x.integer!=y.integer||x.sourceType!=y.sourceType||x.sourceIndex!=y.sourceIndex||x.numbers.size()!=y.numbers.size()||
           (!x.numbers.empty()&&std::memcmp(x.numbers.data(),y.numbers.data(),x.numbers.size()*sizeof(float))))return false;}return true;}
wolf2009::EntityRecord Source(){wolf2009::EntityRecord value;
    wolf2009::EntityProperty origin,rotation,angle;origin.key="origin";origin.kind=wolf2009::EntityProperty::Floats;origin.sourceType=1;origin.numbers={7,-11,23};
    rotation.key="rotation";rotation.kind=wolf2009::EntityProperty::Floats;rotation.sourceType=1;rotation.numbers={1,0,0,0,1,0,0,0,1};
    angle.key="angle";angle.kind=wolf2009::EntityProperty::Integer;angle.sourceType=4;angle.integer=0;value.properties={origin,rotation,angle};return value;}
void Accepted(const wolf2009::EntityRecord& source){const auto before=source;const auto actual=wolfSourceScriptedActorPlacement::Qualify(source);
    Check(Same(before,source),"Placement preflight rewrote raw owning ENTS properties");const auto* origin=source.Find("origin");const auto* rotation=source.Find("rotation");
    Check(actual.hasOrigin&&actual.hasRotation&&origin&&rotation,"Placement preflight invented absent placement");
    for(unsigned i=0;i<3;++i)Check(actual.origin[i]==origin->numbers[i],"Placement preflight changed source origin");
    for(unsigned i=0;i<9;++i)Check(actual.rotation[i]==rotation->numbers[i],"Placement preflight changed source matrix");
    Check(actual.hasAngle==(source.Find("angle")!=NULL)&&(!actual.hasAngle||actual.angle==0),"Placement preflight erased or changed redundant angle fact");}
void Refused(const wolf2009::EntityRecord& source){const auto before=source;bool rejected=false;try{wolfSourceScriptedActorPlacement::Qualify(source);}catch(const std::runtime_error&){rejected=true;}
    Check(rejected&&Same(before,source),"Ambiguous placement was accepted or its source facts changed");}
void Run(const wolf2009::EntityRecord& original){Accepted(original);
    for(int angle:{1,-1,90,-180,(std::numeric_limits<int>::max)()}){auto value=original;Find(value,"angle")->integer=angle;Refused(value);}
    {auto value=original;auto* angle=Find(value,"angle");angle->kind=wolf2009::EntityProperty::Floats;angle->sourceType=3;angle->numbers={0};Refused(value);}
    {auto value=original;auto* angle=Find(value,"angle");angle->kind=wolf2009::EntityProperty::Text;angle->text="0";Refused(value);}
    {auto value=original;Find(value,"rotation")->numbers={0,1,0,-1,0,0,0,0,1};Refused(value);}
    {auto value=original;Find(value,"rotation")->numbers[0]=std::nextafter(1.f,2.f);Refused(value);}
    {auto value=original;auto duplicate=*Find(value,"angle");duplicate.key="ANGLE";value.properties.push_back(duplicate);Refused(value);}
    {auto value=original;value.properties.push_back(*Find(value,"rotation"));Refused(value);}
    {auto value=original;value.properties.erase(std::remove_if(value.properties.begin(),value.properties.end(),[](const wolf2009::EntityProperty& field){return field.key=="rotation";}),value.properties.end());Refused(value);}
    {auto value=original;Find(value,"origin")->numbers[0]=std::numeric_limits<float>::infinity();Refused(value);}
    {auto value=original;Find(value,"rotation")->numbers[8]=-1;Refused(value);}
    // An explicitly stored rigid matrix without an angle keeps the existing
    // full-matrix policy; zero-angle presence must not broaden precedence.
    {auto value=original;value.properties.erase(std::remove_if(value.properties.begin(),value.properties.end(),[](const wolf2009::EntityProperty& field){return field.key=="angle";}),value.properties.end());
        Find(value,"rotation")->numbers={0,1,0,-1,0,0,0,0,1};Accepted(value);}
}
void Original(const char* path){std::ifstream input(path,std::ios::binary);Check(bool(input),"Owner-supplied ENTS file absent");std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    Check(!bytes.empty()&&bytes.size()<=(64u<<20),"Original placement ENTS byte budget");const auto entities=wolf2009::DecodeEntities4(bytes);Check(entities.entities.size()>2034,"Original source greeter ENTS index absent");
    const auto& source=entities.entities[2034];const auto* name=source.Find("name");const auto* angle=source.Find("angle");const auto* rotation=source.Find("rotation");
    Check(name&&name->kind==wolf2009::EntityProperty::Text&&name->text=="train_greeter","Expected original greeter record absent");
    Check(angle&&angle->kind==wolf2009::EntityProperty::Integer&&angle->sourceType==4&&angle->integer==0&&rotation&&rotation->numbers.size()==9,
        "Actual greeter no longer has the independently measured Integer0 plus matrix profile");
    for(unsigned i=0;i<9;++i)Check(rotation->numbers[i]==(i%4==0?1.f:0.f),"Actual greeter source matrix is not the measured identity");
    Run(source);std::cout<<"SCRIPTED_ACTOR_ORIGINAL_PLACEMENT_OK ents_bytes="<<bytes.size()<<" source_index=2034 raw_angle_type=4 integer_angle=0 identity_matrix=1 no_native_model_startup=1\n";
}
}
int main(int argc,char** argv){try{Run(Source());if(argc==3&&std::string(argv[1])=="--entities")Original(argv[2]);else Check(argc==1,"Usage: [--entities owner-supplied-decoded-ENTS]");
    std::cout<<"SCRIPTED_ACTOR_PLACEMENT_OK checks="<<checks<<" redundant_integer_zero_only=1 raw_facts_unchanged=1 ambiguous_refused=1\n";return 0;
}catch(const std::exception& error){std::cerr<<"SCRIPTED_ACTOR_PLACEMENT_FAIL checks="<<checks<<" reason="<<error.what()<<'\n';return 1;}}

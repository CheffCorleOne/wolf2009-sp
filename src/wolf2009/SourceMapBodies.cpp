// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceMapBodies.h"
#include "HavokPack.h"
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 { namespace {
void Need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
std::uint64_t Hash(const std::vector<unsigned char>& bytes){
    std::uint64_t h=UINT64_C(14695981039346656037);
    for(unsigned char c:bytes){h^=c;h*=UINT64_C(1099511628211);}return h;
}
void Limits(const SourceMapBodiesLimits& l){
    Need(l.hkxsBytes>=208&&l.objects&&l.fixups&&l.bodies&&l.bodies<=l.objects&&
        l.nameBytes&&l.entities.entities&&l.entities.properties,
        "Invalid source map body limits");
}
void Name(const std::string& value,std::size_t& count,const SourceMapBodiesLimits& limits){
    Need(value.size()<=4096&&count<=limits.nameBytes&&value.size()<=limits.nameBytes-count,
        "Source map body/name byte budget exceeded");
    for(unsigned char c:value)Need(c!=0,"Embedded NUL in source map body/entity name");
    count+=value.size();
}
bool NameKey(const std::string& text){
    if(text.size()!=4)return false;const char expected[]="name";
    for(unsigned i=0;i<4;++i){unsigned char c=text[i];if(c>='A'&&c<='Z')c+=32;if(c!=expected[i])return false;}return true;
}
std::string EntityName(const EntityRecord& entity){
    const EntityProperty* found=NULL;
    for(const auto& property:entity.properties)if(NameKey(property.key)){
        Need(!found,"Duplicate source ENTS name property");found=&property;}
    if(!found)return std::string();
    Need(found->kind==EntityProperty::Text,"Source ENTS name is not text");return found->text;
}
bool ResourceName(const std::string& name){
    return name.compare(0,7,"models/")==0||name.compare(0,4,"gfx/")==0;
}
typedef std::map<std::string,std::vector<std::size_t>> Names;
}

SourceMapBodyInventory ReadSourceMapBodyInventory(const std::vector<unsigned char>& bytes,
    const SourceMapBodiesLimits& limits){
    Limits(limits);detail::HavokPack pack(bytes,limits.hkxsBytes,limits.objects,limits.fixups);
    SourceMapBodyInventory result;std::size_t nameBytes=0;
    for(const auto& object:pack.objects)if(object.second=="hkpRigidBody"){
        Need(result.bodies.size()<limits.bodies,"Source rigid body count exceeds limit");
        const std::uint32_t at=object.first;pack.DataRange(at,512);SourceMapBodyRecord body;
        body.sourceBodyOffset=at;body.rootShapeOffset=pack.Pointer(at+16);
        body.rootShapeType=pack.Type(body.rootShapeOffset);body.rawFilterInfo=pack.DU(at+44);
        const auto name=pack.pointers.find(at+112);
        if(name!=pack.pointers.end()){body.hasName=true;body.rawName=pack.DataString(name->second);}
        Name(body.rawName,nameBytes,limits);body.motionStateFinite=true;
        for(unsigned i=0;i<32;++i){body.rawMotionStateWords[i]=pack.DU(at+224+i*4);float value;
            std::memcpy(&value,&body.rawMotionStateWords[i],sizeof(value));
            if(!std::isfinite(value))body.motionStateFinite=false;}
        result.bodies.push_back(std::move(body));
    }
    Need(!result.bodies.empty(),"Source HKXS contains no rigid bodies");
    result.source=bytes;result.sourceBytes=bytes.size();result.sourceFingerprint=Hash(bytes);return result;
}

SourceMapBodyPlan BindSourceMapBodies(const SourceMapBodyInventory& inventory,const EntityDocument& entities,
    const std::string& world,const SourceMapBodiesLimits& limits){
    Limits(limits);Need(!world.empty(),"Explicit source world body name required");
    Need(!inventory.bodies.empty()&&inventory.bodies.size()<=limits.bodies,
        "Source map binding body count exceeds limit/empty");
    Need(inventory.source.size()<=limits.hkxsBytes,"Source map binding HKXS byte budget");
    if(!inventory.source.empty())Need(inventory.sourceBytes==inventory.source.size()&&
        inventory.sourceFingerprint==Hash(inventory.source),"Source body inventory byte identity differs");
    Need(entities.entities.size()<=limits.entities.entities,"Source map binding ENTS count exceeds limit");
    std::size_t nameBytes=0,properties=0;Name(world,nameBytes,limits);
    Names bodyNames,entityNames;std::set<std::uint32_t> offsets;
    for(std::size_t i=0;i<inventory.bodies.size();++i){const auto& body=inventory.bodies[i];
        Need(offsets.insert(body.sourceBodyOffset).second,"Duplicate source rigid body offset");
        Need(body.hasName||body.rawName.empty(),"Unnamed source body carries invented name");
        Name(body.rawName,nameBytes,limits);
        if(body.hasName&&!body.rawName.empty())bodyNames[body.rawName].push_back(i);
    }
    for(std::size_t i=0;i<entities.entities.size();++i){const auto& entity=entities.entities[i];
        Need(properties<=limits.entities.properties&&entity.properties.size()<=limits.entities.properties-properties,
            "Source map binding property count exceeds limit");properties+=entity.properties.size();
        const std::string name=EntityName(entity);Name(name,nameBytes,limits);
        if(!name.empty())entityNames[name].push_back(i);
    }
    const auto selected=bodyNames.find(world);
    Need(selected!=bodyNames.end()&&selected->second.size()==1,"Exact source world body is absent or ambiguous");
    SourceMapBodyPlan result;result.inventory=inventory;result.entities=entities;
    result.selectedWorldName=world;result.selectedWorldBody=selected->second.front();
    result.bindings.resize(inventory.bodies.size());
    result.entityBodyIndices.resize(entities.entities.size(),static_cast<std::size_t>(-1));
    for(std::size_t i=0;i<inventory.bodies.size();++i){const auto& body=inventory.bodies[i];auto& binding=result.bindings[i];
        if(i==result.selectedWorldBody){binding.role=SourceMapBodyRole::SelectedWorld;continue;}
        const auto match=body.hasName&&!body.rawName.empty()?entityNames.find(body.rawName):entityNames.end();
        if(match!=entityNames.end()){
            Need(match->second.size()==1&&bodyNames.at(body.rawName).size()==1,
                "Ambiguous exact source body/ENTS-name binding");
            binding.role=SourceMapBodyRole::ExactEntityName;binding.entityIndex=match->second.front();
            result.entityBodyIndices[binding.entityIndex]=i;++result.entityBodies;
        }else if(body.hasName&&ResourceName(body.rawName)){
            binding.role=SourceMapBodyRole::ResourceNameUnbound;++result.resourceNameBodies;
        }else ++result.otherUnboundBodies;
    }
    for(const auto& group:bodyNames)if(group.second.size()>1)result.ambiguousUnboundBodyNames.push_back(group.first);
    for(const auto& group:entityNames)if(group.second.size()>1)result.ambiguousUnboundEntityNames.push_back(group.first);
    return result;
}

SourceMapBodyPlan DecodeSourceMapBodies(const std::vector<unsigned char>& hkxs,
    const std::vector<unsigned char>& ents,const std::string& exactWorldBodyName,const SourceMapBodiesLimits& limits){
    const auto inventory=ReadSourceMapBodyInventory(hkxs,limits);
    const auto entities=DecodeEntities4(ents,limits.entities);
    auto plan=BindSourceMapBodies(inventory,entities,exactWorldBodyName,limits);
    plan.entsSource=ents;plan.entsBytes=ents.size();plan.entsFingerprint=Hash(ents);return plan;
}
}

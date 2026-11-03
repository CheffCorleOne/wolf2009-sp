// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupportGameplayMap.h"
#include "RoundedConvex.h"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool value,const char* reason) { if(!value)throw std::runtime_error(reason); }
std::uint64_t Fingerprint(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash=14695981039346656037ull;
    for(auto byte:bytes){hash^=byte;hash*=1099511628211ull;}return hash;
}
bool Name(const std::string& text) {
    if(text.empty()||text.size()>255)return false;
    for(unsigned char c:text)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
std::string Lower(std::string text) {for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
}
std::vector<SupportGameplayResponse> AdmitSupportGameplayWorld(
    const SupportCollisionWorld& world,SupportGameplayContentsPolicy policy) {
    Need(policy==SupportGameplayContentsPolicy::DeclaredDoom3BitsV1,"Unknown native support contents policy");
    Need(world.selectedGeometryComplete&&world.selectedHierarchyComplete&&world.selectedContentsComplete&&
         world.selectedChildFiltersComplete&&world.selectedContactMetadataComplete&&world.selectedCoverageComplete,
         "Source support world is incomplete for native gameplay");
    Need(world.allSourceRigidBodiesSelected&&world.otherSourceRigidBodies==0,
         "Additional source rigid bodies require explicit native bindings");
    Need(!world.shapes.empty()&&world.shapes.size()<=300000,"Native support world shape budget exceeded/empty");
    std::vector<SupportGameplayResponse> result;result.reserve(world.shapes.size());
    std::map<std::uint64_t,bool> seen;
    for(const auto& shape:world.shapes) {
        Need(shape.source.occurrence&&seen.emplace(shape.source.occurrence,true).second,
             "Native support occurrence identity is missing or duplicated");
        Need(shape.filter.inheritedContentsKnown&&
             shape.filter.contentsProvenance==SupportCollisionContentsProvenance::SelectedBodyCompoundValue,
             "Unmapped source support contents");
        Need(shape.kind!=SupportCollisionKind::Cylinder,
             "Cylinder envelope does not qualify native contact response");
        Need(shape.kind!=SupportCollisionKind::Triangle||shape.triangle.contactMetadataQualified,
             "Unqualified source triangle contact metadata");
        const auto bits=shape.filter.inheritedRawContents;
        Need(bits!=4096,"Source trigger requires an explicitly bound native entity");
        SupportGameplayResponse response;response.contents=bits;
        response.material=GameplayCollisionMaterial(bits); // rejects unknown combinations
        RoundedConvexShape physical;physical.corePoints=shape.corePoints;physical.radius=shape.radius;
        Need(RoundedConvexBounds(physical,shape.placement).valid,"Native support shape/pose exceeds solver profile");
        result.push_back(std::move(response));
    }
    return result;
}
SupportGameplayMap PrepareSupportGameplayMap(const std::vector<unsigned char>& hkxs,
    const std::vector<unsigned char>& ents,const std::string& canonicalMapName,
    std::uint64_t mountEpoch,const std::string& requestedStart) {
    Need(mountEpoch,"Source support map requires a current mount epoch");
    GameplayMapSourcePaths paths;
    Need(ResolveGameplayMapPath(canonicalMapName+".map",paths),"Invalid source support map name");
    const auto marker=paths.collision.find("/HKXS/");
    Need(marker!=std::string::npos,"Source support HKXS path is not typed");
    SupportGameplayMap plan;plan.canonicalMapName=Lower(canonicalMapName);plan.mountEpoch=mountEpoch;
    plan.mountLabel=plan.canonicalMapName.substr(9,plan.canonicalMapName.find('/',9)-9);
    const auto body=paths.collision.substr(marker+6,paths.collision.size()-(marker+6)-5);
    const auto source=DecodeWorldCollisionGeometry(hkxs,body);
    const auto triangles=DecodeWorldTriangleGeometry(hkxs,source);
    std::map<std::uint32_t,HavokPrimitiveKind> unique;
    for(const auto& omission:source.omissions) {
        HavokPrimitiveKind kind;
        if(omission.type=="hkpSphereShape")kind=HavokPrimitiveKind::Sphere;
        else if(omission.type=="hkpCapsuleShape")kind=HavokPrimitiveKind::Capsule;
        else if(omission.type=="hkpCylinderShape")kind=HavokPrimitiveKind::Cylinder;
        else continue;
        const auto inserted=unique.emplace(omission.sourceShapeOffset,kind);
        Need(inserted.second||inserted.first->second==kind,"Conflicting source leaf identity");
    }
    std::vector<HavokPrimitiveSelection> selection;
    for(const auto& leaf:unique)selection.emplace_back(leaf.first,leaf.second);
    // Default unresolved-cylinder interpretation remains explicit here.
    const auto primitives=DecodeHavokPrimitiveSupport(hkxs,selection);
    plan.world=BuildSupportCollisionWorld(source,triangles,primitives);
    plan.responses=AdmitSupportGameplayWorld(plan.world,SupportGameplayContentsPolicy::DeclaredDoom3BitsV1);
    const auto entities=DecodeEntities4(ents);
    const auto candidates=CollectPlayerStarts(entities);
    const auto choice=SelectDiagnosticPlayerStart(candidates,requestedStart);
    Need(choice.selected,PlayerStartSelectionReasonText(choice.reason));
    const auto& candidate=candidates.at(choice.candidateIndex);
    const auto start=ImportGameplayPlayerStart(candidate);
    Need(Name(start.name),"Unsupported native player-start name");
    plan.sourceEntities=entities.entities.size();
    for(std::size_t i=0;i<entities.entities.size();++i) {
        if(i==candidate.entityIndex)continue;
        const auto* type=entities.entities[i].Find("classname");
        if(type&&type->kind==EntityProperty::Text&&Lower(type->text)=="worldspawn")continue;
        ++plan.unimplementedEntities;
    }
    std::ostringstream identity;
    identity.imbue(std::locale::classic());
    identity<<"support1-doom3bits1:"<<std::hex<<mountEpoch<<':'<<plan.world.sourceFingerprint<<':'<<hkxs.size()<<':'
            <<Fingerprint(ents)<<':'<<ents.size()<<':'<<plan.world.sourceBodyOffset<<':'<<plan.world.rootShapeOffset;
    plan.identity=identity.str(); // byte fingerprint/cache identity, not authentication
    std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(17);
    text<<"Version 2\n{\n\"classname\" \"worldspawn\"\n\"name\" \"world\"\n"
        <<"\"gravity\" \"1066\"\n\"wolf2009_support_identity\" \""<<plan.identity<<"\"\n}\n"
        <<"{\n\"classname\" \"info_player_start\"\n\"name\" \""<<start.name<<"\"\n"
        <<"\"origin\" \""<<start.origin.x<<' '<<start.origin.y<<' '<<start.origin.z<<"\"\n\"rotation\" \"";
    for(int k=0;k<9;++k){if(k)text<<' ';text<<start.axis[k];}
    text<<"\"\n}\n";plan.mapText=text.str();
    plan.cmText="CM \"1.00\"\n0\ncollisionModel \"worldMap\" {\nvertices { 0 }\n"
        "edges { 1 ( 0 0 ) 0 0 }\nnodes { ( -1 0 ) }\npolygons { }\nbrushes { }\n}\n";
    return plan;
}
}

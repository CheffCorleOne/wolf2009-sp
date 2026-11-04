// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupportGameplayMap.h"
#include "RoundedConvex.h"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <set>
#include <tuple>
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
typedef std::tuple<std::uint64_t,std::uint64_t,std::uint32_t,std::uint32_t,std::size_t,std::size_t> FeatureKey;
FeatureKey Key(const SupportCollisionIdentity& source){return FeatureKey(source.occurrence,source.sourceRegion,source.sourceShapeOffset,source.sourceRecordOffset,source.sourceSubpartIndex,source.triangleIndex);}
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
    std::set<FeatureKey> seen;
    for(const auto& shape:world.shapes) {
        Need(shape.source.occurrence&&seen.insert(Key(shape.source)).second,
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
std::vector<SupportGameplayResponse> AdmitSelectedSupportGameplayBody(
    const SupportCollisionWorld& world,SupportGameplayContentsPolicy policy){
    if(policy==SupportGameplayContentsPolicy::DeclaredDoom3BitsV1)return AdmitSupportGameplayWorld(world,policy);
    const bool solid=policy==SupportGameplayContentsPolicy::NativeSolidEntityCylinderEnvelopeV1;
    Need(solid||policy==SupportGameplayContentsPolicy::NativeSourceWordsCylinderEnvelopeV1,"Unknown selected body response policy");
    Need(world.selectedGeometryComplete&&world.selectedHierarchyComplete&&world.selectedChildFiltersComplete&&world.selectedContactMetadataComplete,
         "Selected source body geometry/filter/contact metadata is incomplete");
    Need(!world.shapes.empty()&&world.shapes.size()<=300000,"Selected source body shape budget");
    Need(world.sourceUnsupported.count("unverified-storage-contents")==0,"Selected body has an unverified storage-material layout");
    for(const auto& diagnostic:world.unresolved)Need(diagnostic.first=="unmapped-triangle-contents"||
        (solid&&diagnostic.first=="unknown-primitive-contents"),"Selected body retains an unadmitted source diagnostic");
    if(solid)Need(world.rootRawFilterKnown&&world.rootRawFilterInfo==0&&world.selectedBodyName.compare(0,7,"models/")==0,
        "Explicit native solid entity requires a zero-filter model template");
    for(const auto& omission:world.sourceOmissions)Need(omission.resolution!=SupportCollisionOmissionResolution::Unresolved,"Selected body retains an unresolved source shape");
    std::vector<SupportGameplayResponse> result;result.reserve(world.shapes.size());
    std::set<FeatureKey> seen;
    for(const auto& shape:world.shapes){
        Need(shape.source.occurrence&&seen.insert(Key(shape.source)).second,"Selected body occurrence identity is missing/duplicated");
        if(shape.kind==SupportCollisionKind::Cylinder)Need(shape.cylinder.profile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope,
            "Explicit native cylinder envelope profile required");
        std::uint32_t word=shape.filter.inheritedRawContents;
        if(shape.kind==SupportCollisionKind::Triangle){
            Need(!solid&&shape.triangle.contactMetadataQualified&&shape.filter.childFilterKnown&&shape.filter.childFilterStatus==HavokTriangleQueryStatus::Resolved,
                "Selected triangle has unknown response metadata");
            word=shape.filter.rawChildFilterInfo;
            if(!word){Need(world.rootRawFilterKnown,"Zero child filter has no retained root");word=world.rootRawFilterInfo;}
        }else if(!solid)Need(shape.filter.inheritedContentsKnown,"Selected primitive source word is unknown");
        if(solid){Need(word==0,"Native zero-template entity policy cannot override authored nonzero words");word=3;}
        Need(word&&!(word&~0x03ffffffu),"Unqualified source contents word");
        Need(!(word&4096u),"Source triggers require an entity binding");
        RoundedConvexShape physical;physical.corePoints=shape.corePoints;physical.radius=shape.radius;
        Need(RoundedConvexBounds(physical,shape.placement).valid,"Selected source shape has invalid native bounds");
        SupportGameplayResponse response;response.contents=word;std::ostringstream name;name.imbue(std::locale::classic());
        name<<"wolf2009/collision/source-word-v1/"<<std::hex<<std::setw(8)<<std::setfill('0')<<word;response.material=name.str();result.push_back(std::move(response));
    }return result;
}
SupportCollisionWorld DecodeSelectedSupportGameplayBody(const std::vector<unsigned char>& hkxs,
    const std::string& body,SupportGameplayContentsPolicy policy,bool local){
    WorldCollisionGeometryLimits limits;limits.namedLocalBodyShapeFrame=local;
    const auto source=DecodeWorldCollisionGeometry(hkxs,body,limits);
    const auto triangles=DecodeWorldTriangleGeometry(hkxs,source);
    std::map<std::uint32_t,HavokPrimitiveKind> unique;
    for(const auto& omission:source.omissions){HavokPrimitiveKind kind;
        if(omission.type=="hkpSphereShape")kind=HavokPrimitiveKind::Sphere;
        else if(omission.type=="hkpCapsuleShape")kind=HavokPrimitiveKind::Capsule;
        else if(omission.type=="hkpCylinderShape")kind=HavokPrimitiveKind::Cylinder;else continue;
        const auto inserted=unique.emplace(omission.sourceShapeOffset,kind);Need(inserted.second||inserted.first->second==kind,"Conflicting selected leaf identity");}
    std::vector<HavokPrimitiveSelection> selection;for(const auto& leaf:unique)selection.emplace_back(leaf.first,leaf.second);
    const auto profile=policy==SupportGameplayContentsPolicy::DeclaredDoom3BitsV1?HavokCylinderGeometryProfile::Unresolved:HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope;
    return BuildSupportCollisionWorld(source,triangles,DecodeHavokPrimitiveSupport(hkxs,selection,profile));
}
SupportGameplayMap PrepareSupportGameplayMap(const std::vector<unsigned char>& hkxs,
    const std::vector<unsigned char>& ents,const std::string& canonicalMapName,
    std::uint64_t mountEpoch,const std::string& requestedStart,SupportGameplayContentsPolicy policy) {
    Need(mountEpoch,"Source support map requires a current mount epoch");
    GameplayMapSourcePaths paths;
    Need(ResolveGameplayMapPath(canonicalMapName+".map",paths),"Invalid source support map name");
    const auto marker=paths.collision.find("/HKXS/");
    Need(marker!=std::string::npos,"Source support HKXS path is not typed");
    SupportGameplayMap plan;plan.canonicalMapName=Lower(canonicalMapName);plan.mountEpoch=mountEpoch;
    plan.mountLabel=plan.canonicalMapName.substr(9,plan.canonicalMapName.find('/',9)-9);
    const auto body=paths.collision.substr(marker+6,paths.collision.size()-(marker+6)-5);
    Need(policy!=SupportGameplayContentsPolicy::NativeSolidEntityCylinderEnvelopeV1,"Entity override policy is not a map-world policy");
    plan.contentsPolicy=policy;plan.nativeCylinderEnvelopeResponse=policy!=SupportGameplayContentsPolicy::DeclaredDoom3BitsV1;
    plan.world=DecodeSelectedSupportGameplayBody(hkxs,body,policy);
    if(policy!=SupportGameplayContentsPolicy::DeclaredDoom3BitsV1){
        plan.bodies=std::make_shared<SourceMapBodyPlan>(DecodeSourceMapBodies(hkxs,ents,body));
        Need(plan.bodies->otherUnboundBodies==0&&plan.bodies->inventory.sourceFingerprint==plan.world.sourceFingerprint&&
            plan.bodies->inventory.bodies.at(plan.bodies->selectedWorldBody).sourceBodyOffset==plan.world.sourceBodyOffset,
            "Selected map world has an unresolved/foreign body ownership plan");
    }
    plan.responses=AdmitSelectedSupportGameplayBody(plan.world,policy);
    const auto entities=plan.bodies?plan.bodies->entities:DecodeEntities4(ents);
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
    identity<<(policy==SupportGameplayContentsPolicy::DeclaredDoom3BitsV1?"support1-doom3bits1:":"support2-sourcewords-envelope16:")<<std::hex<<mountEpoch<<':'<<plan.world.sourceFingerprint<<':'<<hkxs.size()<<':'
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

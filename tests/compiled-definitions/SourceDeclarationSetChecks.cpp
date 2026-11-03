// SPDX-License-Identifier: GPL-3.0-or-later
// Runs inside the GPL game DSO. The two plans qualify the reader mechanism;
// neither is silently selected as the retail global/map lifecycle.
#include "CompiledDeclarationNamespace.h"
#include "SourceDefinitions.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationRuntime.h"
#include "SourceDeclarationSetRuntime.h"
namespace {
using namespace wolf2009;
struct Stats {std::size_t checks=0,refusals=0;};
void Need(Stats& s,bool value,const char* why){++s.checks;if(!value)throw std::runtime_error(why);}
WolfSourceDeclarationSetRequest Plan(const WolfSourceDeclarationPacket& map,const WolfSourceDeclarationPacket& global,bool protectGlobal){
    WolfSourceDeclarationSetRequest request;request.policy=WolfSourceDeclarationSet_OrdinaryOwnerSectionsV1;
    for(const auto* packet:{&global,&map})for(std::size_t section=0;section<2;++section){WolfSourceDeclarationSetStep step;
        step.packet=packet;step.section=section;step.mode=packet==&global&&protectGlobal?WolfSourceDeclarationSection_ProtectAccepted:WolfSourceDeclarationSection_Ordinary;
        request.steps.push_back(step);}return request;
}
const SourceResolvedLiteral& Field(Stats& s,const ResolvedSourceDefinition& value,const char* key){const auto* field=value.Find(key);Need(s,field!=NULL,"Effective native declaration field missing");return *field;}
}
std::vector<wolf2009::SourceResolvedLiteral> Wolf2009_CheckSourceDeclarationSetQA(const WolfSourceDeclarationPacket& map,const WolfSourceDeclarationPacket& global){
    Stats s;idStr reason;
    const auto guardedPlan=Plan(map,global,true),ordinaryPlan=Plan(map,global,false);
    auto guarded=Wolf2009_CreateSourceDeclarationSet(guardedPlan,reason);Need(s,guarded.get()!=NULL&&reason.IsEmpty(),reason.IsEmpty()?"Native guarded declaration set failed":reason.c_str());
    auto ordinary=Wolf2009_CreateSourceDeclarationSet(ordinaryPlan,reason);Need(s,ordinary.get()!=NULL&&reason.IsEmpty(),reason.IsEmpty()?"Native ordinary declaration set failed":reason.c_str());
    for(const auto* set:{guarded.get(),ordinary.get()}){const auto& counts=set->Counts();const auto& declarations=set->Namespace();
        Need(s,counts.steps==4&&counts.selected==4890&&counts.entityDefinitions==1555&&counts.modelDefinitions==48&&counts.otherDeclarations==3287&&
            set->Definitions().DefinitionCount()==1603&&declarations.DecisionCount()==6515&&declarations.ApplicationCount()==4,"Native effective declaration cardinalities differ");
        for(std::size_t i=0;i<declarations.SelectedCount();++i)Need(s,declarations.Selected(i).IsUnmodified(),"Native selected namespace entry lost its seal");
    }
    std::size_t protectedSkips=0,ordinaryReplacements=0;
    for(std::size_t i=0;i<guarded->Namespace().DecisionCount();++i){
        protectedSkips+=guarded->Namespace().Decision(i).kind==CompiledNamespaceDecisionKind::SkippedProtected;
        ordinaryReplacements+=ordinary->Namespace().Decision(i).kind==CompiledNamespaceDecisionKind::AcceptedReplacement;}
    Need(s,protectedSkips==1621&&ordinaryReplacements==1625,"Native effective source owner/protected decisions differ");
    const auto protectedStream=guarded->Definitions().Resolve(SourceDefinitionKind::EntityDef,"STREAMTECH.def");
    const auto ordinaryStream=ordinary->Definitions().Resolve(SourceDefinitionKind::EntityDef,"streamtech");
    const auto& a=Field(s,protectedStream,"map_heap_size");const auto& b=Field(s,ordinaryStream,"map_heap_size");
    Need(s,a.encoding==SourceLiteralEncoding::CompiledSignedInteger&&a.numericWords==std::vector<std::uint32_t>({140})&&
        b.encoding==SourceLiteralEncoding::CompiledString&&b.value.size()==1&&b.value[0].text=="140"&&b.numericWords.empty(),
        "Native effective namespace coerced a genuine typed conflict");
    Need(s,a.origins.size()==1&&b.origins.size()==1&&a.origins[0].origin.member.compiledRecord->source->role==CompiledCatalogRole::Global&&
        b.origins[0].origin.member.compiledRecord->source->role==CompiledCatalogRole::Map,"Native explicit plans lost selected global/map provenance");
    const auto container=guarded->Definitions().Resolve(SourceDefinitionKind::EntityDef,"CONTAINER_LOCKER_FOOT_01.entitydef");
    const auto& delay=Field(s,container,"giveDelay");
    Need(s,delay.encoding==SourceLiteralEncoding::CompiledSignedInteger&&delay.numericWords==std::vector<std::uint32_t>({1200})&&
        Field(s,container,"spawnclass").value[0].text=="rvAnimatedContainer"&&!container.gameplaySupported,"Native effective container class/defaults changed");
    const auto model=guarded->Definitions().ResolveModelAnimations("MODEL_ENV_LOCKER_FOOT_01_ANIM.model");
    Need(s,model.references.size()==6&&model.Find("open")&&model.Find("open")->assetReference.text=="models/props/locker_foot_01_anim_open.md5anim"&&
        !model.gameplaySupported,"Native effective model reference changed or claimed gameplay");
    const auto reject=[&](const WolfSourceDeclarationSetRequest& request){idStr failure="must-be-replaced";auto result=Wolf2009_CreateSourceDeclarationSet(request,failure);
        Need(s,!result&&!failure.IsEmpty()&&failure!="must-be-replaced","Invalid native declaration plan published a result or omitted its reason");
        Need(s,guarded->Counts().selected==4890&&guarded->Definitions().DefinitionCount()==1603&&ordinary->Counts().selected==4890&&
            ordinary->Definitions().DefinitionCount()==1603,"Refused declaration set changed existing sets");++s.refusals;};
    WolfSourceDeclarationSetRequest bad;reject(bad);bad.policy=WolfSourceDeclarationSet_OrdinaryOwnerSectionsV1;reject(bad);
    bad=guardedPlan;bad.steps[0].packet=NULL;reject(bad);bad=guardedPlan;bad.steps[0].section=2;reject(bad);
    bad=guardedPlan;bad.steps[0].mode=WolfSourceDeclarationSection_Unspecified;reject(bad);
    bad=guardedPlan;bad.steps[0].mode=static_cast<WolfSourceDeclarationSectionMode>(99);reject(bad);
    bad=guardedPlan;bad.steps.resize(257,bad.steps[0]);reject(bad);
    std::vector<SourceResolvedLiteral> facts{a,b,delay};guarded.reset();ordinary.reset();
    for(const auto& fact:facts)Need(s,!fact.origins.empty()&&fact.origins[0].origin.member.compiledRecord&&
        fact.origins[0].origin.member.compiledRecord->IsUnmodified(),"Native resolved set fact borrowed the set's lifetime");
    printf("WOLF2009_NATIVE_SOURCE_DECLARATION_SET_QA_OK checks=%zu refusals=%zu selected=4890 decisions=6515 entitydefs=1555 models=48 imports=1603 protected_skips=%zu ordinary_replacements=%zu explicit_plans=2 typed_conflict_retained=1 owning_after_set=1 retail_schedule_claim=0 native_registry_import=0 gameplay=0\n",
        s.checks,s.refusals,protectedSkips,ordinaryReplacements);
    return facts;
}

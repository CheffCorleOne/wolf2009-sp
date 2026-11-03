// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarationNamespace.h"
#include "SourceDefinitions.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"
namespace {
using namespace wolf2009;
struct Stats {std::size_t checks=0,refusals=0;};
void Need(Stats& s,bool value,const char* why){++s.checks;if(!value)throw std::runtime_error(why);}
void Input(WolfSourceDeclarationRequest& out,const char* path,const char* identity,WolfSourceDeclarationRole role){
    out.resourcePath=path;out.loadIdentity=identity;out.role=role;out.policy=WolfSourceDeclaration_SelectedTypedPacketV1;
}
}
std::vector<wolf2009::SourceResolvedLiteral> Wolf2009_CheckSourceDeclarationStartupQA(const char* map,const char* global,const char* mapLocale,const char* globalLocale){
    Stats s;WolfSourceDeclarationStartupRequest request;request.policy=WolfSourceDeclarationStartup_CompiledLocalizedV1;
    Input(request.globalLocale,globalLocale,"startup.global.locale",WolfSourceDeclaration_SelectedGlobal);
    Input(request.globalMain,global,"startup.global.main",WolfSourceDeclaration_SelectedGlobal);
    Input(request.mapLocale,mapLocale,"startup.map.locale",WolfSourceDeclaration_SelectedMap);
    Input(request.mapMain,map,"startup.map.main",WolfSourceDeclaration_SelectedMap);
    idStr reason;auto set=Wolf2009_LoadSourceDeclarationStartup(request,reason);
    Need(s,set.get()!=NULL&&reason.IsEmpty(),reason.IsEmpty()?"Native compiled startup failed":reason.c_str());
    const auto& counts=set->Counts();const auto& names=set->Namespace();
    Need(s,counts.steps==7&&counts.selected==5687&&counts.entityDefinitions==1555&&counts.modelDefinitions==48&&counts.otherDeclarations==4084&&
        set->Definitions().DefinitionCount()==1603&&names.ApplicationCount()==7&&names.DecisionCount()==7317,"Native four-input startup cardinalities differ");
    const std::size_t section[]={0,0,1,0,1,0,1};
    const char* identities[]={"startup.global.locale","startup.global.main","startup.global.main","startup.map.locale","startup.map.locale","startup.map.main","startup.map.main"};
    const std::size_t begins[]={8,4072,1450880,8,188404,5876,1422636};
    for(std::size_t i=0;i<7;++i){const auto app=names.Application(i);
        Need(s,app.section==section[i]&&app.source->loadIdentity==identities[i]&&app.mode==(i<3?CompiledNamespaceReaderMode::ProtectAccepted:CompiledNamespaceReaderMode::Ordinary)&&
            app.document->sections[section[i]].countSpan.begin==begins[i],"Native startup section order/mode/source span differs from the original trace");}
    const auto gl=names.Application(0).document,ml=names.Application(3).document;
    Need(s,gl->sourceBytes==49906&&gl->sourceFingerprint==UINT64_C(0x845a8a07071ae26e)&&gl->pools[0].words.empty()&&gl->pools[1].words.empty()&&gl->sections[1].groups.empty(),"Native global locale identity differs");
    Need(s,ml->sourceBytes==188406&&ml->sourceFingerprint==UINT64_C(0xe675921b8a577fd7)&&ml->pools[0].words.empty()&&ml->pools[1].words.empty()&&ml->sections[1].groups.empty(),"Native map locale identity differs");
    std::size_t lip=0,protectedSkips=0,replaced=0,localeProtected=0;
    for(std::size_t i=0;i<names.SelectedCount();++i){const auto entry=names.Selected(i);Need(s,entry.IsUnmodified(),"Native startup lost selected source seal");if(entry.type=="lipSync")++lip;}
    for(std::size_t i=0;i<names.DecisionCount();++i){const auto d=names.Decision(i);protectedSkips+=d.kind==CompiledNamespaceDecisionKind::SkippedProtected;
        replaced+=d.kind==CompiledNamespaceDecisionKind::AcceptedReplacement;localeProtected+=d.kind==CompiledNamespaceDecisionKind::SkippedProtected&&d.typeKey=="lipsync";}
    Need(s,lip==797&&protectedSkips==1626&&replaced==4&&localeProtected==5,"Native startup ordinary/protected/localized selection differs");
    const auto voice=names.Lookup("lipSync","trn_9605_izet");Need(s,voice.status==CompiledCatalogLookupStatus::Unique&&voice.matches.size()==1&&
        voice.matches[0].source->loadIdentity=="startup.global.locale"&&voice.matches[0].document==gl,"Native map locale replaced an originally protected global lipSync");
    const auto stream=set->Definitions().Resolve(SourceDefinitionKind::EntityDef,"streamtech");const auto* heap=stream.Find("map_heap_size");
    Need(s,heap&&heap->encoding==SourceLiteralEncoding::CompiledSignedInteger&&heap->numericWords==std::vector<std::uint32_t>({140})&&
        heap->origins[0].origin.member.compiledRecord->source->loadIdentity=="startup.global.main"&&!stream.gameplaySupported,"Native startup selected/coerced the wrong streamtech value");
    const auto container=set->Definitions().Resolve(SourceDefinitionKind::EntityDef,"container_locker_foot_01");const auto* delay=container.Find("giveDelay");
    Need(s,delay&&delay->numericWords==std::vector<std::uint32_t>({1200})&&delay->origins[0].origin.member.compiledRecord->source->loadIdentity=="startup.map.main","Native startup lost the map container defaults");
    const auto reject=[&](const WolfSourceDeclarationStartupRequest& bad){idStr failure="must-change";auto refused=Wolf2009_LoadSourceDeclarationStartup(bad,failure);
        Need(s,!refused&&!failure.IsEmpty()&&failure!="must-change"&&set->Counts().selected==5687,"Native startup refusal published a result/changed an existing namespace");++s.refusals;};
    auto bad=request;bad.policy=WolfSourceDeclarationStartup_Unspecified;reject(bad);bad=request;bad.policy=static_cast<WolfSourceDeclarationStartupPolicy>(99);reject(bad);
    bad=request;bad.globalLocale.role=WolfSourceDeclaration_SelectedMap;reject(bad);bad=request;bad.mapLocale.role=WolfSourceDeclaration_SelectedGlobal;reject(bad);
    bad=request;bad.mapMain.loadIdentity=bad.globalMain.loadIdentity;reject(bad);bad=request;bad.mapMain.policy=WolfSourceDeclaration_Unspecified;reject(bad);
    bad=request;bad.globalLocale.resourcePath="fixtures/source-startup-missing.decls";reject(bad);
    bad=request;bad.mapLocale.resourcePath="wolf2009/qa2_locale/DECL/known-missing-startup";reject(bad);
    std::vector<SourceResolvedLiteral> facts{*heap,*delay};set.reset();
    Need(s,facts[0].origins[0].origin.member.compiledRecord->IsUnmodified()&&facts[1].origins[0].origin.member.compiledRecord->IsUnmodified()&&
        voice.matches[0].IsUnmodified()&&voice.matches[0].document->sourceBytes==49906,"Startup facts lost ownership after all input/set handles died");
    printf("WOLF2009_NATIVE_SOURCE_DECLARATION_STARTUP_QA_OK checks=%zu refusals=%zu packets=4 applications=7 selected=5687 decisions=7317 entitydefs=1555 models=48 lipsync=797 protected_skips=1626 model_replacements=4 localized_protected=5 imports=1603 observed_schedule=1 owning_after_inputs_and_set=1 native_registry_import=0 gameplay=0\n",s.checks,s.refusals);
    return facts;
}

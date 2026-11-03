// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarationNamespace.h"
#include "SourceDefinitions.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationRuntime.h"
#include "SourceDeclarationSetRuntime.h"
namespace {
const std::size_t MaximumSteps=256,MaximumSelectedDefinitions=20000;
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::string Fold(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
wolf2009::SourceDefinitionLimits DefinitionLimits(){
    wolf2009::SourceDefinitionLimits limits;limits.members=MaximumSelectedDefinitions;limits.declarations=MaximumSelectedDefinitions;
    limits.totalSourceBytes=128u<<20;limits.storedBytes=128u<<20;return limits;
}
}
struct WolfSourceDeclarationSet::Impl {
    wolf2009::CompiledDeclarationNamespace declarations;
    wolf2009::SourceDefinitions definitions;
    WolfSourceDeclarationSetCounts counts;
    Impl():declarations(wolf2009::CompiledNamespacePolicy::OrdinaryTextOwnersV1),
        definitions(DefinitionLimits(),wolf2009::SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1){}
};
WolfSourceDeclarationSet::WolfSourceDeclarationSet(std::unique_ptr<Impl> data):impl_(std::move(data)){}
WolfSourceDeclarationSet::~WolfSourceDeclarationSet(){}
const wolf2009::CompiledDeclarationNamespace& WolfSourceDeclarationSet::Namespace()const{return impl_->declarations;}
const wolf2009::SourceDefinitions& WolfSourceDeclarationSet::Definitions()const{return impl_->definitions;}
const WolfSourceDeclarationSetCounts& WolfSourceDeclarationSet::Counts()const{return impl_->counts;}
std::unique_ptr<WolfSourceDeclarationSet> Wolf2009_CreateSourceDeclarationSet(const WolfSourceDeclarationSetRequest& request,idStr& reason){
    reason.Clear();
    try{
        Need(request.policy==WolfSourceDeclarationSet_OrdinaryOwnerSectionsV1,"an explicit OrdinaryOwnerSectionsV1 source declaration set policy is required");
        Need(!request.steps.empty()&&request.steps.size()<=MaximumSteps,"source declaration set step count budget");
        // Validate the entire borrowed request before interpreting its sections.
        // The published set keeps only owning immutable catalog entries.
        for(const auto& step:request.steps){
            Need(step.packet!=NULL,"source declaration set step has no selected packet");
            Need(step.section<2,"source declaration set section is outside the checked two-section format");
            Need(step.mode==WolfSourceDeclarationSection_Ordinary||step.mode==WolfSourceDeclarationSection_ProtectAccepted,
                 "source declaration set step requires an explicit reader mode");
            Need(step.packet->Catalog().LoadUnitCount()==1,"source declaration set step requires one selected packet load unit");
        }
        std::unique_ptr<WolfSourceDeclarationSet::Impl> data(new WolfSourceDeclarationSet::Impl);
        for(const auto& step:request.steps){
            data->declarations.ApplySection(step.packet->Catalog(),0,step.section,
                step.mode==WolfSourceDeclarationSection_ProtectAccepted?wolf2009::CompiledNamespaceReaderMode::ProtectAccepted:
                wolf2009::CompiledNamespaceReaderMode::Ordinary);
        }
        auto& counts=data->counts;counts.steps=request.steps.size();counts.selected=data->declarations.SelectedCount();
        for(std::size_t i=0;i<counts.selected;++i){const auto entry=data->declarations.Selected(i);const auto type=Fold(entry.type);
            if(type=="entitydef")++counts.entityDefinitions;else if(type=="model")++counts.modelDefinitions;else ++counts.otherDeclarations;}
        Need(counts.entityDefinitions<=MaximumSelectedDefinitions&&counts.modelDefinitions<=MaximumSelectedDefinitions-counts.entityDefinitions,
             "source declaration set selected entity/model count budget");
        for(std::size_t i=0;i<counts.selected;++i){const auto entry=data->declarations.Selected(i);const auto type=Fold(entry.type);
            if(type=="entitydef"||type=="model")data->definitions.AddCompiledRecord(entry);}
        Need(data->definitions.DefinitionCount()==counts.entityDefinitions+counts.modelDefinitions,"source declaration set frontend import count differs");
        return std::unique_ptr<WolfSourceDeclarationSet>(new WolfSourceDeclarationSet(std::move(data)));
    }catch(const std::exception& error){reason=error.what();}
    catch(...){reason="source declaration set failed with a native exception";}
    return std::unique_ptr<WolfSourceDeclarationSet>();
}
std::unique_ptr<WolfSourceDeclarationSet> Wolf2009_LoadSourceDeclarationStartup(const WolfSourceDeclarationStartupRequest& request,idStr& reason){
    reason.Clear();
    try{
        Need(request.policy==WolfSourceDeclarationStartup_CompiledLocalizedV1,"an explicit CompiledLocalizedV1 startup policy is required");
        const WolfSourceDeclarationRequest* inputs[]={&request.globalLocale,&request.globalMain,&request.mapLocale,&request.mapMain};
        for(std::size_t i=0;i<4;++i){
            Need(inputs[i]->policy==WolfSourceDeclaration_SelectedTypedPacketV1,"startup inputs require explicit selected packet policies");
            Need(inputs[i]->role==(i<2?WolfSourceDeclaration_SelectedGlobal:WolfSourceDeclaration_SelectedMap),
                 "startup inputs require two Global followed by two Map roles");
            for(std::size_t j=0;j<i;++j)Need(inputs[i]->loadIdentity!=inputs[j]->loadIdentity,"startup packets require distinct load identities");
        }
        std::unique_ptr<WolfSourceDeclarationPacket> packets[4];
        for(std::size_t i=0;i<4;++i){packets[i]=Wolf2009_LoadSourceDeclarations(*inputs[i],reason);
            Need(packets[i].get()!=NULL,reason.IsEmpty()?"startup packet load failed":reason.c_str());}
        Need(packets[0]->Catalog().Document(0)->sections[1].groups.empty(),"qualified startup requires an empty unconsumed global-locale second section");
        WolfSourceDeclarationSetRequest plan;plan.policy=WolfSourceDeclarationSet_OrdinaryOwnerSectionsV1;
        for(std::size_t i=0;i<4;++i)for(std::size_t section=0;section<(i==0?1u:2u);++section){
            WolfSourceDeclarationSetStep step;step.packet=packets[i].get();step.section=section;
            step.mode=i<2?WolfSourceDeclarationSection_ProtectAccepted:WolfSourceDeclarationSection_Ordinary;plan.steps.push_back(step);}
        auto result=Wolf2009_CreateSourceDeclarationSet(plan,reason);
        Need(result.get()!=NULL,reason.IsEmpty()?"startup namespace selection failed":reason.c_str());return result;
    }catch(const std::exception& error){reason=error.what();}
    catch(...){reason="source declaration startup failed with a native exception";}
    return std::unique_ptr<WolfSourceDeclarationSet>();
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceActorBinding.h"
#include "EngineAnimationSet.h"
#include "SourceSoundBinding.h"
#include "SourceEntityRuntime.h"
#include "EngineSourceAF.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"

std::shared_ptr<const WolfSourceActorBinding> Wolf2009_CheckSourceActorBindingQA(
    const WolfSourceDeclarationSet&,const char* exactMapDeclPath);
bool Wolf2009_CheckSourceEntityQA(const WolfSourceDeclarationSet&,
    std::shared_ptr<const WolfSourceActorBinding>,const char* exactEntitiesPath);
bool Wolf2009_CheckNativeSourceAFQA(const WolfSourceDeclarationSet&,
    std::shared_ptr<const WolfSourceActorBinding>,const char*,const WolfSourceAFRequest&);
namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void Input(WolfSourceDeclarationRequest& out,const char* path,const char* name,WolfSourceDeclarationRole role){
    out.resourcePath=path;out.loadIdentity=name;out.role=role;out.policy=WolfSourceDeclaration_SelectedTypedPacketV1;
}
struct Owners {
    const int before=Wolf2009_LiveSourceEntities();
    std::shared_ptr<const WolfSourceActorBinding> binding;
    ~Owners(){
        if(!binding)return;
        if(Wolf2009_LiveSourceEntities()!=before){
            fprintf(stderr,"WOLF2009_SOURCE_ACTOR_QA_CLEANUP_DEFERRED live_source_entities=1\n");return;
        }
        const std::string model=binding->NativeModelDefinition();
        std::vector<std::string> sounds;for(const auto& sound:binding->Sounds())sounds.push_back(sound.nativeSoundAlias);
        binding.reset();
        if(const auto* decl=declManager->FindDeclWithoutParsing(DECL_MODELDEF,model.c_str(),false)){
            auto* native=const_cast<idDeclModelDef*>(static_cast<const idDeclModelDef*>(decl));native->FreeData();native->Invalidate();
        }
        Wolf2009_ForgetSourceAnimationSet(model.c_str());
        for(const auto& name:sounds)if(const auto* decl=declManager->FindDeclWithoutParsing(DECL_SOUND,name.c_str(),false)){
            auto* native=const_cast<idDecl*>(decl);native->FreeData();native->Invalidate();
        }
        animationLib.FlushUnusedAnims();
    }
};
}
int Wolf2009_CheckSourceActorRuntimeQA(const char* map,const char* global,const char* mapLocale,const char* globalLocale){
    try{
        WolfSourceDeclarationStartupRequest request;request.policy=WolfSourceDeclarationStartup_CompiledLocalizedV1;
        Input(request.globalLocale,globalLocale,"actor.global.locale",WolfSourceDeclaration_SelectedGlobal);
        Input(request.globalMain,global,"actor.global.main",WolfSourceDeclaration_SelectedGlobal);
        Input(request.mapLocale,mapLocale,"actor.map.locale",WolfSourceDeclaration_SelectedMap);
        Input(request.mapMain,map,"actor.map.main",WolfSourceDeclaration_SelectedMap);
        idStr reason;auto declarations=Wolf2009_LoadSourceDeclarationStartup(request,reason);
        Need(bool(declarations),reason.IsEmpty()?"source actor startup failed":reason.c_str());
        Owners owners;owners.binding=Wolf2009_CheckSourceActorBindingQA(*declarations,map);
        Need(bool(owners.binding),"source actor native binding QA returned no owner");
        Need(owners.binding->Sounds().size()==2,"source actor QA requires both original sound bindings");
        for(const auto& sound:owners.binding->Sounds())Need(sound.owner&&sound.owner->MatchesNative(reason),"source actor native sound cache lost an original sample");
        printf("WOLF2009_NATIVE_SOURCE_SOUND_QA_OK original_sfx=2 original_snds=2 nondefault_samples=2 headless_output_sink=1 retail_mixer=0\n");
        std::string ents=map;const auto type=ents.find("/decl/");Need(type!=std::string::npos,"source actor QA requires canonical typed map DECL path");ents.replace(type,6,"/ENTS/");
        Need(Wolf2009_CheckSourceEntityQA(*declarations,owners.binding,ents.c_str()),"source entity native lifecycle QA failed");
        Need(Wolf2009_LiveSourceEntities()==owners.before,"source entity QA leaked native actors");
        const std::string afPath=std::string(map).substr(0,type+1)+"HKXR/models/props/locker_foot_01_anim";
        WolfSourceAFRequest af;af.profile=WolfSourceAF_FixedRootWorldHingeNativeConservativeV1;
        af.exactResourcePath=afPath.c_str();af.authoredRagdollReference="models/props/locker_foot_01_anim.HKX";
        af.exactRootVariant="RagdollInstance";af.restAnimation="idle";
        af.expectedBytes=6688;af.expectedFingerprint=UINT64_C(0x69f1372d4a8d149c);
        af.nativeContents=CONTENTS_SOLID|CONTENTS_BODY;af.nativeClipMask=MASK_SOLID|CONTENTS_BODY;
        Need(Wolf2009_CheckNativeSourceAFQA(*declarations,owners.binding,ents.c_str(),af),"source AF native lifecycle QA failed");
        Need(Wolf2009_LiveSourceEntities()==owners.before,"source AF QA leaked native actors");
        printf("WOLF2009_NATIVE_SOURCE_ACTOR_RUNTIME_QA_OK actual_source_class=rvAnimatedContainer original_model=1 original_sounds=1 api8_extension=0 campaign=0\n");return 1;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_ACTOR_RUNTIME_FAIL reason=%s\n",error.what());return 0;}
}

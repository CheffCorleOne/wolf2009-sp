// SPDX-License-Identifier: GPL-3.0-or-later
// Core-side QA driver. The game probe is resolved from the already loaded GPL
// game module by the runtime host; only two POD strings cross that boundary.
#include "AnimationWire.h"
#include "EngineSkinnedModelData.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "EngineAnimationBinding.h"
#include "EngineAnimationQA.h"
namespace {
namespace wire=wolf2009::animation;
const char* const ModelPackage="maps/game/trainyard/trainyard.mpk";
const char* const SkeletonPackage="maps/game/global/global.spk";
const char* const Model="wolf2009/qa_anim/MODL/models/characters/civilians/civ_male_04/anims/body_cm04.md5r";
const char* const Animation="wolf2009/qa_anim/HKXA/maps/game/trainyard/trainyard";
const char* const Skeleton="wolf2009/qa_anim_skel/SKEL/maps/game/global/global";
const char* const OtherModel="wolf2009/qa_anim_other/MODL/models/characters/civilians/civ_male_04/anims/body_cm04.md5r";
struct Stats{std::size_t checks=0;};
void Need(Stats& s,bool value,const char* message){++s.checks;if(!value)throw std::runtime_error(message);}
struct CloseFile{void operator()(idFile* f)const{if(f)fileSystem->CloseFile(f);}};
typedef std::unique_ptr<idFile,CloseFile> File;
struct Resources{
    std::vector<idRenderModel*> models;
    std::vector<idStr> bindings;
    bool main=false,skeleton=false,other=false;
    bool cached=r_useCachedDynamicModels.GetBool();
    int show=r_showSkel.GetInteger();
    Resources(){r_useCachedDynamicModels.SetBool(true);r_showSkel.SetInteger(0);}
    void DeleteModels(){
        for(auto* model:models){renderModelManager->RemoveModel(model);renderModelManager->FreeModel(model);}
        models.clear();
    }
    ~Resources(){
        DeleteModels();for(const auto& b:bindings)Wolf2009_RemoveAnimationBinding(b);
        if(other&&!Wolf2009_Unmount("qa_anim_other"))fprintf(stderr,"WOLF2009_ANIMATION_CLEANUP_FAIL mount=qa_anim_other\n");
        if(main&&!Wolf2009_Unmount("qa_anim"))fprintf(stderr,"WOLF2009_ANIMATION_CLEANUP_FAIL mount=qa_anim\n");
        if(skeleton&&!Wolf2009_Unmount("qa_anim_skel"))fprintf(stderr,"WOLF2009_ANIMATION_CLEANUP_FAIL mount=qa_anim_skel\n");
        r_useCachedDynamicModels.SetBool(cached);r_showSkel.SetInteger(show);
    }
};
idRenderModel* Load(Stats& stats,Resources& resources,const char* path,idStr& alias){
    Need(stats,Wolf2009_RegisterSkinnedModel(path,Animation,Skeleton,0,alias),"normal source model registration failed for animator QA");
    idRenderModel* model=renderModelManager->FindModel(alias);
    if(model&&model!=renderModelManager->DefaultModel()&&std::find(resources.models.begin(),resources.models.end(),model)==resources.models.end())resources.models.push_back(model);
    Need(stats,model&&!model->IsDefaultModel()&&model->IsLoaded()&&model->NumJoints()==52&&model->IsDynamicModel()==DM_CACHED&&dynamic_cast<idRenderModelMD5*>(model),
         "normal model manager did not provide a qualified source MD5 model for animator QA");return model;
}
wire::Envelope Read(Stats& stats,idFile* file){
    Need(stats,file!=NULL,"ordinary filesystem did not route the source descriptor");const int length=file->Length();
    Need(stats,length>0&&std::size_t(length)<=wire::MaximumEnvelopeBytes&&file->Timestamp()==0,"invalid readonly source descriptor metadata");
    wire::Bytes bytes(static_cast<std::size_t>(length));
    Need(stats,file->Read(bytes.data(),length)==length&&file->Tell()==length,"short owned source descriptor read");
    unsigned char extra=0;Need(stats,file->Read(&extra,1)==0,"source descriptor did not stop at exact EOF");
    const auto result=wire::Parse(bytes);
    Need(stats,file->Seek(0,FS_SEEK_SET)==0&&file->Tell()==0&&file->Seek(-1,FS_SEEK_SET)==-1&&file->Tell()==0,
         "readonly descriptor seek range/cursor contract failed");return result;
}
}
bool Wolf2009_RunAnimationChecks(Wolf2009AnimatorQaEntry gameProbe){
    Stats stats;
    try{
        Need(stats,gameProbe!=NULL,"GPL game source animator QA export is absent");
        Resources resources;const unsigned descriptors=Wolf2009_OpenAnimationDescriptors();
        Need(stats,Wolf2009_Mount("qa_anim",ModelPackage),"animator source MPK mount failed");resources.main=true;
        Need(stats,Wolf2009_Mount("qa_anim_skel",SkeletonPackage),"animator source SKEL mount failed");resources.skeleton=true;
        Need(stats,Wolf2009_Mount("qa_anim_other",ModelPackage),"independent identity-control MPK mount failed");resources.other=true;
        unsigned long long epoch=0,skelEpoch=0;
        Need(stats,Wolf2009_MountEpoch("qa_anim",epoch)&&Wolf2009_MountEpoch("qa_anim_skel",skelEpoch)&&epoch&&skelEpoch,"animator source epoch missing");
        idStr modelAlias,otherAlias;Load(stats,resources,Model,modelAlias);Load(stats,resources,OtherModel,otherAlias);
        idStr alias,repeat;
        Need(stats,Wolf2009_RegisterSourceAnimation(modelAlias,alias),"core source animation binding registration failed");resources.bindings.push_back(alias);
        Need(stats,Wolf2009_RegisterSourceAnimation(modelAlias,repeat)&&repeat==alias,"source animation full-key registration was not idempotent");
        {
            File file(fileSystem->OpenFileRead(alias,false));const auto envelope=Read(stats,file.get());
            Need(stats,envelope.binding.modelAlias==modelAlias.c_str()&&envelope.binding.sources[0].epoch==epoch&&envelope.binding.sources[2].epoch==skelEpoch,
                 "descriptor lost its exact qualified model/source epoch identity");
            Need(stats,Wolf2009_OpenAnimationDescriptors()==descriptors+1,"core did not own/count its readonly descriptor");
        }
        Need(stats,Wolf2009_OpenAnimationDescriptors()==descriptors,"closing idFile leaked an animation descriptor");
        Need(stats,gameProbe(alias.c_str(),otherAlias.c_str())==1,"actual native idMD5Anim/idAnimBlend/idAnimator checks failed");
        Need(stats,Wolf2009_OpenAnimationDescriptors()==descriptors,"native game anim/decl flush leaked an owned descriptor/lease");
        resources.DeleteModels();
        Need(stats,Wolf2009_Unmount("qa_anim_other"),"independent identity model did not release its source mount");resources.other=false;
        Need(stats,Wolf2009_Unmount("qa_anim"),"source native animator/model destruction left a model/HKXA mount pin");resources.main=false;
        Need(stats,Wolf2009_Unmount("qa_anim_skel"),"source native animator/model destruction left a skeleton pin");resources.skeleton=false;
        Need(stats,Wolf2009_Mount("qa_anim",ModelPackage),"animation source remount failed");resources.main=true;
        Need(stats,Wolf2009_Mount("qa_anim_skel",SkeletonPackage),"animation skeleton remount failed");resources.skeleton=true;
        unsigned long long next=0,nextSkel=0;
        Need(stats,Wolf2009_MountEpoch("qa_anim",next)&&Wolf2009_MountEpoch("qa_anim_skel",nextSkel)&&next!=epoch&&nextSkel!=skelEpoch,
             "source remount did not change both process-monotonic epochs");
        {
            File stale(fileSystem->OpenFileRead(alias,false));Need(stats,!stale,"old source animation alias silently rebound after source remount");
        }
        idStr freshModel,fresh;Load(stats,resources,Model,freshModel);
        Need(stats,freshModel!=modelAlias&&Wolf2009_RegisterSourceAnimation(freshModel,fresh)&&fresh!=alias,"fresh source epoch did not produce new model/animation aliases");
        resources.bindings.push_back(fresh);
        {
            File owned(fileSystem->OpenFileRead(fresh,false));const auto before=Read(stats,owned.get());
            resources.DeleteModels();
            Need(stats,Wolf2009_RemoveAnimationBinding(fresh),"scoped source animation registration removal failed");
            File absent(fileSystem->OpenFileRead(fresh,false));Need(stats,!absent,"removed source binding was still discoverable");
            const auto after=Read(stats,owned.get());
            Need(stats,wire::Same(before.binding,after.binding)&&before.animation==after.animation&&before.skeleton==after.skeleton,
                 "removing registry entry invalidated the independent descriptor request/payload");
            Need(stats,!Wolf2009_Unmount("qa_anim")&&!Wolf2009_Unmount("qa_anim_skel"),"owned descriptor failed to retain both source leases after model purge/registration removal");
            Need(stats,Wolf2009_OpenAnimationDescriptors()==descriptors+1,"owned source descriptor lifetime changed across registry removal");
        }
        Need(stats,Wolf2009_OpenAnimationDescriptors()==descriptors,"descriptor destruction did not restore the open count");
        Need(stats,Wolf2009_Unmount("qa_anim"),"final descriptor close did not release model/HKXA source pin");resources.main=false;
        Need(stats,Wolf2009_Unmount("qa_anim_skel"),"final descriptor close did not release skeleton pin");resources.skeleton=false;
        Need(stats,Wolf2009_RemoveAnimationBinding(alias),"old immutable animation registration cleanup failed");
        printf("WOLF2009_ACTUAL_ENGINE_ANIMATION_QA_OK checks=%zu game_animator=1 wire_v1=1 alias_full_key=1 model_identity=1 "
               "descriptor_owns_payload=1 scoped_remove=1 same_epoch_reload=1 stale_epoch_refused=1 remount_new_alias=1 source_pins_released=1 "
               "api8_extension=0 retail_actor_clock=0 gpu=0\n",stats.checks);
        return true;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_ENGINE_ANIMATION_FAIL checks=%zu message=%s\n",stats.checks,error.what());}
    catch(...){fprintf(stderr,"WOLF2009_ENGINE_ANIMATION_FAIL checks=%zu message=native-exception\n",stats.checks);}
    return false;
}

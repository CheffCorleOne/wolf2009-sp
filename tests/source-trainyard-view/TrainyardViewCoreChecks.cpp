// SPDX-License-Identifier: GPL-3.0-or-later
// Capture the real trainyard player camera; no detached render world or warps.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <dlfcn.h>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "game/Game.h"
#include "framework/async/AsyncNetwork.h"
#include "../engine-render/EngineOffscreenFrame.h"
#include "TrainyardViewCoreChecks.h"

namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
typedef int (*GameProbe)(int);
struct Module {
    void* handle=NULL;GameProbe probe=NULL;bool begun=false;
    ~Module(){if(begun&&probe)probe(WolfTrainyardViewQa_End);if(handle)dlclose(handle);}
    void Call(int action){Need(probe&&probe(action)==1,"trainyard game-view QA action refused");}
};
typedef std::vector<unsigned char> Pixels;
struct DrawState {int wagonSurfaces=0,wagonIndexes=0;};
DrawState Frame(Pixels& pixels,int wagonEntity,bool visible){
    Need(game&&renderSystem&&renderSystem->IsOpenGLRunning(),"trainyard view needs initialized native game/GLES");
    Need(qglGetError()==GL_NO_ERROR,"trainyard view entered with a prior GL error");
    int front=0,back=0;DrawState result;
    renderSystem->BeginFrame(Wolf2009_QAFrameWidth,Wolf2009_QAFrameHeight);
    const bool drawn=game->Draw(0);const int total=tr.primaryView?tr.primaryView->numDrawSurfs:0;
    if(tr.primaryView)for(int i=0;i<tr.primaryView->numDrawSurfs;++i){const drawSurf_t* surface=tr.primaryView->drawSurfs[i];
        if(surface&&surface->space&&surface->space->entityDef&&surface->space->entityDef->parms.entityNum==wagonEntity){
            ++result.wagonSurfaces;if(surface->geo)result.wagonIndexes+=surface->geo->numIndexes;}}
    renderSystem->EndFrame(&front,&back);
    Need(drawn&&total>0,"ordinary trainyard game->Draw produced no game camera");
    Need(visible?(result.wagonSurfaces>0&&result.wagonIndexes>0):(result.wagonSurfaces==0&&result.wagonIndexes==0),
        visible?"original source wagon has no draw surfaces in the actual player camera":"hidden wagon still has actual game-camera draw surfaces");
    qglFinish();GLint draw=-1,read=-1;qglGetIntegerv(GL_FRAMEBUFFER_BINDING,&draw);qglGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    Need(draw==0&&read==0,"trainyard readback is not the actual EGL pbuffer framebuffer");
    pixels.resize(Wolf2009_QAFrameBytes);qglPixelStorei(GL_PACK_ALIGNMENT,4);
    qglReadPixels(0,0,Wolf2009_QAFrameWidth,Wolf2009_QAFrameHeight,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    const GLenum error=qglGetError();
    printf("WOLF2009_TRAINYARD_VIEW_GPU_FRAME game_draw=1 surfaces=%d wagon_visible=%d wagon_surfaces=%d wagon_indexes=%d gl_error=0x%x front_ms=%d back_ms=%d\n",
        total,int(visible),result.wagonSurfaces,result.wagonIndexes,unsigned(error),front,back);
    Need(error==GL_NO_ERROR,"trainyard native game framebuffer readback has a GL error");return result;
}
unsigned Difference(const Pixels& a,const Pixels& b,int threshold){
    Need(a.size()==b.size()&&a.size()==Wolf2009_QAFrameBytes,"trainyard pixel arrays changed shape");unsigned changed=0;
    for(std::size_t i=0;i<a.size();i+=4){const int delta=std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]));if(delta>threshold)++changed;}return changed;
}
void Save(const char* path,const Pixels& pixels){Need(Wolf2009_WriteQaFrame(path,pixels.data(),int(pixels.size())),"trainyard game-camera BMP write failed");}
}
bool Wolf2009_RunTrainyardViewCoreChecks(){
    idFile* requested=fileSystem->OpenFileRead("fixtures/trainyard-session.enabled",false);if(!requested)return true;fileSystem->CloseFile(requested);
    try{
        Module module;const char* library=cvarSystem->GetCVarString("harm_fs_gameLibPath");
        Need(library&&library[0]=='/',"trainyard view requires the explicit already-loaded game library path");
        module.handle=dlopen(library,RTLD_NOW|RTLD_NOLOAD);Need(module.handle!=NULL,"trainyard view game module is not already loaded");
        module.probe=reinterpret_cast<GameProbe>(dlsym(module.handle,"Wolf2009_TrainyardViewQA"));Need(module.probe!=NULL,"trainyard view game QA export is absent");
        module.Call(WolfTrainyardViewQa_Begin);module.begun=true;
        const int wagonEntity=module.probe(WolfTrainyardViewQa_WagonEntityNumber);
        Need(wagonEntity>=MAX_ASYNC_CLIENTS&&wagonEntity<65536,"trainyard wagon entity is outside the ordinary non-client range");
        Pixels original,withoutWagon,restored;
        const DrawState shown=Frame(original,wagonEntity,true);Save("fixtures/offscreen-trainyard-player.bmp",original);module.Call(WolfTrainyardViewQa_CheckUnchanged);
        module.Call(WolfTrainyardViewQa_HideWagon);Frame(withoutWagon,wagonEntity,false);Save("fixtures/offscreen-trainyard-without-wagon.bmp",withoutWagon);module.Call(WolfTrainyardViewQa_CheckUnchanged);
        module.Call(WolfTrainyardViewQa_RestoreWagon);const DrawState again=Frame(restored,wagonEntity,true);Save("fixtures/offscreen-trainyard-restored.bmp",restored);module.Call(WolfTrainyardViewQa_CheckUnchanged);
        const unsigned changedPixels=Difference(original,withoutWagon,12),restoredPixels=Difference(original,restored,0);
        printf("WOLF2009_TRAINYARD_VIEW_GPU_AB changed_pixels=%u restored_rgb_pixels=%u visible_surfaces=%d restored_surfaces=%d\n",changedPixels,restoredPixels,shown.wagonSurfaces,again.wagonSurfaces);
        Need(shown.wagonSurfaces==again.wagonSurfaces&&shown.wagonIndexes==again.wagonIndexes,"trainyard restoration changed source wagon draw geometry");
        Need(changedPixels>64,"original source wagon has no meaningful geometry contribution in the actual game camera");
        Need(restoredPixels==0,"trainyard visibility A/B changed game/camera/time or did not restore the exact RGB frame");
        module.Call(WolfTrainyardViewQa_End);module.begun=false;
        printf("WOLF2009_TRAINYARD_VIEW_GPU_QA_OK actual_game_draw=1 original_model=1 original_skin=1 wagon_geometry_pixels=%u restored_rgb_pixels=0 same_simulation_time=1 texture_parity_claimed=0 full_campaign=0\n",changedPixels);
        return true;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_TRAINYARD_VIEW_GPU_QA_FAIL reason=%s\n",error.what());return false;}
}

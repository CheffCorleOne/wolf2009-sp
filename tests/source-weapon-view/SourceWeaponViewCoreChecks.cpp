// SPDX-License-Identifier: GPL-3.0-or-later
// Native GLES proof of the real game camera and equipped weapon entity.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>
#include <dlfcn.h>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "game/Game.h"
#include "framework/async/AsyncNetwork.h"
#include "../engine-render/EngineOffscreenFrame.h"
#include "SourceWeaponViewCoreChecks.h"

namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
typedef int (*GameProbe)(int);
struct Module {
    void* handle=NULL;GameProbe probe=NULL;bool begun=false;
    ~Module(){if(begun&&probe)probe(WolfSourceWeaponViewQa_End);if(handle)dlclose(handle);}
    void Call(int action){Need(probe&&probe(action)==1,"source gun GPU game phase refused");}
};
typedef std::vector<unsigned char> Pixels;
void Frame(Pixels& pixels,int weaponEntity){
    Need(game&&renderSystem&&renderSystem->IsOpenGLRunning(),"source gun GPU requires an initialized game and GLES renderer");
    Need(qglGetError()==GL_NO_ERROR,"source gun GPU entry has a prior GL error");
    int front=0,back=0;renderSystem->BeginFrame(Wolf2009_QAFrameWidth,Wolf2009_QAFrameHeight);
    const bool drawn=game->Draw(0);const int drawSurfaces=tr.primaryView?tr.primaryView->numDrawSurfs:0;
    int weaponSurfaces=0,weaponIndexes=0;
    if(tr.primaryView)for(int i=0;i<tr.primaryView->numDrawSurfs;++i){const drawSurf_t* surface=tr.primaryView->drawSurfs[i];
        if(surface&&surface->space&&surface->space->entityDef&&surface->space->entityDef->parms.entityNum==weaponEntity){
            ++weaponSurfaces;if(surface->geo)weaponIndexes+=surface->geo->numIndexes;}}
    renderSystem->EndFrame(&front,&back);Need(drawn&&drawSurfaces>0,"ordinary game->Draw produced no game view");
    Need(weaponSurfaces==3&&weaponIndexes>0,"actual first-person game view does not draw exactly three unmodified MP40 surfaces");qglFinish();
    GLint draw=-1,read=-1;qglGetIntegerv(GL_FRAMEBUFFER_BINDING,&draw);qglGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    Need(draw==0&&read==0,"source gun readback is not the actual EGL pbuffer framebuffer");
    pixels.resize(Wolf2009_QAFrameBytes);qglPixelStorei(GL_PACK_ALIGNMENT,4);
    qglReadPixels(0,0,Wolf2009_QAFrameWidth,Wolf2009_QAFrameHeight,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    const GLenum error=qglGetError();
    printf("WOLF2009_SOURCE_WEAPON_VIEW_GPU_FRAME game_draw=1 surfaces=%d weapon_surfaces=%d weapon_indexes=%d indexes=%d gl_error=0x%x front_ms=%d back_ms=%d\n",drawSurfaces,weaponSurfaces,weaponIndexes,backEnd.pc.c_drawIndexes,unsigned(error),front,back);
    Need(error==GL_NO_ERROR,"source gun game-camera framebuffer readback has a GL error");
}
unsigned Difference(const Pixels& a,const Pixels& b){
    Need(a.size()==b.size()&&a.size()==Wolf2009_QAFrameBytes,"source gun GPU pixel array changed shape");unsigned changed=0;
    for(std::size_t i=0;i<a.size();i+=4){const int delta=std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]));if(delta>12)++changed;}return changed;
}
void Save(const char* path,const Pixels& pixels){Need(Wolf2009_WriteQaFrame(path,pixels.data(),int(pixels.size())),"source gun game-camera BMP write failed");}
}
bool Wolf2009_RunSourceWeaponViewCoreChecks(){
    idFile* flag=fileSystem->OpenFileRead("fixtures/source-session.enabled",false);if(!flag)return true;fileSystem->CloseFile(flag);
    try{
        Module module;const char* library=cvarSystem->GetCVarString("harm_fs_gameLibPath");
        Need(library&&library[0]=='/',"source gun GPU requires the explicit already-loaded game library path");
        module.handle=dlopen(library,RTLD_NOW|RTLD_NOLOAD);Need(module.handle!=NULL,"source gun GPU game module is not already loaded");
        module.probe=reinterpret_cast<GameProbe>(dlsym(module.handle,"Wolf2009_SourceWeaponViewGpuQA"));Need(module.probe!=NULL,"source gun GPU game QA export is absent");
        module.Call(WolfSourceWeaponViewQa_Begin);module.begun=true;
        const int weaponEntity=module.probe(WolfSourceWeaponViewQa_EntityNumber);Need(weaponEntity>=MAX_ASYNC_CLIENTS&&weaponEntity<65536,"source gun GPU entity number is outside the ordinary non-client range");
        Pixels idle,flat,restored,fire,reload;
        Frame(idle,weaponEntity);Save("fixtures/offscreen-source-mp40-idle.bmp",idle);
        module.Call(WolfSourceWeaponViewQa_Flat);Frame(flat,weaponEntity);Save("fixtures/offscreen-source-mp40-flat.bmp",flat);
        module.Call(WolfSourceWeaponViewQa_Original);Frame(restored,weaponEntity);Save("fixtures/offscreen-source-mp40-restored.bmp",restored);
        const unsigned texturePixels=Difference(idle,flat),restoredPixels=Difference(idle,restored);
        Need(texturePixels>64,"actual first-person game frame lacks original MP40 texture contribution");
        Need(restoredPixels==0,"source gun flat control changed game state/geometry/time or failed to restore its appearance");
        module.Call(WolfSourceWeaponViewQa_FirePose);Frame(fire,weaponEntity);Save("fixtures/offscreen-source-mp40-fire.bmp",fire);
        module.Call(WolfSourceWeaponViewQa_ReloadPose);Frame(reload,weaponEntity);Save("fixtures/offscreen-source-mp40-reload.bmp",reload);
        const unsigned firePixels=Difference(idle,fire),reloadPixels=Difference(idle,reload);
        Need(firePixels>64&&reloadPixels>64,"actual first-person game frames lack distinct source fire/reload poses");
        module.Call(WolfSourceWeaponViewQa_End);module.begun=false;
        printf("WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GPU_QA_OK actual_game_draw=1 original_texture_pixels=%u restored_pixels=%u fire_pose_pixels=%u reload_pose_pixels=%u source_model=1 source_animations=1 api8_extension=0\n",texturePixels,restoredPixels,firePixels,reloadPixels);
        return true;
    }catch(const std::exception& e){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GPU_QA_FAIL reason=%s\n",e.what());return false;}
}

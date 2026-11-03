// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_ANIMATION_BINDING_V2_H
#define WOLF2009_ENGINE_ANIMATION_BINDING_V2_H
class idFile;
class idStr;
inline bool Wolf2009_IsSourceAnimationV2Alias(const char* path) {
    if(!path) return false;
    const char* prefix="wolf2009/_animation/anim/v2/";
    for(int i=0;prefix[i];++i) {
        char c=path[i];if(c=='\\')c='/';if(c>='A'&&c<='Z')c+='a'-'A';
        if(c!=prefix[i])return false;
    }
    return true;
}
// Core-only, game/model owner thread. Profile 1 is explicit ordinary visual
// full-local-pose identity mapping. Neither rig name is inferred from the other.
bool Wolf2009_RegisterSourceAnimationV2(const char* modelAlias,const char* hkxaPath,
    const char* animationSkeletonPath,const char* exactClipName,const char* exactAnimationSkeletonName,
    int explicitRigProfile,idStr& animationAlias);
// Called by the shared source-descriptor router before entering the v1 mutex.
idFile* Wolf2009_OpenSourceAnimationV2(const char* alias);
bool Wolf2009_RemoveAnimationBindingV2(const char* alias);
void Wolf2009_InitAnimationBindingsV2();
void Wolf2009_ShutdownAnimationBindingsV2();
unsigned Wolf2009_OpenAnimationDescriptorsV2();
#endif

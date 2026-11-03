// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_ANIMATION_GAME_H
#define WOLF2009_ENGINE_ANIMATION_GAME_H
#include "EngineAnimationBinding.h"
class Wolf2009SourceAnimation;
class idDeclModelDef;
class idStr;
void Wolf2009_DeleteSourceAnimation(Wolf2009SourceAnimation* source);
// Local to the GPL game module. The returned ordinary modelDef references the
// registered core model and the source animation alias; no API8 extension.
bool Wolf2009_CreateSourceModelDef(const char* animationAlias, const char* appearanceAlias, idStr& modelDefAlias);
void Wolf2009_ClearAnimationGameRegistry();
inline bool Wolf2009_IsSourceModelAlias(const char* path) {
    if (!path) return false;
    const char* prefix = "wolf2009/";
    int i = 0;
    for (; prefix[i]; ++i) {
        char c = path[i]; if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (c != prefix[i]) return false;
    }
    int begin = i;
    for (; path[i] && path[i] != '/'; ++i) {}
    if (i == begin || !path[i]) return false;
    ++i; const char* type = "md5r/";
    for (int n = 0; type[n]; ++n) {
        char c = path[i+n]; if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (c != type[n]) return false;
    }
    return true;
}
#endif

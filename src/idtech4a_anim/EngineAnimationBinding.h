// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_ANIMATION_BINDING_H
#define WOLF2009_ENGINE_ANIMATION_BINDING_H
class idFile;
class idStr;
inline bool Wolf2009_IsSourceAnimationAlias(const char* path) {
    if (!path) return false;
    const char* prefix = "wolf2009/_animation/anim/";
    for (int i = 0; prefix[i]; ++i) {
        char c = path[i]; if (c == '\\') c = '/'; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != prefix[i]) return false;
    }
    return true;
}
// Core-only producer. The game consumes a checked readonly idFile through its
// existing API8 filesystem import; it never links private core symbols/STL.
// Registration/open/removal follow the native model/game owner-thread contract.
bool Wolf2009_RegisterSourceAnimation(const char* modelAlias, idStr& animationAlias);
// Removes only this registration. Open readonly descriptors retain their own
// bytes/request/source leases and remain usable until the caller closes them.
bool Wolf2009_RemoveAnimationBinding(const char* animationAlias);
idFile* Wolf2009_OpenSourceAnimation(const char* animationAlias);
void Wolf2009_InitAnimationBindings();
void Wolf2009_ShutdownAnimationBindings();
unsigned Wolf2009_OpenAnimationDescriptors();
#endif

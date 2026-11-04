// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_ANIMATED_DOOR_CHECKS_H
#define WOLF2009_NATIVE_SOURCE_ANIMATED_DOOR_CHECKS_H
class WolfSourceDeclarationSet;
struct WolfSourceAnimatedDoorRequest;
class idEntity;
class idAnimatedEntity;
// Isolated native QA only. Receivers must be the real source wagon and newly
// owned source-controlled greeter. This executes the greeter's actual clip and
// ordinary game frames. The caller owns both and must not reuse a live campaign
// actor for this destructive-to-its-animation-state diagnostic.
bool Wolf2009_CheckNativeSourceAnimatedDoorQA(const WolfSourceDeclarationSet&,
    const WolfSourceAnimatedDoorRequest&,idEntity* sourceWagon,
    idAnimatedEntity* sourceGreeter);
#endif

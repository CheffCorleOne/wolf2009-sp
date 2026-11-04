// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_ANIMATED_DOOR_CHECKS_H
#define WOLF2009_NATIVE_SOURCE_ANIMATED_DOOR_CHECKS_H
#include <cstddef>
class WolfSourceDeclarationSet;
struct WolfSourceAnimatedDoorRequest;
class idEntity;
class idStr;
namespace wolf2009 { struct TrainDoorCommand; }
struct WolfSourceAnimatedDoorActorStartDriver {
    const void* context;
    // Adapter calls the actual scripted-actor owner's Start API. Returning
    // true alone proves nothing: every check also queries its private authority.
    bool (*start)(const void*,const wolf2009::TrainDoorCommand&,idStr&);
    WolfSourceAnimatedDoorActorStartDriver():context(NULL),start(NULL){}
};
// Isolated native QA only. Receivers must be the real source wagon and newly
// owned inactive scripted greeter authority. Executes its actual owner Start and
// ordinary game frames. The caller owns both and must not reuse a live campaign
// actor for this destructive-to-its-animation-state diagnostic.
bool Wolf2009_CheckNativeSourceAnimatedDoorQA(const WolfSourceDeclarationSet&,
    const WolfSourceAnimatedDoorRequest&,idEntity* sourceWagon,
    const WolfSourceAnimatedDoorActorStartDriver&);
// Separate fresh isolated fixtures: actual Start becomes stale before the
// first door acceptance; actual original actor is deleted/recreated under the
// same name/model/clip/time/serial. Replacement QA consumes the original native
// actor, so its caller must discard that actor owner after this case.
bool Wolf2009_CheckNativeSourceAnimatedDoorStaleStartQA(const WolfSourceDeclarationSet&,
    const WolfSourceAnimatedDoorRequest&,idEntity*,const WolfSourceAnimatedDoorActorStartDriver&);
bool Wolf2009_CheckNativeSourceAnimatedDoorReplacementQA(const WolfSourceDeclarationSet&,
    const WolfSourceAnimatedDoorRequest&,idEntity*,const WolfSourceAnimatedDoorActorStartDriver&);
#endif

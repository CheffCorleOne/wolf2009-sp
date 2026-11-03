// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_VISUAL_ENTITY_H
#define WOLF2009_SOURCE_VISUAL_ENTITY_H
// Game-module interface. Include the ordinary native precompiled/game headers
// first. This is deliberately not an API8 extension or a source ENTS translator.
class idEntity;
class idAnimatedEntity;

enum WolfSourceVisualPolicy {
    WolfSourceVisual_Unspecified=0,
    WolfSourceVisual_OneShotNativeMillisecondsV1=1
};
struct WolfSourceVisualRequest {
    idStr name;
    idStr modelDefinition;
    idVec3 origin;
    idMat3 axis;
    int startTimeMs;
    WolfSourceVisualPolicy policy;
    WolfSourceVisualRequest() : origin(0,0,0), axis(mat3_identity), startTimeMs(0),
        policy(WolfSourceVisual_Unspecified) {}
};
// Preflights the full request before allocating/registering a game entity. Only
// a private, constructed spawn dictionary reaches the native lifecycle. No clip
// model, controller/root extraction, script, head attachment or actor AI is added.
// Caller owns the returned native entity; use idEntityPtr for retained references.
// ModelDefs/animation cache own their normal refs after deletion and must be
// released/flushed by the owning level before unmounting source packages.
bool Wolf2009_SpawnSourceVisual(const WolfSourceVisualRequest& request,
                              idAnimatedEntity*& out, idStr& reason);
bool Wolf2009_IsSourceVisualEntity(const idEntity* entity);
int Wolf2009_LiveSourceVisualEntities();
#endif

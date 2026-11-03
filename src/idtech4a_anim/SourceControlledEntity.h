// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_CONTROLLED_ENTITY_H
#define WOLF2009_SOURCE_CONTROLLED_ENTITY_H
// Private game-module interface. Include native game headers first. There is
// no API8 layout/export change and no original interaction/state inference.
class idEntity;
class idAnimatedEntity;

enum WolfSourceControlledPolicy {
    WolfSourceControlled_Unspecified=0,
    WolfSourceControlled_NativeMillisecondsHoldLastV2=1
};
enum WolfSourceControlledPhase {
    WolfSourceControlled_Unbound=0,
    WolfSourceControlled_Held=1,
    WolfSourceControlled_Playing=2
};
struct WolfSourceControlledRequest {
    idStr name;
    idStr modelDefinition;
    idStr initialClip;
    int initialSourceFrame; // Zero-based source frame, not an ENTS frame.
    idVec3 origin;
    idMat3 axis;
    WolfSourceControlledPolicy policy;
    WolfSourceControlledRequest() : initialSourceFrame(0), origin(0,0,0),
        axis(mat3_identity), policy(WolfSourceControlled_Unspecified) {}
};
struct WolfSourceControlledState {
    WolfSourceControlledPhase phase;
    idStr clip;
    int sourceFrame; // Zero-based while Held; -1 while Playing.
    int commandTimeMs; // Last accepted external command on the game clock.
    int endTimeMs; // One-shot ceil end; -1 while Held.
    int blendEndTimeMs;
    unsigned commandSerial; // External commands only; auto hold preserves it.
    WolfSourceControlledState() : phase(WolfSourceControlled_Unbound), sourceFrame(-1),
        commandTimeMs(-1), endTimeMs(-1), blendEndTimeMs(-1), commandSerial(0) {}
};
// The bound set/model is immutable. Commands use gameLocal.time and preflight
// every request before touching native channels. blendMs is nonnegative; a
// PlayOnce blend may not exceed that clip's length. Only full-pose V2 is used.
bool Wolf2009_SpawnSourceControlled(const WolfSourceControlledRequest& request,
    idAnimatedEntity*& out, idStr& reason);
bool Wolf2009_SourceControlledHoldFrame(idAnimatedEntity* entity,const char* exactClip,
    int sourceFrame,int blendMs,idStr& reason);
bool Wolf2009_SourceControlledPlayOnce(idAnimatedEntity* entity,const char* exactClip,
    int blendMs,idStr& reason);
bool Wolf2009_SourceControlledState(const idAnimatedEntity* entity,
    WolfSourceControlledState& state,idStr& reason);
bool Wolf2009_IsSourceControlledEntity(const idEntity* entity);
int Wolf2009_LiveSourceControlledEntities();
// Caller owns the entity (retain idEntityPtr). Destroy entities before freeing
// their native set/model declarations; flush unused animations before unmount.
// Forgetting set discovery does not invalidate a bound entity's owned metadata
// or its additional native animation refs. Save/restore remains unsupported.
#endif

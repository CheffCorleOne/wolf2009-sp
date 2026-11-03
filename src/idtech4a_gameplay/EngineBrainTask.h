// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_BRAIN_TASK_H
#define WOLF2009_ENGINE_BRAIN_TASK_H
class idEntity;
class idStr;
struct WolfBrainGlobals;
struct WolfBrainTask;
enum WolfBrainState { WolfBrain_Loaded, WolfBrain_Bound, WolfBrain_Running, WolfBrain_Unsupported };
struct WolfBrainStats {
    WolfBrainState state;
    int binds, ticks, enters, writes, acceptedUses, rejectedUses, lastActorSpawnId;
    int clock, activeTaskStart, activeNodeId;
    bool initialized, pendingUse;
};
// Engine-thread-only. These objects are owned by registered GPL entities, not
// by the core DLL. No game-import or idFileSystem ABI changes are required.
WolfBrainGlobals *Wolf2009_CreateBrainGlobals(idEntity *owner);
void Wolf2009_DestroyBrainGlobals(WolfBrainGlobals *);
WolfBrainTask *Wolf2009_CreateBrainTask(idEntity *owner);
void Wolf2009_DestroyBrainTask(WolfBrainTask *);
bool Wolf2009_BindPendingBrainTasks();
bool Wolf2009_TickBrainTask(WolfBrainTask *);
bool Wolf2009_ResetBrainTask(WolfBrainTask *);
bool Wolf2009_BrainCanUseKey(const WolfBrainTask *, const char *key);
// A rejected key is a handled, effect-free Use, not an unsupported receiver.
bool Wolf2009_DeliverBrainUse(WolfBrainTask *, idEntity *actor, const char *key);
bool Wolf2009_ReadBrainGlobal(const WolfBrainGlobals *, const char *name, int &value);
bool Wolf2009_QueryBrainTask(const WolfBrainTask *, WolfBrainStats &out);
bool Wolf2009_BrainPendingKey(const WolfBrainTask *, const char *key);
const char *Wolf2009_BrainTaskReason(const WolfBrainTask *);
const char *Wolf2009_BrainGlobalsReason(const WolfBrainGlobals *);
int Wolf2009_LiveBrainTasks();
int Wolf2009_LiveBrainGlobals();
// Implemented by NativeBrainEntities.cpp; only these explicit registered
// receiver/provider classes participate. There is no EV_Activate fallback.
WolfBrainTask *Wolf2009_NativeBrainTask(idEntity *);
bool Wolf2009_IsNativeBrainTaskEntity(const idEntity *);
WolfBrainGlobals *Wolf2009_NativeBrainGlobals(idEntity *);
#ifdef WOLF2009_BRAIN_QA
extern "C" int Wolf2009_RunBrainTaskFixtureProbe(void);
#endif
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdexcept>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineBrainTask.h"

namespace { const idEventDef EV_WolfBrainBind("<wolfBrainBind>", NULL); }

class idWolfBrainGlobals : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfBrainGlobals);
    idWolfBrainGlobals() : globals(NULL) {}
    ~idWolfBrainGlobals() { Wolf2009_DestroyBrainGlobals(globals); }
    void Spawn() {
        globals = Wolf2009_CreateBrainGlobals(this);
        PostEventMS(&EV_WolfBrainBind, 0);
        const char *reason = Wolf2009_BrainGlobalsReason(globals);
        if (reason[0]) gameLocal.Printf("WOLF2009_BRAIN_GLOBALS_UNSUPPORTED entity=%s reason=%s\n",GetName(),reason);
    }
    void Save(idSaveGame *) const { gameLocal.Error("Wolf Brain globals save-state policy is unsupported"); }
    void Restore(idRestoreGame *) { gameLocal.Error("Wolf Brain globals restore policy is unsupported"); }
    WolfBrainGlobals *GetGlobals() const { return globals; }
  private:
    WolfBrainGlobals *globals;
    void Event_Bind() {
        if (gameLocal.GameState() == GAMESTATE_STARTUP) { PostEventMS(&EV_WolfBrainBind, 1); return; }
        Wolf2009_BindPendingBrainTasks();
    }
};
CLASS_DECLARATION(idEntity,idWolfBrainGlobals)
    EVENT(EV_WolfBrainBind,idWolfBrainGlobals::Event_Bind)
END_CLASS

class idWolfBrainTask : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfBrainTask);
    idWolfBrainTask() : brain(NULL) {}
    ~idWolfBrainTask() { Wolf2009_DestroyBrainTask(brain); }
    void Spawn() {
        brain = Wolf2009_CreateBrainTask(this);
        fl.neverDormant = true;
        cinematic = true; // This diagnostic event host keeps its explicit game clock.
        BecomeActive(TH_THINK);
        PostEventMS(&EV_WolfBrainBind, 0);
    }
    void Think() override {
        WolfBrainStats s;
        if (!Wolf2009_QueryBrainTask(brain,s) || s.state == WolfBrain_Unsupported) { BecomeInactive(TH_THINK); return; }
        if (s.state == WolfBrain_Loaded) return; // post-spawn barrier has not run yet
        if (!Wolf2009_TickBrainTask(brain)) BecomeInactive(TH_THINK);
    }
    void Save(idSaveGame *) const { gameLocal.Error("Wolf Brain Task save-state policy is unsupported"); }
    void Restore(idRestoreGame *) { gameLocal.Error("Wolf Brain Task restore policy is unsupported"); }
    WolfBrainTask *GetBrain() const { return brain; }
  private:
    WolfBrainTask *brain;
    void Event_Bind() {
        if (gameLocal.GameState() == GAMESTATE_STARTUP) { PostEventMS(&EV_WolfBrainBind, 1); return; }
        Wolf2009_BindPendingBrainTasks();
    }
};
CLASS_DECLARATION(idEntity,idWolfBrainTask)
    EVENT(EV_WolfBrainBind,idWolfBrainTask::Event_Bind)
END_CLASS

WolfBrainTask *Wolf2009_NativeBrainTask(idEntity *e) {
    return Wolf2009_IsNativeBrainTaskEntity(e) ? static_cast<idWolfBrainTask *>(e)->GetBrain() : NULL;
}
bool Wolf2009_IsNativeBrainTaskEntity(const idEntity *e) {
    return e && e->GetType() == &idWolfBrainTask::Type;
}
WolfBrainGlobals *Wolf2009_NativeBrainGlobals(idEntity *e) {
    return e && e->GetType() == &idWolfBrainGlobals::Type ? static_cast<idWolfBrainGlobals *>(e)->GetGlobals() : NULL;
}

#ifdef WOLF2009_BRAIN_QA
namespace {
void Need(bool v,const char *why) { if (!v) throw std::runtime_error(why); }
class OwnEntities {
    idList< idEntityPtr<idEntity> > entries;
  public:
    void Add(idEntity *e) { idEntityPtr<idEntity> p; p=e; entries.Append(p); }
    void Clear() {
        for (int i=entries.Num()-1;i>=0;--i) { idEntity *e=entries[i].GetEntity(); if(e) delete e; }
        entries.Clear();
    }
    ~OwnEntities() { Clear(); }
};
idEntity *Spawn(OwnEntities &owned,const char *classname,const char *name,const char *resource=NULL,
                const char *provider="brain_globals",int entityNumber=-1,const char *defaults=NULL,bool unknownHostField=false) {
    Need(gameLocal.FindEntity(name)==NULL,"QA Brain name already exists");
    idDict args; args.Set("classname",classname); args.Set("name",name);
    if(resource) { args.Set("wolf_brain_resource",resource); args.Set("wolf_brain_globals",provider); }
    if(entityNumber>=0) { Need(gameLocal.entities[entityNumber]==NULL,"QA replacement slot occupied");args.SetInt("spawn_entnum",entityNumber); }
    if(defaults) args.Set("wolf_brain_defaults",defaults);
    if(unknownHostField) args.Set("wolf_brain_unknown","1");
    idEntity *entity=NULL;
    Need(gameLocal.SpawnEntityDef(args,&entity)&&entity,"GPL SpawnEntityDef failed for own Brain fixture");
    owned.Add(entity); return entity;
}
void Frames(int count) {
    usercmd_t commands[MAX_CLIENTS]; memset(commands,0,sizeof(commands));
    for(int i=0;i<count;++i) gameLocal.RunFrame(commands);
}
WolfBrainStats Stats(idEntity *entity) {
    WolfBrainStats s; Need(Wolf2009_QueryBrainTask(Wolf2009_NativeBrainTask(entity),s),"No native Brain session"); return s;
}
int Global(idEntity *entity,const char *name) {
    int value=-1; Need(Wolf2009_ReadBrainGlobal(Wolf2009_NativeBrainGlobals(entity),name,value),"Explicit shared global unavailable"); return value;
}
void InvalidGroup(const char *resource,bool unknownReceiver,bool missingDefaults,bool unknownHostField=false) {
    OwnEntities owned;
    idEntity *g=Spawn(owned,"wolf2009_brain_globals","brain_globals",NULL,"brain_globals",-1,
        missingDefaults?"wolf2009-brain-qa/own/missing-defaults.txt":NULL);
    if(unknownReceiver) Spawn(owned,"wolf2009_brain_plain_actor","brain_plain_actor");
    idEntity *writer=Spawn(owned,"wolf2009_brain_task","brain_writer","wolf2009-brain-qa/own/writer.brai");
    idEntity *bad=Spawn(owned,"wolf2009_brain_task","brain_invalid",resource,"brain_globals",-1,NULL,unknownHostField);
    Frames(3);
    const WolfBrainStats a=Stats(writer),b=Stats(bad);
    Need(a.state==WolfBrain_Unsupported&&b.state==WolfBrain_Unsupported&&!a.initialized&&!b.initialized&&
        a.writes==0&&b.writes==0&&a.enters==0&&b.enters==0,"Group preflight allowed an effect before unsupported refusal");
    int value=-1; Need(!Wolf2009_ReadBrainGlobal(Wolf2009_NativeBrainGlobals(g),"received",value),"Failed provider reported initialized runtime globals");
    gameLocal.Printf("WOLF2009_BRAIN_PREFLIGHT resource=%s unknown_receiver=%d missing_defaults=%d unknown_host_field=%d effects=0 valid=1\n",
        resource,unknownReceiver,missingDefaults,unknownHostField);
}
void LateProviderBudget() {
    OwnEntities owned; idEntity *first=NULL,*rejected=NULL;
    for(int i=0;i<65;++i) {
        idEntity *e=Spawn(owned,"wolf2009_brain_task",va("brain_budget_%d",i),"wolf2009-brain-qa/own/writer.brai");
        if(i==0) first=e;
        if(i==64) rejected=e;
    }
    Spawn(owned,"wolf2009_brain_globals","brain_globals");
    Need(Wolf2009_NativeBrainTask(rejected)==NULL,"Native Brain session budget precondition changed");
    Frames(3);
    const WolfBrainStats s=Stats(first);
    Need(s.state==WolfBrain_Unsupported&&!s.initialized&&s.writes==0&&s.enters==0,
        "Rejected session bypassed a later provider's group admission");
    gameLocal.Printf("WOLF2009_BRAIN_BUDGET declared=65 admitted=0 provider_spawned_last=1 effects=0 valid=1\n");
}
}
extern "C" ID_GAME_API int Wolf2009_RunBrainTaskFixtureProbe(void) {
    if(gameLocal.GameState()!=GAMESTATE_ACTIVE||Wolf2009_LiveBrainTasks()!=0||Wolf2009_LiveBrainGlobals()!=0) return 0;
    try {
        {
            OwnEntities owned;
            idEntity *g=Spawn(owned,"wolf2009_brain_globals","brain_globals");
            idEntity *actor=Spawn(owned,"wolf2009_brain_plain_actor","brain_actor");
            idEntity *receiver=Spawn(owned,"wolf2009_brain_task","brain_receiver","wolf2009-brain-qa/own/receiver.brai");
            idEntity *sender=Spawn(owned,"wolf2009_brain_task","brain_sender","wolf2009-brain-qa/own/sender.brai");
            for(int i=0;i<8&&Stats(sender).activeNodeId!=2;++i) Frames(1);
            WolfBrainStats s=Stats(sender);
            Need(s.state==WolfBrain_Running&&s.initialized&&s.binds==1&&s.activeNodeId==2&&s.writes==0&&
                Global(g,"received")==0&&Global(g,"sender_done")==0,"OnSpawn did not enter native timer before actions");
            const int deadline=s.activeTaskStart+32;
            while(gameLocal.time<deadline) {
                Frames(1);s=Stats(sender);
                if(gameLocal.time<=deadline) Need(s.writes==0&&Stats(receiver).acceptedUses==0,"Timer fired at or before strict boundary");
            }
            Need(gameLocal.time==deadline&&Stats(sender).writes==0,"Own fixed-step fixture did not observe exact timer boundary");
            for(int i=0;i<12&&Global(g,"received")!=7;++i) Frames(1);
            const WolfBrainStats sent=Stats(sender),got=Stats(receiver);
            Need(Global(g,"sender_done")==1&&Global(g,"received")==7&&sent.writes==1&&got.writes==1&&
                got.acceptedUses==1&&got.lastActorSpawnId==gameLocal.GetSpawnId(actor)&&!got.pendingUse,
                "Native cross-entity OnSpawn/timer/Use/int32 global chain failed");
            gameLocal.Printf("WOLF2009_BRAIN_CHAIN sender_writes=%d receiver_writes=%d actor_spawn_id=%d strict_timer=1 shared_globals=1 valid=1\n",
                sent.writes,got.writes,got.lastActorSpawnId);

            WolfBrainTask *r=Wolf2009_NativeBrainTask(receiver);
            Need(Wolf2009_ResetBrainTask(r),"Native Brain Reset failed");
            Need(!Wolf2009_BrainPendingKey(r,"deliver")&&Global(g,"received")==7,
                "Reset did not clear only the input latch while preserving provider globals");
            const int before=Stats(receiver).writes;
            Need(Wolf2009_DeliverBrainUse(r,actor,"DeLiVeR")&&Wolf2009_DeliverBrainUse(r,actor,"unregistered")&&
                Wolf2009_BrainPendingKey(r,"deliver")&&Stats(receiver).writes==before&&Stats(receiver).rejectedUses==1,
                "CanUseKey failed to preserve pending valid key, or Use recursively ticked receiver");
            for(int i=0;i<12&&Stats(receiver).writes==before;++i) Frames(1);
            Need(Stats(receiver).writes==before+1&&Stats(receiver).binds==1&&!Stats(receiver).pendingUse,
                "Reset/bind lifecycle or delayed keyed delivery failed");
            gameLocal.Printf("WOLF2009_BRAIN_KEY_GATE accepted=%d rejected=%d bind_count=%d recursive_tick=0 valid=1\n",
                Stats(receiver).acceptedUses,Stats(receiver).rejectedUses,Stats(receiver).binds);

            idEntityPtr<idEntity> stale;stale=receiver;const int oldId=stale.GetSpawnId(),slot=receiver->entityNumber;
            delete receiver;
            idEntity *replacement=Spawn(owned,"wolf2009_brain_task","brain_receiver","wolf2009-brain-qa/own/replacement.brai",
                "brain_globals",slot);
            Need(stale.GetEntity()==NULL&&gameLocal.GetSpawnId(replacement)!=oldId&&replacement->entityNumber==slot,
                "GPL generation-aware replacement precondition failed");
            Need(!Wolf2009_TickBrainTask(Wolf2009_NativeBrainTask(sender))&&Stats(sender).state==WolfBrain_Unsupported&&
                Stats(replacement).writes==0&&!Stats(replacement).initialized,"Stale target silently rebound after entity-slot reuse");
            Frames(2);
            Need(Stats(replacement).state==WolfBrain_Unsupported&&Stats(replacement).writes==0,
                "Replacement ran inside an invalid existing provider group");
            gameLocal.Printf("WOLF2009_BRAIN_GENERATION slot=%d old=%d replacement=%d stale_null=1 rebound=0 valid=1\n",
                slot,oldId,gameLocal.GetSpawnId(replacement));
        }
        Need(Wolf2009_LiveBrainTasks()==0&&Wolf2009_LiveBrainGlobals()==0,"Native Brain explicit teardown leaked registry entries");
        InvalidGroup("wolf2009-brain-qa/own/bad-expression.brai",false,false);
        InvalidGroup("wolf2009-brain-qa/own/bad-field.brai",false,false);
        InvalidGroup("wolf2009-brain-qa/own/bad-unreachable.brai",false,false);
        InvalidGroup("wolf2009-brain-qa/own/bad-receiver.brai",true,false);
        InvalidGroup("wolf2009-brain-qa/own/writer.brai",false,true);
        InvalidGroup("wolf2009-brain-qa/own/writer.brai",false,false,true);
        LateProviderBudget();
        Need(Wolf2009_LiveBrainTasks()==0&&Wolf2009_LiveBrainGlobals()==0,"Failed Brain admission leaked native registry state");
        gameLocal.Printf("WOLF2009_BRAIN_QA passed=1 live_tasks=0 live_globals=0 clock=diagnostic-ms teardown=1\n");
        return 1;
    } catch(const std::exception &e) {
        gameLocal.Printf("WOLF2009_BRAIN_QA passed=0 reason=%s live_tasks=%d live_globals=%d\n",e.what(),
            Wolf2009_LiveBrainTasks(),Wolf2009_LiveBrainGlobals());return 0;
    }
}
#endif

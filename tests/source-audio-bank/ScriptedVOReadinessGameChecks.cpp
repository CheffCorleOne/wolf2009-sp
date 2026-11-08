// SPDX-License-Identifier: GPL-3.0-or-later
// Game-module QA only: type19 readiness providers against the real core bank
// service, with ordinary native entities as owners in the running game.
#include "NativeScriptedVOReadiness.h"
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include "idlib/precompiled.h"
#include "game/Game_local.h"

namespace {
int checks = 0;
void Need(bool value, const std::string &why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
typedef wolf2009::ScriptedVOReadinessState State;
typedef std::unique_ptr<WolfScriptedVOReadiness> Provider;
class OwnEntities {
    idList<idEntityPtr<idEntity> > entries;
  public:
    void Add(idEntity *e) { idEntityPtr<idEntity> p; p = e; entries.Append(p); }
    ~OwnEntities() {
        for (int i = entries.Num() - 1; i >= 0; --i) {
            idEntity *e = entries[i].GetEntity();
            if (e) delete e;
        }
    }
};
idEntity *Spawn(OwnEntities &owned, const char *name, int slot = -1) {
    Need(gameLocal.FindEntity(name) == NULL, "QA owner name already exists");
    idDict args;
    args.Set("classname", "wolf2009_target");
    args.Set("name", name);
    if (slot >= 0) {
        Need(gameLocal.entities[slot] == NULL, "QA replacement slot occupied");
        args.SetInt("spawn_entnum", slot);
    }
    idEntity *entity = NULL;
    Need(gameLocal.SpawnEntityDef(args, &entity) && entity, "GPL SpawnEntityDef failed for a QA owner");
    owned.Add(entity);
    return entity;
}
void Frames(int count) {
    usercmd_t commands[MAX_CLIENTS];
    memset(commands, 0, sizeof(commands));
    for (int i = 0; i < count; ++i) gameLocal.RunFrame(commands);
}
// Measured trainyard node-29 fields; the BRAI identity is a QA label because
// this probe does not load the trainyard graph (the greeter chain owns that).
wolf2009::ScriptedVOStreamInSource Source(const char *definition = "scripted_voice_kc1_trn", const char *locale = "english",
                                          std::uint32_t type = 19) {
    wolf2009::ScriptedVOStreamInSource s;
    s.brainResource = "qa/type19-readiness";
    s.nodeId = 29;
    s.nodeType = type;
    s.definition = definition;
    s.locale = locale;
    s.levelGeneration = 1;
    s.eventId = 1;
    return s;
}
Provider Make(idEntity *owner, const wolf2009::ScriptedVOStreamInSource &source = Source()) {
    idStr reason;
    Provider p = WolfScriptedVOReadiness::Request(source, owner, reason);
    Need(p.get() != NULL, std::string("provider request refused: ") + reason.c_str());
    return p;
}
bool StatusOpens(std::uint64_t generation) {
    idFile *f = fileSystem->OpenFileRead(va("wolf2009/_sound/bank/v1/status/%llu", (unsigned long long)generation), false);
    if (f) fileSystem->CloseFile(f);
    return f != NULL;
}
struct Listener : wolf2009::ScriptedVOReadinessListener {
    int calls = 0;
    void OnScriptedVOReady(const wolf2009::ScriptedVOStreamInReadiness &) override { ++calls; }
};

void Run() {
    OwnEntities owned;
    idEntity *a = Spawn(owned, "qa_vo_owner_a"), *b = Spawn(owned, "qa_vo_owner_b");
    // Pending persists across ordinary frames: no timer advances readiness.
    Provider pa = Make(a);
    Need(pa->State() == State::Pending && !pa->ConsumeReady() && !pa->Readiness().HoldsBank(), "fresh request is not pending");
    Frames(30);
    Need(pa->State() == State::Pending && !pa->ConsumeReady(), "frames/time advanced a pending type19 request");
    Need(pa->Update() == State::Ready && pa->Readiness().HoldsBank(), std::string("whole bank did not become ready: ") + pa->Reason());
    const auto bank = pa->Bank();
    Need(bank.members.size() == 19 && bank.package == "streampacks/scripted_voice_kc1_trn_vo_english.spk" &&
             bank.definition == "scripted_voice_kc1_trn" && bank.locale == "english" && bank.mountEpoch,
         "ready provider is not bound to the retained 19-member English bank");
    Need(pa->ConsumeReady() && !pa->ConsumeReady() && pa->Update() == State::Ready && !pa->ConsumeReady(),
         "ready event was not delivered exactly once");
    // A second owner joins the same retained bank.
    Provider pb = Make(b);
    Need(pb->Update() == State::Ready && pb->Bank().generation == bank.generation && pb->ConsumeReady(),
         "second owner did not share the retained bank generation");
    pb->Cancel();
    Need(pb->State() == State::Cancelled && !pb->Readiness().HoldsBank() && pb->Update() == State::Cancelled &&
             !pb->ConsumeReady(),
         "explicit cancellation did not release a ready request");
    // Owner deleted before the bank work: cancelled, no bank acquired.
    {
        idEntity *c = Spawn(owned, "qa_vo_owner_c");
        Provider pc = Make(c);
        delete c;
        Need(pc->Update() == State::Cancelled && !pc->Readiness().HoldsBank() && !pc->ConsumeReady(),
             "deleted owner still acquired readiness");
    }
    // Owner deleted after Ready but before consumption: no late event.
    {
        idEntity *d = Spawn(owned, "qa_vo_owner_d");
        Provider pd = Make(d);
        Need(pd->Update() == State::Ready, "owner d request not ready");
        delete d;
        Need(!pd->ConsumeReady() && pd->State() == State::Cancelled && !pd->Readiness().HoldsBank(),
             "ready event escaped after its owner ended");
    }
    // Same slot, new spawn generation: the stale owner identity is refused.
    {
        idEntity *e = Spawn(owned, "qa_vo_owner_e");
        const int slot = e->entityNumber;
        Provider pe = Make(e);
        delete e;
        idEntity *f = Spawn(owned, "qa_vo_owner_f", slot);
        Need(f->entityNumber == slot, "replacement did not reuse the owner slot");
        Need(pe->Update() == State::Cancelled && !pe->ConsumeReady(), "replacement entity inherited a stale request");
    }
    // Listener subscription is delivered exactly once.
    {
        Listener listener;
        Provider pl = Make(a);
        pl->SetListener(&listener);
        pl->Update();
        pl->Update();
        Need(listener.calls == 1 && !pl->ConsumeReady() && pl->State() == State::Ready, "listener delivery was not once-only");
    }
    // Unqualified source/locale and an invalid bank are Failed, never Ready.
    {
        Provider german = Make(a, Source("scripted_voice_kc1_trn", "german"));
        Need(german->State() == State::Failed && german->Update() == State::Failed && !german->ConsumeReady(),
             "unqualified locale was not failed");
        Provider wrongType = Make(a, Source("scripted_voice_kc1_trn", "english", 18));
        Need(wrongType->State() == State::Failed, "non-type19 source was admitted");
        Provider bad = Make(a, Source("qa_lastbad"));
        Need(bad->Update() == State::Failed && std::string(bad->Reason()).find("trn_5510_ka1") != std::string::npos &&
                 !bad->ConsumeReady() && !bad->Readiness().HoldsBank(),
             std::string("partial bank failure was not a failed readiness: ") + bad->Reason());
        idStr reason;
        Need(!WolfScriptedVOReadiness::Request(Source(), NULL, reason) && reason.Length(), "null owner was admitted");
    }
    // Backing lifetime ends (ordinary level-load purge): ready requests are lost.
    Provider pg = Make(a);
    Need(pg->Update() == State::Ready && pg->Bank().generation == bank.generation, "unconsumed ready request missing");
    soundSystem->BeginLevelLoad();
    soundSystem->EndLevelLoad("");
    Need(pg->Update() == State::Lost && !pg->ConsumeReady() && !pg->Readiness().HoldsBank() && std::strlen(pg->Reason()),
         "purged bank did not invalidate an unconsumed ready request");
    Need(pa->Update() == State::Lost && !pa->Readiness().HoldsBank(), "purged bank did not invalidate a consumed request");
    Need(!StatusOpens(bank.generation), "lost bank generation stayed alive after its last holder");
    gameLocal.Printf("WOLF2009_SCRIPTED_VO_LOST generation=%llu reason=%s\n", (unsigned long long)bank.generation, pg->Reason());
    // A new request after loss registers a new generation; it does not revive.
    Provider ph = Make(a);
    Need(ph->Update() == State::Ready && ph->Bank().generation > bank.generation && ph->ConsumeReady(),
         "request after loss did not bind a fresh bank generation");
    const std::uint64_t last = ph->Bank().generation;
    Need(Wolf2009_HeldScriptedVOBankLeases() == 1, "unexpected held bank leases");
    pa.reset(); pb.reset(); pg.reset(); ph.reset();
    Need(Wolf2009_HeldScriptedVOBankLeases() == 0 && Wolf2009_LiveScriptedVOReadiness() == 0 && !StatusOpens(last),
         "provider teardown did not release the bank");
    gameLocal.Printf("WOLF2009_SCRIPTED_VO_READINESS_OK checks=%d generations=%llu,%llu members=19 once_only=1 "
                     "shared=1 cancelled=1 owner_ended=1 stale_slot=1 failed=1 lost=1 timer_free=1 speech=0\n",
                     checks, (unsigned long long)bank.generation, (unsigned long long)last);
}
} // namespace

extern "C" ID_GAME_API int Wolf2009_RunScriptedVOReadinessQA(void) {
    checks = 0;
    if (gameLocal.GameState() != GAMESTATE_ACTIVE || Wolf2009_LiveScriptedVOReadiness() != 0) return 0;
    try {
        Run();
        Need(Wolf2009_LiveScriptedVOReadiness() == 0 && Wolf2009_HeldScriptedVOBankLeases() == 0, "providers leaked");
        return 1;
    } catch (const std::exception &error) {
        gameLocal.Warning("WOLF2009_SCRIPTED_VO_READINESS_FAIL checks=%d reason=%s", checks, error.what());
        return 0;
    }
}

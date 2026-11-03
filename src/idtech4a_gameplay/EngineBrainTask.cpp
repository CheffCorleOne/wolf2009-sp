// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainLiteralActions.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineBrainTask.h"

namespace {
struct Random : wolf2009::BrainFlowRandom {
    std::uint32_t Next15() override { return static_cast<std::uint32_t>(gameLocal.random.RandomInt(32768)); }
};
bool Equal(const char *a, const char *b) { return a && b && idStr::Cmp(a, b) == 0; }
bool Configuration(const idDict &args, bool provider) {
    for (const idKeyValue *k = args.MatchPrefix("wolf_brain_"); k; k = args.MatchPrefix("wolf_brain_", k)) {
        const char *name = k->GetKey().c_str();
        if (Equal(name, "wolf_brain_mount") || Equal(name, "wolf_brain_diagnostic")) continue;
        if (provider && (Equal(name, "wolf_brain_clock") || Equal(name, "wolf_brain_defaults"))) continue;
        if (!provider && (Equal(name, "wolf_brain_resource") || Equal(name, "wolf_brain_globals"))) continue;
        return false;
    }
    const char *diagnostic = args.GetString("wolf_brain_diagnostic", "0");
    return Equal(diagnostic, "0") || Equal(diagnostic, "1");
}
bool Component(const std::string &s) {
    if (s.empty() || s.size() > 63 || s == "." || s == "..") return false;
    for (unsigned char c : s)
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') return false;
    return true;
}
bool Relative(const std::string &s) {
    if (s.empty() || s.size() > 255 || s.front() == '/' || s.back() == '/') return false;
    std::size_t begin = 0;
    while (begin < s.size()) {
        const auto end = s.find('/', begin), count = end == std::string::npos ? s.size() - begin : end - begin;
        const auto part = s.substr(begin, count);
        if (part.empty() || part == "." || part == "..") return false;
        for (unsigned char c : part)
            if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
                !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '.') return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}
bool Resource(const std::string &path, const std::string &mount, bool diagnostic, bool graph) {
    if (!Component(mount) || !Relative(path)) return false;
    const std::string typed = "wolf2009/" + mount + "/";
    if (graph && path.compare(0, typed.size(), typed) == 0)
        return path.compare(typed.size(), 5, "BRAI/") == 0 && path.size() > typed.size() + 5;
    // Only independently authored diagnostic resources use this loose path.
    const std::string own = "wolf2009-brain-qa/" + mount + "/";
    return diagnostic && path.compare(0, own.size(), own) == 0 && path.size() > own.size();
}
std::vector<unsigned char> Read(const std::string &path, int budget) {
    idFile *f = fileSystem->OpenFileRead(path.c_str(), false);
    if (!f) throw std::runtime_error("Brain resource not found through native FS");
    const int count = f->Length();
    if (count <= 0 || count > budget) { fileSystem->CloseFile(f); throw std::runtime_error("Brain FS byte budget"); }
    std::vector<unsigned char> bytes;
    try { bytes.resize(static_cast<std::size_t>(count)); }
    catch (...) { fileSystem->CloseFile(f); throw; }
    const int actual = f->Read(bytes.data(), count);
    fileSystem->CloseFile(f);
    if (actual != count) throw std::runtime_error("Truncated native Brain resource");
    return bytes;
}
bool Named(idEntity *entity, const std::string &name) {
    return entity && Equal(entity->GetName(), name.c_str());
}
}
struct WolfBrainGlobals {
    idEntityPtr<idEntity> owner;
    std::string mount, reason;
    wolf2009::BrainLiteralGlobals values;
    bool diagnostic = false, ready = false, failed = false;
};
struct WolfBrainTask : wolf2009::BrainTaskHost {
    idEntityPtr<idEntity> owner, providerOwner;
    struct Binding { idEntityPtr<idEntity> actor, target; WolfBrainTask *receiver = NULL; };
    std::string mount, providerName, reason;
    bool diagnostic = false;
    WolfBrainGlobals *provider = NULL;
    wolf2009::BrainGraph graph;
    wolf2009::BrainLiteralPlan plan;
    std::vector<Binding> bindings;
    std::vector<wolf2009::BrainTaskTimer> timers;
    std::unique_ptr<wolf2009::BrainTaskRuntime> runtime;
    Random random;
    WolfBrainStats stats;
    WolfBrainTask() {
        stats.state = WolfBrain_Loaded; stats.binds = stats.ticks = stats.enters = stats.writes =
            stats.acceptedUses = stats.rejectedUses = stats.lastActorSpawnId = 0;
        stats.initialized = stats.pendingUse = false;
        stats.clock = stats.activeTaskStart = stats.activeNodeId = 0;
    }
    void Fail(const char *why) {
        if (stats.state != WolfBrain_Unsupported) {
            reason = why; stats.state = WolfBrain_Unsupported;
            const idEntity *e = owner.GetEntity();
            gameLocal.Printf("WOLF2009_BRAIN_UNSUPPORTED entity=%s reason=%s\n", e ? e->GetName() : "<deleted>", why);
        }
    }
    bool Valid() {
        if (stats.state == WolfBrain_Unsupported) return false;
        if (!owner.GetEntity() || !providerOwner.GetEntity() || !provider || !provider->ready || provider->failed ||
            Wolf2009_NativeBrainGlobals(providerOwner.GetEntity()) != provider) {
            Fail("Brain owner or explicit globals provider expired"); return false;
        }
        for (std::size_t n = 0; n < plan.nodes.size(); ++n) {
            if (plan.nodes[n].type != 68) continue;
            auto &b = bindings[n];
            if (!b.actor.GetEntity() || !b.target.GetEntity() ||
                Wolf2009_NativeBrainTask(b.target.GetEntity()) != b.receiver || !b.receiver ||
                b.receiver->stats.state == WolfBrain_Unsupported || b.receiver->provider != provider) {
                Fail("Bound Brain actor/receiver expired or changed"); return false;
            }
        }
        return true;
    }
    wolf2009::BrainTaskTraits Describe(const wolf2009::BrainGraph &, std::size_t n) override {
        wolf2009::BrainTaskTraits t;
        if (!plan.supported || n >= plan.nodes.size()) return t;
        const auto type = plan.nodes[n].type;
        t.kind = type == 1 || type == 3 ? wolf2009::BrainTaskTraits::Root : wolf2009::BrainTaskTraits::Normal;
        t.normalActivationKnown = true; t.hasPrimary = type == 1 || type == 3 || type == 30;
        t.forceOnceOnInitialize = type == 1; return t;
    }
    bool SupportsCondition(const wolf2009::BrainGraph &, const wolf2009::BrainTaskCondition &r) override {
        return r.IsPrimary() && r.node < plan.nodes.size() &&
            (plan.nodes[r.node].type == 1 || plan.nodes[r.node].type == 3 || plan.nodes[r.node].type == 30);
    }
    wolf2009::BrainTaskHook Task(const wolf2009::BrainGraph &, std::size_t n, wolf2009::BrainTaskPhase phase,
                               const wolf2009::BrainTaskContext &ctx) override {
        using namespace wolf2009;
        if (!Valid() || n >= plan.nodes.size()) return BrainTaskHook::Unsupported;
        const auto &item = plan.nodes[n];
        if (phase == BrainTaskPhase::Exit && item.type == 3 && !(ctx.taskFlags & 1u)) ctx.use->Consume(item.key);
        if (phase != BrainTaskPhase::Enter) return BrainTaskHook::Applied;
        ++stats.enters;
        if (item.type == 68) {
            auto &b = bindings[n];
            // Preserve actor -> target -> key delivery, without posting EV_Activate.
            idEntity *actor = b.actor.GetEntity();
            if (!actor) actor = owner.GetEntity();
            idEntity *target = b.target.GetEntity();
            if (!actor || !target || Wolf2009_NativeBrainTask(target) != b.receiver ||
                !Wolf2009_DeliverBrainUse(b.receiver, actor, item.key.c_str())) return BrainTaskHook::Unsupported;
        } else if (item.type == 220) {
            const auto at = provider->values.find(item.variable);
            if (at == provider->values.end()) return BrainTaskHook::Unsupported;
            at->second = item.value; ++stats.writes;
            gameLocal.Printf("WOLF2009_BRAIN_SET entity=%s name=%s value=%d\n", owner.GetEntity()->GetName(),
                item.variable.c_str(), static_cast<int>(item.value));
        }
        return BrainTaskHook::Applied;
    }
    wolf2009::BrainTaskHook Condition(const wolf2009::BrainGraph &, const wolf2009::BrainTaskCondition &r,
                                    wolf2009::BrainTaskPhase phase, const wolf2009::BrainTaskContext &ctx) override {
        using namespace wolf2009;
        if (!SupportsCondition(graph, r) || !Valid()) return BrainTaskHook::Unsupported;
        if (plan.nodes[r.node].type == 30) {
            auto &timer = timers[r.node];
            if (phase == BrainTaskPhase::Initialize) timer.Initialize();
            if (phase == BrainTaskPhase::Enter)
                return timer.Enter(ctx.now, plan.nodes[r.node].minimum, plan.nodes[r.node].maximum, *ctx.random);
        }
        return BrainTaskHook::Applied;
    }
    wolf2009::BrainConditionValue Evaluate(const wolf2009::BrainGraph &, const wolf2009::BrainTaskCondition &r,
                                         const wolf2009::BrainTaskContext &ctx) override {
        using namespace wolf2009;
        if (!SupportsCondition(graph, r) || !Valid()) return BrainConditionValue::Unsupported;
        const auto &item = plan.nodes[r.node];
        if (item.type == 30) return timers[r.node].Evaluate(ctx.now, ctx.activeTaskStart, item.inverted);
        const bool value = item.type == 1 ? owner.GetEntity() != NULL : ctx.use->Matches(item.key);
        return value != item.inverted ? BrainConditionValue::True : BrainConditionValue::False;
    }
    wolf2009::BrainConditionValue RootReady(const wolf2009::BrainGraph &, std::size_t n,
                                          const wolf2009::BrainTaskContext &ctx) override {
        wolf2009::BrainTaskCondition r; r.node = n; return Evaluate(graph, r, ctx);
    }
};
namespace {
std::vector<WolfBrainTask *> tasks;
std::vector<WolfBrainGlobals *> globals;
void FailGroup(WolfBrainGlobals *g, const char *why) {
    g->failed = true; g->ready = false; g->reason = why;
    for (auto t : tasks) if (t->provider == g) t->Fail(why);
}
bool BindGroup(WolfBrainGlobals *g) {
    if (g->failed || !g->owner.GetEntity()) return false;
    std::vector<WolfBrainTask *> pending;
    const std::string providerName = g->owner.GetEntity()->GetName();
    // Rejected/over-budget registered entities still belong to their declared
    // group. A missing session must not disappear from group admission merely
    // because its provider was spawned later.
    for (idEntity *e = gameLocal.spawnedEntities.Next(); e; e = e->spawnNode.Next()) {
        if (Wolf2009_IsNativeBrainTaskEntity(e) &&
            Equal(e->spawnArgs.GetString("wolf_brain_globals"), providerName.c_str()) &&
            !Wolf2009_NativeBrainTask(e)) {
            FailGroup(g, "Declared Brain group contains a rejected native session"); return false;
        }
    }
    for (auto t : tasks) {
        if (t->providerName != providerName) continue;
        if (t->provider && t->provider != g) { t->Fail("A replacement globals entity cannot rebind a session"); continue; }
        if (t->stats.state == WolfBrain_Bound || t->stats.state == WolfBrain_Running) continue;
        t->provider = g; t->providerOwner = g->owner.GetEntity(); pending.push_back(t);
    }
    // All admission and native bindings precede construction/first Tick of any
    // new runtime. A bad member blocks the entire explicit provider group.
    for (auto t : pending) {
        if (!t->plan.supported || t->stats.state == WolfBrain_Unsupported || t->mount != g->mount ||
            t->diagnostic != g->diagnostic) {
            FailGroup(g, "Group has unsupported graph, resource mount or provider profile"); return false;
        }
        t->bindings.resize(t->plan.nodes.size()); t->timers.resize(t->plan.nodes.size());
        for (std::size_t n = 0; n < t->plan.nodes.size(); ++n) {
            const auto &item = t->plan.nodes[n];
            if (item.type == 220 && !g->values.count(item.variable)) {
                FailGroup(g, "Global action name missing from mandatory explicit defaults"); return false;
            }
            if (item.type != 68) continue;
            auto &b = t->bindings[n];
            idEntity *actor = item.actor.kind == wolf2009::BrainLiteralSelector::Me ? t->owner.GetEntity() :
                gameLocal.FindEntity(item.actor.name.c_str());
            if (!actor || (item.actor.kind == wolf2009::BrainLiteralSelector::Name && !Named(actor, item.actor.name))) {
                FailGroup(g, "Literal actor binding missing"); return false;
            }
            idEntity *target = item.target.kind == wolf2009::BrainLiteralSelector::Me ? actor :
                gameLocal.FindEntity(item.target.name.c_str());
            if (!target || (item.target.kind == wolf2009::BrainLiteralSelector::Name && !Named(target, item.target.name))) {
                FailGroup(g, "Literal target binding missing"); return false;
            }
            b.actor = actor; b.target = target; b.receiver = Wolf2009_NativeBrainTask(target);
            if (!b.receiver || !b.receiver->plan.supported || b.receiver->providerName != providerName ||
                b.receiver->mount != g->mount || b.receiver->diagnostic != g->diagnostic ||
                b.receiver->stats.state == WolfBrain_Unsupported) {
                FailGroup(g, "Use receiver is not an admitted same-provider keyed Brain entity"); return false;
            }
        }
    }
    for (auto t : pending) {
        t->runtime.reset(new wolf2009::BrainTaskRuntime(t->graph, *t, t->random,
            static_cast<std::uint32_t>(t->owner.GetSpawnId())));
        ++t->stats.binds; t->stats.state = WolfBrain_Bound;
    }
    g->ready = true;
    return true;
}
}
WolfBrainGlobals *Wolf2009_CreateBrainGlobals(idEntity *owner) {
    if (globals.size() >= 64) {
        for (auto g : globals) FailGroup(g, "Brain globals entity budget");
        return NULL;
    }
    std::unique_ptr<WolfBrainGlobals> g(new WolfBrainGlobals); g->owner = owner;
    try {
        if (!owner || gameLocal.isMultiplayer || gameLocal.isClient)
            throw std::runtime_error("Brain globals require an explicit singleplayer server owner");
        const auto &a = owner->spawnArgs;
        if (!Configuration(a, true)) throw std::runtime_error("Unknown Brain globals host field/profile");
        g->mount = a.GetString("wolf_brain_mount");
        g->diagnostic = a.GetBool("wolf_brain_diagnostic", "0");
        const std::string path = a.GetString("wolf_brain_defaults");
        // Clock/defaults are explicit host policy. Retail globalVars/lifetime
        // and imported game-clock units have not been reconstructed here.
        if (!Equal(a.GetString("wolf_brain_clock"), "diagnostic-ms") ||
            !Resource(path, g->mount, g->diagnostic, false))
            throw std::runtime_error("Mandatory explicit diagnostic defaults/clock/mount profile");
        if (!wolf2009::DecodeBrainLiteralGlobals(Read(path, 65536), g->values, g->reason))
            throw std::runtime_error(g->reason);
    } catch (const std::exception &e) { g->reason = e.what(); g->failed = true; }
    WolfBrainGlobals *result = g.release(); globals.push_back(result); return result;
}
void Wolf2009_DestroyBrainGlobals(WolfBrainGlobals *g) {
    if (!g) return;
    for (auto t : tasks) if (t->provider == g) {
        t->Fail("Explicit globals provider removed"); t->provider = NULL;
    }
    globals.erase(std::remove(globals.begin(), globals.end(), g), globals.end()); delete g;
}
WolfBrainTask *Wolf2009_CreateBrainTask(idEntity *owner) {
    if (tasks.size() >= 64) {
        for (auto g : globals) FailGroup(g, "Brain task entity budget");
        return NULL;
    }
    std::unique_ptr<WolfBrainTask> t(new WolfBrainTask); t->owner = owner;
    // Retain grouping identity even if configuration/resource admission fails.
    // Otherwise an invalid member could be skipped by its provider barrier.
    if (owner) {
        t->providerName = owner->spawnArgs.GetString("wolf_brain_globals");
        t->mount = owner->spawnArgs.GetString("wolf_brain_mount");
    }
    try {
        if (!owner || gameLocal.isMultiplayer || gameLocal.isClient)
            throw std::runtime_error("Brain Task requires a singleplayer server owner");
        const auto &a = owner->spawnArgs;
        if (!Configuration(a, false)) throw std::runtime_error("Unknown Brain Task host field/profile");
        t->diagnostic = a.GetBool("wolf_brain_diagnostic", "0");
        const std::string path = a.GetString("wolf_brain_resource");
        if (t->providerName.empty() || !Resource(path, t->mount, t->diagnostic, true))
            throw std::runtime_error("Mandatory same-mount BRAI path and explicit globals entity");
        t->graph = wolf2009::DecodeBrainGraph(Read(path, 1024 * 1024));
        t->plan = wolf2009::PrepareBrainLiteralActions(t->graph);
        if (!t->plan.supported) throw std::runtime_error(t->plan.reason);
    } catch (const std::exception &e) { t->Fail(e.what()); }
    WolfBrainTask *result = t.release(); tasks.push_back(result); return result;
}
void Wolf2009_DestroyBrainTask(WolfBrainTask *t) {
    if (!t) return;
    tasks.erase(std::remove(tasks.begin(), tasks.end(), t), tasks.end()); delete t;
}
bool Wolf2009_BindPendingBrainTasks() {
    bool okay = true;
    for (auto g : globals) {
        try { if (!BindGroup(g)) okay = false; }
        catch (const std::exception &e) { FailGroup(g, e.what()); okay = false; }
    }
    for (auto t : tasks) if (t->stats.state == WolfBrain_Loaded && !t->provider) {
        t->Fail("Explicit globals entity not found after spawn"); okay = false;
    }
    return okay;
}
bool Wolf2009_TickBrainTask(WolfBrainTask *t) {
    if (!t || !t->runtime || !t->Valid()) return false;
    ++t->stats.ticks;
    const auto result = t->runtime->Tick(gameLocal.time);
    if (result.status == wolf2009::BrainTaskResult::Unsupported || result.status == wolf2009::BrainTaskResult::BudgetExceeded) {
        t->Fail(result.reason.c_str()); return false;
    }
    t->stats.state = WolfBrain_Running; return true;
}
bool Wolf2009_ResetBrainTask(WolfBrainTask *t) {
    if (!t || !t->runtime || !t->runtime->State().initialized || !t->Valid()) return false;
    const auto r = t->runtime->Reset(gameLocal.time);
    if (r.status == wolf2009::BrainTaskResult::Unsupported) { t->Fail(r.reason.c_str()); return false; }
    return true;
}
bool Wolf2009_BrainCanUseKey(const WolfBrainTask *t, const char *key) {
    if (!t || !key || !t->runtime || t->stats.state == WolfBrain_Unsupported) return false;
    wolf2009::BrainTaskUse input;
    if (!input.Notify(key)) return false;
    for (std::size_t n = 0; n < t->plan.nodes.size(); ++n) {
        const auto &p = t->plan.nodes[n];
        if (p.type == 3 && input.Matches(p.key) && !t->graph.nodes[n].outputs.empty()) return true;
    }
    return false;
}
bool Wolf2009_DeliverBrainUse(WolfBrainTask *t, idEntity *actor, const char *key) {
    if (!t || !t->Valid() || !t->runtime || !actor || !key) return false;
    if (!Wolf2009_BrainCanUseKey(t, key)) { ++t->stats.rejectedUses; return true; }
    if (!t->runtime->NotifyUse(key)) { t->Fail("Use key outside latch contract"); return false; }
    ++t->stats.acceptedUses; t->stats.lastActorSpawnId = gameLocal.GetSpawnId(actor);
    gameLocal.Printf("WOLF2009_BRAIN_USE entity=%s actor=%s key=%s queued=1\n", t->owner.GetEntity()->GetName(), actor->GetName(), key);
    return true;
}
bool Wolf2009_ReadBrainGlobal(const WolfBrainGlobals *g, const char *name, int &value) {
    if (!g || !g->owner.GetEntity() || g->failed || !g->ready || !name) return false;
    const auto at = g->values.find(name); if (at == g->values.end()) return false;
    value = static_cast<int>(at->second); return true;
}
bool Wolf2009_QueryBrainTask(const WolfBrainTask *t, WolfBrainStats &out) {
    if (!t) return false;
    out = t->stats;
    if (t->runtime) { out.initialized = t->runtime->State().initialized; out.pendingUse = t->runtime->State().use.pending; }
    if (t->runtime) {
        const auto &s = t->runtime->State(); out.clock = s.clock; out.activeTaskStart = s.activeTaskStart;
        out.activeNodeId = s.activeNode < t->graph.nodes.size() ? static_cast<int>(t->graph.nodes[s.activeNode].id) : 0;
    }
    return true;
}
bool Wolf2009_BrainPendingKey(const WolfBrainTask *t, const char *key) {
    return t && t->runtime && key && t->runtime->State().use.Matches(key);
}
const char *Wolf2009_BrainTaskReason(const WolfBrainTask *t) { return t ? t->reason.c_str() : "No Brain session"; }
const char *Wolf2009_BrainGlobalsReason(const WolfBrainGlobals *g) { return g ? g->reason.c_str() : "No globals provider"; }
int Wolf2009_LiveBrainTasks() { return static_cast<int>(tasks.size()); }
int Wolf2009_LiveBrainGlobals() { return static_cast<int>(globals.size()); }

// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceAIShell.h"
#include "NativeSourceAIVisuals.h"
#include "BrainTask.h"
#include "Entities.h"
#include "SourceDefinitions.h"
#include "TrainyardExit.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceDirector.h"
#include "NativeSourceRegistry.h"
#include "SourceDeclarationSetRuntime.h"

namespace {
constexpr const char* EntitiesPath = "wolf2009/main/ENTS/maps/game/trainyard/trainyard";
constexpr const char* BrainPrefix = "wolf2009/main/BRAI/brains/trainyard/";
constexpr const char* ArrivalRelay = "train_greeter_brainuse_1";
constexpr const char* WagonName = "starting_boxcar_model";

struct ActorProfile {
    const char* name;
    const char* classname;
    const char* brain;
    bool hidden;
    unsigned stopNode, stopType;
};
const ActorProfile Actors[] = {
    {"train_greeter", "trainyard_greeter", "train_greeter", false, 2, 49},
    {"nazi_interragator_1", "trainyard_nazi_talker_1", "nazi_interragator", true, 11, 19},
    {"nazi_interragator_2", "trainyard_nazi_talker_2", "", true, 0, 0},
    {"trainyard_hostage_1", "trainyard_allies01", "", true, 0, 0},
    {"trainyard_hostage_2", "trainyard_allies02", "", true, 0, 0}
};

int liveShells = 0, liveActors = 0, liveBrains = 0, livePendingUses = 0;

void Need(bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}

void NeedReason(bool valid, const idStr& reason, const char* fallback) {
    if (!valid) throw std::runtime_error(reason.IsEmpty() ? fallback : reason.c_str());
}

std::string Fold(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}

struct CloseFile {
    void operator()(idFile* file) const { if (file) fileSystem->CloseFile(file); }
};

std::vector<unsigned char> ReadExact(const std::string& path, std::size_t maxBytes) {
    std::unique_ptr<idFile, CloseFile> input(fileSystem->OpenFileRead(path.c_str(), false));
    Need(input && input->Tell() == 0, "Source AI resource is absent");
    const int length = input->Length();
    Need(length > 0 && std::size_t(length) <= maxBytes, "Source AI resource byte budget");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const int read = input->Read(bytes.data() + offset, int(bytes.size() - offset));
        Need(read > 0 && std::size_t(read) <= bytes.size() - offset, "Short source AI resource read");
        offset += std::size_t(read);
    }
    unsigned char extra = 0;
    Need(input->Read(&extra, 1) == 0 && input->Tell() == length, "Source AI resource length changed");
    return bytes;
}

const wolf2009::EntityRecord& FindExact(const wolf2009::EntityDocument& document, const char* name) {
    const wolf2009::EntityRecord* match = nullptr;
    for (const auto& record : document.entities) {
        const auto* value = record.Find("name");
        if (!value || value->kind != wolf2009::EntityProperty::Text || idStr::Icmp(value->text.c_str(), name)) continue;
        Need(!match && value->text == name, "Source AI actor name is ambiguous or differs in case");
        match = &record;
    }
    Need(match != nullptr, "Source AI actor is absent from ENTS");
    std::set<std::string> fields;
    for (const auto& field : match->properties)
        Need(fields.insert(Fold(field.key)).second, "Source AI actor has ambiguous ENTS fields");
    return *match;
}

std::string DefinitionText(const wolf2009::ResolvedSourceDefinition& definition, const char* key) {
    const auto* value = definition.Find(key);
    if (!value) return {};
    Need((value->encoding == wolf2009::SourceLiteralEncoding::CompiledString ||
          value->encoding == wolf2009::SourceLiteralEncoding::PlainText) && value->value.size() == 1,
         "Source AI definition text field is unqualified");
    return value->value[0].text;
}

std::string TextField(const wolf2009::EntityRecord& record, const wolf2009::ResolvedSourceDefinition& definition,
                      const char* key) {
    const auto* value = record.Find(key);
    if (!value) return DefinitionText(definition, key);
    Need(value->kind == wolf2009::EntityProperty::Text, "Source AI ENTS text field is unqualified");
    return value->text;
}

bool BoolText(const std::string& value) {
    const auto folded = Fold(value);
    if (folded == "1" || folded == "true") return true;
    Need(folded == "0" || folded == "false" || folded.empty(), "Source AI Boolean field is unqualified");
    return false;
}

bool BoolField(const wolf2009::EntityRecord& record, const wolf2009::ResolvedSourceDefinition& definition,
               const char* key) {
    if (const auto* value = record.Find(key)) {
        if (value->kind == wolf2009::EntityProperty::Integer) {
            Need(value->integer == 0 || value->integer == 1, "Source AI ENTS Boolean integer differs");
            return value->integer != 0;
        }
        Need(value->kind == wolf2009::EntityProperty::Text, "Source AI ENTS Boolean field is unqualified");
        return BoolText(value->text);
    }
    const auto* value = definition.Find(key);
    if (!value) return false;
    if (value->encoding == wolf2009::SourceLiteralEncoding::CompiledSignedInteger) {
        Need(value->numericWords.size() == 1 && value->numericWords[0] <= 1, "Source AI definition Boolean integer differs");
        return value->numericWords[0] != 0;
    }
    return BoolText(DefinitionText(definition, key));
}

void RequireDefaultActiveLimit(const wolf2009::EntityRecord& record,
                               const wolf2009::ResolvedSourceDefinition& definition) {
    if (const auto* value = record.Find("maxActiveAI")) {
        Need((value->kind == wolf2009::EntityProperty::Integer && value->integer == 0) ||
             (value->kind == wolf2009::EntityProperty::Text && value->text == "0"),
             "Source AI maxActiveAI limits are outside this profile");
        return;
    }
    const auto* value = definition.Find("maxActiveAI");
    if (!value) return;
    if (value->encoding == wolf2009::SourceLiteralEncoding::CompiledSignedInteger) {
        Need(value->numericWords.size() == 1 && value->numericWords[0] == 0, "Source AI maxActiveAI limit differs");
        return;
    }
    const auto literal = DefinitionText(definition, "maxActiveAI");
    Need(literal.empty() || literal == "0", "Source AI maxActiveAI limits are outside this profile");
}

bool IsLive(idEntity* entity) {
    return entity && entity->entityNumber >= 0 && entity->entityNumber < MAX_GENTITIES &&
           gameLocal.entities[entity->entityNumber] == entity;
}

bool IsKey(const std::string& key) {
    return key.size() <= 256 && std::find(key.begin(), key.end(), '\0') == key.end();
}

class Actor;
const std::shared_ptr<Actor>* spawningActor = nullptr;
class SpawnContext {
  public:
    explicit SpawnContext(const std::shared_ptr<Actor>& value) {
        Need(!spawningActor, "Recursive Source AI event host spawn");
        spawningActor = &value;
    }
    ~SpawnContext() { spawningActor = nullptr; }
};

class SpawnConditions : public wolf2009::BrainFlowConditions {
  public:
    SpawnConditions(const idEntityPtr<idEntity>& owner, std::size_t spawnRoot)
        : owner(owner), spawnRoot(spawnRoot) {}
    wolf2009::BrainConditionValue Primary(const wolf2009::BrainGraph&, std::size_t node) override {
        if (node != spawnRoot) return wolf2009::BrainConditionValue::Unsupported;
        return owner.GetEntity() ? wolf2009::BrainConditionValue::True : wolf2009::BrainConditionValue::False;
    }
    wolf2009::BrainConditionValue Explicit(const wolf2009::BrainGraph&, std::size_t) override {
        return wolf2009::BrainConditionValue::Unsupported;
    }
  private:
    const idEntityPtr<idEntity>& owner;
    std::size_t spawnRoot;
};

class SingleDestination : public wolf2009::BrainFlowRandom {
  public:
    std::uint32_t Next15() override { throw std::runtime_error("Source AI root would require an unadmitted random choice"); }
};
} // namespace

class idWolfSourceAIThink : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceAIThink);
    idWolfSourceAIThink();
    void Spawn() {
        fl.networkSync = false;
        fl.neverDormant = true;
        cinematic = true;
    }
    void Think() override;
    void Save(idSaveGame*) const { gameLocal.Error("Source AI shell save state is unsupported"); }
    void Restore(idRestoreGame*) { gameLocal.Error("Source AI shell restore is unsupported"); }
  private:
    void Event_Activate(idEntity*);
    std::weak_ptr<Actor> owner;
};

CLASS_DECLARATION(idEntity, idWolfSourceAIThink)
    EVENT(EV_Activate, idWolfSourceAIThink::Event_Activate)
END_CLASS

namespace {
class Actor {
  public:
    explicit Actor(const ActorProfile& profile, std::uint64_t generation) : profile(profile) {
        state.name = profile.name;
        state.classname = profile.classname;
        state.levelGeneration = generation;
        ++liveActors;
    }
    ~Actor() {
        if (auto* entity = host.GetEntity()) delete entity;
        visuals.reset();
        if (state.brainBound) --liveBrains;
        if (use.pending) --livePendingUses;
        --liveActors;
    }

    void Prepare(const wolf2009::EntityRecord& record, const WolfSourceDeclarationSet& declarations) {
        const auto placement = wolf2009::ReadEntityPlacement(record);
        Need(placement.name == profile.name && placement.classname == profile.classname && placement.hasOrigin,
             "Source AI actor ENTS identity or origin differs");
        const auto definition = declarations.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef, profile.classname);
        Need(DefinitionText(definition, "spawnclass") == "idAI", "Source AI actor has a different compiled spawnclass");
        state.hidden = BoolField(record, definition, "hide");
        Need(state.hidden == profile.hidden, "Source AI actor initial Hide differs");
        state.noUnhideOnUse = BoolField(record, definition, "noUnhideOnUse");
        Need(!BoolField(record, definition, "brainIsPostBind"), "Source AI brainIsPostBind is unsupported");
        RequireDefaultActiveLimit(record, definition);
        const auto brain = TextField(record, definition, "brain");
        if (!profile.brain[0]) {
            Need(brain.empty(), "Unexpected source AI brain on this actor");
            return;
        }
        Need(brain == std::string("trainyard/") + profile.brain + ".brain", "Source AI authored brain differs");
        state.brainResource = std::string(BrainPrefix) + profile.brain;
        const auto bytes = ReadExact(state.brainResource, wolf2009::BrainGraphLimits().sourceBytes);
        graph = wolf2009::DecodeBrainGraph(bytes);
        Need(graph.format == wolf2009::BrainGraph::H1023, "Source AI brain requires the measured H1023 envelope");
        usedRoots = wolf2009::DecodeSourceOnUsedRootsH1023(bytes);
        const auto expectedUse = [this](std::size_t index, unsigned node, const char* key, unsigned target) {
            const auto& root = usedRoots[index];
            return root.node == node && root.key == key && root.outputFlags == std::vector<unsigned>{16} &&
                   root.outputTargets == std::vector<unsigned>{target} && root.outputPrimaryCondition == std::vector<bool>{true};
        };
        if (profile.stopType == 49) {
            Need(usedRoots.size() == 3 && expectedUse(0, 3, "train_stop", 29) &&
                 expectedUse(1, 4, "player_leaves_train", 10) && expectedUse(2, 5, "player_safe", 22),
                 "Source greeter OnUsed roots differ from the measured profile");
        } else {
            Need(usedRoots.size() == 1 && expectedUse(0, 12, "player_leaves_train", 43),
                 "Source interrogator OnUsed root differs from the measured profile");
        }
        unsigned spawnRoots = 0;
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            const auto& root = graph.nodes[index];
            if (root.type != 1) continue;
            ++spawnRoots;
            spawnRoot = index;
        }
        Need(spawnRoots == 1, "Source AI brain lacks one exact OnSpawn root");
        const auto& root = graph.nodes[spawnRoot];
        const auto& tokens = graph.records[root.record].tokens;
        Need(root.id == 1 && tokens.size() == 9 && tokens[5] == "^1" && tokens[6] == "1" &&
             tokens[7] == "^74" && tokens[8] == "1024" && root.outputs.size() == 1,
             "Source AI OnSpawn root is outside the measured Exists owner profile");
        const auto& output = graph.outputs[root.outputs[0]];
        const auto& wire = graph.records[output.record].tokens;
        Need(output.destinations.size() == 1 && wire.size() == 99 && wire[0] == "16" && wire[4] == "0" &&
             output.conditions.size() == 1 && graph.records[output.conditions[0]].tokens == std::vector<std::string>{"-1"},
             "Source AI OnSpawn edge is outside the measured single-destination profile");
        const auto& next = graph.nodes[output.destinations[0].targetNode];
        Need(next.id == profile.stopNode && next.type == profile.stopType, "Source AI first unsupported destination differs");
        flow = wolf2009::MakeBrainFlowState(graph);
        Need(flow.outputs[root.outputs[0]].supported, "Source AI OnSpawn output is unsupported");
        state.brainBound = true;
        ++liveBrains;
    }

    bool Alive() const {
        const auto* entity = body.GetEntity();
        if (!entity || !visuals || visuals->Body() != entity || !visuals->Head() || !host.GetEntity() ||
            !registry || registry->Generation() != state.levelGeneration || registry->NativeEntity(state.name) != entity ||
            gameLocal.FindEntity(state.name.c_str()) != entity) return false;
        for (unsigned index = 0; index < visuals->AccessoryCount(); ++index)
            if (!visuals->Accessory(index)) return false;
        return true;
    }

    void Notify(idEntity* activator, const std::string& key) {
        if (activator == gameLocal.GetLocalPlayer()) state.lastPlayerUseTimeMs = gameLocal.time;
        if (!state.brainBound || wolf2009::SourceCanUseKey(usedRoots, key) < 0) {
            ++state.rejectedUses;
            return;
        }
        const bool alreadyPending = use.pending;
        Need(use.Notify(key), "Source AI network rejected its bounded use key");
        if (!alreadyPending) ++livePendingUses;
        ++state.useNotifications;
    }

    // The source Use event first notifies, then performs base Use's activate.
    // Capture the real caller's spawn ID before the synchronous native event.
    bool Deliver(idEntity* activator, const std::string* key, idStr& reason) {
        reason.Clear();
        try {
            Need(Alive() && IsLive(activator), "Source AI native actor or activator generation expired");
            Need(!dispatching, "Recursive Source AI event dispatch");
            struct Scope {
                bool& dispatching;
                explicit Scope(bool& dispatching) : dispatching(dispatching) { dispatching = true; }
                ~Scope() { dispatching = false; }
            } scope(dispatching);
            idEntityPtr<idEntity> original;
            original = activator;
            if (key) {
                Need(IsKey(*key), "Source AI Use key is outside its wire domain");
                if (state.noUnhideOnUse && !state.thinking) {
                    ++state.rejectedUses;
                    reason = "Source AI noUnhideOnUse refused Use while not thinking";
                    return false;
                }
                Notify(activator, *key);
            }
            auto* eventHost = host.GetEntity();
            Need(eventHost && original.GetEntity() == activator, "Source AI activation endpoint expired");
            const auto count = state.activations;
            Need(eventHost->ProcessEvent(&EV_Activate, activator) && state.activations == count + 1,
                 "Source AI native Activate event was refused");
            return true;
        } catch (const std::exception& error) {
            reason = error.what();
            return false;
        }
    }

    void Activated(idEntity* activator) {
        if (!Alive() || !IsLive(activator)) return;
        // Exact actors use no active-AI cap or spawner. These adapters own
        // their presentation children, so Show wakes the complete assembly.
        state.lastActivator = activator->GetName();
        state.activated = true;
        ++state.activations;
        if (!state.hidden) {
            const idKeyValue* key = activator->spawnArgs.FindKey("useKey");
            if (key && IsKey(key->GetValue().c_str())) Notify(activator, key->GetValue().c_str());
            return;
        }
        visuals->SetHidden(false);
        state.hidden = false;
        state.thinking = true;
        ++state.wakes;
        state.wakeTimeMs = gameLocal.time;
        host.GetEntity()->BecomeActive(TH_THINK);
        gameLocal.Printf("WOLF2009_SOURCE_AI_WAKE actor=%s activator=%s pending_key=%s time=%d\n",
                         state.name.c_str(), state.lastActivator.c_str(), use.pending ? use.key.c_str() : "",
                         state.wakeTimeMs);
    }

    void Tick() {
        if (!Alive() || state.hidden || !state.thinking) return;
        if (director && !arrivalFinished) {
            wolf2009::TrainDoorUseEvent event;
            idEntity* activator = nullptr;
            bool pending = false;
            idStr why;
            if (!director->TakeRelayRequest(event, activator, pending, why)) {
                state.relayReason = why.c_str();
                arrivalFinished = true;
            } else if (pending) {
                arrivalFinished = true;
                if (event.levelGeneration != state.levelGeneration || !event.eventId || event.sourceEntityName != ArrivalRelay ||
                    event.ownerEntityName != profile.name || event.useKey != "train_stop" ||
                    !registry->NativeEntity(ArrivalRelay) || !activator || registry->NativeEntity(WagonName) != activator) {
                    state.relayReason = "Source AI arrival relay owner, key, or generation differs";
                } else if (Deliver(activator, &event.useKey, why)) {
                    ++state.relayDeliveries;
                } else {
                    state.relayReason = why.c_str();
                }
            }
        }
        idStr reason;
        if (state.poseReason.empty() && !visuals->UpdatePose(reason)) {
            state.poseReason = reason.c_str();
            gameLocal.Warning("WOLF2009_SOURCE_AI_POSE_REFUSED actor=%s reason=%s", profile.name, reason.c_str());
        }
        if (!state.brainBound || state.rootRefusals) return;

        ++state.brainTicks;
        state.firstBrainTimeMs = gameLocal.time;
        SpawnConditions conditions(body, spawnRoot);
        SingleDestination random;
        // Work on a temporary selector state. OnSpawn's priority 10 beats any
        // ready OnUsed (30). The selected target has no handler here, so retain
        // the prior graph state and pending Use with no task/exit side effects.
        auto candidate = flow;
        const auto decision = wolf2009::SelectBrainTransition(graph, spawnRoot, candidate, conditions, random);
        ++state.rootRefusals;
        state.stopRoot = graph.nodes[spawnRoot].id;
        if (decision.status == wolf2009::BrainFlowDecision::Transition) {
            const auto& target = graph.nodes[decision.targetNode];
            state.stopNode = target.id;
            state.stopType = target.type;
            state.reason = "Source AI first destination has no admitted native task handler";
        } else {
            state.reason = decision.reason.empty() ? "Source AI OnSpawn found no eligible destination" : decision.reason;
        }
        gameLocal.Printf("WOLF2009_SOURCE_AI_HOLD actor=%s root=%u node=%u type=%u pending_key=%s time=%d reason=%s\n",
                         profile.name, state.stopRoot, state.stopNode, state.stopType,
                         use.pending ? use.key.c_str() : "", state.firstBrainTimeMs, state.reason.c_str());
    }

    WolfSourceAIActorState state;
    std::unique_ptr<WolfSourceAIVisuals> visuals;
    idEntityPtr<idEntity> body, host;
    std::shared_ptr<WolfNativeSourceRegistry> registry;
    WolfSourceDirector* director = nullptr;
    wolf2009::BrainTaskUse use;
  private:
    const ActorProfile& profile;
    wolf2009::BrainGraph graph;
    wolf2009::BrainFlowState flow;
    std::vector<wolf2009::SourceOnUsedRoot> usedRoots;
    std::size_t spawnRoot = wolf2009::BrainRecord::None();
    bool dispatching = false, arrivalFinished = false;
};
} // namespace

idWolfSourceAIThink::idWolfSourceAIThink() {
    Need(spawningActor && *spawningActor, "Source AI event host requires a live assembly owner");
    owner = *spawningActor;
}

void idWolfSourceAIThink::Think() {
    if (const auto actor = owner.lock()) {
        if (actor->Alive() && actor->state.thinking) {
            try {
                actor->Tick();
                return;
            } catch (const std::exception& error) {
                actor->state.thinking = false;
                actor->state.reason = error.what();
                gameLocal.Warning("WOLF2009_SOURCE_AI_THINK_REFUSED actor=%s reason=%s", actor->state.name.c_str(), error.what());
            }
        }
    }
    BecomeInactive(TH_THINK);
}

void idWolfSourceAIThink::Event_Activate(idEntity* activator) {
    if (const auto actor = owner.lock()) actor->Activated(activator);
}

struct WolfSourceAIShell::Impl {
    std::vector<std::shared_ptr<Actor>> actors;
    Actor* Find(const std::string& name) const {
        for (const auto& actor : actors) if (actor->state.name == name) return actor.get();
        return nullptr;
    }
    Impl() { ++liveShells; }
    ~Impl() { actors.clear(); --liveShells; }
};

WolfSourceAIShell::WolfSourceAIShell() : impl(new Impl) {}
WolfSourceAIShell::~WolfSourceAIShell() = default;

bool WolfSourceAIShell::Spawn(const WolfSourceDeclarationSet& declarations, const std::string& exactEntities,
                              const std::string& materialArchive, std::uint64_t generation, WolfSourceDirector* director,
                              std::shared_ptr<WolfNativeSourceRegistry> registry, idStr& reason) {
    reason.Clear();
    try {
        Need(impl->actors.empty() && generation && registry && registry->Generation() == generation,
             "Source AI shell assembly state or map generation differs");
        Need(exactEntities == EntitiesPath && !gameLocal.isMultiplayer && !gameLocal.isClient,
             "SourceAIShellV1 requires the original local trainyard session");
        const auto document = wolf2009::DecodeEntities4(ReadExact(exactEntities, wolf2009::EntityLimits().sourceBytes));
        std::vector<std::shared_ptr<Actor>> pending;
        for (const auto& profile : Actors) {
            Need(!gameLocal.FindEntity(profile.name), "Source AI native actor name is occupied");
            auto actor = std::make_shared<Actor>(profile, generation);
            actor->Prepare(FindExact(document, profile.name), declarations);
            actor->registry = registry;
            if (std::string(profile.name) == "train_greeter") actor->director = director;
            pending.push_back(std::move(actor));
        }
        for (auto& actor : pending) {
            actor->visuals = Wolf2009_SpawnSourceAIVisuals(
                declarations, FindExact(document, actor->state.name.c_str()), materialArchive, reason);
            NeedReason(actor->visuals && actor->visuals->Body() && actor->visuals->Head(), reason,
                       "Source AI presentation assembly failed");
            actor->body = actor->visuals->Body();
            actor->state.spawnId = unsigned(actor->body.GetSpawnId());
            actor->visuals->SetHidden(actor->state.hidden);
            Need(actor->body.GetEntity()->IsHidden() == actor->state.hidden, "Source AI body post-spawn Hide differs");
            const std::string hostName = "wolf2009_source_ai_" + actor->state.name;
            Need(!gameLocal.FindEntity(hostName.c_str()), "Source AI event host name is occupied");
            idDict args;
            args.Set("classname", "wolf2009_source_ai_event_host");
            args.Set("name", hostName.c_str());
            args.Set("noclipmodel", "1");
            SpawnContext context(actor);
            std::unique_ptr<idEntity> host(gameLocal.SpawnEntityType(idWolfSourceAIThink::Type, &args));
            Need(host && host->GetType() == &idWolfSourceAIThink::Type, "Source AI native event host allocation failed");
            actor->host = host.release();
        }
        for (auto& actor : pending) {
            registry->Bind(actor->state.name, actor->state.classname, actor->body.GetEntity(), nullptr);
            actor->state.thinking = !actor->state.hidden;
            if (actor->state.thinking) actor->host.GetEntity()->BecomeActive(TH_THINK);
            gameLocal.Printf("WOLF2009_SOURCE_AI_SPAWN profile=SourceAIShellV1 actor=%s hidden=%d brain=%s spawn_id=%u\n",
                             actor->state.name.c_str(), int(actor->state.hidden),
                             actor->state.brainResource.c_str(), actor->state.spawnId);
        }
        impl->actors.swap(pending);
        return true;
    } catch (const std::exception& error) {
        reason = error.what();
        return false;
    }
}

bool WolfSourceAIShell::Activate(const std::string& name, idEntity* activator, idStr& reason) {
    if (auto* actor = impl->Find(name)) return actor->Deliver(activator, nullptr, reason);
    reason = "Source AI Activate target is outside the trainyard profile";
    return false;
}

bool WolfSourceAIShell::Use(const std::string& name, const std::string& key, idEntity* activator, idStr& reason) {
    if (auto* actor = impl->Find(name)) return actor->Deliver(activator, &key, reason);
    reason = "Source AI Use target is outside the trainyard profile";
    return false;
}

bool WolfSourceAIShell::Query(const std::string& name, WolfSourceAIActorState& state, idStr& reason) const {
    const auto* actor = impl->Find(name);
    if (!actor || !actor->Alive()) {
        reason = "Source AI actor generation is absent or expired";
        return false;
    }
    reason.Clear();
    state = actor->state;
    state.pendingUse = actor->use.pending;
    state.pendingKey = actor->use.key;
    return true;
}

unsigned WolfSourceAIShell::Count() const { return unsigned(impl->actors.size()); }

idEntity* WolfSourceAIShell::Body(const std::string& name) const {
    const auto* actor = impl->Find(name);
    return actor && actor->Alive() ? actor->body.GetEntity() : nullptr;
}

idEntity* WolfSourceAIShell::Head(const std::string& name) const {
    const auto* actor = impl->Find(name);
    return actor && actor->Alive() ? actor->visuals->Head() : nullptr;
}

unsigned WolfSourceAIShell::AccessoryCount(const std::string& name) const {
    const auto* actor = impl->Find(name);
    return actor && actor->Alive() ? actor->visuals->AccessoryCount() : 0;
}

idEntity* WolfSourceAIShell::Accessory(const std::string& name, unsigned index) const {
    const auto* actor = impl->Find(name);
    return actor && actor->Alive() && index < actor->visuals->AccessoryCount() ? actor->visuals->Accessory(index) : nullptr;
}

int Wolf2009_LiveSourceAIShells() { return liveShells; }
int Wolf2009_LiveSourceAIActors() { return liveActors; }
int Wolf2009_LiveSourceAIBrains() { return liveBrains; }
int Wolf2009_LiveSourceAIPendingUses() { return livePendingUses; }

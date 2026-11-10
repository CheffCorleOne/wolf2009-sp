// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceAIShell.h"
#include "NativeSourceAIVisuals.h"
#include "BrainTask.h"
#include "Entities.h"
#include "SourceDefinitions.h"
#include "TrainyardExit.h"
#include "SourceAnimCommands.h"
#include <algorithm>
#include <map>
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
    // SourceInterrogatorChainV1: the actor's own network runs through the held
    // stepper and the SourceBrainActionsV1 handlers instead of the first-hold rule.
    bool chain;
};
const ActorProfile Actors[] = {
    {"train_greeter", "trainyard_greeter", "train_greeter", false, 2, 49, false},
    {"nazi_interragator_1", "trainyard_nazi_talker_1", "nazi_interragator", true, 11, 19, true},
    {"nazi_interragator_2", "trainyard_nazi_talker_2", "", true, 0, 0, false},
    {"trainyard_hostage_1", "trainyard_allies01", "", true, 0, 0, false},
    {"trainyard_hostage_2", "trainyard_allies02", "", true, 0, 0, false}
};
const char* const ChainProfile = "SourceInterrogatorChainV1";
const char* const MapModelDefinition = "model_trainyard_base";

// SourceAIScriptedAnimV1 execution clips (docs/AI-PRESENTATION.md §5.1). The
// expected blocks are the editor .def text; the compiled model_trainyard_base
// declaration must parse to exactly these before admission.
const char* const ClipAnimation = "wolf2009/main/hkxa/maps/game/trainyard/trainyard";
const char* const ClipSkeletons = "wolf2009/sp_global/skel/maps/game/global/global";
const char* const ClipSkeleton = "models/characters/_humanoid_male/humanoid_male.hkx";
const char* const ClipDirectory = "models/characters/_humanoid_male/";
struct ExecutionClip {
    const char* actor;
    const char* logical;
    const char* stem;   // HKXA clip and authored .md5anim stem
    int frames;
    const char* block;  // canonical parsed block, or null for a pure reference
};
const ExecutionClip ExecutionClips[] = {
    {"nazi_interragator_1", "srp_tyd_execute_nzi_one", "srp_tyd_execute_nzi_01", 273,
     "flags=0x0;f111 speak lipsync_trn_1001_wi1;f206 trigger execution_gunshot"},
    {"nazi_interragator_1", "srp_tyd_execute_nzi_01_start_idle", "srp_tyd_execute_nzi_01_start_idle", 2, nullptr},
    {"nazi_interragator_2", "srp_tyd_execute_nzi_two", "srp_tyd_execute_nzi_02", 281,
     "flags=0x0;f96 speak lipsync_trn_1006_wi2;f206 trigger execution_gunshot2"},
    {"nazi_interragator_2", "srp_tyd_execute_nzi_02_start_idle", "srp_tyd_execute_nzi_02_start_idle", 2, nullptr},
    {"trainyard_hostage_1", "srp_tyd_execute_kc_one", "srp_tyd_execute_kc_01", 283,
     "flags=0x80000;f206 fx_offset fx_execute_blood01 head_channel 0 0 0 0 0 0;f206 trigger blood_decal_1;f229 interactive_ragdoll"},
    {"trainyard_hostage_1", "srp_tyd_execute_kc_01_start_idle", "srp_tyd_execute_kc_01_start_idle", 2, nullptr},
    {"trainyard_hostage_2", "srp_tyd_execute_kc_two", "srp_tyd_execute_kc_02", 285,
     "flags=0x80000;f206 fx_offset fx_execute_blood01 head_channel 0 0 0 0 0 0;f206 trigger blood_decal_2;f229 interactive_ragdoll"},
    {"trainyard_hostage_2", "srp_tyd_execute_kc_02_start_idle", "srp_tyd_execute_kc_02_start_idle", 2, nullptr}};
// Frame-command trigger targets: exact ENTS identities and the keys this
// profile admits (Data, §5.2).
struct ExecutionTargetProfile { const char* name; const char* classname; const char* key; const char* value; };
const ExecutionTargetProfile ExecutionTargets[] = {
    {"execution_gunshot", "speaker", "s_shader", "ai_kar98_20ft"},
    {"execution_gunshot2", "speaker", "s_shader", "ai_kar98_60ft"},
    {"blood_decal_1", "func_static", "hide", "1"},
    {"blood_decal_2", "func_static", "hide", "1"}};

int liveShells = 0, liveActors = 0, liveBrains = 0, livePendingUses = 0, liveExecutionTargets = 0;

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

// Native owner of one frame-command trigger target. activate is recorded
// synchronously; the original speaker/func_static responses are not admitted.
class idWolfSourceExecutionTarget : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceExecutionTarget);
    idWolfSourceExecutionTarget() { ++liveExecutionTargets; }
    ~idWolfSourceExecutionTarget() { --liveExecutionTargets; }
    void Spawn() {
        fl.networkSync = false;
        fl.neverDormant = true;
        cinematic = true;
    }
    void Save(idSaveGame*) const { gameLocal.Error("Source AI execution target save state is unsupported"); }
    void Restore(idRestoreGame*) { gameLocal.Error("Source AI execution target restore is unsupported"); }
    WolfSourceAIExecutionTargetState target;
  private:
    void Event_Activate(idEntity* activator) {
        ++target.activations;
        target.lastActivator = activator ? activator->GetName() : "";
        target.lastTimeMs = gameLocal.time;
        gameLocal.Printf("WOLF2009_SOURCE_AI_EXEC_TARGET name=%s class=%s activator=%s activations=%u time=%d effect=\"%s\"\n",
                         target.name.c_str(), target.classname.c_str(), target.lastActivator.c_str(), target.activations,
                         target.lastTimeMs, target.effect.c_str());
    }
};
CLASS_DECLARATION(idEntity, idWolfSourceExecutionTarget)
    EVENT(EV_Activate, idWolfSourceExecutionTarget::Event_Activate)
END_CLASS

namespace {
class Actor;
typedef std::vector<std::shared_ptr<Actor>> Peers;

// Subject resolution for a network owned by one shell actor (Focus subset of
// SourceBrainActionsV1): Me, Player, and exact names of live shell actors or
// other live entities. A stale actor resolves to null, as a removed entity does.
class ChainResolver : public WolfSourceBrainActionResolver {
  public:
    ChainResolver(Actor& owner, const Peers* peers) : owner(owner), peers(peers) {}
    WolfSourceSubject Resolve(const wolf2009::BrainEntityReference& reference) override;
    WolfSourcePlayerScriptState& PlayerState() override { return player; }
    void SetPeers(const Peers* value) { peers = value; }
  private:
    Actor& owner;
    const Peers* peers;
    WolfSourcePlayerScriptState player;
};

class Actor : public WolfSourceAIActionTarget {
  public:
    explicit Actor(const ActorProfile& profile, std::uint64_t generation) : profile(profile) {
        state.name = profile.name;
        state.classname = profile.classname;
        state.levelGeneration = generation;
        ++liveActors;
    }
    ~Actor() {
        network.reset();
        streams.reset();
        animator.reset();
        if (auto* entity = host.GetEntity()) delete entity;
        visuals.reset();
        if (state.brainBound) --liveBrains;
        if (use.pending) --livePendingUses;
        --liveActors;
    }

    // WolfSourceAIActionTarget
    const std::string& SourceName() const override { return state.name; }
    idEntity* NativeBody() const override { return Alive() ? body.GetEntity() : nullptr; }
    bool Dead() const override { return animator && animator->Dead(); }
    bool Hidden() const override { return state.hidden; }
    bool PostSpawnPending() const override { return !postSpawned; }
    WolfSourceAIScriptState& Script() override { return state.script; }
    void ScriptShow() override {
        Need(Alive() && state.hidden, "Source AI script Show requires a live hidden actor");
        visuals->SetHidden(false);
        state.hidden = false;
        state.thinking = true;
        host.GetEntity()->BecomeActive(TH_THINK);
        gameLocal.Printf("WOLF2009_SOURCE_AI_SHOW actor=%s cause=SetScriptedManual time=%d wakes=%u activated=%d\n",
                         state.name.c_str(), gameLocal.time, state.wakes, int(state.activated));
    }

    // SourceAIScriptedAnimV1 overrides (stage 2).
    bool ScriptedAnimAdmitted() const override { return animator && Alive(); }
    bool ScriptedAnim(const std::string& name, int blendFrames, int numLoops, bool endWithIdle, const std::string& task,
                      std::string& effect) override {
        if (!animator || !Alive()) { effect = "actor has no live SourceAIScriptedAnimV1 animator"; return false; }
        if (Dead()) { effect = "dead AI (ai+0x954 bit 0x4): Begin returns false"; return false; }
        std::string shown = "visible";
        // Begin +0x458: Show only when hidden and ai+0x955 bit 0x2 is clear.
        if (state.hidden) {
            if (PostSpawnPending()) shown = "hidden; post-spawn flag set, not shown";
            else { ScriptShow(); shown = "shown by Begin"; }
        }
        const bool ok = animator->ScriptedAnim(name, blendFrames, numLoops, endWithIdle, task, effect);
        effect = "Begin(" + shown + ", scripted bit 0x20, endWithIdle=" + (endWithIdle ? "1" : "0") +
                 ", movement stop: no native locomotion); " + effect;
        return ok;
    }
    bool AnimDoneByName(const std::string& name, int blendFrames, int& remaining, std::string& current) const override {
        if (!animator || !Alive()) { remaining = -1; current.clear(); return true; }
        return animator->AnimDoneByName(name, blendFrames, remaining, current);
    }
    bool Kill(bool bypass, std::string& effect) override {
        if (!animator || !Alive()) { effect = "actor has no live SourceAIScriptedAnimV1 animator"; return false; }
        return animator->Kill(bypass, state.script.deathOverride, effect);
    }

    // Admits this actor's execution clips from the compiled model declaration.
    void PrepareClips() {
        for (const auto& entry : ExecutionClips) {
            if (state.name != entry.actor) continue;
            const auto* reference = animations.Find(entry.logical);
            Need(reference && reference->logicalName.text == entry.logical && reference->hasLiteralAssetReference &&
                 !reference->origins.empty(), "SourceAIScriptedAnimV1 execution clip is not declared by the compiled model");
            Need(Fold(reference->assetReference.text) == std::string(ClipDirectory) + entry.stem + ".md5anim",
                 "SourceAIScriptedAnimV1 execution clip asset reference differs");
            const auto& values = reference->origins[0].data.values;
            for (const auto& origin : reference->origins) {
                Need(origin.data.values.size() == values.size(), "SourceAIScriptedAnimV1 inherited clip blocks conflict");
                for (std::size_t i = 0; i < values.size(); ++i)
                    Need(origin.data.values[i].text == values[i].text && origin.data.values[i].brace == values[i].brace,
                         "SourceAIScriptedAnimV1 inherited clip block bytes conflict");
            }
            WolfSourceAIClipProfile clip;
            clip.logicalName = entry.logical;
            clip.frames = entry.frames;
            std::string canonical;
            if (reference->shape == wolf2009::SourceAnimationReferenceShape::SingleLiteralNoTail) {
                Need(values.size() == 2, "SourceAIScriptedAnimV1 pure clip has trailing tokens");
            } else {
                Need(reference->shape == wolf2009::SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock &&
                     values.size() >= 4 && values[2].brace == '{' && values.back().brace == '}',
                     "SourceAIScriptedAnimV1 clip block shape is unsupported");
                std::vector<std::string> inner;
                for (std::size_t i = 3; i + 1 < values.size(); ++i) {
                    Need(!values[i].brace, "SourceAIScriptedAnimV1 nested clip block");
                    inner.push_back(values[i].text);
                }
                clip.block = wolf2009::ParseSourceAnimBlockV1(inner, std::uint32_t(entry.frames));
                std::string flags;
                for (std::uint32_t v = clip.block.flags; ; v >>= 4) { flags.insert(flags.begin(), "0123456789abcdef"[v & 15]); if (v < 16) break; }
                canonical = "flags=0x" + flags;
                for (const auto& command : clip.block.commands) {
                    canonical += ";f" + std::to_string(command.frame) + " " + command.name;
                    for (const auto& argument : command.arguments) canonical += " " + argument;
                }
            }
            Need(entry.block ? canonical == entry.block : canonical.empty(),
                 ("SourceAIScriptedAnimV1 compiled block differs from the editor .def: " + canonical).c_str());
            clipProfiles.push_back(clip);
            WolfSourceAIClipSelection selection;
            selection.logicalName = entry.logical;
            selection.clipName = std::string(ClipDirectory) + entry.stem + ".hkx";
            clipRequest.clips.push_back(selection);
            gameLocal.Printf("WOLF2009_SOURCE_AI_CLIP_ADMITTED actor=%s profile=SourceAIScriptedAnimV1 clip=%s hkxa=%s frames=%d "
                             "block=\"%s\"\n", state.name.c_str(), entry.logical, selection.clipName.c_str(), entry.frames,
                             canonical.c_str());
        }
        if (!clipRequest.clips.empty()) {
            clipRequest.animationResource = ClipAnimation;
            clipRequest.animationSkeletonResource = ClipSkeletons;
            clipRequest.animationSkeletonName = ClipSkeleton;
        }
    }
    // After the visuals spawned the clip set: bind native anim numbers.
    void BindAnimator(wolf2009::SourceGameLcg& lcg) {
        if (clipProfiles.empty()) return;
        const auto& native = visuals->Clips();
        Need(native.size() == clipProfiles.size(), "SourceAIScriptedAnimV1 native clip count differs");
        for (std::size_t i = 0; i < native.size(); ++i) {
            auto& clip = clipProfiles[i];
            Need(native[i].logicalName == clip.logicalName && native[i].frames == clip.frames && native[i].animNumber > 0,
                 "SourceAIScriptedAnimV1 native clip frame count or identity differs");
            clip.animNumber = native[i].animNumber;
            clip.lengthMs = native[i].lengthMs;
            state.admittedClips.push_back(clip.logicalName);
        }
        animator.reset(new WolfSourceAIScriptedAnimator(state.name, visuals->Body(), clipProfiles, declaredClips, lcg, deathKeys));
        state.animationAdmitted = true;
    }

    void Prepare(const wolf2009::EntityRecord& record, const WolfSourceDeclarationSet& declarations) {
        const auto placement = wolf2009::ReadEntityPlacement(record);
        Need(placement.name == profile.name && placement.classname == profile.classname && placement.hasOrigin,
             "Source AI actor ENTS identity or origin differs");
        const auto definition = declarations.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef, profile.classname);
        Need(DefinitionText(definition, "spawnclass") == "idAI", "Source AI actor has a different compiled spawnclass");
        PrepareScript(record, definition, declarations);
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
        if (profile.chain) PrepareChain();
        state.brainBound = true;
        ++liveBrains;
    }

    // Spawn args the recovered SetScriptedManual(false) restores, and the
    // model animation names the idle consumer resolves against.
    void PrepareScript(const wolf2009::EntityRecord& record, const wolf2009::ResolvedSourceDefinition& definition,
                       const WolfSourceDeclarationSet& declarations) {
        auto optionalBool = [&](const char* key) {
            try { return BoolField(record, definition, key); } catch (const std::exception&) { return false; }
        };
        state.script.spawnDisableAnnouncements = optionalBool("disableAnnouncements");
        state.script.spawnIdleLookForEnemy = optionalBool("idleLookForEnemy");
        state.script.announcementsOff = state.script.spawnDisableAnnouncements;
        state.script.idleLookForEnemy = state.script.spawnIdleLookForEnemy;
        // Death keys (ai+0x956 bits 0x4/0x10 Inferred from these names).
        deathKeys.playDeathAnim = optionalBool("playDeathAnim");
        deathKeys.ragdollOnDeath = optionalBool("ragdollOnDeath");
        try { deathKeys.deathAnimOverride = TextField(record, definition, "deathAnimOverride"); } catch (const std::exception&) {}
        try {
            wolf2009::SourceDefinitionContext context;
            context.mapModelDefinition = MapModelDefinition;
            const auto model = DefinitionText(definition, "model");
            animations = declarations.Definitions().ResolveModelAnimations(model, context);
            for (const auto& entry : animations.references) {
                animationKeys.insert(entry.first);
                declaredClips.push_back(entry.second.logicalName.text);
            }
            state.modelAnimationsReason = model + ": " + std::to_string(animationKeys.size()) + " declared animations";
        } catch (const std::exception& error) {
            animationKeys.clear();
            state.modelAnimationsReason = std::string("model animations unresolved: ") + error.what();
        }
    }

    // SourceInterrogatorChainV1 data qualification: the measured OnSpawn
    // 1 -> 11 (19) -> 20 (19) -> 2 (33) -> 38 (54) normal path and the single
    // OnUsed root 12 -> 43 (54). Every admitted node must decode.
    void PrepareChain() {
        auto normal = [this](unsigned id) -> const wolf2009::BrainNode& {
            const auto* node = graph.FindNode(id);
            Need(node != nullptr, "SourceInterrogatorChainV1 node is absent");
            const wolf2009::BrainNode* target = nullptr;
            for (auto index : node->outputs) {
                if (!(flow.outputs[index].flags & 0x10u) || graph.outputs[index].destinations.size() != 1) continue;
                Need(!target, "SourceInterrogatorChainV1 node has more than one normal output");
                target = &graph.nodes[graph.outputs[index].destinations[0].targetNode];
            }
            Need(target != nullptr, "SourceInterrogatorChainV1 normal output is absent");
            return *target;
        };
        const unsigned path[][2] = {{1, 11}, {11, 20}, {20, 2}, {2, 38}};
        const unsigned types[] = {19, 19, 33, 54};
        for (unsigned i = 0; i < 4; ++i) {
            const auto& next = normal(path[i][0]);
            Need(next.id == path[i][1] && next.type == types[i], "SourceInterrogatorChainV1 OnSpawn path differs");
        }
        const auto* end = graph.FindNode(38);
        for (auto index : end->outputs) Need(!(flow.outputs[index].flags & 0x10u), "SourceInterrogatorChainV1 node 38 gained a normal output");
        Need(normal(12).id == 43, "SourceInterrogatorChainV1 OnUsed path differs");
        for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
            if (!wolf2009::IsBrainActionType(graph.nodes[n].type)) continue;
            wolf2009::BrainActionSpec spec;
            std::string why;
            Need(wolf2009::DecodeBrainActionV1(graph, n, spec, why), why.c_str());
            specs[n] = spec;
        }
        state.chainProfile = ChainProfile;
        streams.reset(new WolfSourceVoiceStreams);
        network.reset(new Network(*this));
    }

    bool Declared(const std::string& name) const {
        std::string key = name;
        for (char& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return animationKeys.count(key) != 0;
    }
    static bool DeclaredThunk(const void* context, const std::string& name) {
        return static_cast<const Actor*>(context)->Declared(name);
    }
    void UpdateAnimationState() {
        if (Wolf2009_UpdateSourceAIIdle(*this, &Actor::DeclaredThunk, this))
            gameLocal.Printf("WOLF2009_SOURCE_AI_IDLE_STATE actor=%s profile=SourceAIIdleOverrideV1 %s\n",
                             state.name.c_str(), state.modelAnimationsReason.c_str());
    }

    // Held stepper host over this actor's own network.
    class Network : public wolf2009::BrainHeldStepHost {
      public:
        explicit Network(Actor& actor) : actor(actor), resolver(actor, nullptr), stepper(actor.graph, *this) {}
        bool Root(const wolf2009::BrainGraph& g, std::size_t node, std::uint32_t& priority) override {
            // Descriptor priorities: OnSpawn 10, OnUsed 30 (AI-ACTOR §3.3).
            priority = g.nodes[node].type == 1 ? 10 : 30;
            return g.nodes[node].type == 1 || g.nodes[node].type == 3;
        }
        wolf2009::BrainConditionValue RootReady(const wolf2009::BrainGraph& g, std::size_t root) override {
            return Primary(g, root);
        }
        wolf2009::BrainConditionValue Primary(const wolf2009::BrainGraph& g, std::size_t node) override {
            if (node == actor.spawnRoot)
                return actor.body.GetEntity() ? wolf2009::BrainConditionValue::True : wolf2009::BrainConditionValue::False;
            const auto spec = actor.specs.find(node);
            if (spec != actor.specs.end() && spec->second.kind == wolf2009::BrainActionKind::Animate) {
                // Embedded AnimFinished 50 on an entered Animate task.
                auto task = actor.animateTasks.find(node);
                if (task == actor.animateTasks.end() || !task->second.entered) return wolf2009::BrainConditionValue::Unsupported;
                return Wolf2009_EvaluateSourceAnimFinished(spec->second, resolver, task->second)
                           ? wolf2009::BrainConditionValue::True : wolf2009::BrainConditionValue::False;
            }
            if (g.nodes[node].type == 3) {
                for (const auto& root : actor.usedRoots)
                    if (root.node == g.nodes[node].id)
                        return actor.use.Matches(root.key) ? wolf2009::BrainConditionValue::True
                                                           : wolf2009::BrainConditionValue::False;
            }
            return wolf2009::BrainConditionValue::Unsupported;
        }
        wolf2009::BrainConditionValue Explicit(const wolf2009::BrainGraph&, std::size_t) override {
            return wolf2009::BrainConditionValue::Unsupported;
        }
        bool Admits(const wolf2009::BrainGraph& g, std::size_t node, std::string& reason) override {
            if (actor.specs.count(node)) return true;
            reason = "type " + std::to_string(g.nodes[node].type) + " has no admitted native handler";
            return false;
        }
        void Enter(const wolf2009::BrainGraph& g, std::size_t node, bool probe) override {
            const auto& spec = actor.specs.at(node);
            if (spec.kind == wolf2009::BrainActionKind::Animate) {
                WolfSourceBrainActionReceipt receipt;
                receipt.brain = actor.state.brainResource;
                receipt.probe = probe;
                auto& task = actor.animateTasks[node];
                if (!Wolf2009_EnterSourceAnimate(spec, resolver, receipt, task,
                                                 "brain task " + std::to_string(spec.nodeId) + " Animate"))
                    throw std::runtime_error("Source brain action refused at node " + std::to_string(g.nodes[node].id) + ": " + receipt.effect);
                if (actor.state.actions.size() < 256) actor.state.actions.push_back(receipt);
                return;
            }
            if (spec.kind == wolf2009::BrainActionKind::ScriptedVOStreamIn) {
                const auto subject = resolver.Resolve(spec.subject);
                idEntity* owner = subject.kind == WolfSourceSubject::AI ? subject.ai->NativeBody() : subject.entity;
                actor.streams->Start(spec, actor.state.brainResource, owner, actor.state.levelGeneration, ++actor.eventIds);
                return;
            }
            WolfSourceBrainActionReceipt receipt;
            receipt.brain = actor.state.brainResource;
            receipt.probe = probe;
            if (!Wolf2009_ApplySourceBrainAction(spec, resolver, receipt))
                throw std::runtime_error("Source brain action refused at node " + std::to_string(g.nodes[node].id) + ": " + receipt.effect);
            if (actor.state.actions.size() < 256) actor.state.actions.push_back(receipt);
        }
        void ExitRoot(const wolf2009::BrainGraph& g, std::size_t root) override {
            if (g.nodes[root].type != 3) return; // OnSpawn Exit is a no-op
            for (const auto& used : actor.usedRoots) {
                if (used.node != g.nodes[root].id || !actor.use.Matches(used.key)) continue;
                actor.use.Clear();
                --livePendingUses;
            }
        }
        Actor& actor;
        ChainResolver resolver;
        wolf2009::BrainHeldStepper stepper;
    };

    void TickChain() {
        auto result = network->stepper.Tick();
        state.brainTransitions += unsigned(result.transitions);
        state.brainProbes += unsigned(result.probes);
        if (result.status == wolf2009::BrainHeldStepResult::Failed) {
            state.brainFailed = true;
            state.reason = result.reason;
            gameLocal.Warning("WOLF2009_SOURCE_AI_CHAIN_REFUSED actor=%s reason=%s", profile.name, result.reason.c_str());
        }
        const auto active = network->stepper.Active();
        if (result.transitions || result.probes)
            gameLocal.Printf("WOLF2009_SOURCE_AI_CHAIN actor=%s profile=%s transitions=%u probes=%u active=%u time=%d pending_key=%s\n",
                             profile.name, ChainProfile, unsigned(result.transitions), unsigned(result.probes),
                             active == wolf2009::BrainRecord::None() ? 0u : graph.nodes[active].id, gameLocal.time,
                             use.pending ? use.key.c_str() : "");
        if (result.status == wolf2009::BrainHeldStepResult::Held) {
            const bool known = result.heldNode != wolf2009::BrainRecord::None();
            const unsigned node = known ? graph.nodes[result.heldNode].id : 0u, type = known ? graph.nodes[result.heldNode].type : 0u;
            const bool sourced = result.heldSource != wolf2009::BrainRecord::None();
            const unsigned source = sourced ? graph.nodes[result.heldSource].id : 0u;
            if (node != state.stopNode || source != state.heldSourceNode) {
                ++state.holds;
                state.stopNode = node;
                state.stopType = type;
                state.heldSourceNode = source;
                state.stopRoot = sourced && (graph.nodes[result.heldSource].type == 1 || graph.nodes[result.heldSource].type == 3) ? source : 0;
                state.reason = result.reason;
                gameLocal.Printf("WOLF2009_SOURCE_AI_HOLD actor=%s profile=%s from=%u node=%u type=%u pending_key=%s time=%d reason=%s\n",
                                 profile.name, ChainProfile, source, node, type, use.pending ? use.key.c_str() : "",
                                 gameLocal.time, result.reason.c_str());
            }
        }
        streams->Update();
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
                } else if ((!brainHost || (brainHost->HostedRelay(event, activator), true)) &&
                           Deliver(activator, &event.useKey, why)) {
                    ++state.relayDeliveries;
                } else {
                    state.relayReason = why.c_str();
                }
            }
        }
        // idAI Think order: brain networks tick before the animation update.
        if (network && !state.brainFailed) {
            ++state.brainTicks;
            if (state.firstBrainTimeMs < 0) state.firstBrainTimeMs = gameLocal.time;
            TickChain();
        }
        if (brainHost) {
            // SourceAIShellGreeterRouteV1: the host runs the greeter network
            // against this actor's own pending-key latch.
            const bool wasPending = use.pending;
            ++state.brainTicks;
            if (state.firstBrainTimeMs < 0) state.firstBrainTimeMs = gameLocal.time;
            brainHost->HostedThink(use);
            if (wasPending && !use.pending) --livePendingUses;
            if (!wasPending && use.pending) ++livePendingUses;
        }
        UpdateAnimationState();
        // SourceAIScriptedAnimV1: state thread, animation update, frame service.
        if (animator) animator->Update(state.script.scriptedManual);
        idStr reason;
        if (state.poseReason.empty() && !visuals->UpdatePose(reason)) {
            state.poseReason = reason.c_str();
            gameLocal.Warning("WOLF2009_SOURCE_AI_POSE_REFUSED actor=%s reason=%s", profile.name, reason.c_str());
        }
        if (brainHost || network || !state.brainBound || state.rootRefusals) return;

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
    WolfSourceAIBrainHost* brainHost = nullptr;
    wolf2009::BrainTaskUse use;
    bool postSpawned = false;
    void SetPeers(const Peers* peers) { if (network) network->resolver.SetPeers(peers); }
    const std::vector<std::pair<unsigned, WolfSourceAnimateTask>> AnimateTasks() const {
        std::vector<std::pair<unsigned, WolfSourceAnimateTask>> out;
        for (const auto& task : animateTasks) out.push_back(std::make_pair(graph.nodes[task.first].id, task.second));
        return out;
    }
    std::unique_ptr<WolfSourceAIScriptedAnimator> animator;
    WolfSourceAIClipRequest clipRequest;
    std::map<std::size_t, WolfSourceAnimateTask> animateTasks;
    std::vector<WolfSourceVoiceStreamReceipt> VoiceReceipts() const {
        return streams ? streams->Receipts() : std::vector<WolfSourceVoiceStreamReceipt>();
    }
  private:
    std::map<std::size_t, wolf2009::BrainActionSpec> specs;
    std::set<std::string> animationKeys;
    wolf2009::ResolvedSourceModelAnimations animations;
    std::vector<std::string> declaredClips;
    std::vector<WolfSourceAIClipProfile> clipProfiles;
    WolfSourceAIDeathKeys deathKeys;
    std::unique_ptr<WolfSourceVoiceStreams> streams;
    std::unique_ptr<Network> network;
    std::uint64_t eventIds = 0;
    const ActorProfile& profile;
    wolf2009::BrainGraph graph;
    wolf2009::BrainFlowState flow;
    std::vector<wolf2009::SourceOnUsedRoot> usedRoots;
    std::size_t spawnRoot = wolf2009::BrainRecord::None();
    bool dispatching = false, arrivalFinished = false;
};

WolfSourceSubject ChainResolver::Resolve(const wolf2009::BrainEntityReference& reference) {
    WolfSourceSubject out;
    switch (reference.mode) {
    case wolf2009::BrainEntityMode::None: return out;
    case wolf2009::BrainEntityMode::Me:
        if (owner.Alive()) { out.kind = WolfSourceSubject::AI; out.ai = &owner; }
        return out;
    case wolf2009::BrainEntityMode::Player:
        if (auto* player = gameLocal.GetLocalPlayer()) { out.kind = WolfSourceSubject::Player; out.entity = player; }
        return out;
    case wolf2009::BrainEntityMode::Named: break;
    }
    if (peers) {
        for (const auto& peer : *peers) {
            if (peer->state.name != reference.name) continue;
            // A stale shell actor resolves to null: the original handle check
            // fails and the name lookup finds no live entity.
            if (peer->Alive()) { out.kind = WolfSourceSubject::AI; out.ai = peer.get(); }
            return out;
        }
    }
    if (auto* entity = gameLocal.FindEntity(reference.name.c_str())) {
        out.kind = entity == gameLocal.GetLocalPlayer() ? WolfSourceSubject::Player : WolfSourceSubject::Entity;
        out.entity = entity;
    }
    return out;
}
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
    // The shared game LCG (0x2063376c) outlives every actor's animator.
    wolf2009::SourceGameLcg lcg;
    std::vector<idEntityPtr<idWolfSourceExecutionTarget>> targets;
    std::vector<std::shared_ptr<Actor>> actors;
    WolfSourceAIBrainHost* greeterHost = nullptr;
    Actor* Find(const std::string& name) const {
        for (const auto& actor : actors) if (actor->state.name == name) return actor.get();
        return nullptr;
    }
    Impl() { ++liveShells; }
    ~Impl() {
        actors.clear();
        for (auto& target : targets) delete target.GetEntity();
        targets.clear();
        --liveShells;
    }
    idWolfSourceExecutionTarget* Target(const std::string& name) const {
        for (const auto& target : targets)
            if (auto* entity = target.GetEntity())
                if (entity->target.name == name && gameLocal.FindEntity(name.c_str()) == entity) return entity;
        return nullptr;
    }
};

WolfSourceAIShell::WolfSourceAIShell() : impl(new Impl) {}
WolfSourceAIShell::~WolfSourceAIShell() = default;

bool WolfSourceAIShell::HostGreeter(WolfSourceAIBrainHost* host, idStr& reason) {
    if (!host || !impl->actors.empty() || impl->greeterHost || !host->HostedBody()) {
        reason = "Source AI greeter host must be bound once, before Spawn, with its native body";
        return false;
    }
    reason.Clear();
    impl->greeterHost = host;
    return true;
}

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
            const bool greeter = std::string(profile.name) == "train_greeter";
            const bool hosted = greeter && impl->greeterHost;
            Need(hosted ? gameLocal.FindEntity(profile.name) == impl->greeterHost->HostedBody()
                        : !gameLocal.FindEntity(profile.name), "Source AI native actor name is occupied");
            auto actor = std::make_shared<Actor>(profile, generation);
            actor->Prepare(FindExact(document, profile.name), declarations);
            actor->PrepareClips();
            actor->registry = registry;
            if (greeter) actor->director = director;
            if (hosted) {
                actor->brainHost = impl->greeterHost;
                actor->state.hosted = true;
            }
            pending.push_back(std::move(actor));
        }
        for (auto& actor : pending) {
            actor->visuals = actor->brainHost
                ? Wolf2009_SpawnSourceAIVisualsOnBody(declarations, FindExact(document, actor->state.name.c_str()),
                                                      materialArchive, actor->brainHost->HostedBody(), reason)
                : !actor->clipRequest.clips.empty()
                ? Wolf2009_SpawnSourceAIVisualsWithClips(declarations, FindExact(document, actor->state.name.c_str()),
                                                         materialArchive, actor->clipRequest, reason)
                : Wolf2009_SpawnSourceAIVisuals(declarations, FindExact(document, actor->state.name.c_str()),
                                                materialArchive, reason);
            NeedReason(actor->visuals && actor->visuals->Body() && actor->visuals->Head(), reason,
                       "Source AI presentation assembly failed");
            actor->body = actor->visuals->Body();
            actor->BindAnimator(impl->lcg);
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
        // Frame-command trigger targets: one native owner per exact ENTS name.
        for (const auto& profile : ExecutionTargets) {
            const auto& record = FindExact(document, profile.name);
            const auto placement = wolf2009::ReadEntityPlacement(record);
            Need(placement.classname == profile.classname && placement.hasOrigin,
                 "SourceAIScriptedAnimV1 execution target identity differs");
            const auto* key = record.Find(profile.key);
            Need(key && ((key->kind == wolf2009::EntityProperty::Text && key->text == profile.value) ||
                         (key->kind == wolf2009::EntityProperty::Integer && std::to_string(key->integer) == profile.value)),
                 "SourceAIScriptedAnimV1 execution target key differs");
            Need(!gameLocal.FindEntity(profile.name), "SourceAIScriptedAnimV1 execution target name is occupied");
            idDict args;
            args.Set("name", profile.name);
            args.Set("noclipmodel", "1");
            args.Set("origin", va("%g %g %g", placement.origin[0], placement.origin[1], placement.origin[2]));
            std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourceExecutionTarget::Type, &args));
            Need(entity && entity->GetType() == &idWolfSourceExecutionTarget::Type, "SourceAIScriptedAnimV1 execution target allocation failed");
            auto* target = static_cast<idWolfSourceExecutionTarget*>(entity.get());
            target->target.name = profile.name;
            target->target.classname = profile.classname;
            target->target.live = true;
            target->target.effect = std::string(profile.classname) == "speaker"
                ? std::string("speaker activate received (s_waitfortrigger); ") + profile.value + " playback is not admitted (audio unverified)"
                : "activate on a hidden func_static received; its response is Unknown, no visible change";
            idEntityPtr<idWolfSourceExecutionTarget> handle;
            handle = target;
            entity.release();
            impl->targets.push_back(handle);
        }
        impl->actors.swap(pending);
        // idAI PostSpawn has run for every actor: ai+0x955 bit 0x2 is clear.
        for (auto& actor : impl->actors) {
            actor->postSpawned = true;
            actor->SetPeers(&impl->actors);
        }
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
    state.voice = actor->VoiceReceipts();
    if (actor->animator) state.animation = actor->animator->State();
    state.animateTasks = actor->AnimateTasks();
    return true;
}

bool WolfSourceAIShell::ExecutionTarget(const std::string& name, WolfSourceAIExecutionTargetState& out) const {
    for (const auto& target : impl->targets) {
        auto* entity = target.GetEntity();
        if (!entity || entity->target.name != name) continue;
        out = entity->target;
        out.live = gameLocal.FindEntity(name.c_str()) == entity;
        return true;
    }
    out = WolfSourceAIExecutionTargetState();
    out.name = name;
    return false;
}

idEntity* WolfSourceAIShell::ExecutionTargetEntity(const std::string& name) const { return impl->Target(name); }

namespace {
wolf2009::BrainActionSpec QAAnimateSpec(const std::string& actor, const std::string& clip) {
    wolf2009::BrainActionSpec spec;
    spec.kind = wolf2009::BrainActionKind::Animate;
    spec.nodeType = 51;
    spec.profile = "SourceAIScriptedAnimV1";
    spec.subject = wolf2009::ParseBrainEntityReferenceV1(actor);
    spec.text = clip;
    spec.blendFrames = 2;
    spec.numLoops = 0;
    spec.value = true;
    return spec;
}
}

bool WolfSourceAIShell::QAAnimate(const std::string& name, const std::string& clip, WolfSourceAnimateTask& task, std::string& effect) {
    auto* actor = impl->Find(name);
    if (!actor) { effect = "QA Animate actor is outside the trainyard profile"; return false; }
    ChainResolver resolver(*actor, &impl->actors);
    WolfSourceBrainActionReceipt receipt;
    receipt.brain = "qa";
    const bool ok = Wolf2009_EnterSourceAnimate(QAAnimateSpec(name, clip), resolver, receipt, task, "qa synthetic Animate");
    effect = receipt.effect;
    return ok;
}

bool WolfSourceAIShell::QAAnimFinished(const std::string& name, const std::string& clip, WolfSourceAnimateTask& task) {
    auto* actor = impl->Find(name);
    if (!actor) return false;
    ChainResolver resolver(*actor, &impl->actors);
    return Wolf2009_EvaluateSourceAnimFinished(QAAnimateSpec(name, clip), resolver, task);
}

bool WolfSourceAIShell::QAKill(const std::string& name, const std::string& deathOverride, std::string& effect) {
    auto* actor = impl->Find(name);
    if (!actor || !actor->Alive()) { effect = "QA kill actor is absent or expired"; return false; }
    actor->state.script.deathOverride = deathOverride;
    return actor->Kill(false, effect);
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
int Wolf2009_LiveSourceAIExecutionTargets() { return liveExecutionTargets; }

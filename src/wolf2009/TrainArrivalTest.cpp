// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainArrival.h"
#include <functional>
#include <iostream>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void Refuses(const std::function<void()> &action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Unsupported arrival input accepted");
}
std::string Line(std::string text) {
    for (auto &c : text)
        if (c == '|')
            c = 0;
    return text;
}
std::string Edge(const std::string &target, const std::string &mode = "1", const std::string &selector = "0",
                 int altered = -1) {
    std::string text = "\t";
    for (int i = 0; i < 99; i++)
        text += Line((i == 0         ? "16"
                      : i == 1       ? mode
                      : i == 2       ? target
                      : i == 4       ? selector
                      : i == 98      ? "1.000000"
                      : i == altered ? "1"
                                     : "0") +
                     "|");
    return text + "\n";
}
std::vector<unsigned char> Fixture(bool bypass = false) {
    const std::string text =
        Line("h1023|32|32|32|0|\n1|17|0|0|0|^1|1|^74|1024|\n") + Edge("5") +
        Line("\t\t-1|\n132|5|10|20|0|^132|16|^131|1024|mMover|Me|mDestination|finish|mSpeed|25|mTime|0|"
             "mAccelTime|0|mDecelTime|0|mMaxSpeed|0|mMaxTime|0|mUseCustomSounds|false|\n") +
        Edge(bypass ? "40" : "31", "3", "2") + Line("\t\t-1|\n70|31|0|0|0|^70|0|mRemoveTarget|shake|\n") +
        Edge("40", "1", "3") + Line("68|40|0|0|0|^68|0|mUseTarget|front|\n") + Edge("41") +
        Line("68|41|0|0|0|^68|0|mUseTarget|middle|\n") + Edge("42") +
        Line("68|42|0|0|0|^68|0|mUseTarget|rear|\n") + Edge("77") +
        Line("68|77|0|0|0|^68|0|mUseTarget|hook|mUseKey|stop|");
    return {text.begin(), text.end()};
}
std::vector<unsigned char> Replace(const std::vector<unsigned char> &bytes, const std::string &before,
                                   const std::string &after) {
    std::string text(bytes.begin(), bytes.end());
    const auto at = text.find(before);
    Need(at != std::string::npos, "Synthetic replacement not found");
    text.replace(at, before.size(), after);
    return {text.begin(), text.end()};
}
EntityProperty Text(const char *key, const std::string &value) {
    EntityProperty p;
    p.key = key;
    p.text = value;
    return p;
}
EntityDocument Entities() {
    EntityDocument document;
    EntityRecord owner;
    owner.properties = {Text("name", "wagon"), Text("classname", "func_mover"),
                        Text("brain", "case/start.brain")};
    document.entities.push_back(owner);
    EntityRecord quake;
    quake.properties = {Text("name", "shake"), Text("classname", "func_earthquake")};
    document.entities.push_back(quake);
    for (const char *name : {"front", "middle", "rear"}) {
        EntityRecord entity;
        entity.properties = {Text("name", name), Text("classname", "func_static"),
                             Text("model", std::string("models/") + name)};
        EntityProperty hide;
        hide.key = "Hide";
        hide.kind = EntityProperty::Integer;
        hide.integer = 1;
        hide.sourceType = 4;
        entity.properties.push_back(hide);
        document.entities.push_back(entity);
    }
    EntityRecord hook;
    hook.properties = {Text("name", "hook"), Text("classname", "target_brainuse")};
    document.entities.push_back(hook);
    return document;
}
TrainArrivalMoveStamp Before() {
    TrainArrivalMoveStamp stamp;
    stamp.brainResource = "brains/case/start";
    stamp.ownerEntityName = "wagon";
    stamp.moveNodeId = 5;
    stamp.levelGeneration = 7;
    stamp.spawnDelivered = true;
    stamp.active = true;
    return stamp;
}
TrainArrivalMoveStamp After() {
    auto stamp = Before();
    stamp.active = false;
    stamp.arrived = true;
    return stamp;
}
bool Original(const LevelEntityRegistry &registry) {
    for (const char *name : {"shake", "front", "middle", "rear"}) {
        const auto *s = registry.Find(name);
        if (!s || !s->alive)
            return false;
        if (s->classname == "func_static" && !s->hidden)
            return false;
    }
    return true;
}
} // namespace
int RunTrainArrivalTests() {
    int passed = 0;
    try {
        const auto bytes = Fixture();
        const auto source = Entities();
        LevelEntityRegistry registry(source);
        const auto plan = DecodeTrainArrivalH1023(bytes, "brains/case/start", registry);
        Need(plan.hasRemoval && plan.removal.nodeId == 31 && plan.removal.name == "shake" &&
                 plan.shows.size() == 3 && plan.shows[0].nodeId == 40 && plan.unsupportedNodeId == 77 &&
                 plan.unsupportedNodeType == 68 && plan.unsupportedTargetClassname == "target_brainuse" &&
                 plan.unsupportedUseKey == "stop" && plan.unexecutedNodeCount == 1,
             "Arrival did not follow graph IDs or disclose boundary");
        ++passed;
        const auto keyedTail=QualifyTrainArrivalKeyedUseTail(plan);
        Need(keyedTail.sourceNodeId==77 && keyedTail.targetName=="hook" &&
             keyedTail.targetClassname=="target_brainuse" && keyedTail.useKey=="stop" &&
             keyedTail.sourceHeader==plan.unsupportedHeader,
             "Keyed-Use admission lost its full unchanged source header");
        ++passed;
        // The old informational projection accepts this boundary without
        // executing it. The new native consumer must qualify its metadata
        // before delivering the newly supported own-key relay action.
        for(unsigned slot=2;slot<=4;++slot)for(const char* invalid:{"garbage","1.5","2147483648","--1"}){
            std::vector<std::string> header={"68","77","0","0","0","^68","0","mUseTarget","hook","mUseKey","stop"};
            header[slot]=invalid;std::string changed;
            for(const auto& token:header)changed+=Line(token+"|");
            const auto malformed=Replace(bytes,Line("68|77|0|0|0|^68|0|mUseTarget|hook|mUseKey|stop|"),changed);
            const auto preserved=malformed;
            const auto projected=DecodeTrainArrivalH1023(malformed,"brains/case/start",registry);
            Need(projected.unsupportedTargetName=="hook"&&projected.unsupportedUseKey=="stop"&&
                 projected.unsupportedHeader==header&&malformed==preserved,
                 "Opaque arrival boundary or original bytes were rewritten");
            Refuses([&]{QualifyTrainArrivalKeyedUseTail(projected);});
            ++passed;
        }
        {
            const auto signedMetadata=Replace(bytes,Line("68|77|0|0|0|"),Line("68|77|-23|0007|0|"));
            const auto projected=DecodeTrainArrivalH1023(signedMetadata,"brains/case/start",registry);
            const auto qualified=QualifyTrainArrivalKeyedUseTail(projected);
            Need(qualified.sourceHeader[2]=="-23"&&qualified.sourceHeader[3]=="0007"&&
                 qualified.sourceHeader==projected.unsupportedHeader,
                 "Keyed-Use admission normalized source integer metadata");
            ++passed;
        }
        {
            auto forged=plan;forged.unsupportedHeader[1]="78";
            Refuses([&]{QualifyTrainArrivalKeyedUseTail(forged);});
            forged=plan;forged.unsupportedUseKey="other";
            Refuses([&]{QualifyTrainArrivalKeyedUseTail(forged);});
            ++passed;
        }
        TrainArrivalRuntime runtime(plan, "wagon", 7);
        auto before = Before(), after = After();
        auto r = runtime.ObserveCommittedMove(before, before, registry);
        Need(r.status == TrainArrivalStatus::NoCompletion && r.changes.empty() && Original(registry) &&
                 !runtime.Applied(),
             "Idle frame synthesized completion");
        ++passed;
        auto badBefore = before;
        badBefore.spawnDelivered = false;
        r = runtime.ObserveCommittedMove(badBefore, after, registry);
        Need(r.status == TrainArrivalStatus::UnconfirmedSpawn && Original(registry),
             "Undelivered spawn activated arrival");
        ++passed;
        for (unsigned field = 0; field < 4; field++) {
            auto wrong = after;
            if (field == 0)
                wrong.levelGeneration = 8;
            if (field == 1)
                wrong.ownerEntityName = "another";
            if (field == 2)
                wrong.brainResource = "brains/other/start";
            if (field == 3)
                wrong.moveNodeId = 99;
            r = runtime.ObserveCommittedMove(before, wrong, registry);
            Need(r.status == TrainArrivalStatus::WrongSource && r.changes.empty() && Original(registry),
                 "Foreign completion source changed registry");
        }
        ++passed;
        auto atSpawn = after;
        r = runtime.ObserveCommittedMove(atSpawn, atSpawn, registry);
        Need(r.status == TrainArrivalStatus::InvalidTransition && Original(registry),
             "Zero-length arrival-at-spawn synthesized event");
        ++passed;
        auto inconsistent = after;
        inconsistent.active = true;
        r = runtime.ObserveCommittedMove(before, inconsistent, registry);
        Need(r.status == TrainArrivalStatus::InvalidTransition && Original(registry),
             "Active-and-arrived state applied event");
        ++passed;
        r = runtime.ObserveCommittedMove(before, after, registry);
        Need(r.status == TrainArrivalStatus::Applied && r.changes.size() == 4 &&
                 r.changes[0].kind == TrainArrivalChange::RemoveEntity && r.changes[0].sourceNodeId == 31 &&
                 !registry.Find("shake")->alive && runtime.Applied(),
             "Removal event/state missing");
        for (std::size_t i = 1; i < 4; i++)
            Need(r.changes[i].kind == TrainArrivalChange::ShowEntity && r.changes[i].wasAlive &&
                     r.changes[i].wasHidden && r.changes[i].alive && !r.changes[i].hidden,
                 "Show delta does not describe live transition");
        Need(!registry.Find("front")->hidden && !registry.Find("middle")->hidden &&
                 !registry.Find("rear")->hidden && registry.Find("hook")->alive,
             "Show events missing or unsupported boundary executed");
        ++passed;
        r = runtime.ObserveCommittedMove(before, after, registry);
        Need(r.status == TrainArrivalStatus::AlreadyApplied && r.changes.empty(),
             "Completion prefix replayed");
        ++passed;
        LevelEntityRegistry resetRegistry(source);
        TrainArrivalRuntime reset(plan, "wagon", 8);
        r = reset.ObserveCommittedMove(before, after, resetRegistry);
        Need(r.status == TrainArrivalStatus::WrongSource && Original(resetRegistry),
             "Stale previous-generation event survived reset");
        before.levelGeneration = after.levelGeneration = 8;
        r = reset.ObserveCommittedMove(before, after, resetRegistry);
        Need(r.status == TrainArrivalStatus::Applied, "New generation could not apply independent arrival");
        before = Before();
        after = After();
        ++passed;
        auto changed = source;
        changed.entities[4].properties.back().integer = 0;
        LevelEntityRegistry conflict(changed);
        TrainArrivalRuntime conflictRuntime(plan, "wagon", 7);
        r = conflictRuntime.ObserveCommittedMove(before, after, conflict);
        Need(r.status == TrainArrivalStatus::RegistryConflict && conflict.Find("shake")->alive &&
                 conflict.Find("front")->hidden && !conflictRuntime.Applied() && r.changes.empty(),
             "Late registry conflict partially removed or showed entities");
        ++passed;
        changed = source;
        changed.entities.erase(changed.entities.begin() + 4);
        LevelEntityRegistry missing(changed);
        r = conflictRuntime.ObserveCommittedMove(before, after, missing);
        Need(r.status == TrainArrivalStatus::RegistryConflict && missing.Find("shake")->alive &&
                 missing.Find("front")->hidden,
             "Missing target caused partial commit");
        ++passed;
        changed = source;
        changed.entities[0].properties.back().text = "another/brain.brain";
        LevelEntityRegistry wrongBinding(changed);
        r = conflictRuntime.ObserveCommittedMove(before, after, wrongBinding);
        Need(r.status == TrainArrivalStatus::RegistryConflict && Original(wrongBinding),
             "Owner ENTS brain binding was ignored");
        ++passed;
        LevelEntityRegistry bypass(source);
        const auto bypassPlan = DecodeTrainArrivalH1023(Fixture(true), "brains/case/start", bypass);
        Need(!bypassPlan.hasRemoval && bypassPlan.unexecutedNodeCount == 2,
             "Bypassed orphan removal entered execution plan");
        TrainArrivalRuntime bypassRuntime(bypassPlan, "wagon", 7);
        r = bypassRuntime.ObserveCommittedMove(before, after, bypass);
        Need(r.status == TrainArrivalStatus::Applied && r.changes.size() == 3 &&
                 bypass.Find("shake")->alive && !bypass.Find("front")->hidden &&
                 !bypass.Find("middle")->hidden && !bypass.Find("rear")->hidden,
             "Causal bypass did not preserve removal target while showing cars");
        ++passed;
        LevelEntityRegistry clean(source);
        Refuses([&] {
            DecodeTrainArrivalH1023(
                Replace(bytes, Edge("31", "3", "2"), Edge("31", "3", "2") + Edge("40", "3", "2")),
                "brains/case/start", clean);
        });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(Replace(bytes, Edge("31", "3", "2"), Edge("31", "3", "2", 50)),
                                    "brains/case/start", clean);
        });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(Replace(bytes, Line("70|31|"), Line("30|31|")), "brains/case/start",
                                    clean);
        });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(Replace(bytes, Edge("42"), Edge("999")), "brains/case/start", clean);
        });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(Replace(bytes, Edge("77"), Edge("40")), "brains/case/start", clean);
        });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(Replace(bytes, Line("mUseTarget|middle|"), Line("mUseTarget|front|")),
                                    "brains/case/start", clean);
        });
        ++passed;
        changed = source;
        changed.entities[2].properties.back().integer = 0;
        LevelEntityRegistry visible(changed);
        Refuses([&] { DecodeTrainArrivalH1023(bytes, "brains/case/start", visible); });
        ++passed;
        changed = source;
        changed.entities[2].properties.pop_back();
        LevelEntityRegistry noWitness(changed);
        Refuses([&] { DecodeTrainArrivalH1023(bytes, "brains/case/start", noWitness); });
        ++passed;
        changed = source;
        changed.entities[2].properties.back().kind = EntityProperty::Text;
        changed.entities[2].properties.back().text = "1";
        Refuses([&] { LevelEntityRegistry unknownType(changed); });
        ++passed;
        changed = source;
        changed.entities[2].properties[1].text = "light";
        LevelEntityRegistry wrongClass(changed);
        Refuses([&] { DecodeTrainArrivalH1023(bytes, "brains/case/start", wrongClass); });
        ++passed;
        changed = source;
        changed.entities[2].properties.push_back(Text("brain", "unknown/behavior.brain"));
        LevelEntityRegistry unknownBehavior(changed);
        Refuses([&] { DecodeTrainArrivalH1023(bytes, "brains/case/start", unknownBehavior); });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(
                Replace(bytes, Line("mRemoveTarget|shake|"), Line("mRemoveTarget|absent|")),
                "brains/case/start", clean);
        });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(
                Replace(bytes, Line("mUseTarget|front|"), Line("mUseTarget|front|mUseKey|unsupported|")),
                "brains/case/start", clean);
        });
        ++passed;
        Refuses([&] {
            DecodeTrainArrivalH1023(Replace(bytes, Line("68|41|"), Line("68|40|")), "brains/case/start",
                                    clean);
        });
        ++passed;
        changed = source;
        changed.entities.push_back(changed.entities[2]);
        changed.entities.back().properties[0].text = "FRONT";
        Refuses([&] { LevelEntityRegistry ambiguous(changed); });
        ++passed;
        changed = source;
        changed.entities[2].properties.push_back(changed.entities[2].properties.back());
        Refuses([&] { LevelEntityRegistry duplicateHide(changed); });
        ++passed;
        TrainArrivalLimits limits;
        limits.registryEntities = 1;
        Refuses([&] { LevelEntityRegistry over(source, limits); });
        limits = TrainArrivalLimits();
        limits.registryStringBytes = 2;
        Refuses([&] { LevelEntityRegistry over(source, limits); });
        limits = TrainArrivalLimits();
        limits.brain.sourceBytes = 1;
        Refuses([&] { DecodeTrainArrivalH1023(bytes, "brains/case/start", clean, limits); });
        ++passed;
        Refuses([&] { DecodeTrainArrivalH1023(bytes, "brains/../start", clean); });
        ++passed;
        auto invalidPlan = plan;
        invalidPlan.shows.pop_back();
        Refuses([&] { TrainArrivalRuntime invalid(invalidPlan, "wagon", 7); });
        Refuses([&] { TrainArrivalRuntime invalid(plan, "wagon", 0); });
        ++passed;
        Need(Original(clean), "Rejected parsing changed source registry");
        ++passed;
        std::cout << "Train arrival self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Train arrival test failed after " << passed << ": " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009
#ifdef WOLF2009_TRAIN_ARRIVAL_TEST_MAIN
#include "Archive.h"
#include "WagonScene.h"
#include <fstream>
#include <iterator>
namespace {
std::vector<unsigned char> Bytes(const char *path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read private fixture");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
wolf2009::TrainArrivalMoveStamp Stamp(const wolf2009::WagonScene &scene,
                                      const wolf2009::TrainArrivalPlan &plan) {
    wolf2009::TrainArrivalMoveStamp stamp;
    const auto &state = scene.State();
    stamp.brainResource = plan.brainResource;
    stamp.ownerEntityName = scene.Description().entityName;
    stamp.moveNodeId = plan.moveNodeId;
    stamp.levelGeneration = 1;
    stamp.spawnDelivered = state.spawnDelivered;
    stamp.active = state.moving;
    stamp.arrived = state.arrived;
    return stamp;
}
int Real(const char *control, const char *bypass, const char *mpk) {
    std::ifstream in(mpk, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open private MPK");
    const auto archive = wolf2009::Archive::Scan(in);
    std::string resource;
    std::vector<unsigned char> entities;
    unsigned count = 0;
    for (std::size_t c = 0; c < archive.Chunks().size(); c++)
        for (std::size_t e = 0; e < archive.Chunks()[c].entries.size(); e++) {
            const auto &chunk = archive.Chunks()[c];
            if (chunk.type == "ENTS") {
                entities = archive.ReadEntry(in, c, e);
                resource = chunk.entries[e].name;
                ++count;
            }
        }
    if (count != 1)
        throw std::runtime_error("Private MPK must contain one ENTS");
    const auto document = wolf2009::DecodeEntities4(entities);
    wolf2009::LevelEntityRegistry a(document), b(document);
    auto scene = wolf2009::WagonScene::Load(in, archive, resource);
    const auto planA =
                   wolf2009::DecodeTrainArrivalH1023(Bytes(control), scene->Description().brainResource, a),
               planB =
                   wolf2009::DecodeTrainArrivalH1023(Bytes(bypass), scene->Description().brainResource, b);
    if (!planA.hasRemoval || planB.hasRemoval)
        throw std::runtime_error("Private causal fixtures have wrong profiles");
    wolf2009::TrainArrivalRuntime runA(planA, scene->Description().entityName, 1),
        runB(planB, scene->Description().entityName, 1);
    unsigned eventsA = 0, eventsB = 0;
    for (unsigned i = 0; i < 900; i++) {
        const auto before = Stamp(*scene, planA);
        scene->Advance(1.0 / 60);
        const auto after = Stamp(*scene, planA);
        const auto ra = runA.ObserveCommittedMove(before, after, a),
                   rb = runB.ObserveCommittedMove(before, after, b);
        eventsA += unsigned(ra.changes.size());
        eventsB += unsigned(rb.changes.size());
    }
    if (!scene->State().arrived || !runA.Applied() || !runB.Applied() || eventsA != 4 || eventsB != 3 ||
        a.Find(planA.removal.name)->alive || !b.Find(planA.removal.name)->alive)
        throw std::runtime_error("Retail arrival did not reproduce causal A/B");
    for (const auto &target : planA.shows)
        if (a.Find(target.name)->hidden || b.Find(target.name)->hidden)
            throw std::runtime_error("Retail static visibility differs from PC");
    std::cout << "Retail arrival PASS entities=" << a.Size() << " owner=" << scene->Description().entityName
              << " move=" << planA.moveNodeId << " removal=" << planA.removal.name
              << " controlEvents=" << eventsA << " bypassEvents=" << eventsB
              << " boundary=" << planA.unsupportedNodeId << "/" << planA.unsupportedNodeType
              << " cartY=" << scene->State().cartOrigin.y << " unsupportedNotExecuted=1\n";
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        return argc == 4 ? Real(argv[1], argv[2], argv[3]) : wolf2009::RunTrainArrivalTests();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif

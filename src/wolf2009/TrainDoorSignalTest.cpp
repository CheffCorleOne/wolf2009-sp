// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainDoorSignal.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace wolf2009 {
namespace {

void Need(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Refuses(const std::function<void()> &action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Unsupported door-signal input accepted");
}

std::string Line(std::string value) {
    for (char &c : value)
        if (c == '|')
            c = 0;
    return value;
}

std::string Edge(const char *target, bool sentinel = true) {
    std::string result = "\t";
    for (unsigned i = 0; i < 99; ++i)
        result += Line(std::string(i == 0 ? "16" : i == 1 ? "1" : i == 2 ? target :
                                  i == 98 ? "1.000000" : "0") + "|");
    result += '\n';
    if (sentinel)
        result += Line("\t\t-1|\n");
    return result;
}

std::vector<unsigned char> Fixture() {
    const auto text = Line("h1023|32|32|32|0|\n3|91|2|4|7|^3|0|^64|1024|mUseKey|arrived|\n") +
                      Edge("17") +
                      Line("19|17|12|4|9|^19|16|^18|1024|mDefName|voice_set|\n") + Edge("42") +
                      Line("59|42|24|4|13|^59|16|mAnim|open_clip|\n") + Edge("66", false) +
                      Line("243|66|0|0|0|^243|0|opaque|later|\n");
    return {text.begin(), text.end()};
}

std::vector<unsigned char> Replace(const std::vector<unsigned char> &bytes,
                                   const std::string &before, const std::string &after) {
    std::string value(bytes.begin(), bytes.end());
    const auto at = value.find(Line(before));
    Need(at != std::string::npos, "Door-signal test replacement not found");
    value.replace(at, before.size(), Line(after));
    return {value.begin(), value.end()};
}

EntityProperty Text(const char *key, const char *value) {
    EntityProperty property;
    property.key = key;
    property.text = value;
    return property;
}

EntityDocument Entities(bool target = true) {
    EntityDocument result;
    EntityRecord signal, actor, door;
    signal.properties = {Text("name", "hook"), Text("classname", "target_brainuse"),
                         Text("target", "actor"), Text("useKey", "arrived")};
    actor.properties = {Text("name", "actor"), Text("classname", "actor_class"),
                        Text("brain", "case/greeter.brain"), Text("anim", "open_clip"),
                        Text("triggerStartMoveEnt", target ? "panel" : "")};
    door.properties = {Text("name", "panel"), Text("classname", "animated_panel")};
    result.entities = {signal, actor, door};
    return result;
}

TrainDoorSignalPlan Plan(bool target = true) {
    return DecodeTrainDoorSignalH1023(Fixture(), "brains/case/greeter", "hook", Entities(target));
}

TrainDoorUseEvent Event(std::uint64_t generation = 7) {
    TrainDoorUseEvent event;
    event.sourceEntityName = "hook";
    event.ownerEntityName = "actor";
    event.useKey = "arrived";
    event.levelGeneration = generation;
    event.eventId = 39;
    return event;
}

} // namespace

int RunTrainDoorSignalTests() {
    unsigned tests = 0;
    const auto test = [&tests](const std::function<void()> &action) {
        action();
        ++tests;
    };
    test([] {
        const auto plan = Plan();
        Need(plan.onUsedNodeId == 91 && plan.voiceNodeId == 17 && plan.actorStartNodeId == 42 &&
                 plan.actorAnimation == "open_clip" && plan.targetEntityName == "panel" &&
                 plan.voiceDefinition == "voice_set" && plan.unsupportedOutputs.size() == 1 &&
                 plan.unsupportedOutputs[0].destinationNodeType == 243 &&
                 plan.unexecutedNodeCount == 1 && plan.voiceRawFlags == 16,
             "Door-signal graph/target extraction");
    });
    test([] {
        TrainDoorSignalRuntime runtime(Plan(), 7);
        Need(runtime.Status() == TrainDoorSignalStatus::AwaitingUse, "Constructor fabricated use");
        const auto voice = runtime.OnUsed(Event());
        Need(voice.status == TrainDoorSignalStatus::VoicePreparationRequested &&
                 voice.command.kind == TrainDoorCommand::PrepareVoiceResources &&
                 voice.command.resourceName == "voice_set", "Missing explicit preparation request");
        const auto actor = runtime.VoiceResourcesReady(voice.command, true);
        Need(actor.command.kind == TrainDoorCommand::StartActorAnimation &&
                 actor.command.resourceName == "open_clip", "Missing actor start request");
        const auto activate = runtime.ActorStartCommitted(actor.command, true);
        Need(activate.status == TrainDoorSignalStatus::TargetActivationRequested &&
                 activate.command.kind == TrainDoorCommand::ActivateTarget &&
                 activate.command.targetEntityName == "panel" && activate.command.sourceNodeId == 42,
             "Missing target activation from committed actor start");
        Need(runtime.ActorStartCommitted(actor.command, true).command.kind == TrainDoorCommand::None,
             "Replayed actor acknowledgement repeated activation");
    });
    test([] {
        TrainDoorSignalRuntime runtime(Plan(false), 7);
        const auto voice = runtime.OnUsed(Event());
        const auto actor = runtime.VoiceResourcesReady(voice.command, true);
        const auto result = runtime.ActorStartCommitted(actor.command, true);
        Need(result.status == TrainDoorSignalStatus::CompleteWithoutTarget &&
                 result.command.kind == TrainDoorCommand::None, "Empty target invented activation");
    });
    test([] {
        TrainDoorSignalRuntime runtime(Plan(), 7);
        const auto voice = runtime.OnUsed(Event());
        Need(runtime.VoiceResourcesReady(voice.command, false).status ==
                 TrainDoorSignalStatus::ResourceNotReady &&
                 runtime.Status() == TrainDoorSignalStatus::VoicePreparationRequested,
             "Missing resources fabricated success");
        const auto actor = runtime.VoiceResourcesReady(voice.command, true);
        Need(runtime.ActorStartCommitted(actor.command, false).status ==
                 TrainDoorSignalStatus::ActorStartNotCommitted &&
                 runtime.Status() == TrainDoorSignalStatus::ActorStartRequested,
             "Uncommitted actor start activated target");
        Need(runtime.ActorStartCommitted(actor.command, true).command.kind ==
                 TrainDoorCommand::ActivateTarget, "Valid retry was not accepted");
    });
    for (unsigned altered = 0; altered < 5; ++altered) {
        test([altered] {
            TrainDoorSignalRuntime runtime(Plan(), 7);
            auto event = Event();
            if (altered == 0) event.levelGeneration = 8;
            if (altered == 1) event.eventId = 0;
            if (altered == 2) event.sourceEntityName = "other_hook";
            if (altered == 3) event.ownerEntityName = "other_actor";
            if (altered == 4) event.useKey = "Arrived";
            Need(runtime.OnUsed(event).status == TrainDoorSignalStatus::WrongEvent &&
                     runtime.Status() == TrainDoorSignalStatus::AwaitingUse,
                 "Wrong event mutated state");
        });
    }
    for (unsigned altered = 0; altered < 7; ++altered) {
        test([altered] {
            TrainDoorSignalRuntime runtime(Plan(), 7);
            const auto voice = runtime.OnUsed(Event());
            auto request = voice.command;
            if (altered == 0) request.levelGeneration = 8;
            if (altered == 1) request.eventId = 40;
            if (altered == 2) request.sourceNodeId = 18;
            if (altered == 3) request.ownerEntityName = "other";
            if (altered == 4) request.resourceName = "wrong_resource";
            if (altered == 5) request.targetEntityName = "injected";
            if (altered == 6) request.kind = TrainDoorCommand::StartActorAnimation;
            Need(runtime.VoiceResourcesReady(request, true).status == TrainDoorSignalStatus::WrongCallback &&
                     runtime.Status() == TrainDoorSignalStatus::VoicePreparationRequested,
                 "Wrong callback mutated state");
            const auto actor = runtime.VoiceResourcesReady(voice.command, true);
            Need(runtime.VoiceResourcesReady(voice.command, true).status ==
                     TrainDoorSignalStatus::WrongCallback, "Voice callback replay accepted");
            auto wrong = actor.command;
            wrong.eventId++;
            Need(runtime.ActorStartCommitted(wrong, true).command.kind == TrainDoorCommand::None &&
                     runtime.Status() == TrainDoorSignalStatus::ActorStartRequested,
                 "Wrong actor callback mutated state");
        });
    }
    test([] {
        TrainDoorSignalRuntime runtime(Plan(), 7);
        runtime.OnUsed(Event());
        Need(runtime.OnUsed(Event()).status == TrainDoorSignalStatus::AlreadyAccepted,
             "Use event replay accepted");
        auto event = Event();
        event.eventId++;
        Need(runtime.OnUsed(event).status == TrainDoorSignalStatus::AlreadyAccepted,
             "Observed single-use profile accepted a second use");
        TrainDoorSignalRuntime reset(Plan(), 8);
        Need(reset.OnUsed(Event()).command.kind == TrainDoorCommand::None,
             "Old level event accepted by reset runtime");
    });
    const auto bytes = Fixture();
    for (const auto &replacement : std::vector<std::pair<std::string, std::string>>{
             {"h1023", "h1024"}, {"mUseKey|arrived", "mUseKey|other"},
             {"^3|0|", "^3|1|"}, {"^19|16|", "^19|0|"},
             {"^59|16|", "^59|0|"}, {"mDefName|voice_set", "mOwner|voice_set"},
             {"mAnim|open_clip", "mAnim|other_clip"}, {"19|17|", "19|91|"},
             {"16|1|17|0|0|", "16|1|999|0|0|"},
             {"16|1|17|0|0|", "16|3|17|0|0|"},
             {"16|1|17|0|0|", "16|1|17|0|1|"},
             {"\t\t-1|\n", "\t\t-2|\n"}, {"243|66|", "243|67|"}}) {
        test([&bytes, replacement] {
            Refuses([&] { DecodeTrainDoorSignalH1023(
                Replace(bytes, replacement.first, replacement.second), "brains/case/greeter", "hook",
                Entities()); });
        });
    }
    test([&bytes] {
        auto duplicate = bytes;
        const auto line = Line("3|92|0|0|0|^3|0|^64|1024|mUseKey|arrived|\n");
        duplicate.insert(duplicate.end(), line.begin(), line.end());
        Refuses([&] { DecodeTrainDoorSignalH1023(duplicate, "brains/case/greeter", "hook", Entities()); });
    });
    test([&bytes] {
        const auto line = Edge("17");
        const auto data = Replace(bytes, line, line + line);
        Refuses([&] { DecodeTrainDoorSignalH1023(data, "brains/case/greeter", "hook", Entities()); });
    });
    for (unsigned limit = 0; limit < 7; ++limit) {
        test([limit, &bytes] {
            TrainDoorSignalLimits limits;
            if (limit == 0) limits.brain.sourceBytes = bytes.size() - 1;
            if (limit == 1) limits.brain.records = 2;
            if (limit == 2) limits.brain.tokens = 5;
            if (limit == 3) limits.brain.tokenBytes = 3;
            if (limit == 4) limits.brain.nesting = 1;
            if (limit == 5) limits.entities = 2;
            if (limit == 6) limits.entityNameBytes = 3;
            Refuses([&] { DecodeTrainDoorSignalH1023(bytes, "brains/case/greeter", "hook", Entities(),
                                                   limits); });
        });
    }
    for (unsigned altered = 0; altered < 8; ++altered) {
        test([altered, &bytes] {
            auto entities = Entities();
            if (altered == 0) entities.entities[0].properties.push_back(Text("target1", "other"));
            if (altered == 1) entities.entities[0].properties[1].text = "unqualified_source";
            if (altered == 2) entities.entities[1].properties[2].text = "other/brain.brain";
            if (altered == 3) entities.entities[1].properties[4].text = "missing";
            if (altered == 4) entities.entities[1].properties[4].text = "actor";
            if (altered == 5) entities.entities[0].properties.push_back(Text("TARGET", "actor"));
            if (altered == 6) entities.entities[2].properties[0].text = "ACTOR";
            if (altered == 7) entities.entities[0].properties[3].kind = EntityProperty::Integer;
            Refuses([&] { DecodeTrainDoorSignalH1023(bytes, "brains/case/greeter", "hook", entities); });
        });
    }
    test([] { Refuses([] { TrainDoorSignalRuntime runtime(Plan(), 0); }); });
    test([] {
        auto plan = Plan();
        plan.actorRawFlags = 0;
        Refuses([&] { TrainDoorSignalRuntime runtime(plan, 7); });
    });
    test([&bytes] {
        auto data = Replace(bytes, "opaque|later", std::string("opaque|later") + char(0x92));
        Need(DecodeTrainDoorSignalH1023(data, "brains/case/greeter", "hook", Entities()).actorStartNodeId
                 == 42, "Bounded opaque CP1252 debug token rejected");
    });
    test([&bytes] {
        const auto data = Replace(bytes, "mDefName|voice_set", std::string("mDefName|voice") + char(0x92));
        Refuses([&] { DecodeTrainDoorSignalH1023(data, "brains/case/greeter", "hook", Entities()); });
    });
    test([&bytes] {
        auto truncated = bytes;
        truncated.pop_back();
        truncated.pop_back();
        Refuses([&] { DecodeTrainDoorSignalH1023(truncated, "brains/case/greeter", "hook", Entities()); });
    });
    std::cout << "Train-door signal tests passed: " << tests
              << " (explicit use/resource readiness/committed start; later outputs unsupported)\n";
    return 0;
}

} // namespace wolf2009

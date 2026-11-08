// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainDoorSignal.h"
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {

void Need(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string Fold(std::string value) {
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
        if (c == '\\')
            c = '/';
    }
    return value;
}

void Name(const std::string &value) {
    Need(!value.empty() && value.size() <= 1024, "Invalid door-signal name length");
    for (unsigned char c : value)
        Need(c >= 32 && c < 127, "Invalid door-signal name byte");
}

std::string Brain(std::string value, bool binding) {
    Name(value);
    value = Fold(value);
    if (binding && value.size() >= 6 && value.compare(value.size() - 6, 6, ".brain") == 0)
        value.resize(value.size() - 6);
    if (binding && value.compare(0, 7, "brains/") != 0)
        value = "brains/" + value;
    Need(value.compare(0, 7, "brains/") == 0 && value.size() > 7,
         "Expected named BRAI resource for door signal");
    std::size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find('/', start);
        const auto part = value.substr(start, end == std::string::npos ? end : end - start);
        Need(!part.empty() && part != "." && part != ".." && part.find(':') == std::string::npos,
             "Malformed door-signal brain resource");
        if (end == std::string::npos)
            break;
        start = end + 1;
        Need(start < value.size(), "Trailing slash in door-signal brain resource");
    }
    return value;
}

std::uint32_t Integer(const std::string &value, bool zero = false) {
    Need(!value.empty(), "Empty door-signal integer");
    std::uint32_t result = 0;
    for (unsigned char c : value) {
        Need(c >= '0' && c <= '9', "Malformed door-signal integer");
        const auto digit = unsigned(c - '0');
        Need(result <= (2147483647u - digit) / 10, "Door-signal integer limit");
        result = result * 10 + digit;
    }
    Need(zero || result != 0, "Zero door-signal node ID");
    return result;
}

void Metadata(const std::string &value) {
    const auto start = !value.empty() && value[0] == '-' ? 1u : 0u;
    Integer(value.substr(start), true);
}

const EntityProperty *Property(const EntityRecord &entity, const char *key) {
    const EntityProperty *found = NULL;
    for (const auto &property : entity.properties) {
        if (Fold(property.key) != key)
            continue;
        Need(!found, "Duplicate door-signal entity property");
        found = &property;
    }
    return found;
}

std::string Text(const EntityRecord &entity, const char *key, bool required = true) {
    const auto *property = Property(entity, key);
    Need(!required || property, "Missing door-signal entity property");
    if (!property)
        return "";
    Need(property->kind == EntityProperty::Text, "Door-signal property must be text");
    if (!property->text.empty())
        Name(property->text);
    Need(!required || !property->text.empty(), "Empty required door-signal property");
    return property->text;
}

struct Edge {
    std::vector<std::string> tokens;
    std::size_t sentinels = 0;
    bool unknown = false;
};

struct Node {
    std::uint32_t type = 0, id = 0;
    std::vector<std::string> tokens;
    std::vector<Edge> edges;
    bool unknownNested = false;
};

std::map<std::uint32_t, Node> Nodes(const std::vector<unsigned char> &data,
                                    const LinearMoverLimits &limits) {
    Need(!data.empty() && data.size() <= limits.sourceBytes, "Door-signal brain byte budget");
    std::map<std::uint32_t, Node> nodes;
    std::size_t cursor = 0, records = 0, tokenCount = 0;
    Node *owner = NULL;
    while (cursor < data.size()) {
        Need(records < limits.records, "Door-signal record budget");
        std::size_t depth = 0;
        while (cursor < data.size() && data[cursor] == '\t') {
            ++cursor;
            ++depth;
        }
        Need(depth <= limits.nesting, "Door-signal nesting budget");
        std::vector<std::string> tokens;
        while (cursor < data.size() && data[cursor] != '\n') {
            Need(tokenCount < limits.tokens, "Door-signal token budget");
            ++tokenCount;
            std::string token;
            while (cursor < data.size() && data[cursor] != 0) {
                const auto c = data[cursor++];
                // Unexecuted retail dialogue/debug tokens contain CP1252 bytes.
                // Preserve bounded opaque bytes; executable names/IDs below still
                // require their own strict ASCII/profile validation.
                Need(c >= 32 && c != 127 && token.size() < limits.tokenBytes,
                     "Invalid or excessive door-signal token");
                token += char(c);
            }
            Need(cursor < data.size() && !token.empty(), "Unterminated/empty door-signal token");
            ++cursor;
            tokens.push_back(std::move(token));
        }
        if (cursor < data.size())
            ++cursor;
        Need(!tokens.empty(), "Empty door-signal record");
        if (records == 0) {
            Need(depth == 0 && tokens == std::vector<std::string>({"h1023", "32", "32", "32", "0"}),
                 "Unsupported door-signal brain header");
        } else if (depth == 0) {
            Need(tokens.size() >= 2, "Truncated door-signal node");
            Node node;
            node.type = Integer(tokens[0]);
            node.id = Integer(tokens[1]);
            node.tokens = std::move(tokens);
            const auto inserted = nodes.insert(std::make_pair(node.id, std::move(node)));
            Need(inserted.second, "Duplicate door-signal node ID");
            owner = &inserted.first->second;
        } else {
            Need(owner, "Door-signal nested record lacks node");
            if (depth == 1) {
                Edge edge;
                edge.tokens = std::move(tokens);
                owner->edges.push_back(std::move(edge));
            } else if (owner->edges.empty()) {
                owner->unknownNested = true;
            } else if (depth == 2 && tokens == std::vector<std::string>({"-1"})) {
                ++owner->edges.back().sentinels;
            } else {
                owner->edges.back().unknown = true;
            }
        }
        ++records;
    }
    return nodes;
}

const Node &At(const std::map<std::uint32_t, Node> &nodes, std::uint32_t id) {
    const auto found = nodes.find(id);
    Need(found != nodes.end(), "Door-signal edge references missing node");
    return found->second;
}

std::uint32_t Next(const Node &node) {
    Need(!node.unknownNested && node.edges.size() == 1, "Branched/missing door-signal edge");
    const auto &edge = node.edges[0];
    const auto &tokens = edge.tokens;
    Need(!edge.unknown && edge.sentinels == 1 && tokens.size() == 99 && tokens[0] == "16" &&
             tokens[1] == "1" && tokens[3] == "0" && tokens[4] == "0" && tokens[98] == "1.000000",
         "Unsupported door-signal edge profile");
    for (std::size_t i = 5; i < 98; ++i)
        Need(tokens[i] == "0", "Nondefault door-signal edge field");
    return Integer(tokens[2]);
}

void Header(const Node &node, std::uint32_t type, std::size_t size) {
    Need(node.type == type && node.tokens.size() == size && node.tokens[5] == "^" + std::to_string(type),
         "Unsupported door-signal node profile");
    for (unsigned i = 2; i <= 4; ++i)
        Metadata(node.tokens[i]);
}

bool Same(const TrainDoorCommand &a, const TrainDoorCommand &b) {
    return a.kind == b.kind && a.ownerEntityName == b.ownerEntityName &&
           a.resourceName == b.resourceName && a.targetEntityName == b.targetEntityName &&
           a.levelGeneration == b.levelGeneration && a.eventId == b.eventId &&
           a.sourceNodeId == b.sourceNodeId;
}

TrainDoorSignalResult Result(TrainDoorSignalStatus status,
                             const TrainDoorCommand &command = TrainDoorCommand()) {
    TrainDoorSignalResult result;
    result.status = status;
    result.command = command;
    return result;
}

TrainDoorCommand Command(const TrainDoorSignalPlan &plan, std::uint64_t generation, std::uint64_t event,
                         TrainDoorCommand::Kind kind) {
    TrainDoorCommand command;
    command.kind = kind;
    command.ownerEntityName = plan.ownerEntityName;
    command.levelGeneration = generation;
    command.eventId = event;
    if (kind == TrainDoorCommand::PrepareVoiceResources) {
        command.resourceName = plan.voiceDefinition;
        command.sourceNodeId = plan.voiceNodeId;
    } else if (kind == TrainDoorCommand::StartActorAnimation) {
        command.resourceName = plan.actorAnimation;
        command.sourceNodeId = plan.actorStartNodeId;
    } else if (kind == TrainDoorCommand::ActivateTarget) {
        command.targetEntityName = plan.targetEntityName;
        command.sourceNodeId = plan.actorStartNodeId;
    }
    return command;
}

} // namespace

TrainDoorSignalPlan DecodeTrainDoorSignalH1023(const std::vector<unsigned char> &data,
                                               const std::string &brainResource,
                                               const std::string &signalEntityName,
                                               const EntityDocument &entities,
                                               const TrainDoorSignalLimits &limits) {
    Need(entities.entities.size() <= limits.entities, "Door-signal entity budget");
    Name(signalEntityName);
    std::map<std::string, const EntityRecord *> registry;
    std::set<std::string> folded;
    std::size_t nameBytes = 0;
    for (const auto &entity : entities.entities) {
        const auto name = Text(entity, "name", false);
        if (name.empty())
            continue;
        Need(nameBytes <= limits.entityNameBytes && name.size() <= limits.entityNameBytes - nameBytes,
             "Door-signal aggregate entity name budget");
        nameBytes += name.size();
        Need(folded.insert(Fold(name)).second, "Ambiguous door-signal entity name");
        registry.insert(std::make_pair(name, &entity));
    }
    const auto find = [&registry](const std::string &name) -> const EntityRecord & {
        const auto entry = registry.find(name);
        Need(entry != registry.end(), "Missing door-signal target entity");
        return *entry->second;
    };
    const auto &signal = find(signalEntityName);
    Need(Text(signal, "classname") == "target_brainuse", "Expected target_brainuse signal source");
    const std::set<std::string> sourceProperties{"name",  "classname", "origin", "rotation",
                                                 "angle", "target",    "usekey"};
    for (const auto &property : signal.properties)
        Need(sourceProperties.count(Fold(property.key)) != 0,
             "Unsupported target_brainuse signal property");
    TrainDoorSignalPlan plan;
    plan.brainResource = Brain(brainResource, false);
    plan.signalEntityName = signalEntityName;
    plan.ownerEntityName = Text(signal, "target");
    plan.useKey = Text(signal, "usekey");
    const auto &owner = find(plan.ownerEntityName);
    Need(Brain(Text(owner, "brain"), true) == plan.brainResource,
         "Door-signal owner brain does not match resource");
    plan.ownerClassname = Text(owner, "classname");
    plan.targetEntityName = Text(owner, "triggerstartmoveent", false);
    if (!plan.targetEntityName.empty()) {
        Need(plan.targetEntityName != plan.ownerEntityName && plan.targetEntityName != signalEntityName,
             "Self-referential door-signal activation");
        plan.targetClassname = Text(find(plan.targetEntityName), "classname");
    }
    const auto nodes = Nodes(data, limits.brain);
    const Node *onUsed = NULL;
    for (const auto &entry : nodes) {
        const auto &node = entry.second;
        if (node.type != 3)
            continue;
        const auto &tokens = node.tokens;
        bool matches = false;
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
            if (tokens[i] == "mUseKey" && tokens[i + 1] == plan.useKey)
                matches = true;
        if (!matches)
            continue;
        Need(!onUsed, "Ambiguous matching door-signal OnUsed nodes");
        Header(node, 3, 11);
        Need(tokens[6] == "0" && tokens[7] == "^64" && tokens[8] == "1024" && tokens[9] == "mUseKey" &&
                 tokens[10] == plan.useKey,
             "Unsupported matching OnUsed configuration");
        onUsed = &node;
    }
    Need(onUsed, "No exact matching door-signal OnUsed node");
    const auto &voice = At(nodes, Next(*onUsed));
    Header(voice, 19, 11);
    Need(voice.tokens[6] == "16" && voice.tokens[7] == "^18" && voice.tokens[8] == "1024" &&
             voice.tokens[9] == "mDefName",
         "Unsupported voice preparation configuration");
    plan.voiceDefinition = voice.tokens[10];
    Name(plan.voiceDefinition);
    const auto &actor = At(nodes, Next(voice));
    Header(actor, 59, 9);
    Need(actor.tokens[6] == "16" && actor.tokens[7] == "mAnim" && !actor.unknownNested,
         "Unsupported actor-animation start configuration");
    plan.actorAnimation = actor.tokens[8];
    Name(plan.actorAnimation);
    Need(Text(owner, "anim") == plan.actorAnimation,
         "Owner initial animation differs from observed start profile");
    plan.onUsedNodeId = onUsed->id;
    plan.voiceNodeId = voice.id;
    plan.actorStartNodeId = actor.id;
    plan.onUsedRawFlags = Integer(onUsed->tokens[6], true);
    plan.voiceRawFlags = Integer(voice.tokens[6], true);
    plan.actorRawFlags = Integer(actor.tokens[6], true);
    for (const auto &edge : actor.edges) {
        Need(edge.tokens.size() >= 5, "Truncated unsupported actor output");
        TrainDoorUnsupportedOutput output;
        output.outputToken = Integer(edge.tokens[0], true);
        output.modeToken = Integer(edge.tokens[1], true);
        output.destinationNodeId = Integer(edge.tokens[2]);
        Need(edge.tokens[3] == "0", "Unsupported actor output destination field");
        output.selectorToken = Integer(edge.tokens[4], true);
        const auto &destination = At(nodes, output.destinationNodeId);
        output.destinationNodeType = destination.type;
        if (destination.type == 12 && output.outputToken == 32 && output.selectorToken == 1) {
            for (std::size_t i = 0; i + 1 < destination.tokens.size(); ++i) {
                if (destination.tokens[i] != "mLine")
                    continue;
                plan.referencedSpeechLines.push_back(destination.tokens[i + 1]);
            }
        }
        plan.unsupportedOutputs.push_back(output);
    }
    plan.unexecutedNodeCount = nodes.size() - 3;
    return plan;
}

TrainDoorSignalRuntime::TrainDoorSignalRuntime(const TrainDoorSignalPlan &plan,
                                               std::uint64_t levelGeneration)
    : plan_(plan), generation_(levelGeneration) {
    Need(generation_ != 0, "Door-signal generation must be nonzero");
    Name(plan_.signalEntityName);
    Name(plan_.ownerEntityName);
    Name(plan_.ownerClassname);
    Name(plan_.useKey);
    Name(plan_.voiceDefinition);
    Name(plan_.actorAnimation);
    Need(Brain(plan_.brainResource, false) == plan_.brainResource,
         "Door-signal runtime requires canonical brain resource");
    Need(plan_.onUsedNodeId && plan_.voiceNodeId && plan_.actorStartNodeId &&
             plan_.onUsedNodeId != plan_.voiceNodeId && plan_.onUsedNodeId != plan_.actorStartNodeId &&
             plan_.voiceNodeId != plan_.actorStartNodeId && plan_.onUsedRawFlags == 0 &&
             plan_.voiceRawFlags == 16 && plan_.actorRawFlags == 16,
         "Invalid door-signal runtime plan");
    if (!plan_.targetEntityName.empty()) {
        Name(plan_.targetEntityName);
        Name(plan_.targetClassname);
        Need(plan_.targetEntityName != plan_.ownerEntityName &&
                 plan_.targetEntityName != plan_.signalEntityName,
             "Recursive door-signal target");
    }
}

TrainDoorSignalResult TrainDoorSignalRuntime::OnUsed(const TrainDoorUseEvent &event) {
    if (event.levelGeneration != generation_ || event.eventId == 0 ||
        event.sourceEntityName != plan_.signalEntityName ||
        event.ownerEntityName != plan_.ownerEntityName || event.useKey != plan_.useKey)
        return Result(TrainDoorSignalStatus::WrongEvent);
    if (status_ != TrainDoorSignalStatus::AwaitingUse)
        return Result(TrainDoorSignalStatus::AlreadyAccepted);
    auto request = Command(plan_, generation_, event.eventId, TrainDoorCommand::PrepareVoiceResources);
    auto result = Result(TrainDoorSignalStatus::VoicePreparationRequested, request);
    pending_ = std::move(request);
    eventId_ = event.eventId;
    status_ = result.status;
    return result;
}

TrainDoorSignalResult TrainDoorSignalRuntime::VoiceResourcesReady(const TrainDoorCommand &request,
                                                                  bool ready) {
    if (status_ != TrainDoorSignalStatus::VoicePreparationRequested || !Same(request, pending_))
        return Result(TrainDoorSignalStatus::WrongCallback);
    if (!ready)
        return Result(TrainDoorSignalStatus::ResourceNotReady);
    auto command = Command(plan_, generation_, eventId_, TrainDoorCommand::StartActorAnimation);
    auto result = Result(TrainDoorSignalStatus::ActorStartRequested, command);
    pending_ = std::move(command);
    status_ = result.status;
    return result;
}

TrainDoorSignalResult TrainDoorSignalRuntime::ConsumeVoiceReadiness(const TrainDoorCommand &request,
                                                                    TrainDoorVoiceReadiness &provider) {
    // Validate the pending request first, so a mismatched command can never
    // consume the provider's once-only Ready.
    if (status_ != TrainDoorSignalStatus::VoicePreparationRequested || !Same(request, pending_))
        return Result(TrainDoorSignalStatus::WrongCallback);
    if (!provider.ConsumeReady())
        return Result(TrainDoorSignalStatus::ResourceNotReady);
    auto command = Command(plan_, generation_, eventId_, TrainDoorCommand::StartActorAnimation);
    auto result = Result(TrainDoorSignalStatus::ActorStartRequested, command);
    pending_ = std::move(command);
    status_ = result.status;
    return result;
}

TrainDoorSignalResult TrainDoorSignalRuntime::ActorStartCommitted(const TrainDoorCommand &request,
                                                                  bool committed) {
    if (status_ != TrainDoorSignalStatus::ActorStartRequested || !Same(request, pending_))
        return Result(TrainDoorSignalStatus::WrongCallback);
    if (!committed)
        return Result(TrainDoorSignalStatus::ActorStartNotCommitted);
    if (plan_.targetEntityName.empty()) {
        status_ = TrainDoorSignalStatus::CompleteWithoutTarget;
        return Result(status_);
    }
    auto command = Command(plan_, generation_, eventId_, TrainDoorCommand::ActivateTarget);
    auto result = Result(TrainDoorSignalStatus::TargetActivationRequested, command);
    status_ = result.status;
    return result;
}

} // namespace wolf2009

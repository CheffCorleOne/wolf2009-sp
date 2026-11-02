// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainArrival.h"
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *message) {
    if (!ok)
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
void Name(const std::string &name) {
    Need(!name.empty() && name.size() <= 1024, "Unsupported arrival entity/resource name");
    for (unsigned char c : name)
        Need(c >= 32 && c < 127, "Unsupported arrival name byte");
}
std::string BrainResource(std::string value, bool binding) {
    Name(value);
    value = Fold(value);
    if (binding && value.size() >= 6 && value.compare(value.size() - 6, 6, ".brain") == 0)
        value.resize(value.size() - 6);
    if (binding && value.compare(0, 7, "brains/") != 0)
        value = "brains/" + value;
    Need(value.compare(0, 7, "brains/") == 0 && value.size() > 7, "Expected a BRAI virtual resource");
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto part = value.substr(begin, end == std::string::npos ? end : end - begin);
        Need(!part.empty() && part != "." && part != ".." && part.find(':') == std::string::npos,
             "Malformed BRAI virtual resource");
        if (end == std::string::npos)
            break;
        begin = end + 1;
        Need(begin < value.size(), "Malformed BRAI virtual resource");
    }
    return value;
}
const EntityProperty *Unique(const EntityRecord &entity, const char *key) {
    const EntityProperty *result = NULL;
    for (const auto &p : entity.properties)
        if (Fold(p.key) == key) {
            Need(!result, "Duplicate arrival ENTS property");
            result = &p;
        }
    return result;
}
std::string Text(const EntityRecord &entity, const char *key, bool required = false) {
    const auto *p = Unique(entity, key);
    Need(!required || p, "Missing arrival ENTS text property");
    if (!p)
        return "";
    Need(p->kind == EntityProperty::Text, "Arrival ENTS text has wrong value type");
    if (!p->text.empty())
        Name(p->text);
    return p->text;
}
bool Hide(const EntityProperty &property) {
    if (property.kind == EntityProperty::Integer) {
        Need(property.integer == 0 || property.integer == 1, "Unsupported ENTS hide integer");
        return property.integer == 1;
    }
    Need(property.kind == EntityProperty::Floats && property.numbers.size() == 1 &&
             std::isfinite(property.numbers[0]) && (property.numbers[0] == 0 || property.numbers[0] == 1),
         "Unsupported ENTS hide value type");
    return property.numbers[0] == 1;
}
std::uint32_t Integer(const std::string &value, bool allowZero = false) {
    Need(!value.empty(), "Empty arrival node ID");
    std::uint32_t result = 0;
    for (unsigned char c : value) {
        Need(c >= '0' && c <= '9', "Malformed arrival node ID");
        const auto digit = unsigned(c - '0');
        Need(result <= (2147483647u - digit) / 10, "Arrival node ID limit");
        result = result * 10 + digit;
    }
    Need(allowZero || result != 0, "Zero arrival node ID");
    return result;
}
void MetadataInteger(const std::string &text) {
    const auto first = !text.empty() && text[0] == '-' ? 1u : 0u;
    Need(first < text.size(), "Invalid arrival metadata integer");
    Integer(text.substr(first), true);
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
std::map<std::uint32_t, Node> Nodes(const std::vector<unsigned char> &data, const LinearMoverLimits &limits) {
    std::map<std::uint32_t, Node> nodes;
    std::size_t cursor = 0, record = 0, count = 0;
    Node *owner = NULL;
    while (cursor < data.size()) {
        Need(record < limits.records, "Arrival record budget");
        std::size_t depth = 0;
        while (cursor < data.size() && data[cursor] == '\t') {
            ++depth;
            ++cursor;
        }
        Need(depth <= limits.nesting, "Arrival nesting budget");
        std::vector<std::string> tokens;
        while (cursor < data.size() && data[cursor] != '\n') {
            Need(++count <= limits.tokens, "Arrival token budget");
            std::string token;
            while (cursor < data.size() && data[cursor] != 0) {
                const auto c = data[cursor++];
                Need(c >= 32 && c < 127, "Unsupported arrival token byte");
                Need(token.size() < limits.tokenBytes, "Arrival token byte budget");
                token += char(c);
            }
            Need(cursor < data.size(), "Unterminated arrival token");
            ++cursor;
            Need(!token.empty(), "Empty arrival token");
            tokens.push_back(std::move(token));
        }
        if (cursor < data.size())
            ++cursor;
        Need(!tokens.empty(), "Empty arrival record");
        if (record == 0)
            Need(depth == 0 && tokens == std::vector<std::string>({"h1023", "32", "32", "32", "0"}),
                 "Unsupported arrival header");
        else if (depth == 0) {
            Need(tokens.size() >= 2, "Truncated arrival node");
            Node node;
            node.type = Integer(tokens[0]);
            node.id = Integer(tokens[1]);
            node.tokens = std::move(tokens);
            const auto entry = nodes.insert(std::make_pair(node.id, std::move(node)));
            Need(entry.second, "Duplicate arrival node ID");
            owner = &entry.first->second;
        } else {
            Need(owner, "Nested arrival record without owner");
            if (depth == 1) {
                Edge edge;
                edge.tokens = std::move(tokens);
                owner->edges.push_back(std::move(edge));
            } else if (owner->edges.empty())
                owner->unknownNested = true;
            else if (depth == 2 && tokens == std::vector<std::string>({"-1"}))
                ++owner->edges.back().sentinels;
            else
                owner->edges.back().unknown = true;
        }
        ++record;
    }
    return nodes;
}
const Node &At(const std::map<std::uint32_t, Node> &nodes, std::uint32_t id) {
    const auto found = nodes.find(id);
    Need(found != nodes.end(), "Arrival edge references missing node");
    return found->second;
}
std::uint32_t Next(const Node &node, const char *mode, const char *selector, std::size_t sentinels) {
    Need(!node.unknownNested && node.edges.size() == 1, "Branched/missing arrival output is unsupported");
    const auto &edge = node.edges[0];
    const auto &t = edge.tokens;
    Need(!edge.unknown && edge.sentinels == sentinels && t.size() == 99 && t[0] == "16" && t[1] == mode &&
             t[3] == "0" && t[4] == selector && t[98] == "1.000000",
         "Unsupported arrival output profile");
    for (std::size_t i = 5; i < 98; i++)
        Need(t[i] == "0", "Nondefault arrival edge field is unsupported");
    return Integer(t[2]);
}
std::string Target(const Node &node, std::uint32_t type, const char *property) {
    const auto &t = node.tokens;
    Need(node.type == type && t.size() == 9 && t[5] == "^" + std::to_string(type) && t[6] == "0" &&
             t[7] == property,
         "Unsupported arrival action/property profile");
    for (unsigned i = 2; i <= 4; i++)
        MetadataInteger(t[i]);
    Name(t[8]);
    return t[8];
}
TrainArrivalTarget EntityTarget(const Node &node, const std::string &name,
                                const LevelEntityRegistry &entities, bool show) {
    const auto *state = entities.Find(name);
    Need(state && state->alive, "Arrival action target does not exist");
    if (show)
        Need(state->classname == "func_static" && state->staticVisibilityProfileKnown && state->hiddenKnown &&
                 state->initiallyHidden && state->hidden,
             "UseTarget requires qualified plain func_static with explicit initial hide");
    TrainArrivalTarget target;
    target.nodeId = node.id;
    target.name = name;
    target.classname = state->classname;
    target.model = state->model;
    return target;
}
bool Matches(const LevelEntityState *state, const TrainArrivalTarget &target) {
    return state && state->alive && state->classname == target.classname && state->model == target.model;
}
} // namespace
LevelEntityRegistry::LevelEntityRegistry(const EntityDocument &document, const TrainArrivalLimits &limits) {
    Need(document.entities.size() <= limits.registryEntities, "Level registry entity budget");
    std::size_t strings = 0;
    std::set<std::string> foldedNames;
    for (const auto &entity : document.entities) {
        const auto *rawName = Unique(entity, "name");
        if (!rawName)
            continue;
        Need(rawName->kind == EntityProperty::Text, "ENTS entity name is not text");
        if (rawName->text.empty())
            continue;
        LevelEntityState state;
        state.name = rawName->text;
        Name(state.name);
        state.classname = Text(entity, "classname", true);
        Name(state.classname);
        state.model = Text(entity, "model");
        state.brain = Text(entity, "brain");
        for (const auto *value : {&state.name, &state.classname, &state.model, &state.brain}) {
            Need(strings <= limits.registryStringBytes &&
                     value->size() <= limits.registryStringBytes - strings,
                 "Level registry string budget");
            strings += value->size();
        }
        if (const auto *hide = Unique(entity, "hide")) {
            state.hiddenKnown = true;
            state.hidden = state.initiallyHidden = Hide(*hide);
        }
        if (state.classname == "func_static") {
            const std::set<std::string> supported{"name",   "classname",      "model",
                                                  "origin", "rotation",       "angle",
                                                  "skin",   "rendersideonly", "hide"};
            state.staticVisibilityProfileKnown = true;
            for (const auto &property : entity.properties)
                if (supported.count(Fold(property.key)) == 0) {
                    state.staticVisibilityProfileKnown = false;
                    break;
                }
        }
        Need(foldedNames.insert(Fold(state.name)).second, "Ambiguous level entity name");
        auto key = state.name;
        Need(entities_.insert(std::make_pair(std::move(key), std::move(state))).second,
             "Duplicate level entity name");
    }
}
const LevelEntityState *LevelEntityRegistry::Find(const std::string &name) const {
    const auto found = entities_.find(name);
    return found == entities_.end() ? NULL : &found->second;
}
TrainArrivalPlan DecodeTrainArrivalH1023(const std::vector<unsigned char> &data, const std::string &resource,
                                         const LevelEntityRegistry &entities,
                                         const TrainArrivalLimits &limits) {
    const auto startup = DecodeDirectBrainStartupH1023(data, limits.brain);
    const auto nodes = Nodes(data, limits.brain);
    const auto &move = At(nodes, startup.moveNodeId);
    TrainArrivalPlan plan;
    plan.brainResource = BrainResource(resource, false);
    plan.onSpawnNodeId = startup.onSpawnNodeId;
    plan.moveNodeId = startup.moveNodeId;
    std::set<std::uint32_t> visited{plan.onSpawnNodeId, plan.moveNodeId};
    std::set<std::string> targets;
    const Node *node = &At(nodes, Next(move, "3", "2", 1));
    if (node->type == 70) {
        Need(visited.insert(node->id).second, "Arrival cycle unsupported");
        plan.hasRemoval = true;
        plan.removal = EntityTarget(*node, Target(*node, 70, "mRemoveTarget"), entities, false);
        Need(targets.insert(plan.removal.name).second, "Duplicate arrival target");
        node = &At(nodes, Next(*node, "1", "3", 0));
    }
    for (unsigned i = 0; i < 3; i++) {
        Need(visited.insert(node->id).second, "Arrival cycle unsupported");
        const auto target = EntityTarget(*node, Target(*node, 68, "mUseTarget"), entities, true);
        Need(targets.insert(target.name).second, "Duplicate arrival target");
        plan.shows.push_back(target);
        node = &At(nodes, Next(*node, "1", "0", 0));
    }
    Need(visited.count(node->id) == 0, "Arrival continuation cycles into executed prefix");
    plan.unsupportedNodeId = node->id;
    plan.unsupportedNodeType = node->type;
    plan.unexecutedNodeCount = nodes.size() - visited.size();
    const auto &tail = node->tokens;
    if (node->type == 68 && tail.size() == 11 && tail[5] == "^68" && tail[6] == "0" &&
        tail[7] == "mUseTarget" && tail[9] == "mUseKey") {
        Name(tail[8]);
        Name(tail[10]);
        plan.unsupportedTargetName = tail[8];
        plan.unsupportedUseKey = tail[10];
        if (const auto *entity = entities.Find(tail[8]))
            plan.unsupportedTargetClassname = entity->classname;
    }
    return plan;
}
TrainArrivalRuntime::TrainArrivalRuntime(const TrainArrivalPlan &plan, const std::string &owner,
                                         std::uint64_t generation)
    : plan_(plan), owner_(owner), generation_(generation) {
    Name(owner_);
    Need(generation_ != 0 && plan_.moveNodeId != 0 && plan_.onSpawnNodeId != 0 &&
             plan_.moveNodeId != plan_.onSpawnNodeId && plan_.shows.size() == 3,
         "Invalid arrival runtime plan/source");
    Need(plan_.brainResource == BrainResource(plan_.brainResource, false),
         "Noncanonical arrival brain resource");
    std::set<std::string> names;
    std::set<std::uint32_t> ids{plan_.moveNodeId, plan_.onSpawnNodeId};
    if (plan_.hasRemoval) {
        Name(plan_.removal.name);
        Need(plan_.removal.name != owner_ && plan_.removal.nodeId &&
                 ids.insert(plan_.removal.nodeId).second && names.insert(plan_.removal.name).second,
             "Ambiguous arrival removal");
    } else
        Need(plan_.removal.name.empty() && plan_.removal.nodeId == 0, "Inconsistent bypass arrival plan");
    for (const auto &target : plan_.shows) {
        Name(target.name);
        Need(target.name != owner_ && target.nodeId && target.classname == "func_static" &&
                 ids.insert(target.nodeId).second && names.insert(target.name).second,
             "Ambiguous arrival show target");
    }
}
TrainArrivalResult TrainArrivalRuntime::ObserveCommittedMove(const TrainArrivalMoveStamp &before,
                                                             const TrainArrivalMoveStamp &after,
                                                             LevelEntityRegistry &entities) {
    TrainArrivalResult result;
    result.unsupportedNodeId = plan_.unsupportedNodeId;
    result.unsupportedNodeType = plan_.unsupportedNodeType;
    for (const auto *stamp : {&before, &after})
        if (stamp->brainResource != plan_.brainResource || stamp->ownerEntityName != owner_ ||
            stamp->moveNodeId != plan_.moveNodeId || stamp->levelGeneration != generation_) {
            result.status = TrainArrivalStatus::WrongSource;
            return result;
        }
    if (applied_) {
        result.status = TrainArrivalStatus::AlreadyApplied;
        return result;
    }
    if (!before.spawnDelivered || !after.spawnDelivered) {
        result.status = TrainArrivalStatus::UnconfirmedSpawn;
        return result;
    }
    if (!before.arrived && !after.arrived) {
        result.status = TrainArrivalStatus::NoCompletion;
        return result;
    }
    if (before.arrived || !after.arrived || !before.active || after.active) {
        result.status = TrainArrivalStatus::InvalidTransition;
        return result;
    }
    const auto *owner = entities.Find(owner_);
    bool ownerValid = owner && owner->alive && owner->classname == "func_mover";
    if (ownerValid)
        try {
            ownerValid = BrainResource(owner->brain, true) == plan_.brainResource;
        } catch (const std::runtime_error &) {
            ownerValid = false;
        }
    if (!ownerValid) {
        result.status = TrainArrivalStatus::RegistryConflict;
        return result;
    }
    if (plan_.hasRemoval && !Matches(entities.Find(plan_.removal.name), plan_.removal)) {
        result.status = TrainArrivalStatus::RegistryConflict;
        return result;
    }
    for (const auto &target : plan_.shows) {
        const auto *state = entities.Find(target.name);
        if (!Matches(state, target) || !state->staticVisibilityProfileKnown || !state->hiddenKnown ||
            !state->initiallyHidden || !state->hidden) {
            result.status = TrainArrivalStatus::RegistryConflict;
            return result;
        }
    }
    // Allocate every event/name before touching live state. Boolean writes and
    // guard commit below cannot allocate; renderer receives owned stable deltas.
    result.changes.reserve(plan_.shows.size() + (plan_.hasRemoval ? 1 : 0));
    auto stage = [&](const TrainArrivalTarget &target, TrainArrivalChange::Kind kind) {
        const auto *state = entities.Find(target.name);
        TrainArrivalChange change;
        change.kind = kind;
        change.name = target.name;
        change.sourceNodeId = target.nodeId;
        change.wasAlive = state->alive;
        change.wasHidden = state->hidden;
        change.alive = kind != TrainArrivalChange::RemoveEntity;
        change.hidden = kind == TrainArrivalChange::ShowEntity ? false : state->hidden;
        result.changes.push_back(std::move(change));
    };
    if (plan_.hasRemoval)
        stage(plan_.removal, TrainArrivalChange::RemoveEntity);
    for (const auto &target : plan_.shows)
        stage(target, TrainArrivalChange::ShowEntity);
    for (const auto &change : result.changes) {
        auto &state = entities.entities_.find(change.name)->second;
        state.alive = change.alive;
        state.hidden = change.hidden;
    }
    applied_ = true;
    result.status = TrainArrivalStatus::Applied;
    return result;
}
const char *TrainArrivalStatusText(TrainArrivalStatus status) {
    switch (status) {
    case TrainArrivalStatus::NoCompletion:
        return "No committed completion transition";
    case TrainArrivalStatus::Applied:
        return "Arrival prefix applied once; later actions remain unsupported";
    case TrainArrivalStatus::AlreadyApplied:
        return "Arrival prefix already applied for this level generation";
    case TrainArrivalStatus::WrongSource:
        return "Arrival refused: wrong brain/owner/node/level generation";
    case TrainArrivalStatus::UnconfirmedSpawn:
        return "Arrival refused: spawn event was not already delivered";
    case TrainArrivalStatus::InvalidTransition:
        return "Arrival refused: expected committed active-to-arrived transition";
    case TrainArrivalStatus::RegistryConflict:
        return "Arrival refused: live entity registry differs from qualified input";
    }
    return "Unknown arrival status";
}
} // namespace wolf2009

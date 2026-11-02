// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainStartup.h"
#include <map>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::uint32_t Id(const std::string &text) {
    Need(!text.empty(), "Empty BRAI node identifier");
    std::uint32_t value = 0;
    for (unsigned char c : text) {
        Need(c >= '0' && c <= '9', "Invalid BRAI node identifier");
        const unsigned digit = c - '0';
        Need(value <= (2147483647u - digit) / 10, "BRAI node identifier limit");
        value = value * 10 + digit;
    }
    Need(value != 0, "Zero BRAI node identifier is unsupported");
    return value;
}
void OpaqueInteger(const std::string &text) {
    std::size_t i = !text.empty() && text[0] == '-' ? 1 : 0;
    Need(i < text.size(), "Invalid BRAI node metadata integer");
    std::uint32_t value = 0;
    for (; i < text.size(); i++) {
        const unsigned char c = text[i];
        Need(c >= '0' && c <= '9', "Invalid BRAI node metadata integer");
        const unsigned digit = c - '0';
        Need(value <= (2147483647u - digit) / 10, "BRAI node metadata integer limit");
        value = value * 10 + digit;
    }
}
struct Node {
    std::uint32_t type = 0, id = 0;
    std::size_t record = 0, outputs = 0, sentinels = 0;
    std::vector<std::string> header, edge;
};
} // namespace
BrainStartupPlan DecodeDirectBrainStartupH1023(const std::vector<unsigned char> &data,
                                               const LinearMoverLimits &limits) {
    Need(!data.empty() && data.size() <= limits.sourceBytes, "BRAI startup source size limit");
    std::vector<Node> nodes;
    std::map<std::uint32_t, std::size_t> byId;
    std::size_t cursor = 0, record = 0, totalTokens = 0;
    while (cursor < data.size()) {
        Need(record < limits.records, "BRAI startup record limit");
        std::size_t depth = 0;
        while (cursor < data.size() && data[cursor] == '\t') {
            ++cursor;
            ++depth;
        }
        Need(depth <= limits.nesting, "BRAI startup nesting limit");
        std::vector<std::string> tokens;
        while (cursor < data.size() && data[cursor] != '\n') {
            Need(++totalTokens <= limits.tokens, "BRAI startup token limit");
            std::string token;
            while (cursor < data.size() && data[cursor] != 0) {
                const auto c = data[cursor++];
                Need(c >= 32 && c <= 126, "Unsupported BRAI startup token byte");
                Need(token.size() < limits.tokenBytes, "BRAI startup token byte limit");
                token += char(c);
            }
            Need(cursor < data.size(), "Unterminated BRAI startup token");
            ++cursor;
            Need(!token.empty(), "Empty BRAI startup token");
            tokens.push_back(token);
        }
        if (cursor < data.size())
            ++cursor;
        Need(!tokens.empty(), "Empty BRAI startup record");
        if (record == 0)
            Need(depth == 0 && tokens == std::vector<std::string>({"h1023", "32", "32", "32", "0"}),
                 "Unsupported BRAI startup header");
        else if (depth == 0) {
            Need(tokens.size() >= 2, "Truncated BRAI node header");
            Node node;
            node.type = Id(tokens[0]);
            node.id = Id(tokens[1]);
            node.record = record;
            Need(byId.insert(std::make_pair(node.id, nodes.size())).second, "Duplicate BRAI node ID");
            if (node.type == 1)
                node.header = std::move(tokens);
            nodes.push_back(std::move(node));
        } else {
            Need(!nodes.empty(), "BRAI nested record has no owning node");
            auto &owner = nodes.back();
            if (depth == 1) {
                ++owner.outputs;
                if (owner.type == 1) {
                    Need(owner.outputs == 1, "Branched OnSpawn startup is unsupported");
                    owner.edge = std::move(tokens);
                }
            } else if (owner.type == 1) {
                Need(depth == 2 && owner.outputs == 1 && tokens == std::vector<std::string>({"-1"}),
                     "Unknown OnSpawn condition/subrecord profile");
                ++owner.sentinels;
            }
        }
        ++record;
    }
    const Node *source = NULL;
    for (const auto &node : nodes)
        if (node.type == 1) {
            Need(!source, "Ambiguous BRAI: multiple OnSpawn nodes");
            source = &node;
        }
    Need(source != NULL, "No OnSpawn node in BRAI");
    Need(source->header.size() == 9 && source->header[5] == "^1" && source->header[6] == "1" &&
             source->header[7] == "^74" && source->header[8] == "1024",
         "Unsupported OnSpawn field profile");
    for (unsigned i = 2; i <= 4; i++)
        OpaqueInteger(source->header[i]);
    Need(source->outputs == 1 && source->sentinels == 1,
         "OnSpawn needs one observed direct output and sentinel");
    const auto &edge = source->edge;
    Need(edge.size() == 99 && edge[0] == "16" && edge[1] == "1" && edge[98] == "1.000000",
         "Unsupported OnSpawn output profile");
    for (std::size_t i = 3; i < 98; i++)
        Need(edge[i] == "0", "Nondefault OnSpawn output/condition field unsupported");
    const auto destination = byId.find(Id(edge[2]));
    Need(destination != byId.end(), "OnSpawn output references missing node");
    const Node &move = nodes[destination->second];
    Need(move.type == 132,
         "Startup requires direct OnSpawn -> MoveToEnt; intermediate action/delay unsupported");
    BrainStartupPlan plan;
    plan.move = DecodeSelfLinearMoveH1023(data, limits);
    Need(plan.move.sourceRecord == move.record, "OnSpawn target differs from extracted linear mover");
    plan.onSpawnNodeId = source->id;
    plan.moveNodeId = move.id;
    plan.unexecutedNodeCount = nodes.size() - 2;
    plan.moveOutputRecords = move.outputs;
    return plan;
}
BrainStartupRuntime::BrainStartupRuntime(const BrainStartupPlan &plan, const EntityDocument &entities,
                                         LinearMoverVec3 worldOrigin)
    : mover_(ResolveSelfLinearMove(plan.move, entities, worldOrigin)) {}
bool BrainStartupRuntime::OnSpawn() {
    if (spawnDelivered_)
        return false;
    spawnDelivered_ = true;
    mover_.Activate();
    return true;
}
} // namespace wolf2009

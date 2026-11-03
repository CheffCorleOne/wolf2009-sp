// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainStartup.h"
#include "BrainGraph.h"
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
    BrainGraphLimits graphLimits;
    graphLimits.sourceBytes = limits.sourceBytes;
    graphLimits.records = limits.records;
    graphLimits.tokens = limits.tokens;
    graphLimits.tokenBytes = limits.tokenBytes;
    graphLimits.nesting = limits.nesting;
    const auto graph = DecodeBrainGraph(data, graphLimits);
    Need(graph.format == BrainGraph::H1023, "Unsupported BRAI startup header");
    // The structural reader preserves all data. This deliberately narrow
    // executor still requires the previously established ASCII profile.
    for (const auto &record : graph.records)
        for (const auto &token : record.tokens) {
            Need(!token.empty(), "Empty BRAI startup token");
            for (unsigned char c : token)
                Need(c >= 32 && c <= 126, "Unsupported BRAI startup token byte");
        }
    std::vector<Node> nodes;
    std::map<std::uint32_t, std::size_t> byId;
    for (const auto &source : graph.nodes) {
        Node node;
        node.type = source.type;
        node.id = source.id;
        node.record = source.record;
        node.outputs = source.outputs.size();
        byId.insert(std::make_pair(node.id, nodes.size()));
        if (source.type == 1) {
            node.header = graph.records[source.record].tokens;
            Need(source.outputs.size() <= 1, "Branched OnSpawn startup is unsupported");
            for (const auto outputIndex : source.outputs) {
                const auto &output = graph.outputs[outputIndex];
                Need(output.destinationsComplete && output.destinations.size() == 1,
                     "Direct startup requires exactly one decoded destination");
                node.edge = graph.records[output.record].tokens;
                for (std::size_t i = output.record + 1;
                     i < graph.records.size() && graph.records[i].depth > 1; ++i) {
                    const auto &condition = graph.records[i];
                    Need(condition.depth == 2 && condition.tokens == std::vector<std::string>({"-1"}),
                         "Unknown OnSpawn condition/subrecord profile");
                    ++node.sentinels;
                }
            }
        }
        nodes.push_back(std::move(node));
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

// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainGraph.h"
#include <map>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
void Need(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::uint32_t Id(const std::string &text) {
    Need(!text.empty(), "Empty BRAI identifier");
    std::uint32_t value = 0;
    for (unsigned char c : text) {
        Need(c >= '0' && c <= '9', "Invalid BRAI identifier");
        const unsigned digit = c - '0';
        Need(value <= (2147483647u - digit) / 10, "BRAI identifier limit");
        value = value * 10 + digit;
    }
    Need(value != 0, "Zero BRAI identifier");
    return value;
}
} // namespace

const BrainNode *BrainGraph::FindNode(std::uint32_t id) const {
    for (const auto &node : nodes)
        if (node.id == id)
            return &node;
    return NULL;
}

BrainGraph DecodeBrainGraph(const std::vector<unsigned char> &data, const BrainGraphLimits &limits) {
    Need(!data.empty() && data.size() <= limits.sourceBytes, "BRAI source size limit");
    BrainGraph graph;
    std::map<std::uint32_t, std::size_t> byId;
    std::vector<std::size_t> ancestors;
    std::size_t cursor = 0, owner = BrainRecord::None(), activeOutput = BrainRecord::None();
    while (cursor < data.size()) {
        Need(graph.records.size() < limits.records, "BRAI record limit");
        BrainRecord record;
        record.sourceBegin = cursor;
        while (cursor < data.size() && data[cursor] == '\t') {
            ++cursor;
            Need(++record.depth <= limits.nesting, "BRAI nesting limit");
        }
        while (cursor < data.size() && data[cursor] != '\n') {
            Need(graph.tokenCount < limits.tokens, "BRAI token limit");
            ++graph.tokenCount;
            const std::size_t begin = cursor;
            while (cursor < data.size() && data[cursor] != 0) {
                Need(cursor - begin < limits.tokenBytes, "BRAI token byte limit");
                ++cursor;
            }
            Need(cursor < data.size(), "Unterminated BRAI token");
            record.tokens.emplace_back(reinterpret_cast<const char *>(data.data() + begin),
                                       cursor - begin);
            ++cursor;
        }
        // The final record may end directly after its final NUL.
        if (cursor < data.size())
            ++cursor;
        record.sourceEnd = cursor;
        Need(!record.tokens.empty(), "Empty BRAI record");
        const std::size_t index = graph.records.size();
        if (index == 0) {
            Need(record.depth == 0, "Nested BRAI header");
            if (record.tokens == std::vector<std::string>({"h1023", "32", "32", "32", "0"}))
                graph.format = BrainGraph::H1023;
            else if (record.tokens == std::vector<std::string>({"g1023", "32", "32", "32"}))
                graph.format = BrainGraph::G1023;
            else
                throw std::runtime_error("Unsupported BRAI header");
        } else if (record.depth == 0) {
            Need(record.tokens.size() >= 2, "Truncated BRAI node header");
            BrainNode node;
            node.type = Id(record.tokens[0]);
            node.id = Id(record.tokens[1]);
            node.record = index;
            owner = graph.nodes.size();
            Need(byId.insert(std::make_pair(node.id, owner)).second, "Duplicate BRAI node ID");
            record.node = owner;
            graph.nodes.push_back(std::move(node));
            activeOutput = BrainRecord::None();
        } else {
            Need(owner != BrainRecord::None(), "BRAI nested record has no owning node");
            Need(record.depth <= ancestors.size(), "BRAI record skipped a parent depth");
            record.parentRecord = ancestors[record.depth - 1];
            record.node = owner;
            if (record.depth == 1) {
                Need(record.tokens.size() >= 3, "Truncated BRAI output");
                BrainOutput output;
                output.record = index;
                output.sourceNode = owner;
                // Resolve after all nodes, including forward references, are known.
                output.targetNode = Id(record.tokens[2]);
                activeOutput = graph.outputs.size();
                graph.nodes[owner].outputs.push_back(activeOutput);
                graph.outputs.push_back(std::move(output));
            } else {
                Need(activeOutput != BrainRecord::None(), "BRAI condition has no owning output");
                if (record.depth == 2)
                    graph.outputs[activeOutput].conditions.push_back(index);
            }
        }
        ancestors.resize(record.depth);
        ancestors.push_back(index);
        graph.records.push_back(std::move(record));
    }
    for (auto &output : graph.outputs) {
        const auto target = byId.find(static_cast<std::uint32_t>(output.targetNode));
        Need(target != byId.end(), "BRAI output references missing node");
        output.targetNode = target->second;
    }
    return graph;
}
} // namespace wolf2009

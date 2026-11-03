// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_BRAIN_GRAPH_H
#define WOLF2009_BRAIN_GRAPH_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
struct BrainGraphLimits {
    std::size_t sourceBytes = 4u * 1024u * 1024u, records = 20000, tokens = 500000, tokenBytes = 1024,
                nesting = 64;
};

// Indices refer to this document's vectors. None() denotes no parent.
struct BrainRecord {
    std::size_t depth = 0, sourceBegin = 0, sourceEnd = 0, parentRecord = None(), node = None();
    // NUL-delimited source bytes, with no assumed character encoding. Empty strings
    // and legacy 8-bit text occur in retail graphs and must survive decoding.
    std::vector<std::string> tokens;
    static std::size_t None() { return static_cast<std::size_t>(-1); }
};
struct BrainNode {
    std::uint32_t type = 0, id = 0;
    std::size_t record = 0;
    std::vector<std::size_t> outputs;
};
struct BrainDestination {
    std::size_t targetNode = 0, slot = 0;
};
struct BrainOutput {
    std::size_t sourceNode = 0, targetNode = BrainRecord::None(), record = 0;
    // targetNode remains the first populated destination for existing consumers.
    // Fixed-layout outputs carry 32 (node ID, weight, auxiliary) triples.
    // The latter two fields remain in records[record].tokens, without guessing
    // their lifecycle. Zero/negative destination slots are unpopulated.
    std::vector<BrainDestination> destinations;
    bool destinationsComplete = false;
    // Direct children only. Deeper records retain their explicit parents.
    std::vector<std::size_t> conditions;
};
struct BrainGraph {
    enum Format { H1023, G1023 } format = H1023;
    std::vector<BrainRecord> records;
    std::vector<BrainNode> nodes;
    std::vector<BrainOutput> outputs;
    std::size_t tokenCount = 0;
    const BrainNode *FindNode(std::uint32_t id) const;
};

// Structural decoding only. Output flags, node properties and condition bodies
// remain opaque. Successful parsing does not authorize executing any node or
// treating a conditional output as unconditional.
BrainGraph DecodeBrainGraph(const std::vector<unsigned char> &data,
                            const BrainGraphLimits &limits = BrainGraphLimits());
} // namespace wolf2009
#endif

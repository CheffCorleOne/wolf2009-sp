// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainGraph.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::vector<unsigned char> Bytes(std::string source) {
    for (auto &c : source)
        if (c == '|')
            c = 0;
    return {source.begin(), source.end()};
}
void Refuses(const std::function<void()> &test) {
    bool refused = false;
    try {
        test();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Malformed BRAI graph accepted");
}
const std::string header = "h1023|32|32|32|0|\n";
const std::string body = "1|20|opaque||\x92|\n\t17|3|9|opaque|\n\t\t65|x|y|\n"
                         "\t\t-1|\n\t32|0|20|\n\t\t-1|\n132|9|property|value|";
} // namespace
int RunBrainGraphTests() {
    int passed = 0;
    try {
        const auto bytes = Bytes(header + body);
        const auto graph = DecodeBrainGraph(bytes);
        Need(graph.format == BrainGraph::H1023 && graph.nodes.size() == 2 && graph.outputs.size() == 2 &&
                 graph.records.size() == 8 && graph.FindNode(20) == &graph.nodes[0] &&
                 !graph.FindNode(21),
             "Graph nodes not indexed by authored ID");
        ++passed;
        Need(graph.nodes[0].type == 1 && graph.outputs[0].targetNode == 1 &&
                 graph.outputs[1].targetNode == 0 && graph.outputs[0].conditions.size() == 2 &&
                 graph.records[3].parentRecord == 2 && graph.records[4].parentRecord == 2 &&
                 graph.records[6].parentRecord == 5 &&
                 graph.records[7].parentRecord == BrainRecord::None(),
             "Branched/cyclic graph or condition ownership changed");
        ++passed;
        Need(graph.records[1].tokens[3].empty() &&
                 graph.records[1].tokens[4] == std::string(1, '\x92') &&
                 graph.records[2].tokens[0] == "17" && graph.records[3].tokens[0] == "65" &&
                 graph.records.back().sourceEnd == bytes.size(),
             "Opaque data or byte offsets lost");
        ++passed;
        const auto legacy = DecodeBrainGraph(Bytes("g1023|32|32|32|\n" + body));
        Need(legacy.format == BrainGraph::G1023 && legacy.outputs.size() == 2,
             "Observed g1023 envelope rejected");
        ++passed;
        Need(DecodeBrainGraph(Bytes(header)).nodes.empty(), "Empty graph envelope rejected");
        ++passed;
        const auto deep = DecodeBrainGraph(Bytes(header + "1|1|\n\t16|1|1|\n\t\tx|\n\t\t\ty|\n"));
        Need(deep.records[4].parentRecord == 3 && deep.outputs[0].conditions.size() == 1,
             "Nested opaque condition flattened");
        ++passed;
        const std::string malformed[] = {"",
                                         "h1024|32|32|32|0|\n",
                                         header + "1|",
                                         header + "1|0|\n",
                                         header + "0|1|\n",
                                         header + "-1|1|\n",
                                         header + "1|2147483648|\n",
                                         header + "1|1|\n3|1|\n",
                                         header + "\t16|1|1|\n",
                                         header + "1|1|\n\t\t-1|\n",
                                         header + "1|1|\n\t16|1|2|\n",
                                         header + "1|1|\n\t16|1|0|\n",
                                         header + "1|1|\n\t16|1|\n",
                                         header + "1|1|\n\n",
                                         header + "1|1|\n\t16|1|1|\n\t\t\tx|\n",
                                         header + "1|1|unterminated"};
        for (const auto &source : malformed) {
            Refuses([&] { DecodeBrainGraph(Bytes(source)); });
            ++passed;
        }
        BrainGraphLimits limits;
        limits.sourceBytes = bytes.size() - 1;
        Refuses([&] { DecodeBrainGraph(bytes, limits); });
        ++passed;
        limits = BrainGraphLimits();
        limits.records = graph.records.size() - 1;
        Refuses([&] { DecodeBrainGraph(bytes, limits); });
        ++passed;
        limits = BrainGraphLimits();
        limits.tokens = graph.tokenCount - 1;
        Refuses([&] { DecodeBrainGraph(bytes, limits); });
        ++passed;
        limits = BrainGraphLimits();
        limits.tokenBytes = 5;
        Refuses([&] { DecodeBrainGraph(bytes, limits); });
        ++passed;
        limits = BrainGraphLimits();
        limits.nesting = 1;
        Refuses([&] { DecodeBrainGraph(bytes, limits); });
        ++passed;
        std::cout << "BRAI graph self-tests: " << passed << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "BRAI graph test failed after " << passed << ": " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

// SPDX-License-Identifier: GPL-3.0-or-later
#include "Archive.h"
#include "BrainGraph.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace wolf2009 {
int RunBrainGraphTests();
}
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test")
        return wolf2009::RunBrainGraphTests();
    if (argc < 2) {
        std::cerr << "Usage: wolf2009-brains <owner-supplied .mpk/.spk> [...]\n";
        return 2;
    }
    try {
        std::size_t sources = 0, records = 0, nodes = 0, outputs = 0, tokens = 0, legacy = 0;
        std::map<std::uint32_t, std::size_t> types;
        for (int i = 1; i < argc; ++i) {
            std::ifstream stream(argv[i], std::ios::binary);
            if (!stream)
                throw std::runtime_error("Cannot open input package");
            const auto archive = wolf2009::Archive::Scan(stream);
            for (std::size_t c = 0; c < archive.Chunks().size(); ++c) {
                const auto &chunk = archive.Chunks()[c];
                if (chunk.type != "BRAI")
                    continue;
                for (std::size_t f = 0; f < chunk.entries.size(); ++f) {
                    const auto &file = chunk.entries[f];
                    try {
                        const auto graph = wolf2009::DecodeBrainGraph(archive.ReadEntry(stream, c, f));
                        ++sources;
                        records += graph.records.size();
                        nodes += graph.nodes.size();
                        outputs += graph.outputs.size();
                        tokens += graph.tokenCount;
                        legacy += graph.format == wolf2009::BrainGraph::G1023 ? 1 : 0;
                        for (const auto &node : graph.nodes)
                            ++types[node.type];
                    } catch (const std::exception &e) {
                        throw std::runtime_error(file.name + ": " + e.what());
                    }
                }
            }
        }
        std::cout << "BRAI sources=" << sources << " records=" << records << " nodes=" << nodes
                  << " outputs=" << outputs << " tokens=" << tokens << " g1023=" << legacy
                  << " distinctTypes=" << types.size() << '\n';
        for (const auto &type : types)
            std::cout << "type=" << type.first << " nodes=" << type.second << '\n';
        std::cout << "Structural decoding only; node/condition execution is not implied.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "BRAI validation failed: " << e.what() << '\n';
        return 1;
    }
}

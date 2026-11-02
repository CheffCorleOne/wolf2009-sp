// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapPhysics.h"
#include <stdexcept>

namespace wolf2009 {
namespace {
std::string ResourceKey(std::string name) {
    for (char& c : name) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return name;
}

std::vector<unsigned char> ReadMapResource(std::istream& input, const Archive& archive,
                                            const std::string& name, const std::string& type) {
    const std::string key = ResourceKey(name);
    std::size_t selectedChunk = 0, selectedEntry = 0;
    bool found = false;
    for (std::size_t c = 0; c < archive.Chunks().size(); ++c) {
        const Chunk& chunk = archive.Chunks()[c];
        if (chunk.type != type) continue;
        for (std::size_t e = 0; e < chunk.entries.size(); ++e) {
            if (ResourceKey(chunk.entries[e].name) != key) continue;
            if (found) throw std::runtime_error("Ambiguous " + type + " for map " + name);
            selectedChunk = c;
            selectedEntry = e;
            found = true;
        }
    }
    if (!found) throw std::runtime_error("Missing " + type + " for map " + name);
    return archive.ReadEntry(input, selectedChunk, selectedEntry);
}
}

EntityDocument ReadMapEntities(std::istream& input, const Archive& archive,
                               const std::string& procResourceName) {
    return DecodeEntities4(ReadMapResource(input, archive, procResourceName, "ENTS"));
}

std::vector<PlayerStartCandidate> ReadMapPlayerStarts(std::istream& input, const Archive& archive,
                                                     const std::string& procResourceName) {
    return CollectPlayerStarts(ReadMapEntities(input, archive, procResourceName));
}

StaticCollisionScene ReadMapStaticCollision(std::istream& input, const Archive& archive,
                                           const std::string& procResourceName) {
    return DecodeStaticCollision(ReadMapResource(input, archive, procResourceName, "HKXS"),
                                 procResourceName);
}
}

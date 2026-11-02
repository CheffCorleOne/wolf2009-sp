// SPDX-License-Identifier: GPL-3.0-or-later
// Independent, read-only Wolfenstein (2009) resource-container implementation.
#ifndef WOLF2009_ARCHIVE_H
#define WOLF2009_ARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace wolf2009 {

struct Limits {
    std::size_t compressedChunkBytes = 64u * 1024u * 1024u;
    std::size_t inflatedChunkBytes = 256u * 1024u * 1024u;
    std::size_t entries = 100000;
    std::size_t chunks = 4096;
    std::size_t nameBytes = 4096;
    std::size_t totalNameBytes = 64u * 1024u * 1024u;
};

struct Entry {
    std::string name; // Original container name; an extension is not invented.
    std::uint32_t size = 0;
    std::size_t payloadOffset = 0; // Relative to the inflated chunk.
};

struct Chunk {
    std::string type; // FourCC, reversed from the little-endian on-disk bytes.
    std::uint64_t headerOffset = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t inflatedSize = 0;
    std::vector<Entry> entries;
};

// The caller owns the input stream and keeps it open. Scan validates every
// payload but retains only metadata; ReadEntry inflates one chunk again.
// Binary format errors and stream failures throw std::runtime_error.
class Archive {
public:
    static Archive Scan(std::istream& input, const Limits& limits = Limits());
    const std::vector<Chunk>& Chunks() const { return chunks_; }
    std::vector<unsigned char> ReadEntry(std::istream& input,
                                        std::size_t chunkIndex,
                                        std::size_t entryIndex) const;

private:
    Limits limits_;
    std::vector<Chunk> chunks_;
};

int RunSelfTests();
} // namespace wolf2009
#endif

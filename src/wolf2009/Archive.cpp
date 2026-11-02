// SPDX-License-Identifier: GPL-3.0-or-later
#include "Archive.h"

#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t U32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

void Read(std::istream& input, unsigned char* data, std::size_t size) {
    Require(size <= std::size_t((std::numeric_limits<std::streamsize>::max)()),
            "Read length cannot be represented by the host stream");
    if (size) input.read(reinterpret_cast<char*>(data), std::streamsize(size));
    Require(bool(input), "Truncated archive or stream read failure");
}

void Seek(std::istream& input, std::uint64_t offset) {
    Require(offset <= std::uint64_t((std::numeric_limits<std::streamoff>::max)()),
            "Archive offset cannot be represented by the host stream");
    input.clear();
    input.seekg(std::streamoff(offset), std::ios::beg);
    Require(bool(input), "Archive seek failure");
}

std::size_t Aligned(std::size_t value, std::size_t end) {
    Require(value <= end && end - value >= ((4u - (value & 3u)) & 3u),
            "Truncated four-byte alignment padding");
    return value + ((4u - (value & 3u)) & 3u);
}

Bytes Inflate(std::istream& input, const Chunk& chunk, const Limits& limits) {
    Require(chunk.compressedSize != 0 && chunk.compressedSize <= limits.compressedChunkBytes,
            "Compressed chunk exceeds configured limit or is empty");
    Require(chunk.inflatedSize >= 4 && chunk.inflatedSize <= limits.inflatedChunkBytes,
            "Inflated chunk exceeds configured limit or has no file count");
    Require(chunk.compressedSize <= (std::numeric_limits<uInt>::max)() &&
            chunk.inflatedSize <= (std::numeric_limits<uInt>::max)(),
            "Chunk sizes exceed the decompressor integer range");
    Bytes compressed(chunk.compressedSize), inflated(chunk.inflatedSize);
    Seek(input, chunk.headerOffset + 12);
    Read(input, compressed.data(), compressed.size());
    z_stream state;
    std::memset(&state, 0, sizeof(state));
    state.next_in = compressed.data();
    state.avail_in = uInt(compressed.size());
    state.next_out = inflated.data();
    state.avail_out = uInt(inflated.size());
    Require(inflateInit(&state) == Z_OK, "Could not initialize zlib decompression");
    const int result = inflate(&state, Z_FINISH);
    const bool complete = result == Z_STREAM_END &&
                          state.total_out == inflated.size() &&
                          state.total_in == compressed.size();
    inflateEnd(&state);
    Require(complete, "Invalid zlib stream, size mismatch, or trailing compressed bytes");
    return inflated;
}

void ParseTable(Chunk& chunk, const Bytes& data, const Limits& limits,
                std::size_t& totalEntries, std::size_t& totalNameBytes) {
    const std::uint32_t count = U32(data.data());
    Require(totalEntries <= limits.entries && count <= limits.entries - totalEntries,
            "Archive entry count exceeds configured limit");
    Require(count <= (data.size() - 4) / 9, "File table cannot contain declared entry count");
    std::size_t cursor = 4;
    chunk.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t available = data.size() - cursor;
        const std::size_t searchLength = (std::min)(available, limits.nameBytes);
        const unsigned char* nameEnd = static_cast<const unsigned char*>(
            std::memchr(data.data() + cursor, 0, searchLength));
        Require(nameEnd != NULL, "Unterminated archive entry name or name limit exceeded");
        const std::size_t nameLength = std::size_t(nameEnd - data.data()) - cursor;
        Require(nameLength != 0, "Empty archive entry name");
        Require(totalNameBytes <= limits.totalNameBytes &&
                nameLength <= limits.totalNameBytes - totalNameBytes,
                "Archive name storage exceeds configured limit");
        Entry entry;
        entry.name.assign(reinterpret_cast<const char*>(data.data() + cursor), nameLength);
        totalNameBytes += nameLength;
        cursor += nameLength + 1;
        Require(data.size() - cursor >= 8, "Truncated entry size pair");
        entry.size = U32(data.data() + cursor);
        Require(entry.size == U32(data.data() + cursor + 4), "Entry size pair does not match");
        cursor += 8;
        chunk.entries.push_back(entry);
    }
    cursor = Aligned(cursor, data.size());
    for (std::size_t i = 0; i < chunk.entries.size(); ++i) {
        Entry& entry = chunk.entries[i];
        Require(entry.size <= data.size() - cursor, "Entry payload exceeds inflated chunk");
        entry.payloadOffset = cursor;
        cursor += entry.size;
        cursor = Aligned(cursor, data.size());
    }
    Require(cursor == data.size(), "Unexpected bytes after the final entry payload");
    totalEntries += count;
}
} // namespace

Archive Archive::Scan(std::istream& input, const Limits& limits) {
    input.clear();
    input.seekg(0, std::ios::end);
    const std::streamoff end = input.tellg();
    Require(end >= 4, "Archive is too small or not seekable");
    const std::uint64_t length = std::uint64_t(end);
    Seek(input, 0);
    unsigned char magic[4];
    Read(input, magic, sizeof(magic));
    Require(U32(magic) == 300, "Expected Wolfenstein SPK/MPK container version 300");
    Archive archive;
    archive.limits_ = limits;
    std::uint64_t position = 4;
    std::size_t totalEntries = 0, totalNameBytes = 0;
    while (position < length) {
        Require(archive.chunks_.size() < limits.chunks, "Archive chunk count exceeds configured limit");
        Require(length - position >= 12, "Truncated chunk header");
        Seek(input, position);
        unsigned char header[12];
        Read(input, header, sizeof(header));
        Chunk chunk;
        chunk.headerOffset = position;
        for (int i = 3; i >= 0; --i) {
            Require(header[i] >= 32 && header[i] <= 126, "Non-printable chunk FourCC");
            chunk.type.push_back(char(header[i]));
        }
        chunk.inflatedSize = U32(header + 4);
        chunk.compressedSize = U32(header + 8);
        Require(chunk.compressedSize <= length - position - 12, "Chunk payload exceeds archive");
        const Bytes data = Inflate(input, chunk, limits);
        ParseTable(chunk, data, limits, totalEntries, totalNameBytes);
        position += 12 + std::uint64_t(chunk.compressedSize);
        archive.chunks_.push_back(chunk);
    }
    return archive;
}

std::vector<unsigned char> Archive::ReadEntry(std::istream& input,
                                            std::size_t chunkIndex,
                                            std::size_t entryIndex) const {
    Require(chunkIndex < chunks_.size(), "Chunk index is out of range");
    const Chunk& chunk = chunks_[chunkIndex];
    Require(entryIndex < chunk.entries.size(), "Entry index is out of range");
    // Check metadata again so callers cannot accidentally reuse an index with
    // another stream or a file changed since Scan.
    Seek(input, chunk.headerOffset);
    unsigned char header[12];
    Read(input, header, sizeof(header));
    Require(U32(header + 4) == chunk.inflatedSize && U32(header + 8) == chunk.compressedSize,
            "Archive chunk header changed since indexing");
    for (int i = 0; i < 4; ++i)
        Require(header[3 - i] == static_cast<unsigned char>(chunk.type[i]),
                "Archive chunk type changed since indexing");
    const Bytes data = Inflate(input, chunk, limits_);
    Chunk checked;
    std::size_t entryCount = 0, nameBytes = 0;
    ParseTable(checked, data, limits_, entryCount, nameBytes);
    Require(checked.entries.size() == chunk.entries.size(), "Archive file table changed since indexing");
    for (std::size_t i = 0; i < checked.entries.size(); ++i)
        Require(checked.entries[i].name == chunk.entries[i].name &&
                checked.entries[i].size == chunk.entries[i].size &&
                checked.entries[i].payloadOffset == chunk.entries[i].payloadOffset,
                "Archive entry changed since indexing");
    const Entry& entry = chunk.entries[entryIndex];
    return Bytes(data.begin() + entry.payloadOffset,
                 data.begin() + entry.payloadOffset + entry.size);
}
} // namespace wolf2009

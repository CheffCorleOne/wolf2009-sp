// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic fixtures only: no original game content is included.
#include "Archive.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Put32(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
void Pad(Bytes& bytes) { while (bytes.size() & 3u) bytes.push_back(0); }
Bytes Plain() {
    Bytes bytes;
    Put32(bytes, 2);
    const std::string names[2] = {"synthetic/first", "synthetic/empty"};
    for (int i = 0; i < 2; ++i) {
        bytes.insert(bytes.end(), names[i].begin(), names[i].end());
        bytes.push_back(0);
        Put32(bytes, i == 0 ? 3 : 0);
        Put32(bytes, i == 0 ? 3 : 0);
    }
    Pad(bytes);
    bytes.push_back('a'); bytes.push_back('b'); bytes.push_back('c');
    Pad(bytes);
    return bytes;
}
Bytes PackedChunk(const Bytes& plain, const char* type = "TXTR") {
    uLongf capacity = compressBound(uLong(plain.size()));
    Bytes compressed(capacity);
    if (compress2(compressed.data(), &capacity, plain.data(), uLong(plain.size()), Z_BEST_SPEED) != Z_OK)
        throw std::runtime_error("Synthetic fixture compression failed");
    compressed.resize(capacity);
    Bytes bytes;
    for (int i = 3; i >= 0; --i) bytes.push_back(static_cast<unsigned char>(type[i]));
    Put32(bytes, std::uint32_t(plain.size()));
    Put32(bytes, std::uint32_t(compressed.size()));
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
    return bytes;
}
Bytes Container(const Bytes& plain) {
    Bytes bytes;
    Put32(bytes, 300);
    const Bytes chunk = PackedChunk(plain);
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
    return bytes;
}
std::istringstream Input(const Bytes& bytes) {
    return std::istringstream(std::string(bytes.begin(), bytes.end()), std::ios::binary | std::ios::in);
}
void Check(bool condition) { if (!condition) throw std::runtime_error("Assertion failed"); }
void Reject(const Bytes& bytes, const Limits& limits = Limits()) {
    std::istringstream input = Input(bytes);
    try { Archive::Scan(input, limits); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Malformed fixture was accepted");
}
}

int RunSelfTests() {
    int passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& run) {
        try { run(); ++passed; }
        catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    test("valid multi-entry payload and empty read", [] {
        std::istringstream input = Input(Container(Plain()));
        const Archive archive = Archive::Scan(input);
        Check(archive.Chunks().size() == 1 && archive.Chunks()[0].type == "TXTR");
        Check(archive.Chunks()[0].entries.size() == 2);
        Check(archive.ReadEntry(input, 0, 0) == Bytes({'a', 'b', 'c'}));
        Check(archive.ReadEntry(input, 0, 1).empty());
    });
    test("multiple chunks and unknown printable type", [] {
        Bytes bytes = Container(Plain());
        Bytes empty; Put32(empty, 0);
        const Bytes chunk = PackedChunk(empty, "TEST");
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        std::istringstream input = Input(bytes);
        const Archive archive = Archive::Scan(input);
        Check(archive.Chunks().size() == 2 && archive.Chunks()[1].type == "TEST");
    });
    test("empty version-only archive", [] {
        Bytes bytes; Put32(bytes, 300); std::istringstream input = Input(bytes);
        Check(Archive::Scan(input).Chunks().empty());
    });
    test("truncated magic", [] { Reject(Bytes(3, 0)); });
    test("wrong version", [] { Bytes bytes = Container(Plain()); bytes[0] = 0; Reject(bytes); });
    test("partial chunk header", [] { Bytes bytes = Container(Plain()); bytes.resize(15); Reject(bytes); });
    test("partial chunk payload", [] { Bytes bytes = Container(Plain()); bytes.pop_back(); Reject(bytes); });
    test("zlib corruption", [] { Bytes bytes = Container(Plain()); bytes[16] ^= 255; Reject(bytes); });
    test("inflated size mismatch", [] { Bytes bytes = Container(Plain()); ++bytes[8]; Reject(bytes); });
    test("compressed limit", [] { Limits limits; limits.compressedChunkBytes = 1; Reject(Container(Plain()), limits); });
    test("inflated limit", [] { Limits limits; limits.inflatedChunkBytes = 4; Reject(Container(Plain()), limits); });
    test("entry count limit", [] { Limits limits; limits.entries = 1; Reject(Container(Plain()), limits); });
    test("chunk count limit", [] { Limits limits; limits.chunks = 0; Reject(Container(Plain()), limits); });
    test("name limit", [] { Limits limits; limits.nameBytes = 4; Reject(Container(Plain()), limits); });
    test("total name limit", [] { Limits limits; limits.totalNameBytes = 1; Reject(Container(Plain()), limits); });
    test("unbounded entry count", [] { Bytes bytes = Plain(); bytes[0] = 255; Reject(Container(bytes)); });
    test("unterminated name", [] {
        Bytes bytes; Put32(bytes, 1); bytes.insert(bytes.end(), 20, 'x'); Reject(Container(bytes));
    });
    test("mismatched entry size pair", [] {
        Bytes bytes = Plain(); bytes[4 + std::string("synthetic/first").size() + 1 + 4] = 9;
        Reject(Container(bytes));
    });
    test("payload overrun", [] {
        Bytes bytes = Plain(); bytes.resize(bytes.size() - 4); Reject(Container(bytes));
    });
    test("extra inflated bytes", [] {
        Bytes bytes = Plain(); bytes.insert(bytes.end(), 4, 0); Reject(Container(bytes));
    });
    test("compressed stream trailing bytes", [] {
        Bytes bytes = Container(Plain()); ++bytes[12]; bytes.push_back(0); Reject(bytes);
    });
    test("invalid entry index", [] {
        std::istringstream input = Input(Container(Plain())); const Archive archive = Archive::Scan(input);
        bool rejected = false;
        try { archive.ReadEntry(input, 0, 2); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected);
    });
    test("changed stream metadata", [] {
        std::istringstream input = Input(Container(Plain())); const Archive archive = Archive::Scan(input);
        Bytes bytes = Container(Plain()); bytes[4] = 'Z'; std::istringstream changed = Input(bytes);
        bool rejected = false;
        try { archive.ReadEntry(changed, 0, 0); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected);
    });
    std::cout << "wolf2009 archive self-test: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
} // namespace wolf2009

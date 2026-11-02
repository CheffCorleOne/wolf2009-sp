// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ZIP_ARCHIVE_H
#define WOLF2009_ZIP_ARCHIVE_H
#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace wolf2009 {
struct ZipLimits {
    std::uint64_t fileBytes = 1024ULL * 1024 * 1024;
    std::size_t compressedEntryBytes = 64u * 1024u * 1024u;
    std::size_t inflatedEntryBytes = 256u * 1024u * 1024u;
    std::size_t entries = 100000;
    std::size_t nameBytes = 4096;
    std::size_t totalNameBytes = 64u * 1024u * 1024u;
};
struct ZipEntry {
    // Original virtual name bytes. The reader never uses these as host paths.
    std::string name;
    std::uint32_t compressedSize = 0, size = 0, crc = 0;
    std::uint16_t method = 0, flags = 0;
    std::uint64_t offset = 0; // Compressed payload's absolute stream offset.
    std::uint64_t localHeaderOffset = 0;
};
class ZipArchive {
public:
    // Structural indexing only; validates central/local headers, ranges and
    // descriptors. Supports coherent single-disk ZIP32 and Wolf2009 SP PK4.
    // Encryption, ZIP64, duplicate names and unsupported methods are rejected.
    static ZipArchive Scan(std::istream& input, const ZipLimits& limits = ZipLimits());
    const std::vector<ZipEntry>& Entries() const { return entries_; }
    // Caller owns the seekable stream. Rechecks indexed metadata, fully reads
    // one stored/deflated member and verifies exact stream size and CRC32.
    std::vector<unsigned char> ReadEntry(std::istream& input, std::size_t index) const;
private:
    ZipLimits limits_;
    std::uint64_t fileSize_ = 0, footerOffset_ = 0, centralOffset_ = 0;
    std::vector<unsigned char> footer_;
    std::vector<ZipEntry> entries_;
    std::vector<std::uint64_t> centralEntries_;
};
int RunZipTests();
}
#endif

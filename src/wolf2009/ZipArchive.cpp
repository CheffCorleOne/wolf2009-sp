// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZipArchive.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 { namespace {
typedef std::vector<unsigned char> Bytes;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::uint16_t U16(const unsigned char* p) { return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
std::uint32_t U32(const unsigned char* p) { return std::uint32_t(U16(p)) | (std::uint32_t(U16(p + 2)) << 16); }
void Span(std::uint64_t at, std::uint64_t bytes, std::uint64_t end, const char* message) {
    Check(at <= end && bytes <= end - at, message);
}
void Seek(std::istream& input, std::uint64_t offset) {
    Check(offset <= std::uint64_t((std::numeric_limits<std::streamoff>::max)()), "ZIP offset exceeds host stream range");
    input.clear(); input.seekg(std::streamoff(offset), std::ios::beg);
    Check(bool(input), "ZIP stream seek failed");
}
void Read(std::istream& input, unsigned char* data, std::size_t bytes) {
    Check(bytes <= std::size_t((std::numeric_limits<std::streamsize>::max)()), "ZIP read exceeds host stream range");
    if (bytes) input.read(reinterpret_cast<char*>(data), std::streamsize(bytes));
    Check(bool(input), "Truncated ZIP data or stream read failure");
}
Bytes At(std::istream& input, std::uint64_t offset, std::size_t bytes) {
    Seek(input, offset); Bytes result(bytes); Read(input, result.data(), bytes); return result;
}
std::uint64_t Length(std::istream& input, const ZipLimits& limits) {
    input.clear(); input.seekg(0, std::ios::end); const std::streamoff size = input.tellg();
    Check(size >= 22 && std::uint64_t(size) <= limits.fileBytes, "ZIP source size exceeds limit or is too small/not seekable");
    return std::uint64_t(size);
}
struct EndRecord { std::uint64_t position, centralOffset, centralBytes; std::size_t entries; Bytes bytes; };
EndRecord Footer(std::istream& input, std::uint64_t fileSize, const ZipLimits& limits) {
    const std::size_t tailBytes = std::size_t((std::min)(fileSize, std::uint64_t(22 + 65535)));
    const std::uint64_t tailOffset = fileSize - tailBytes;
    const Bytes tail = At(input, tailOffset, tailBytes);
    bool zip64 = false, multiDisk = false;
    for (std::size_t i = tail.size() - 22;; --i) {
        const unsigned char* p = tail.data() + i;
        const std::uint32_t signature = U32(p);
        if ((signature == 0x06054b50U || signature == 0x48530a02U) &&
            std::uint64_t(i) + 22 + U16(p + 20) == tail.size()) {
            const std::size_t count = U16(p + 10);
            const std::uint64_t centralBytes = U32(p + 12), centralOffset = U32(p + 16), position = tailOffset + i;
            if (count == 0xffff || U16(p + 8) == 0xffff || centralBytes == 0xffffffffU || centralOffset == 0xffffffffU) zip64 = true;
            else if (U16(p + 4) || U16(p + 6)) multiDisk = true;
            else if (U16(p + 8) == count && centralOffset <= position && centralBytes == position - centralOffset &&
                     centralBytes >= std::uint64_t(count) * 46 && (count || centralBytes == 0)) {
                Check(count <= limits.entries, "ZIP entry count exceeds configured limit");
                EndRecord record = {position, centralOffset, centralBytes, count, Bytes(p, p + 22)}; return record;
            }
        }
        if (i == 0) break;
    }
    Check(!zip64, "ZIP64 archives are unsupported");
    Check(!multiDisk, "Multi-disk ZIP archives are unsupported");
    Check(false, "Missing or incoherent ZIP32 end record; ZIP64 is unsupported");
    EndRecord unreachable = {}; return unreachable;
}
void Features(std::uint16_t version, std::uint16_t flags, std::uint16_t method) {
    Check(!(flags & (1u | 64u | 8192u)), "Encrypted ZIP entries are unsupported");
    Check((flags & ~std::uint16_t(0x080e)) == 0, "Unsupported ZIP entry flags");
    Check(method == 0 || method == 8, "Only stored and deflated ZIP entries are supported");
    Check(version >= 10 && version <= 20 && (method != 8 || version >= 20), "ZIP64 or unsupported ZIP extraction version");
    // Some original PK4 stored members retain compression-option bits 1/2.
    // Those bits do not change the stored-byte interpretation.
}
void Extra(const Bytes& extra) {
    std::size_t position = 0;
    while (position < extra.size()) {
        Check(extra.size() - position >= 4, "Truncated ZIP extra-field header");
        const std::uint16_t id = U16(extra.data() + position), size = U16(extra.data() + position + 2);
        position += 4;
        Check(size <= extra.size() - position, "ZIP extra-field span exceeds header");
        Check(id != 1, "ZIP64 extra fields are unsupported");
        position += size;
    }
}
struct CentralRecord { ZipEntry entry; std::uint64_t next; std::uint16_t version; };
CentralRecord Central(std::istream& input, std::uint64_t position, std::uint64_t centralEnd, const ZipLimits& limits) {
    Span(position, 46, centralEnd, "Truncated ZIP central-directory header");
    const Bytes header = At(input, position, 46); const unsigned char* p = header.data();
    Check(U32(p) == 0x02014b50U, "Invalid ZIP central-directory signature");
    CentralRecord record;
    record.version = U16(p + 6); record.entry.flags = U16(p + 8); record.entry.method = U16(p + 10);
    Features(record.version, record.entry.flags, record.entry.method);
    record.entry.crc = U32(p + 16); record.entry.compressedSize = U32(p + 20); record.entry.size = U32(p + 24);
    record.entry.localHeaderOffset = U32(p + 42);
    Check(record.entry.compressedSize != 0xffffffffU && record.entry.size != 0xffffffffU &&
          record.entry.localHeaderOffset != 0xffffffffU, "ZIP64 entry sizes/offsets are unsupported");
    Check(U16(p + 34) == 0, "Multi-disk ZIP member is unsupported");
    Check(record.entry.compressedSize <= limits.compressedEntryBytes && record.entry.size <= limits.inflatedEntryBytes,
          "ZIP entry sizes exceed configured limits");
    Check(record.entry.method != 0 || record.entry.compressedSize == record.entry.size, "Stored ZIP entry size mismatch");
    Check(record.entry.method != 8 || record.entry.compressedSize != 0, "Empty deflate byte stream is invalid");
    const std::size_t nameBytes = U16(p + 28), extraBytes = U16(p + 30), commentBytes = U16(p + 32);
    Check(nameBytes != 0 && nameBytes <= limits.nameBytes, "Empty or excessive ZIP member name");
    const std::uint64_t variableBytes = nameBytes + extraBytes + commentBytes;
    Span(position + 46, variableBytes, centralEnd, "ZIP central entry variable data exceeds directory");
    const Bytes name = At(input, position + 46, nameBytes);
    Check(std::find(name.begin(), name.end(), 0) == name.end(), "NUL byte in ZIP member name");
    record.entry.name.assign(name.begin(), name.end());
    Extra(At(input, position + 46 + nameBytes, extraBytes));
    record.next = position + 46 + variableBytes;
    return record;
}
std::uint64_t Local(std::istream& input, ZipEntry& entry, std::uint16_t version, std::uint64_t centralOffset) {
    Span(entry.localHeaderOffset, 30, centralOffset, "ZIP local header overlaps directory or is truncated");
    const Bytes header = At(input, entry.localHeaderOffset, 30); const unsigned char* p = header.data();
    Check(U32(p) == 0x04034b50U, "Invalid ZIP local-header signature");
    Check(U16(p + 4) == version && U16(p + 6) == entry.flags && U16(p + 8) == entry.method,
          "ZIP local/central extraction version, flags or method conflict");
    const std::uint32_t crc = U32(p + 14), compressed = U32(p + 18), size = U32(p + 22);
    Check(compressed != 0xffffffffU && size != 0xffffffffU, "ZIP64 local entry sizes are unsupported");
    if (entry.flags & 8) {
        Check((crc == 0 || crc == entry.crc) && (compressed == 0 || compressed == entry.compressedSize) &&
              (size == 0 || size == entry.size), "Conflicting ZIP local descriptor placeholders");
    } else Check(crc == entry.crc && compressed == entry.compressedSize && size == entry.size,
                 "ZIP local/central CRC or sizes conflict");
    const std::size_t nameBytes = U16(p + 26), extraBytes = U16(p + 28);
    Check(nameBytes == entry.name.size(), "ZIP local/central name length conflict");
    Span(entry.localHeaderOffset + 30, nameBytes + extraBytes, centralOffset, "ZIP local variable data overlaps directory");
    const Bytes name = At(input, entry.localHeaderOffset + 30, nameBytes);
    Check(std::memcmp(name.data(), entry.name.data(), name.size()) == 0, "ZIP local/central member names conflict");
    Extra(At(input, entry.localHeaderOffset + 30 + nameBytes, extraBytes));
    entry.offset = entry.localHeaderOffset + 30 + nameBytes + extraBytes;
    Span(entry.offset, entry.compressedSize, centralOffset, "ZIP payload overlaps directory");
    const std::uint64_t end = entry.offset + entry.compressedSize;
    if (!(entry.flags & 8)) return end;
    Span(end, 12, centralOffset, "Truncated ZIP data descriptor");
    const std::size_t bytes = std::size_t((std::min)(std::uint64_t(16), centralOffset - end));
    const Bytes descriptor = At(input, end, bytes); p = descriptor.data();
    const bool signature = descriptor.size() >= 16 && U32(p) == 0x08074b50U && U32(p + 4) == entry.crc &&
                           U32(p + 8) == entry.compressedSize && U32(p + 12) == entry.size;
    const bool plain = U32(p) == entry.crc && U32(p + 4) == entry.compressedSize && U32(p + 8) == entry.size;
    Check(signature || plain, "ZIP data descriptor conflicts with central entry");
    return end + (signature ? 16 : 12);
}
bool Same(const ZipEntry& a, const ZipEntry& b) {
    return a.name == b.name && a.compressedSize == b.compressedSize && a.size == b.size && a.crc == b.crc &&
           a.method == b.method && a.flags == b.flags && a.offset == b.offset && a.localHeaderOffset == b.localHeaderOffset;
}
Bytes Contents(std::istream& input, const ZipEntry& entry) {
    Check(entry.compressedSize <= (std::numeric_limits<uInt>::max)() && entry.size <= (std::numeric_limits<uInt>::max)(),
          "ZIP member sizes exceed decompressor integer range");
    Bytes compressed = At(input, entry.offset, entry.compressedSize), result;
    if (entry.method == 0) result.swap(compressed);
    else {
        result.resize(entry.size); unsigned char emptyOutput = 0;
        z_stream state; std::memset(&state, 0, sizeof(state));
        state.next_in = compressed.data(); state.avail_in = uInt(compressed.size());
        state.next_out = result.empty() ? &emptyOutput : result.data(); state.avail_out = result.empty() ? 1 : uInt(result.size());
        Check(inflateInit2(&state, -MAX_WBITS) == Z_OK, "Could not initialize raw ZIP decompression");
        const int status = inflate(&state, Z_FINISH);
        const bool complete = status == Z_STREAM_END && state.total_in == compressed.size() && state.total_out == result.size();
        inflateEnd(&state);
        Check(complete, "ZIP deflate stream invalid, size mismatch or trailing compressed bytes");
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    if (!result.empty()) crc = crc32(crc, result.data(), uInt(result.size()));
    Check(std::uint32_t(crc) == entry.crc, "ZIP member CRC32 mismatch");
    return result;
}
} // namespace

ZipArchive ZipArchive::Scan(std::istream& input, const ZipLimits& limits) {
    ZipArchive archive; archive.limits_ = limits; archive.fileSize_ = Length(input, limits);
    const EndRecord footer = Footer(input, archive.fileSize_, limits);
    archive.footerOffset_ = footer.position; archive.centralOffset_ = footer.centralOffset; archive.footer_ = footer.bytes;
    archive.entries_.reserve(footer.entries); archive.centralEntries_.reserve(footer.entries);
    std::uint64_t position = footer.centralOffset;
    std::size_t names = 0;
    std::set<std::string> distinctNames;
    std::vector<std::pair<std::uint64_t, std::uint64_t> > ranges; ranges.reserve(footer.entries);
    for (std::size_t i = 0; i < footer.entries; ++i) {
        CentralRecord central = Central(input, position, footer.position, limits);
        Check(names <= limits.totalNameBytes && central.entry.name.size() <= limits.totalNameBytes - names,
              "ZIP total member-name bytes exceed configured limit");
        names += central.entry.name.size();
        Check(distinctNames.insert(central.entry.name).second, "Duplicate ZIP member names are unsupported");
        const std::uint64_t end = Local(input, central.entry, central.version, footer.centralOffset);
        ranges.push_back(std::make_pair(central.entry.localHeaderOffset, end));
        archive.centralEntries_.push_back(position); archive.entries_.push_back(std::move(central.entry));
        position = central.next;
    }
    Check(position == footer.position, "ZIP central-directory byte/count mismatch");
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i)
        Check(ranges[i].first >= ranges[i - 1].second, "Overlapping ZIP local entries");
    return archive;
}

std::vector<unsigned char> ZipArchive::ReadEntry(std::istream& input, std::size_t index) const {
    Check(index < entries_.size(), "ZIP member index out of range");
    Check(Length(input, limits_) == fileSize_, "ZIP source size changed since indexing");
    Check(At(input, footerOffset_, footer_.size()) == footer_, "ZIP footer changed since indexing");
    CentralRecord current = Central(input, centralEntries_[index], footerOffset_, limits_);
    Local(input, current.entry, current.version, centralOffset_);
    Check(Same(entries_[index], current.entry), "ZIP member metadata changed since indexing");
    return Contents(input, current.entry);
}
}

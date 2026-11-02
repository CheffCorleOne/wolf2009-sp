// SPDX-License-Identifier: GPL-3.0-or-later
// Shared bounded reader for the observed little-endian 32-bit Havok 5.6.0-r1 packfiles.
#ifndef WOLF2009_HAVOK_PACK_H
#define WOLF2009_HAVOK_PACK_H
#include <cstdint>
#include <cstring>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace wolf2009 {
namespace detail {
class HavokPack {
    struct Section {
        std::uint32_t absolute, local, global, virtuals, exports, imports, end;
    };
    const std::size_t maxSourceBytes, maxObjects, maxFixups;
    std::size_t fixupCount = 0;
    static void Check(bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    }

  public:
    const std::vector<unsigned char> &bytes;
    Section sections[3];
    std::map<std::uint32_t, std::uint32_t> pointers;
    std::map<std::uint32_t, std::string> objects;
    HavokPack(const std::vector<unsigned char> &data, std::size_t sourceBytes, std::size_t objectLimit,
              std::size_t fixupLimit)
        : maxSourceBytes(sourceBytes), maxObjects(objectLimit), maxFixups(fixupLimit), bytes(data) {
        Header();
    }
    std::string DataString(std::uint32_t offset) const {
        DataRange(offset, 1);
        return String(sections[2].absolute + offset,
                      sections[2].absolute + std::size_t(sections[2].local));
    }
    void Range(std::size_t pos, std::size_t size) const {
        Check(pos <= bytes.size() && size <= bytes.size() - pos,
              "Havok packfile file range is truncated");
    }
    void DataRange(std::size_t pos, std::size_t size) const {
        Check(pos <= sections[2].local && size <= sections[2].local - pos,
              "Havok packfile data range exceeds section");
    }
    void ArrayRange(std::size_t pos, std::size_t count, std::size_t stride) const {
        Check(stride && count <= sections[2].local / stride,
              "Havok packfile array byte size exceeds data section");
        DataRange(pos, count * stride);
    }
    std::uint32_t U(std::size_t pos) const {
        Range(pos, 4);
        return std::uint32_t(bytes[pos]) | (std::uint32_t(bytes[pos + 1]) << 8) |
               (std::uint32_t(bytes[pos + 2]) << 16) | (std::uint32_t(bytes[pos + 3]) << 24);
    }
    std::uint32_t DU(std::size_t pos) const {
        DataRange(pos, 4);
        return U(sections[2].absolute + pos);
    }
    double F(std::size_t pos) const {
        const std::uint32_t bits = DU(pos);
        float value;
        static_assert(sizeof(float) == 4, "IEEE32 float required");
        std::memcpy(&value, &bits, 4);
        Check(std::isfinite(value), "Non-finite packfile float");
        return value;
    }
    std::string String(std::size_t absolute, std::size_t end) const {
        Check(absolute < end && end <= bytes.size(), "Havok packfile string range invalid");
        std::string text;
        while (absolute < end && bytes[absolute]) {
            Check(text.size() < 4096, "Havok packfile string limit exceeded");
            Check(bytes[absolute] >= 32 && bytes[absolute] <= 126,
                  "Non-printable Havok packfile type/name string");
            text += char(bytes[absolute++]);
        }
        Check(absolute < end, "Unterminated Havok packfile string");
        return text;
    }
    std::uint32_t Pointer(std::size_t source) const {
        DataRange(source, 4);
        const std::map<std::uint32_t, std::uint32_t>::const_iterator i =
            pointers.find(static_cast<std::uint32_t>(source));
        Check(i != pointers.end(), "Required Havok packfile pointer fixup missing");
        return i->second;
    }
    std::string Type(std::uint32_t object) const {
        const std::map<std::uint32_t, std::string>::const_iterator i = objects.find(object);
        return i == objects.end() ? "untyped-shape" : i->second;
    }
  private:
    void Padding(std::size_t begin, std::size_t end) const {
        for (std::size_t p = begin; p < end; ++p)
            Check(bytes[p] == 255, "Unexpected Havok packfile fixup padding");
    }
    void Fixups(std::uint32_t begin, std::uint32_t end, bool global) {
        const std::size_t stride = global ? 12 : 8, absolute = sections[2].absolute;
        std::size_t p = absolute + begin, stop = absolute + end;
        while (p + stride <= stop) {
            const std::uint32_t source = U(p);
            if (source == 0xffffffffu) {
                Padding(p, stop);
                return;
            }
            Check(++fixupCount <= maxFixups, "Havok packfile fixup count limit exceeded");
            DataRange(source, 4);
            const std::uint32_t section = global ? U(p + 4) : 2, destination = U(p + (global ? 8 : 4));
            Check(section < 3 && destination < sections[section].local,
                  "Havok packfile pointer destination outside section");
            // Consumers follow only links into the data section.
            if (section == 2)
                Check(pointers.insert(std::make_pair(source, destination)).second,
                      "Duplicate/ambiguous Havok packfile pointer source");
            p += stride;
        }
        Padding(p, stop);
    }
    void Header() {
        Check(bytes.size() >= 208 && bytes.size() <= maxSourceBytes,
              "Havok packfile byte limit/header size invalid");
        Check(U(0) == 0x57e0e057 && U(4) == 0x10c0c010, "Expected Havok packfile magic");
        Check(U(12) == 5 && bytes[16] == 4 && bytes[17] == 1 && bytes[18] == 0 && bytes[19] == 1,
              "Unsupported Havok packfile version/layout profile");
        Check(String(40, 56) == "Havok-5.6.0-r1" && U(20) == 3,
              "Unsupported Havok contents version or section count");
        const char *names[3] = {"__classnames__", "__types__", "__data__"};
        for (std::size_t i = 0; i < 3; ++i) {
            const std::size_t h = 64 + i * 48;
            Check(String(h, h + 20) == names[i], "Unexpected Havok packfile section order/name");
            Section &s = sections[i];
            s.absolute = U(h + 20);
            s.local = U(h + 24);
            s.global = U(h + 28);
            s.virtuals = U(h + 32);
            s.exports = U(h + 36);
            s.imports = U(h + 40);
            s.end = U(h + 44);
            Check(s.local <= s.global && s.global <= s.virtuals && s.virtuals <= s.exports &&
                      s.exports <= s.imports && s.imports <= s.end,
                  "Havok packfile section offsets are not monotonic");
            Range(s.absolute, s.end);
        }
        Check(sections[0].absolute >= 208 &&
                  sections[0].absolute + std::size_t(sections[0].end) <= sections[2].absolute &&
                  sections[1].end == 0,
              "Unsupported overlapping/reflection Havok packfile sections");
        Check(sections[2].absolute + std::size_t(sections[2].end) == bytes.size() &&
                  sections[2].exports == sections[2].imports && sections[2].imports == sections[2].end,
              "Havok packfile imports/exports/trailing data unsupported");
        Fixups(sections[2].local, sections[2].global, false);
        Fixups(sections[2].global, sections[2].virtuals, true);
        std::size_t p = sections[2].absolute + std::size_t(sections[2].virtuals),
                    end = sections[2].absolute + std::size_t(sections[2].exports);
        while (p + 12 <= end) {
            const std::uint32_t object = U(p);
            if (object == 0xffffffffu) {
                Padding(p, end);
                break;
            }
            Check(objects.size() < maxObjects, "Havok packfile typed object limit exceeded");
            DataRange(object, 4);
            const std::uint32_t section = U(p + 4), name = U(p + 8);
            Check(section == 0 && name < sections[0].local,
                  "Havok packfile class fixup is outside class-name section");
            const std::string type = String(sections[0].absolute + name,
                                            sections[0].absolute + std::size_t(sections[0].local));
            Check(!type.empty() && type.size() <= 256, "Havok packfile class-name size limit exceeded");
            Check(objects.insert(std::make_pair(object, type)).second,
                  "Duplicate Havok packfile typed object");
            p += 12;
        }
        Padding(p, end);
        Check(U(24) == 2 && U(32) == 0 && Type(U(28)) == "hkRootLevelContainer" &&
                  U(36) < sections[0].local,
              "Unsupported Havok packfile root container");
        Check(String(sections[0].absolute + U(36),
                     sections[0].absolute + std::size_t(sections[0].local)) == "hkRootLevelContainer",
              "Havok packfile root class-name contradicts object type");
    }
};
} // namespace detail
} // namespace wolf2009
#endif

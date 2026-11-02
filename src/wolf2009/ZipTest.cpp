// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZipArchive.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wolf2009 { namespace {
typedef std::vector<unsigned char> Bytes;
void Assert(bool condition) { if (!condition) throw std::runtime_error("Assertion failed"); }
void Set16(Bytes& b, std::size_t at, std::uint16_t v) {
    Assert(at + 2 <= b.size()); b[at] = static_cast<unsigned char>(v); b[at + 1] = static_cast<unsigned char>(v >> 8);
}
void Set32(Bytes& b, std::size_t at, std::uint32_t v) {
    Assert(at + 4 <= b.size()); for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<unsigned char>(v >> (i * 8));
}
void Add32(Bytes& b, std::uint32_t v) { const std::size_t at = b.size(); b.resize(at + 4); Set32(b, at, v); }
std::uint32_t Crc(const Bytes& b) { return std::uint32_t(crc32(crc32(0L, Z_NULL, 0), b.empty() ? Z_NULL : b.data(), uInt(b.size()))); }
Bytes RawDeflate(const Bytes& data) {
    Bytes output(data.size() + 128);
    z_stream state; std::memset(&state, 0, sizeof(state));
    state.next_in = data.empty() ? Z_NULL : const_cast<unsigned char*>(data.data()); state.avail_in = uInt(data.size());
    state.next_out = output.data(); state.avail_out = uInt(output.size());
    Assert(deflateInit2(&state, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    const int status = deflate(&state, Z_FINISH); const std::size_t bytes = state.total_out;
    deflateEnd(&state); Assert(status == Z_STREAM_END); output.resize(bytes); return output;
}
struct Spec {
    std::string name;
    Bytes data, extra;
    std::uint16_t method = 0;
    unsigned descriptor = 0; // 0 absent, 1 without signature, 2 with signature.
    bool utf8 = false;
    explicit Spec(const std::string& filename = "synthetic/test.txt", const std::string& value = "synthetic bytes\n")
        : name(filename), data(value.begin(), value.end()) {}
};
struct Fixture {
    Bytes data;
    std::vector<std::size_t> locals, centrals, payloads, descriptors;
    std::size_t central, footer;
};
Fixture Build(bool wolf, const std::vector<Spec>& specs, std::size_t comment = 0) {
    Fixture f;
    std::vector<Bytes> compressed;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const Spec& s = specs[i]; compressed.push_back(s.method == 8 ? RawDeflate(s.data) : s.data);
        const Bytes& packed = compressed.back();
        const std::uint16_t flags = (s.descriptor ? 8 : 0) | (s.utf8 ? 2048 : 0), version = s.method == 8 ? 20 : 10;
        const std::size_t at = f.data.size(); f.locals.push_back(at); f.data.resize(at + 30);
        Set32(f.data, at, 0x04034b50U); Set16(f.data, at + 4, version); Set16(f.data, at + 6, flags); Set16(f.data, at + 8, s.method);
        if (!s.descriptor) { Set32(f.data, at + 14, Crc(s.data)); Set32(f.data, at + 18, std::uint32_t(packed.size())); Set32(f.data, at + 22, std::uint32_t(s.data.size())); }
        Set16(f.data, at + 26, std::uint16_t(s.name.size())); Set16(f.data, at + 28, std::uint16_t(s.extra.size()));
        f.data.insert(f.data.end(), s.name.begin(), s.name.end()); f.data.insert(f.data.end(), s.extra.begin(), s.extra.end());
        f.payloads.push_back(f.data.size()); f.data.insert(f.data.end(), packed.begin(), packed.end());
        f.descriptors.push_back(f.data.size());
        if (s.descriptor) {
            if (s.descriptor == 2) Add32(f.data, 0x08074b50U);
            Add32(f.data, Crc(s.data)); Add32(f.data, std::uint32_t(packed.size())); Add32(f.data, std::uint32_t(s.data.size()));
        }
    }
    f.central = f.data.size();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const Spec& s = specs[i]; const Bytes& packed = compressed[i]; const std::size_t at = f.data.size();
        f.centrals.push_back(at); f.data.resize(at + 46);
        Set32(f.data, at, 0x02014b50U); Set16(f.data, at + 4, 20); Set16(f.data, at + 6, s.method == 8 ? 20 : 10);
        Set16(f.data, at + 8, (s.descriptor ? 8 : 0) | (s.utf8 ? 2048 : 0)); Set16(f.data, at + 10, s.method);
        Set32(f.data, at + 16, Crc(s.data)); Set32(f.data, at + 20, std::uint32_t(packed.size())); Set32(f.data, at + 24, std::uint32_t(s.data.size()));
        Set16(f.data, at + 28, std::uint16_t(s.name.size())); Set16(f.data, at + 30, std::uint16_t(s.extra.size()));
        Set32(f.data, at + 42, std::uint32_t(f.locals[i])); f.data.insert(f.data.end(), s.name.begin(), s.name.end()); f.data.insert(f.data.end(), s.extra.begin(), s.extra.end());
    }
    f.footer = f.data.size(); f.data.resize(f.footer + 22 + comment, 'x');
    for (std::size_t i = f.footer; i < f.footer + 22; ++i) f.data[i] = 0;
    Set32(f.data, f.footer, wolf ? 0x48530a02U : 0x06054b50U);
    Set16(f.data, f.footer + 8, std::uint16_t(specs.size())); Set16(f.data, f.footer + 10, std::uint16_t(specs.size()));
    Set32(f.data, f.footer + 12, std::uint32_t(f.footer - f.central)); Set32(f.data, f.footer + 16, std::uint32_t(f.central)); Set16(f.data, f.footer + 20, std::uint16_t(comment));
    return f;
}
std::stringstream Stream(const Bytes& data) { return std::stringstream(std::string(data.begin(), data.end()), std::ios::in | std::ios::out | std::ios::binary); }
ZipArchive Scan(const Bytes& data, const ZipLimits& limits = ZipLimits()) { std::stringstream input = Stream(data); return ZipArchive::Scan(input, limits); }
void Reject(const std::function<void()>& operation) {
    bool rejected = false; try { operation(); } catch (const std::exception&) { rejected = true; } Assert(rejected);
}
void Roundtrip(bool wolf, std::uint16_t method, std::size_t comment, unsigned descriptor = 0) {
    Spec s; s.method = method; s.descriptor = descriptor;
    const Fixture f = Build(wolf, std::vector<Spec>{s, Spec("synthetic/directory/", "")}, comment);
    std::stringstream input = Stream(f.data); const ZipArchive archive = ZipArchive::Scan(input);
    Assert(archive.Entries().size() == 2 && archive.Entries()[0].name == s.name && archive.Entries()[0].offset == f.payloads[0]);
    Assert(archive.ReadEntry(input, 0) == s.data && archive.ReadEntry(input, 1).empty());
}
} // namespace

int RunZipTests() {
    unsigned passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& fn) {
        try { fn(); ++passed; } catch (const std::exception& e) { ++failed; std::cerr << "zip test " << name << ": " << e.what() << '\n'; }
    };
    test("standard/Wolf stored and deflated roundtrips", [] {
        for (unsigned wolf = 0; wolf < 2; ++wolf) for (unsigned method = 0; method < 2; ++method) Roundtrip(wolf != 0, method ? 8 : 0, 0);
    });
    test("comments including former search-buffer boundaries and max ZIP32 comment", [] {
        const std::size_t sizes[] = {1, 1010, 1024, 2047, 65535};
        for (unsigned wolf = 0; wolf < 2; ++wolf) for (std::size_t i = 0; i < 5; ++i) Roundtrip(wolf != 0, 8, sizes[i]);
    });
    test("signed/unsigned data descriptors", [] {
        for (unsigned wolf = 0; wolf < 2; ++wolf) for (unsigned method = 0; method < 2; ++method)
            for (unsigned descriptor = 1; descriptor < 3; ++descriptor) Roundtrip(wolf != 0, method ? 8 : 0, 0, descriptor);
    });
    test("empty archives and empty deflated member", [] {
        Assert(Scan(Build(true, {}).data).Entries().empty()); Assert(Scan(Build(false, {}, 50).data).Entries().empty());
        Spec s("synthetic/empty.bin", ""); s.method = 8; Fixture f = Build(true, {s}); std::stringstream in = Stream(f.data);
        const ZipArchive archive = ZipArchive::Scan(in); Assert(archive.ReadEntry(in, 0).empty());
    });
    test("UTF8 name bytes, virtual names and harmless extra fields", [] {
        Spec s("../virtual/\xc3\xa9.txt"); s.utf8 = true; s.extra = Bytes{0xfe,0xca,3,0,1,2,3};
        Fixture f = Build(true, {s}); std::stringstream in = Stream(f.data); const ZipArchive archive = ZipArchive::Scan(in);
        Assert(archive.Entries()[0].name == s.name && archive.ReadEntry(in, 0) == s.data);
    });
    test("original PK4 stored compression-option flags", [] {
        Fixture f = Build(true, {Spec()}); Set16(f.data, f.central + 8, 2); Set16(f.data, 6, 2);
        std::stringstream in = Stream(f.data); const ZipArchive archive = ZipArchive::Scan(in);
        Assert(archive.ReadEntry(in, 0) == Spec().data);
    });
    test("empty and NUL-containing names rejected", [] {
        Reject([] { Scan(Build(true, {Spec("", "data")}).data); });
        Reject([] { Scan(Build(true, {Spec(std::string("a\0b", 3), "data")}).data); });
    });
    test("false footer signature in comment does not replace coherent footer", [] {
        Fixture f = Build(true, {Spec()}, 40); const std::size_t fake = f.data.size() - 22;
        for (std::size_t i = fake; i < f.data.size(); ++i) f.data[i] = 0; Set32(f.data, fake, 0x06054b50U);
        Assert(Scan(f.data).Entries().size() == 1);
    });
    test("truncated and trailing source", [] {
        Reject([] { Scan(Bytes()); }); Fixture f = Build(true, {Spec()}); f.data.pop_back(); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); f.data.push_back(0); Reject([&] { Scan(f.data); });
    });
    test("coherent footer and central byte counts", [] {
        Fixture f = Build(true, {Spec()}); Set32(f.data, f.footer + 16, 0); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set16(f.data, f.footer + 10, 2); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set32(f.data, f.central, 0); Reject([&] { Scan(f.data); });
    });
    test("multi-disk and ZIP64 unsupported", [] {
        Fixture f = Build(false, {Spec()}); Set16(f.data, f.footer + 4, 1); Reject([&] { Scan(f.data); });
        f = Build(false, {Spec()}); Set16(f.data, f.footer + 8, 0xffff); Set16(f.data, f.footer + 10, 0xffff); Reject([&] { Scan(f.data); });
        f = Build(false, {Spec()}); Set32(f.data, f.central + 24, 0xffffffffU); Reject([&] { Scan(f.data); });
        Spec s; s.extra = Bytes{1,0,0,0}; Reject([&] { Scan(Build(false, {s}).data); });
    });
    test("unsupported encryption, flags and methods", [] {
        Fixture f = Build(true, {Spec()}); Set16(f.data, f.central + 8, 1); Set16(f.data, 6, 1); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set16(f.data, f.central + 8, 32); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set16(f.data, f.central + 10, 12); Set16(f.data, 8, 12); Reject([&] { Scan(f.data); });
    });
    test("local name, method, flags and CRC conflicts", [] {
        Fixture f = Build(true, {Spec()}); f.data[30] ^= 1; Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set16(f.data, 8, 8); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set16(f.data, 6, 8); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set32(f.data, 14, 1); Reject([&] { Scan(f.data); });
    });
    test("duplicate names and conflicting local entry offsets", [] {
        Reject([] { Scan(Build(true, {Spec(), Spec()}).data); });
        Fixture f = Build(true, {Spec("synthetic/a"), Spec("synthetic/b")}); Set32(f.data, f.centrals[1] + 42, 0); Reject([&] { Scan(f.data); });
    });
    test("overlapping local header/payload spans", [] {
        Spec outer("outer", std::string(128, 'x')), inner("inner", "inside"); Fixture f = Build(true, {outer, inner});
        const Bytes innerLocal(f.data.begin() + f.locals[1], f.data.begin() + f.central);
        std::copy(innerLocal.begin(), innerLocal.end(), f.data.begin() + f.payloads[0]);
        Set32(f.data, f.centrals[1] + 42, std::uint32_t(f.payloads[0])); Reject([&] { Scan(f.data); });
    });
    test("payload and variable header bounds", [] {
        Fixture f = Build(true, {Spec()}); Set32(f.data, f.central + 42, std::uint32_t(f.central)); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set16(f.data, f.central + 28, 4096); Reject([&] { Scan(f.data); });
        f = Build(true, {Spec()}); Set16(f.data, 28, 65535); Reject([&] { Scan(f.data); });
        Spec s; s.extra = Bytes{7,0,255,255}; Reject([&] { Scan(Build(true, {s}).data); });
    });
    test("descriptor conflicts", [] {
        Spec s; s.descriptor = 2; Fixture f = Build(true, {s}); Set32(f.data, f.descriptors[0] + 4, 0); Reject([&] { Scan(f.data); });
        f = Build(true, {s}); Set32(f.data, 18, 1); Reject([&] { Scan(f.data); });
    });
    test("stored CRC corruption", [] {
        Fixture f = Build(true, {Spec()}); f.data[f.payloads[0]] ^= 1; std::stringstream in = Stream(f.data);
        const ZipArchive archive = ZipArchive::Scan(in); Reject([&] { archive.ReadEntry(in, 0); });
    });
    test("deflate corruption and exact size", [] {
        Spec s; s.method = 8; Fixture f = Build(false, {s}); f.data[f.payloads[0]] ^= 255; std::stringstream in = Stream(f.data);
        const ZipArchive archive = ZipArchive::Scan(in); Reject([&] { archive.ReadEntry(in, 0); });
        f = Build(false, {s}); Set32(f.data, 22, std::uint32_t(s.data.size() + 1)); Set32(f.data, f.central + 24, std::uint32_t(s.data.size() + 1));
        std::stringstream other = Stream(f.data); const ZipArchive wrong = ZipArchive::Scan(other); Reject([&] { wrong.ReadEntry(other, 0); });
    });
    test("trailing compressed bytes", [] {
        Spec s; s.method = 8; Fixture f = Build(false, {s}); f.data.insert(f.data.begin() + f.central, 0);
        ++f.central; ++f.footer; Set32(f.data, 18, std::uint32_t(f.central - f.payloads[0]));
        Set32(f.data, f.central + 20, std::uint32_t(f.central - f.payloads[0])); Set32(f.data, f.footer + 16, std::uint32_t(f.central));
        std::stringstream in = Stream(f.data); const ZipArchive archive = ZipArchive::Scan(in); Reject([&] { archive.ReadEntry(in, 0); });
    });
    test("metadata/source changed since indexing and bad index", [] {
        Fixture f = Build(true, {Spec()}); std::stringstream in = Stream(f.data); const ZipArchive archive = ZipArchive::Scan(in);
        Reject([&] { archive.ReadEntry(in, 1); }); f.data[f.central + 16] ^= 1; in.str(std::string(f.data.begin(), f.data.end())); Reject([&] { archive.ReadEntry(in, 0); });
        f = Build(true, {Spec()}); f.data.push_back(0); in.str(std::string(f.data.begin(), f.data.end())); Reject([&] { archive.ReadEntry(in, 0); });
    });
    test("resource limits", [] {
        const Bytes b = Build(true, {Spec()}).data; ZipLimits l; l.fileBytes = 22; Reject([&] { Scan(b, l); });
        l = ZipLimits(); l.entries = 0; Reject([&] { Scan(b, l); }); l = ZipLimits(); l.nameBytes = 1; Reject([&] { Scan(b, l); });
        l = ZipLimits(); l.totalNameBytes = 1; Reject([&] { Scan(b, l); }); l = ZipLimits(); l.compressedEntryBytes = 1; Reject([&] { Scan(b, l); });
        l = ZipLimits(); l.inflatedEntryBytes = 1; Reject([&] { Scan(b, l); });
    });
    std::cout << "wolf2009 zip self-test: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
}

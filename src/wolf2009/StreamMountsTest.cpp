// SPDX-License-Identifier: GPL-3.0-or-later
#include "StreamMounts.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
typedef std::pair<std::string, Bytes> File;
void Need(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void Reject(F op) {
    try {
        op();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("Invalid stream-mount operation accepted");
}
void U32(Bytes &b, std::uint32_t x) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(x >> (i * 8)));
}
void Pad(Bytes &b) {
    while (b.size() % 4)
        b.push_back(0);
}
Bytes Chunk(const char *type, const std::vector<File> &files) {
    Bytes plain;
    U32(plain, static_cast<std::uint32_t>(files.size()));
    for (std::size_t i = 0; i < files.size(); ++i) {
        plain.insert(plain.end(), files[i].first.begin(), files[i].first.end());
        plain.push_back(0);
        U32(plain, static_cast<std::uint32_t>(files[i].second.size()));
        U32(plain, static_cast<std::uint32_t>(files[i].second.size()));
    }
    Pad(plain);
    for (std::size_t i = 0; i < files.size(); ++i) {
        plain.insert(plain.end(), files[i].second.begin(), files[i].second.end());
        Pad(plain);
    }
    uLongf size = compressBound(static_cast<uLong>(plain.size()));
    Bytes packed(size);
    Need(compress2(packed.data(), &size, plain.data(), static_cast<uLong>(plain.size()), Z_BEST_SPEED) ==
             Z_OK,
         "Synthetic compression failed");
    packed.resize(size);
    Bytes out;
    for (int i = 3; i >= 0; --i)
        out.push_back(type[i]);
    U32(out, static_cast<std::uint32_t>(plain.size()));
    U32(out, static_cast<std::uint32_t>(packed.size()));
    out.insert(out.end(), packed.begin(), packed.end());
    return out;
}
Bytes Package(const std::vector<File> &files, const char *type = "MODL") {
    Bytes b;
    U32(b, 300);
    const Bytes c = Chunk(type, files);
    b.insert(b.end(), c.begin(), c.end());
    return b;
}
struct Store {
    std::map<std::string, Bytes> files;
    int opened = 0, closed = 0, active = 0, peak = 0;
    bool missing = false, fail = false;
    std::function<void()> hook, closeHook;
    struct Input : std::istringstream {
        Store &store;
        Input(Store &s, const Bytes &b)
            : std::istringstream(std::string(b.begin(), b.end()), std::ios::binary | std::ios::in), store(s) {
            ++store.opened;
            ++store.active;
            if (store.active > store.peak)
                store.peak = store.active;
        }
        ~Input() {
            --store.active;
            ++store.closed;
            if (store.closeHook)
                store.closeHook();
        }
    };
    std::unique_ptr<std::istream> Open(const std::string &id) {
        if (hook)
            hook();
        if (fail)
            throw std::runtime_error("Synthetic provider failure");
        if (missing || !files.count(id))
            return {};
        return std::unique_ptr<std::istream>(new Input(*this, files.at(id)));
    }
    StreamProvider Provider() {
        return [this](const std::string &id) { return Open(id); };
    }
};
void Balanced(const Store &s) {
    Need(s.active == 0 && s.opened == s.closed && s.peak <= 1,
         "Provider handle leaked or concurrency bound exceeded");
}
} // namespace
int RunStreamMountsTests() {
    int passed = 0;
    try {
        Store store;
        const Bytes data = {'a', 'b', 'c'};
        store.files["map/main"] = Package({{"Models\\TEST", data}, {"synthetic/empty", {}}});
        StreamMounts vfs(store.Provider());
        const StreamMountInfo info = vfs.Mount("MAP\\MAIN");
        Need(info.id == "map/main" && info.resources == 2 && info.chunks == 1 &&
                 vfs.Totals().resources == 2 && vfs.Mounts().size() == 1,
             "Mount metadata/count failed");
        Balanced(store);
        ++passed;
        StreamLookup q = vfs.Find("modl", "MODELS/test");
        Need(q.status == StreamLookupStatus::Unique && q.matches.size() == 1 &&
                 q.matches[0].name == "models/test" && q.matches[0].mountId == "map/main",
             "Typed slash/ASCII lookup failed");
        Need(vfs.Read(q.matches[0]) == data, "Selected read mismatch");
        Balanced(store);
        ++passed;
        const int afterRead = store.opened;
        q = vfs.Find("MODL", "models/test");
        Need(store.opened == afterRead, "Lookup unexpectedly opened stream");
        Need(vfs.ReadUnique("MODL", "synthetic/empty").empty(), "Empty resource read failed");
        Balanced(store);
        ++passed;
        Need(vfs.Find("TXTR", "models/test").status == StreamLookupStatus::Missing &&
                 vfs.Find("MODL", "models/test.md5r").status == StreamLookupStatus::Missing,
             "Type or extension was invented");
        Reject([&] { vfs.ReadUnique("MODL", "missing"); });
        ++passed;
        store.files["map/detail"] = Package({{"models/test", {'x'}}});
        vfs.Mount("map/detail");
        q = vfs.Find("MODL", "models/test");
        Need(q.status == StreamLookupStatus::Ambiguous && q.matches.size() == 2,
             "Cross-mount duplicate precedence guessed");
        Reject([&] { vfs.ReadUnique("MODL", "models/test"); });
        Need(vfs.Read(q.matches[0]) == data && vfs.Read(q.matches[1]) == Bytes({'x'}),
             "Explicit source-qualified read failed");
        Balanced(store);
        ++passed;
        store.files["map/types"] = Package({{"models/test", {'t'}}}, "TXTR");
        vfs.Mount("map/types");
        Need(vfs.Find("TXTR", "models/test").status == StreamLookupStatus::Unique,
             "Same name in another type became ambiguous");
        ++passed;
        store.files["map/dupes"] = Package({{"same/name", {'1'}}, {"SAME\\NAME", {'2'}}});
        vfs.Mount("map/dupes");
        Need(vfs.Find("MODL", "same/name").matches.size() == 2, "Within-mount normalized duplicates hidden");
        ++passed;
        const StreamResource stable = vfs.Find("MODL", "synthetic/empty").matches[0];
        const StreamMountInfo before = vfs.Totals();
        store.files["map/bad"] = {0, 0, 0, 0};
        Reject([&] { vfs.Mount("map/bad"); });
        Need(vfs.Totals().resources == before.resources && vfs.Totals().archiveBytes == before.archiveBytes &&
                 vfs.Read(stable).empty(),
             "Failed mount damaged existing resources");
        Balanced(store);
        ++passed;
        const int opens = store.opened;
        Reject([&] { vfs.Mount("MAP/MAIN"); });
        Need(store.opened == opens, "Duplicate mount reopened source");
        ++passed;
        store.missing = true;
        Reject([&] { vfs.Mount("map/null"); });
        store.missing = false;
        store.fail = true;
        Reject([&] { vfs.Mount("map/fail"); });
        store.fail = false;
        Need(vfs.Totals().resources == before.resources, "Provider failure committed mount");
        Balanced(store);
        ++passed;
        Reject([&] { vfs.Find("MODL", "../models/test"); });
        Reject([&] { vfs.Find("MODL", "C:/models/test"); });
        Reject([&] { vfs.Find("MODL", "/models/test"); });
        Reject([&] { vfs.Find("MODL", "models//test"); });
        Reject([&] { vfs.Find("BAD", "models/test"); });
        ++passed;
        store.files["map/unsafe"] = Package({{"../unsafe", data}});
        Reject([&] { vfs.Mount("map/unsafe"); });
        Need(vfs.Totals().resources == before.resources, "Unsafe name mount partially committed");
        Balanced(store);
        ++passed;
        StreamResource modified = stable;
        modified.size = 99;
        Reject([&] { vfs.Read(modified); });
        StreamResource invalid;
        Reject([&] { vfs.Read(invalid); });
        StreamMounts other(store.Provider());
        other.Mount("map/main");
        Reject([&] { other.Read(stable); });
        Balanced(store);
        ++passed;
        const StreamResource old = vfs.Find("MODL", "models/test").matches[0];
        Need(vfs.Unmount("map/main"), "Unmount missing existing ID");
        Reject([&] { vfs.Read(old); });
        vfs.Mount("map/main");
        Reject([&] { vfs.Read(old); });
        Need(!vfs.Unmount("not/mounted"), "Absent unmount reported success");
        Need(vfs.ReadUnique("MODL", "synthetic/empty").empty(), "Remount did not restore lookup");
        Balanced(store);
        ++passed;
        StreamResource cleared = vfs.Find("MODL", "synthetic/empty").matches[0];
        vfs.Clear();
        Need(vfs.Mounts().empty() && vfs.Totals().resources == 0 && vfs.Totals().chunks == 0 &&
                 vfs.Totals().nameBytes == 0 && vfs.Totals().archiveBytes == 0,
             "Clear did not release metadata counters");
        vfs.Mount("map/main");
        Reject([&] { vfs.Read(cleared); });
        ++passed;
        const Bytes unchanged = store.files["map/main"];
        store.files["map/main"].push_back(0);
        Reject([&] { vfs.ReadUnique("MODL", "models/test"); });
        store.files["map/main"] = unchanged;
        Need(vfs.ReadUnique("MODL", "models/test") == data, "Read failure damaged stable mount");
        Balanced(store);
        ++passed;
        store.hook = [&] { vfs.Clear(); };
        Reject([&] { vfs.Mount("map/types"); });
        store.hook = {};
        Need(vfs.ReadUnique("MODL", "models/test") == data, "Reentrant provider damaged mount");
        Balanced(store);
        ++passed;
        store.fail = true;
        Reject([&] { vfs.ReadUnique("MODL", "models/test"); });
        store.fail = false;
        store.missing = true;
        Reject([&] { vfs.ReadUnique("MODL", "models/test"); });
        store.missing = false;
        Need(vfs.ReadUnique("MODL", "models/test") == data, "Failed lazy read invalidated mount");
        Balanced(store);
        ++passed;
        bool closeReentryRejected = false;
        store.closeHook = [&] {
            try {
                vfs.Clear();
            } catch (const std::runtime_error &) {
                closeReentryRejected = true;
            }
        };
        Need(vfs.ReadUnique("MODL", "models/test") == data && closeReentryRejected,
             "Stream destructor reentry was permitted");
        store.closeHook = {};
        Balanced(store);
        ++passed;
        StreamMountLimits limits;
        limits.mounts = 1;
        StreamMounts one(store.Provider(), limits);
        one.Mount("map/main");
        Reject([&] { one.Mount("map/detail"); });
        Need(one.Mounts().size() == 1, "Mount-count limit failed");
        ++passed;
        limits = StreamMountLimits();
        limits.resources = 2;
        StreamMounts entries(store.Provider(), limits);
        entries.Mount("map/main");
        Reject([&] { entries.Mount("map/detail"); });
        Need(entries.Totals().resources == 2, "Aggregate resource limit failed");
        ++passed;
        limits = StreamMountLimits();
        limits.chunks = 1;
        StreamMounts chunks(store.Provider(), limits);
        chunks.Mount("map/main");
        Reject([&] { chunks.Mount("map/detail"); });
        Need(chunks.Totals().chunks == 1, "Aggregate chunk limit failed");
        ++passed;
        limits = StreamMountLimits();
        limits.totalNameBytes = info.nameBytes;
        StreamMounts names(store.Provider(), limits);
        names.Mount("map/main");
        Reject([&] { names.Mount("map/detail"); });
        Need(names.Totals().nameBytes == info.nameBytes, "Aggregate name limit failed");
        ++passed;
        limits = StreamMountLimits();
        limits.archiveBytes = unchanged.size() - 1;
        StreamMounts bytes(store.Provider(), limits);
        Reject([&] { bytes.Mount("map/main"); });
        Need(bytes.Mounts().empty(), "Per-archive byte limit failed");
        limits = StreamMountLimits();
        limits.totalArchiveBytes = unchanged.size();
        StreamMounts totalBytes(store.Provider(), limits);
        totalBytes.Mount("map/main");
        Reject([&] { totalBytes.Mount("map/detail"); });
        Need(totalBytes.Mounts().size() == 1, "Aggregate archive byte limit failed");
        ++passed;
        limits = StreamMountLimits();
        limits.readBytes = 2;
        StreamMounts reads(store.Provider(), limits);
        reads.Mount("map/main");
        Reject([&] { reads.ReadUnique("MODL", "models/test"); });
        Need(reads.ReadUnique("MODL", "synthetic/empty").empty(),
             "Per-resource read limit damaged smaller read");
        Balanced(store);
        ++passed;
        limits = StreamMountLimits();
        limits.lookupMatches = 1;
        StreamMounts matches(store.Provider(), limits);
        matches.Mount("map/dupes");
        Reject([&] { matches.Find("MODL", "same/name"); });
        Need(matches.Totals().resources == 2, "Lookup match budget changed mounts");
        ++passed;
        limits = StreamMountLimits();
        limits.archive.inflatedChunkBytes = 4;
        StreamMounts packed(store.Provider(), limits);
        Reject([&] { packed.Mount("map/main"); });
        Need(packed.Mounts().empty(), "Per-archive limits not forwarded");
        Balanced(store);
        ++passed;
        typename std::aligned_storage<sizeof(StreamMounts), alignof(StreamMounts)>::type storage;
        StreamMounts *first = new (&storage) StreamMounts(store.Provider());
        first->Mount("map/main");
        const StreamResource retired = first->Find("MODL", "models/test").matches[0];
        first->~StreamMounts();
        StreamMounts *second = new (&storage) StreamMounts(store.Provider());
        second->Mount("map/main");
        bool staleRejected = false;
        try {
            second->Read(retired);
        } catch (const std::runtime_error &) {
            staleRejected = true;
        }
        second->~StreamMounts();
        Need(staleRejected, "Handle survived destruction and instance-address reuse");
        Balanced(store);
        ++passed;
        std::cout << "Stream mounts self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Stream mounts self-test failed after " << passed << " checks: " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

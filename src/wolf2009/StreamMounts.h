// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_STREAM_MOUNTS_H
#define WOLF2009_STREAM_MOUNTS_H
#include "Archive.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace wolf2009 {
// A provider maps an opaque normalized virtual mount ID to a fresh, seekable,
// read-only stream. Its backing bytes must remain stable until Unmount. The
// core neither interprets host paths nor retains streams between operations.
typedef std::function<std::unique_ptr<std::istream>(const std::string &)> StreamProvider;
struct StreamMountLimits {
    Limits archive;
    std::size_t mounts = 64, resources = 200000, chunks = 65536;
    // Counts original and normalized resource-name bytes together. Mount IDs
    // are separately bounded by mounts * virtualNameBytes.
    std::size_t totalNameBytes = 64u * 1024u * 1024u;
    std::size_t virtualNameBytes = 4096, lookupMatches = 1024;
    std::size_t readBytes = 256u * 1024u * 1024u;
    std::uint64_t archiveBytes = 1024ull * 1024ull * 1024ull;
    std::uint64_t totalArchiveBytes = 4ull * 1024ull * 1024ull * 1024ull;
};
struct StreamMountInfo {
    std::string id;
    std::size_t resources = 0, chunks = 0, nameBytes = 0;
    std::uint64_t archiveBytes = 0;
};
class StreamMounts;
struct StreamResource {
    std::string mountId, type, name; // Canonical ASCII case/slash names.
    std::uint32_t size = 0;

  private:
    friend class StreamMounts;
    std::shared_ptr<const int> owner_;
    std::uint64_t generation_ = 0;
    std::size_t chunk_ = 0, entry_ = 0;
};
enum class StreamLookupStatus { Missing, Unique, Ambiguous };
struct StreamLookup {
    StreamLookupStatus status = StreamLookupStatus::Missing;
    std::vector<StreamResource> matches;
};
class StreamMounts {
  public:
    explicit StreamMounts(StreamProvider provider, const StreamMountLimits &limits = StreamMountLimits());
    StreamMounts(const StreamMounts &) = delete;
    StreamMounts &operator=(const StreamMounts &) = delete;
    // Strong transaction: failures retain all prior mounts, counters, handles.
    // Scan validates/decompresses chunks transiently; only metadata is retained.
    StreamMountInfo Mount(const std::string &mountId);
    bool Unmount(const std::string &mountId);
    void Clear();
    std::vector<StreamMountInfo> Mounts() const;
    StreamMountInfo Totals() const;
    // Exact typed lookup, without extension invention or precedence guessing.
    // Duplicates within/across mounts remain explicit and source-qualified.
    StreamLookup Find(const std::string &type, const std::string &name) const;
    std::vector<unsigned char> Read(const StreamResource &resource) const;
    std::vector<unsigned char> ReadUnique(const std::string &type, const std::string &name) const;
    // Resource handles are tied to this instance and mount generation. Unmount
    // or Clear invalidates them permanently, including after an identical remount.
    // At most one provider stream is owned during Mount/Read; zero otherwise.
    // Not thread-safe: caller synchronizes access. Reentrant mutations/read are
    // refused, including calls made from a provider or stream destructor.
  private:
    struct NamedEntry {
        std::string type, name;
        std::uint32_t size = 0;
        std::size_t chunk = 0, entry = 0;
    };
    struct Mounted {
        StreamMountInfo info;
        std::uint64_t generation = 0;
        Archive archive;
        std::vector<NamedEntry> entries;
    };
    StreamProvider provider_;
    std::shared_ptr<const int> identity_;
    StreamMountLimits limits_;
    std::vector<Mounted> mounts_;
    StreamMountInfo totals_;
    std::uint64_t nextGeneration_ = 1;
    mutable bool active_ = false;
};
int RunStreamMountsTests();
} // namespace wolf2009
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#include "StreamMounts.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
struct Operation {
    bool &active;
    explicit Operation(bool &flag) : active(flag) {
        Need(!active, "Reentrant stream-mount operation is unsupported");
        active = true;
    }
    ~Operation() {
        active = false;
    }
};
std::string VirtualName(std::string name, std::size_t limit) {
    Need(!name.empty() && name.size() <= limit, "Virtual resource name is empty or exceeds limit");
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        Need(c >= 32 && c != 127 && c != ':', "Unsafe byte in virtual resource name");
        if (name[i] == '\\')
            name[i] = '/';
        if (name[i] >= 'A' && name[i] <= 'Z')
            name[i] = char(name[i] - 'A' + 'a');
    }
    Need(name[0] != '/' && name[name.size() - 1] != '/', "Absolute or empty virtual resource path");
    std::size_t begin = 0;
    while (begin < name.size()) {
        const std::size_t end = name.find('/', begin),
                          length = end == std::string::npos ? name.size() - begin : end - begin;
        const std::string part = name.substr(begin, length);
        Need(!part.empty() && part != "." && part != "..", "Unsafe component in virtual resource name");
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return name;
}
std::string TypeName(std::string type) {
    Need(type.size() == 4, "Resource type must be a four-byte ASCII FourCC");
    for (std::size_t i = 0; i < 4; ++i) {
        Need(static_cast<unsigned char>(type[i]) >= 32 && static_cast<unsigned char>(type[i]) <= 126,
             "Resource type must be printable ASCII");
        if (type[i] >= 'a' && type[i] <= 'z')
            type[i] = char(type[i] - 'a' + 'A');
    }
    return type;
}
std::uint64_t Length(std::istream &stream) {
    stream.clear();
    stream.seekg(0, std::ios::end);
    const std::streamoff n = stream.tellg();
    Need(n >= 4, "Provider stream is too short or not seekable");
    return static_cast<std::uint64_t>(n);
}
} // namespace
StreamMounts::StreamMounts(StreamProvider provider, const StreamMountLimits &limits)
    : provider_(std::move(provider)), identity_(new int(0)), limits_(limits) {
    Need(bool(provider_), "A stream provider is required");
}
StreamMountInfo StreamMounts::Mount(const std::string &mountId) {
    Operation operation(active_);
    const std::string id = VirtualName(mountId, limits_.virtualNameBytes);
    Need(mounts_.size() < limits_.mounts, "Mounted archive count exceeds configured limit");
    for (std::size_t i = 0; i < mounts_.size(); ++i)
        Need(mounts_[i].info.id != id, "Virtual mount ID is already mounted");
    Need(nextGeneration_ != (std::numeric_limits<std::uint64_t>::max)(), "Mount generation space exhausted");
    std::unique_ptr<std::istream> input = provider_(id);
    Need(bool(input), "Stream provider returned no stream");
    Mounted staged;
    staged.info.id = id;
    staged.generation = nextGeneration_;
    staged.info.archiveBytes = Length(*input);
    Need(staged.info.archiveBytes <= limits_.archiveBytes &&
             totals_.archiveBytes <= limits_.totalArchiveBytes &&
             staged.info.archiveBytes <= limits_.totalArchiveBytes - totals_.archiveBytes,
         "Mounted archive byte limit exceeded");
    Limits scan = limits_.archive;
    scan.entries = (std::min)(scan.entries, limits_.resources - totals_.resources);
    scan.chunks = (std::min)(scan.chunks, limits_.chunks - totals_.chunks);
    scan.nameBytes = (std::min)(scan.nameBytes, limits_.virtualNameBytes);
    scan.totalNameBytes = (std::min)(scan.totalNameBytes, (limits_.totalNameBytes - totals_.nameBytes) / 2);
    staged.archive = Archive::Scan(*input, scan);
    const std::vector<Chunk> &chunks = staged.archive.Chunks();
    staged.info.chunks = chunks.size();
    for (std::size_t c = 0; c < chunks.size(); ++c)
        for (std::size_t e = 0; e < chunks[c].entries.size(); ++e) {
            const Entry &entry = chunks[c].entries[e];
            NamedEntry named;
            named.type = TypeName(chunks[c].type);
            named.name = VirtualName(entry.name, limits_.virtualNameBytes);
            named.size = entry.size;
            named.chunk = c;
            named.entry = e;
            Need(staged.info.nameBytes <= limits_.totalNameBytes - totals_.nameBytes &&
                     entry.name.size() <=
                         (limits_.totalNameBytes - totals_.nameBytes - staged.info.nameBytes) / 2,
                 "Mounted resource-name byte limit exceeded");
            staged.info.nameBytes += entry.name.size() * 2;
            staged.entries.push_back(std::move(named));
        }
    staged.info.resources = staged.entries.size();
    StreamMountInfo info = staged.info;
    mounts_.push_back(std::move(staged));
    ++nextGeneration_;
    totals_.resources += info.resources;
    totals_.chunks += info.chunks;
    totals_.nameBytes += info.nameBytes;
    totals_.archiveBytes += info.archiveBytes;
    return info;
}
bool StreamMounts::Unmount(const std::string &mountId) {
    Operation operation(active_);
    const std::string id = VirtualName(mountId, limits_.virtualNameBytes);
    for (std::size_t i = 0; i < mounts_.size(); ++i)
        if (mounts_[i].info.id == id) {
            const StreamMountInfo info = mounts_[i].info;
            mounts_.erase(mounts_.begin() + i);
            totals_.resources -= info.resources;
            totals_.chunks -= info.chunks;
            totals_.nameBytes -= info.nameBytes;
            totals_.archiveBytes -= info.archiveBytes;
            return true;
        }
    return false;
}
void StreamMounts::Clear() {
    Operation operation(active_);
    mounts_.clear();
    totals_ = StreamMountInfo();
}
std::vector<StreamMountInfo> StreamMounts::Mounts() const {
    std::vector<StreamMountInfo> out;
    out.reserve(mounts_.size());
    for (std::size_t i = 0; i < mounts_.size(); ++i)
        out.push_back(mounts_[i].info);
    return out;
}
StreamMountInfo StreamMounts::Totals() const {
    return totals_;
}
StreamLookup StreamMounts::Find(const std::string &requestedType, const std::string &requestedName) const {
    const std::string type = TypeName(requestedType),
                      name = VirtualName(requestedName, limits_.virtualNameBytes);
    StreamLookup out;
    for (std::size_t m = 0; m < mounts_.size(); ++m) {
        const Mounted &mount = mounts_[m];
        for (std::size_t e = 0; e < mount.entries.size(); ++e) {
            const NamedEntry &entry = mount.entries[e];
            if (entry.type != type || entry.name != name)
                continue;
            Need(out.matches.size() < limits_.lookupMatches,
                 "Resource lookup match count exceeds configured limit");
            StreamResource resource;
            resource.mountId = mount.info.id;
            resource.type = entry.type;
            resource.name = entry.name;
            resource.size = entry.size;
            resource.owner_ = identity_;
            resource.generation_ = mount.generation;
            resource.chunk_ = entry.chunk;
            resource.entry_ = entry.entry;
            out.matches.push_back(std::move(resource));
        }
    }
    out.status = out.matches.empty()       ? StreamLookupStatus::Missing
                 : out.matches.size() == 1 ? StreamLookupStatus::Unique
                                           : StreamLookupStatus::Ambiguous;
    return out;
}
std::vector<unsigned char> StreamMounts::Read(const StreamResource &resource) const {
    Operation operation(active_);
    Need(resource.owner_ == identity_ && resource.generation_ != 0,
         "Resource handle belongs to another stream-mount instance");
    const Mounted *mounted = nullptr;
    for (std::size_t i = 0; i < mounts_.size(); ++i)
        if (mounts_[i].generation == resource.generation_) {
            mounted = &mounts_[i];
            break;
        }
    Need(mounted != nullptr, "Resource handle refers to an unmounted generation");
    const std::vector<Chunk> &chunks = mounted->archive.Chunks();
    Need(resource.chunk_ < chunks.size() && resource.entry_ < chunks[resource.chunk_].entries.size(),
         "Invalid resource handle entry");
    const Chunk &chunk = chunks[resource.chunk_];
    const Entry &entry = chunk.entries[resource.entry_];
    Need(resource.mountId == mounted->info.id && resource.type == TypeName(chunk.type) &&
             resource.name == VirtualName(entry.name, limits_.virtualNameBytes) &&
             resource.size == entry.size,
         "Resource handle metadata was modified");
    Need(entry.size <= limits_.readBytes, "Selected resource exceeds read-byte limit");
    std::unique_ptr<std::istream> input = provider_(mounted->info.id);
    Need(bool(input), "Stream provider returned no stream");
    Need(Length(*input) == mounted->info.archiveBytes, "Provider archive length changed since mount");
    return mounted->archive.ReadEntry(*input, resource.chunk_, resource.entry_);
}
std::vector<unsigned char> StreamMounts::ReadUnique(const std::string &type, const std::string &name) const {
    const StreamLookup lookup = Find(type, name);
    Need(lookup.status != StreamLookupStatus::Missing, "Typed virtual resource was not found");
    Need(lookup.status == StreamLookupStatus::Unique,
         "Typed virtual resource is ambiguous; explicitly qualify its source");
    return Read(lookup.matches[0]);
}
} // namespace wolf2009

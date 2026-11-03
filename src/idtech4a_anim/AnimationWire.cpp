// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnimationWire.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#ifdef __FAST_MATH__
#error Source_animation_wire_requires_finite_IEEE_arithmetic
#endif
namespace wolf2009 { namespace animation {
namespace {
void Need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::string> Parts(const std::string& value) {
    Need(!value.empty() && value.size() <= 4096 && value.front() != '/' && value.back() != '/', "invalid animation binding virtual name");
    for (unsigned char c : value)
        Need(c >= 33 && c < 127 && !(c >= 'A' && c <= 'Z') && c != '\\' && c != ':' && c != '"' && c != '#' &&
             c != '{' && c != '}' && c != '(' && c != ')' && c != ',', "noncanonical animation binding virtual name");
    std::vector<std::string> out;
    std::size_t begin = 0;
    for (;;) {
        const auto end = value.find('/', begin); const auto part = value.substr(begin, end - begin);
        Need(!part.empty() && part != "." && part != "..", "unsafe animation binding path component");
        out.push_back(part); if (end == std::string::npos) break; begin = end + 1;
    }
    return out;
}
std::string Typed(const std::string& value, const char* type) {
    const auto parts = Parts(value);
    Need(parts.size() >= 4 && parts[0] == "wolf2009" && parts[2] == type, "animation binding source FourCC/namespace mismatch");
    return parts[1];
}
void Validate(const Binding& b) {
    Need(b.policy == PlaybackPolicy::VisualPoseNoExtraction && b.joints == 52 && b.frames == 182,
         "unsupported source animation wire profile");
    Need(std::isfinite(b.duration) && std::isfinite(b.frameDuration) && b.duration > 0 && b.frameDuration > 0 &&
         std::abs(b.frameDuration - 1.0 / 24) < 1e-7 && std::abs(b.duration - 181 * b.frameDuration) < 1e-5,
         "source animation wire timing mismatch");
    Need(std::ceil(b.duration * 1000) <= (std::numeric_limits<int>::max)(), "source animation duration exceeds native milliseconds");
    std::array<std::string,3> mounts{{Typed(b.paths[0], "modl"), Typed(b.paths[1], "hkxa"), Typed(b.paths[2], "skel")}};
    Need(Typed(b.modelAlias, "md5r") == mounts[0], "source animation model alias lost its source mount");
    Parts(b.clipName); Parts(b.skeletonName);
    for (unsigned i = 0; i < 3; ++i) {
        Need(b.sources[i].epoch && b.sources[i].bytes && b.sources[i].bytes <= MaximumSourceBytes,
             "source animation wire source identity/budget mismatch");
        for (unsigned j = 0; j < i; ++j)
            Need(mounts[j] != mounts[i] || b.sources[j].epoch == b.sources[i].epoch, "inconsistent epochs for a shared animation source mount");
    }
}
void U32(Bytes& out, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(value >> (8*i))); }
void U64(Bytes& out, std::uint64_t value) { for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<unsigned char>(value >> (8*i))); }
std::uint64_t Bits(double value) { std::uint64_t bits; static_assert(sizeof(bits) == sizeof(value), "IEEE64 descriptor"); std::memcpy(&bits, &value, sizeof(bits)); return bits; }
void Text(Bytes& out, const std::string& value) { U32(out, static_cast<std::uint32_t>(value.size())); out.insert(out.end(), value.begin(), value.end()); }
class Reader {
    const Bytes& bytes;
    std::size_t at = 0;
  public:
    explicit Reader(const Bytes& b) : bytes(b) {}
    void Range(std::size_t count) const { Need(at <= bytes.size() && count <= bytes.size() - at, "truncated animation binding descriptor"); }
    std::uint32_t U32() { Range(4); std::uint32_t out = 0; for (unsigned i = 0; i < 4; ++i) out |= std::uint32_t(bytes[at++]) << (8*i); return out; }
    std::uint64_t U64() { Range(8); std::uint64_t out = 0; for (unsigned i = 0; i < 8; ++i) out |= std::uint64_t(bytes[at++]) << (8*i); return out; }
    double Double() { const std::uint64_t bits = U64(); double value; std::memcpy(&value, &bits, sizeof(value)); return value; }
    std::string Text() { const auto count = U32(); Need(count && count <= 4096, "animation binding string budget"); Range(count); std::string out(bytes.begin()+at, bytes.begin()+at+count); at += count; return out; }
    Bytes Blob() { const auto count = U32(); Need(count && count <= MaximumSourceBytes, "animation binding payload budget"); Range(count); Bytes out(bytes.begin()+at, bytes.begin()+at+count); at += count; return out; }
    void Magic() { static const unsigned char magic[8] = {'W','2','A','N','I','M','1',0}; Range(8); Need(std::equal(magic, magic+8, bytes.begin()+at), "animation binding magic mismatch"); at += 8; }
    bool End() const { return at == bytes.size(); }
};
}
std::uint64_t Fingerprint(const unsigned char* data, std::size_t bytes) {
    std::uint64_t out = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i < bytes; ++i) out = (out ^ data[i]) * UINT64_C(1099511628211);
    return out;
}
Bytes Metadata(const Binding& b) {
    Validate(b);
    Bytes out{'W','2','A','N','I','M','1',0}; U32(out,1); U32(out,static_cast<std::uint32_t>(b.policy)); U32(out,b.joints); U32(out,b.frames);
    U64(out,Bits(b.duration)); U64(out,Bits(b.frameDuration));
    for (const auto& source : b.sources) { U64(out,source.epoch); U64(out,source.bytes); U64(out,source.fingerprint); }
    Text(out,b.modelAlias); for (const auto& path : b.paths) Text(out,path); Text(out,b.clipName); Text(out,b.skeletonName);
    Need(out.size() <= 65536, "animation binding metadata budget"); return out;
}
Bytes Serialize(const Envelope& envelope) {
    Bytes out = Metadata(envelope.binding);
    const std::array<const Bytes*,2> blobs{{&envelope.animation,&envelope.skeleton}};
    for (unsigned i = 0; i < 2; ++i) {
        const auto& bytes = *blobs[i]; const auto& identity = envelope.binding.sources[i+1];
        Need(!bytes.empty() && bytes.size() <= MaximumSourceBytes && bytes.size() == identity.bytes &&
             Fingerprint(bytes.data(),bytes.size()) == identity.fingerprint, "animation binding payload identity mismatch");
        U32(out,static_cast<std::uint32_t>(bytes.size())); out.insert(out.end(),bytes.begin(),bytes.end());
    }
    Need(out.size() <= MaximumEnvelopeBytes, "animation binding envelope budget"); return out;
}
Envelope Parse(const Bytes& bytes) {
    Need(bytes.size() <= MaximumEnvelopeBytes, "animation binding envelope budget");
    Reader r(bytes); r.Magic(); Need(r.U32() == 1, "unsupported animation descriptor version");
    Envelope out; auto& b = out.binding;
    b.policy = static_cast<PlaybackPolicy>(r.U32()); b.joints = r.U32(); b.frames = r.U32(); b.duration = r.Double(); b.frameDuration = r.Double();
    for (auto& source : b.sources) { source.epoch = r.U64(); source.bytes = r.U64(); source.fingerprint = r.U64(); }
    b.modelAlias = r.Text(); for (auto& path : b.paths) path = r.Text(); b.clipName = r.Text(); b.skeletonName = r.Text(); Validate(b);
    out.animation = r.Blob(); out.skeleton = r.Blob(); Need(r.End(), "animation descriptor has unexplained trailing bytes");
    for (unsigned i = 0; i < 2; ++i) {
        const auto& payload = i ? out.skeleton : out.animation; const auto& identity = b.sources[i+1];
        Need(identity.bytes == payload.size() && identity.fingerprint == Fingerprint(payload.data(),payload.size()), "animation descriptor payload identity mismatch");
    }
    return out;
}
std::string Alias(const Binding& b) {
    const Bytes bytes = Metadata(b); std::uint64_t hash = Fingerprint(bytes.data(),bytes.size()); std::string hex(16,'0');
    for (int i = 15; i >= 0; --i) { hex[std::size_t(i)] = "0123456789abcdef"[hash & 15]; hash >>= 4; }
    return "wolf2009/_animation/anim/" + hex + ".md5anim";
}
bool Same(const Binding& a, const Binding& b) { return Metadata(a) == Metadata(b); }
} }

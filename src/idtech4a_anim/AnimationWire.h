// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ANIMATION_WIRE_H
#define WOLF2009_ANIMATION_WIRE_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace wolf2009 { namespace animation {
typedef std::vector<unsigned char> Bytes;
enum class PlaybackPolicy : std::uint32_t { VisualPoseNoExtraction = 1 };
struct SourceIdentity {
    std::uint64_t epoch = 0, bytes = 0, fingerprint = 0;
};
struct Binding {
    PlaybackPolicy policy = PlaybackPolicy::VisualPoseNoExtraction;
    std::uint32_t joints = 52, frames = 182;
    double duration = 0, frameDuration = 0;
    std::array<SourceIdentity,3> sources; // MODL, HKXA, SKEL.
    std::string modelAlias;
    std::array<std::string,3> paths;
    std::string clipName, skeletonName;
};
struct Envelope {
    Binding binding;
    Bytes animation, skeleton;
};
const std::size_t MaximumSourceBytes = 64u * 1024u * 1024u;
const std::size_t MaximumEnvelopeBytes = 128u * 1024u * 1024u + 65536u;
std::uint64_t Fingerprint(const unsigned char* data, std::size_t bytes);
Bytes Metadata(const Binding& binding);
Bytes Serialize(const Envelope& envelope);
Envelope Parse(const Bytes& bytes);
std::string Alias(const Binding& binding);
bool Same(const Binding& a, const Binding& b);
} }
#endif

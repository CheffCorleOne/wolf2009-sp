// SPDX-License-Identifier: GPL-3.0-or-later
// Checked scalar adaptation of HavokLib's transform spline decompressor.
// Upstream: PredatorCZ/HavokLib ef5d5c6f5ab8d7f32cb4e8983300b8db61e4dc6b.
#ifndef WOLF2009_SPLINE_TRANSFORM_CODEC_H
#define WOLF2009_SPLINE_TRANSFORM_CODEC_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace wolf2009 {
enum class SplineCodecFailure { InvalidData, Unsupported, LimitExceeded };
class SplineCodecError : public std::runtime_error {
  public:
    SplineCodecError(SplineCodecFailure failure, const std::string& message)
        : std::runtime_error(message), failure_(failure) {}
    SplineCodecFailure Failure() const { return failure_; }
  private:
    SplineCodecFailure failure_;
};
enum class SplineScalePolicy { RequireIdentityScale, RetainFiniteScale };
struct SplineTransformLimits {
    std::size_t compressedBytes = 64u * 1024u * 1024u;
    std::size_t tracks = 4096, floatTracks = 4096, blocks = 4096, frames = 1000000;
    std::size_t controlsPerCurve = 4096, totalControls = 1000000;
    // Conservative retained storage charge (including string/vector overhead),
    // not an exact allocator/RSS bound. Checked before retained allocations.
    std::size_t retainedBytes = 128u * 1024u * 1024u, work = 16000000;
    double componentMagnitude = 1000000, durationSeconds = 1000000;
};
struct SplineTransformInput {
    std::vector<unsigned char> data;
    std::uint32_t transformTracks = 0, floatTracks = 0, numFrames = 0;
    std::uint32_t maxFramesPerBlock = 0, maskAndQuantizationSize = 0;
    double duration = 0, blockDuration = 0, blockInverseDuration = 0, frameDuration = 0;
    // Offsets are absolute indices in data. Strictly ordered, four-byte aligned
    // block starts. Each caller-supplied transform end is authoritative: the
    // decoded TRS data must consume that exact range, never its float suffix.
    // This codec does NOT infer relative/absolute source array semantics.
    std::vector<std::uint32_t> blockOffsets, transformEndOffsets;
    // Optional source facts retained unchanged; NOT interpreted by this codec.
    std::vector<std::uint32_t> rawFloatBlockOffsets, rawTransformOffsets, rawFloatOffsets;
};
struct SplineLocalTransform {
    std::array<double, 3> translation{{0, 0, 0}}, scale{{1, 1, 1}};
    std::array<double, 4> rotation{{0, 0, 0, 1}}; // x,y,z,w, normalized
};
struct SplineTransformBlockInfo {
    std::size_t begin = 0, end = 0, transformBegin = 0, transformEnd = 0;
    std::size_t floatMasksBegin = 0, floatMasksEnd = 0;
    std::size_t opaqueSuffixBegin = 0, opaqueSuffixEnd = 0;
    std::uint32_t firstFrame = 0, lastLocalFrame = 0;
};
// Immutable owning transform curves. No OS, skeleton, names, retargeting,
// track-to-bone mapping, additive policy, events or float-track evaluation.
// Floats/masks remain byte ranges in Source().data; scale admission is explicit.
class SplineTransformCodec {
  public:
    static SplineTransformCodec Load(const SplineTransformInput& input,
        SplineScalePolicy scalePolicy = SplineScalePolicy::RequireIdentityScale,
        const SplineTransformLimits& limits = SplineTransformLimits());
    std::size_t TrackCount() const;
    std::size_t FrameCount() const;
    std::size_t BlockCount() const;
    double Duration() const;
    double FrameDuration() const;
    bool HasNonIdentityScale() const;
    const SplineTransformInput& Source() const;
    const SplineTransformBlockInfo& Block(std::size_t index) const;
    // Finite source seconds, clamped to endpoints; direct B-spline evaluation,
    // no 24-Hz baking/SLERP or integer-FPS inference. Output owns its values.
    std::vector<SplineLocalTransform> Sample(double seconds) const;
    SplineLocalTransform SampleTrack(double seconds, std::size_t track) const;
  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit SplineTransformCodec(std::shared_ptr<const Impl> impl) : impl_(impl) {}
};
} // namespace wolf2009
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SPLINE_CLIP_DATA_H
#define WOLF2009_SPLINE_CLIP_DATA_H
#include "SplineTransformCodec.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace wolf2009 {
enum class SplineClipFailure { InvalidData, Unsupported, LimitExceeded, NotFound, Ambiguous };
class SplineClipError : public std::runtime_error {
  public:
    SplineClipError(SplineClipFailure failure,const std::string& text) : std::runtime_error(text),failure_(failure) {}
    SplineClipFailure Failure() const { return failure_; }
  private:
    SplineClipFailure failure_;
};
struct SplineClipDataLimits {
    std::size_t sourceBytes=64u*1024u*1024u,objects=100000,fixups=1000000;
    std::size_t variants=256,clips=4096,bindings=4096,arrayEntries=1000000;
    std::size_t nameBytes=4096,totalNameBytes=1024u*1024u;
    std::size_t compressedClipBytes=64u*1024u*1024u;
    // Conservative charge for output and temporary selected-reader metadata;
    // canonical HavokPack's transient fixup/type maps have separate limits.
    std::size_t decodedBytes=128u*1024u*1024u,work=128u*1024u*1024u;
};
struct SplinePointerFact {
    std::uint32_t sourceSlot=0,rawWord=0,targetOffset=0;
    bool hasDataFixup=false;
    std::string targetType; // A fact, never a rig/default/binding decision.
};
struct SplineArrayFact {
    SplinePointerFact pointer;
    std::uint32_t count=0,rawCapacityAndFlags=0;
};
enum class SplineBindingFactsState { Absent, Uninterpreted };
struct SplineBindingFact {
    std::uint32_t tableIndex=0,objectOffset=0;
    std::string sourceType;
    std::uint32_t rawObjectWord=0;
    bool hasKnownLayout=false; // Other layouts remain bounded pointer/type facts.
    std::array<unsigned char,24> rawHeader{{}};
    SplinePointerFact animation;
    std::vector<std::int16_t> rawTransformTrackToBoneIndices,rawFloatTrackToSlotIndices;
    std::uint8_t rawBlendHint=0;
};
// Owning selected source facts and independent transform-codec input. Exact
// names, not casefolded paths or inferred skeleton/track identities. Neither
// absent nor nonempty bindings qualify a rig, additive math, reference pose,
// missing-track fill, native palette or gameplay behavior.
struct SplineClipData {
    std::string name;
    std::size_t sourceBytes=0;
    std::uint64_t sourceFingerprint=0; // FNV-1a bytes, local identity, not authentication.
    std::uint32_t rootOffset=0,animationContainerOffset=0,nameContainerOffset=0;
    std::uint32_t nameTableIndex=0,animationObjectOffset=0;
    std::array<unsigned char,40> rawAnimationContainerHeader{{}};
    std::array<unsigned char,8> rawNameContainerHeader{{}};
    std::array<unsigned char,128> rawAnimationHeader{{}};
    std::uint32_t skeletonCount=0,animationCount=0,bindingCount=0,attachmentCount=0,skinCount=0;
    SplinePointerFact bindingTable,extractedMotion,annotationTracks;
    std::uint32_t annotationTrackCount=0,rawAnimationType=0,rawEndian=0;
    SplineBindingFactsState bindingState=SplineBindingFactsState::Absent;
    std::vector<SplineBindingFact> bindings;
    SplineArrayFact blockOffsets,floatBlockOffsets,transformOffsets,floatOffsets,compressedData;
    SplineTransformInput input;
    // Owns all returned data. The original source vector can be destroyed.
    // Unknown selected-clip endian/layout/class or malformed consumed ranges
    // fail explicitly. Unknown binding layouts remain uninterpreted facts.
    // Unsupported transform quantizations remain codec-level typed refusals.
    static SplineClipData Load(const std::vector<unsigned char>& hkxa,const std::string& exactName,
                              const SplineClipDataLimits& limits=SplineClipDataLimits());
};
} // namespace wolf2009
#endif

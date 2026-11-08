// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_AUDIO_BANK_H
#define WOLF2009_SOURCE_AUDIO_BANK_H
#include "Archive.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace wolf2009 {
typedef std::vector<unsigned char> SourceAudioBankBytes;
enum class SourceAudioBankPolicy { Unspecified=0, SndsOnlyEncodedV1=1 };
struct SourceAudioBankRequest {
    SourceAudioBankPolicy policy=SourceAudioBankPolicy::Unspecified;
    // Caller-established virtual filename, never a host path or inferred locale.
    std::string exactFilename;
    bool expectedSourceIdentityKnown=false;
    std::uint64_t expectedSourceBytes=0,expectedSourceFingerprint=0;
};
struct SourceAudioBankLimits {
    Limits archive;
    std::size_t sourceBytes=64u<<20,chunkBytes=64u<<20;
    // Sum of all Archive::Scan inflation plus the complete chunk inflation
    // repeated by each ReadEntry call. Checked before those operation phases.
    std::size_t inflatedWorkBytes=256u<<20;
    // Bound repeated compressed-input reads too: a valid zlib stream need not
    // compress well and can contain many empty/no-output blocks.
    std::size_t compressedWorkBytes=256u<<20;
    std::size_t chunks=128,members=4096,memberBytes=16u<<20,encodedBytes=64u<<20;
    std::size_t filenameBytes=4096,nameBytes=4096,totalNameBytes=4u<<20;
    // Conservative owned-data charge, including the immutable source snapshot,
    // member bytes and manifest storage. Not an allocator/RSS measurement.
    std::size_t retainedBytes=160u<<20;
};
struct SourceAudioBankIdentity {
    SourceAudioBankPolicy policy=SourceAudioBankPolicy::Unspecified;
    std::string rawFilename;
    std::array<unsigned char,4> rawVersionHeader{{}};
    std::uint32_t version=0;
    std::uint64_t sourceBytes=0,sourceFingerprint=0,totalEncodedBytes=0;
};
struct SourceAudioBankChunk {
    std::size_t ordinal=0,firstMember=0,members=0;
    std::string rawType;
    std::uint64_t headerOffset=0,dataOffset=0;
    std::uint32_t compressedBytes=0,inflatedBytes=0;
    std::uint64_t compressedFingerprint=0;
    std::array<unsigned char,12> rawHeader{{}};
};
struct SourceAudioBankMember {
    std::size_t chunkOrdinal=0,entryOrdinal=0;
    std::string rawType,rawName,lookupName;
    // Offset in the inflated chunk, not a file offset into compressed bytes.
    std::uint64_t payloadOffset=0,encodedLength=0,encodedFingerprint=0;
    SourceAudioBankBytes encoded;
};
// Immutable owning DATA plan. Successful construction validates the complete
// archive/manifest and owns encoded bytes; it makes NO native readiness,
// PCM/decode, registration, playback, mixer or gameplay claim. Codec format is
// intentionally opaque. A native consumer must separately qualify every entry.
class SourceAudioBankPlan {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit SourceAudioBankPlan(std::shared_ptr<const Impl>);
  public:
    SourceAudioBankPlan(){}
    bool Valid()const{return bool(impl_);} // Data ownership only.
    static SourceAudioBankPlan Prepare(std::istream& input,const std::string& suppliedExactFilename,
        const SourceAudioBankRequest&,const SourceAudioBankLimits& = SourceAudioBankLimits());
    const SourceAudioBankRequest& Request()const;
    const SourceAudioBankIdentity& Identity()const;
    const SourceAudioBankBytes& SourceBytes()const;
    const std::vector<SourceAudioBankChunk>& Chunks()const;
    const std::vector<SourceAudioBankMember>& Members()const;
    // Byte-exact raw identity lookup. No extension rewrite or source selection.
    const SourceAudioBankMember* FindExact(const std::string& rawType,const std::string& rawName)const;
};
// FNV-1a64 values above are local byte/cache identities, never authentication.
// Compare actual encoded bytes as well when attaching a strict decoder/cache
// receipt; an equal fingerprint alone is not ownership or completion proof.
}
#endif

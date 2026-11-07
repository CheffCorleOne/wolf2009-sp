// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_SOUND_DECODE_RECEIPT_H
#define WOLF2009_SOURCE_SOUND_DECODE_RECEIPT_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
class idSoundCache;
class idStr;

enum class WolfSourceMp3ReceiptCode {
    Unverified, Verified, InvalidRequest, CacheAbsent, UnsupportedCache,
    IdentityMismatch, LimitExceeded, InvalidFraming, CodecError, ShortDecode,
    CacheChanged
};
struct WolfSourceMp3ReceiptRequest {
    // Already canonical native cache name, not a source SFX or playback alias.
    std::string exactNativeSampleName;
    const std::vector<unsigned char>* expectedEncoded = NULL;
    std::uint64_t expectedFingerprint = 0;
    std::size_t maximumEncodedBytes = 16u * 1024u * 1024u;
    std::uint64_t maximumDecodedSamples = 32u * 1024u * 1024u;
};

// An owning observation of one already registered native cache sample.
// This is not a cache handle, mount lease, bank Ready flag or playback ACK.
// A consumer must retain its own bank/cache lifetime and generation separately.
class WolfSourceMp3DecodeReceipt {
    WolfSourceMp3ReceiptCode code_ = WolfSourceMp3ReceiptCode::Unverified;
    std::string name_;
    std::vector<unsigned char> encoded_;
    std::uint64_t fingerprint_ = 0, samples_ = 0, frames_ = 0;
    friend bool Wolf2009_InspectSourceMp3Cache(idSoundCache&,
        const WolfSourceMp3ReceiptRequest&,WolfSourceMp3DecodeReceipt&,idStr&);
  public:
    WolfSourceMp3ReceiptCode Code() const { return code_; }
    const std::string& NativeSampleName() const { return name_; }
    const std::vector<unsigned char>& EncodedSnapshot() const { return encoded_; }
    std::uint64_t EncodedFingerprint() const { return fingerprint_; }
    std::uint64_t DecodedSamples() const { return samples_; }
    std::uint64_t CompleteFrames() const { return frames_; }
};

// Core-private; call on the engine resource thread outside a sound decoder
// critical section, while cache and expected bytes are stable for this call.
// Does not load, mutate, purge or play a cache sample and does not change cvars.
// Only resident encoded MPEG1 LayerIII mono/44100 cache samples are admitted.
bool Wolf2009_InspectSourceMp3Cache(idSoundCache&,
    const WolfSourceMp3ReceiptRequest&,WolfSourceMp3DecodeReceipt&,idStr&);
#endif

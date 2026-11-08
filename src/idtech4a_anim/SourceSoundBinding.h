// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_SOUND_BINDING_H
#define WOLF2009_SOURCE_SOUND_BINDING_H
#include "SourceSound.h"
#include <memory>
#include <string>
class idFile;
class idStr;
namespace wolf2009 {class CompiledDeclarationNamespace;}
// Private readonly filesystem dispatch. Only idFile operations cross API8.
bool Wolf2009_IsSourceSoundRequest(const char*);
idFile* Wolf2009_OpenSourceSoundSample(const char*);
#ifdef WOLF2009_SOURCE_SOUND_CORE
// Canonical v1 sample capability for an explicit mounted SNDS resource
// (wolf2009/<mount>/snds/<name>.mp3) with a known epoch and exact expected
// bytes. Opening it still re-reads and re-checks those bytes. Core only.
bool Wolf2009_SourceSoundSampleCapability(const std::string& resourcePath,unsigned long long epoch,
    unsigned long long bytes,unsigned long long fingerprint,std::string& capability,std::string& reason);
#endif
#ifdef WOLF2009_SOURCE_SOUND_GAME
enum WolfSourceSoundMixPolicy {WolfSourceSoundMix_Unspecified=0,WolfSourceSoundMix_NativeWorldUnitGainV1=1,
    WolfSourceSoundMix_NativePlayerUnitGainV1=2};
struct WolfSourceSoundRequest {
    std::string sourceSound,sampleResource;
    // Explicit native mixer profile. Source category/rms/duration are retained;
    // retail category attenuation, RMS normalization and bus routing are not
    // inferred from those metadata fields.
    WolfSourceSoundMixPolicy mixPolicy=WolfSourceSoundMix_Unspecified;
};
class WolfSourceSoundBinding {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceSoundBinding(std::unique_ptr<Impl>);
    friend std::shared_ptr<const WolfSourceSoundBinding> Wolf2009_BindSourceSound(
        const wolf2009::CompiledDeclarationNamespace&,const WolfSourceSoundRequest&,idStr&);
  public:
    ~WolfSourceSoundBinding();
    const wolf2009::SourceSound& Source() const;
    const char* NativeAlias() const;
    const char* SampleCapability() const;
    bool MatchesNative(idStr& reason) const;
    WolfSourceSoundBinding(const WolfSourceSoundBinding&)=delete;
    WolfSourceSoundBinding& operator=(const WolfSourceSoundBinding&)=delete;
};
std::shared_ptr<const WolfSourceSoundBinding> Wolf2009_BindSourceSound(
    const wolf2009::CompiledDeclarationNamespace&,const WolfSourceSoundRequest&,idStr& reason);
#endif
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_SOUND_H
#define WOLF2009_SOURCE_SOUND_H
#include "CompiledDeclarationCatalog.h"
#include <cstdint>
#include <string>

namespace wolf2009 {
enum class SourceSoundProfile { Unspecified, SingleWorldSampleV1 };
// Owning source facts. Preparing this profile does not load a sample or assert
// equivalence between the retail category mixer and another sound backend.
struct SourceSound {
    CompiledCatalogEntry declaration;
    std::string name,category,samplePath;
    std::uint32_t durationMs=0,rms=0;
};
SourceSound PrepareSourceSound(const CompiledCatalogEntry&,SourceSoundProfile);
// Only the caller-selected SNDS resource may bind an authored WAV reference.
// This checks their literal names after ASCII/slash folding and removal of
// the final filename extension. It never searches or rewrites other assets.
bool SourceSoundSampleMatches(const SourceSound&,const std::string& exactSndsName);
}
#endif

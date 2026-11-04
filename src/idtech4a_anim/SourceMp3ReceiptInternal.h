// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_MP3_RECEIPT_INTERNAL_H
#define WOLF2009_SOURCE_MP3_RECEIPT_INTERNAL_H
#include "SourceMp3Strict.h"
class idSoundSample;
// Optional private DecodeMP3 accounting, never part of its ordinary callers.
typedef wolfSourceMp3Strict::Read WolfSourceMp3ReadAccounting;
struct WolfSourceMp3NativeCounts {std::uint64_t frames=0,samples=0;};
// Defined in the already compiled native MP3 codec TU. Caller holds
// CRITICAL_SECTION_ONE; no filesystem/cache mutation or nested lock happens.
WolfSourceMp3NativeCounts Wolf2009_InspectSourceMp3BytesLocked(
    const idSoundSample&,std::uint64_t maximumSamples);
#endif

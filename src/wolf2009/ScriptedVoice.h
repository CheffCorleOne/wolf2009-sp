// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SCRIPTED_VOICE_H
#define WOLF2009_SCRIPTED_VOICE_H
#include "StreamMounts.h"
#include "DeclarationText.h"

namespace wolf2009 {

struct ScriptedVoiceLimits : DeclarationLimits {
    std::size_t sampleBytes = 16u * 1024u * 1024u;
};

struct ScriptedVoiceBinding {
    std::string definitionName, lipSyncKey, soundAlias, lipSyncName, language;
    std::string detailPack, localizedPack, sampleName, subtitleKey;
    std::uint32_t declaredDurationMs = 0;
};

// Strict observed flat entityDef + localized lipSync profile. Resolve the
// explicit lipsync_<name> -> <name>_stream relationship and declared sample;
// reject inheritance, duplicate declarations/keys and unsupported selected
// commands. Language is caller-selected, never inferred from host settings.
ScriptedVoiceBinding ResolveScriptedVoice(const std::string &definitionText,
                                          const std::string &lipSyncText,
                                          const std::string &definitionName,
                                          const std::string &lipSyncKey, const std::string &language,
                                          const ScriptedVoiceLimits &limits = ScriptedVoiceLimits());

// Read only the exact assets.pk4 members def/voice/scripted_voice.def and
// lipsync/vo_<language>/vo.lipsync through the existing CRC-checking ZIP reader.
// Case/slash aliases in member names must still identify a unique member.
ScriptedVoiceBinding ReadScriptedVoiceBinding(std::istream &input, const ZipArchive &assets,
                                              const std::string &definitionName,
                                              const std::string &lipSyncKey, const std::string &language,
                                              const ScriptedVoiceLimits &limits = ScriptedVoiceLimits());

struct PreparedScriptedVoice {
    ScriptedVoiceBinding binding;
    std::string mountId, mountedPackageName;
    StreamResource resource;
    // Owns one fully read sample, so readiness survives a mount's later removal.
    // Keep this object across ticks; it is not necessary to reread per frame.
    // Readable encoded bytes are not proof of audio decoding, playback or Speak.
    std::vector<unsigned char> bytes;
};

// Caller supplies the package->mount association established by its read-only
// stream provider. Require that exact package spelling to match localizedPack;
// select only that mount, leaving cross-mount precedence and locale fallback
// explicit. Missing/ambiguous/empty/excessive samples throw. This prepares only
// the requested key, not every line in a voice set or a later AI Speak action.
PreparedScriptedVoice PrepareScriptedVoice(const ScriptedVoiceBinding &binding,
                                           const StreamMounts &mounts, const std::string &mountId,
                                           const std::string &mountedPackageName,
                                           const ScriptedVoiceLimits &limits = ScriptedVoiceLimits());

int RunScriptedVoiceTests();

} // namespace wolf2009
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptedVoice.h"
#include <map>
#include <stdexcept>

namespace wolf2009 {
namespace {

void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

using detail::Selected;
using detail::Token;
using detail::Virtual;
using detail::ZipText;

std::string Language(const std::string &input) {
    const auto value = Virtual(input);
    Need(value.size() <= 32, "Voice language token too long");
    for (char c : value)
        Need(c >= 'a' && c <= 'z', "Unsupported voice language token");
    return value;
}

std::uint32_t Number(const std::string &value) {
    Need(!value.empty(), "Empty voice duration");
    std::uint32_t result = 0;
    for (unsigned char c : value) {
        Need(c >= '0' && c <= '9' && result <= (86400000u - unsigned(c - '0')) / 10,
             "Invalid/excessive declared voice duration");
        result = result * 10 + unsigned(c - '0');
    }
    Need(result > 0, "Zero declared voice duration");
    return result;
}

std::map<std::string, std::vector<std::string>> Fields(const std::vector<Token> &tokens, bool lipSync) {
    std::map<std::string, std::vector<std::string>> result;
    for (std::size_t p = 0; p < tokens.size();) {
        Need(!tokens[p].brace, "Nested selected voice declaration is unsupported");
        const auto key = Virtual(tokens[p++].value);
        const std::size_t count = lipSync && key == "visemes" ? 2 : 1;
        Need(count <= tokens.size() - p, "Missing voice declaration field value");
        std::vector<std::string> values;
        for (std::size_t i = 0; i < count; ++i) {
            Need(!tokens[p].brace, "Nested selected voice field is unsupported");
            values.push_back(tokens[p++].value);
        }
        Need(result.insert(std::make_pair(key, values)).second, "Duplicate selected voice field");
    }
    return result;
}

const std::string &Field(const std::map<std::string, std::vector<std::string>> &fields,
                         const std::string &key) {
    const auto found = fields.find(key);
    Need(found != fields.end() && found->second.size() == 1, "Missing selected voice field");
    return found->second[0];
}

} // namespace

ScriptedVoiceBinding ResolveScriptedVoice(const std::string &definitionText,
                                          const std::string &lipSyncText,
                                          const std::string &definitionName,
                                          const std::string &lipSyncKey, const std::string &language,
                                          const ScriptedVoiceLimits &limits) {
    ScriptedVoiceBinding binding;
    binding.definitionName = Virtual(definitionName);
    binding.lipSyncKey = Virtual(lipSyncKey);
    binding.language = Language(language);
    Need(binding.lipSyncKey.compare(0, 8, "lipsync_") == 0 && binding.lipSyncKey.size() > 8,
         "Expected explicit lipsync_ voice key");
    binding.lipSyncName = binding.lipSyncKey.substr(8);
    const auto voice =
        Fields(Selected(definitionText, "entityDef", binding.definitionName, limits), false);
    for (const auto &entry : voice)
        Need(entry.first == "detailpack" || entry.first.compare(0, 8, "lipsync_") == 0,
             "Unsupported selected scripted voice field/inheritance");
    binding.detailPack = Virtual(Field(voice, "detailpack"));
    Need(binding.detailPack.size() > 4 &&
             binding.detailPack.compare(binding.detailPack.size() - 4, 4, ".spk") == 0,
         "Expected explicit scripted voice SPK");
    binding.localizedPack =
        binding.detailPack.substr(0, binding.detailPack.size() - 4) + "_vo_" + binding.language + ".spk";
    binding.soundAlias = Virtual(Field(voice, binding.lipSyncKey));
    Need(binding.soundAlias == binding.lipSyncName + "_stream",
         "Unsupported scripted voice alias relationship");
    const auto lip = Fields(Selected(lipSyncText, "lipSync", binding.lipSyncName, limits), true);
    for (const auto &entry : lip)
        Need(entry.first == "category" || entry.first == "subtitles" || entry.first == "sample" ||
                 entry.first == "durationms" || entry.first == "stream" || entry.first == "visemes",
             "Unsupported selected lipSync field");
    Need(Field(lip, "stream") == "1", "Only explicit streamed lipSync is supported");
    binding.sampleName = Virtual(Field(lip, "sample"));
    Need(binding.sampleName.compare(0, 6, "sound/") == 0, "Expected explicit sound/ voice resource");
    binding.subtitleKey = Field(lip, "subtitles");
    Need(binding.subtitleKey.compare(0, 5, "#str_") == 0, "Unsupported voice subtitle reference");
    binding.declaredDurationMs = Number(Field(lip, "durationms"));
    const auto visemes = lip.find("visemes");
    if (visemes != lip.end())
        Need(visemes->second.size() == 2 && Language(visemes->second[0]) == binding.language,
             "Localized lipSync viseme language mismatch");
    return binding;
}

ScriptedVoiceBinding ReadScriptedVoiceBinding(std::istream &input, const ZipArchive &assets,
                                              const std::string &definitionName,
                                              const std::string &lipSyncKey, const std::string &language,
                                              const ScriptedVoiceLimits &limits) {
    const auto locale = Language(language);
    const auto defs = ZipText(input, assets, "def/voice/scripted_voice.def", limits);
    const auto lip = ZipText(input, assets, "lipsync/vo_" + locale + "/vo.lipsync", limits);
    return ResolveScriptedVoice(defs, lip, definitionName, lipSyncKey, locale, limits);
}

PreparedScriptedVoice PrepareScriptedVoice(const ScriptedVoiceBinding &binding,
                                           const StreamMounts &mounts, const std::string &mountId,
                                           const std::string &mountedPackageName,
                                           const ScriptedVoiceLimits &limits) {
    const auto id = Virtual(mountId), package = Virtual(mountedPackageName);
    Need(Virtual(binding.definitionName) == binding.definitionName &&
             binding.lipSyncKey == "lipsync_" + Virtual(binding.lipSyncName) &&
             binding.soundAlias == binding.lipSyncName + "_stream" &&
             Language(binding.language) == binding.language &&
             Virtual(binding.detailPack) == binding.detailPack && binding.detailPack.size() > 4 &&
             binding.detailPack.compare(binding.detailPack.size() - 4, 4, ".spk") == 0 &&
             binding.localizedPack == binding.detailPack.substr(0, binding.detailPack.size() - 4) +
                                          "_vo_" + binding.language + ".spk",
         "Inconsistent prepared voice binding");
    Need(package == binding.localizedPack,
         "Mounted voice package does not match resolved locale/source");
    Need(Virtual(binding.sampleName) == binding.sampleName &&
             binding.sampleName.compare(0, 6, "sound/") == 0 && binding.declaredDurationMs > 0 &&
             binding.declaredDurationMs <= 86400000u,
         "Invalid prepared voice binding");
    const auto lookup = mounts.Find("SNDS", binding.sampleName);
    const StreamResource *resource = NULL;
    for (const auto &match : lookup.matches) {
        if (match.mountId != id)
            continue;
        Need(!resource, "Ambiguous voice resource in selected mount");
        resource = &match;
    }
    Need(resource, "Missing voice resource in selected mount");
    Need(resource->size > 0 && resource->size <= limits.sampleBytes, "Voice sample byte budget");
    PreparedScriptedVoice prepared;
    prepared.binding = binding;
    prepared.mountId = id;
    prepared.mountedPackageName = package;
    prepared.resource = *resource;
    prepared.bytes = mounts.Read(*resource);
    Need(prepared.bytes.size() == resource->size, "Prepared voice read size changed");
    return prepared;
}

} // namespace wolf2009

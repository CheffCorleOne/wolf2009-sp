// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptedVoice.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;

void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

void Refuses(const std::function<void()> &action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    Need(rejected, "Unsupported voice input accepted");
}

const char *Def = "// synthetic\nentityDef voice_case {\n"
                  "\"detailPack\" \"streampacks/voice_case.spk\"\n"
                  "\"lipsync_line_a\" \"line_a_stream\"\n} \n";
const char *Lip = "lipSync line_a { category \"Voice Over\" subtitles \"#str_123\" "
                  "sample \"sound/vo/actor/line_a\" durationMS 1234 stream 1 "
                  "visemes \"english\" \"AA3q x1k\" }\n";

ScriptedVoiceBinding Binding() {
    return ResolveScriptedVoice(Def, Lip, "voice_case", "lipsync_line_a", "english");
}

std::string Replace(std::string input, const std::string &before, const std::string &after) {
    const auto at = input.find(before);
    Need(at != std::string::npos, "Voice fixture replacement missing");
    input.replace(at, before.size(), after);
    return input;
}

void U32(Bytes &bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(static_cast<unsigned char>(value >> (i * 8)));
}

void U16(Bytes &bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
}

Bytes Zip(const std::vector<std::pair<std::string, std::string>> &files) {
    Bytes bytes, central;
    for (const auto &file : files) {
        const auto offset = bytes.size(), length = file.second.size();
        const auto crc = crc32(crc32(0, Z_NULL, 0),
                               reinterpret_cast<const unsigned char *>(file.second.data()), unsigned(length));
        U32(bytes, 0x04034b50u);
        U16(bytes, 10);
        U16(bytes, 0);
        U16(bytes, 0);
        U16(bytes, 0);
        U16(bytes, 0);
        U32(bytes, std::uint32_t(crc));
        U32(bytes, std::uint32_t(length));
        U32(bytes, std::uint32_t(length));
        U16(bytes, std::uint16_t(file.first.size()));
        U16(bytes, 0);
        bytes.insert(bytes.end(), file.first.begin(), file.first.end());
        bytes.insert(bytes.end(), file.second.begin(), file.second.end());
        U32(central, 0x02014b50u);
        U16(central, 10);
        U16(central, 10);
        U16(central, 0);
        U16(central, 0);
        U16(central, 0);
        U16(central, 0);
        U32(central, std::uint32_t(crc));
        U32(central, std::uint32_t(length));
        U32(central, std::uint32_t(length));
        U16(central, std::uint16_t(file.first.size()));
        U16(central, 0);
        U16(central, 0);
        U16(central, 0);
        U16(central, 0);
        U32(central, 0);
        U32(central, std::uint32_t(offset));
        central.insert(central.end(), file.first.begin(), file.first.end());
    }
    const auto offset = bytes.size();
    bytes.insert(bytes.end(), central.begin(), central.end());
    U32(bytes, 0x06054b50u);
    U16(bytes, 0);
    U16(bytes, 0);
    U16(bytes, std::uint16_t(files.size()));
    U16(bytes, std::uint16_t(files.size()));
    U32(bytes, std::uint32_t(central.size()));
    U32(bytes, std::uint32_t(offset));
    U16(bytes, 0);
    return bytes;
}

void Pad(Bytes &bytes) {
    while (bytes.size() % 4)
        bytes.push_back(0);
}

Bytes Package(const std::vector<std::pair<std::string, Bytes>> &files) {
    Bytes payload;
    U32(payload, static_cast<std::uint32_t>(files.size()));
    for (const auto &file : files) {
        payload.insert(payload.end(), file.first.begin(), file.first.end());
        payload.push_back(0);
        U32(payload, static_cast<std::uint32_t>(file.second.size()));
        U32(payload, static_cast<std::uint32_t>(file.second.size()));
    }
    Pad(payload);
    for (const auto &file : files) {
        payload.insert(payload.end(), file.second.begin(), file.second.end());
        Pad(payload);
    }
    uLongf size = compressBound(static_cast<uLong>(payload.size()));
    Bytes compressed(size);
    Need(compress2(compressed.data(), &size, payload.data(), static_cast<uLong>(payload.size()),
                   Z_BEST_SPEED) == Z_OK, "Synthetic voice package compression failed");
    compressed.resize(size);
    Bytes bytes;
    U32(bytes, 300);
    bytes.insert(bytes.end(), {'S', 'D', 'N', 'S'});
    U32(bytes, static_cast<std::uint32_t>(payload.size()));
    U32(bytes, static_cast<std::uint32_t>(compressed.size()));
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
    return bytes;
}

struct Store {
    std::map<std::string, Bytes> files;
    unsigned opens = 0;
    StreamProvider Provider() {
        return [this](const std::string &id) -> std::unique_ptr<std::istream> {
            ++opens;
            const auto found = files.find(id);
            if (found == files.end())
                return {};
            return std::unique_ptr<std::istream>(new std::istringstream(
                std::string(found->second.begin(), found->second.end()), std::ios::binary | std::ios::in));
        };
    }
};

} // namespace

int RunScriptedVoiceTests() {
    unsigned tests = 0;
    const auto test = [&tests](const std::function<void()> &action) {
        action();
        ++tests;
    };
    test([] {
        const auto binding = Binding();
        Need(binding.definitionName == "voice_case" && binding.lipSyncName == "line_a" &&
                 binding.soundAlias == "line_a_stream" && binding.declaredDurationMs == 1234 &&
                 binding.localizedPack == "streampacks/voice_case_vo_english.spk" &&
                 binding.sampleName == "sound/vo/actor/line_a", "Voice resolution mismatch");
    });
    test([] {
        const auto binding = ResolveScriptedVoice(
            std::string(Def) + "entityDef unused { nested { opaque \"value\" } }\n", Lip,
            "VOICE_CASE", "LIPSYNC_LINE_A", "ENGLISH");
        Need(binding.sampleName == "sound/vo/actor/line_a", "Bounded unrelated declaration rejected");
    });
    for (const auto &change : std::vector<std::pair<std::string, std::string>>{
             {"line_a_stream", "other_stream"}, {"detailPack", "inherit"},
             {"voice_case.spk", "voice_case"}, {"streampacks/", "../streampacks/"},
             {"lipsync_line_a", "lipsync_other"},
             {"\"detailPack\"", "\"detailPack\" \"duplicate.spk\" \"detailPack\""}}) {
        test([change] {
            Refuses([&] { ResolveScriptedVoice(Replace(Def, change.first, change.second), Lip,
                                              "voice_case", "lipsync_line_a", "english"); });
        });
    }
    for (const auto &change : std::vector<std::pair<std::string, std::string>>{
             {"stream 1", "stream 0"}, {"stream 1", "stream 1 text2speech 1"},
             {"1234", "0"}, {"1234", "86400001"}, {"1234", "1.2"},
             {"sound/vo", "../sound/vo"}, {"sound/vo", "other/vo"},
             {"#str_123", "literal subtitle"}, {"english", "russian"},
             {"sample", "unknown"}, {"line_a {", "line_a { nested { x y }"},
             {"durationMS 1234", "durationMS 1234 durationMS 10"},
             {"\"AA3q x1k\"", "\"AA3q x1k"}}) {
        test([change] {
            Refuses([&] { ResolveScriptedVoice(Def, Replace(Lip, change.first, change.second),
                                              "voice_case", "lipsync_line_a", "english"); });
        });
    }
    test([] {
        Refuses([] { ResolveScriptedVoice(std::string(Def) + Def, Lip, "voice_case", "lipsync_line_a",
                                          "english"); });
        Refuses([] { ResolveScriptedVoice(Def, std::string(Lip) + Lip, "voice_case", "lipsync_line_a",
                                          "english"); });
    });
    for (unsigned limit = 0; limit < 5; ++limit) {
        test([limit] {
            ScriptedVoiceLimits limits;
            if (limit == 0) limits.sourceBytes = 10;
            if (limit == 1) limits.tokens = 3;
            if (limit == 2) limits.tokenBytes = 3;
            if (limit == 3) limits.nesting = 0;
            if (limit == 4) limits.declarations = 0;
            Refuses([&] { ResolveScriptedVoice(Def, Lip, "voice_case", "lipsync_line_a", "english",
                                              limits); });
        });
    }
    for (const char *language : {"../english", "english/fallback", "english_2", ""}) {
        test([language] {
            Refuses([&] { ResolveScriptedVoice(Def, Lip, "voice_case", "lipsync_line_a", language); });
        });
    }
    test([] {
        Store store;
        store.files["voice"] = Package({{"sound/vo/actor/line_a", {'o', 'n', 'e'}}});
        store.files["other"] = Package({{"sound/vo/actor/line_a", {'t', 'w', 'o'}}});
        StreamMounts mounts(store.Provider());
        mounts.Mount("voice");
        mounts.Mount("other");
        Need(mounts.Find("SNDS", "sound/vo/actor/line_a").status == StreamLookupStatus::Ambiguous,
             "Fixture did not preserve cross-mount ambiguity");
        const auto prepared = PrepareScriptedVoice(Binding(), mounts, "VOICE",
                                                   "STREAMPACKS\\VOICE_CASE_VO_ENGLISH.SPK");
        Need(prepared.bytes == Bytes({'o', 'n', 'e'}) && prepared.resource.mountId == "voice",
             "Explicit voice source did not win over unrelated mount");
        const auto opens = store.opens;
        mounts.Unmount("voice");
        Need(prepared.bytes == Bytes({'o', 'n', 'e'}) && store.opens == opens,
             "Owned prepared sample depended on mount lifetime or per-frame read");
        Refuses([&] { mounts.Read(prepared.resource); });
    });
    for (unsigned altered = 0; altered < 6; ++altered) {
        test([altered] {
            Store store;
            const Bytes payload = altered == 2 ? Bytes() : Bytes({'a', 'b', 'c'});
            const std::string name = altered == 1 ? "sound/other" : "sound/vo/actor/line_a";
            std::vector<std::pair<std::string, Bytes>> files{{name, payload}};
            if (altered == 3)
                files.push_back({"SOUND/VO/ACTOR/LINE_A", payload});
            store.files["voice"] = Package(files);
            StreamMounts mounts(store.Provider());
            mounts.Mount("voice");
            ScriptedVoiceLimits limits;
            if (altered == 4)
                limits.sampleBytes = 2;
            Refuses([&] {
                PrepareScriptedVoice(Binding(), mounts, altered == 0 ? "missing" : "voice",
                    altered == 5 ? "streampacks/voice_case_vo_russian.spk" :
                                   "streampacks/voice_case_vo_english.spk", limits);
            });
        });
    }
    for (unsigned variant = 0; variant < 5; ++variant) {
        test([variant] {
            std::vector<std::pair<std::string, std::string>> files{
                {"def/voice/scripted_voice.def", Def}, {"lipsync/vo_english/vo.lipsync", Lip}};
            if (variant == 1)
                files[1].first = "lipsync/vo_russian/vo.lipsync";
            if (variant == 2)
                files.push_back({"DEF/VOICE/SCRIPTED_VOICE.DEF", Def});
            auto bytes = Zip(files);
            if (variant == 3)
                bytes[30 + files[0].first.size()] ^= 1;
            std::istringstream input(std::string(bytes.begin(), bytes.end()),
                                      std::ios::binary | std::ios::in);
            const auto zip = ZipArchive::Scan(input);
            ScriptedVoiceLimits limits;
            if (variant == 4)
                limits.sourceBytes = 10;
            const auto read = [&] {
                return ReadScriptedVoiceBinding(input, zip, "voice_case", "lipsync_line_a", "english",
                                                 limits);
            };
            if (variant == 0)
                Need(read().declaredDurationMs == 1234, "ZIP declaration read failed");
            else
                Refuses([&] { read(); });
        });
    }
    test([] {
        Store store;
        store.files["voice"] = Package({{"sound/vo/actor/line_a", {'a', 'b', 'c'}}});
        StreamMounts mounts(store.Provider());
        mounts.Mount("voice");
        auto binding = Binding();
        binding.language = "russian";
        Refuses([&] { PrepareScriptedVoice(binding, mounts, "voice", binding.localizedPack); });
        binding = Binding();
        store.files.erase("voice");
        Refuses([&] { PrepareScriptedVoice(binding, mounts, "voice", binding.localizedPack); });
    });
    std::cout << "Scripted voice tests passed: " << tests
              << " (owned readable sample; no audio decoding/playback claim)\n";
    return 0;
}

} // namespace wolf2009

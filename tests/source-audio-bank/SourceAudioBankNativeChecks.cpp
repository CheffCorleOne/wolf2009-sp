// SPDX-License-Identifier: GPL-3.0-or-later
// Core QA only. Exercises the whole-bank service through its ordinary private
// filesystem capability in the running engine: real SPK snapshot, mount/pin,
// native sound-cache registration and strict per-member decode receipts.
// Fixtures: the owner's original English pack plus derived/authored packs that
// the test script stages into the isolated device directory only.
#include "SourceAudioBankNative.h"
#include "ScriptedVoiceBank.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "sound/snd_local.h"
#include "framework/Wolf2009Streams.h"

namespace {
int checks = 0, refusalsChecked = 0;
void Need(bool value, const std::string& why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
struct Closer {
    void operator()(idFile* f) const { if (f) fileSystem->CloseFile(f); }
};
typedef std::unique_ptr<idFile, Closer> File;
typedef wolf2009::ScriptedVoiceBankManifest Manifest;
std::string Text(idFile* f) {
    Need(f != NULL, "bank capability opened nothing");
    std::string out(std::size_t(f->Length()), '\0');
    Need(f->Seek(0, FS_SEEK_SET) == 0 && f->Read(&out[0], int(out.size())) == int(out.size()), "short manifest read");
    return out;
}
File Request(const char* locale, const char* definition) {
    return File(fileSystem->OpenFileRead(va("wolf2009/_sound/bank/v1/request/%s/%s", locale, definition), false));
}
File Status(std::uint64_t generation) {
    return File(fileSystem->OpenFileRead(va("wolf2009/_sound/bank/v1/status/%llu", (unsigned long long)generation), false));
}
Manifest Parse(idFile* f) { return wolf2009::ParseScriptedVoiceBankManifest(Text(f)); }
int CacheObjects() { return soundSystemLocal.soundCache ? soundSystemLocal.soundCache->GetNumObjects() : -1; }
const idSoundSample* Cached(const std::string& name) {
    const idSoundSample* found = NULL;
    for (int i = 0; i < soundSystemLocal.soundCache->GetNumObjects(); ++i) {
        const idSoundSample* s = soundSystemLocal.soundCache->GetObject(i);
        if (s && name == s->name.c_str()) { Need(found == NULL, "duplicate cache name"); found = s; }
    }
    return found;
}
// Every native cache sample carrying this generation's mount label.
void GenerationSamples(std::uint64_t generation, int& total, int& resident) {
    const std::string marker = std::string("/wolf2009/vobank") + std::to_string((unsigned long long)generation) + "/";
    total = resident = 0;
    for (int i = 0; i < soundSystemLocal.soundCache->GetNumObjects(); ++i) {
        const idSoundSample* s = soundSystemLocal.soundCache->GetObject(i);
        if (!s || std::string(s->name.c_str()).find(marker) == std::string::npos) continue;
        ++total;
        resident += !s->purged;
    }
}
bool MountPresent(const std::string& label) {
    unsigned long long epoch = 0, resources = 0, chunks = 0;
    return Wolf2009_MountInventory(label.c_str(), epoch, resources, chunks);
}
void NeedReadyCache(const Manifest& m) {
    for (const auto& member : m.members) {
        const idSoundSample* s = Cached(member.cacheName);
        Need(s && !s->purged && !s->defaultSound && !s->hardwareBuffer && s->nonCacheData &&
                 s->objectInfo.wFormatTag == WAVE_FORMAT_TAG_MP3 && s->objectInfo.nChannels == 1 &&
                 s->objectInfo.nSamplesPerSec == 44100 && std::uint64_t(s->objectMemSize) == member.bytes &&
                 std::uint64_t(s->objectSize) == member.samples,
             "registered native cache sample differs from its receipt: " + member.rawName);
        Need(member.samples == member.frames * 1152, "receipt PCM count differs from complete mono frame coverage");
    }
}
std::uint64_t ExpectFailure(const char* definition, const char* expected, int& total, int& resident) {
    const unsigned long long before = Wolf2009_SourceAudioBankRefusals();
    const unsigned live = Wolf2009_LiveSourceAudioBanks();
    File f = Request("english", definition);
    const Manifest m = Parse(f.get());
    Need(m.state == Manifest::State::Failed && m.members.empty() && !m.reason.empty(),
         std::string("invalid bank was not refused as a whole: ") + definition);
    Need(m.reason.find(expected) != std::string::npos,
         std::string("bank refusal reason differs for ") + definition + ": " + m.reason);
    Need(Wolf2009_SourceAudioBankRefusals() == before + 1 && Wolf2009_LiveSourceAudioBanks() == live,
         "refused bank retained an owner");
    Need(!MountPresent("vobank" + std::to_string((unsigned long long)m.generation)), "refused bank left its mount");
    Need(!Status(m.generation), "refused generation answers a status query");
    GenerationSamples(m.generation, total, resident);
    Need(resident == 0, "refused bank left a resident provisional cache sample");
    ++refusalsChecked;
    printf("WOLF2009_AUDIO_BANK_REFUSAL definition=%s generation=%llu provisional_registered=%d resident=%d reason=%s\n",
           definition, (unsigned long long)m.generation, total, resident, m.reason.c_str());
    return m.generation;
}
std::vector<unsigned char> ReadBytes(const char* path) {
    File f(fileSystem->OpenFileRead(path, false));
    Need(f.get() != NULL && f->Length() > 0, std::string("QA input absent: ") + path);
    std::vector<unsigned char> out(std::size_t(f->Length()));
    Need(f->Read(out.data(), int(out.size())) == int(out.size()), "short QA input read");
    return out;
}
void WriteBytes(const char* path, const std::vector<unsigned char>& bytes) {
    idFile* f = fileSystem->OpenFileWrite(path);
    Need(f != NULL, std::string("cannot stage QA retry pack ") + path);
    const int written = f->Write(bytes.data(), int(bytes.size()));
    fileSystem->CloseFile(f);
    Need(written == int(bytes.size()), "short QA retry pack write");
}

void Original() {
    const int cacheBefore = CacheObjects();
    const unsigned long long registered = Wolf2009_SourceAudioBankRegistrations();
    File lease = Request("english", "scripted_voice_kc1_trn");
    const Manifest m = Parse(lease.get());
    Need(m.state == Manifest::State::Ready, "original English bank was not registered: " + m.reason);
    Need(m.profile == wolf2009::ScriptedVoiceBankProfile::Category8SndsMp3V1 && m.definition == "scripted_voice_kc1_trn" &&
             m.locale == "english" && m.package == "streampacks/scripted_voice_kc1_trn_vo_english.spk",
         "original bank identity/expansion differs");
    // Independent audit facts (header-only frame walk, archive audit and an
    // earlier first-line decode); they are expectations, not readiness inputs.
    Need(m.sourceBytes == 385486 && m.encodedBytes == 389672 && m.members.size() == 19,
         "original bank differs from the audited 19-member package");
    Need(m.members[0].rawName == "sound/vo/kreisau_agent_1/trn_0505_ka1" && m.members[0].bytes == 47350 &&
             m.members[0].samples == 218880,
         "first original member identity/PCM differs from the independent reference");
    std::uint64_t frames = 0, samples = 0;
    for (const auto& member : m.members) {
        frames += member.frames;
        samples += member.samples;
        Need(member.rawName.compare(0, 25, "sound/vo/kreisau_agent_1/") == 0, "unexpected original member prefix");
        printf("WOLF2009_AUDIO_BANK_RECEIPT index=%u name=%s bytes=%llu frames=%llu pcm=%llu fnv=%016llx\n",
               unsigned(member.index), member.rawName.c_str(), (unsigned long long)member.bytes,
               (unsigned long long)member.frames, (unsigned long long)member.samples,
               (unsigned long long)member.fingerprint);
    }
    Need(frames == 1622 && samples == 1868544, "original whole-bank frame/PCM totals differ from the audit");
    Need(CacheObjects() == cacheBefore + 19 && Wolf2009_SourceAudioBankRegistrations() == registered + 1,
         "original bank did not register exactly its 19 native cache samples");
    NeedReadyCache(m);
    const std::string label = m.mountLabel;
    Need(MountPresent(label), "ready bank has no retained mount");
    // Duplicate request joins the same bank: no second mount/decode/registration.
    {
        File second = Request("english", "scripted_voice_kc1_trn");
        const Manifest joined = Parse(second.get());
        Need(joined.state == Manifest::State::Ready && joined.generation == m.generation && joined.leases == 2 &&
                 CacheObjects() == cacheBefore + 19 && Wolf2009_SourceAudioBankRegistrations() == registered + 1,
             "duplicate request repeated whole-bank work");
    }
    Need(Wolf2009_LiveSourceAudioBanks() == 1, "joined lease release ended the shared bank");
    // A pinned bank mount cannot be removed by an ordinary unmount.
    Need(!Wolf2009_Unmount(label.c_str()) && MountPresent(label), "pinned bank mount was removed");
    {
        File status = Status(m.generation);
        const Manifest observed = Parse(status.get());
        Need(observed.state == Manifest::State::Ready && observed.generation == m.generation &&
                 observed.mountEpoch == m.mountEpoch,
             "live bank status is not ready");
    }
    lease.reset();
    Need(Wolf2009_LiveSourceAudioBanks() == 0 && !MountPresent(label), "last lease did not release the bank mount");
    Need(!Status(m.generation), "released generation still answers status (stale generation accepted)");
    // Retry/re-request: a fresh generation with a fresh mount epoch.
    File again = Request("english", "scripted_voice_kc1_trn");
    const Manifest fresh = Parse(again.get());
    Need(fresh.state == Manifest::State::Ready && fresh.generation > m.generation && fresh.mountEpoch != m.mountEpoch &&
             fresh.members.size() == 19 && fresh.mountLabel != label,
         "re-request did not produce a fresh complete generation");
    NeedReadyCache(fresh);
    // Ordinary level-load purge of unreferenced samples revokes the bank.
    soundSystemLocal.soundCache->BeginLevelLoad();
    soundSystemLocal.soundCache->EndLevelLoad();
    {
        File status = Status(fresh.generation);
        const Manifest observed = Parse(status.get());
        Need(observed.state == Manifest::State::Lost && !observed.reason.empty(), "purged bank still reported ready");
        printf("WOLF2009_AUDIO_BANK_LOST generation=%llu reason=%s\n", (unsigned long long)fresh.generation,
               observed.reason.c_str());
    }
    // A new request never revives a lost bank; it registers a new generation.
    {
        File replacement = Request("english", "scripted_voice_kc1_trn");
        const Manifest r = Parse(replacement.get());
        Need(r.state == Manifest::State::Ready && r.generation > fresh.generation && r.members.size() == 19,
             "request after loss did not register a fresh bank");
        NeedReadyCache(r);
        Need(Wolf2009_LiveSourceAudioBanks() == 2, "lost and replacement banks are not separately owned");
    }
    again.reset();
    Need(Wolf2009_LiveSourceAudioBanks() == 0, "lost/replacement bank owners leaked");
    printf("WOLF2009_AUDIO_BANK_ORIGINAL_OK generations=%llu,%llu members=19 receipts=19 frames=%llu pcm=%llu "
           "source_bytes=%llu encoded_bytes=%llu shared_join=1 pinned_unmount_refused=1 stale_refused=1 purge_lost=1\n",
           (unsigned long long)m.generation, (unsigned long long)fresh.generation, (unsigned long long)frames,
           (unsigned long long)samples, (unsigned long long)m.sourceBytes, (unsigned long long)m.encodedBytes);
}

void Refusals() {
    int total = 0, resident = 0;
    // Authored valid silence: a whole bank of legitimately silent samples.
    File silence = Request("english", "qa_silence");
    const Manifest s = Parse(silence.get());
    Need(s.state == Manifest::State::Ready && s.members.size() == 3, "authored silent bank refused: " + s.reason);
    NeedReadyCache(s);
    const int cacheWithForeign = CacheObjects();
    // Unqualified requests are refused before any I/O.
    {
        File f = Request("german", "scripted_voice_kc1_trn");
        const Manifest m = Parse(f.get());
        Need(m.state == Manifest::State::Failed && m.reason.find("locale") != std::string::npos, "unqualified locale admitted");
        ++refusalsChecked;
    }
    {
        File f = Request("english", "Scripted_Voice_KC1_TRN");
        Need(Parse(f.get()).state == Manifest::State::Failed, "uppercase definition admitted");
        ++refusalsChecked;
    }
    ExpectFailure("qa_missing", "absent", total, resident);
    Need(total == 0, "missing pack registered samples");
    ExpectFailure("qa_unknown", "unexpected typed chunk", total, resident);
    Need(total == 0, "unknown-type pack registered samples before refusal");
    ExpectFailure("qa_corrupt", "", total, resident);
    Need(total == 0, "corrupt pack registered samples before refusal");
    ExpectFailure("qa_truncated", "", total, resident);
    Need(total == 0, "truncated pack registered samples before refusal");
    ExpectFailure("qa_alias", "alias", total, resident);
    Need(total == 0, "ambiguous pack registered samples before refusal");
    ExpectFailure("qa_dupcase", "duplicate", total, resident);
    Need(total == 0, "case-colliding pack registered samples before refusal");
    ExpectFailure("qa_badcodec", "member sound/qa/c", total, resident);
    Need(total == 3, "bad-codec member was not reached after two good registrations");
    // Original bytes with only the final member shortened by one byte: 18 real
    // receipts and native registrations, then one failure refuses everything.
    ExpectFailure("qa_lastbad", "member sound/vo/kreisau_agent_1/trn_5510_ka1", total, resident);
    Need(total == 19, "partial failure did not occur after 18 native registrations");
    // The previously registered foreign bank is untouched by these refusals.
    {
        File status = Status(s.generation);
        Need(status && Parse(status.get()).state == Manifest::State::Ready, "refusals damaged an unrelated live bank");
        NeedReadyCache(s);
        Need(CacheObjects() >= cacheWithForeign, "refusal removed native cache entries");
    }
    // Retry after restoring valid input.
    const auto bad = ReadBytes("streampacks/qa_lastbad_vo_english.spk");
    const auto good = ReadBytes("streampacks/scripted_voice_kc1_trn_vo_english.spk");
    WriteBytes("streampacks/qa_retry_vo_english.spk", bad);
    const std::uint64_t failedGeneration = ExpectFailure("qa_retry", "trn_5510_ka1", total, resident);
    WriteBytes("streampacks/qa_retry_vo_english.spk", good);
    {
        File retry = Request("english", "qa_retry");
        const Manifest r = Parse(retry.get());
        Need(r.state == Manifest::State::Ready && r.generation > failedGeneration && r.members.size() == 19 &&
                 r.sourceBytes == 385486,
             "retry after restoring valid input did not register the whole bank: " + r.reason);
        NeedReadyCache(r);
        printf("WOLF2009_AUDIO_BANK_RETRY failed_generation=%llu ready_generation=%llu members=%u\n",
               (unsigned long long)failedGeneration, (unsigned long long)r.generation, unsigned(r.members.size()));
    }
    silence.reset();
    Need(Wolf2009_LiveSourceAudioBanks() == 0, "QA bank owners leaked");
}
} // namespace

bool Wolf2009_RunSourceAudioBankChecks() {
    checks = refusalsChecked = 0;
    try {
        Need(soundSystemLocal.soundCache != NULL, "audio bank QA requires the native sound cache (s_noSound 0)");
        Need(Wolf2009_LiveSourceAudioBanks() == 0, "audio bank QA started with live banks");
        Original();
        Refusals();
        printf("WOLF2009_AUDIO_BANK_NATIVE_OK checks=%d refusals=%d registrations=%llu refused=%llu live=%u playback=0\n",
               checks, refusalsChecked, Wolf2009_SourceAudioBankRegistrations(), Wolf2009_SourceAudioBankRefusals(),
               Wolf2009_LiveSourceAudioBanks());
        fflush(stdout);
        return true;
    } catch (const std::exception& error) {
        fprintf(stderr, "WOLF2009_AUDIO_BANK_NATIVE_FAIL checks=%d reason=%s\n", checks, error.what());
        return false;
    }
}

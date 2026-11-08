// SPDX-License-Identifier: GPL-3.0-or-later
// Independent native whole-bank registration for the explicit scripted-voice
// Category8SndsMp3V1 profile. No retail streaming implementation is copied.
#include "SourceAudioBankNative.h"
#include "ScriptedVoiceBank.h"
#include "SourceAudioBank.h"
#include "SourceSoundBinding.h"
#include "SourceSoundDecodeReceipt.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "sound/snd_local.h"
#include "framework/Wolf2009Streams.h"

namespace {
const char BankPrefix[] = "wolf2009/_sound/bank/v1/";
const char RequestPrefix[] = "wolf2009/_sound/bank/v1/request/";
const char StatusPrefix[] = "wolf2009/_sound/bank/v1/status/";
const std::size_t MaximumPathBytes = 4608, MaximumPackageBytes = 64u << 20;
unsigned liveBanks = 0;
unsigned long long registrations = 0, refusals = 0, nextGeneration = 1;
bool busy = false;

void Need(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
struct DecoderLock {
    DecoderLock() { Sys_EnterCriticalSection(CRITICAL_SECTION_ONE); }
    ~DecoderLock() { Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE); }
};
bool StartsWith(const char* path, const char* prefix) {
    if (!path) return false;
    for (std::size_t i = 0; prefix[i]; ++i)
        if (path[i] != prefix[i]) return false;
    return true;
}
std::string Bounded(const char* value) {
    Need(value != NULL, "audio bank request path is absent");
    std::size_t n = 0;
    while (n <= MaximumPathBytes && value[n]) ++n;
    Need(n <= MaximumPathBytes, "audio bank request path budget");
    return std::string(value, n);
}
std::uint64_t Fingerprint(const unsigned char* bytes, std::size_t size) {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}
std::uint64_t Decimal(const std::string& value) {
    Need(!value.empty() && value.size() <= 20 && (value.size() == 1 || value[0] != '0'), "noncanonical audio bank generation");
    std::uint64_t out = 0;
    for (char c : value) {
        Need(c >= '0' && c <= '9', "invalid audio bank generation");
        Need(out <= (UINT64_MAX - std::uint64_t(c - '0')) / 10, "audio bank generation overflow");
        out = out * 10 + std::uint64_t(c - '0');
    }
    return out;
}
// The native cache is engine-owned. Look samples up by exact canonical name on
// every observation; no idSoundSample pointer is retained across calls.
idSoundSample* FindCached(const std::string& name) {
    idSoundCache* cache = soundSystemLocal.soundCache;
    if (!cache) return NULL;
    const int count = cache->GetNumObjects();
    Need(count >= 0 && count <= 65536, "native sound cache lookup budget exceeded");
    idSoundSample* found = NULL;
    for (int i = 0; i < count; ++i) {
        const idSoundSample* candidate = cache->GetObject(i);
        if (candidate && name == candidate->name.c_str()) {
            Need(found == NULL, "native sound cache holds a duplicate canonical name");
            found = const_cast<idSoundSample*>(candidate);
        }
    }
    return found;
}

struct Bank {
    struct Member {
        std::string rawName, cacheName;
        std::uint64_t bytes = 0, fingerprint = 0, frames = 0, samples = 0;
        const wolf2009::SourceAudioBankMember* source = NULL; // owned by plan
    };
    wolf2009::ScriptedVoicePack pack;
    std::uint64_t generation = 0;
    std::string label, key, lostReason;
    wolf2009::SourceAudioBankPlan plan;
    unsigned long long epoch = 0;
    bool mounted = false, pinned = false, ready = false, lost = false;
    std::vector<Member> members;
    std::vector<std::string> provisional;
    unsigned leases = 0;

    Bank() { ++liveBanks; }
    ~Bank() {
        Release();
        --liveBanks;
    }
    Bank(const Bank&) = delete;
    Bank& operator=(const Bank&) = delete;
    // Releases this bank's own mount claims. Native cache samples registered by
    // a published bank stay with the engine cache (a consumer may still mix
    // them); its stale capability epoch prevents any later reload.
    void Release() {
        if (pinned) {
            if (!Wolf2009_UnpinMount(label.c_str(), epoch))
                common->Warning("Wolf2009 audio bank %llu could not unpin %s", (unsigned long long)generation, label.c_str());
            pinned = false;
        }
        if (mounted) {
            if (!Wolf2009_Unmount(label.c_str()))
                common->Warning("Wolf2009 audio bank %llu could not unmount %s", (unsigned long long)generation, label.c_str());
            mounted = false;
        }
    }
    // Before publication only: purge this attempt's own provisional samples.
    // Their names carry this attempt's fresh mount epoch, so they cannot be a
    // foreign registration. Unrelated cache entries are never touched.
    void PurgeProvisional() {
        DecoderLock lock;
        for (const auto& name : provisional) {
            idSoundSample* sample = FindCached(name);
            if (sample && !sample->purged) sample->PurgeSoundSample();
        }
        provisional.clear();
    }
    bool Verify(std::string& why) {
        why.clear();
        if (lost) {
            why = lostReason;
            return false;
        }
        try {
            Need(ready && pinned && mounted, "audio bank is not a published whole bank");
            unsigned long long current = 0, resources = 0, chunks = 0;
            Need(Wolf2009_MountInventory(label.c_str(), current, resources, chunks) && current == epoch &&
                     resources == plan.Members().size() && chunks == plan.Chunks().size(),
                 "audio bank mount epoch/inventory ended or changed");
            Need(soundSystemLocal.soundCache != NULL, "native sound cache was shut down");
            DecoderLock lock;
            for (const auto& member : members) {
                const idSoundSample* sample = FindCached(member.cacheName);
                Need(sample != NULL, "audio bank native cache sample disappeared");
                Need(!sample->purged && !sample->defaultSound && !sample->hardwareBuffer && sample->nonCacheData &&
                         sample->objectInfo.wFormatTag == WAVE_FORMAT_TAG_MP3 &&
                         std::uint64_t(sample->objectSize) == member.samples &&
                         std::uint64_t(sample->objectMemSize) == member.bytes &&
                         std::memcmp(sample->nonCacheData, member.source->encoded.data(), std::size_t(member.bytes)) == 0,
                     "audio bank native cache sample was purged or changed");
            }
            return true;
        } catch (const std::exception& error) {
            lost = true;
            lostReason = error.what();
            why = lostReason;
            return false;
        }
    }
    wolf2009::ScriptedVoiceBankManifest Manifest(wolf2009::ScriptedVoiceBankManifest::State state) const {
        wolf2009::ScriptedVoiceBankManifest m;
        m.state = state;
        m.profile = pack.profile;
        m.definition = pack.definition;
        m.locale = pack.locale;
        m.package = pack.package;
        m.generation = generation;
        if (state == wolf2009::ScriptedVoiceBankManifest::State::Lost) m.reason = lostReason;
        m.sourceBytes = plan.Identity().sourceBytes;
        m.sourceFingerprint = plan.Identity().sourceFingerprint;
        m.mountLabel = label;
        m.mountEpoch = epoch;
        m.leases = leases ? leases : 1;
        m.encodedBytes = plan.Identity().totalEncodedBytes;
        for (std::size_t i = 0; i < members.size(); ++i) {
            wolf2009::ScriptedVoiceBankMember out;
            out.index = i;
            out.rawName = members[i].rawName;
            out.cacheName = members[i].cacheName;
            out.bytes = members[i].bytes;
            out.fingerprint = members[i].fingerprint;
            out.frames = members[i].frames;
            out.samples = members[i].samples;
            m.members.push_back(out);
        }
        return m;
    }
};
std::map<std::string, std::weak_ptr<Bank>> byKey;
std::map<std::uint64_t, std::weak_ptr<Bank>> byGeneration;

class ManifestFile : public idFile {
    const std::string name, text;
    std::shared_ptr<Bank> owner; // null for refusals/status observations
    std::size_t position = 0;
  public:
    ManifestFile(std::string path, std::string body, std::shared_ptr<Bank> bank)
        : name(std::move(path)), text(std::move(body)), owner(std::move(bank)) {
        if (owner) ++owner->leases;
    }
    ~ManifestFile() override {
        if (owner) --owner->leases;
    }
    const char* GetName() override { return name.c_str(); }
    const char* GetFullPath() override { return name.c_str(); }
    int Length() override { return int(text.size()); }
    int Tell() override { return int(position); }
    ID_TIME_T Timestamp() override { return 0; }
    int Read(void* out, int count) override {
        if (!out || count <= 0) return 0;
        const std::size_t n = (std::min)(std::size_t(count), text.size() - position);
        if (n) std::memcpy(out, text.data() + position, n);
        position += n;
        return int(n);
    }
    int Seek(long offset, fsOrigin_t origin) override {
        const long long base = origin == FS_SEEK_SET ? 0 : origin == FS_SEEK_CUR ? (long long)position
                             : origin == FS_SEEK_END ? (long long)text.size() : -1;
        if (base < 0 || offset < -base || offset > (long long)text.size() - base) return -1;
        position = std::size_t(base + offset);
        return 0;
    }
};

std::vector<unsigned char> Snapshot(const std::string& package) {
    struct Closer {
        void operator()(idFile* f) const { if (f) fileSystem->CloseFile(f); }
    };
    std::unique_ptr<idFile, Closer> file(fileSystem->OpenFileRead(package.c_str(), false));
    Need(file.get() != NULL, "scripted voice pack is absent");
    const int length = file->Length();
    Need(length >= 4 && std::size_t(length) <= MaximumPackageBytes, "scripted voice pack size budget");
    std::vector<unsigned char> bytes(std::size_t(length), 0);
    Need(file->Read(bytes.data(), length) == length, "short scripted voice pack read");
    unsigned char extra = 0;
    Need(file->Read(&extra, 1) == 0, "scripted voice pack grew while reading");
    return bytes;
}

std::string MemberResource(const Bank& bank, const wolf2009::SourceAudioBankMember& member) {
    // lookupName is the plan's folded transport name. The SNDS VFS route
    // matches the exact name or one stripped ".mp3"; the plan has already
    // refused any pair for which that alias could be ambiguous.
    std::string name = member.lookupName;
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".mp3") != 0) name += ".mp3";
    return "wolf2009/" + bank.label + "/snds/" + name;
}

// Complete, synchronous whole-bank transaction. Throws without publishing.
void Prepare(Bank& bank) {
    Need(soundSystemLocal.soundCache != NULL, "native sound cache is disabled; no bank registration is possible");
    const auto bytes = Snapshot(bank.pack.package);
    wolf2009::SourceAudioBankRequest request;
    request.policy = wolf2009::SourceAudioBankPolicy::SndsOnlyEncodedV1;
    request.exactFilename = bank.pack.package;
    request.expectedSourceIdentityKnown = true;
    request.expectedSourceBytes = bytes.size();
    request.expectedSourceFingerprint = Fingerprint(bytes.data(), bytes.size());
    {
        std::istringstream input(std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::binary);
        bank.plan = wolf2009::SourceAudioBankPlan::Prepare(input, bank.pack.package, request);
    }
    const auto& plan = bank.plan;
    Need(Wolf2009_Mount(bank.label.c_str(), bank.pack.package.c_str()), "scripted voice pack mount refused");
    bank.mounted = true;
    Need(Wolf2009_PinMount(bank.label.c_str(), bank.epoch) && bank.epoch, "scripted voice pack mount could not be pinned");
    bank.pinned = true;
    unsigned long long epoch = 0, resources = 0, chunks = 0;
    Need(Wolf2009_MountInventory(bank.label.c_str(), epoch, resources, chunks) && epoch == bank.epoch &&
             resources == plan.Members().size() && chunks == plan.Chunks().size(),
         "mounted pack inventory differs from the complete bank data plan");
    for (const auto& member : plan.Members()) {
        Need(member.rawType == "SNDS", "audio bank profile admits only SNDS members");
        Bank::Member out;
        out.rawName = member.rawName;
        out.bytes = member.encodedLength;
        out.fingerprint = member.encodedFingerprint;
        out.source = &member;
        std::string reason;
        Need(Wolf2009_SourceSoundSampleCapability(MemberResource(bank, member), bank.epoch, out.bytes, out.fingerprint,
                                                  out.cacheName, reason),
             reason.empty() ? "audio bank member capability refused" : reason.c_str());
        // A fresh epoch makes the canonical name unique to this attempt. An
        // existing entry would be a foreign registration and is never adopted.
        {
            DecoderLock lock;
            Need(FindCached(out.cacheName) == NULL, "audio bank member name is already registered in the native cache");
        }
        bank.provisional.push_back(out.cacheName);
        idSoundSample* sample = soundSystemLocal.soundCache->FindSound(idStr(out.cacheName.c_str()), false);
        Need(sample != NULL, "native sound cache registration returned no sample");
        WolfSourceMp3ReceiptRequest receiptRequest;
        receiptRequest.exactNativeSampleName = out.cacheName;
        receiptRequest.expectedEncoded = &member.encoded;
        receiptRequest.expectedFingerprint = out.fingerprint;
        WolfSourceMp3DecodeReceipt receipt;
        idStr why;
        if (!Wolf2009_InspectSourceMp3Cache(*soundSystemLocal.soundCache, receiptRequest, receipt, why)) {
            const std::string message = "member " + member.rawName + ": " + (why.IsEmpty() ? "strict decode receipt refused" : why.c_str());
            throw std::runtime_error(message);
        }
        Need(receipt.Code() == WolfSourceMp3ReceiptCode::Verified && receipt.NativeSampleName() == out.cacheName &&
                 receipt.EncodedSnapshot() == member.encoded && receipt.DecodedSamples() && receipt.CompleteFrames(),
             "audio bank member receipt identity differs");
        out.frames = receipt.CompleteFrames();
        out.samples = receipt.DecodedSamples();
        bank.members.push_back(std::move(out));
    }
    Need(bank.members.size() == plan.Members().size() && !bank.members.empty(), "audio bank member coverage is incomplete");
    // Commit once, after every member. The registrations now belong to the bank.
    bank.provisional.clear();
    bank.ready = true;
}

std::string FailedManifest(const wolf2009::ScriptedVoicePack& pack, std::uint64_t generation, const std::string& reason,
                           const std::string& definition, const std::string& locale) {
    wolf2009::ScriptedVoiceBankManifest m;
    m.state = wolf2009::ScriptedVoiceBankManifest::State::Failed;
    m.profile = wolf2009::ScriptedVoiceBankProfile::Category8SndsMp3V1;
    m.definition = pack.definition.empty() ? definition : pack.definition;
    m.locale = pack.locale.empty() ? locale : pack.locale;
    m.package = pack.package.empty() ? "unqualified" : pack.package;
    m.generation = generation;
    m.reason = reason.empty() ? "audio bank refused" : reason;
    return wolf2009::SerializeScriptedVoiceBankManifest(m);
}

void Prune() {
    for (auto i = byKey.begin(); i != byKey.end();) i = i->second.expired() ? byKey.erase(i) : std::next(i);
    for (auto i = byGeneration.begin(); i != byGeneration.end();) i = i->second.expired() ? byGeneration.erase(i) : std::next(i);
}

idFile* OpenRequest(const std::string& path) {
    const std::string rest = path.substr(sizeof(RequestPrefix) - 1);
    const std::size_t slash = rest.find('/');
    const std::string locale = slash == std::string::npos ? std::string() : rest.substr(0, slash);
    const std::string definition = slash == std::string::npos ? rest : rest.substr(slash + 1);
    wolf2009::ScriptedVoicePack pack;
    std::uint64_t generation = 0;
    try {
        Need(slash != std::string::npos && !definition.empty() && !locale.empty(), "audio bank request needs locale/definition");
        Need(definition.size() <= 128 && locale.size() <= 64, "audio bank request field budget");
        pack = wolf2009::ExpandScriptedVoicePack(definition, locale);
        Prune();
        const std::string key = pack.package;
        const auto existing = byKey.find(key);
        if (existing != byKey.end()) {
            if (auto bank = existing->second.lock()) {
                std::string why;
                if (bank->Verify(why)) {
                    // Join: no repeated mount, decode or cache registration.
                    // The manifest reports the lease count including this one.
                    ++bank->leases;
                    const std::string text = wolf2009::SerializeScriptedVoiceBankManifest(
                        bank->Manifest(wolf2009::ScriptedVoiceBankManifest::State::Ready));
                    --bank->leases;
                    return new ManifestFile(path, text, bank);
                }
                // A lost bank stays with its current holders; a new request
                // starts a fresh generation instead of reviving it.
            }
        }
        Need(nextGeneration != 0, "audio bank generations exhausted");
        generation = nextGeneration++;
        auto bank = std::make_shared<Bank>();
        bank->pack = pack;
        bank->generation = generation;
        bank->key = key;
        bank->label = "vobank" + std::to_string((unsigned long long)generation);
        try {
            Prepare(*bank);
        } catch (...) {
            bank->PurgeProvisional();
            bank->Release();
            throw;
        }
        byKey[key] = bank;
        byGeneration[generation] = bank;
        ++registrations;
        ++bank->leases;
        const std::string text =
            wolf2009::SerializeScriptedVoiceBankManifest(bank->Manifest(wolf2009::ScriptedVoiceBankManifest::State::Ready));
        --bank->leases;
        common->Printf("Wolf2009 audio bank %llu ready: %s, %u members\n", (unsigned long long)generation,
                       pack.package.c_str(), unsigned(bank->members.size()));
        return new ManifestFile(path, text, bank);
    } catch (const std::exception& error) {
        ++refusals;
        common->Warning("Wolf2009 audio bank refused %s: %s", path.c_str(), error.what());
        if (!generation) {
            Need(nextGeneration != 0, "audio bank generations exhausted");
            generation = nextGeneration++;
        }
        return new ManifestFile(path, FailedManifest(pack, generation, error.what(), definition, locale), std::shared_ptr<Bank>());
    }
}

idFile* OpenStatus(const std::string& path) {
    const std::uint64_t generation = Decimal(path.substr(sizeof(StatusPrefix) - 1));
    Prune();
    const auto found = byGeneration.find(generation);
    if (found == byGeneration.end()) return NULL; // unknown or released: stale
    auto bank = found->second.lock();
    if (!bank) return NULL;
    std::string why;
    const bool ready = bank->Verify(why);
    const auto state = ready ? wolf2009::ScriptedVoiceBankManifest::State::Ready : wolf2009::ScriptedVoiceBankManifest::State::Lost;
    // A status file observes; it does not retain the bank.
    return new ManifestFile(path, wolf2009::SerializeScriptedVoiceBankManifest(bank->Manifest(state)), std::shared_ptr<Bank>());
}
} // namespace

bool Wolf2009_IsSourceAudioBankRequest(const char* path) { return StartsWith(path, BankPrefix); }

idFile* Wolf2009_OpenSourceAudioBank(const char* rawPath) {
    try {
        const std::string path = Bounded(rawPath);
        Need(!busy, "recursive audio bank request");
        struct Busy {
            Busy() { busy = true; }
            ~Busy() { busy = false; }
        } guard;
        if (StartsWith(path.c_str(), RequestPrefix)) return OpenRequest(path);
        if (StartsWith(path.c_str(), StatusPrefix)) return OpenStatus(path);
        throw std::runtime_error("unknown audio bank request kind");
    } catch (const std::exception& error) {
        common->Warning("Wolf2009 audio bank: %s", error.what());
        return NULL;
    }
}

unsigned Wolf2009_LiveSourceAudioBanks() { return liveBanks; }
unsigned long long Wolf2009_SourceAudioBankRegistrations() { return registrations; }
unsigned long long Wolf2009_SourceAudioBankRefusals() { return refusals; }

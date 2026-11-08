// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeScriptedVOReadiness.h"
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"

namespace {
const char RequestPrefix[] = "wolf2009/_sound/bank/v1/request/";
const char StatusPrefix[] = "wolf2009/_sound/bank/v1/status/";
const int MaximumManifestBytes = 1 << 20;
unsigned liveProviders = 0, heldLeases = 0;

struct FileCloser {
    void operator()(idFile *file) const {
        if (file) fileSystem->CloseFile(file);
    }
};
typedef std::unique_ptr<idFile, FileCloser> File;

std::string ReadAll(idFile *file) {
    if (!file) throw std::runtime_error("scripted voice bank capability unavailable");
    const int length = file->Length();
    if (length <= 0 || length > MaximumManifestBytes) throw std::runtime_error("scripted voice manifest size budget");
    std::string text(std::size_t(length), '\0');
    if (file->Seek(0, FS_SEEK_SET) != 0 || file->Read(&text[0], length) != length)
        throw std::runtime_error("short scripted voice manifest read");
    return text;
}

// Holds the core bank lease: the open idFile is the owner reference.
class NativeLease : public wolf2009::ScriptedVOBankLease {
    File file_;
    wolf2009::ScriptedVoiceBankManifest manifest_;

  public:
    NativeLease(File file, wolf2009::ScriptedVoiceBankManifest manifest)
        : file_(std::move(file)), manifest_(std::move(manifest)) {
        ++heldLeases;
    }
    ~NativeLease() override { --heldLeases; }
    const wolf2009::ScriptedVoiceBankManifest &Manifest() const override { return manifest_; }
    bool StillReady(std::string &reason) override {
        try {
            const std::string path = std::string(StatusPrefix) + std::to_string((unsigned long long)manifest_.generation);
            File status(fileSystem->OpenFileRead(path.c_str(), false));
            if (!status) {
                reason = "scripted voice bank generation is released or stale";
                return false;
            }
            const auto observed = wolf2009::ParseScriptedVoiceBankManifest(ReadAll(status.get()));
            if (observed.generation != manifest_.generation || observed.package != manifest_.package ||
                observed.mountEpoch != manifest_.mountEpoch || observed.members.size() != manifest_.members.size()) {
                reason = "scripted voice bank status identity differs from the held lease";
                return false;
            }
            if (observed.state != wolf2009::ScriptedVoiceBankManifest::State::Ready) {
                reason = observed.reason.empty() ? "scripted voice bank lost" : observed.reason;
                return false;
            }
            return true;
        } catch (const std::exception &error) {
            reason = error.what();
            return false;
        }
    }
};

class NativeHost : public wolf2009::ScriptedVOReadinessHost {
  public:
    bool OwnerLive(const wolf2009::ScriptedVOOwner &owner) override {
        if (owner.entityNumber < 0 || owner.entityNumber >= MAX_GENTITIES) return false;
        idEntity *entity = gameLocal.entities[owner.entityNumber];
        return entity && gameLocal.GetSpawnId(entity) == owner.spawnId;
    }
    std::unique_ptr<wolf2009::ScriptedVOBankLease> Acquire(const wolf2009::ScriptedVoicePack &pack,
                                                           std::string &reason) override {
        const std::string path = std::string(RequestPrefix) + pack.locale + "/" + pack.definition;
        File file(fileSystem->OpenFileRead(path.c_str(), false));
        if (!file) {
            reason = "core scripted voice bank service is unavailable";
            return std::unique_ptr<wolf2009::ScriptedVOBankLease>();
        }
        auto manifest = wolf2009::ParseScriptedVoiceBankManifest(ReadAll(file.get()));
        if (manifest.state != wolf2009::ScriptedVoiceBankManifest::State::Ready) {
            reason = manifest.reason.empty() ? "scripted voice bank refused" : manifest.reason;
            return std::unique_ptr<wolf2009::ScriptedVOBankLease>();
        }
        return std::unique_ptr<wolf2009::ScriptedVOBankLease>(new NativeLease(std::move(file), std::move(manifest)));
    }
};
} // namespace

struct WolfScriptedVOReadiness::Impl {
    NativeHost host;
    wolf2009::ScriptedVOStreamInReadiness readiness;
    Impl(const wolf2009::ScriptedVOStreamInSource &source, const wolf2009::ScriptedVOOwner &owner)
        : readiness(source, owner) {}
};

WolfScriptedVOReadiness::WolfScriptedVOReadiness(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) { ++liveProviders; }
WolfScriptedVOReadiness::~WolfScriptedVOReadiness() {
    impl_.reset();
    --liveProviders;
}

std::unique_ptr<WolfScriptedVOReadiness> WolfScriptedVOReadiness::Request(
    const wolf2009::ScriptedVOStreamInSource &source, idEntity *owner, idStr &reason) {
    reason.Clear();
    if (!owner || owner->entityNumber < 0 || owner->entityNumber >= MAX_GENTITIES ||
        gameLocal.entities[owner->entityNumber] != owner) {
        reason = "scripted voice readiness owner is absent";
        return std::unique_ptr<WolfScriptedVOReadiness>();
    }
    wolf2009::ScriptedVOOwner id;
    id.entityNumber = owner->entityNumber;
    id.spawnId = gameLocal.GetSpawnId(owner);
    id.name = owner->GetName();
    std::unique_ptr<Impl> impl(new Impl(source, id));
    if (impl->readiness.State() == wolf2009::ScriptedVOReadinessState::Failed) reason = impl->readiness.Reason().c_str();
    return std::unique_ptr<WolfScriptedVOReadiness>(new WolfScriptedVOReadiness(std::move(impl)));
}

wolf2009::ScriptedVOReadinessState WolfScriptedVOReadiness::Update() { return impl_->readiness.Update(impl_->host); }
bool WolfScriptedVOReadiness::ConsumeReady() { return impl_->readiness.ConsumeReady(impl_->host); }
void WolfScriptedVOReadiness::Cancel(const char *reason) { impl_->readiness.Cancel(reason ? reason : "cancelled by brain host"); }
void WolfScriptedVOReadiness::SetListener(wolf2009::ScriptedVOReadinessListener *listener) {
    impl_->readiness.SetListener(listener);
}
wolf2009::ScriptedVOReadinessState WolfScriptedVOReadiness::State() const { return impl_->readiness.State(); }
const char *WolfScriptedVOReadiness::StateName() const { return wolf2009::ScriptedVOReadinessStateName(State()); }
const char *WolfScriptedVOReadiness::Reason() const { return impl_->readiness.Reason().c_str(); }
const wolf2009::ScriptedVoiceBankManifest &WolfScriptedVOReadiness::Bank() const {
    return impl_->readiness.BankManifest();
}
const wolf2009::ScriptedVOStreamInReadiness &WolfScriptedVOReadiness::Readiness() const { return impl_->readiness; }

unsigned Wolf2009_LiveScriptedVOReadiness() { return liveProviders; }
unsigned Wolf2009_HeldScriptedVOBankLeases() { return heldLeases; }

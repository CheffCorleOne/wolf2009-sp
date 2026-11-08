// SPDX-License-Identifier: GPL-3.0-or-later
// Portable contract tests for pack expansion, the bank manifest and the type19
// readiness state machine. The host here is a declared test double; native
// readiness is tested separately against the real core bank service.
#include "ScriptedVoiceBank.h"
#include <functional>
#include <iostream>
#include <stdexcept>
namespace {
using namespace wolf2009;
int checks = 0, refusals = 0;
void Check(bool value, const char *why) {
    ++checks;
    if (!value) throw std::logic_error(why);
}
void Rejects(const std::function<void()> &work) {
    bool rejected = false;
    try { work(); } catch (const std::runtime_error &) { rejected = true; }
    Check(rejected, "invalid input was accepted");
    ++refusals;
}
ScriptedVoiceBankManifest Ready(std::uint64_t generation = 7) {
    ScriptedVoiceBankManifest m;
    m.state = ScriptedVoiceBankManifest::State::Ready;
    m.profile = ScriptedVoiceBankProfile::Category8SndsMp3V1;
    m.definition = "scripted_voice_kc1_trn";
    m.locale = "english";
    m.package = "streampacks/scripted_voice_kc1_trn_vo_english.spk";
    m.generation = generation;
    m.sourceBytes = 100;
    m.sourceFingerprint = 0x0123456789abcdefull;
    m.mountLabel = "vobank7";
    m.mountEpoch = 3;
    m.leases = 1;
    for (std::size_t i = 0; i < 2; ++i) {
        ScriptedVoiceBankMember member;
        member.index = i;
        member.rawName = i ? "sound/vo/b" : "sound/vo/a name";
        member.cacheName = i ? "cache/b.mp3" : "cache/a.mp3";
        member.bytes = 10 + i;
        member.fingerprint = i;
        member.frames = 2;
        member.samples = 2304;
        m.members.push_back(member);
        m.encodedBytes += member.bytes;
    }
    return m;
}
void Expansion() {
    const auto p = ExpandScriptedVoicePack("scripted_voice_kc1_trn", "english");
    Check(p.package == "streampacks/scripted_voice_kc1_trn_vo_english.spk" &&
              p.profile == ScriptedVoiceBankProfile::Category8SndsMp3V1,
          "measured common expansion differs");
    for (const char *bad : {"", "a/b", "a.spk", "Upper", "a b", "a-b"}) Rejects([&] { ExpandScriptedVoicePack(bad, "english"); });
    for (const char *bad : {"", "German", "german", "english/x"}) Rejects([&] { ExpandScriptedVoicePack("x", bad); });
}
void ManifestRoundTrip() {
    const auto m = Ready();
    const auto text = SerializeScriptedVoiceBankManifest(m);
    const auto back = ParseScriptedVoiceBankManifest(text);
    Check(back.state == m.state && back.members.size() == 2 && back.members[0].rawName == "sound/vo/a name" &&
              back.sourceFingerprint == m.sourceFingerprint && back.encodedBytes == 21 && back.mountEpoch == 3 &&
              back.reason.empty(),
          "manifest round trip lost identity");
    Check(SerializeScriptedVoiceBankManifest(back) == text, "manifest serialization is not canonical");
    ScriptedVoiceBankManifest failed;
    failed.state = ScriptedVoiceBankManifest::State::Failed;
    failed.profile = ScriptedVoiceBankProfile::Category8SndsMp3V1;
    failed.definition = "x"; failed.locale = "english"; failed.package = "streampacks/x_vo_english.spk";
    failed.generation = 9; failed.reason = "member sound/x: short";
    Check(ParseScriptedVoiceBankManifest(SerializeScriptedVoiceBankManifest(failed)).reason == failed.reason,
          "failed manifest reason lost");
    auto mutate = [&](const std::string &from, const std::string &to) {
        std::string t = text;
        const auto at = t.find(from);
        Check(at != std::string::npos, "test mutation anchor");
        t.replace(at, from.size(), to);
        Rejects([&] { ParseScriptedVoiceBankManifest(t); });
    };
    mutate("members 2", "members 3");          // member coverage
    mutate("encoded 21", "encoded 20");        // total
    mutate("state ready", "state done");       // unknown state
    mutate("reason %", "reason x");            // ready with a reason
    mutate("leases 1\n", "leases 1\nextra 1\n");
    mutate("member 1 ", "member 2 ");          // order
    mutate(" 2304 cache/b.mp3", " 0 cache/b.mp3"); // incomplete receipt
    mutate("cache/b.mp3", "cache/a.mp3");      // duplicate cache identity
    mutate("generation 7", "generation 07");
    mutate("wolf2009-scripted-voice-bank v1", "wolf2009-scripted-voice-bank v2");
    Rejects([&] { ParseScriptedVoiceBankManifest(text.substr(0, text.size() - 4)); });
    auto lostNoReason = Ready(); lostNoReason.state = ScriptedVoiceBankManifest::State::Lost;
    Rejects([&] { ParseScriptedVoiceBankManifest(SerializeScriptedVoiceBankManifest(lostNoReason)); });
}
struct Lease : ScriptedVOBankLease {
    ScriptedVoiceBankManifest m;
    bool *alive;
    int *released;
    Lease(ScriptedVoiceBankManifest value, bool *a, int *r) : m(value), alive(a), released(r) {}
    ~Lease() override { ++*released; }
    const ScriptedVoiceBankManifest &Manifest() const override { return m; }
    bool StillReady(std::string &why) override { if (!*alive) why = "purged"; return *alive; }
};
struct Host : ScriptedVOReadinessHost {
    bool owner = true, bankAlive = true, refuse = false, wrongIdentity = false, killOwnerDuringAcquire = false;
    int acquires = 0, released = 0;
    bool OwnerLive(const ScriptedVOOwner &) override { return owner; }
    std::unique_ptr<ScriptedVOBankLease> Acquire(const ScriptedVoicePack &pack, std::string &why) override {
        ++acquires;
        Check(pack.package == "streampacks/scripted_voice_kc1_trn_vo_english.spk", "host received a different pack");
        if (killOwnerDuringAcquire) owner = false;
        if (refuse) { why = "member x failed"; return nullptr; }
        auto m = Ready();
        if (wrongIdentity) m.locale = "german";
        return std::unique_ptr<ScriptedVOBankLease>(new Lease(m, &bankAlive, &released));
    }
};
ScriptedVOStreamInSource Source() {
    ScriptedVOStreamInSource s;
    s.brainResource = "wolf2009/main/BRAI/test"; s.nodeId = 29; s.definition = "scripted_voice_kc1_trn"; s.locale = "english";
    return s;
}
ScriptedVOOwner Owner() { ScriptedVOOwner o; o.entityNumber = 5; o.spawnId = 77; return o; }
struct CountListener : ScriptedVOReadinessListener {
    int calls = 0;
    void OnScriptedVOReady(const ScriptedVOStreamInReadiness &) override { ++calls; }
};
void Readiness() {
    typedef ScriptedVOReadinessState S;
    {
        Host host; ScriptedVOStreamInReadiness r(Source(), Owner());
        Check(r.State() == S::Pending && !r.ConsumeReady(host) && host.acquires == 0, "pending advanced without work");
        Check(r.Update(host) == S::Ready && host.acquires == 1 && r.HoldsBank() && r.BankManifest().generation == 7, "ready missing");
        Check(r.ConsumeReady(host) && !r.ConsumeReady(host) && r.Update(host) == S::Ready && host.acquires == 1, "not once-only");
        host.bankAlive = false;
        Check(r.Update(host) == S::Lost && r.Reason() == "purged" && !r.HoldsBank() && host.released == 1, "loss not observed");
        Check(r.Update(host) == S::Lost && host.acquires == 1, "lost state revived");
    }
    {
        Host host; ScriptedVOStreamInReadiness r(Source(), Owner());
        r.Update(host); host.owner = false;
        Check(!r.ConsumeReady(host) && r.State() == S::Cancelled && host.released == 1, "event escaped for ended owner");
    }
    {
        Host host; host.owner = false; ScriptedVOStreamInReadiness r(Source(), Owner());
        Check(r.Update(host) == S::Cancelled && host.acquires == 0, "ended owner acquired a bank");
    }
    {
        Host host; host.killOwnerDuringAcquire = true; ScriptedVOStreamInReadiness r(Source(), Owner());
        Check(r.Update(host) == S::Cancelled && host.released == 1 && !r.ConsumeReady(host), "owner end during work");
    }
    {
        Host host; host.refuse = true; ScriptedVOStreamInReadiness r(Source(), Owner());
        Check(r.Update(host) == S::Failed && r.Reason() == "member x failed" && !r.ConsumeReady(host), "refusal not failed");
        host.refuse = false;
        Check(r.Update(host) == S::Failed && host.acquires == 1, "failed request retried silently");
    }
    {
        Host host; host.wrongIdentity = true; ScriptedVOStreamInReadiness r(Source(), Owner());
        Check(r.Update(host) == S::Failed && host.released == 1, "foreign bank identity accepted");
    }
    {
        Host host; ScriptedVOStreamInReadiness r(Source(), Owner());
        r.Cancel();
        Check(r.State() == S::Cancelled && r.Update(host) == S::Cancelled && host.acquires == 0, "cancel not terminal");
    }
    {
        Host host; CountListener listener; ScriptedVOStreamInReadiness r(Source(), Owner());
        r.SetListener(&listener); r.Update(host); r.Update(host);
        Check(listener.calls == 1 && !r.ConsumeReady(host), "listener not once-only");
    }
    {
        Host host;
        auto s = Source(); s.nodeType = 18;
        ScriptedVOStreamInReadiness a(s, Owner());
        s = Source(); s.locale = "german";
        ScriptedVOStreamInReadiness b(s, Owner());
        auto o = Owner(); o.entityNumber = -1;
        ScriptedVOStreamInReadiness c(Source(), o);
        Check(a.State() == S::Failed && b.State() == S::Failed && c.State() == S::Failed &&
                  a.Update(host) == S::Failed && host.acquires == 0,
              "unqualified sources were admitted");
    }
}
} // namespace
int main() {
    try {
        Expansion();
        ManifestRoundTrip();
        Readiness();
        std::cout << "SCRIPTED_VOICE_BANK_PORTABLE_OK checks=" << checks << " refusals=" << refusals
                  << " native_bank=0 speech=0\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "SCRIPTED_VOICE_BANK_PORTABLE_FAIL checks=" << checks << " reason=" << error.what() << '\n';
        return 1;
    }
}

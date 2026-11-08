// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptedVoiceBank.h"
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
bool Plain(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
bool Safe(char c) { return Plain(c) || c == '.' || c == '/' || c == '-'; }
const char Hex[] = "0123456789abcdef";
std::string Encode(const std::string &value) {
    if (value.empty())
        return "%";
    std::string out;
    for (unsigned char c : value) {
        if (Safe(char(c))) {
            out.push_back(char(c));
        } else {
            out.push_back('%');
            out.push_back(Hex[c >> 4]);
            out.push_back(Hex[c & 15]);
        }
    }
    return out;
}
int Digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}
std::string Decode(const std::string &value) {
    if (value == "%")
        return std::string();
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c != '%') {
            Need(Safe(c), "Scripted voice manifest value has an unencoded byte");
            out.push_back(c);
            continue;
        }
        Need(i + 2 < value.size(), "Truncated scripted voice manifest escape");
        const int hi = Digit(value[i + 1]), lo = Digit(value[i + 2]);
        Need(hi >= 0 && lo >= 0, "Invalid scripted voice manifest escape");
        const char decoded = char((hi << 4) | lo);
        Need(!Safe(decoded), "Noncanonical scripted voice manifest escape");
        out.push_back(decoded);
        i += 2;
    }
    return out;
}
std::uint64_t Unsigned(const std::string &value) {
    Need(!value.empty() && value.size() <= 20 && (value.size() == 1 || value[0] != '0'),
         "Noncanonical scripted voice manifest integer");
    std::uint64_t out = 0;
    for (char c : value) {
        Need(c >= '0' && c <= '9', "Invalid scripted voice manifest integer");
        const std::uint64_t digit = std::uint64_t(c - '0');
        Need(out <= (UINT64_MAX - digit) / 10, "Scripted voice manifest integer overflow");
        out = out * 10 + digit;
    }
    return out;
}
std::uint64_t Fingerprint(const std::string &value) {
    Need(value.size() == 16, "Scripted voice manifest fingerprint width");
    std::uint64_t out = 0;
    for (char c : value) {
        const int d = Digit(c);
        Need(d >= 0, "Scripted voice manifest fingerprint encoding");
        out = (out << 4) | std::uint64_t(d);
    }
    return out;
}
std::string HexValue(std::uint64_t value) {
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[std::size_t(i)] = Hex[value & 15];
        value >>= 4;
    }
    return out;
}
ScriptedVoiceBankProfile ProfileFrom(const std::string &value) {
    Need(value == "Category8SndsMp3V1", "Unknown scripted voice bank profile");
    return ScriptedVoiceBankProfile::Category8SndsMp3V1;
}
std::vector<std::string> Split(const std::string &line) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= line.size()) {
        const std::size_t end = line.find(' ', at);
        out.push_back(line.substr(at, end == std::string::npos ? std::string::npos : end - at));
        Need(!out.back().empty(), "Scripted voice manifest has an empty field");
        if (end == std::string::npos)
            break;
        at = end + 1;
    }
    return out;
}
} // namespace

const char *ScriptedVoiceBankProfileName(ScriptedVoiceBankProfile profile) {
    return profile == ScriptedVoiceBankProfile::Category8SndsMp3V1 ? "Category8SndsMp3V1" : "Unspecified";
}
const char *ScriptedVoiceBankStateName(ScriptedVoiceBankManifest::State state) {
    switch (state) {
    case ScriptedVoiceBankManifest::State::Ready: return "ready";
    case ScriptedVoiceBankManifest::State::Failed: return "failed";
    case ScriptedVoiceBankManifest::State::Lost: return "lost";
    default: return "invalid";
    }
}
const char *ScriptedVOReadinessStateName(ScriptedVOReadinessState state) {
    switch (state) {
    case ScriptedVOReadinessState::Pending: return "pending";
    case ScriptedVOReadinessState::Ready: return "ready";
    case ScriptedVOReadinessState::Failed: return "failed";
    case ScriptedVOReadinessState::Cancelled: return "cancelled";
    case ScriptedVOReadinessState::Lost: return "lost";
    }
    return "invalid";
}

ScriptedVoicePack ExpandScriptedVoicePack(const std::string &definition, const std::string &locale) {
    Need(!definition.empty() && definition.size() <= 128, "Scripted voice definition is empty or exceeds its budget");
    // Lowercase only: the VFS mount path is case-folded while the host
    // filesystem lookup may be case-sensitive; no case rewriting is guessed.
    for (char c : definition)
        Need(Plain(c) && !(c >= 'A' && c <= 'Z'),
             "Scripted voice definition is outside the qualified lowercase no-slash/no-extension profile");
    // Only the measured English pack is qualified. vo_lang is not consulted.
    Need(locale == "english", "Scripted voice locale is not qualified by this profile");
    ScriptedVoicePack out;
    out.profile = ScriptedVoiceBankProfile::Category8SndsMp3V1;
    out.definition = definition;
    out.locale = locale;
    out.package = "streampacks/" + definition + "_vo_" + locale + ".spk";
    return out;
}

std::string SerializeScriptedVoiceBankManifest(const ScriptedVoiceBankManifest &m) {
    Need(m.state != ScriptedVoiceBankManifest::State::Invalid, "Cannot serialize an invalid scripted voice manifest");
    std::ostringstream out;
    out << "wolf2009-scripted-voice-bank v1\n";
    out << "state " << ScriptedVoiceBankStateName(m.state) << '\n';
    out << "profile " << ScriptedVoiceBankProfileName(m.profile) << '\n';
    out << "definition " << Encode(m.definition) << '\n';
    out << "locale " << Encode(m.locale) << '\n';
    out << "package " << Encode(m.package) << '\n';
    out << "generation " << m.generation << '\n';
    out << "reason " << Encode(m.reason) << '\n';
    if (m.state != ScriptedVoiceBankManifest::State::Failed) {
        out << "source " << m.sourceBytes << ' ' << HexValue(m.sourceFingerprint) << '\n';
        out << "mount " << Encode(m.mountLabel) << ' ' << m.mountEpoch << '\n';
        out << "leases " << m.leases << '\n';
        out << "encoded " << m.encodedBytes << '\n';
        out << "members " << m.members.size() << '\n';
        for (const auto &member : m.members)
            out << "member " << member.index << ' ' << Encode(member.rawName) << ' ' << member.bytes << ' '
                << HexValue(member.fingerprint) << ' ' << member.frames << ' ' << member.samples << ' '
                << Encode(member.cacheName) << '\n';
    }
    out << "end\n";
    return out.str();
}

ScriptedVoiceBankManifest ParseScriptedVoiceBankManifest(const std::string &text, std::size_t maximumBytes) {
    Need(!text.empty() && text.size() <= maximumBytes && text.back() == '\n', "Scripted voice manifest size/terminator");
    std::vector<std::string> lines;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end = text.find('\n', at);
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    Need(lines.size() >= 3 && lines.front() == "wolf2009-scripted-voice-bank v1" && lines.back() == "end",
         "Scripted voice manifest header/end");
    ScriptedVoiceBankManifest m;
    std::set<std::string> seen;
    std::size_t declared = 0;
    bool haveMembers = false;
    for (std::size_t i = 1; i + 1 < lines.size(); ++i) {
        const auto f = Split(lines[i]);
        const std::string &key = f[0];
        if (key != "member")
            Need(seen.insert(key).second, "Duplicate scripted voice manifest key");
        if (key == "state") {
            Need(f.size() == 2, "Scripted voice manifest state arity");
            m.state = f[1] == "ready" ? ScriptedVoiceBankManifest::State::Ready
                      : f[1] == "failed" ? ScriptedVoiceBankManifest::State::Failed
                      : f[1] == "lost" ? ScriptedVoiceBankManifest::State::Lost
                                       : ScriptedVoiceBankManifest::State::Invalid;
            Need(m.state != ScriptedVoiceBankManifest::State::Invalid, "Unknown scripted voice manifest state");
        } else if (key == "profile") {
            Need(f.size() == 2, "Scripted voice manifest profile arity");
            m.profile = ProfileFrom(f[1]);
        } else if (key == "definition" || key == "locale" || key == "package" || key == "reason") {
            Need(f.size() == 2, "Scripted voice manifest text arity");
            (key == "definition" ? m.definition : key == "locale" ? m.locale : key == "package" ? m.package : m.reason) =
                Decode(f[1]);
        } else if (key == "generation" || key == "leases" || key == "encoded") {
            Need(f.size() == 2, "Scripted voice manifest integer arity");
            (key == "generation" ? m.generation : key == "leases" ? m.leases : m.encodedBytes) = Unsigned(f[1]);
        } else if (key == "source") {
            Need(f.size() == 3, "Scripted voice manifest source arity");
            m.sourceBytes = Unsigned(f[1]);
            m.sourceFingerprint = Fingerprint(f[2]);
        } else if (key == "mount") {
            Need(f.size() == 3, "Scripted voice manifest mount arity");
            m.mountLabel = Decode(f[1]);
            m.mountEpoch = Unsigned(f[2]);
        } else if (key == "members") {
            Need(f.size() == 2 && !haveMembers, "Scripted voice manifest member-count arity");
            declared = std::size_t(Unsigned(f[1]));
            Need(declared <= 4096, "Scripted voice manifest member budget");
            haveMembers = true;
        } else if (key == "member") {
            Need(haveMembers && f.size() == 8 && m.members.size() < declared, "Scripted voice manifest member arity/order");
            ScriptedVoiceBankMember member;
            member.index = std::size_t(Unsigned(f[1]));
            Need(member.index == m.members.size(), "Scripted voice manifest member order");
            member.rawName = Decode(f[2]);
            member.bytes = Unsigned(f[3]);
            member.fingerprint = Fingerprint(f[4]);
            member.frames = Unsigned(f[5]);
            member.samples = Unsigned(f[6]);
            member.cacheName = Decode(f[7]);
            Need(!member.rawName.empty() && member.bytes && member.frames && member.samples && !member.cacheName.empty(),
                 "Scripted voice manifest member lacks a complete receipt");
            m.members.push_back(std::move(member));
        } else {
            Need(false, "Unknown scripted voice manifest key");
        }
    }
    for (const char *key : {"state", "profile", "definition", "locale", "package", "generation", "reason"})
        Need(seen.count(key) == 1, "Scripted voice manifest is missing a required key");
    Need(m.generation != 0 && !m.definition.empty() && !m.locale.empty() && !m.package.empty(),
         "Scripted voice manifest identity is incomplete");
    if (m.state == ScriptedVoiceBankManifest::State::Failed) {
        Need(seen.size() == 7 && m.members.empty() && !m.reason.empty(),
             "Failed scripted voice manifest carries ownership fields or lacks a reason");
    } else {
        for (const char *key : {"source", "mount", "leases", "encoded", "members"})
            Need(seen.count(key) == 1, "Ready/lost scripted voice manifest is missing ownership fields");
        Need(declared == m.members.size() && declared > 0 && m.sourceBytes && m.mountEpoch && !m.mountLabel.empty() &&
                 m.leases,
             "Ready/lost scripted voice manifest member coverage is incomplete");
        std::uint64_t total = 0;
        std::set<std::string> names, caches;
        for (const auto &member : m.members) {
            Need(names.insert(member.rawName).second && caches.insert(member.cacheName).second,
                 "Scripted voice manifest member identity is duplicated");
            Need(total <= UINT64_MAX - member.bytes, "Scripted voice manifest encoded total overflow");
            total += member.bytes;
        }
        Need(total == m.encodedBytes, "Scripted voice manifest encoded total differs");
        Need(m.state == ScriptedVoiceBankManifest::State::Lost ? !m.reason.empty() : m.reason.empty(),
             "Scripted voice manifest reason/state mismatch");
    }
    return m;
}

ScriptedVOStreamInReadiness::ScriptedVOStreamInReadiness(const ScriptedVOStreamInSource &source,
                                                         const ScriptedVOOwner &owner)
    : source_(source), owner_(owner) {
    try {
        Need(source.nodeType == 19 && source.nodeId != 0, "Readiness source is not a type19 ScriptedVOStreamIn node");
        Need(!source.brainResource.empty(), "Readiness source BRAI identity is absent");
        Need(owner.entityNumber >= 0, "Readiness owner identity is absent");
        pack_ = ExpandScriptedVoicePack(source.definition, source.locale);
    } catch (const std::exception &error) {
        state_ = ScriptedVOReadinessState::Failed;
        reason_ = error.what();
    }
}

void ScriptedVOStreamInReadiness::Terminal(ScriptedVOReadinessState state, const std::string &reason) {
    state_ = state;
    reason_ = reason;
    lease_.reset(); // releases the bank owner reference
}

void ScriptedVOStreamInReadiness::Cancel(const char *reason) {
    if (state_ == ScriptedVOReadinessState::Pending || state_ == ScriptedVOReadinessState::Ready)
        Terminal(ScriptedVOReadinessState::Cancelled, reason ? reason : "cancelled");
}

bool ScriptedVOStreamInReadiness::Recheck(ScriptedVOReadinessHost &host) {
    if (!host.OwnerLive(owner_)) {
        Terminal(ScriptedVOReadinessState::Cancelled, "readiness owner ended or was replaced");
        return false;
    }
    if (state_ == ScriptedVOReadinessState::Ready) {
        std::string why;
        if (!lease_ || !lease_->StillReady(why)) {
            Terminal(ScriptedVOReadinessState::Lost, why.empty() ? "retained bank backing ended" : why);
            return false;
        }
    }
    return true;
}

ScriptedVOReadinessState ScriptedVOStreamInReadiness::Update(ScriptedVOReadinessHost &host) {
    if (state_ != ScriptedVOReadinessState::Pending && state_ != ScriptedVOReadinessState::Ready)
        return state_;
    if (updating_) {
        Terminal(ScriptedVOReadinessState::Failed, "recursive scripted voice readiness update");
        return state_;
    }
    struct Guard {
        bool &value;
        explicit Guard(bool &v) : value(v) { value = true; }
        ~Guard() { value = false; }
    } guard(updating_);
    if (!Recheck(host))
        return state_;
    if (state_ == ScriptedVOReadinessState::Pending) {
        std::string why;
        std::unique_ptr<ScriptedVOBankLease> lease;
        try {
            lease = host.Acquire(pack_, why);
        } catch (const std::exception &error) {
            lease.reset();
            why = error.what();
        }
        if (!lease) {
            Terminal(ScriptedVOReadinessState::Failed, why.empty() ? "scripted voice bank refused" : why);
            return state_;
        }
        const auto &m = lease->Manifest();
        if (m.state != ScriptedVoiceBankManifest::State::Ready || m.profile != pack_.profile ||
            m.definition != pack_.definition || m.locale != pack_.locale || m.package != pack_.package ||
            m.members.empty()) {
            Terminal(ScriptedVOReadinessState::Failed, "bank lease identity differs from the type19 request");
            return state_;
        }
        manifest_ = m;
        lease_ = std::move(lease);
        state_ = ScriptedVOReadinessState::Ready;
        reason_.clear();
        // The owner may have ended during the synchronous native work.
        if (!Recheck(host))
            return state_;
        if (listener_ && !delivered_) {
            delivered_ = true;
            listener_->OnScriptedVOReady(*this);
        }
    }
    return state_;
}

bool ScriptedVOStreamInReadiness::ConsumeReady(ScriptedVOReadinessHost &host) {
    if (state_ != ScriptedVOReadinessState::Ready || delivered_ || updating_)
        return false;
    if (!Recheck(host))
        return false;
    delivered_ = true;
    return true;
}

} // namespace wolf2009

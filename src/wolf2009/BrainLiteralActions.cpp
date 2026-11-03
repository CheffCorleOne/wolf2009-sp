// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainLiteralActions.h"
#include <limits>
#include <set>

namespace wolf2009 {
namespace {
bool Text(const std::string &s, std::size_t maximum, bool empty) {
    if ((!empty && s.empty()) || s.size() > maximum) return false;
    for (unsigned char c : s) if (c < 32 || c > 126 || c == '@') return false;
    return true;
}
std::string Fold(std::string s) {
    for (auto &c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
bool Selector(const std::string &s, BrainLiteralSelector &out) {
    if (s.empty() || s == "Me") return true;
    if (!Text(s, 127, false)) return false;
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || s[0] == '_')) return false;
    for (unsigned char c : s)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '_' && c != '-') return false;
    // These are selector keywords, not safely inferred entity-name literals.
    const std::set<std::string> reserved = {"none", "me", "myenemy", "myleader", "mypartner",
        "player", "friend", "enemy", "actor", "usable", "waypoint", "weapon", "mover",
        "door", "light", "trigger", "tgt", "tgt_music", "tgt_remove", "tgt_use", "vehicle",
        "sound", "entity", "mytarget", "myvehicle", "mytgtoccupier", "focus_type_max"};
    if (reserved.count(Fold(s))) return false;
    out.kind = BrainLiteralSelector::Name; out.name = s;
    return true;
}
bool Duration(const std::string &s, std::int32_t &out, bool infinite) {
    if (infinite && s == "-1 0") { out = -1; return true; }
    std::int32_t value = 0;
    // Here both the integer and the source float32 value+0.5 are exact. Larger
    // timer conversion profiles need their own numeric admission proof.
    if (s.size() < 3 || s.compare(s.size() - 2, 2, " 0") ||
        !BrainCanonicalNonnegativeInteger(s.substr(0, s.size() - 2), value) || value > 8388607) return false;
    out = value; return true;
}
BrainLiteralPlan Refuse(const char *s) { BrainLiteralPlan p; p.reason = s; return p; }
} // namespace
bool BrainCanonicalNonnegativeInteger(const std::string &s, std::int32_t &out) {
    if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == '0')) return false;
    std::uint32_t value = 0;
    const std::uint32_t maximum = 2147483647u;
    for (unsigned char c : s) {
        if (c < '0' || c > '9') return false;
        const std::uint32_t d = c - '0';
        if (value > (maximum - d) / 10) return false;
        value = value * 10 + d;
    }
    out = static_cast<std::int32_t>(value);
    return true;
}
bool BrainLiteralGlobalName(const std::string &s) {
    if (s.empty() || s.size() > 63 ||
        !((s[0] >= 'a' && s[0] <= 'z') || s[0] == '_')) return false;
    for (unsigned char c : s)
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') return false;
    return true;
}
BrainLiteralPlan PrepareBrainLiteralActions(const BrainGraph &g) {
    if (g.format != BrainGraph::H1023 || g.nodes.empty() || g.nodes.size() > 256 ||
        g.records.size() > 4096 || g.outputs.size() > 2048)
        return Refuse("Unsupported literal graph format or budget");
    BrainLiteralPlan p;
    p.nodes.resize(g.nodes.size());
    std::vector<unsigned char> roles(g.records.size(), 0);
    if (g.records.empty() || g.records[0].tokens != std::vector<std::string>({"h1023", "32", "32", "32", "0"}))
        return Refuse("Unsupported literal graph header");
    roles[0] = 1;
    for (std::size_t n = 0; n < g.nodes.size(); ++n)
        for (auto o : g.nodes[n].outputs)
            if (o >= g.outputs.size() || g.outputs[o].sourceNode != n)
                return Refuse("Invalid literal node/output binding");
    for (const auto &o : g.outputs) {
        if (o.sourceNode >= g.nodes.size() || o.record >= g.records.size() || o.destinations.size() > 32)
            return Refuse("Invalid literal output record");
        std::uint32_t slots = 0;
        for (const auto &d : o.destinations) {
            if (d.slot >= 32 || d.targetNode >= g.nodes.size() || (slots & (1u << d.slot)))
                return Refuse("Invalid literal destination slot/binding");
            slots |= 1u << d.slot;
        }
    }
    for (std::size_t n = 0; n < g.nodes.size(); ++n) {
        const auto &node = g.nodes[n];
        if (node.record >= g.records.size()) return Refuse("Invalid literal node record");
        if (roles[node.record]++) return Refuse("Duplicate literal node record binding");
        const auto &t = g.records[node.record].tokens;
        if (g.records[node.record].depth != 0 || t.size() < 5 || (t.size() - 5) % 2 ||
            t[0] != std::to_string(node.type) || t[1] != std::to_string(node.id) ||
            t[2] != "0" || t[3] != "0" || t[4] != "0")
            return Refuse("Unsupported literal task header");
        auto &item = p.nodes[n]; item.type = node.type;
        const bool spawn = node.type == 1, used = node.type == 3, wait = node.type == 30;
        if (!spawn && !used && !wait && node.type != 68 && node.type != 220)
            return Refuse("Unsupported literal task type");
        std::map<std::string, std::string> fields;
        for (std::size_t i = 5; i < t.size(); i += 2)
            if (!fields.insert(std::make_pair(t[i], t[i + 1])).second)
                return Refuse("Duplicate literal task field");
        const std::string flag = "^" + std::to_string(node.type);
        std::uint32_t taskFlags = 0;
        if (!ReadBrainTaskFlags(g, n, taskFlags) || (taskFlags & ~(1u | 0x10u)))
            return Refuse("Unsupported literal task flags");
        fields.erase(flag);
        auto get = [&](const char *key, const char *fallback) {
            const auto at = fields.find(key);
            if (at == fields.end()) return std::string(fallback);
            const std::string value = at->second; fields.erase(at); return value;
        };
        const char *conditionFlag = spawn ? "^74" : used ? "^64" : wait ? "^65" : NULL;
        if (conditionFlag) {
            const auto f = get(conditionFlag, "1024");
            if (f != "1024" && f != "1025") return Refuse("Unsupported primary condition flags");
            item.inverted = f == "1025";
        }
        if (used || node.type == 68) {
            item.key = get("mUseKey", "");
            if (!Text(item.key, 1023, true)) return Refuse("Unsupported literal Use key");
        }
        if (wait) {
            if (!Duration(get("mDuration", "2000 0"), item.minimum, true) ||
                !Duration(get("mDurationMax", "0 0"), item.maximum, false) ||
                get("mActualDuration", "0") != "0" || get("mTimeSet", "0") != "0" ||
                (item.minimum != -1 && item.maximum &&
                 (item.maximum < item.minimum || static_cast<std::int64_t>(item.maximum) - item.minimum > 65535)))
                return Refuse("Unsupported literal timer expression/state");
        }
        if (node.type == 68) {
            if (!Selector(get("mActor", "Me"), item.actor) ||
                !Selector(get("mUseTarget", "Me"), item.target))
                return Refuse("Unsupported literal entity selector");
        }
        if (node.type == 220) {
            item.variable = get("mVar", "");
            if (!BrainLiteralGlobalName(item.variable) ||
                !BrainCanonicalNonnegativeInteger(get("mExpression", ""), item.value))
                return Refuse("Unsupported global name or integer expression");
        }
        if (!fields.empty()) return Refuse("Unknown literal task field");
    }
    const auto flow = MakeBrainFlowState(g);
    for (std::size_t o = 0; o < g.outputs.size(); ++o) {
        const auto &output = g.outputs[o];
        if (roles[output.record]++ || g.records[output.record].depth != 1 ||
            g.records[output.record].parentRecord != g.nodes[output.sourceNode].record)
            return Refuse("Invalid literal output ownership");
        if (output.record >= g.records.size() || !flow.outputs[o].flagsKnown ||
            !flow.outputs[o].supported || (flow.outputs[o].flags & ~(1u | 0x10u)))
            return Refuse("Unsupported literal output profile");
        std::uint32_t sum = 0;
        for (auto weight : flow.outputs[o].weights) {
            if (sum > 65535u - weight) return Refuse("Literal output weight sum budget");
            sum += weight;
        }
        for (const auto &d : output.destinations)
            if (d.targetNode >= p.nodes.size() || p.nodes[d.targetNode].type == 1 || p.nodes[d.targetNode].type == 3)
                return Refuse("Literal output cannot activate a root");
        for (auto r : output.conditions) {
            if (r >= g.records.size() || g.records[r].depth != 2 ||
                g.records[r].parentRecord != output.record || roles[r]++ ||
                g.records[r].tokens != std::vector<std::string>({"-1"}))
                return Refuse("Only literal primary condition references are admitted");
        }
    }
    // Reject orphan/deeper records even when no selected branch reaches them.
    for (std::size_t r = 0; r < g.records.size(); ++r)
        if (g.records[r].depth > 2 || roles[r] != 1) return Refuse("Nested or unbound literal records are unsupported");
    p.supported = true;
    return p;
}
bool DecodeBrainLiteralGlobals(const std::vector<unsigned char> &bytes, BrainLiteralGlobals &out,
                              std::string &reason) {
    const auto fail = [&](const char *s) { reason = s; return false; };
    if (bytes.empty() || bytes.size() > 65536) return fail("Explicit global defaults byte budget");
    const std::string text(bytes.begin(), bytes.end()), header = "wolf2009-brain-globals-v1\n";
    if (text.compare(0, header.size(), header) || text.back() != '\n')
        return fail("Explicit diagnostic globals header/terminator required");
    BrainLiteralGlobals staged;
    std::size_t start = header.size();
    while (start < text.size()) {
        const auto end = text.find('\n', start), equals = text.find('=', start);
        if (end == std::string::npos || equals == std::string::npos || equals >= end)
            return fail("Malformed explicit global row");
        const auto name = text.substr(start, equals - start);
        std::int32_t value = 0;
        if (!BrainLiteralGlobalName(name) ||
            !BrainCanonicalNonnegativeInteger(text.substr(equals + 1, end - equals - 1), value) ||
            staged.size() >= 256 || !staged.insert(std::make_pair(name, value)).second)
            return fail("Unsupported or duplicate explicit global row");
        start = end + 1;
    }
    if (staged.empty()) return fail("Explicit global defaults cannot be empty");
    out.swap(staged); reason.clear(); return true;
}
} // namespace wolf2009

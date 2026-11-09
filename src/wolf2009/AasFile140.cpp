// SPDX-License-Identifier: GPL-3.0-or-later
// Independent reader for the measured DewmAAS 1.40 layout (docs/AAS.md).
// No original code or data is embedded.
#include "AasFile140.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <utility>
namespace wolf2009 {
namespace {

std::string Str(std::size_t v) { std::ostringstream s; s << v; return s.str(); }
[[noreturn]] void Refuse(const std::string &code, const std::string &detail) { throw Aas140Refusal(code, detail); }
void Need(bool ok, const char *code, const std::string &detail) { if (!ok) Refuse(code, detail); }

// ---------------------------------------------------------------------------
// Little-endian field decoding.
std::uint16_t U16(const unsigned char *p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::int16_t I16(const unsigned char *p) { return std::int16_t(U16(p)); }
std::uint32_t U32(const unsigned char *p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::int32_t I32(const unsigned char *p) { std::uint32_t u = U32(p); std::int32_t v; std::memcpy(&v, &u, 4); return v; }
float F32(const unsigned char *p) { std::uint32_t u = U32(p); float v; std::memcpy(&v, &u, 4); return v; }

// ---------------------------------------------------------------------------
// Token reader. Punctuation `{ } ( ) =` and quoted strings are single tokens;
// everything else is a run of non-space, non-punctuation bytes.
class Reader {
  public:
    Reader(const unsigned char *b, std::size_t n) : bytes(b), size(n) {}
    std::size_t pos = 0;
    void SkipSpace() { while (pos < size && IsSpace(bytes[pos])) ++pos; }
    bool AtEnd() { SkipSpace(); return pos >= size; }
    std::string Token() {
        SkipSpace();
        Need(pos < size, "truncated", "unexpected end of file at byte " + Str(pos));
        const unsigned char c = bytes[pos];
        if (IsPunct(c)) { ++pos; return std::string(1, char(c)); }
        if (c == '"') {
            const std::size_t start = ++pos;
            while (pos < size && bytes[pos] != '"' && bytes[pos] != '\n') ++pos;
            Need(pos < size && bytes[pos] == '"', "token", "unterminated string at byte " + Str(start));
            std::string s(reinterpret_cast<const char *>(bytes + start), pos - start);
            ++pos;
            return "\"" + s + "\"";
        }
        const std::size_t start = pos;
        while (pos < size && !IsSpace(bytes[pos]) && !IsPunct(bytes[pos]) && bytes[pos] != '"') {
            Need(bytes[pos] >= 0x21 && bytes[pos] < 0x7f, "token", "non-text byte in a token at byte " + Str(pos));
            ++pos;
        }
        return std::string(reinterpret_cast<const char *>(bytes + start), pos - start);
    }
    void Expect(const std::string &want, const char *code) {
        const std::size_t at = pos;
        const std::string got = Token();
        Need(got == want, code, "expected '" + want + "' but found '" + got + "' near byte " + Str(at));
    }
    long long Integer(const char *code, long long lo, long long hi) {
        const std::size_t at = pos;
        const std::string t = Token();
        bool ok = !t.empty() && t.size() <= 12;
        for (std::size_t i = 0; ok && i < t.size(); ++i) ok = (t[i] >= '0' && t[i] <= '9') || (i == 0 && t[i] == '-' && t.size() > 1);
        Need(ok, code, "expected an integer but found '" + t + "' near byte " + Str(at));
        const long long v = std::strtoll(t.c_str(), NULL, 10);
        Need(v >= lo && v <= hi, code, "integer " + t + " out of range near byte " + Str(at));
        return v;
    }
    double Number(const char *code) {
        const std::size_t at = pos;
        const std::string t = Token();
        bool ok = !t.empty() && t.size() < 40;
        for (std::size_t i = 0; ok && i < t.size(); ++i)
            ok = (t[i] >= '0' && t[i] <= '9') || t[i] == '-' || t[i] == '+' || t[i] == '.' || t[i] == 'e' || t[i] == 'E';
        char *end = NULL;
        const double v = ok ? std::strtod(t.c_str(), &end) : 0;
        Need(ok && end && *end == 0 && std::isfinite(v), code, "expected a number but found '" + t + "' near byte " + Str(at));
        return v;
    }
    const unsigned char *bytes;
    std::size_t size;
  private:
    static bool IsSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    static bool IsPunct(unsigned char c) { return c == '{' || c == '}' || c == '(' || c == ')' || c == '='; }
};

// ---------------------------------------------------------------------------
// Settings
const char *const kSettingKeys[] = {
    "bboxes", "usePatches", "writeBrushMap", "playerFlood", "allowSwimReachabilities",
    "allowFlyReachabilities", "generateTacticalFeatures", "fileExtension", "gravity",
    "maxStepHeight", "maxBarrierHeight", "maxWaterJumpHeight", "maxFallHeight", "minFloorCos",
    "maxStandHeight", "maxCrouchHeight", "maxProneHeight", "maxGapLength", "tt_barrierJump",
    "tt_transCrouch", "tt_transProne", "tt_waterJump", "tt_startWalkOffLedge", "tt_gapJump"};
const std::size_t kSettingCount = sizeof(kSettingKeys) / sizeof(kSettingKeys[0]);

void ParseVector(Reader &r, float out[3], std::string &raw) {
    r.Expect("(", "settings-value");
    raw = "(";
    for (int i = 0; i < 3; ++i) {
        const std::size_t at = r.pos; r.SkipSpace();
        const std::size_t begin = r.pos;
        out[i] = float(r.Number("settings-value"));
        raw += (i ? " " : "") + std::string(reinterpret_cast<const char *>(r.bytes + begin), r.pos - begin);
        (void)at;
    }
    r.Expect(")", "settings-value");
    raw += ")";
}

Aas140Settings ParseSettings(Reader &r) {
    Aas140Settings s;
    r.Expect("settings", "settings");
    r.Expect("{", "settings");
    std::size_t next = 0;
    for (;;) {
        const std::size_t at = r.pos;
        const std::string key = r.Token();
        if (key == "}") break;
        Need(next < kSettingCount && key == kSettingKeys[next], "settings-key",
             "unexpected settings key '" + key + "' near byte " + Str(at) +
                 (next < kSettingCount ? std::string(", expected '") + kSettingKeys[next] + "'" : std::string()));
        ++next;
        Aas140Setting entry; entry.key = key;
        if (key == "bboxes") {
            r.Expect("{", "settings-bboxes");
            int boxes = 0;
            for (;;) {
                r.SkipSpace();
                Need(r.pos < r.size, "truncated", "settings bboxes");
                if (r.bytes[r.pos] == '}') { ++r.pos; break; }
                float lo[3], hi[3]; std::string a, b;
                ParseVector(r, lo, a);
                r.Expect("-", "settings-bboxes");
                ParseVector(r, hi, b);
                if (boxes == 0) { std::copy(lo, lo + 3, s.bboxMin); std::copy(hi, hi + 3, s.bboxMax); }
                entry.value += (boxes ? " " : "") + a + "-" + b;
                ++boxes;
            }
            Need(boxes == 1, "settings-bboxes", "profile admits exactly one bounding box, found " + Str(std::size_t(boxes)));
            s.entries.push_back(entry);
            continue;
        }
        r.Expect("=", "settings-value");
        if (key == "gravity") {
            ParseVector(r, s.gravity, entry.value);
        } else if (key == "fileExtension") {
            const std::string t = r.Token();
            Need(t.size() >= 2 && t[0] == '"', "settings-value", "fileExtension is not a string");
            entry.value = t;
            s.fileExtension = t.substr(1, t.size() - 2);
        } else {
            r.SkipSpace();
            const std::size_t begin = r.pos;
            const double v = r.Number("settings-value");
            entry.value.assign(reinterpret_cast<const char *>(r.bytes + begin), r.pos - begin);
            const bool flag = v == 0 || v == 1;
            if (key == "usePatches") { Need(flag, "settings-value", key); s.usePatches = v != 0; }
            else if (key == "writeBrushMap") { Need(flag, "settings-value", key); s.writeBrushMap = v != 0; }
            else if (key == "playerFlood") { Need(flag, "settings-value", key); s.playerFlood = v != 0; }
            else if (key == "allowSwimReachabilities") { Need(flag, "settings-value", key); s.allowSwimReachabilities = v != 0; }
            else if (key == "allowFlyReachabilities") { Need(flag, "settings-value", key); s.allowFlyReachabilities = v != 0; }
            else if (key == "generateTacticalFeatures") { Need(flag, "settings-value", key); s.generateTacticalFeatures = v != 0; }
            else if (key == "maxStepHeight") s.maxStepHeight = float(v);
            else if (key == "maxBarrierHeight") s.maxBarrierHeight = float(v);
            else if (key == "maxWaterJumpHeight") s.maxWaterJumpHeight = float(v);
            else if (key == "maxFallHeight") s.maxFallHeight = float(v);
            else if (key == "minFloorCos") s.minFloorCos = float(v);
            else if (key == "maxStandHeight") s.maxStandHeight = float(v);
            else if (key == "maxCrouchHeight") s.maxCrouchHeight = float(v);
            else if (key == "maxProneHeight") s.maxProneHeight = float(v);
            else if (key == "maxGapLength") s.maxGapLength = float(v);
            else {
                Need(v == std::floor(v) && std::fabs(v) < 1e6, "settings-value", key + " is not an integer");
                const int i = int(v);
                if (key == "tt_barrierJump") s.tt_barrierJump = i;
                else if (key == "tt_transCrouch") s.tt_transCrouch = i;
                else if (key == "tt_transProne") s.tt_transProne = i;
                else if (key == "tt_waterJump") s.tt_waterJump = i;
                else if (key == "tt_startWalkOffLedge") s.tt_startWalkOffLedge = i;
                else s.tt_gapJump = i;
            }
        }
        s.entries.push_back(entry);
    }
    Need(next == kSettingCount, "settings-key", std::string("settings block ends before '") +
         (next < kSettingCount ? kSettingKeys[next] : "") + "'");
    Need(s.fileExtension == "aas28", "settings-extension", "fileExtension '" + s.fileExtension + "' is not aas28");
    return s;
}

// ---------------------------------------------------------------------------
// Sections
struct SectionSpec { const char *name; std::size_t record; };
const SectionSpec kSections[] = {
    {"planes", 16}, {"vertices", 12}, {"edges", 8}, {"edgeIndex", 4}, {"faces", 16}, {"faceIndex", 4},
    {"areas", 56}, {"reachabilities", 48}, {"nodes", 12}, {"portals", 10}, {"portalIndex", 4},
    {"clusters", 12}, {"featureIndex", 4}, {"features", 36}, {"locationIndex", 4}, {"locations", 0}};
const std::size_t kSectionCount = sizeof(kSections) / sizeof(kSections[0]);
const long long kMaxCount = 1 << 24;

std::vector<std::int32_t> Ints(const unsigned char *p, std::size_t n) {
    std::vector<std::int32_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = I32(p + 4 * i);
    return v;
}

void Decode(Aas140File &f, std::size_t s, const unsigned char *p, std::size_t n) {
    switch (s) {
    case 0: f.planes.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 16) { Aas140Plane &x = f.planes[i]; x.a = F32(p); x.b = F32(p + 4); x.c = F32(p + 8); x.d = F32(p + 12); }
        break;
    case 1: f.vertices.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 12) { Aas140Vertex &x = f.vertices[i]; x.x = F32(p); x.y = F32(p + 4); x.z = F32(p + 8); }
        break;
    case 2: f.edges.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 8) { f.edges[i].v[0] = I32(p); f.edges[i].v[1] = I32(p + 4); }
        break;
    case 3: f.edgeIndex = Ints(p, n); break;
    case 4: f.faces.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 16) {
            Aas140Face &x = f.faces[i];
            x.numEdges = I32(p); x.firstEdge = I32(p + 4); x.planeNum = U16(p + 8); x.flags = U16(p + 10);
            x.areas[0] = I16(p + 12); x.areas[1] = I16(p + 14);
        }
        break;
    case 5: f.faceIndex = Ints(p, n); break;
    case 6: f.areas.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 56) {
            Aas140Area &x = f.areas[i];
            x.reachHead = U32(p); x.revReachHead = U32(p + 4); x.unknown8 = U32(p + 8);
            x.center[0] = F32(p + 12); x.center[1] = F32(p + 16); x.center[2] = F32(p + 20);
            x.firstFace = I32(p + 24); x.unknown28 = U32(p + 28); x.cluster = I16(p + 32);
            for (int k = 0; k < 4; ++k) x.bounds2d[k] = I16(p + 34 + 2 * k);
            x.flags = U16(p + 42); x.contents = U16(p + 44); x.numFaces = U16(p + 46);
            x.numFeatures = U16(p + 48); x.firstFeature = U16(p + 50);
            x.numLocations = U16(p + 52); x.firstLocation = U16(p + 54);
        }
        break;
    case 7: f.reachabilities.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 48) {
            Aas140Reach &x = f.reachabilities[i];
            x.travelType = U32(p);
            for (int k = 0; k < 3; ++k) { x.start[k] = F32(p + 4 + 4 * k); x.end[k] = F32(p + 16 + 4 * k); }
            x.edgeNum = I32(p + 28); x.nextPtr = U32(p + 32); x.revNextPtr = U32(p + 36);
            x.toAreaNum = I16(p + 40); x.fromAreaNum = I16(p + 42); x.travelTime = U16(p + 44);
            x.pad46[0] = p[46]; x.pad46[1] = p[47];
        }
        break;
    case 8: f.nodes.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 12) {
            Aas140Node &x = f.nodes[i];
            x.children[0] = I32(p); x.children[1] = I32(p + 4); x.planeNum = U16(p + 8); x.pad10 = U16(p + 10);
        }
        break;
    case 9: f.portals.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 10) {
            Aas140Portal &x = f.portals[i];
            x.areaNum = I16(p); x.clusters[0] = I16(p + 2); x.clusters[1] = I16(p + 4);
            x.clusterAreaNum[0] = I16(p + 6); x.clusterAreaNum[1] = I16(p + 8);
        }
        break;
    case 10: f.portalIndex = Ints(p, n); break;
    case 11: f.clusters.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 12) {
            Aas140Cluster &x = f.clusters[i];
            x.firstPortal = I32(p); x.numPortals = I32(p + 4); x.unknown8 = U16(p + 8); x.tail10 = U16(p + 10);
        }
        break;
    case 12: f.featureIndex = Ints(p, n); break;
    case 13: f.features.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 36) {
            Aas140Feature &x = f.features[i];
            for (int k = 0; k < 3; ++k) x.origin[k] = F32(p + 4 * k);
            x.normal[0] = F32(p + 12); x.normal[1] = F32(p + 16);
            std::memcpy(x.unknown20, p + 20, 8);
            x.flags = U16(p + 28); x.area = U16(p + 30); x.unknown32 = U16(p + 32);
            x.height = p[34]; x.unknown35 = p[35];
        }
        break;
    case 14: f.locationIndex = Ints(p, n); break;
    }
}

void ParseSections(Reader &r, Aas140File &f) {
    for (std::size_t s = 0; s < kSectionCount; ++s) {
        const SectionSpec &spec = kSections[s];
        const std::size_t at = r.pos;
        const std::string name = r.Token();
        Need(name == spec.name, "section-order", "expected section '" + std::string(spec.name) + "' but found '" + name +
             "' near byte " + Str(at));
        Aas140Section section; section.name = name;
        section.count = std::size_t(r.Integer("section-count", 0, kMaxCount));
        r.Expect("{", "section-open");
        if (s + 1 == kSectionCount) {
            Need(section.count >= 1, "locations", "locations must contain the EMPTY entry");
            section.offset = r.pos;
            for (std::size_t i = 0; i < section.count; ++i) {
                Need(r.Integer("locations", 0, kMaxCount) == (long long)i, "locations", "location index out of order");
                r.Expect("(", "locations");
                const std::string n = r.Token();
                Need(n != "(" && n != ")" && n != "{" && n != "}" && n != "=" && n[0] != '"', "locations",
                     "invalid location name '" + n + "'");
                f.locations.push_back(n);
                r.Expect(")", "locations");
            }
            r.Expect("}", "section-close");
            section.bytes = r.pos - section.offset;
            f.sections.push_back(section);
            continue;
        }
        const std::size_t tokenAt = r.pos;
        const std::string bin = r.Token();
        Need(bin == "bin", "section-text-records", "section '" + name + "' uses text records near byte " + Str(tokenAt) +
             "; the profile admits only binary blocks");
        section.bytes = std::size_t(r.Integer("bin-size", 0, (long long)1 << 30));
        Need(r.pos < r.size && r.bytes[r.pos] == '\n', "bin-separator", "bin size of '" + name + "' is not followed by one newline");
        ++r.pos;
        section.offset = r.pos;
        Need(section.bytes <= r.size - r.pos, "bin-overflow", "bin block of '" + name + "' extends past the end of the file");
        Need(section.bytes == section.count * spec.record, "record-size", "section '" + name + "' has " + Str(section.bytes) +
             " bytes for " + Str(section.count) + " records of " + Str(spec.record));
        Decode(f, s, r.bytes + r.pos, section.count);
        r.pos += section.bytes;
        r.Expect("}", "section-close");
        f.sections.push_back(section);
    }
    Need(r.AtEnd(), "trailing-bytes", "unaccounted bytes after the locations section at byte " + Str(r.pos));
}

// ---------------------------------------------------------------------------
// Validation (docs/AAS.md 2.4)
bool InRange(long long v, std::size_t n) { return v >= 0 && (unsigned long long)v < n; }

void ValidateRanges(const std::vector<std::pair<std::size_t, std::size_t> > &ranges, std::size_t listSize, const char *code,
                    const char *what) {
    // Ranges in owner order must be contiguous from 0 and cover the list.
    std::size_t next = 0;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        if (!ranges[i].second) continue;
        Need(ranges[i].first == next, code, std::string(what) + " range of owner " + Str(i) + " is not contiguous");
        next += ranges[i].second;
    }
    Need(next == listSize, code, std::string(what) + " ranges cover " + Str(next) + " of " + Str(listSize) + " entries");
}

void Validate(Aas140File &f) {
    Aas140Exceptions &x = f.exceptions;
    const std::size_t P = f.planes.size(), V = f.vertices.size(), E = f.edges.size(), F = f.faces.size(),
                      A = f.areas.size(), N = f.nodes.size(), PO = f.portals.size(), C = f.clusters.size(),
                      FE = f.features.size(), L = f.locations.size();
    Need(A >= 1 && F >= 1 && E >= 1 && N >= 2 && PO >= 1 && C >= 1, "counts", "a dummy record or the BSP root is missing");
    Need(A <= 32767 && PO <= 32767 && C <= 32767 && P <= 65536, "counts", "counts exceed the 16-bit fields");
    // planes
    Need(P % 2 == 0, "plane-pair", "odd plane count");
    for (std::size_t i = 0; i < P; ++i) {
        const Aas140Plane &p = f.planes[i];
        Need(std::isfinite(p.a) && std::isfinite(p.b) && std::isfinite(p.c) && std::isfinite(p.d), "plane", "non-finite plane " + Str(i));
        const double len = std::sqrt(double(p.a) * p.a + double(p.b) * p.b + double(p.c) * p.c);
        Need(len > 0.9 && len < 1.1, "plane", "plane " + Str(i) + " normal is not near unit length");
        if (std::fabs(len - 1) > 1e-3) ++x.nonUnitPlanes;
        if (i % 2) {
            const Aas140Plane &q = f.planes[i - 1];
            Need(p.a == -q.a && p.b == -q.b && p.c == -q.c && p.d == -q.d, "plane-pair", "plane " + Str(i) + " is not the negation of its pair");
        }
    }
    for (std::size_t i = 0; i < V; ++i)
        Need(std::isfinite(f.vertices[i].x) && std::isfinite(f.vertices[i].y) && std::isfinite(f.vertices[i].z), "vertex", "non-finite vertex " + Str(i));
    // edges
    Need(f.edges[0].v[0] == 0 && f.edges[0].v[1] == 0, "dummy-record", "edge 0 is not zero");
    for (std::size_t i = 1; i < E; ++i)
        Need(InRange(f.edges[i].v[0], V) && InRange(f.edges[i].v[1], V), "range-edge-vertex", "edge " + Str(i) + " vertex out of range");
    for (std::size_t i = 0; i < f.edgeIndex.size(); ++i) {
        const long long e = f.edgeIndex[i];
        Need(e != 0 && InRange(e < 0 ? -e : e, E), "range-edge-index", "edgeIndex " + Str(i) + " out of range");
    }
    // faces
    {
        const Aas140Face &z = f.faces[0];
        Need(!z.numEdges && !z.firstEdge && !z.planeNum && !z.flags && !z.areas[0] && !z.areas[1], "dummy-record", "face 0 is not zero");
    }
    std::size_t nextEdge = 0;
    for (std::size_t i = 1; i < F; ++i) {
        const Aas140Face &fc = f.faces[i];
        Need(fc.planeNum < P, "range-face-plane", "face " + Str(i) + " plane out of range");
        Need(InRange(fc.areas[0], A) && InRange(fc.areas[1], A), "range-face-area", "face " + Str(i) + " area out of range");
        Need(!(fc.flags & ~AAS140_FACE_ADMITTED_BITS), "face-flags", "face " + Str(i) + " has unadmitted flags " + Str(std::size_t(fc.flags)));
        const bool floor = (fc.flags & AAS140_FACE_FLOOR) != 0;
        Need(floor == (fc.numEdges > 0), "face-edges", "face " + Str(i) + (floor ? " is a floor without edges" : " has edges but is not a floor"));
        if (!floor) { Need(fc.firstEdge == 0 && fc.numEdges == 0, "face-edges", "non-floor face " + Str(i) + " has an edge range"); continue; }
        Need(fc.numEdges >= 3 && (std::size_t)fc.firstEdge == nextEdge, "face-edge-range", "floor face " + Str(i) + " edge range is not contiguous");
        nextEdge += fc.numEdges;
        Need(nextEdge <= f.edgeIndex.size(), "face-edge-range", "floor face " + Str(i) + " edge range past edgeIndex");
    }
    Need(nextEdge == f.edgeIndex.size(), "face-edge-range", "floor face edge ranges do not cover edgeIndex");
    for (std::size_t i = 1; i < F; ++i)
        if (f.faces[i].numEdges) Aas140ChainFloorFace(f, int(i));   // consecutive edges share a vertex; loop closes
    for (std::size_t i = 0; i < f.faceIndex.size(); ++i) {
        const long long v = f.faceIndex[i];
        Need(v != 0 && InRange(v < 0 ? -v : v, F), "range-face-index", "faceIndex " + Str(i) + " out of range");
    }
    // areas
    {
        const Aas140Area &z = f.areas[0];
        Need((z.flags == 0 || z.flags == AAS140_AREA_OUTSIDE) && !z.contents && !z.cluster && !z.numFaces && !z.firstFace &&
             !z.numFeatures && !z.firstFeature && !z.numLocations && !z.firstLocation && !z.center[0] && !z.center[1] &&
             !z.center[2] && !z.bounds2d[0] && !z.bounds2d[1] && !z.bounds2d[2] && !z.bounds2d[3] && !z.reachHead &&
             !z.revReachHead && !z.unknown8 && !z.unknown28, "dummy-record", "area 0 is not the dummy area");
    }
    std::vector<std::pair<std::size_t, std::size_t> > faceRanges(A), featureRanges(A), locationRanges(A);
    for (std::size_t i = 1; i < A; ++i) {
        const Aas140Area &a = f.areas[i];
        const std::string id = "area " + Str(i);
        Need(!a.reachHead && !a.revReachHead && !a.unknown8 && !a.unknown28, "area-runtime-fields", id + " has nonzero runtime bytes");
        Need(std::isfinite(a.center[0]) && std::isfinite(a.center[1]) && std::isfinite(a.center[2]), "area", id + " center");
        Need(!(a.flags & ~AAS140_AREA_KNOWN_BITS), "area-flags", id + " has unknown flags");
        Need(!(a.contents & ~AAS140_CONTENTS_ADMITTED_BITS), "area-contents", id + " has unadmitted contents bits");
        Need(a.firstFace >= 0, "area-face-range", id + " face range");
        faceRanges[i] = std::make_pair(std::size_t(a.firstFace), std::size_t(a.numFaces));
        Need(std::size_t(a.firstFace) + a.numFaces <= f.faceIndex.size(), "area-face-range", id + " face range past faceIndex");
        bool floorFace = false;
        for (int k = 0; k < a.numFaces; ++k) {
            const std::int32_t v = f.faceIndex[a.firstFace + k];
            const Aas140Face &fc = f.faces[v < 0 ? -v : v];
            Need(fc.areas[v < 0 ? 1 : 0] == (int)i, "face-side", id + " faceIndex sign disagrees with the face's area side");
            if (fc.flags & AAS140_FACE_FLOOR) floorFace = true;
        }
        if ((a.flags & AAS140_AREA_FLOOR) && !floorFace) ++x.floorAreasWithoutFloorFace;
        if (!(a.flags & AAS140_AREA_FLOOR) && floorFace) ++x.floorFaceAreasWithoutFloorFlag;
        if (a.cluster > 0) {
            Need(InRange(a.cluster, C), "range-area-cluster", id + " cluster out of range");
            Need(!(a.contents & AAS140_CONTENTS_CLUSTERPORTAL), "portal-contents", id + " has the portal contents bit outside a portal");
        } else if (a.cluster < 0) {
            Need(InRange(-a.cluster, PO) && f.portals[-a.cluster].areaNum == (int)i, "portal-area", id + " portal back-reference");
            Need(a.contents & AAS140_CONTENTS_CLUSTERPORTAL, "portal-contents", id + " is a portal without the portal contents bit");
        } else {
            Need(!(a.contents & AAS140_CONTENTS_CLUSTERPORTAL), "portal-contents", id + " has the portal contents bit and no cluster");
        }
        featureRanges[i] = std::make_pair(std::size_t(a.firstFeature), std::size_t(a.numFeatures));
        Need(std::size_t(a.firstFeature) + a.numFeatures <= f.featureIndex.size(), "area-feature-range", id + " feature range");
        locationRanges[i] = std::make_pair(std::size_t(a.firstLocation), std::size_t(a.numLocations));
        Need(std::size_t(a.firstLocation) + a.numLocations <= f.locationIndex.size(), "area-location-range", id + " location range");
        Need(!a.numLocations || (a.contents & AAS140_CONTENTS_LOCATION_VOL), "location-contents", id + " lists locations without contents 0x80");
    }
    ValidateRanges(faceRanges, f.faceIndex.size(), "area-face-range", "face");
    ValidateRanges(featureRanges, f.featureIndex.size(), "area-feature-range", "feature");
    ValidateRanges(locationRanges, f.locationIndex.size(), "area-location-range", "location");
    for (std::size_t i = 0; i < f.featureIndex.size(); ++i)
        Need(InRange(f.featureIndex[i], FE), "range-feature-index", "featureIndex " + Str(i) + " out of range");
    for (std::size_t i = 0; i < FE; ++i) {
        Need(InRange(f.features[i].area, A), "range-feature-area", "feature " + Str(i) + " area out of range");
        if (!f.features[i].area) ++x.area0Features;
    }
    Need(L >= 1 && f.locations[0] == "EMPTY", "locations", "location 0 is not EMPTY");
    for (std::size_t i = 0; i < f.locationIndex.size(); ++i)
        Need(f.locationIndex[i] >= 1 && InRange(f.locationIndex[i], L), "range-location-index", "locationIndex " + Str(i) + " out of range");
    // reachabilities
    std::vector<std::size_t> perArea(A, 0);
    for (std::size_t i = 0; i < f.reachabilities.size(); ++i) {
        const Aas140Reach &r = f.reachabilities[i];
        const std::string id = "reachability " + Str(i);
        Need(InRange(r.fromAreaNum, A) && InRange(r.toAreaNum, A), "range-reach-area", id + " area out of range");
        Need(r.fromAreaNum != r.toAreaNum, "reach-self", id + " is a self-loop");
        Need(!i || f.reachabilities[i - 1].fromAreaNum <= r.fromAreaNum, "reach-order", id + " is not sorted by source area");
        Need(InRange(r.edgeNum, E), "range-reach-edge", id + " edge out of range");
        Need(!r.nextPtr && !r.revNextPtr && !r.pad46[0] && !r.pad46[1], "reach-runtime-fields", id + " has nonzero runtime or padding bytes");
        Need(!(r.travelType & ~AAS140_TRAVEL_KNOWN_BITS), "reach-travel-type", id + " has unknown travel bits");
        for (int k = 0; k < 3; ++k) Need(std::isfinite(r.start[k]) && std::isfinite(r.end[k]), "reach", id + " non-finite point");
        if (!r.fromAreaNum || !r.toAreaNum) {
            Need(r.travelType & AAS140_TFL_PLAYERONLY, "reach-area0", id + " links area 0 without TFL_PLAYERONLY");
            ++x.area0PlayerOnlyLinks;
        }
        if (!r.travelType) ++x.travelTypeZeroLinks;
        if (++perArea[r.fromAreaNum] > f.maxReachPerArea) f.maxReachPerArea = perArea[r.fromAreaNum];
    }
    Need(f.maxReachPerArea <= 256, "reach-per-area", "more than 256 reachabilities in one area");
    // nodes, BSP leaves and depth
    {
        const Aas140Node &z = f.nodes[0];
        Need(!z.children[0] && !z.children[1] && !z.planeNum && !z.pad10, "dummy-record", "node 0 is not zero");
    }
    for (std::size_t i = 1; i < N; ++i) {
        const Aas140Node &n = f.nodes[i];
        Need(n.planeNum < P && !n.pad10, "range-node-plane", "node " + Str(i) + " plane");
        for (int k = 0; k < 2; ++k) {
            const long long c = n.children[k];
            Need(c > 0 ? InRange(c, N) && c != 1 : InRange(-c, A), "range-node-child", "node " + Str(i) + " child out of range");
        }
    }
    {
        std::vector<bool> seenNode(N, false), leaf(A, false);
        std::vector<std::pair<std::int32_t, std::size_t> > stack(1, std::make_pair(1, std::size_t(1)));
        while (!stack.empty()) {
            const std::int32_t n = stack.back().first; const std::size_t depth = stack.back().second; stack.pop_back();
            if (n < 0) { leaf[-n] = true; continue; }
            if (n == 0) continue;
            Need(!seenNode[n], "bsp-tree", "node " + Str(std::size_t(n)) + " is reachable twice");
            seenNode[n] = true;
            if (depth > f.maxTreeDepth) f.maxTreeDepth = depth;
            Need(depth <= 128, "bsp-depth", "BSP depth exceeds MAX_AAS_TREE_DEPTH");
            stack.push_back(std::make_pair(f.nodes[n].children[0], depth + 1));
            stack.push_back(std::make_pair(f.nodes[n].children[1], depth + 1));
        }
        for (std::size_t i = 1; i < A; ++i) Need(leaf[i], "bsp-leaf", "area " + Str(i) + " has no BSP leaf");
    }
    // portals and clusters
    {
        const Aas140Portal &z = f.portals[0];
        Need(!z.areaNum && !z.clusters[0] && !z.clusters[1] && !z.clusterAreaNum[0] && !z.clusterAreaNum[1], "dummy-record", "portal 0 is not zero");
    }
    for (std::size_t i = 1; i < PO; ++i) {
        const Aas140Portal &p = f.portals[i];
        Need(p.areaNum > 0 && InRange(p.areaNum, A) && f.areas[p.areaNum].cluster == -(int)i, "portal-area", "portal " + Str(i) + " area");
        Need(p.clusters[0] > 0 && p.clusters[1] > 0 && InRange(p.clusters[0], C) && InRange(p.clusters[1], C) &&
             p.clusters[0] != p.clusters[1], "portal-clusters", "portal " + Str(i) + " clusters");
    }
    std::vector<int> listed(PO * 2, 0);
    for (std::size_t c = 1; c < C; ++c) {
        const Aas140Cluster &cl = f.clusters[c];
        Need(cl.firstPortal >= 0 && cl.numPortals >= 0 && std::size_t(cl.firstPortal) + cl.numPortals <= f.portalIndex.size(),
             "cluster-portal-range", "cluster " + Str(c) + " portal range");
        for (int k = 0; k < cl.numPortals; ++k) {
            const long long p = f.portalIndex[cl.firstPortal + k];
            Need(p > 0 && InRange(p, PO), "range-portal-index", "cluster " + Str(c) + " portal index out of range");
            const Aas140Portal &po = f.portals[p];
            Need(po.clusters[0] == (int)c || po.clusters[1] == (int)c, "cluster-portal", "cluster " + Str(c) + " lists a portal of another cluster");
            ++listed[p * 2 + (po.clusters[0] == (int)c ? 0 : 1)];
        }
    }
    for (std::size_t p = 1; p < PO; ++p) {
        // Each side is listed at most once and the front side always; a back
        // side missing from its cluster's list is counted (docs/AAS.md 7).
        Need(listed[p * 2] == 1 && listed[p * 2 + 1] <= 1, "cluster-portal", "portal " + Str(p) + " is not listed once by its front cluster");
        if (!listed[p * 2 + 1]) ++x.unlistedPortalBackSides;
    }
    for (std::size_t i = 0; i < f.portalIndex.size(); ++i)
        Need(f.portalIndex[i] > 0 && InRange(f.portalIndex[i], PO), "range-portal-index", "portalIndex " + Str(i));
}

// ---------------------------------------------------------------------------
// Geometry helpers for the conversion (double precision).
struct Vec { double x, y, z; };
Vec Sub(const Vec &a, const Vec &b) { Vec r = {a.x - b.x, a.y - b.y, a.z - b.z}; return r; }
Vec Add(const Vec &a, const Vec &b) { Vec r = {a.x + b.x, a.y + b.y, a.z + b.z}; return r; }
Vec Mul(const Vec &a, double s) { Vec r = {a.x * s, a.y * s, a.z * s}; return r; }
double Dot(const Vec &a, const Vec &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Cross(const Vec &a, const Vec &b) { Vec r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; return r; }
Vec Normalized(const Vec &a) { const double l = std::sqrt(Dot(a, a)); return Mul(a, 1 / l); }
struct HalfSpace { Vec n; double d; };   // keep n.p + d >= 0
typedef std::vector<Vec> Poly;
const double kBase = 131072, kClipEpsilon = 0.01, kUnbounded = 65536;

HalfSpace PlaneOf(const Aas140Plane &p, bool negate) {
    HalfSpace h; const double s = negate ? -1 : 1;
    h.n.x = s * p.a; h.n.y = s * p.b; h.n.z = s * p.c; h.d = s * p.d;
    return h;
}
Poly BaseWinding(const HalfSpace &h) {
    // Square on the plane centred at the point closest to the origin.
    const double len = std::sqrt(Dot(h.n, h.n));
    const Vec n = Mul(h.n, 1 / len);
    const double dist = -h.d / len;
    const Vec ax = std::fabs(n.z) > std::fabs(n.x) && std::fabs(n.z) > std::fabs(n.y) ? Vec{1, 0, 0} : Vec{0, 0, 1};
    const Vec u = Normalized(Cross(n, ax)), v = Cross(n, u);
    const Vec o = Mul(n, dist);
    Poly w(4);
    w[0] = Add(o, Add(Mul(u, -kBase), Mul(v, -kBase)));
    w[1] = Add(o, Add(Mul(u, kBase), Mul(v, -kBase)));
    w[2] = Add(o, Add(Mul(u, kBase), Mul(v, kBase)));
    w[3] = Add(o, Add(Mul(u, -kBase), Mul(v, kBase)));
    return w;
}
void Clip(Poly &w, const HalfSpace &h) {
    if (w.empty()) return;
    const double len = std::sqrt(Dot(h.n, h.n));
    std::vector<double> d(w.size());
    bool anyBack = false, anyFront = false;
    for (std::size_t i = 0; i < w.size(); ++i) {
        d[i] = (Dot(h.n, w[i]) + h.d) / len;
        if (d[i] < -kClipEpsilon) anyBack = true;
        if (d[i] > kClipEpsilon) anyFront = true;
    }
    if (!anyBack) return;
    if (!anyFront) { w.clear(); return; }
    Poly out;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const Vec &a = w[i], &b = w[(i + 1) % w.size()];
        const double da = d[i], db = d[(i + 1) % w.size()];
        if (da >= -kClipEpsilon) out.push_back(a);
        if ((da > kClipEpsilon && db < -kClipEpsilon) || (da < -kClipEpsilon && db > kClipEpsilon)) {
            const double t = da / (da - db);
            out.push_back(Add(a, Mul(Sub(b, a), t)));
        }
    }
    w.swap(out);
    if (w.size() < 3) w.clear();
}
void RemoveDuplicatePoints(Poly &w) {
    Poly out;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const Vec &p = w[i];
        if (!out.empty()) { const Vec q = Sub(p, out.back()); if (Dot(q, q) < 1e-6) continue; }
        out.push_back(p);
    }
    while (out.size() > 1) { const Vec q = Sub(out.front(), out.back()); if (Dot(q, q) < 1e-6) out.pop_back(); else break; }
    w.swap(out);
    if (w.size() < 3) w.clear();
}
struct AreaPlanes { std::vector<HalfSpace> half; std::vector<int> planePair; };
AreaPlanes CollectAreaPlanes(const Aas140File &f, int area) {
    AreaPlanes out;
    const Aas140Area &a = f.areas[area];
    for (int k = 0; k < a.numFaces; ++k) {
        const std::int32_t v = f.faceIndex[a.firstFace + k];
        const Aas140Face &fc = f.faces[v < 0 ? -v : v];
        out.half.push_back(PlaneOf(f.planes[fc.planeNum], v < 0));
        out.planePair.push_back(fc.planeNum >> 1);
    }
    return out;
}
// Facet of the polytope `planes` on the half-space boundary `h` (pair `pair`),
// additionally clipped by `extra` (the neighbour's half-spaces) when given.
Poly Facet(const HalfSpace &h, int pair, const AreaPlanes &planes, const AreaPlanes *extra) {
    Poly w = BaseWinding(h);
    for (std::size_t j = 0; j < planes.half.size() && !w.empty(); ++j)
        if (planes.planePair[j] != pair) Clip(w, planes.half[j]);
    if (extra)
        for (std::size_t j = 0; j < extra->half.size() && !w.empty(); ++j)
            if (extra->planePair[j] != pair) Clip(w, extra->half[j]);
    RemoveDuplicatePoints(w);
    return w;
}
bool Unbounded(const Poly &w) {
    for (std::size_t i = 0; i < w.size(); ++i)
        if (std::fabs(w[i].x) > kUnbounded || std::fabs(w[i].y) > kUnbounded || std::fabs(w[i].z) > kUnbounded) return true;
    return false;
}
Vec V(const Aas140Vertex &v) { Vec r = {v.x, v.y, v.z}; return r; }
double SegmentDistance(const Vec &p, const Vec &a, const Vec &b) {
    const Vec ab = Sub(b, a); const double l2 = Dot(ab, ab);
    double t = l2 > 0 ? Dot(Sub(p, a), ab) / l2 : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const Vec q = Sub(p, Add(a, Mul(ab, t)));
    return std::sqrt(Dot(q, q));
}

} // namespace

Aas140Refusal::Aas140Refusal(const std::string &c, const std::string &detail)
    : std::runtime_error("AAS 1.40 refused [" + c + "]: " + detail), code(c) {}

Aas140File ParseAas140(const unsigned char *bytes, std::size_t size) {
    Need(bytes != NULL && size > 0, "truncated", "empty input");
    Reader r(bytes, size);
    Aas140File f;
    r.Expect("DewmAAS", "header");
    const std::string version = r.Token();
    Need(version == "\"1.40\"", "version", "version " + version + " is not \"1.40\"");
    f.version = "1.40";
    f.crc = std::uint32_t(r.Integer("header", 0, 0xffffffffLL));
    f.settings = ParseSettings(r);
    ParseSections(r, f);
    Validate(f);
    return f;
}

int Aas140PointAreaNum(const Aas140File &f, double x, double y, double z) {
    std::int32_t n = 1;
    for (int depth = 0; n > 0 && depth < 256; ++depth) {
        const Aas140Node &node = f.nodes[n];
        const Aas140Plane &p = f.planes[node.planeNum];
        n = double(p.a) * x + double(p.b) * y + double(p.c) * z + double(p.d) > 0 ? node.children[0] : node.children[1];
    }
    return n < 0 ? -n : 0;
}

Aas140FloorLoop Aas140ChainFloorFace(const Aas140File &f, int faceNum) {
    Need(faceNum > 0 && std::size_t(faceNum) < f.faces.size(), "floor-loop", "face out of range");
    const Aas140Face &fc = f.faces[faceNum];
    Need((fc.flags & AAS140_FACE_FLOOR) && fc.numEdges >= 3, "floor-loop", "face " + Str(std::size_t(faceNum)) + " is not a floor face");
    Aas140FloorLoop loop;
    const int n = fc.numEdges;
    for (int k = 0; k < n; ++k) {
        const std::int32_t e = f.edgeIndex[fc.firstEdge + k];
        loop.edges.push_back(e < 0 ? -e : e);
    }
    const Aas140Edge &e0 = f.edges[loop.edges[0]], &e1 = f.edges[loop.edges[1]];
    Need(e0.v[0] != e0.v[1], "floor-loop", "degenerate floor edge");
    const bool v0Shared = e0.v[0] == e1.v[0] || e0.v[0] == e1.v[1], v1Shared = e0.v[1] == e1.v[0] || e0.v[1] == e1.v[1];
    Need(v0Shared != v1Shared, "floor-loop", "face " + Str(std::size_t(faceNum)) + " first edges do not chain");
    std::int32_t start = v0Shared ? e0.v[1] : e0.v[0];
    const std::int32_t first = start;
    for (int k = 0; k < n; ++k) {
        const Aas140Edge &e = f.edges[loop.edges[k]];
        Need(e.v[0] != e.v[1] && (e.v[0] == start || e.v[1] == start), "floor-loop",
             "face " + Str(std::size_t(faceNum)) + " edges do not chain by shared vertex");
        loop.startVertex.push_back(start);
        start = e.v[0] == start ? e.v[1] : e.v[0];
    }
    Need(start == first, "floor-loop", "face " + Str(std::size_t(faceNum)) + " floor winding is not closed");
    return loop;
}

Aas140CentroidStats Aas140FloorCentroidLookup(const Aas140File &f) {
    Aas140CentroidStats s;
    for (std::size_t i = 1; i < f.faces.size(); ++i) {
        const Aas140Face &fc = f.faces[i];
        if (!fc.numEdges) continue;
        ++s.floorFaces;
        const int area = fc.areas[0] ? fc.areas[0] : fc.areas[1];
        double c[3] = {0, 0, 0};
        for (int k = 0; k < fc.numEdges; ++k) {
            const std::int32_t e = f.edgeIndex[fc.firstEdge + k];
            const Aas140Edge &ed = f.edges[e < 0 ? -e : e];
            for (int j = 0; j < 2; ++j) {
                const Aas140Vertex &v = f.vertices[ed.v[j]];
                c[0] += v.x; c[1] += v.y; c[2] += v.z;
            }
        }
        const double m = 2.0 * fc.numEdges;
        const int hit = Aas140PointAreaNum(f, c[0] / m, c[1] / m, c[2] / m + 1);
        if (hit == area) ++s.ownArea;
        else if (!hit) ++s.solid;
        else if (f.areas[hit].flags & AAS140_AREA_FLOOR) ++s.otherFloorArea;
        else ++s.noFloorArea;
    }
    return s;
}

std::uint32_t Aas140EngineTravelType(std::uint32_t t) {
    if (!t) return WOLF_AAS_TFL_UNTYPED;
    std::uint32_t out = t & ~AAS140_TFL_PLAYERONLY;   // all other admitted bits keep their value
    if (t & AAS140_TFL_PLAYERONLY) out |= WOLF_AAS_TFL_PLAYERONLY;
    return out;
}

std::uint16_t Aas140EngineAreaFlags(std::uint16_t flags) {
    std::uint16_t out = std::uint16_t(flags & ~AAS140_AREA_VISITED);
    if (flags & AAS140_AREA_VISITED) out = std::uint16_t(out | WOLF_AAS_AREA_VISITED);
    return out;
}

Aas140EngineFile ConvertAas140ToEngine(const Aas140File &f) {
    Need(f.crc == AAS140_RETAIL_CRC_FIELD, "crc-policy", "map CRC field " + Str(std::size_t(f.crc)) +
         " is not the measured retail value 1 admitted by WolfAas140V1");
    Aas140EngineFile out;
    Aas140ConversionStats &st = out.stats;
    out.profile = "WolfAas140V1";
    out.fileCrc = f.crc;
    out.planes = f.planes;
    out.vertices = f.vertices;
    out.edges = f.edges;
    out.edgeIndex = f.edgeIndex;
    out.nodes = f.nodes;
    out.faceIndex = f.faceIndex;
    out.portalIndex = f.portalIndex;
    const std::size_t A = f.areas.size(), F = f.faces.size();

    // Faces: Doom 3 field order; flags keep SOLID and FLOOR only.
    out.faces.resize(F);
    for (std::size_t i = 0; i < F; ++i) {
        const Aas140Face &s = f.faces[i];
        Aas140EngineFace &d = out.faces[i];
        d.planeNum = s.planeNum;
        d.flags = std::uint16_t(s.flags & (AAS140_FACE_SOLID | AAS140_FACE_FLOOR));
        d.numEdges = s.numEdges; d.firstEdge = s.firstEdge;
        d.areas[0] = s.areas[0]; d.areas[1] = s.areas[1];
    }
    // Floor loops: chain by shared vertex, orient counter-clockwise seen from
    // above (+z), and re-sign each edge so the sign gives the direction.
    for (std::size_t i = 1; i < F; ++i) {
        if (!f.faces[i].numEdges) continue;
        Aas140FloorLoop loop = Aas140ChainFloorFace(f, int(i));
        const std::size_t n = loop.edges.size();
        double area2 = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const Aas140Vertex &a = f.vertices[loop.startVertex[k]], &b = f.vertices[loop.startVertex[(k + 1) % n]];
            area2 += double(a.x) * b.y - double(b.x) * a.y;
        }
        std::vector<std::int32_t> signedEdges(n);
        if (area2 >= 0) {
            for (std::size_t k = 0; k < n; ++k)
                signedEdges[k] = f.edges[loop.edges[k]].v[0] == loop.startVertex[k] ? loop.edges[k] : -loop.edges[k];
        } else {
            ++st.floorLoopsReversed;
            for (std::size_t k = 0; k < n; ++k) {
                // Reverse traversal: edge n-1-k starts at the end of its forward direction.
                const std::size_t j = n - 1 - k;
                const std::int32_t endVertex = loop.startVertex[(j + 1) % n];
                signedEdges[k] = f.edges[loop.edges[j]].v[0] == endVertex ? loop.edges[j] : -loop.edges[j];
            }
        }
        std::copy(signedEdges.begin(), signedEdges.end(), out.edgeIndex.begin() + f.faces[i].firstEdge);
    }

    // Areas: file center kept, flags/contents remapped, Doom 3 travel flags
    // from contents (no water bit is admitted, so every area is TFL_AIR).
    out.areas.resize(A);
    for (std::size_t i = 0; i < A; ++i) {
        const Aas140Area &s = f.areas[i];
        Aas140EngineArea &d = out.areas[i];
        d.numFaces = s.numFaces; d.firstFace = s.firstFace;
        std::copy(s.center, s.center + 3, d.center);
        d.flags = Aas140EngineAreaFlags(s.flags);
        d.contents = std::uint16_t(s.contents & AAS140_CONTENTS_CLUSTERPORTAL);
        d.cluster = s.cluster;
        d.travelFlags = std::int32_t(D3_TFL_AIR);
        if (i && !s.cluster && (d.flags & (D3_AREA_REACHABLE_WALK | D3_AREA_REACHABLE_FLY))) {
            // Doom 3 routing indexes cluster 0 as a real cluster; never offer it.
            d.flags = std::uint16_t(d.flags & ~(D3_AREA_REACHABLE_WALK | D3_AREA_REACHABLE_FLY));
            ++st.cluster0ReachableFlagsCleared;
        }
    }

    // Cluster numbering with Doom 3's NumberClusterAreas rule.
    const std::size_t C = f.clusters.size();
    out.clusters.resize(C);
    out.portals.resize(f.portals.size());
    for (std::size_t p = 0; p < f.portals.size(); ++p) {
        out.portals[p].areaNum = f.portals[p].areaNum;
        out.portals[p].clusters[0] = f.portals[p].clusters[0];
        out.portals[p].clusters[1] = f.portals[p].clusters[1];
        out.portals[p].clusterAreaNum[0] = out.portals[p].clusterAreaNum[1] = 0;
    }
    std::vector<std::vector<int> > clusterAreas(C);
    for (std::size_t i = 1; i < A; ++i)
        if (f.areas[i].cluster > 0) clusterAreas[f.areas[i].cluster].push_back(int(i));
    const std::uint16_t reachable = D3_AREA_REACHABLE_WALK | D3_AREA_REACHABLE_FLY;
    // Portal sides that the file's cluster lists omit. Doom 3 indexes a portal
    // area in both clusters, so such a side still needs an index in its
    // cluster; it is numbered after the listed portals of the same pass and is
    // NOT added to the cluster's portal list (the file's list stays as is).
    std::vector<std::vector<int> > unlisted(C);
    {
        std::vector<char> listedSide(f.portals.size() * 2, 0);
        for (std::size_t c = 1; c < C; ++c)
            for (int k = 0; k < f.clusters[c].numPortals; ++k) {
                const int p = f.portalIndex[f.clusters[c].firstPortal + k];
                listedSide[p * 2 + (f.portals[p].clusters[0] == (int)c ? 0 : 1)] = 1;
            }
        for (std::size_t p = 1; p < f.portals.size(); ++p)
            for (int s = 0; s < 2; ++s)
                if (!listedSide[p * 2 + s]) { unlisted[f.portals[p].clusters[s]].push_back(int(p)); ++st.unlistedPortalSidesNumbered; }
    }
    for (std::size_t c = 1; c < C; ++c) {
        Aas140EngineCluster &cl = out.clusters[c];
        cl.firstPortal = f.clusters[c].firstPortal; cl.numPortals = f.clusters[c].numPortals;
        for (int pass = 0; pass < 2; ++pass) {
            const bool wantReachable = pass == 0;
            for (std::size_t k = 0; k < clusterAreas[c].size(); ++k) {
                Aas140EngineArea &a = out.areas[clusterAreas[c][k]];
                if (((a.flags & reachable) != 0) != wantReachable) continue;
                a.clusterAreaNum = std::int16_t(cl.numAreas++);
                if (wantReachable) ++cl.numReachableAreas;
                ++st.clusterAreasNumbered;
            }
            for (int k = 0; k < cl.numPortals; ++k) {
                Aas140EnginePortal &po = out.portals[f.portalIndex[cl.firstPortal + k]];
                if (((out.areas[po.areaNum].flags & reachable) != 0) != wantReachable) continue;
                po.clusterAreaNum[po.clusters[0] == (int)c ? 0 : 1] = std::int16_t(cl.numAreas++);
                if (wantReachable) ++cl.numReachableAreas;
                ++st.portalSidesNumbered;
            }
            for (std::size_t k = 0; k < unlisted[c].size(); ++k) {
                Aas140EnginePortal &po = out.portals[unlisted[c][k]];
                if (((out.areas[po.areaNum].flags & reachable) != 0) != wantReachable) continue;
                po.clusterAreaNum[po.clusters[0] == (int)c ? 0 : 1] = std::int16_t(cl.numAreas++);
                if (wantReachable) ++cl.numReachableAreas;
                ++st.portalSidesNumbered;
            }
        }
        Need(cl.numAreas <= 32767, "cluster-size", "cluster " + Str(c) + " has more areas than a short index allows");
    }

    // Reachabilities: classify against Doom 3's cluster preconditions.
    out.linkClass.resize(f.reachabilities.size());
    std::vector<std::vector<std::uint32_t> > perArea(A);
    for (std::size_t i = 0; i < f.reachabilities.size(); ++i) {
        const Aas140Reach &r = f.reachabilities[i];
        Aas140LinkClass cls = Aas140LinkClass::Linked;
        if (!r.fromAreaNum || !r.toAreaNum) cls = Aas140LinkClass::Area0;
        else {
            const int xc = f.areas[r.fromAreaNum].cluster, yc = f.areas[r.toAreaNum].cluster;
            Need(xc && yc, "reach-cluster0", "reachability " + Str(i) + " touches an area without a cluster");
            if (xc > 0 && yc > 0 && xc != yc) cls = Aas140LinkClass::LinkedCrossCluster;
            else if (xc < 0) {
                // The reverse flood enters the source portal from every cluster
                // the target belongs to; each must be a side of that portal.
                const Aas140Portal &px = f.portals[-xc];
                int targets[2] = {yc, 0};
                if (yc < 0) { targets[0] = f.portals[-yc].clusters[0]; targets[1] = f.portals[-yc].clusters[1]; }
                for (int k = 0; k < 2; ++k)
                    if (targets[k] && px.clusters[0] != targets[k] && px.clusters[1] != targets[k]) cls = Aas140LinkClass::PortalBorder;
            }
        }
        out.linkClass[i] = cls;
        if (cls == Aas140LinkClass::Area0) { ++st.droppedArea0; continue; }
        if (cls == Aas140LinkClass::PortalBorder) { ++st.droppedPortalBorder; continue; }
        if (cls == Aas140LinkClass::LinkedCrossCluster) ++st.linkedCrossCluster; else ++st.linked;
        perArea[r.fromAreaNum].push_back(std::uint32_t(i));
        if (!(out.areas[r.fromAreaNum].flags & reachable) || !(out.areas[r.toAreaNum].flags & reachable)) ++st.reachableAreasOutsideRange;
    }
    for (std::size_t a = 0; a < A; ++a) {
        out.areas[a].firstReach = std::int32_t(out.reach.size());
        out.areas[a].numReach = std::int32_t(perArea[a].size());
        for (std::size_t k = 0; k < perArea[a].size(); ++k) {
            const Aas140Reach &r = f.reachabilities[perArea[a][k]];
            Aas140EngineReach d;
            d.travelType = std::int32_t(Aas140EngineTravelType(r.travelType));
            d.toAreaNum = r.toAreaNum; d.fromAreaNum = r.fromAreaNum;
            std::copy(r.start, r.start + 3, d.start); std::copy(r.end, r.end + 3, d.end);
            d.edgeNum = r.edgeNum; d.travelTime = r.travelTime; d.sourceIndex = perArea[a][k];
            out.reach.push_back(d);
        }
    }

    // Area polytopes: bounds from the facets, floor and bounds2d checks, and
    // windings for faces that have none in the file.
    std::vector<AreaPlanes> planes(A);
    for (std::size_t i = 1; i < A; ++i) planes[i] = CollectAreaPlanes(f, int(i));
    std::map<std::pair<std::pair<float, float>, float>, std::int32_t> vertexIds;
    std::map<std::pair<std::int32_t, std::int32_t>, std::int32_t> edgeIds;
    const auto addVertex = [&](const Vec &p) -> std::int32_t {
        const float x = float(p.x), y = float(p.y), z = float(p.z);
        const auto key = std::make_pair(std::make_pair(x, y), z);
        const auto found = vertexIds.find(key);
        if (found != vertexIds.end()) return found->second;
        const std::int32_t id = std::int32_t(out.vertices.size());
        Aas140Vertex v; v.x = x; v.y = y; v.z = z;
        out.vertices.push_back(v); vertexIds[key] = id; ++st.appendedVertices;
        return id;
    };
    const auto addEdge = [&](std::int32_t a, std::int32_t b) -> std::int32_t {
        const auto key = std::make_pair((std::min)(a, b), (std::max)(a, b));
        const auto found = edgeIds.find(key);
        std::int32_t id;
        if (found != edgeIds.end()) id = found->second;
        else {
            id = std::int32_t(out.edges.size());
            Aas140Edge e; e.v[0] = key.first; e.v[1] = key.second;
            out.edges.push_back(e); edgeIds[key] = id; ++st.appendedEdges;
        }
        return out.edges[id].v[0] == a ? id : -id;
    };
    std::vector<int> open;   // areas whose own faces do not close a polytope
    for (std::size_t i = 1; i < A; ++i) {
        const Aas140Area &a = f.areas[i];
        Aas140EngineArea &d = out.areas[i];
        if (!a.numFaces) { ++st.areasWithoutFaces; open.push_back(int(i)); continue; }
        double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
        bool unbounded = false;
        Poly floorPoly;
        for (std::size_t k = 0; k < planes[i].half.size(); ++k) {
            const Poly w = Facet(planes[i].half[k], planes[i].planePair[k], planes[i], NULL);
            if (Unbounded(w)) unbounded = true;
            for (std::size_t j = 0; j < w.size(); ++j) {
                const double c[3] = {w[j].x, w[j].y, w[j].z};
                for (int m = 0; m < 3; ++m) { lo[m] = (std::min)(lo[m], c[m]); hi[m] = (std::max)(hi[m], c[m]); }
            }
            const std::int32_t v = f.faceIndex[a.firstFace + k];
            const Aas140Face &fc = f.faces[v < 0 ? -v : v];
            if (!(fc.flags & AAS140_FACE_FLOOR)) continue;
            // Reconstructed facet versus the stored floor windings on that plane.
            bool match = !w.empty();
            std::vector<Vec> stored;
            for (int q = 0; q < a.numFaces; ++q) {
                const std::int32_t u = f.faceIndex[a.firstFace + q];
                const Aas140Face &g = f.faces[u < 0 ? -u : u];
                if (!(g.flags & AAS140_FACE_FLOOR) || (g.planeNum >> 1) != (fc.planeNum >> 1)) continue;
                for (int e = 0; e < g.numEdges; ++e) {
                    const std::int32_t en = f.edgeIndex[g.firstEdge + e];
                    const Aas140Edge &ed = f.edges[en < 0 ? -en : en];
                    stored.push_back(V(f.vertices[ed.v[0]])); stored.push_back(V(f.vertices[ed.v[1]]));
                }
            }
            for (std::size_t j = 0; match && j < w.size(); ++j) {
                double best = 1e30;
                for (std::size_t q = 0; q < stored.size(); ++q) { const Vec dv = Sub(w[j], stored[q]); best = (std::min)(best, Dot(dv, dv)); }
                match = best < 0.25;   // within 0.5 unit
            }
            for (std::size_t q = 0; match && q < stored.size(); ++q) {
                double best = 1e30;
                for (std::size_t j = 0; j < w.size(); ++j) best = (std::min)(best, SegmentDistance(stored[q], w[j], w[(j + 1) % w.size()]));
                match = best < 0.5;
            }
            if (match) ++st.floorWindingsMatched; else ++st.floorWindingsMismatched;
        }
        if (unbounded || lo[0] > hi[0]) { open.push_back(int(i)); continue; }
        ++st.areasReconstructed;
        for (int m = 0; m < 3; ++m) { d.boundsMin[m] = float(lo[m]); d.boundsMax[m] = float(hi[m]); }
        if (a.bounds2d[0] || a.bounds2d[1] || a.bounds2d[2] || a.bounds2d[3]) {
            const bool ok = std::fabs(lo[0] - a.bounds2d[0]) <= 1 && std::fabs(lo[1] - a.bounds2d[1]) <= 1 &&
                            std::fabs(hi[0] - a.bounds2d[2]) <= 1 && std::fabs(hi[1] - a.bounds2d[3]) <= 1;
            if (ok) ++st.bounds2dMatched; else ++st.bounds2dMismatched;
        }
    }
    // Open areas: the BSP partitions space, so each leaf cell of the area
    // (its ancestors' half-spaces) intersected with the area's own face
    // half-spaces is a bounded part of the area when the tree closes it.
    if (!open.empty()) {
        std::vector<char> wanted(A, 0);
        for (std::size_t k = 0; k < open.size(); ++k) wanted[open[k]] = 1;
        std::vector<std::vector<std::vector<HalfSpace> > > cells(A);
        std::vector<HalfSpace> path;
        struct Frame { std::int32_t node; int side; };
        std::vector<Frame> stack(1, Frame{1, 0});
        while (!stack.empty()) {
            Frame &fr = stack.back();
            if (fr.node <= 0) {
                if (fr.node < 0 && wanted[-fr.node]) cells[-fr.node].push_back(path);
                stack.pop_back();
                if (!stack.empty()) path.pop_back();
                continue;
            }
            if (fr.side == 2) { stack.pop_back(); if (!stack.empty()) path.pop_back(); continue; }
            const Aas140Node &n = f.nodes[fr.node];
            const int side = fr.side++;
            path.push_back(PlaneOf(f.planes[n.planeNum], side == 1));
            stack.push_back(Frame{n.children[side], 0});
        }
        for (std::size_t k = 0; k < open.size(); ++k) {
            const int i = open[k];
            double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
            bool unbounded = cells[i].empty();
            for (std::size_t c = 0; c < cells[i].size() && !unbounded; ++c) {
                std::vector<HalfSpace> hs = cells[i][c];
                hs.insert(hs.end(), planes[i].half.begin(), planes[i].half.end());
                for (std::size_t h = 0; h < hs.size() && !unbounded; ++h) {
                    Poly w = BaseWinding(hs[h]);
                    for (std::size_t j = 0; j < hs.size() && !w.empty(); ++j) if (j != h) Clip(w, hs[j]);
                    RemoveDuplicatePoints(w);
                    if (Unbounded(w)) unbounded = true;
                    for (std::size_t j = 0; j < w.size(); ++j) {
                        const double p[3] = {w[j].x, w[j].y, w[j].z};
                        for (int m = 0; m < 3; ++m) { lo[m] = (std::min)(lo[m], p[m]); hi[m] = (std::max)(hi[m], p[m]); }
                    }
                }
            }
            if (unbounded || lo[0] > hi[0]) {
                ++st.areasUnbounded;
                if (f.areas[i].flags & (AAS140_AREA_REACHABLE_WALK | AAS140_AREA_REACHABLE_FLY)) ++st.areasUnboundedReachable;
                // No closed region is known; claim only the file's center point.
                for (int m = 0; m < 3; ++m) out.areas[i].boundsMin[m] = out.areas[i].boundsMax[m] = f.areas[i].center[m];
                continue;
            }
            ++st.areasBoundedByBspCells;
            for (int m = 0; m < 3; ++m) { out.areas[i].boundsMin[m] = float(lo[m]); out.areas[i].boundsMax[m] = float(hi[m]); }
        }
    }
    for (std::size_t i = 1; i < F; ++i) {
        const Aas140Face &fc = f.faces[i];
        if (fc.numEdges) continue;
        const int owner = fc.areas[0] ? fc.areas[0] : fc.areas[1];
        const int other = fc.areas[0] ? fc.areas[1] : 0;
        if (!owner || !f.areas[owner].numFaces) { ++st.wallFacesWithoutWinding; continue; }
        const HalfSpace h = PlaneOf(f.planes[fc.planeNum], owner != fc.areas[0]);
        Poly w = Facet(h, fc.planeNum >> 1, planes[owner], other && f.areas[other].numFaces ? &planes[other] : NULL);
        if (w.size() < 3 || Unbounded(w)) { ++st.wallFacesWithoutWinding; continue; }
        // Counter-clockwise around the face's own plane normal.
        Vec newell = {0, 0, 0};
        for (std::size_t j = 0; j < w.size(); ++j) newell = Add(newell, Cross(w[j], w[(j + 1) % w.size()]));
        const Aas140Plane &pl = f.planes[fc.planeNum];
        if (newell.x * pl.a + newell.y * pl.b + newell.z * pl.c < 0) std::reverse(w.begin(), w.end());
        std::vector<std::int32_t> ids;
        for (std::size_t j = 0; j < w.size(); ++j) {
            const std::int32_t id = addVertex(w[j]);
            if (ids.empty() || ids.back() != id) ids.push_back(id);
        }
        while (ids.size() > 1 && ids.front() == ids.back()) ids.pop_back();
        if (ids.size() < 3) { ++st.wallFacesWithoutWinding; continue; }
        Aas140EngineFace &d = out.faces[i];
        d.firstEdge = std::int32_t(out.edgeIndex.size());
        for (std::size_t j = 0; j < ids.size(); ++j) out.edgeIndex.push_back(addEdge(ids[j], ids[(j + 1) % ids.size()]));
        d.numEdges = std::int32_t(ids.size());
        ++st.wallWindings;
    }
    return out;
}

} // namespace wolf2009

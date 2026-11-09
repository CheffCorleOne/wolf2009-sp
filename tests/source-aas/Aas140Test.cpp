// SPDX-License-Identifier: GPL-3.0-or-later
// Portable contract tests for the DewmAAS 1.40 reader and its WolfAas140V1
// conversion. The synthetic fixture is authored here (two 64-unit boxes).
// `--private <files>` validates the owner's extracted AASS members, which stay
// outside Git, and reproduces the totals recorded in docs/AAS.md.
#include "AasFile140.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace wolf2009;
int checks = 0, refusals = 0;
void Check(bool value, const std::string &why) {
    ++checks;
    if (!value) throw std::logic_error(why);
}

// ---------------------------------------------------------------------------
// Synthetic fixture
struct Fixture {
    std::string version = "\"1.40\"", crc = "1", extension = "\"aas28\"", playerFloodKey = "playerFlood";
    int boxes = 1;
    std::vector<Aas140Plane> planes;
    std::vector<Aas140Vertex> vertices;
    std::vector<Aas140Edge> edges;
    std::vector<std::int32_t> edgeIndex, faceIndex, portalIndex, featureIndex, locationIndex;
    std::vector<Aas140Face> faces;
    std::vector<Aas140Area> areas;
    std::vector<Aas140Reach> reach;
    std::vector<Aas140Node> nodes;
    std::vector<Aas140Portal> portals;
    std::vector<Aas140Cluster> clusters;
    std::vector<Aas140Feature> features;
    std::vector<std::string> locations;
};
void Plane(Fixture &x, float a, float b, float c, float d) {
    Aas140Plane p; p.a = a; p.b = b; p.c = c; p.d = d; x.planes.push_back(p);
    p.a = -a; p.b = -b; p.c = -c; p.d = -d; x.planes.push_back(p);
}
void Vertex(Fixture &x, float a, float b, float c) { Aas140Vertex v; v.x = a; v.y = b; v.z = c; x.vertices.push_back(v); }
void Edge(Fixture &x, int a, int b) { Aas140Edge e; e.v[0] = a; e.v[1] = b; x.edges.push_back(e); }
void Face(Fixture &x, int plane, int front, int back, int flags, int numEdges = 0, int firstEdge = 0) {
    Aas140Face f; f.planeNum = std::uint16_t(plane); f.areas[0] = std::int16_t(front); f.areas[1] = std::int16_t(back);
    f.flags = std::uint16_t(flags); f.numEdges = numEdges; f.firstEdge = firstEdge; x.faces.push_back(f);
}
Aas140Area Area(int firstFace, int numFaces, float cx, short x0, short x1) {
    Aas140Area a; a.firstFace = firstFace; a.numFaces = std::uint16_t(numFaces); a.cluster = 1;
    a.flags = AAS140_AREA_FLOOR | AAS140_AREA_REACHABLE_WALK;
    a.center[0] = cx; a.center[1] = 32; a.center[2] = 0.1f;
    a.bounds2d[0] = x0; a.bounds2d[1] = 0; a.bounds2d[2] = x1; a.bounds2d[3] = 64;
    return a;
}
Aas140Reach Reach(int from, int to, std::uint32_t type, float x0, float x1) {
    Aas140Reach r; r.fromAreaNum = std::int16_t(from); r.toAreaNum = std::int16_t(to); r.travelType = type;
    r.start[0] = x0; r.start[1] = 32; r.end[0] = x1; r.end[1] = 32; r.edgeNum = 2; r.travelTime = 1;
    return r;
}
Fixture Base() {
    Fixture x;
    Plane(x, 1, 0, 0, 0); Plane(x, 1, 0, 0, -64); Plane(x, 0, 1, 0, 0); Plane(x, 0, 1, 0, -64);
    Plane(x, 0, 0, 1, 0); Plane(x, 0, 0, 1, -64); Plane(x, 1, 0, 0, -128);
    Vertex(x, 0, 0, 0); Vertex(x, 64, 0, 0); Vertex(x, 64, 64, 0); Vertex(x, 0, 64, 0); Vertex(x, 128, 0, 0); Vertex(x, 128, 64, 0);
    Edge(x, 0, 0); Edge(x, 0, 1); Edge(x, 1, 2); Edge(x, 2, 3); Edge(x, 3, 0); Edge(x, 1, 4); Edge(x, 4, 5); Edge(x, 5, 2);
    const int floorA[] = {1, 2, 3, 4}, floorB[] = {5, 6, 7, -2};
    x.edgeIndex.assign(floorA, floorA + 4); x.edgeIndex.insert(x.edgeIndex.end(), floorB, floorB + 4);
    Face(x, 0, 0, 0, 0);                   // dummy
    Face(x, 0, 1, 0, 1); Face(x, 2, 2, 1, 0x40); Face(x, 4, 1, 0, 1); Face(x, 6, 0, 1, 1);
    Face(x, 8, 1, 0, 4, 4, 0); Face(x, 10, 0, 1, 0x21);
    Face(x, 12, 0, 2, 1); Face(x, 4, 2, 0, 1); Face(x, 6, 0, 2, 1); Face(x, 8, 2, 0, 4, 4, 4); Face(x, 10, 0, 2, 0x21);
    const int faceIndex[] = {1, -2, 3, -4, 5, -6, 2, -7, 8, -9, 10, -11};
    x.faceIndex.assign(faceIndex, faceIndex + 12);
    x.areas.push_back(Aas140Area());
    x.areas.push_back(Area(0, 6, 32, 0, 64)); x.areas.push_back(Area(6, 6, 96, 64, 128));
    x.areas[1].contents = AAS140_CONTENTS_LOCATION_VOL; x.areas[1].numLocations = 1;
    x.areas[1].numFeatures = 1;
    x.reach.push_back(Reach(1, 2, AAS140_TFL_WALK, 63, 65)); x.reach.push_back(Reach(2, 1, AAS140_TFL_WALK, 65, 63));
    Aas140Node n;
    x.nodes.push_back(n);
    n.planeNum = 2; n.children[0] = 2; n.children[1] = 3; x.nodes.push_back(n);
    n.planeNum = 8; n.children[0] = -2; n.children[1] = 0; x.nodes.push_back(n);
    n.planeNum = 8; n.children[0] = -1; n.children[1] = 0; x.nodes.push_back(n);
    x.portals.push_back(Aas140Portal());
    Aas140Cluster c; x.clusters.push_back(c); c.tail10 = 0x105e; x.clusters.push_back(c);
    Aas140Feature feature; feature.origin[0] = 8; feature.area = 1; feature.height = 40; x.features.push_back(feature);
    x.featureIndex.push_back(0); x.locationIndex.push_back(1);
    x.locations.push_back("EMPTY"); x.locations.push_back("room");
    return x;
}

// Serialization in the measured layout.
struct Out {
    std::string s;
    void U8(unsigned v) { s.push_back(char(v & 255)); }
    void U16(unsigned v) { U8(v); U8(v >> 8); }
    void U32(std::uint32_t v) { U16(v & 0xffff); U16(v >> 16); }
    void F(float v) { std::uint32_t u; std::memcpy(&u, &v, 4); U32(u); }
};
template <class T, class W> std::string Section(const char *name, const std::vector<T> &items, W write) {
    Out o;
    for (std::size_t i = 0; i < items.size(); ++i) write(o, items[i]);
    std::ostringstream t;
    t << name << " " << items.size() << " {\n\tbin " << o.s.size() << "\n" << o.s << "\n}\n";
    return t.str();
}
std::string Serialize(const Fixture &x) {
    std::ostringstream t;
    t << "DewmAAS " << x.version << "\n\n" << x.crc << "\n\nsettings\n{\n\tbboxes\n\t{\n";
    for (int i = 0; i < x.boxes; ++i) t << "\t\t(-14 -14 0)-(14 14 24)\n";
    t << "\t}\n\tusePatches = 0\n\twriteBrushMap = 0\n\t" << x.playerFloodKey << " = 1\n\tallowSwimReachabilities = 0\n"
         "\tallowFlyReachabilities = 0\n\tgenerateTacticalFeatures = 1\n\tfileExtension = " << x.extension << "\n"
         "\tgravity = (0 0 -1050)\n\tmaxStepHeight = 13\n\tmaxBarrierHeight = 56\n\tmaxWaterJumpHeight = 20\n"
         "\tmaxFallHeight = 90\n\tminFloorCos = 0.6499999762\n\tmaxStandHeight = 72\n\tmaxCrouchHeight = 40\n"
         "\tmaxProneHeight= 24\n\tmaxGapLength = 72\n\ttt_barrierJump = 100\n\ttt_transCrouch = 100\n"
         "\ttt_transProne = 150\n\ttt_waterJump = 100\n\ttt_startWalkOffLedge = 100\n\ttt_gapJump = 200\n}\n";
    std::string s = t.str();
    s += Section("planes", x.planes, [](Out &o, const Aas140Plane &p) { o.F(p.a); o.F(p.b); o.F(p.c); o.F(p.d); });
    s += Section("vertices", x.vertices, [](Out &o, const Aas140Vertex &v) { o.F(v.x); o.F(v.y); o.F(v.z); });
    s += Section("edges", x.edges, [](Out &o, const Aas140Edge &e) { o.U32(e.v[0]); o.U32(e.v[1]); });
    const auto index = [](Out &o, const std::int32_t &v) { o.U32(std::uint32_t(v)); };
    s += Section("edgeIndex", x.edgeIndex, index);
    s += Section("faces", x.faces, [](Out &o, const Aas140Face &f) {
        o.U32(f.numEdges); o.U32(f.firstEdge); o.U16(f.planeNum); o.U16(f.flags); o.U16(f.areas[0]); o.U16(f.areas[1]); });
    s += Section("faceIndex", x.faceIndex, index);
    s += Section("areas", x.areas, [](Out &o, const Aas140Area &a) {
        o.U32(a.reachHead); o.U32(a.revReachHead); o.U32(a.unknown8);
        o.F(a.center[0]); o.F(a.center[1]); o.F(a.center[2]); o.U32(a.firstFace); o.U32(a.unknown28);
        o.U16(a.cluster); for (int k = 0; k < 4; ++k) o.U16(a.bounds2d[k]);
        o.U16(a.flags); o.U16(a.contents); o.U16(a.numFaces); o.U16(a.numFeatures); o.U16(a.firstFeature);
        o.U16(a.numLocations); o.U16(a.firstLocation); });
    s += Section("reachabilities", x.reach, [](Out &o, const Aas140Reach &r) {
        o.U32(r.travelType); for (int k = 0; k < 3; ++k) o.F(r.start[k]); for (int k = 0; k < 3; ++k) o.F(r.end[k]);
        o.U32(r.edgeNum); o.U32(r.nextPtr); o.U32(r.revNextPtr); o.U16(r.toAreaNum); o.U16(r.fromAreaNum);
        o.U16(r.travelTime); o.U8(r.pad46[0]); o.U8(r.pad46[1]); });
    s += Section("nodes", x.nodes, [](Out &o, const Aas140Node &n) { o.U32(n.children[0]); o.U32(n.children[1]); o.U16(n.planeNum); o.U16(n.pad10); });
    s += Section("portals", x.portals, [](Out &o, const Aas140Portal &p) {
        o.U16(p.areaNum); o.U16(p.clusters[0]); o.U16(p.clusters[1]); o.U16(p.clusterAreaNum[0]); o.U16(p.clusterAreaNum[1]); });
    s += Section("portalIndex", x.portalIndex, index);
    s += Section("clusters", x.clusters, [](Out &o, const Aas140Cluster &c) { o.U32(c.firstPortal); o.U32(c.numPortals); o.U16(c.unknown8); o.U16(c.tail10); });
    s += Section("featureIndex", x.featureIndex, index);
    s += Section("features", x.features, [](Out &o, const Aas140Feature &f) {
        for (int k = 0; k < 3; ++k) o.F(f.origin[k]); o.F(f.normal[0]); o.F(f.normal[1]);
        for (int k = 0; k < 8; ++k) o.U8(f.unknown20[k]);
        o.U16(f.flags); o.U16(f.area); o.U16(f.unknown32); o.U8(f.height); o.U8(f.unknown35); });
    s += Section("locationIndex", x.locationIndex, index);
    std::ostringstream l;
    l << "locations " << x.locations.size() << " {\n";
    for (std::size_t i = 0; i < x.locations.size(); ++i) l << "\t" << i << " ( " << x.locations[i] << " )\n";
    l << "}\n";
    return s + l.str();
}
Aas140File Parse(const std::string &s) { return ParseAas140(reinterpret_cast<const unsigned char *>(s.data()), s.size()); }
std::string Replace(std::string s, const std::string &from, const std::string &to) {
    const std::size_t at = s.find(from);
    if (at == std::string::npos) throw std::logic_error("fixture text missing: " + from);
    return s.replace(at, from.size(), to);
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
void Refuses(const std::string &code, const std::function<void()> &work) {
    std::string got = "<accepted>";
    try { work(); } catch (const Aas140Refusal &e) { got = e.Code(); }
    Check(got == code, "expected refusal " + code + ", got " + got);
    ++refusals;
}
void RefusesText(const std::string &code, const std::string &text) { Refuses(code, [&] { Parse(text); }); }
void RefusesFixture(const std::string &code, const std::function<void(Fixture &)> &mutate) {
    Fixture x = Base(); mutate(x); RefusesText(code, Serialize(x));
}

void Synthetic() {
    const std::string text = Serialize(Base());
    const Aas140File f = Parse(text);
    Check(f.version == "1.40" && f.crc == 1 && f.settings.fileExtension == "aas28", "header/settings");
    Check(f.settings.entries.size() == 24 && f.settings.entries[16].key == "maxProneHeight" && f.settings.entries[16].value == "24",
          "settings keep raw keys in file order");
    Check(f.settings.bboxMax[2] == 24 && f.settings.gravity[2] == -1050 && f.settings.tt_gapJump == 200, "settings values");
    Check(f.sections.size() == 16 && f.sections[6].name == "areas" && f.sections[6].bytes == 3 * 56, "section accounting");
    Check(f.clusters[1].tail10 == 0x105e && f.locations[1] == "room", "raw fields kept");
    Check(Aas140PointAreaNum(f, 32, 32, 10) == 1 && Aas140PointAreaNum(f, 96, 32, 10) == 2 && Aas140PointAreaNum(f, 32, 32, -10) == 0,
          "BSP lookup");
    const Aas140CentroidStats c = Aas140FloorCentroidLookup(f);
    Check(c.floorFaces == 2 && c.ownArea == 2, "floor centroid lookup");
    const Aas140FloorLoop loop = Aas140ChainFloorFace(f, 10);
    Check(loop.startVertex.size() == 4 && loop.startVertex[0] == 1 && loop.startVertex[3] == 2, "floor chain by shared vertex");

    const Aas140EngineFile e = ConvertAas140ToEngine(f);
    Check(e.profile == "WolfAas140V1" && e.stats.linked == 2 && e.reach.size() == 2, "links");
    Check(e.areas[1].firstReach == 0 && e.areas[1].numReach == 1 && e.areas[2].firstReach == 1, "per-area reach ranges");
    Check(e.clusters[1].numAreas == 2 && e.clusters[1].numReachableAreas == 2 && e.areas[1].clusterAreaNum == 0 &&
          e.areas[2].clusterAreaNum == 1, "Doom 3 cluster numbering");
    Check(Near(e.areas[1].boundsMin[0], 0) && Near(e.areas[1].boundsMax[0], 64) && Near(e.areas[1].boundsMax[2], 64) &&
          Near(e.areas[2].boundsMin[0], 64) && Near(e.areas[2].boundsMax[0], 128) && Near(e.areas[2].boundsMin[2], 0), "area bounds from the polytope");
    Check(e.stats.areasReconstructed == 2 && e.stats.floorWindingsMatched == 2 && e.stats.bounds2dMatched == 2, "polytope checks");
    Check(e.stats.wallWindings == 9 && e.faces[2].numEdges == 4 && e.faces[1].numEdges == 4, "wall windings");
    Check(e.areas[1].center[2] == 0.1f && e.areas[1].travelFlags == std::int32_t(D3_TFL_AIR) &&
          e.areas[1].contents == 0, "center kept, contents remapped");
    // Floor loops are counter-clockwise from above and signed by direction.
    for (int face = 5; face <= 10; face += 5) {
        double area2 = 0;
        const Aas140EngineFace &ef = e.faces[face];
        for (int k = 0; k < ef.numEdges; ++k) {
            const std::int32_t s = e.edgeIndex[ef.firstEdge + k], n = e.edgeIndex[ef.firstEdge + (k + 1) % ef.numEdges];
            const Aas140Edge &a = e.edges[s < 0 ? -s : s], &b = e.edges[n < 0 ? -n : n];
            Check(a.v[s < 0 ? 0 : 1] == b.v[n < 0 ? 1 : 0], "re-signed floor edges chain head to tail");
            const Aas140Vertex &p = e.vertices[a.v[s < 0 ? 1 : 0]], &q = e.vertices[a.v[s < 0 ? 0 : 1]];
            area2 += double(p.x) * q.y - double(q.x) * p.y;
        }
        Check(area2 > 0, "floor loop is counter-clockwise");
    }
    // Bit remaps.
    Check(Aas140EngineTravelType(0x202) == (0x2 | WOLF_AAS_TFL_PLAYERONLY) && Aas140EngineTravelType(0) == WOLF_AAS_TFL_UNTYPED &&
          Aas140EngineTravelType(0x800008) == 0x800008, "travel remap");
    Check(Aas140EngineAreaFlags(0x1051) == (0x1041 | WOLF_AAS_AREA_VISITED), "area flag remap");

    // Tolerated exceptions are counted, not refused.
    {
        Fixture x = Base();
        x.reach.insert(x.reach.begin(), Reach(0, 1, AAS140_TFL_PLAYERONLY, 0, 1));
        x.reach.push_back(Reach(2, 1, 0, 64, 63));
        x.features[0].area = 0;
        x.areas.push_back(Area(12, 0, 0, 0, 0));
        x.areas[3].bounds2d[2] = x.areas[3].bounds2d[3] = 0;
        x.nodes[3].children[1] = -3;
        const Aas140File g = Parse(Serialize(x));
        Check(g.exceptions.area0PlayerOnlyLinks == 1 && g.exceptions.travelTypeZeroLinks == 1 && g.exceptions.area0Features == 1 &&
              g.exceptions.floorAreasWithoutFloorFace == 1, "counted exceptions");
        const Aas140EngineFile h = ConvertAas140ToEngine(g);
        Check(h.stats.droppedArea0 == 1 && h.linkClass[0] == Aas140LinkClass::Area0 && h.stats.linked == 3 &&
              h.reach.back().travelType == std::int32_t(WOLF_AAS_TFL_UNTYPED) && h.stats.areasWithoutFaces == 1, "exception conversion");
    }

    // Refusals with named reasons.
    RefusesText("header", Replace(text, "DewmAAS", "DoomAAS"));
    RefusesText("version", Replace(text, "\"1.40\"", "\"1.07\""));
    RefusesText("settings-key", Replace(text, "playerFlood", "playerFloods"));
    RefusesText("settings-key", Replace(text, "\ttt_gapJump = 200\n", ""));
    RefusesText("settings-bboxes", Replace(text, "(-14 -14 0)-(14 14 24)\n", "(-14 -14 0)-(14 14 24)\n(-1 -1 0)-(1 1 2)\n"));
    RefusesText("settings-extension", Replace(text, "\"aas28\"", "\"aas48\""));
    RefusesText("settings-value", Replace(text, "maxStepHeight = 13", "maxStepHeight = x13"));
    RefusesText("section-order", Replace(text, "vertices 6", "vertexes 6"));
    RefusesText("section-text-records", Replace(text, "\tbin 224\n", "\t0 ( 1 0 0 0 )\n"));
    RefusesText("bin-separator", Replace(text, "\tbin 224\n", "\tbin 224 \n"));
    RefusesText("record-size", Replace(text, "planes 14 {", "planes 13 {"));
    RefusesText("bin-overflow", text.substr(0, text.find("\tbin 224\n") + 20));
    RefusesText("trailing-bytes", text + "x");
    RefusesText("truncated", text.substr(0, text.find("planes")));
    RefusesText("section-close", Replace(text, "( room )\n}", "( room )\n]"));
    RefusesText("locations", Replace(text, "( EMPTY )", "( NONE )"));
    RefusesFixture("plane-pair", [](Fixture &x) { x.planes[1].d = 3; });
    RefusesFixture("dummy-record", [](Fixture &x) { x.edges[0].v[1] = 1; });
    RefusesFixture("dummy-record", [](Fixture &x) { x.areas[0].cluster = 1; });
    RefusesFixture("range-edge-vertex", [](Fixture &x) { x.edges[3].v[1] = 99; });
    RefusesFixture("range-edge-index", [](Fixture &x) { x.edgeIndex[2] = 0; });
    RefusesFixture("face-flags", [](Fixture &x) { x.faces[1].flags = 0x8; });
    RefusesFixture("face-edges", [](Fixture &x) { x.faces[5].flags = 1; });
    RefusesFixture("face-edge-range", [](Fixture &x) { x.faces[10].firstEdge = 3; });
    RefusesFixture("floor-loop", [](Fixture &x) { std::swap(x.edgeIndex[1], x.edgeIndex[2]); });
    RefusesFixture("range-face-plane", [](Fixture &x) { x.faces[3].planeNum = 99; });
    RefusesFixture("range-face-index", [](Fixture &x) { x.faceIndex[0] = 99; });
    RefusesFixture("face-side", [](Fixture &x) { x.faceIndex[0] = -1; });
    RefusesFixture("area-flags", [](Fixture &x) { x.areas[1].flags |= 0x8000; });
    RefusesFixture("area-contents", [](Fixture &x) { x.areas[2].contents = 0x1; });
    RefusesFixture("area-runtime-fields", [](Fixture &x) { x.areas[2].unknown28 = 1; });
    RefusesFixture("portal-contents", [](Fixture &x) { x.areas[2].contents = AAS140_CONTENTS_CLUSTERPORTAL; });
    RefusesFixture("range-area-cluster", [](Fixture &x) { x.areas[2].cluster = 5; });
    RefusesFixture("portal-area", [](Fixture &x) { x.areas[2].cluster = -1; x.areas[2].contents = AAS140_CONTENTS_CLUSTERPORTAL; });
    RefusesFixture("area-face-range", [](Fixture &x) { x.areas[2].numFaces = 5; });
    RefusesFixture("location-contents", [](Fixture &x) { x.areas[1].contents = 0; });
    RefusesFixture("range-location-index", [](Fixture &x) { x.locationIndex[0] = 0; });
    RefusesFixture("range-feature-area", [](Fixture &x) { x.features[0].area = 9; });
    RefusesFixture("area-feature-range", [](Fixture &x) { x.areas[1].numFeatures = 0; });
    RefusesFixture("reach-order", [](Fixture &x) { std::swap(x.reach[0], x.reach[1]); });
    RefusesFixture("reach-self", [](Fixture &x) { x.reach[0].toAreaNum = 1; });
    RefusesFixture("reach-travel-type", [](Fixture &x) { x.reach[0].travelType = 0x400; });
    RefusesFixture("reach-area0", [](Fixture &x) { x.reach[0].toAreaNum = 0; });
    RefusesFixture("reach-runtime-fields", [](Fixture &x) { x.reach[1].nextPtr = 1; });
    RefusesFixture("range-reach-edge", [](Fixture &x) { x.reach[1].edgeNum = 99; });
    RefusesFixture("range-node-child", [](Fixture &x) { x.nodes[2].children[1] = 99; });
    RefusesFixture("bsp-tree", [](Fixture &x) { x.nodes[2].children[1] = 3; });
    RefusesFixture("bsp-leaf", [](Fixture &x) { x.nodes[3].children[0] = 0; });
    RefusesFixture("counts", [](Fixture &x) { x.nodes.resize(1); });
    Refuses("crc-policy", [&] { ConvertAas140ToEngine(Parse(Replace(text, "\n\n1\n\n", "\n\n2\n\n"))); });
}

// ---------------------------------------------------------------------------
// Private all-files validation
std::vector<unsigned char> ReadFile(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
int DownToFloor(const Aas140File &f, double x, double y, double z, int &drop) {
    for (drop = 0; drop <= 128; ++drop) {
        const int a = Aas140PointAreaNum(f, x, y, z + 1 - drop);
        if (a && (f.areas[a].flags & AAS140_AREA_FLOOR)) return a;
    }
    return 0;
}
int Private(int argc, char **argv) {
    std::size_t files = 0, planes = 0, vertices = 0, edges = 0, faces = 0, areas = 0, reach = 0, nodes = 0, portals = 0,
                clusters = 0, features = 0, locations = 0, portalAreas = 0;
    Aas140Exceptions ex;
    Aas140CentroidStats cs;
    Aas140ConversionStats st;
    bool trainyard = false;
    for (int i = 2; i < argc; ++i) {
        const std::vector<unsigned char> bytes = ReadFile(argv[i]);
        const Aas140File f = ParseAas140(bytes.data(), bytes.size());
        const Aas140EngineFile e = ConvertAas140ToEngine(f);
        const Aas140CentroidStats c = Aas140FloorCentroidLookup(f);
        ++files;
        planes += f.planes.size(); vertices += f.vertices.size(); edges += f.edges.size(); faces += f.faces.size();
        areas += f.areas.size(); reach += f.reachabilities.size(); nodes += f.nodes.size(); portals += f.portals.size();
        clusters += f.clusters.size(); features += f.features.size(); locations += f.locations.size();   // entries, EMPTY included
        for (std::size_t a = 1; a < f.areas.size(); ++a) if (f.areas[a].contents & AAS140_CONTENTS_CLUSTERPORTAL) ++portalAreas;
        ex.area0PlayerOnlyLinks += f.exceptions.area0PlayerOnlyLinks; ex.travelTypeZeroLinks += f.exceptions.travelTypeZeroLinks;
        ex.area0Features += f.exceptions.area0Features; ex.floorAreasWithoutFloorFace += f.exceptions.floorAreasWithoutFloorFace; ex.floorFaceAreasWithoutFloorFlag += f.exceptions.floorFaceAreasWithoutFloorFlag;
        ex.nonUnitPlanes += f.exceptions.nonUnitPlanes; ex.unlistedPortalBackSides += f.exceptions.unlistedPortalBackSides;
        cs.floorFaces += c.floorFaces; cs.ownArea += c.ownArea; cs.noFloorArea += c.noFloorArea; cs.solid += c.solid;
        cs.otherFloorArea += c.otherFloorArea;
        const Aas140ConversionStats &s = e.stats;
        st.linked += s.linked; st.linkedCrossCluster += s.linkedCrossCluster; st.droppedArea0 += s.droppedArea0;
        st.droppedPortalBorder += s.droppedPortalBorder; st.areasReconstructed += s.areasReconstructed;
        st.areasUnbounded += s.areasUnbounded; st.areasWithoutFaces += s.areasWithoutFaces; st.areasBoundedByBspCells += s.areasBoundedByBspCells; st.areasUnboundedReachable += s.areasUnboundedReachable;
        st.floorWindingsMatched += s.floorWindingsMatched; st.floorWindingsMismatched += s.floorWindingsMismatched;
        st.bounds2dMatched += s.bounds2dMatched; st.bounds2dMismatched += s.bounds2dMismatched; st.wallWindings += s.wallWindings;
        st.wallFacesWithoutWinding += s.wallFacesWithoutWinding; st.floorLoopsReversed += s.floorLoopsReversed;
        st.reachableAreasOutsideRange += s.reachableAreasOutsideRange; st.unlistedPortalSidesNumbered += s.unlistedPortalSidesNumbered; st.cluster0ReachableFlagsCleared += s.cluster0ReachableFlagsCleared;
        std::printf("AAS140_FILE %s areas=%u reach=%u linked=%u cross=%u dropped_area0=%u dropped_border=%u unbounded=%u floor_mismatch=%u walls=%u depth=%u\n",
                    argv[i], unsigned(f.areas.size()), unsigned(f.reachabilities.size()), unsigned(s.linked), unsigned(s.linkedCrossCluster),
                    unsigned(s.droppedArea0), unsigned(s.droppedPortalBorder), unsigned(s.areasUnbounded),
                    unsigned(s.floorWindingsMismatched), unsigned(s.wallWindings), unsigned(f.maxTreeDepth));
        if (std::strstr(argv[i], "trainyard_trainyard")) {
            trainyard = true;
            // The greeter origin lies exactly on its floor plane (z 296), where
            // the strict '> 0' rule returns solid; the search starts 1 unit up.
            int drop = 0, greeterDrop = 0;
            const int greeterPoint = Aas140PointAreaNum(f, 176.75, 271.75, 296);
            const int greeter = DownToFloor(f, 176.75, 271.75, 296, greeterDrop);
            const int ladder = DownToFloor(f, -271, 702, 318, drop);
            std::printf("AAS140_TRAINYARD_GREETER point_area=%d floor_area=%d drop=%d\n", greeterPoint, greeter, greeterDrop);
            Check(greeterDrop == 0, "greeter stands on its floor area");
            bool trapDoor = false;
            const Aas140Area &a = f.areas[ladder];
            for (int k = 0; k < a.numLocations; ++k)
                if (f.locationIndex[a.firstLocation + k] == 26 && f.locations[26] == "trap_door_room") trapDoor = true;
            std::printf("AAS140_TRAINYARD greeter_area=%d greeter_cluster=%d ladder_floor_area=%d drop=%d ladder_cluster=%d trap_door_room=%d locations=%u\n",
                        greeter, f.areas[greeter].cluster, ladder, drop, a.cluster, trapDoor, unsigned(f.locations.size() - 1));
            Check(greeter == 6199 && f.areas[greeter].cluster < 0 && ladder == 8040 && drop == 6 && a.cluster == 575 && trapDoor &&
                  f.locations.size() == 37, "trainyard greeter/ladder areas and trap_door_room");
            Check(e.areas[greeter].cluster < 0 && (e.areas[ladder].flags & D3_AREA_REACHABLE_WALK), "trainyard engine areas reachable");
        }
    }
    std::printf("AAS140_TOTALS files=%u planes=%u vertices=%u edges=%u faces=%u areas=%u reach=%u nodes=%u portals=%u clusters=%u features=%u location_entries=%u portal_areas=%u\n",
                unsigned(files), unsigned(planes), unsigned(vertices), unsigned(edges), unsigned(faces), unsigned(areas), unsigned(reach),
                unsigned(nodes), unsigned(portals), unsigned(clusters), unsigned(features), unsigned(locations), unsigned(portalAreas));
    std::printf("AAS140_EXCEPTIONS area0_player_only=%u travel_type_zero=%u area0_features=%u floor_areas_without_floor_face=%u floor_face_areas_without_floor_flag=%u non_unit_planes=%u unlisted_portal_back_sides=%u\n",
                unsigned(ex.area0PlayerOnlyLinks), unsigned(ex.travelTypeZeroLinks), unsigned(ex.area0Features),
                unsigned(ex.floorAreasWithoutFloorFace), unsigned(ex.floorFaceAreasWithoutFloorFlag), unsigned(ex.nonUnitPlanes), unsigned(ex.unlistedPortalBackSides));
    std::printf("AAS140_CENTROIDS floor_faces=%u own=%u no_floor=%u solid=%u other_floor=%u\n", unsigned(cs.floorFaces),
                unsigned(cs.ownArea), unsigned(cs.noFloorArea), unsigned(cs.solid), unsigned(cs.otherFloorArea));
    std::printf("AAS140_CONVERSION linked=%u linked_cross_cluster=%u dropped_area0=%u dropped_portal_border=%u areas_reconstructed=%u "
                "areas_unbounded=%u areas_without_faces=%u floor_matched=%u floor_mismatched=%u bounds2d_matched=%u bounds2d_mismatched=%u "
                "wall_windings=%u walls_without_winding=%u floor_loops_reversed=%u links_outside_reachable=%u cluster0_flags_cleared=%u unlisted_sides_numbered=%u bsp_closed=%u unbounded_reachable=%u\n",
                unsigned(st.linked), unsigned(st.linkedCrossCluster), unsigned(st.droppedArea0), unsigned(st.droppedPortalBorder),
                unsigned(st.areasReconstructed), unsigned(st.areasUnbounded), unsigned(st.areasWithoutFaces),
                unsigned(st.floorWindingsMatched), unsigned(st.floorWindingsMismatched), unsigned(st.bounds2dMatched),
                unsigned(st.bounds2dMismatched), unsigned(st.wallWindings), unsigned(st.wallFacesWithoutWinding),
                unsigned(st.floorLoopsReversed), unsigned(st.reachableAreasOutsideRange), unsigned(st.cluster0ReachableFlagsCleared), unsigned(st.unlistedPortalSidesNumbered), unsigned(st.areasBoundedByBspCells), unsigned(st.areasUnboundedReachable));
    Check(files == 27 && trainyard, "private mode expects all 27 AASS members including trainyard");
    Check(planes == 225082 && vertices == 306068 && edges == 415034 && faces == 1147184 && areas == 196388 && reach == 450298 &&
          nodes == 529913 && portals == 3344 && clusters == 12800 && features == 9919 && locations == 450 && portalAreas == 3317,
          "record totals differ from docs/AAS.md");
    Check(ex.area0PlayerOnlyLinks == 11 && ex.travelTypeZeroLinks == 2 && ex.area0Features == 11 && ex.floorAreasWithoutFloorFace == 157 &&
          ex.floorFaceAreasWithoutFloorFlag == 33 && ex.unlistedPortalBackSides == 349,
          "exception counts differ from docs/AAS.md");
    Check(cs.floorFaces == 120046 && cs.ownArea == 117772 && cs.noFloorArea == 2018 && cs.solid == 195 && cs.otherFloorArea == 61,
          "BSP centroid counts differ from docs/AAS.md");
    Check(st.droppedArea0 == 11, "every area-0 link is retained raw only");
    // Conversion results recorded in docs/AAS.md section 7; a change must be explained.
    Check(st.linked == 388992 && st.linkedCrossCluster == 60464 && st.droppedPortalBorder == 831 &&
          st.unlistedPortalSidesNumbered == 349 && st.reachableAreasOutsideRange == 0 && st.cluster0ReachableFlagsCleared == 0,
          "link classification differs");
    Check(st.areasReconstructed == 195768 && st.areasBoundedByBspCells == 572 && st.areasUnbounded == 21 &&
          st.areasUnboundedReachable == 20 && st.floorWindingsMatched == 119383 && st.floorWindingsMismatched == 663 &&
          st.wallWindings == 1025305 && st.wallFacesWithoutWinding == 1806 && st.floorLoopsReversed == 118948,
          "polytope reconstruction differs");
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        Synthetic();
        std::printf("AAS140_PORTABLE_OK checks=%d refusals=%d\n", checks, refusals);
        if (argc >= 2 && std::strcmp(argv[1], "--private") == 0) {
            Private(argc, argv);
            std::printf("AAS140_PRIVATE_OK checks=%d\n", checks);
        }
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "AAS140_TEST_FAIL checks=%d %s\n", checks, e.what());
        return 1;
    }
}

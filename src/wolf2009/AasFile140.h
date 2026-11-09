// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_AAS_FILE_140_H
#define WOLF2009_AAS_FILE_140_H
// Portable reader and validator for the Wolfenstein (2009) `DewmAAS "1.40"`
// navigation stream (one AASS member per map pack). See docs/AAS.md.
//
// The format is token text with raw `bin <bytes>` blocks in a fixed section
// order. Binary records are little-endian memory images; this reader decodes
// each field explicitly and retains every raw field, including fields whose
// meaning is unknown. It performs no operating-system I/O.
//
// ParseAas140 accepts exactly the measured shape and the invariants listed in
// docs/AAS.md section 2.4. The three documented exceptions are tolerated and
// counted. Everything else throws Aas140Refusal with a stable reason code.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace wolf2009 {

class Aas140Refusal : public std::runtime_error {
  public:
    Aas140Refusal(const std::string &code, const std::string &detail);
    const std::string &Code() const { return code; }
  private:
    std::string code;
};

// Wolf bit names (engine name tables, docs/AAS.md 2.3). Values are raw file bits.
enum : std::uint32_t {
    AAS140_TFL_INVALID = 0x1, AAS140_TFL_WALK = 0x2, AAS140_TFL_CROUCH = 0x4,
    AAS140_TFL_WALKOFFLEDGE = 0x8, AAS140_TFL_BARRIERJUMP = 0x10, AAS140_TFL_JUMP = 0x20,
    AAS140_TFL_LADDER = 0x40, AAS140_TFL_SWIM = 0x80, AAS140_TFL_WATERJUMP = 0x100,
    AAS140_TFL_PLAYERONLY = 0x200, AAS140_TFL_FLY = 0x800,
    AAS140_TFL_PRONE = 0x800000, AAS140_TFL_GAPJUMP = 0x1000000,
};
const std::uint32_t AAS140_TRAVEL_KNOWN_BITS = 0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u | 0x40u |
    0x80u | 0x100u | 0x200u | 0x800u | 0x800000u | 0x1000000u;
enum : std::uint16_t {
    AAS140_AREA_FLOOR = 0x1, AAS140_AREA_GAP = 0x2, AAS140_AREA_LEDGE = 0x4, AAS140_AREA_LADDER = 0x8,
    AAS140_AREA_VISITED = 0x10, AAS140_AREA_CROUCH = 0x20, AAS140_AREA_REACHABLE_WALK = 0x40,
    AAS140_AREA_REACHABLE_FLY = 0x80, AAS140_AREA_PRONE = 0x100, AAS140_AREA_DRIVABLE = 0x200,
    AAS140_AREA_DRIVABLE_COMBAT = 0x400, AAS140_AREA_DRIVABLE_NARROW = 0x800,
    AAS140_AREA_OUTSIDE = 0x1000, AAS140_AREA_AXISALLIGNED_RECT = 0x2000,
};
const std::uint16_t AAS140_AREA_KNOWN_BITS = 0x3fff;
const std::uint16_t AAS140_CONTENTS_CLUSTERPORTAL = 0x4, AAS140_CONTENTS_LOCATION_VOL = 0x80;
// 0x2, 0x10 and 0x200 occur with unknown meaning; any other contents bit refuses.
const std::uint16_t AAS140_CONTENTS_ADMITTED_BITS = 0x2 | 0x4 | 0x10 | 0x80 | 0x200;
const std::uint16_t AAS140_FACE_SOLID = 0x1, AAS140_FACE_FLOOR = 0x4;
// 0x20 (ceiling side) and 0x40 (between two areas) occur with unknown names.
const std::uint16_t AAS140_FACE_ADMITTED_BITS = 0x1 | 0x4 | 0x20 | 0x40;

struct Aas140Plane { float a = 0, b = 0, c = 0, d = 0; };       // a*x+b*y+c*z+d = 0
struct Aas140Vertex { float x = 0, y = 0, z = 0; };
struct Aas140Edge { std::int32_t v[2] = {0, 0}; };
struct Aas140Face {
    std::int32_t numEdges = 0, firstEdge = 0;
    std::uint16_t planeNum = 0, flags = 0;
    std::int16_t areas[2] = {0, 0};
};
struct Aas140Area {
    std::uint32_t reachHead = 0, revReachHead = 0;   // runtime pointers, zero in files
    std::uint32_t unknown8 = 0, unknown28 = 0;       // always zero, meaning unknown
    float center[3] = {0, 0, 0};
    std::int32_t firstFace = 0;
    std::int16_t cluster = 0;                        // negative = -portal
    std::int16_t bounds2d[4] = {0, 0, 0, 0};         // minX minY maxX maxY
    std::uint16_t flags = 0, contents = 0, numFaces = 0;
    std::uint16_t numFeatures = 0, firstFeature = 0, numLocations = 0, firstLocation = 0;
};
struct Aas140Reach {
    std::uint32_t travelType = 0;
    float start[3] = {0, 0, 0}, end[3] = {0, 0, 0};
    std::int32_t edgeNum = 0;
    std::uint32_t nextPtr = 0, revNextPtr = 0;       // runtime pointers, zero in files
    std::int16_t toAreaNum = 0, fromAreaNum = 0;
    std::uint16_t travelTime = 0;
    std::uint8_t pad46[2] = {0, 0};
};
struct Aas140Node { std::int32_t children[2] = {0, 0}; std::uint16_t planeNum = 0, pad10 = 0; };
struct Aas140Portal { std::int16_t areaNum = 0, clusters[2] = {0, 0}, clusterAreaNum[2] = {0, 0}; };
struct Aas140Cluster {
    std::int32_t firstPortal = 0, numPortals = 0;
    std::uint16_t unknown8 = 0, tail10 = 0;          // tail is uninitialised padding
};
struct Aas140Feature {
    float origin[3] = {0, 0, 0};
    float normal[2] = {0, 0};
    std::uint8_t unknown20[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::uint16_t flags = 0, area = 0, unknown32 = 0;
    std::uint8_t height = 0, unknown35 = 0;
};
struct Aas140Setting { std::string key, value; };   // raw tokens in file order

struct Aas140Settings {
    std::vector<Aas140Setting> entries;
    float bboxMin[3] = {0, 0, 0}, bboxMax[3] = {0, 0, 0};
    bool usePatches = false, writeBrushMap = false, playerFlood = false;
    bool allowSwimReachabilities = false, allowFlyReachabilities = false, generateTacticalFeatures = false;
    std::string fileExtension;
    float gravity[3] = {0, 0, 0};
    float maxStepHeight = 0, maxBarrierHeight = 0, maxWaterJumpHeight = 0, maxFallHeight = 0, minFloorCos = 0;
    float maxStandHeight = 0, maxCrouchHeight = 0, maxProneHeight = 0, maxGapLength = 0;
    int tt_barrierJump = 0, tt_transCrouch = 0, tt_transProne = 0, tt_waterJump = 0;
    int tt_startWalkOffLedge = 0, tt_gapJump = 0;
};

// Counted exceptions that docs/AAS.md requires a reader to tolerate.
struct Aas140Exceptions {
    std::size_t area0PlayerOnlyLinks = 0;     // TFL_PLAYERONLY with fromAreaNum or toAreaNum 0
    std::size_t travelTypeZeroLinks = 0;      // travelType == 0
    std::size_t area0Features = 0;            // feature area field 0
    std::size_t floorAreasWithoutFloorFace = 0;
    std::size_t floorFaceAreasWithoutFloorFlag = 0;   // the converse; with the above, the 190 of docs/AAS.md
    std::size_t nonUnitPlanes = 0;            // |normal| differs from 1 by more than 1e-3
    std::size_t unlistedPortalBackSides = 0;  // back cluster does not list the portal
};

struct Aas140Section { std::string name; std::size_t count = 0, bytes = 0, offset = 0; };

struct Aas140File {
    std::string version;                      // "1.40"
    std::uint32_t crc = 0;                    // map CRC field
    Aas140Settings settings;
    std::vector<Aas140Section> sections;      // file order, with byte accounting
    std::vector<Aas140Plane> planes;
    std::vector<Aas140Vertex> vertices;
    std::vector<Aas140Edge> edges;
    std::vector<std::int32_t> edgeIndex;
    std::vector<Aas140Face> faces;
    std::vector<std::int32_t> faceIndex;
    std::vector<Aas140Area> areas;
    std::vector<Aas140Reach> reachabilities;
    std::vector<Aas140Node> nodes;
    std::vector<Aas140Portal> portals;
    std::vector<std::int32_t> portalIndex;
    std::vector<Aas140Cluster> clusters;
    std::vector<std::int32_t> featureIndex;
    std::vector<Aas140Feature> features;
    std::vector<std::int32_t> locationIndex;
    std::vector<std::string> locations;       // entry 0 is EMPTY
    Aas140Exceptions exceptions;
    std::size_t maxReachPerArea = 0, maxTreeDepth = 0;
};

// Parses and validates; throws Aas140Refusal. The input is not retained.
Aas140File ParseAas140(const unsigned char *bytes, std::size_t size);

// BSP lookup with the Doom 3 descent rule (docs/AAS.md 2.3 nodes): go to
// children[0] when a*x+b*y+c*z+d > 0 (in double), otherwise children[1].
// Returns 0 for solid.
int Aas140PointAreaNum(const Aas140File &file, double x, double y, double z);

// Floor winding of a floor face in loop order, chained by shared vertex.
// `startVertex[i]` is the first vertex of edge i in that loop. Throws if the
// edges do not form a closed chain.
struct Aas140FloorLoop { std::vector<std::int32_t> edges, startVertex; };
Aas140FloorLoop Aas140ChainFloorFace(const Aas140File &file, int faceNum);

// Acceptance check 4: centroid of each floor winding (mean of both endpoints
// of every edge), raised 1 unit, looked up with Aas140PointAreaNum.
struct Aas140CentroidStats {
    std::size_t floorFaces = 0, ownArea = 0, noFloorArea = 0, solid = 0, otherFloorArea = 0;
};
Aas140CentroidStats Aas140FloorCentroidLookup(const Aas140File &file);

// ---------------------------------------------------------------------------
// Conversion to the Doom 3 1.07 in-memory model (idAASFile fields), profile
// WolfAas140V1. The result is plain data; a native adapter copies it.

// Reserved engine travel bits for Wolf meanings that collide with or are absent
// from Doom 3. No Doom 3 AI travel mask contains them, so an AI admits these
// links only by an explicit caller choice.
const std::uint32_t WOLF_AAS_TFL_PLAYERONLY = 1u << 13;   // Wolf 0x200 (Doom 3 TFL_TELEPORT)
const std::uint32_t WOLF_AAS_TFL_UNTYPED = 1u << 14;      // Wolf travelType 0
const std::uint32_t WOLF_AAS_TFL_PRONE = 0x800000;        // raw bit, unused by Doom 3
const std::uint32_t WOLF_AAS_TFL_GAPJUMP = 0x1000000;     // raw bit, unused by Doom 3
// Wolf AREA_VISITED uses Doom 3's AREA_LIQUID value; move it to a free bit.
const std::uint16_t WOLF_AAS_AREA_VISITED = 1u << 14;
// Doom 3 values used by the conversion.
const std::uint32_t D3_TFL_AIR = 1u << 22;
const std::uint16_t D3_AREA_REACHABLE_WALK = 0x40, D3_AREA_REACHABLE_FLY = 0x80;
const std::uint16_t D3_AREACONTENTS_CLUSTERPORTAL = 0x4;
const std::uint16_t D3_FACE_SOLID = 0x1, D3_FACE_FLOOR = 0x4;

std::uint32_t Aas140EngineTravelType(std::uint32_t wolfTravelType);
std::uint16_t Aas140EngineAreaFlags(std::uint16_t wolfFlags);

// Why a reachability is or is not linked into the engine's per-area lists.
enum class Aas140LinkClass : std::uint8_t {
    Linked = 0,                 // ordinary Doom 3 routing link
    LinkedCrossCluster = 1,     // between two different positive clusters; linked,
                                // Doom 3 routing never follows it ("don't leave the cluster")
    Area0 = 2,                  // from/to dummy area 0; retained raw only
    PortalBorder = 3,           // reverse flood would enter a portal that does not
                                // border the flood cluster; Doom 3 ClusterAreaNum would
                                // return another cluster's index. Retained raw only.
};

struct Aas140EngineFace {
    std::uint16_t planeNum = 0, flags = 0;
    std::int32_t numEdges = 0, firstEdge = 0;
    std::int16_t areas[2] = {0, 0};
};
struct Aas140EngineArea {
    std::int32_t numFaces = 0, firstFace = 0;
    float boundsMin[3] = {0, 0, 0}, boundsMax[3] = {0, 0, 0};
    float center[3] = {0, 0, 0};
    std::uint16_t flags = 0, contents = 0;
    std::int16_t cluster = 0, clusterAreaNum = 0;
    std::int32_t travelFlags = 0;
    std::int32_t firstReach = 0, numReach = 0;   // range in Aas140EngineFile::reach
};
struct Aas140EngineReach {
    std::int32_t travelType = 0;
    std::int16_t toAreaNum = 0, fromAreaNum = 0;
    float start[3] = {0, 0, 0}, end[3] = {0, 0, 0};
    std::int32_t edgeNum = 0;
    std::uint16_t travelTime = 0;
    std::uint32_t sourceIndex = 0;               // index in Aas140File::reachabilities
};
struct Aas140EnginePortal { std::int16_t areaNum = 0, clusters[2] = {0, 0}, clusterAreaNum[2] = {0, 0}; };
struct Aas140EngineCluster { std::int32_t numAreas = 0, numReachableAreas = 0, numPortals = 0, firstPortal = 0; };

struct Aas140ConversionStats {
    std::size_t linked = 0, linkedCrossCluster = 0, droppedArea0 = 0, droppedPortalBorder = 0;
    // Area polytopes from the face half-spaces (docs/AAS.md 5, derived field 2).
    std::size_t areasReconstructed = 0, areasUnbounded = 0, areasWithoutFaces = 0;
    std::size_t areasBoundedByBspCells = 0;   // own faces open; closed by the BSP leaf cells
    std::size_t areasUnboundedReachable = 0;  // still open and flagged reachable (bounds stay zero)
    std::size_t floorWindingsMatched = 0, floorWindingsMismatched = 0;
    std::size_t bounds2dMatched = 0, bounds2dMismatched = 0;   // xy extent vs bounds2d within 1 unit
    std::size_t wallWindings = 0, wallFacesWithoutWinding = 0, appendedVertices = 0, appendedEdges = 0;
    std::size_t floorLoopsReversed = 0;
    // Routing preconditions (docs/AAS.md acceptance 5).
    std::size_t clusterAreasNumbered = 0, portalSidesNumbered = 0, reachableAreasOutsideRange = 0;
    std::size_t unlistedPortalSidesNumbered = 0;   // indexed in the cluster, not added to its portal list
    std::size_t cluster0ReachableFlagsCleared = 0;
};

struct Aas140EngineFile {
    std::string profile;                         // "WolfAas140V1"
    std::uint32_t fileCrc = 0;
    std::vector<Aas140Plane> planes;
    std::vector<Aas140Vertex> vertices;          // file vertices, then reconstructed ones
    std::vector<Aas140Edge> edges;               // file edges, then reconstructed ones
    std::vector<std::int32_t> edgeIndex;         // floor loops re-signed, then wall loops
    std::vector<Aas140EngineFace> faces;
    std::vector<std::int32_t> faceIndex;
    std::vector<Aas140EngineArea> areas;
    std::vector<Aas140EngineReach> reach;        // grouped by source area, file order
    std::vector<Aas140Node> nodes;
    std::vector<Aas140EnginePortal> portals;
    std::vector<std::int32_t> portalIndex;
    std::vector<Aas140EngineCluster> clusters;
    std::vector<Aas140LinkClass> linkClass;      // one per Aas140File::reachabilities entry
    Aas140ConversionStats stats;
};

// Explicit CRC policy: the profile admits only the measured retail field
// value 1 and records the caller's map CRC separately; it never compares them.
const std::uint32_t AAS140_RETAIL_CRC_FIELD = 1;
Aas140EngineFile ConvertAas140ToEngine(const Aas140File &file);

} // namespace wolf2009
#endif

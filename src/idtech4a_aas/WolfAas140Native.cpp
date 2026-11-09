// SPDX-License-Identifier: GPL-3.0-or-later
// Native adapter for the profile WolfAas140V1 (docs/AAS.md). The portable
// reader validates and converts; this file only copies the converted model
// into the GPL idAASFileLocal and keeps the Wolf side tables.
#include "AasFile140.h"
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "tools/compilers/aas/AASFile_local.h"
#include "WolfAas140Native.h"

static idCVar wolf2009_aasProfile("wolf2009_aasProfile", "", CVAR_SYSTEM,
    "Explicit Wolf2009 AAS conversion profile for reserved wolf2009/<label>/PROC/<map>.aas28 requests "
    "(WolfAas140V1); empty refuses them");

namespace {
const char kProfile[] = "WolfAas140V1";
struct Record {
    std::string name;
    unsigned mapCrc = 0;
    std::shared_ptr<const wolf2009::Aas140File> raw;
    wolf2009::Aas140ConversionStats stats;
    unsigned linkedReach = 0;
};
std::mutex registryMutex;
std::map<const idAASFile *, Record> registry;
unsigned loads = 0, frees = 0, refusals = 0;

bool LowerEquals(const std::string &a, const char *b) {
    if (a.size() != std::strlen(b)) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != b[i]) return false;
    }
    return true;
}
// wolf2009/<label>/PROC/<map>.aas28 -> wolf2009/<label>/AASS/<map>
std::string ResourcePath(const std::string &request) {
    const std::size_t labelEnd = request.find('/', 9), typeEnd = labelEnd == std::string::npos ? labelEnd : request.find('/', labelEnd + 1);
    if (!LowerEquals(request.substr(0, 9), "wolf2009/") || labelEnd == std::string::npos || labelEnd == 9 ||
        typeEnd == std::string::npos || !LowerEquals(request.substr(labelEnd + 1, typeEnd - labelEnd - 1), "proc") ||
        request.size() <= typeEnd + 1 + 6)
        throw std::runtime_error("request is not wolf2009/<label>/PROC/<map>.aas28");
    for (std::size_t i = 9; i < labelEnd; ++i) {
        const char c = request[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            throw std::runtime_error("mount label is not [a-z0-9_]");
    }
    const std::string map = request.substr(typeEnd + 1, request.size() - typeEnd - 1 - 6);
    return request.substr(0, labelEnd) + "/AASS/" + map;
}
} // namespace

// Friend of idAASFileLocal (patches/idtech4a-wolf2009-aas140.patch).
class Wolf2009Aas140Builder {
  public:
    static unsigned Fill(idAASFileLocal &file, const char *name, const wolf2009::Aas140File &raw,
                         const wolf2009::Aas140EngineFile &m) {
        file.Clear();
        file.name = name;
        file.crc = m.fileCrc;   // the file's own field (explicit CRC policy)
        file.planeList.Resize(int(m.planes.size()));
        for (std::size_t i = 0; i < m.planes.size(); ++i)
            file.planeList.Append(idPlane(m.planes[i].a, m.planes[i].b, m.planes[i].c, m.planes[i].d));
        file.vertices.SetNum(int(m.vertices.size()));
        for (std::size_t i = 0; i < m.vertices.size(); ++i) file.vertices[i].Set(m.vertices[i].x, m.vertices[i].y, m.vertices[i].z);
        file.edges.SetNum(int(m.edges.size()));
        for (std::size_t i = 0; i < m.edges.size(); ++i) {
            file.edges[i].vertexNum[0] = m.edges[i].v[0]; file.edges[i].vertexNum[1] = m.edges[i].v[1];
        }
        CopyIndex(file.edgeIndex, m.edgeIndex);
        CopyIndex(file.faceIndex, m.faceIndex);
        CopyIndex(file.portalIndex, m.portalIndex);
        file.faces.SetNum(int(m.faces.size()));
        for (std::size_t i = 0; i < m.faces.size(); ++i) {
            aasFace_t &d = file.faces[i]; const wolf2009::Aas140EngineFace &s = m.faces[i];
            d.planeNum = s.planeNum; d.flags = s.flags; d.numEdges = s.numEdges; d.firstEdge = s.firstEdge;
            d.areas[0] = s.areas[0]; d.areas[1] = s.areas[1];
        }
        file.areas.SetNum(int(m.areas.size()));
        for (std::size_t i = 0; i < m.areas.size(); ++i) {
            aasArea_t &d = file.areas[i]; const wolf2009::Aas140EngineArea &s = m.areas[i];
            d.numFaces = s.numFaces; d.firstFace = s.firstFace;
            d.bounds = idBounds(idVec3(s.boundsMin[0], s.boundsMin[1], s.boundsMin[2]), idVec3(s.boundsMax[0], s.boundsMax[1], s.boundsMax[2]));
            d.center.Set(s.center[0], s.center[1], s.center[2]);
            d.flags = s.flags; d.contents = s.contents; d.cluster = s.cluster; d.clusterAreaNum = s.clusterAreaNum;
            d.travelFlags = s.travelFlags; d.reach = NULL; d.rev_reach = NULL;
        }
        file.nodes.SetNum(int(m.nodes.size()));
        for (std::size_t i = 0; i < m.nodes.size(); ++i) {
            aasNode_t &d = file.nodes[i];
            d.planeNum = m.nodes[i].planeNum; d.children[0] = m.nodes[i].children[0]; d.children[1] = m.nodes[i].children[1];
        }
        file.portals.SetNum(int(m.portals.size()));
        for (std::size_t i = 0; i < m.portals.size(); ++i) {
            aasPortal_t &d = file.portals[i]; const wolf2009::Aas140EnginePortal &s = m.portals[i];
            d.areaNum = s.areaNum; d.clusters[0] = s.clusters[0]; d.clusters[1] = s.clusters[1];
            d.clusterAreaNum[0] = s.clusterAreaNum[0]; d.clusterAreaNum[1] = s.clusterAreaNum[1];
            d.maxAreaTravelTime = 0;   // computed by idAASLocal::CalculateAreaTravelTimes
        }
        file.clusters.SetNum(int(m.clusters.size()));
        for (std::size_t i = 0; i < m.clusters.size(); ++i) {
            aasCluster_t &d = file.clusters[i]; const wolf2009::Aas140EngineCluster &s = m.clusters[i];
            d.numAreas = s.numAreas; d.numReachableAreas = s.numReachableAreas; d.numPortals = s.numPortals; d.firstPortal = s.firstPortal;
        }
        FillSettings(file.settings, raw.settings);
        // Per-area forward lists in file order, then Doom 3's reverse linking.
        unsigned linked = 0;
        for (std::size_t a = 0; a < m.areas.size(); ++a) {
            idReachability *tail = NULL;
            for (int k = 0; k < m.areas[a].numReach; ++k) {
                const wolf2009::Aas140EngineReach &s = m.reach[m.areas[a].firstReach + k];
                idReachability *r = new idReachability;
                r->travelType = s.travelType; r->toAreaNum = s.toAreaNum; r->fromAreaNum = s.fromAreaNum;
                r->start.Set(s.start[0], s.start[1], s.start[2]); r->end.Set(s.end[0], s.end[1], s.end[2]);
                r->edgeNum = s.edgeNum; r->travelTime = s.travelTime; r->number = 0; r->disableCount = 0;
                r->next = NULL; r->rev_next = NULL; r->areaTravelTimes = NULL;
                if (tail) tail->next = r; else file.areas[int(a)].reach = r;
                tail = r; ++linked;
            }
        }
        file.LinkReversedReachability();
        return linked;
    }
  private:
    static void CopyIndex(idList<aasIndex_t> &d, const std::vector<std::int32_t> &s) {
        d.SetNum(int(s.size()));
        for (std::size_t i = 0; i < s.size(); ++i) d[int(i)] = s[i];
    }
    static void FillSettings(idAASSettings &d, const wolf2009::Aas140Settings &s) {
        d.numBoundingBoxes = 1;
        d.boundingBoxes[0] = idBounds(idVec3(s.bboxMin[0], s.bboxMin[1], s.bboxMin[2]), idVec3(s.bboxMax[0], s.bboxMax[1], s.bboxMax[2]));
        d.usePatches = s.usePatches; d.writeBrushMap = s.writeBrushMap; d.playerFlood = s.playerFlood; d.noOptimize = false;
        d.allowSwimReachabilities = s.allowSwimReachabilities; d.allowFlyReachabilities = s.allowFlyReachabilities;
        d.fileExtension = s.fileExtension.c_str();
        d.gravity.Set(s.gravity[0], s.gravity[1], s.gravity[2]);
        d.gravityDir = d.gravity; d.gravityValue = d.gravityDir.Normalize(); d.invGravityDir = -d.gravityDir;
        d.maxStepHeight = s.maxStepHeight; d.maxBarrierHeight = s.maxBarrierHeight; d.maxWaterJumpHeight = s.maxWaterJumpHeight;
        d.maxFallHeight = s.maxFallHeight; d.minFloorCos = s.minFloorCos;
        d.tt_barrierJump = s.tt_barrierJump; d.tt_waterJump = s.tt_waterJump; d.tt_startWalkOffLedge = s.tt_startWalkOffLedge;
        // tt_startCrouching has no 1.40 key; the Doom 3 default stays (it is
        // only read by the AAS compiler). Wolf's stand/crouch/prone/gap keys
        // remain in the raw settings side table.
    }
};

bool Wolf2009_IsAas140Request(const char *fileName) {
    if (!fileName) return false;
    const std::string name(fileName);
    return name.size() > 15 && LowerEquals(name.substr(0, 9), "wolf2009/") && LowerEquals(name.substr(name.size() - 6), ".aas28");
}

idAASFile *Wolf2009_LoadAas140(const char *fileName, unsigned int mapFileCRC) {
    const std::string name(fileName ? fileName : "");
    void *buffer = NULL;
    try {
        const std::string profile = wolf2009_aasProfile.GetString();
        if (profile.empty()) throw std::runtime_error("no AAS profile selected (wolf2009_aasProfile is empty)");
        if (profile != kProfile) throw std::runtime_error("unknown AAS profile '" + profile + "'");
        const std::string resource = ResourcePath(name);
        const int length = fileSystem->ReadFile(resource.c_str(), &buffer, NULL);
        if (length <= 0 || !buffer) throw std::runtime_error("typed resource " + resource + " is unavailable");
        std::shared_ptr<wolf2009::Aas140File> raw(new wolf2009::Aas140File(
            wolf2009::ParseAas140(static_cast<const unsigned char *>(buffer), std::size_t(length))));
        fileSystem->FreeFile(buffer); buffer = NULL;
        const wolf2009::Aas140EngineFile model = wolf2009::ConvertAas140ToEngine(*raw);
        idAASFileLocal *file = new idAASFileLocal();
        Record record;
        record.name = name; record.mapCrc = mapFileCRC; record.raw = raw; record.stats = model.stats;
        record.linkedReach = Wolf2009Aas140Builder::Fill(*file, name.c_str(), *raw, model);
        unsigned live;
        {
            std::lock_guard<std::mutex> lock(registryMutex);
            registry[file] = record; ++loads; live = unsigned(registry.size());
        }
        common->Printf("WOLF2009_AAS140_LOAD name=%s profile=%s resource=%s file_crc=%u map_crc=%u areas=%d reach=%u linked=%u "
                       "cross_cluster=%u dropped_area0=%u dropped_portal_border=%u unbounded_reachable=%u wall_windings=%u live=%u loads=%u\n",
                       name.c_str(), kProfile, resource.c_str(), unsigned(raw->crc), mapFileCRC, file->GetNumAreas(),
                       unsigned(raw->reachabilities.size()), record.linkedReach, unsigned(model.stats.linkedCrossCluster),
                       unsigned(model.stats.droppedArea0), unsigned(model.stats.droppedPortalBorder),
                       unsigned(model.stats.areasUnboundedReachable), unsigned(model.stats.wallWindings), live, loads);
        return file;
    } catch (const std::exception &error) {
        if (buffer) fileSystem->FreeFile(buffer);
        { std::lock_guard<std::mutex> lock(registryMutex); ++refusals; }
        common->Warning("WOLF2009_AAS140_REFUSED name=%s reason=%s", name.c_str(), error.what());
        return NULL;
    }
}

void Wolf2009_ReleaseAas140(idAASFile *file) {
    std::string name;
    unsigned live;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        std::map<const idAASFile *, Record>::iterator found = registry.find(file);
        if (found == registry.end()) return;
        name = found->second.name;
        registry.erase(found); ++frees; live = unsigned(registry.size());
    }
    common->Printf("WOLF2009_AAS140_RELEASE name=%s live=%u loads=%u frees=%u\n", name.c_str(), live, loads, frees);
}

bool Wolf2009_Aas140Lookup(const char *name, const idAASFile *&file, Wolf2009Aas140Info &info) {
    std::lock_guard<std::mutex> lock(registryMutex);
    for (std::map<const idAASFile *, Record>::const_iterator i = registry.begin(); i != registry.end(); ++i) {
        if (!name || idStr::Icmp(i->second.name.c_str(), name)) continue;
        const Record &r = i->second;
        file = i->first;
        info.name = r.name; info.profile = kProfile; info.fileCrc = r.raw->crc; info.mapCrc = r.mapCrc;
        info.areas = unsigned(r.raw->areas.size()); info.reachabilities = unsigned(r.raw->reachabilities.size());
        info.linked = r.linkedReach; info.linkedCrossCluster = unsigned(r.stats.linkedCrossCluster);
        info.droppedArea0 = unsigned(r.stats.droppedArea0); info.droppedPortalBorder = unsigned(r.stats.droppedPortalBorder);
        info.unboundedReachable = unsigned(r.stats.areasUnboundedReachable); info.wallWindings = unsigned(r.stats.wallWindings);
        return true;
    }
    return false;
}

bool Wolf2009_Aas140AreaLocations(const idAASFile *file, int area, std::vector<std::pair<int, std::string> > &out) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::map<const idAASFile *, Record>::const_iterator found = registry.find(file);
    if (found == registry.end()) return false;
    const wolf2009::Aas140File &raw = *found->second.raw;
    if (area <= 0 || std::size_t(area) >= raw.areas.size()) return false;
    out.clear();
    const wolf2009::Aas140Area &a = raw.areas[area];
    for (int k = 0; k < a.numLocations; ++k) {
        const int location = raw.locationIndex[a.firstLocation + k];
        out.push_back(std::make_pair(location, raw.locations[location]));
    }
    return true;
}

void Wolf2009_Aas140Counters(unsigned &live, unsigned &loadCount, unsigned &freeCount, unsigned &refusalCount) {
    std::lock_guard<std::mutex> lock(registryMutex);
    live = unsigned(registry.size()); loadCount = loads; freeCount = frees; refusalCount = refusals;
}

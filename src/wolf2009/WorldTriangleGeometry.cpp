// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldTriangleGeometry.h"
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool Finite(CollisionVec3 p, double bound) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= bound &&
           std::abs(p.y) <= bound && std::abs(p.z) <= bound;
}
double Dot(CollisionVec3 a, CollisionVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CollisionVec3 Rotate(const CollisionRigidTransform &t, CollisionVec3 p) {
    return {t.axis[0].x * p.x + t.axis[1].x * p.y + t.axis[2].x * p.z,
            t.axis[0].y * p.x + t.axis[1].y * p.y + t.axis[2].y * p.z,
            t.axis[0].z * p.x + t.axis[1].z * p.y + t.axis[2].z * p.z};
}
void Rigid(const CollisionRigidTransform &t, double bound) {
    Need(Finite(t.translation, bound), "Triangle placement translation exceeds budget");
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            Need(Finite(t.axis[i], 2) &&
                     std::abs(Dot(t.axis[i], t.axis[j]) - (i == j ? 1 : 0)) <= .00002,
                 "Triangle placement is not rigid");
    const auto &a = t.axis[0], &b = t.axis[1], &c = t.axis[2];
    const double det =
        a.x * (b.y * c.z - b.z * c.y) - b.x * (a.y * c.z - a.z * c.y) + c.x * (a.y * b.z - a.z * b.y);
    Need(std::abs(det - 1) <= .00002, "Triangle placement is reflected");
}
void Add(std::size_t &total, std::size_t count, std::size_t maximum, const char *message) {
    Need(total <= maximum && count <= maximum - total, message);
    total += count;
}
std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) {
    for (int k = 0; k < 8; ++k) {
        hash ^= static_cast<unsigned char>(value >> (8 * k));
        hash *= 1099511628211ull;
    }
    return hash;
}
void Limits(const WorldTriangleGeometryLimits &l) {
    Need(l.occurrences && l.worldVertices && l.worldTriangles && l.coordinateGameUnits > 0 &&
             std::isfinite(l.coordinateGameUnits),
         "Invalid world triangle limits");
}
} // namespace
WorldTriangleGeometry PlaceWorldTriangleGeometry(const WorldCollisionGeometry &world,
                                                 HavokTriangleMeshDocument buffers,
                                                 const WorldTriangleGeometryLimits &limits) {
    Limits(limits);
    Need(world.identityBodyPoseVerified, "World triangle placement needs a verified body pose");
    Need(world.sourceBytes && world.sourceBytes == buffers.sourceBytes &&
             world.sourceFingerprint == buffers.sourceFingerprint,
         "World triangle source provenance mismatch");
    WorldTriangleGeometry result;
    result.sourceBodyName = world.selectedBodyName;
    result.sourceBodyOffset = world.sourceBodyOffset;
    result.buffers = std::move(buffers);
    std::map<std::uint32_t, std::size_t> storageIndices;
    for (std::size_t i = 0; i < result.buffers.storages.size(); ++i)
        Need(storageIndices.emplace(result.buffers.storages[i].sourceStorageOffset, i).second,
             "Duplicate triangle storage in world join");
    bool complete = true;
    for (const auto &occurrence : world.omissions) {
        if (occurrence.type != "storage-triangle-subpart")
            continue;
        Need(result.parts.size() < limits.occurrences, "World triangle occurrence budget exceeded");
        const auto found = storageIndices.find(occurrence.sourceShapeOffset);
        Need(found != storageIndices.end(), "World triangle storage buffer is missing");
        const auto &storage = result.buffers.storages[found->second];
        Need(occurrence.sourceSubpartIndex < storage.subparts.size(),
             "World triangle subpart is missing");
        const auto &source = storage.subparts[occurrence.sourceSubpartIndex];
        Need(source.sourceStorageOffset == occurrence.sourceShapeOffset &&
                 source.subpartIndex == occurrence.sourceSubpartIndex &&
                 source.declaredTriangles == occurrence.triangles,
             "World triangle graph/buffer records disagree");
        Add(result.reportedTriangles, occurrence.triangles, limits.worldTriangles,
            "World triangle count budget exceeded");
        Rigid(occurrence.placement, limits.coordinateGameUnits);
        WorldTrianglePart part;
        part.storageIndex = found->second;
        part.subpartIndex = occurrence.sourceSubpartIndex;
        part.occurrence = occurrence.occurrence;
        part.placement = occurrence.placement;
        part.sourceRegion = Mix(Mix(source.sourceRegion, world.sourceBodyOffset), occurrence.occurrence);
        if (!part.sourceRegion)
            part.sourceRegion = 1;
        if (storage.identityScale && source.buffersDecoded && source.identityScaleBufferProfile) {
            Need(source.triangles.size() == source.declaredTriangles,
                 "Incomplete triangle buffer profile");
            Add(result.placedVertices, source.vertices.size(), limits.worldVertices,
                "World triangle vertex budget exceeded");
            Add(result.placedTriangles, source.triangles.size(), limits.worldTriangles,
                "World placed triangle budget exceeded");
            part.worldVertices.reserve(source.vertices.size());
            for (const auto &v : source.vertices) {
                const auto placed = TransformCollisionPoint(part.placement, v.unscaledGamePosition);
                Need(Finite(placed, limits.coordinateGameUnits), "World triangle vertex exceeds budget");
                part.worldVertices.push_back(placed);
            }
            part.positionsPlaced = true;
            if (source.finiteExtrusion) {
                part.worldExtrusion = Rotate(part.placement, source.sourceExtrusion);
                Need(Finite(part.worldExtrusion, limits.coordinateGameUnits),
                     "World triangle extrusion exceeds budget");
                part.extrusionPlaced = true;
            }
        }
        complete = complete && part.positionsPlaced && part.extrusionPlaced;
        result.parts.push_back(std::move(part));
    }
    result.allReportedOccurrencesPlaced = complete;
    return result;
}
WorldTriangleGeometry DecodeWorldTriangleGeometry(const std::vector<unsigned char> &hkxs,
                                                  const WorldCollisionGeometry &world,
                                                  const WorldTriangleGeometryLimits &limits) {
    Limits(limits);
    std::uint64_t fingerprint = 14695981039346656037ull;
    Need(hkxs.size() <= limits.buffers.sourceBytes, "World triangle source exceeds budget");
    for (const auto c : hkxs) {
        fingerprint ^= c;
        fingerprint *= 1099511628211ull;
    }
    Need(world.sourceBytes == hkxs.size() && world.sourceFingerprint == fingerprint,
         "World triangle bytes differ from the selected-body source");
    std::set<std::uint32_t> selected;
    for (const auto &o : world.omissions)
        if (o.type == "storage-triangle-subpart")
            selected.insert(o.sourceShapeOffset);
    HavokTriangleMeshDocument buffers;
    buffers.sourceBytes = hkxs.size();
    buffers.sourceFingerprint = fingerprint;
    // An empty selection means "enumerate all" to the low-level reader;
    // selected-body scope must instead retain an empty document here.
    if (!selected.empty())
        buffers = DecodeHavokTriangleMeshes(
            hkxs, std::vector<std::uint32_t>(selected.begin(), selected.end()), limits.buffers);
    return PlaceWorldTriangleGeometry(world, std::move(buffers), limits);
}
} // namespace wolf2009

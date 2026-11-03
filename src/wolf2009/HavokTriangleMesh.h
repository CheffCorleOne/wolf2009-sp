// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_HAVOK_TRIANGLE_MESH_H
#define WOLF2009_HAVOK_TRIANGLE_MESH_H
#include "StaticCollision.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
struct HavokTriangleMeshLimits {
    std::size_t sourceBytes = 64u << 20, objects = 100000, fixups = 1000000;
    std::size_t storageShapes = 4096, subparts = 100000;
    std::size_t verticesPerSubpart = 1000000, trianglesPerSubpart = 1000000;
    std::size_t totalVertices = 2000000, totalTriangles = 2000000;
    std::size_t arrayElements = 8000000, decodedBytes = 128u << 20;
    std::size_t maximumIndexStride = 256;
    double coordinateGameUnits = 10000000;
};
struct HavokTriangleFixup {
    std::uint32_t fieldOffset = 0, targetDataOffset = 0;
};
struct HavokTriangleRawArray {
    // The field offset is relative to the owned MeshSubpartStorage object.
    // Descriptor words and payload are preserved without material semantics.
    std::uint32_t fieldOffset = 0, rawPointerWord = 0, count = 0, rawCapacityFlags = 0;
    std::uint32_t targetDataOffset = 0, elementBytes = 0;
    bool hasPointer = false;
    std::vector<unsigned char> bytes;
};
struct HavokTriangleVertex {
    // Unscaled source xyz converted from metres into GAME UNITS. Storage
    // scaling is retained separately and is never applied or normalized here.
    CollisionVec3 unscaledGamePosition;
    std::uint32_t rawWBits = 0;
};
struct HavokTriangle {
    std::uint32_t indices[3] = {0,0,0}; // Stored order, no winding inference.
    std::vector<unsigned char> strideTail; // Exact bytes after the three indices.
};
struct HavokTriangleSubpart {
    std::uint32_t sourceStorageOffset = 0, sourceRecordOffset = 0, sourceOwnedStorageOffset = 0;
    std::size_t subpartIndex = 0;
    std::uint64_t sourceRegion = 0;
    std::array<unsigned char,64> rawRecord{};
    std::array<unsigned char,80> rawOwnedStorageHeader{};
    std::vector<HavokTriangleFixup> recordFixups, ownedStorageFixups;
    std::uint32_t declaredTriangles = 0, declaredVertices = 0;
    std::uint32_t vertexStride = 0, indexStride = 0, rawIndexType = 0;
    std::uint32_t indexBytes = 0;
    // Retail class reflection separates this byte from its adjacent signed
    // flip flag. Keep the original four-byte word above for provenance.
    std::uint8_t indexType = 0, rawFlipAlternateTriangles = 0;
    std::uint32_t rawTriangleOffset = 0;
    std::array<std::uint32_t,4> rawExtrusionBits{};
    CollisionVec3 sourceExtrusion; // xyz converted from metres to game units
    bool finiteExtrusion = false;
    // Layout observed in the 5.6/32-bit profile, not Havok SDK types:
    // 0: field8/16B vertices; 1: field20/u16 indices; 2: field32/u32 indices;
    // 3: field44/raw bytes; 4: field56/raw u32 words; 5: field68/raw u16 words.
    // Fields44/56/68 are retained as raw material metadata. No value is
    // interpreted as contents, material index, runtime mask or activation.
    std::array<HavokTriangleRawArray,6> arrays;
    std::vector<HavokTriangleVertex> vertices;
    std::vector<HavokTriangle> triangles;
    bool buffersDecoded = false, identityScaleBufferProfile = false;
    std::vector<std::string> unsupportedProfiles;
};
struct HavokTriangleStorage {
    std::uint32_t sourceStorageOffset = 0;
    std::array<unsigned char,216> rawHeader{};
    std::array<std::uint32_t,4> rawScaleBits{};
    CollisionVec3 sourceScale;
    bool finiteScale = false, identityScale = false;
    std::vector<HavokTriangleSubpart> subparts;
};
struct HavokTriangleMeshDocument {
    std::uint64_t sourceFingerprint = 0; // noncryptographic provenance, not a trust boundary
    std::size_t sourceBytes = 0;
    std::vector<HavokTriangleStorage> storages;
    std::size_t declaredSubparts = 0, decodedSubparts = 0, qualifiedIdentitySubparts = 0;
    std::size_t declaredTriangles = 0, decodedTriangles = 0, decodedVertices = 0;
    std::size_t decodedBytes = 0;
    // True only for the measured buffer profiles and exact(1,1,1,1) storage
    // scale. Only selected typed storage objects, never world/physics coverage.
    bool completeBufferCoverage = true;
};

// Independent bounded reader for the already measured HKXS Havok5.6/32-bit LE
// triangle-subpart and owned-buffer profile. Uses HavokPack's checked fixups.
// Empty selection enumerates all typed hkpStorageExtendedMeshShape objects;
// explicit offsets must identify unique objects of that exact type. No shape
// graph, rigid-body pose, instance placement, MOPP, winding, welding, contents,
// active physics or collision-controller semantics are reconstructed here.
// Recognized index types1/u16 and2/u32 preserve stride padding. Malformed
// bounds/counts/indices throw; unknown layouts retain raw buffers and explicit
// unsupportedProfiles. Nonidentity scale keeps geometry unqualified and raw.
HavokTriangleMeshDocument DecodeHavokTriangleMeshes(
    const std::vector<unsigned char> &hkxs,
    const std::vector<std::uint32_t> &selectedStorageOffsets = {},
    const HavokTriangleMeshLimits &limits = HavokTriangleMeshLimits());
} // namespace wolf2009
#endif

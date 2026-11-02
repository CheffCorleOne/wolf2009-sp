// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_STATIC_COLLISION_H
#define WOLF2009_STATIC_COLLISION_H
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace wolf2009 {
struct CollisionVec3 {
    double x,y,z;
    CollisionVec3(double px=0,double py=0,double pz=0):x(px),y(py),z(pz){}
};
struct StaticCollisionBox {
    CollisionVec3 minimum,maximum;
    std::uint32_t sourceShapeOffset = 0;
    // Caller-authored boxes default to solid. Decoded boxes always receive an
    // explicit observed value/known flag; unknown values are never skipped.
    std::uint32_t contents = 1;
    bool contentsKnown = true;
};
// Verified in the installed scripts/lib/doom_defs.script. These are contents
// bits, not a reconstruction of Havok group-layer filtering or entity state.
enum StaticCollisionContents {
    CollisionSolid = 1, CollisionPlayerClip = 8, CollisionMonsterClip = 16,
    CollisionBody = 256, CollisionPlayerMask = 1|8|256
};
struct StaticCollisionLimits {
    std::size_t sourceBytes = 64u*1024u*1024u;
    std::size_t objects = 100000;
    std::size_t fixups = 1000000;
    std::size_t shapeVisits = 100000;
    std::size_t transforms = 20000;
    std::size_t boxes = 20000;
    std::size_t nesting = 64;
    std::size_t compoundEntries = 100000;
    double coordinateGameUnits = 10000000;
};
struct StaticCollisionScene {
    std::vector<StaticCollisionBox> boxes;
    std::string selectedBodyName;
    // Complete only over the selected body's supported geometric shape graph.
    // This is NOT proof of original character masks, dynamics or gameplay.
    bool coverageComplete = false;
    bool identityBodyPoseVerified = false;
    bool contentsComplete = true;
    // Decoder preserves flags; queries apply a mask. This legacy field remains
    // false rather than claiming that loading performed runtime filtering.
    bool physicsFiltersApplied = false;
    std::size_t otherRigidBodies = 0,visitedShapes = 0,omittedTriangles = 0,omittedTransformedSubparts = 0;
    std::map<std::string,std::size_t> unsupported;
    std::vector<std::string> diagnostics;
};
// Independent read-only decoder for the measured Havok-5.6.0-r1 / pack-v5 /
// 32-bit LE profile. Caller must explicitly select the resource's main body by
// its exact virtual name. Its observed transform slots must be identity.
// Supported: MOPP child links (not bytecode), List, ConvexTranslate, axis boxes,
// and identity placed-shape subparts of StorageExtendedMesh. Unsupported shapes
// and transformed placements are counted and make coverageComplete false.
// Coordinates are returned in original game units (one unit = 0.0254 metres).
StaticCollisionScene DecodeStaticCollision(const std::vector<unsigned char>& hkxs,
                                          const std::string& expectedBodyName,
                                          const StaticCollisionLimits& limits = StaticCollisionLimits());
struct StaticCollisionHit {
    bool hit = false,startedInside = false,coverageComplete = false;
    bool contentsMaskApplied = false,hitContentsKnown = false;
    std::uint32_t contents = 0;
    double fraction = 1;
    CollisionVec3 normal;
    std::size_t boxIndex = static_cast<std::size_t>(-1);
};
// Segment queries: delta is displacement, fraction is in [0,1]. A miss on a
// partial scene is UNKNOWN, not a statement that passage is clear. Known
// contents are tested against contentsMask (player mask by default); unknown
// contents remain conservatively included and make query coverage incomplete.
// Use 0xffffffffu explicitly for geometric diagnostic queries. Entity state,
// group layers and other bodies remain outside this subset. Sweep is an AABB,
// NOT a capsule. Original runtime movement/physics is not reconstructed here.
StaticCollisionHit RaycastStatic(const StaticCollisionScene& scene,CollisionVec3 origin,CollisionVec3 delta,std::uint32_t contentsMask = CollisionPlayerMask);
StaticCollisionHit SweepStaticBox(const StaticCollisionScene& scene,CollisionVec3 center,CollisionVec3 halfExtents,CollisionVec3 delta,std::uint32_t contentsMask = CollisionPlayerMask);
int RunStaticCollisionTests();
}
#endif

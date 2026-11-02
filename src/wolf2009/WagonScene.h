// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_WAGON_SCENE_H
#define WOLF2009_WAGON_SCENE_H
#include "Archive.h"
#include "Geometry.h"
#include "LinearMover.h"
#include "MovingSupport.h"
#include "TrainArrival.h"
#include "SkinCatalog.h"
#include <memory>
namespace wolf2009 {
struct WagonSceneOptions {
    GeometryLimits geometry;
    StaticCollisionLimits collision;
    LinearMoverLimits brain;
    MovingSupportLimits support;
    // Standing view offset64 is observed in the original player definition.
    // AABB16/72 and gap tolerance2 remain explicit prototype contact choices.
    double eyeHeight = 64, halfWidth = 16, standingHeight = 72, maximumSupportGap = 2;
    // Opt-in .7 slice. Default preserves the published .6 ride-only behavior.
    bool enableArrival = false;
    const SkinCatalog *skins = nullptr; // borrowed only during Load
};
struct WagonSceneDescription {
    std::string mapResource, entityName, modelResource, brainResource, destinationName, spawnName;
    std::string authoredBind, authoredSkin;
    CollisionVec3 cartStart, cartDestination, playerStart, playerDestination;
    double speed = 0, eyeHeight = 64, playerYawDegrees = 0;
    std::size_t supportBoxIndex = static_cast<std::size_t>(-1);
    std::size_t unexecutedBrainNodes = 0, moveOutputRecords = 0;
    bool authoredWorldOriginCaseVerified = false, skinResolved = false;
    bool completeModelCollision = false, rawModelContentsComplete = false;
    std::map<std::string, std::size_t> omittedCollision;
    bool arrivalEnabled = false;
    std::uint32_t arrivalBoundaryNode = 0, arrivalBoundaryType = 0;
    std::string arrivalUnsupportedTarget, arrivalUnsupportedClass, arrivalUnsupportedUseKey;
};
struct WagonSceneState {
    CollisionVec3 cartOrigin, cartOffset, playerFeet, cameraOrigin;
    double elapsedSeconds = 0, travelled = 0;
    std::uint64_t sample = 0;
    std::uint64_t levelGeneration = 0;
    std::size_t visibleArrivalInstances = 0, arrivalChangeCount = 0;
    bool arrivalApplied = false;
    bool spawnDelivered = false, moving = false, arrived = false, supportContact = false;
    // This mode follows only the qualified initial wagon ride. It exposes no
    // walking input and never certifies the surrounding partial map as clear.
    bool rideOnly = true, worldWalkable = false, worldClearanceVerified = false;
};
struct WagonSceneStaticInstance {
    EntityPlacement authored;
    std::string entityName, modelResource, skin;
    GeometryScene geometry; // immutable authored world pose; upload once
    bool skinResolved = false;
    std::size_t skinMatchedSurfaces = 0, skinChangedSurfaces = 0;
};
class WagonScene {
  public:
    // Narrow compatibility gate: original trainyard resource, initial named
    // func_mover/box_car_a, authored first primary spawn and verified direct
    // OnSpawn->MoveToEnt BRAI profile. All coordinates/speed/target come from
    // resource data. Missing/ambiguous/unsupported cases throw; no substitute
    // scene is created. Input/archive are not retained after this operation.
    static std::unique_ptr<WagonScene> Load(std::istream &input, const Archive &archive,
                                            const std::string &mapResource,
                                            const WagonSceneOptions &options = WagonSceneOptions());
    ~WagonScene();
    WagonScene(WagonScene &&) noexcept;
    WagonScene &operator=(WagonScene &&) noexcept;
    WagonScene(const WagonScene &) = delete;
    WagonScene &operator=(const WagonScene &) = delete;
    // Already placed in the authored/base world pose, including original UVs
    // and material names. Upload once; render with State().cartOffset. The
    // object and vertex/index storage stay unchanged through Advance/Reset.
    const GeometryScene &PlacedGeometry() const;
    const WagonSceneDescription &Description() const;
    const WagonSceneState &State() const;
    const std::vector<WagonSceneStaticInstance> &ArrivalInstances() const;
    bool IsArrivalInstanceVisible(std::size_t index) const;
    const LevelEntityRegistry *EntityRegistry() const;
    // Last one-shot committed batch for this generation; stable until Reset.
    const std::vector<TrainArrivalChange> &ArrivalChanges() const;
    // Delivers the level owner's OnSpawn after assembly. Reset repeats from
    // saved immutable startup/spawn input without reading or copying the mesh.
    void Reset();
    // Finite0..1seconds, with no lost-time catchup. Opt-in arrival executes only
    // its qualified Remove/Show prefix. No AI/door/general walking/unknown nodes.
    // Failure leaves runtime/visible state unchanged.
    void Advance(double seconds);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WagonScene(std::unique_ptr<Impl> impl);
};
int RunWagonSceneTests();
} // namespace wolf2009
#endif

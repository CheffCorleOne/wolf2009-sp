// SPDX-License-Identifier: GPL-3.0-or-later
#include "WagonScene.h"
#include "BrainStartup.h"
#include "MapPhysics.h"
#include "ModelCollision.h"
#include "ModelPlacement.h"
#include "PlayerStart.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
std::string Fold(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}
std::string Virtual(std::string s) {
    Need(!s.empty() && s.size() <= 4096, "Invalid wagon virtual resource name");
    for (char &c : s) {
        Need(static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) <= 126 && c != ':',
             "Unsafe wagon virtual resource byte");
        if (c == '\\')
            c = '/';
    }
    Need(s[0] != '/' && s[s.size() - 1] != '/', "Absolute/empty wagon virtual resource path");
    std::size_t p = 0;
    while (p < s.size()) {
        const std::size_t e = s.find('/', p);
        const std::string part = s.substr(p, e == std::string::npos ? s.size() - p : e - p);
        Need(!part.empty() && part != "." && part != "..", "Unsafe wagon virtual resource component");
        if (e == std::string::npos)
            break;
        p = e + 1;
    }
    return Fold(s);
}
std::string Key(std::string s, const char *suffix) {
    s = Virtual(s);
    const std::string tail = suffix;
    if (s.size() > tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0)
        s.resize(s.size() - tail.size());
    return s;
}
const EntityProperty *Unique(const EntityRecord &e, const char *key) {
    const EntityProperty *found = NULL;
    for (const auto &p : e.properties)
        if (Fold(p.key) == key) {
            Need(!found, "Duplicate critical wagon entity property");
            found = &p;
        }
    return found;
}
std::string Text(const EntityRecord &e, const char *key) {
    const auto *p = Unique(e, key);
    if (!p)
        return {};
    Need(p->kind == EntityProperty::Text, "Wagon entity property must be text");
    return p->text;
}
const EntityRecord &Named(const EntityDocument &doc, const std::string &name) {
    const EntityRecord *found = NULL;
    for (const auto &e : doc.entities)
        if (Fold(Text(e, "name")) == Fold(name)) {
            Need(!found, "Ambiguous selected wagon entity");
            found = &e;
        }
    Need(found, "Selected wagon entity missing");
    return *found;
}
std::vector<unsigned char> ReadUnique(std::istream &input, const Archive &archive, const std::string &type,
                                      const std::string &key, std::size_t budget) {
    std::size_t matches = 0, c = 0, e = 0;
    for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
        if (archive.Chunks()[i].type == type)
            for (std::size_t j = 0; j < archive.Chunks()[i].entries.size(); ++j)
                if (Virtual(archive.Chunks()[i].entries[j].name) == key) {
                    Need(archive.Chunks()[i].entries[j].size <= budget,
                         "Wagon resource byte budget exceeded");
                    ++matches;
                    c = i;
                    e = j;
                }
    Need(matches == 1, "Wagon resource missing or ambiguous in supplied archive");
    return archive.ReadEntry(input, c, e);
}
CollisionVec3 V(const float *p) {
    return {p[0], p[1], p[2]};
}
CollisionVec3 V(LinearMoverVec3 p) {
    return {p.x, p.y, p.z};
}
LinearMoverVec3 L(CollisionVec3 p) {
    return {p.x, p.y, p.z};
}
CollisionVec3 Add(CollisionVec3 a, CollisionVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
CollisionVec3 Sub(CollisionVec3 a, CollisionVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double Length(CollisionVec3 p) {
    return std::hypot(std::hypot(p.x, p.y), p.z);
}
bool Finite(CollisionVec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 10000000 &&
           std::abs(p.y) <= 10000000 && std::abs(p.z) <= 10000000;
}
double Yaw(const EntityPlacement &p) {
    Need(p.hasOrigin && Finite(V(p.origin)), "Authored wagon player start requires finite origin");
    double yaw = 0;
    if (p.hasRotation) {
        const float *r = p.rotation;
        for (int i = 0; i < 9; ++i)
            Need(std::isfinite(r[i]), "Nonfinite wagon player rotation");
        Need(std::abs(r[2]) + std::abs(r[5]) + std::abs(r[6]) + std::abs(r[7]) + std::abs(r[8] - 1) < 1e-5,
             "Only upright wagon player starts supported");
        Need(std::abs(double(r[0]) * r[0] + double(r[1]) * r[1] - 1) < 1e-5 &&
                 std::abs(double(r[3]) * r[3] + double(r[4]) * r[4] - 1) < 1e-5 &&
                 std::abs(double(r[0]) * r[3] + double(r[1]) * r[4]) < 1e-5 &&
                 std::abs(double(r[0]) * r[4] - double(r[1]) * r[3] - 1) < 1e-5,
             "Invalid wagon player rigid rotation");
        yaw = std::atan2(double(r[1]), double(r[0])) * 180 / 3.14159265358979323846;
    }
    if (p.hasAngle) {
        Need(std::isfinite(p.angle), "Nonfinite wagon player angle");
        if (p.hasRotation)
            Need(std::abs(std::remainder(yaw - p.angle, 360.0)) < 1e-3,
                 "Conflicting wagon player angle/matrix");
        else
            yaw = p.angle;
    }
    Need(p.hasRotation || p.hasAngle, "Wagon player start has no authored heading");
    return yaw;
}
std::size_t Support(const MovingSupportWorld &world, CollisionVec3 feet, const WagonSceneOptions &options) {
    std::size_t chosen = static_cast<std::size_t>(-1), ties = 0;
    double best = options.maximumSupportGap + 1;
    for (std::size_t i = 0; i < world.boxes.size(); ++i) {
        const auto &b = world.boxes[i].box;
        if (b.contentsKnown && !(b.contents & CollisionPlayerMask))
            continue;
        const double gap = feet.z - b.maximum.z;
        if (gap < -1e-7 || gap > options.maximumSupportGap)
            continue;
        // This startup slice requires the complete proxy footprint on one
        // horizontal top face; no ledge snap or speculative nearest surface.
        if (feet.x - options.halfWidth < b.minimum.x - 1e-7 ||
            feet.x + options.halfWidth > b.maximum.x + 1e-7 ||
            feet.y - options.halfWidth < b.minimum.y - 1e-7 ||
            feet.y + options.halfWidth > b.maximum.y + 1e-7)
            continue;
        if (gap < best - 1e-7) {
            chosen = i;
            best = gap;
            ties = 1;
        } else if (std::abs(gap - best) <= 1e-7)
            ++ties;
    }
    Need(chosen != static_cast<std::size_t>(-1) && ties == 1,
         "No unique supported wagon top face under authored player start");
    return chosen;
}
void CheckSpawnBox(const MovingSupportWorld &world, CollisionVec3 feet, const WagonSceneOptions &options) {
    StaticCollisionScene subset;
    subset.coverageComplete = false;
    subset.contentsComplete = false;
    for (const auto &box : world.boxes)
        subset.boxes.push_back(box.box);
    const auto overlap =
        SweepStaticBox(subset, Add(feet, {0, 0, options.standingHeight * .5}),
                       {options.halfWidth, options.halfWidth, options.standingHeight * .5}, {});
    Need(!overlap.startedInside, "Authored wagon player proxy overlaps supported collision boxes");
}
} // namespace
struct WagonScene::Impl {
    struct Runtime {
        explicit Runtime(const BrainStartupRuntime &original) : brain(original) {}
        BrainStartupRuntime brain;
        MovingSupportWorld world;
        MovingSupportContact contact;
        WagonSceneState state;
    };
    explicit Impl(const BrainStartupRuntime &original) : pristine(original) {}
    const BrainStartupRuntime pristine;
    std::unique_ptr<Runtime> live;
    GeometryScene geometry;
    WagonSceneDescription description;
    WagonSceneOptions options;
    MovingSupportGeometry support;
    std::vector<WagonSceneStaticInstance> instances;
    TrainArrivalPlan arrivalPlan;
    std::unique_ptr<LevelEntityRegistry> pristineRegistry, registry;
    std::unique_ptr<TrainArrivalRuntime> arrival;
    std::vector<TrainArrivalChange> changes;
    std::uint64_t generation = 0;
    // A stable scalar-only public view; committing it cannot allocate/throw.
    WagonSceneState state;
    TrainArrivalMoveStamp Stamp(const WagonSceneState &value) const {
        TrainArrivalMoveStamp stamp;
        stamp.brainResource = arrivalPlan.brainResource;
        stamp.ownerEntityName = description.entityName;
        stamp.moveNodeId = arrivalPlan.moveNodeId;
        stamp.levelGeneration = generation;
        stamp.spawnDelivered = value.spawnDelivered;
        stamp.active = value.moving;
        stamp.arrived = value.arrived;
        return stamp;
    }
    void Commit(std::unique_ptr<Runtime> &staged) {
        if (arrival && !live->state.arrived && staged->state.arrived) {
            auto result =
                arrival->ObserveCommittedMove(Stamp(live->state), Stamp(staged->state), *registry);
            Need(result.status == TrainArrivalStatus::Applied,
                 "Qualified arrival event/registry failed");
            // All allocating work completed before ObserveCommittedMove's own
            // boolean commit; everything after it is scalar/noexcept swapping.
            staged->state.arrivalApplied = true;
            staged->state.arrivalChangeCount = result.changes.size();
            for (const auto &instance : instances) {
                const auto *entity = registry->Find(instance.entityName);
                if (entity && entity->alive && !entity->hidden)
                    ++staged->state.visibleArrivalInstances;
            }
            changes.swap(result.changes);
        }
        live.swap(staged);
        state = live->state;
    }
};
WagonScene::WagonScene(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WagonScene::~WagonScene() {}
WagonScene::WagonScene(WagonScene &&) noexcept = default;
WagonScene &WagonScene::operator=(WagonScene &&) noexcept = default;
std::unique_ptr<WagonScene> WagonScene::Load(std::istream &input, const Archive &archive,
                                             const std::string &resource, const WagonSceneOptions &options) {
    const std::string map = Virtual(resource);
    Need(map == "maps/game/trainyard/trainyard",
         "WagonScene supports only the verified initial trainyard case");
    Need(std::isfinite(options.eyeHeight) && options.eyeHeight > 0 && std::isfinite(options.halfWidth) &&
             options.halfWidth > 0 && options.halfWidth <= 10000 && std::isfinite(options.standingHeight) &&
             options.standingHeight > 0 && options.standingHeight <= 10000 &&
             options.eyeHeight <= options.standingHeight && std::isfinite(options.maximumSupportGap) &&
             options.maximumSupportGap >= 0 && options.maximumSupportGap <= options.support.maximumContactGap,
         "Invalid wagon visual/contact options");
    const EntityDocument entities = ReadMapEntities(input, archive, map);
    const EntityRecord &actor = Named(entities, "starting_boxcar_model");
    if (options.enableArrival) {
        const std::set<std::string> known{"name",  "classname", "model", "origin", "rotation",
                                          "angle", "skin",      "bind",  "brain"};
        for (const auto &property : actor.properties)
            Need(known.count(Fold(property.key)) != 0,
                 "Unverified wagon actor property in arrival profile");
    }
    Need(Fold(Text(actor, "classname")) == "func_mover" &&
             Key(Text(actor, "model"), ".md5r") == "models/vehicles/box_car_a",
         "Unsupported initial wagon actor/model");
    Need(Text(actor, "bind") == "starting_boxcar", "Unverified wagon bind/world-origin compatibility case");
    for (const char *key : {"bindtojoint", "bindtobody", "bindorientated", "scriptobject", "script"})
        Need(Text(actor, key).empty(), "Additional wagon bind/script state unsupported");
    for (const char *key : {"origin", "rotation", "angle"})
        Unique(actor, key);
    std::string brain = Key(Text(actor, "brain"), ".brain");
    if (brain.compare(0, 7, "brains/") != 0)
        brain = "brains/" + brain;
    Need(brain == "brains/trainyard/movetrain", "Unsupported initial wagon brain resource");
    const auto starts = CollectPlayerStarts(entities);
    const auto selected = SelectDiagnosticPlayerStart(starts);
    Need(selected.selected, "WagonScene requires a unique authored primary player start");
    const auto &start = starts[selected.candidateIndex];
    const auto &spawn = entities.entities[start.entityIndex];
    Need(start.placement.name == "info_player_start" && Text(spawn, "mission") == "mission_trainyard",
         "Unverified first-mission spawn selection");
    for (const char *key : {"origin", "rotation", "angle"})
        Unique(spawn, key);
    for (const char *key : {"bind", "bindtojoint", "bindtobody"})
        Need(Text(spawn, key).empty(), "Bound player spawn world pose unsupported");
    const double yaw = Yaw(start.placement);
    ModelPlacementOptions placement;
    placement.geometry = options.geometry;
    PlacedStaticModel model =
        LoadPlacedStaticModel1(archive, input, entities, "starting_boxcar_model", placement);
    Need(!model.nonIdentityRotation && !model.unresolvedAnimation && !model.parentRecordFound &&
             !model.parentRecordAmbiguous,
         "Unsupported wagon base rotation/animation/parent record");
    const CollisionVec3 origin = V(model.authored.origin);
    Need(Finite(origin), "Wagon authored origin exceeds budget");
    const auto brainData = ReadUnique(input, archive, "BRAI", brain, options.brain.sourceBytes);
    const auto plan = DecodeDirectBrainStartupH1023(brainData, options.brain);
    const auto definition = ResolveSelfLinearMove(plan.move, entities, L(origin));
    Need(definition.speed <= options.support.maximumSpeed &&
             definition.speed <= options.support.maximumMotion,
         "Wagon speed exceeds bounded carry step policy");
    const BrainStartupRuntime pristine(plan, entities, L(origin));
    std::unique_ptr<Impl> impl(new Impl(pristine));
    impl->options = options;
    impl->options.skins = nullptr;
    if (options.enableArrival) {
        if (!model.skin.empty()) {
            Need(options.skins, "Arrival requires the authored wagon skin catalog");
            options.skins->Apply(model.skin, model.geometry);
        }
        TrainArrivalLimits arrivalLimits;
        arrivalLimits.brain = options.brain;
        impl->pristineRegistry.reset(new LevelEntityRegistry(entities, arrivalLimits));
        impl->arrivalPlan =
            DecodeTrainArrivalH1023(brainData, brain, *impl->pristineRegistry, arrivalLimits);
        std::size_t vertices = model.geometry.vertices.size(), surfaces = model.geometry.surfaces.size(),
                    triangles = 0;
        for (const auto &surface : model.geometry.surfaces)
            triangles += surface.indices.size() / 3;
        for (const auto &target : impl->arrivalPlan.shows) {
            auto placed = LoadPlacedStaticModel1(archive, input, entities, target.name, placement);
            Need(!placed.unresolvedParent && !placed.unresolvedAnimation && !placed.unresolvedBehavior,
                 "Arrival static model has unsupported bind/animation/behavior");
            Need(vertices <= options.geometry.vertices &&
                     placed.geometry.vertices.size() <= options.geometry.vertices - vertices,
                 "Arrival actor vertex budget");
            vertices += placed.geometry.vertices.size();
            Need(surfaces <= options.geometry.surfaces &&
                     placed.geometry.surfaces.size() <= options.geometry.surfaces - surfaces,
                 "Arrival actor surface budget");
            surfaces += placed.geometry.surfaces.size();
            for (const auto &surface : placed.geometry.surfaces) {
                const auto count = surface.indices.size() / 3;
                Need(triangles <= options.geometry.triangles &&
                         count <= options.geometry.triangles - triangles,
                     "Arrival actor triangle budget");
                triangles += count;
            }
            Need(vertices <= options.geometry.totalBufferBytes / sizeof(GeometryVertex) &&
                     triangles <=
                         (options.geometry.totalBufferBytes - vertices * sizeof(GeometryVertex)) /
                             (3 * sizeof(std::uint32_t)),
                 "Arrival actor CPU buffer budget");
            WagonSceneStaticInstance instance;
            instance.authored = placed.authored;
            instance.entityName = target.name;
            instance.modelResource = placed.resourceName;
            instance.skin = placed.skin;
            if (!placed.skin.empty()) {
                Need(options.skins, "Arrival requires the authored static-model skin catalog");
                const auto applied = options.skins->Apply(placed.skin, placed.geometry);
                instance.skinMatchedSurfaces = applied.matchedSurfaces;
                instance.skinChangedSurfaces = applied.changedSurfaces;
            }
            instance.skinResolved = true;
            instance.geometry = std::move(placed.geometry);
            impl->instances.push_back(std::move(instance));
        }
    }
    impl->geometry = std::move(model.geometry);
    const auto collision =
        DecodeModelCollision(ReadUnique(input, archive, "HKXS", map, options.collision.sourceBytes),
                             model.resourceName, options.collision);
    Need(collision.localPoseVerified && !collision.localBoxes.empty(),
         "No supported local wagon collision boxes");
    impl->support.instanceId = "trainyard/starting_boxcar_model";
    impl->support.geometryComplete = collision.geometryComplete;
    for (const auto &local : collision.localBoxes) {
        MovingSupportBox box;
        box.box = local;
        impl->support.localBoxes.push_back(box);
    }
    MovingSupportPose pose;
    pose.translation = origin;
    pose.sample = 1;
    const auto world = PlaceMovingSupport(impl->support, pose, options.support);
    const CollisionVec3 feet = V(start.placement.origin);
    const std::size_t supportIndex = Support(world, feet, options);
    CheckSpawnBox(world, feet, options);
    // Narrow, empirically qualified entity/top-plane case. The selected index
    // comes from geometry, not a retail shape offset. Raw flags are untouched.
    impl->support.localBoxes[supportIndex].verifiedQueryMask = CollisionPlayerMask;
    impl->support.localBoxes[supportIndex].topFaceVerified = true;
    auto &d = impl->description;
    d.mapResource = map;
    d.entityName = model.authored.name;
    d.modelResource = model.resourceName;
    d.brainResource = brain;
    d.destinationName = definition.destinationName;
    d.spawnName = start.placement.name;
    d.authoredBind = model.bind;
    d.authoredSkin = model.skin;
    d.skinResolved = options.enableArrival;
    d.arrivalEnabled = options.enableArrival;
    if (options.enableArrival) {
        d.arrivalBoundaryNode = impl->arrivalPlan.unsupportedNodeId;
        d.arrivalBoundaryType = impl->arrivalPlan.unsupportedNodeType;
        d.arrivalUnsupportedTarget = impl->arrivalPlan.unsupportedTargetName;
        d.arrivalUnsupportedClass = impl->arrivalPlan.unsupportedTargetClassname;
        d.arrivalUnsupportedUseKey = impl->arrivalPlan.unsupportedUseKey;
    }
    d.cartStart = origin;
    d.cartDestination = V(definition.destination);
    d.playerStart = feet;
    d.playerDestination = Add(feet, Sub(d.cartDestination, origin));
    d.speed = definition.speed;
    d.eyeHeight = options.eyeHeight;
    d.playerYawDegrees = yaw;
    d.supportBoxIndex = supportIndex;
    d.unexecutedBrainNodes = plan.unexecutedNodeCount;
    if (options.enableArrival)
        d.unexecutedBrainNodes = impl->arrivalPlan.unexecutedNodeCount;
    d.moveOutputRecords = plan.moveOutputRecords;
    d.authoredWorldOriginCaseVerified = true;
    d.completeModelCollision = collision.geometryComplete;
    d.rawModelContentsComplete = collision.contentsComplete;
    d.omittedCollision = collision.unsupportedGeometry;
    Need(Finite(d.playerDestination) && Finite(Add(feet, {0, 0, options.eyeHeight})),
         "Wagon player path/camera exceeds coordinate budget");
    std::unique_ptr<WagonScene> result(new WagonScene(std::move(impl)));
    result->Reset();
    return result;
}
const GeometryScene &WagonScene::PlacedGeometry() const {
    Need(bool(impl_), "Moved-from WagonScene");
    return impl_->geometry;
}
const WagonSceneDescription &WagonScene::Description() const {
    Need(bool(impl_), "Moved-from WagonScene");
    return impl_->description;
}
const WagonSceneState &WagonScene::State() const {
    Need(bool(impl_), "Moved-from WagonScene");
    return impl_->state;
}
const std::vector<WagonSceneStaticInstance> &WagonScene::ArrivalInstances() const {
    Need(bool(impl_), "Moved-from WagonScene");
    return impl_->instances;
}
bool WagonScene::IsArrivalInstanceVisible(std::size_t index) const {
    Need(bool(impl_) && index < impl_->instances.size(), "Arrival instance index out of range");
    if (!impl_->registry)
        return false;
    const auto *state = impl_->registry->Find(impl_->instances[index].entityName);
    return state && state->alive && state->hiddenKnown && !state->hidden;
}
const LevelEntityRegistry *WagonScene::EntityRegistry() const {
    Need(bool(impl_), "Moved-from WagonScene");
    return impl_->registry.get();
}
const std::vector<TrainArrivalChange> &WagonScene::ArrivalChanges() const {
    Need(bool(impl_), "Moved-from WagonScene");
    return impl_->changes;
}
void WagonScene::Reset() {
    Need(bool(impl_), "Moved-from WagonScene");
    auto &p = *impl_;
    Need(p.generation < (std::numeric_limits<std::uint64_t>::max)(), "Wagon level generation overflow");
    const auto generation = p.generation + 1;
    std::unique_ptr<LevelEntityRegistry> registry;
    std::unique_ptr<TrainArrivalRuntime> arrival;
    if (p.pristineRegistry) {
        registry.reset(new LevelEntityRegistry(*p.pristineRegistry));
        arrival.reset(new TrainArrivalRuntime(p.arrivalPlan, p.description.entityName, generation));
    }
    MovingSupportPose pose;
    pose.translation = p.description.cartStart;
    pose.sample = 1;
    MovingSupportWorld world = PlaceMovingSupport(p.support, pose, p.options.support);
    MovingSupportWitness witness;
    witness.contactEstablished = true;
    witness.qualifiedPartialContact = true;
    witness.boxIndex = p.description.supportBoxIndex;
    witness.maximumGap = p.options.maximumSupportGap;
    const auto established = EstablishMovingSupport(world, p.description.playerStart, p.options.halfWidth,
                                                    p.options.standingHeight, witness, p.options.support);
    Need(established.contact.valid, "Wagon startup support contact failed");
    std::unique_ptr<Impl::Runtime> staged(new Impl::Runtime(p.pristine));
    Need(staged->brain.OnSpawn(), "Wagon initial OnSpawn was already consumed");
    auto &state = staged->state;
    state.cartOrigin = p.description.cartStart;
    state.playerFeet = p.description.playerStart;
    state.cameraOrigin = Add(state.playerFeet, {0, 0, p.options.eyeHeight});
    state.sample = 1;
    state.levelGeneration = generation;
    state.spawnDelivered = staged->brain.SpawnDelivered();
    state.moving = staged->brain.Active();
    state.arrived = staged->brain.Arrived();
    state.supportContact = true;
    staged->world = std::move(world);
    staged->contact = established.contact;
    p.live.swap(staged);
    p.registry.swap(registry);
    p.arrival.swap(arrival);
    p.generation = generation;
    p.changes.clear();
    p.state = p.live->state;
}
void WagonScene::Advance(double seconds) {
    Need(bool(impl_) && std::isfinite(seconds) && seconds >= 0 && seconds <= 1,
         "WagonScene Advance requires finite0..1seconds");
    if (seconds == 0)
        return;
    auto &p = *impl_;
    std::unique_ptr<Impl::Runtime> staged(new Impl::Runtime(*p.live));
    auto &state = staged->state;
    state.elapsedSeconds += seconds;
    Need(std::isfinite(state.elapsedSeconds), "Wagon elapsed time overflow");
    const auto step = staged->brain.Step(seconds);
    state.cartOrigin = V(step.position);
    state.cartOffset = Sub(state.cartOrigin, p.description.cartStart);
    state.moving = step.active;
    state.arrived = step.arrived;
    state.travelled = Length(state.cartOffset);
    if (Length(V(step.delta)) == 0) {
        p.Commit(staged);
        return;
    }
    Need(state.sample < (std::numeric_limits<std::uint64_t>::max)(), "Wagon sample overflow");
    ++state.sample;
    MovingSupportPose pose;
    pose.translation = state.cartOrigin;
    pose.sample = state.sample;
    MovingSupportWorld world = PlaceMovingSupport(p.support, pose, p.options.support);
    const auto carry = TransportMovingSupport(p.live->world, world, p.live->contact, state.playerFeet, true,
                                              seconds, p.options.support);
    Need(carry.hasCandidate && carry.status == MovingSupportStatus::CandidateOnly,
         "Qualified wagon support transport failed");
    // Only this data-driven authored ride is applied. This is not permission
    // for player wish movement or a claim about world/unknown-shape clearance.
    state.playerFeet = carry.candidateFeet;
    state.cameraOrigin = Add(state.playerFeet, {0, 0, p.options.eyeHeight});
    state.supportContact = carry.contact.valid;
    Need(Finite(state.playerFeet) && Finite(state.cameraOrigin),
         "Wagon rider/camera exceeds coordinate budget");
    staged->world = std::move(world);
    staged->contact = carry.contact;
    p.Commit(staged);
}
} // namespace wolf2009

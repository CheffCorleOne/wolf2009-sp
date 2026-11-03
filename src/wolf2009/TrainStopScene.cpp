// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainStopScene.h"
#include "DeclarationText.h"
#include "DoorModel.h"
#include "MapPhysics.h"
#include "TrainDoorSignal.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wolf2009 {
namespace {
const char *const MapName = "maps/game/trainyard/trainyard";
const char *const GlobalName = "maps/game/global/global";
using Matrix = std::array<float, 16>;
CollisionVec3 Add(CollisionVec3 a, CollisionVec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
CollisionVec3 Sub(CollisionVec3 a, CollisionVec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
void Require(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
std::string Text(const EntityRecord &e, const char *key) {
    const auto *property = e.Find(key);
    Require(property && property->kind == EntityProperty::Text, "Missing trainyard text property");
    return property->text;
}
const EntityRecord &Named(const EntityDocument &doc, const std::string &name) {
    const EntityRecord *result = nullptr;
    for (const auto &e : doc.entities) {
        const auto *candidate = e.Find("name");
        if (!candidate || candidate->kind != EntityProperty::Text || candidate->text != name)
            continue;
        Require(!result, "Ambiguous named trainyard entity");
        result = &e;
    }
    Require(result, "Missing named trainyard entity");
    return *result;
}
const EntityRecord &BoundDoor(const EntityDocument &doc, const std::string &target,
                              const std::string &parent) {
    if (!target.empty())
        return Named(doc, target);
    const EntityRecord *door = nullptr;
    for (const auto &entity : doc.entities) {
        const auto *classname = entity.Find("classname");
        const auto *bind = entity.Find("bind");
        if (!classname || classname->text != "env_train_door" || !bind || bind->text != parent)
            continue;
        Require(!door, "Ambiguous untriggered door");
        door = &entity;
    }
    Require(door, "Missing bound wagon door");
    return *door;
}
std::string Strip(std::string name, const std::string &suffix) {
    name = detail::Virtual(name);
    Require(name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0,
            "Unexpected trainyard resource extension");
    return name.substr(0, name.size() - suffix.size());
}
// Read a directly declared asset/animation alias. Ignore nested frame commands.
std::string DeclValue(const std::vector<detail::Token> &block, const char *key,
                      const std::string &alias = {}) {
    std::string result;
    int depth = 0;
    for (std::size_t i = 0; i < block.size(); ++i) {
        if (block[i].brace == '{') {
            ++depth;
            continue;
        }
        if (block[i].brace == '}') {
            --depth;
            continue;
        }
        if (depth || block[i].value != key)
            continue;
        auto value = i + 1;
        if (!alias.empty()) {
            if (value >= block.size() || block[value].brace || block[value].value != alias)
                continue;
            ++value;
        }
        Require(value < block.size() && !block[value].brace && result.empty(),
                "Ambiguous/missing asset declaration value");
        result = block[value].value;
    }
    Require(!result.empty(), "Required asset declaration not found");
    return result;
}
Matrix World(const EntityPlacement &entity, const CollisionVec3 &shift, const Matrix &skin) {
    Matrix result{};
    for (unsigned col = 0; col < 4; ++col)
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned axis = 0; axis < 3; ++axis)
                result[col * 4 + row] += entity.rotation[axis * 3 + row] * skin[col * 4 + axis];
    result[12] += entity.origin[0] + static_cast<float>(shift.x);
    result[13] += entity.origin[1] + static_cast<float>(shift.y);
    result[14] += entity.origin[2] + static_cast<float>(shift.z);
    result[15] = 1;
    return result;
}
} // namespace

struct TrainStopScene::Impl {
    std::unique_ptr<WagonScene> wagon;
    TrainDoorSignalPlan plan;
    PreparedScriptedVoice voice;
    GreeterAnimation greeter;
    DoorModel door;
    EntityPlacement doorEntity;
    RiderMovementSetup riderSetup;
    struct Run {
        TrainDoorSignalRuntime signal;
        GreeterAnimationPlayback actor;
        TrainStopState state;
        GreeterPlaybackPose pose;
        Matrix matrix;
        RiderMovementState rider;
        CollisionVec3 playerFeet, playerEye;
        std::uint64_t riderParentSample = 0;
        Run(const Impl &scene, std::uint64_t generation, bool pristine = false)
            : signal(scene.plan, generation), actor(scene.greeter),
              matrix(World(scene.doorEntity, {}, scene.door.ClosedMatrix())) {
            state.generation = generation;
            const auto &description = scene.wagon->Description();
            state.riderSourceQualified = scene.riderSetup.Succeeded();
            state.riderStatus = scene.riderSetup.status;
            state.riderBlocked = true;
            playerFeet = pristine ? description.playerStart : scene.wagon->State().playerFeet;
            playerEye = Add(playerFeet,{0,0,description.eyeHeight});
            if (state.riderSourceQualified) {
                RiderMovementParent parent;
                parent.instanceId = description.entityName;
                parent.translation = pristine ? description.cartStart : scene.wagon->State().cartOrigin;
                const auto initial = InitializeRiderMovement(scene.riderSetup.profile,parent);
                state.riderControlEnabled = initial.Succeeded();
                state.riderBlocked = !initial.Succeeded();
                state.riderStatus = initial.status;
                if (initial.Succeeded()) {
                    rider = initial.state;
                    playerFeet = rider.worldFeet;
                    playerEye = Add(playerFeet,{0,0,description.eyeHeight});
                }
            }
        }
    };
    std::unique_ptr<Run> run;
    Impl(std::unique_ptr<WagonScene> w, TrainDoorSignalPlan p, PreparedScriptedVoice v,
         GreeterAnimation a, DoorModel m, EntityPlacement e, RiderMovementSetup r)
        : wagon(std::move(w)), plan(std::move(p)), voice(std::move(v)), greeter(std::move(a)),
          door(std::move(m)), doorEntity(std::move(e)), riderSetup(std::move(r)),
          run(new Run(*this, wagon->State().levelGeneration)) {}

    void RiderFrame(Run &next) const {
        next.state.riderEndContact = false;
        next.state.riderBrakingForReversal = false;
        if (next.state.riderControlEnabled) {
            Require(next.riderParentSample < std::numeric_limits<std::uint64_t>::max(),
                    "Rider parent sample overflow");
            RiderMovementParent parent;
            parent.instanceId = wagon->Description().entityName;
            parent.translation = wagon->State().cartOrigin;
            parent.sample = ++next.riderParentSample;
            auto result = StepRiderMovement(riderSetup.profile,next.rider,next.state.riderInput,
                                            parent,riderSetup.profile.parameters.fixedSeconds);
            const bool braking = result.status == RiderMovementStatus::UnsupportedReversal;
            if (braking)
                result = StepRiderMovement(riderSetup.profile,next.rider,RiderMovementInput::Released,
                                          parent,riderSetup.profile.parameters.fixedSeconds);
            if (result.Succeeded()) {
                next.rider = result.state;
                next.state.riderStatus = braking ? RiderMovementStatus::UnsupportedReversal : result.status;
                next.state.riderBrakingForReversal = braking;
                next.state.riderEndContact = result.status == RiderMovementStatus::EndContact;
                next.state.riderBlocked = braking || next.state.riderEndContact;
            } else {
                // Preserve the last local offset and continue the already
                // qualified parent ride. Failed movement never resets position.
                next.state.riderControlEnabled = false;
                next.state.riderInput = RiderMovementInput::Released;
                next.state.riderStatus = result.status;
                next.state.riderBlocked = true;
            }
        }
        next.playerFeet = Add(wagon->State().playerFeet,next.rider.localOffset);
        next.playerEye = Add(wagon->State().cameraOrigin,next.rider.localOffset);
    }

    void Dispatch(Run &next, double time) const {
        const auto &ride = wagon->State();
        Require(ride.arrived && ride.arrivalApplied && ride.levelGeneration == next.state.generation &&
                    !wagon->ArrivalChanges().empty() && wagon->EntityRegistry(),
                "Door signal has no committed wagon arrival");
        const auto *source = wagon->EntityRegistry()->Find(plan.signalEntityName);
        const auto *owner = wagon->EntityRegistry()->Find(plan.ownerEntityName);
        Require(source && source->alive && owner && owner->alive,
                "Train door signal entity disappeared");
        TrainDoorUseEvent event;
        event.sourceEntityName = source->name;
        event.ownerEntityName = owner->name;
        event.useKey = plan.useKey;
        event.levelGeneration = ride.levelGeneration;
        event.eventId = ride.sample;
        auto request = next.signal.OnUsed(event);
        Require(request.command.kind == TrainDoorCommand::PrepareVoiceResources &&
                    request.command.resourceName == voice.binding.definitionName && !voice.bytes.empty(),
                "Voice resources do not match the greeter request");
        request = next.signal.VoiceResourcesReady(request.command, true);
        Require(request.command.kind == TrainDoorCommand::StartActorAnimation &&
                    request.command.resourceName == plan.actorAnimation && next.actor.Start(time),
                "Greeter animation did not start");
        next.pose = next.actor.SampleAt(time);
        Require(next.pose.status == GreeterPlaybackStatus::Playing &&
                    next.pose.pose.model.size() == greeter.Joints().size(),
                "Missing initial actor pose");
        request = next.signal.ActorStartCommitted(request.command, true);
        if (request.command.kind == TrainDoorCommand::ActivateTarget) {
            const auto *target = wagon->EntityRegistry()->Find(request.command.targetEntityName);
            Require(target && target->alive && target->name == doorEntity.name,
                    "Door activation target unavailable");
            next.state.doorActivated = true;
        } else
            Require(request.status == TrainDoorSignalStatus::CompleteWithoutTarget,
                    "Unresolved door activation");
        next.state.voiceReady = true;
        next.state.actorStarted = true;
    }
    void Frame(Run &next, double time, bool initialPoseReady = false) const {
        if (next.actor.Started()) {
            if (!initialPoseReady)
                next.pose = next.actor.SampleAt(time);
            next.state.actorStatus = next.pose.status;
        }
        const auto seconds = next.state.doorActivated ? next.pose.elapsed : 0.;
        next.state.clock = time;
        next.state.doorSeconds = std::min(seconds, door.Duration());
        if (next.state.doorActivated) {
            next.state.doorSlide =
                door.SamplePose(seconds).translation[0] - door.ClosedPose().translation[0];
            next.matrix = World(doorEntity, wagon->State().cartOffset, door.SampleMatrix(seconds));
        } else {
            next.matrix = World(doorEntity, wagon->State().cartOffset, door.ClosedMatrix());
        }
    }
};
TrainStopScene::TrainStopScene(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TrainStopScene::~TrainStopScene() = default;

std::unique_ptr<TrainStopScene> TrainStopScene::Load(const TrainStopSources &src,
                                                     WagonSceneOptions options) {
    options.skins = &src.skins;
    options.enableArrival = true;
    auto wagon = WagonScene::Load(src.map, src.mapArchive, MapName, options);
    const auto entities = ReadMapEntities(src.map, src.mapArchive, MapName);
    const auto &description = wagon->Description();
    Require(description.arrivalUnsupportedClass == "target_brainuse",
            "Unsupported train arrival endpoint");
    const auto &source = Named(entities, description.arrivalUnsupportedTarget);
    const auto &actorEntity = Named(entities, Text(source, "target"));
    auto brain = Strip(Text(actorEntity, "brain"), ".brain");
    if (brain.compare(0, 7, "brains/") != 0)
        brain = "brains/" + brain;
    const auto plan = DecodeTrainDoorSignalH1023(
        ReadMapResource(src.map, src.mapArchive, brain, "BRAI", options.brain.sourceBytes), brain,
        description.arrivalUnsupportedTarget, entities);
    Require(plan.referencedSpeechLines.size() == 1,
            "Expected one speech resource reference in the greeter branch");
    const auto voiceBinding =
        ReadScriptedVoiceBinding(src.assets, src.assetsArchive, plan.voiceDefinition,
                                 plan.referencedSpeechLines.front(), "english");
    auto voice = PrepareScriptedVoice(voiceBinding, src.voiceMounts, voiceBinding.localizedPack,
                                      voiceBinding.localizedPack);
    const auto mapAnimation = ReadMapResource(src.map, src.mapArchive, MapName, "HKXA", 64u << 20);
    const auto greeter = GreeterAnimation::Load(
        mapAnimation, ReadMapResource(src.global, src.globalArchive, GlobalName, "SKEL", 64u << 20));
    const auto mapDefs = detail::ZipText(src.assets, src.assetsArchive, "def/maps/trainyard.def");
    const auto actorModel = detail::Selected(mapDefs, "model", "model_trainyard_base");
    const auto actorClip =
        Strip(DeclValue(actorModel, "anim", plan.actorAnimation), ".md5anim") + ".hkx";
    Require(actorClip == greeter.ResourceName() &&
                detail::Virtual(DeclValue(actorModel, "skeleton")) == greeter.SkeletonName(),
            "Greeter clip/skeleton differs from model declaration");
    const auto &doorEntity = BoundDoor(entities, plan.targetEntityName, description.entityName);
    const auto placement = ReadEntityPlacement(doorEntity);
    Require(placement.hasOrigin && placement.hasRotation && placement.classname == "env_train_door" &&
                Text(doorEntity, "bind") == description.entityName &&
                Text(doorEntity, "start_anim") == "idle" && Text(doorEntity, "anim1") == "door_move" &&
                Text(doorEntity, "anim2") == "end_idle",
            "Unsupported bound door placement");
    const auto doorDefs =
        detail::ZipText(src.assets, src.assetsArchive, "def/mapobjects/train_door.def");
    const auto definition = detail::Selected(doorDefs, "entityDef", placement.classname);
    Require(DeclValue(definition, "noclipmodel") == "1", "Unknown door collision mode");
    const auto model = detail::Selected(doorDefs, "model", DeclValue(definition, "model"));
    const auto resource = Strip(DeclValue(model, "mesh"), ".md5r");
    Require(detail::Virtual(DeclValue(model, "anim", "door_move")) ==
                "models/props/train_door/srp_tyd_nml_train_door_anim.hkx",
            "Door move alias refers to a different clip");
    const auto animation = DoorAnimation::Load(
        mapAnimation, ReadMapResource(src.map, src.mapArchive, MapName, "SKEL", 64u << 20));
    auto door = DoorModel::Load(
        ReadMapResource(src.map, src.mapArchive, resource, "MODL", options.geometry.sourceBytes),
        resource, animation, options.geometry);
    RiderMovementSetup rider;
    try {
        ModelConvexCollisionLimits limits;
        limits.graph = options.collision;
        const auto collision = DecodeModelPrimitiveCollision(
            ReadMapResource(src.map,src.mapArchive,MapName,"HKXS",options.collision.sourceBytes),
            description.modelResource,limits);
        RiderMovementQualification qualification;
        qualification.instanceId = description.entityName;
        qualification.centralOpeningCaseVerified = description.authoredWorldOriginCaseVerified &&
                                                   wagon->State().supportContact;
        RiderMovementParameters parameters;
        parameters.halfWidth = options.halfWidth;
        parameters.height = options.standingHeight;
        parameters.maximumSupportGap = options.maximumSupportGap;
        rider = BuildRiderMovementProfile(collision,Sub(description.playerStart,description.cartStart),
                                          qualification,parameters);
    } catch (const std::exception &) {
        rider.status = RiderMovementStatus::UnknownGeometry;
        rider.reason = "Unrecognized local collision profile";
    }
    std::unique_ptr<Impl> impl(
        new Impl(std::move(wagon), plan, std::move(voice), greeter, std::move(door), placement,
                 std::move(rider)));
    return std::unique_ptr<TrainStopScene>(new TrainStopScene(std::move(impl)));
}
const WagonScene &TrainStopScene::Wagon() const { return *impl_->wagon; }
const GeometryScene &TrainStopScene::DoorBindGeometry() const {
    return impl_->door.Source().bindGeometry;
}
const Matrix &TrainStopScene::DoorWorldMatrix() const { return impl_->run->matrix; }
const TrainStopState &TrainStopScene::State() const { return impl_->run->state; }
const GreeterPlaybackPose &TrainStopScene::ActorPose() const { return impl_->run->pose; }
const CollisionVec3 &TrainStopScene::PlayerFeet() const { return impl_->run->playerFeet; }
const CollisionVec3 &TrainStopScene::PlayerEye() const { return impl_->run->playerEye; }
void TrainStopScene::SetRiderInput(RiderMovementInput input) {
    const int value = static_cast<int>(input);
    Require(value >= -1 && value <= 1,"Unsupported rider input");
    impl_->run->state.riderInput = input;
}
void TrainStopScene::Advance(double seconds) {
    Require(std::isfinite(seconds) && seconds >= 0 && seconds <= 1, "Invalid trainyard frame time");
    if (seconds == 0)
        return;
    auto &p = *impl_;
    const double step = 1.0/60.0;
    const double accumulated = p.run->state.fixedRemainderSeconds + seconds;
    const auto ticks = static_cast<std::size_t>(std::floor((accumulated+1e-12)/step));
    Require(ticks <= 61,"Trainyard fixed-step budget exceeded");
    double remainder = accumulated-ticks*step;
    if (remainder < 0 && remainder > -1e-10)
        remainder = 0;
    Require(std::isfinite(remainder) && remainder >= 0 && remainder < step,
            "Invalid trainyard fractional step");
    for (std::size_t tick=0;tick<ticks;++tick) {
        const double time = p.run->state.clock+step;
        Require(std::isfinite(time),"Trainyard clock overflow");
        if (!p.wagon->State().arrived)
            p.wagon->Advance(step);
        if (p.wagon->State().arrivalApplied && !p.run->state.actorStarted) {
            std::unique_ptr<Impl::Run> staged(new Impl::Run(*p.run));
            p.Dispatch(*staged,time);
            p.Frame(*staged,time,true);
            p.RiderFrame(*staged);
            p.run.swap(staged);
        } else {
            const bool finished = p.run->state.actorStatus == GreeterPlaybackStatus::Finished &&
                (!p.run->state.doorActivated || p.run->state.doorSeconds >= p.door.Duration());
            if (finished)
                p.run->state.clock = time;
            else
                p.Frame(*p.run,time);
            p.RiderFrame(*p.run);
        }
    }
    p.run->state.fixedRemainderSeconds = remainder;
}
void TrainStopScene::Reset() {
    auto &p = *impl_;
    const auto generation = p.wagon->State().levelGeneration;
    Require(generation < std::numeric_limits<std::uint64_t>::max(), "Trainyard generation overflow");
    std::unique_ptr<Impl::Run> next(new Impl::Run(p, generation + 1, true));
    p.wagon->Reset();
    p.run.swap(next);
}
} // namespace wolf2009

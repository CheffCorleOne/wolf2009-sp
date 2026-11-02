// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAIN_STOP_SCENE_H
#define WOLF2009_TRAIN_STOP_SCENE_H
#include "GreeterAnimation.h"
#include "ScriptedVoice.h"
#include "WagonScene.h"
#include <array>
namespace wolf2009 {
struct TrainStopSources {
    std::istream &map;
    const Archive &mapArchive;
    std::istream &assets;
    const ZipArchive &assetsArchive;
    std::istream &global;
    const Archive &globalArchive;
    // Mount the localized SPK under its full, verified relative package name.
    const StreamMounts &voiceMounts;
    const SkinCatalog &skins;
};
struct TrainStopState {
    double clock = 0, doorSeconds = 0, doorSlide = 0;
    bool voiceReady = false, actorStarted = false, doorActivated = false;
    std::uint64_t generation = 0;
    GreeterPlaybackStatus actorStatus = GreeterPlaybackStatus::NotStarted;
};
// First trainyard signal from the wagon through voice preparation, an actual
// started skeletal clip and its bound animated door. Caller owns all sources
// only during Load. World collision and later AI/BRAI outputs are separate.
class TrainStopScene {
  public:
    static std::unique_ptr<TrainStopScene> Load(const TrainStopSources &sources,
                                                WagonSceneOptions options = WagonSceneOptions());
    ~TrainStopScene();
    const WagonScene &Wagon() const;
    const GeometryScene &DoorBindGeometry() const;
    // One column-major matrix for the original bind-pose vertices, including
    // the door's authored basis and its parent's current world displacement.
    const std::array<float, 16> &DoorWorldMatrix() const;
    const TrainStopState &State() const;
    const GreeterPlaybackPose &ActorPose() const;
    void Advance(double seconds);
    void Reset();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit TrainStopScene(std::unique_ptr<Impl> impl);
};
} // namespace wolf2009
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainStopScene.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool condition, const char *msg) {
    if (!condition)
        throw std::runtime_error(msg);
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 2 || argc == 3, "Expected SP/base [experimental no-trigger trainyard.mpk]");
        const std::string base = argv[1];
        const bool bypass = argc == 3;
        std::ifstream map(bypass ? argv[2] : base + "/maps/game/trainyard/trainyard.mpk",
                          std::ios::binary);
        std::ifstream assets(base + "/assets.pk4", std::ios::binary);
        std::ifstream global(base + "/maps/game/global/global.spk", std::ios::binary);
        auto mpk = wolf2009::Archive::Scan(map), globalArchive = wolf2009::Archive::Scan(global);
        const auto zip = wolf2009::ZipArchive::Scan(assets);
        auto skins = wolf2009::SkinCatalog::Read(assets, zip);
        wolf2009::StreamMounts voices([&](const std::string &id) {
            return std::unique_ptr<std::istream>(new std::ifstream(base + '/' + id, std::ios::binary));
        });
        const std::string pack = "streampacks/scripted_voice_kc1_trn_vo_english.spk";
        voices.Mount(pack);
        auto scene = wolf2009::TrainStopScene::Load(
            {map, mpk, assets, zip, global, globalArchive, voices, skins});
        Check(voices.Unmount(pack), "Voice package did not unmount");
        map.close();
        assets.close();
        global.close();
        const auto &door = scene->DoorBindGeometry();
        Check(door.vertices.size() == 196 && door.surfaces.size() == 1 &&
                  door.surfaces[0].indices.size() == 504,
              "Wrong train door mesh");
        const auto *vb = door.vertices.data();
        const auto start = scene->DoorWorldMatrix();
        Check(scene->State().riderSourceQualified && scene->State().riderControlEnabled &&
                  !scene->State().riderBlocked &&
                  std::abs(scene->PlayerFeet().y-scene->Wagon().Description().playerStart.y)<1e-7 &&
                  std::abs(scene->PlayerEye().z-scene->PlayerFeet().z-64)<1e-7,
              "Central rider source/initial placement not ready");
        for (int frame = 0; frame < 620; ++frame)
            scene->Advance(.02);
        Check(!scene->State().actorStarted && !scene->State().doorActivated &&
                  !scene->Wagon().State().arrived,
              "Door/greeter started before the committed arrival");
        for (int frame = 0; frame < 25; ++frame)
            scene->Advance(.02);
        Check(scene->Wagon().State().arrivalChangeCount == 4 && scene->State().voiceReady &&
                  scene->State().actorStarted && scene->State().doorActivated != bypass &&
                  scene->ActorPose().pose.model.size() == 52,
              "Greeter/door event sequence differs from the PC");
        for (int frame = 0; frame < 420; ++frame)
            scene->Advance(.02);
        const auto &end = scene->State();
        const auto expected = bypass ? 0. : -59.24313;
        Check(std::abs(end.doorSlide - expected) < .005 &&
                  end.actorStatus == wolf2009::GreeterPlaybackStatus::Finished &&
                  std::abs(scene->DoorWorldMatrix()[13] - (start[13] + 320 + expected)) < .03,
              "Bound door motion in world space differs from the original");
        std::cout << "Trainyard arrived, visible=" << scene->Wagon().State().visibleArrivalInstances
                  << " voice=" << end.voiceReady << " bones=" << scene->ActorPose().pose.model.size()
                  << " door=" << end.doorActivated << " slide=" << end.doorSlide << '\n';
        scene->Reset();
        Check(scene->State().generation == 2 && !scene->State().voiceReady &&
                  !scene->State().doorActivated && !scene->State().actorStarted &&
                  scene->Wagon().State().visibleArrivalInstances == 0 &&
                  vb == scene->DoorBindGeometry().vertices.data() && scene->DoorWorldMatrix() == start,
              "Failed to reset scene");
        Check(scene->State().riderInput == wolf2009::RiderMovementInput::Released &&
                  scene->State().fixedRemainderSeconds == 0 &&
                  scene->State().riderControlEnabled && !scene->State().riderEndContact,
              "Reset retained rider input/velocity/fraction");
        const auto *cartVertices=scene->Wagon().PlacedGeometry().vertices.data();
        const double step=1.0/60.0;
        const auto frames=[&](wolf2009::RiderMovementInput input,int count) {
            scene->SetRiderInput(input);
            for(int i=0;i<count;++i)scene->Advance(step);
        };
        const auto offset=[&](){return scene->PlayerFeet().y-scene->Wagon().State().playerFeet.y;};
        const auto pristine=scene->PlayerFeet();
        scene->SetRiderInput(wolf2009::RiderMovementInput::Forward);
        scene->Advance(step*.5);
        Check(scene->PlayerFeet().y==pristine.y && scene->State().clock==0 &&
                  scene->State().fixedRemainderSeconds>0,
              "Fractional frame advanced fixed rider early");
        scene->Advance(step*.5);
        Check(std::abs(offset()-131.23*step)<1e-7 &&
                  scene->State().fixedRemainderSeconds==0,
              "Fixed rider fraction/carry double counted");
        scene->Advance(step);
        scene->SetRiderInput(wolf2009::RiderMovementInput::Released);
        const auto paused=scene->PlayerFeet();
        scene->Advance(0);
        Check(scene->PlayerFeet().y==paused.y &&
                  scene->State().riderInput==wolf2009::RiderMovementInput::Released,
              "Release/pause changed rider position");
        scene->Advance(step);
        const double beforeReverse=offset();
        frames(wolf2009::RiderMovementInput::Backward,1);
        Check(scene->State().riderBrakingForReversal && scene->State().riderBlocked &&
                  scene->State().riderControlEnabled && std::abs(offset()-beforeReverse)<1e-7,
              "Rapid reversal reset position or bypassed normal release");
        frames(wolf2009::RiderMovementInput::Backward,1);
        Check(!scene->State().riderBrakingForReversal && offset()<beforeReverse,
              "Requested backward motion did not resume after braking");

        scene->Reset();
        const auto movingGeneration=scene->State().generation;
        frames(wolf2009::RiderMovementInput::Forward,38);
        frames(wolf2009::RiderMovementInput::Released,18);
        Check(scene->Wagon().State().moving && std::abs(offset()-88.68)<.04,
              "Original W/release route during ride differs");
        frames(wolf2009::RiderMovementInput::Backward,38);
        frames(wolf2009::RiderMovementInput::Released,18);
        Check(scene->Wagon().State().moving && std::abs(offset()-8.88)<.04 &&
                  scene->State().generation==movingGeneration && !scene->State().actorStarted,
              "Original moving W/release/S route or story generation differs");

        scene->Reset();
        frames(wolf2009::RiderMovementInput::Released,780);
        const auto stoppedCart=scene->Wagon().State().cartOrigin;
        const auto stoppedClock=scene->State().clock;
        const auto stoppedGeneration=scene->State().generation;
        frames(wolf2009::RiderMovementInput::Forward,38);
        frames(wolf2009::RiderMovementInput::Released,18);
        frames(wolf2009::RiderMovementInput::Backward,38);
        frames(wolf2009::RiderMovementInput::Released,18);
        Check(scene->Wagon().State().arrived &&
                  std::abs(scene->Wagon().State().cartOrigin.y-stoppedCart.y)<1e-9 &&
                  std::abs(offset()-8.88)<.04 &&
                  std::abs(scene->State().clock-stoppedClock-112*step)<1e-8 &&
                  scene->State().generation==stoppedGeneration && scene->State().actorStarted,
              "Stopped W/release/S route stopped door clock or doubled carry");
        frames(wolf2009::RiderMovementInput::Backward,90);
        const double back=scene->PlayerFeet().y-scene->Wagon().State().cartOrigin.y;
        Check(scene->State().riderEndContact && scene->State().riderBlocked &&
                  std::abs(back+186.925437988)<1e-6,
              "Back end did not follow measured source AABB contact");
        frames(wolf2009::RiderMovementInput::Forward,1);
        Check(!scene->State().riderBlocked &&
                  scene->PlayerFeet().y-scene->Wagon().State().cartOrigin.y>back,
              "Source end contact prevented moving away");
        frames(wolf2009::RiderMovementInput::Forward,240);
        Check(scene->State().riderEndContact &&
                  std::abs(scene->PlayerFeet().y-scene->Wagon().State().cartOrigin.y-186.955212151)<1e-6 &&
                  std::abs(scene->PlayerEye().z-scene->PlayerFeet().z-64)<1e-7 &&
                  cartVertices==scene->Wagon().PlacedGeometry().vertices.data() &&
                  vb==scene->DoorBindGeometry().vertices.data(),
              "Front source end/camera/immutable buffers differ");
        scene->Reset();
        Check(scene->State().fixedRemainderSeconds==0 &&
                  scene->State().riderInput==wolf2009::RiderMovementInput::Released &&
                  std::abs(offset())<1e-8 &&
                  std::abs(scene->PlayerFeet().y-scene->Wagon().Description().playerStart.y)<1e-8,
              "Rider replay did not restart from authored data");
        const double unchangedClock=scene->State().clock;
        bool rejected=false;
        try{scene->Advance((std::numeric_limits<double>::quiet_NaN)());}catch(const std::exception&){rejected=true;}
        Check(rejected&&scene->State().clock==unchangedClock,"Invalid frame changed train scene");
        std::cout<<"Central rider W/release/S: moving+stopped; genuine end contacts; reset/fraction/carry passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainStopScene.h"
#include <cmath>
#include <fstream>
#include <iostream>
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
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

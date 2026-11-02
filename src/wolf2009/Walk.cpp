// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapPhysics.h"
#include "PlayerMove.h"
#include "LinearMover.h"
#include "MovingSupport.h"
#include "BrainStartup.h"
#include "ModelCollision.h"
#include "ConvexContact.h"
#include "ModelConvexCollision.h"
#include "ModelPrimitiveCollision.h"
#include "WagonScene.h"
#include "TrainArrival.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
std::string MapName(const wolf2009::Archive& archive) {
    std::string name;
    for (const auto& chunk : archive.Chunks()) {
        if (chunk.type != "PROC") continue;
        for (const auto& entry : chunk.entries) {
            if (!name.empty()) throw std::runtime_error("More than one map PROC in the selected package");
            name = entry.name;
        }
    }
    if (name.empty()) throw std::runtime_error("Missing map PROC");
    return name;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        using SelfTest = int (*)();
        const SelfTest suites[] = {
            wolf2009::RunStaticCollisionTests, wolf2009::RunPlayerMoveTests,
            wolf2009::RunLinearMoverTests, wolf2009::RunMovingSupportTests,
            wolf2009::RunModelCollisionTests, wolf2009::RunConvexContactTests,
            wolf2009::RunModelConvexCollisionTests, wolf2009::RunModelPrimitiveCollisionTests,
            wolf2009::RunBrainStartupTests,
            wolf2009::RunWagonSceneTests, wolf2009::RunTrainArrivalTests
        };
        int failed = 0;
        for (const auto run : suites) failed += run() != 0;
        return failed == 0 ? 0 : 1;
    }
    if (argc < 2 || argc > 3) {
        std::cerr << "wolf2009-walk INPUT.mpk [START_NAME]\n"
                     "Simulates four seconds of movement east from an authored spawn.\n"
                     "Requires a fully decoded static body and no omitted bodies.\n";
        return 2;
    }
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open the map package");
        const auto archive = wolf2009::Archive::Scan(input);
        const auto map = MapName(archive);
        const auto starts = wolf2009::ReadMapPlayerStarts(input, archive, map);
        const auto choice = wolf2009::SelectDiagnosticPlayerStart(starts, argc == 3 ? argv[2] : "");
        if (!choice.selected) throw std::runtime_error(wolf2009::PlayerStartSelectionReasonText(choice.reason));
        const auto& placement = starts.at(choice.candidateIndex).placement;
        if (!placement.hasOrigin) throw std::runtime_error("Selected spawn has no authored position");
        const auto collision = wolf2009::ReadMapStaticCollision(input, archive, map);
        const wolf2009::CollisionVec3 origin(placement.origin[0], placement.origin[1], placement.origin[2]);
        const wolf2009::PlayerMoveParameters parameters;
        auto result = wolf2009::InitializePlayerMove(collision, origin, parameters);
        if (!result.Succeeded()) throw std::runtime_error(wolf2009::PlayerMoveStatusText(result.status));
        std::size_t contacts = 0;
        for (int step = 0; step < 240; ++step) {
            result = wolf2009::StepPlayerMove(collision, result.state, {1,0,0}, parameters);
            if (!result.Succeeded()) throw std::runtime_error(wolf2009::PlayerMoveStatusText(result.status));
            contacts += result.contacts;
        }
        const auto& feet = result.state.feet;
        std::cout << std::fixed << std::setprecision(4)
                  << "Spawn XYZ: " << origin.x << ' ' << origin.y << ' ' << origin.z
                  << "\nFinal XYZ: " << feet.x << ' ' << feet.y << ' ' << feet.z
                  << "\nGrounded: " << result.state.grounded << "\nContacts: " << contacts << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Walk unavailable: " << error.what() << '\n';
        return 1;
    }
}

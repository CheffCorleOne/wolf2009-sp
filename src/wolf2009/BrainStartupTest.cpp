// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainStartup.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void Refuses(const std::function<void()> &action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Unsupported startup graph accepted");
}
std::string Line(std::string value) {
    for (auto &c : value)
        if (c == '|')
            c = 0;
    return value;
}
std::string Output(const std::string &target = "9", int nonzeroIndex = -1) {
    std::string text = "\t";
    for (int i = 0; i < 99; i++)
        text += Line((i == 0              ? "16"
                      : i == 1            ? "1"
                      : i == 2            ? target
                      : i == 98           ? "1.000000"
                      : i == nonzeroIndex ? "1"
                                          : "0") +
                     "|");
    return text + "\n";
}
std::vector<unsigned char> Graph(const std::string &edge = "", const std::string &extra = "",
                                 const std::string &source = "1|20|0|0|0|^1|1|^74|1024|",
                                 bool sentinel = true) {
    const auto text = Line("h1023|32|32|32|0|\n") + Line(source + "\n") + (edge.empty() ? Output() : edge) +
                      (sentinel ? Line("\t\t-1|\n") : "") +
                      Line("132|9|5|5|0|^132|16|^131|1024|mMover|Me|mDestination|finish|mSpeed|25|mTime|0|"
                           "mAccelTime|0|mDecelTime|0|mMaxSpeed|0|mMaxTime|0|mUseCustomSounds|false|\n") +
                      extra;
    return {text.begin(), text.end()};
}
EntityDocument Entities() {
    EntityDocument document;
    EntityRecord entity;
    EntityProperty name;
    name.key = "name";
    name.text = "finish";
    entity.properties.push_back(name);
    EntityProperty origin;
    origin.key = "origin";
    origin.kind = EntityProperty::Floats;
    origin.numbers = {0, 50, 0};
    entity.properties.push_back(origin);
    document.entities.push_back(entity);
    return document;
}
} // namespace
int RunBrainStartupTests() {
    int passed = 0;
    try {
        const auto bytes = Graph();
        const auto plan = DecodeDirectBrainStartupH1023(bytes);
        Need(plan.onSpawnNodeId == 20 && plan.moveNodeId == 9 && plan.move.speed == 25 &&
                 plan.move.destinationName == "finish",
             "Startup selection used record order rather than IDs");
        ++passed;
        BrainStartupRuntime runtime(plan, Entities(), {0, 0, 0});
        Need(!runtime.SpawnDelivered() && !runtime.Active() && runtime.Step(1).delta.y == 0,
             "Construction or Step synthesized OnSpawn");
        ++passed;
        Need(runtime.OnSpawn() && runtime.SpawnDelivered() && runtime.Active(),
             "Level event did not activate confirmed mover");
        ++passed;
        auto step = runtime.Step(.5);
        Need(step.position.y == 12.5 && step.delta.y == 12.5, "Spawn event mover used wrong speed");
        ++passed;
        Need(!runtime.OnSpawn() && runtime.Position().y == 12.5,
             "Duplicate spawn event reset or retriggered mover");
        ++passed;
        runtime.Step(1);
        step = runtime.Step(1);
        Need(step.arrived && step.position.y == 50 && !runtime.Active() && !runtime.OnSpawn() &&
                 runtime.Step(1).delta.y == 0,
             "Arrival or repeated event replayed startup chain");
        ++passed;
        const auto later = DecodeDirectBrainStartupH1023(
            Graph("", Line("\t16|1|11|0|0|\n456|11|0|0|0|^456|0|mCommand|DO_NOT_EXECUTE|")));
        Need(later.unexecutedNodeCount == 1 && later.moveOutputRecords == 1,
             "Unexecuted continuation was hidden");
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Output() + Output())); });
        ++passed;
        Refuses([&] {
            DecodeDirectBrainStartupH1023(
                Graph(Output("8"), Line("30|8|0|0|0|^30|0|^65|1024|mDuration|1000 0|")));
        });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Output("20"))); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Output("999"))); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Output("9", 3))); });
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Output("9", 50))); });
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Output("9", 97))); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Line("\t16|1|9|0|0|\n"))); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph("", "", "1|20|0|0|0|^1|0|^74|1024|")); });
        ++passed;
        Refuses(
            [&] { DecodeDirectBrainStartupH1023(Graph("", "", "1|20|0|0|0|^1|1|^74|1024|unexpected|1|")); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph("", Line("1|21|0|0|0|^1|1|^74|1024|"))); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph("", Line("70|9|0|0|0|^70|0|"))); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph("", "", "3|20|0|0|0|^3|1|^74|1024|")); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph("", "", "1|20|0|0|0|^1|1|^74|1024|", false)); });
        ++passed;
        Refuses([&] { DecodeDirectBrainStartupH1023(Graph(Output() + Line("\t\t1|\n"))); });
        ++passed;
        auto bad = bytes;
        bad.pop_back();
        bad.pop_back();
        Refuses([&] { DecodeDirectBrainStartupH1023(bad); });
        ++passed;
        LinearMoverLimits limits;
        limits.sourceBytes = 4;
        Refuses([&] { DecodeDirectBrainStartupH1023(bytes, limits); });
        limits = LinearMoverLimits();
        limits.records = 2;
        Refuses([&] { DecodeDirectBrainStartupH1023(bytes, limits); });
        limits = LinearMoverLimits();
        limits.tokens = 10;
        Refuses([&] { DecodeDirectBrainStartupH1023(bytes, limits); });
        ++passed;
        std::cout << "Brain startup self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Brain startup test failed after " << passed << ": " << error.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009
#ifdef WOLF2009_BRAIN_STARTUP_TEST_MAIN
#include <fstream>
#include <iterator>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            return wolf2009::RunBrainStartupTests();
        std::ifstream input(argv[1], std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open private BRAI fixture");
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>()};
        const auto plan = wolf2009::DecodeDirectBrainStartupH1023(bytes);
        std::cout << "accepted OnSpawn=" << plan.onSpawnNodeId << " MoveToEnt=" << plan.moveNodeId
                  << " speed=" << plan.move.speed << " destination=" << plan.move.destinationName
                  << " unexecutedNodes=" << plan.unexecutedNodeCount
                  << " moveOutputRecords=" << plan.moveOutputRecords << '\n';
        if (bytes.size() == 2620 && bytes[439] == '1' && bytes[440] == 0) {
            bytes[439] = '3';
            bool refused = false;
            try {
                wolf2009::DecodeDirectBrainStartupH1023(bytes);
            } catch (const std::exception &e) {
                refused = true;
                std::cout << "causal redirect refused: " << e.what() << '\n';
            }
            if (!refused)
                throw std::runtime_error("Real one-byte redirected startup accepted");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif

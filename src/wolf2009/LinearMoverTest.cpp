// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinearMover.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool Near(double a, double b, double epsilon = 1e-8) {
    return std::abs(a - b) <= epsilon;
}
void Refuses(const std::function<void()> &f) {
    bool refused = false;
    try {
        f();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Unsupported/malformed input accepted");
}
std::vector<unsigned char> Fixture(const std::string &properties = "") {
    const std::string text =
        "h1023|32|32|32|0|\n132|7|10|20|1|^132|16|^131|1024|" +
        (properties.empty() ? "mMover|Me|mDestination|finish|mSpeed|25|mTime|0|mAccelTime|0|mDecelTime|0|"
                              "mMaxSpeed|0|mMaxTime|0|mUseCustomSounds|false|"
                            : properties) +
        "\n\t16|3|10|0|\n\t\t-1|";
    std::vector<unsigned char> bytes(text.begin(), text.end());
    for (auto &c : bytes)
        if (c == '|')
            c = 0;
    return bytes;
}
EntityDocument Targets() {
    EntityDocument document;
    EntityRecord entity;
    EntityProperty name;
    name.key = "name";
    name.text = "finish";
    entity.properties.push_back(name);
    EntityProperty origin;
    origin.key = "origin";
    origin.kind = EntityProperty::Floats;
    origin.numbers = {10, 30, 20};
    entity.properties.push_back(origin);
    document.entities.push_back(entity);
    return document;
}
std::vector<unsigned char> Replace(std::vector<unsigned char> data, const std::string &before,
                                   const std::string &after) {
    std::string text(data.begin(), data.end());
    const auto at = text.find(before);
    Need(at != std::string::npos, "Synthetic replacement not found");
    text.replace(at, before.size(), after);
    return {text.begin(), text.end()};
}
} // namespace
int RunLinearMoverTests() {
    int passed = 0;
    try {
        const auto bytes = Fixture();
        const auto configuration = DecodeSelfLinearMoveH1023(bytes);
        Need(configuration.destinationName == "finish" && configuration.speed == 25 &&
                 configuration.sourceRecord == 1,
             "Profile extraction incorrect");
        ++passed;
        auto entities = Targets();
        auto definition = ResolveSelfLinearMove(configuration, entities, {10, -20, 20});
        Need(definition.start.y == -20 && definition.destination.y == 30,
             "Resolver changed caller world origin");
        ++passed;
        LinearMover mover(definition);
        Need(!mover.Active() && !mover.Arrived() && mover.Step(1).delta.y == 0,
             "Mover autoactivated from configuration");
        ++passed;
        mover.Activate();
        auto step = mover.Step(.5);
        Need(Near(step.position.y, -7.5) && Near(step.delta.y, 12.5) && step.active,
             "Constant speed step incorrect");
        ++passed;
        step = mover.Step(1);
        Need(Near(step.position.y, 17.5) && Near(step.delta.y, 25), "Second constant speed step incorrect");
        ++passed;
        step = mover.Step(1);
        Need(step.arrived && !step.active && step.position.y == 30 && Near(step.delta.y, 12.5),
             "Mover overshot or failed exact endpoint clamp");
        ++passed;
        const auto rider = TranslateConfirmedRider({12, 10, 60}, step);
        Need(rider.x == 12 && Near(rider.y, 22.5) && rider.z == 60,
             "Confirmed rider translation changed relative offset");
        ++passed;
        mover.Activate();
        Need(mover.Step(1).delta.y == 0 && mover.Position().y == 30, "Arrived mover restarted or drifted");
        ++passed;
        definition.start = definition.destination;
        LinearMover zero(definition);
        zero.Activate();
        Need(zero.Arrived() && !zero.Active() && zero.Step(0).delta.x == 0,
             "Zero-length movement divided by zero");
        ++passed;
        definition.start = {0, 0, 0};
        definition.destination = {3, 4, 0};
        definition.speed = 2;
        LinearMover diagonal(definition);
        diagonal.Activate();
        Need(Near(diagonal.Step(.5).position.x, .6) && Near(diagonal.Position().y, .8),
             "Diagonal motion used per-axis speed");
        ++passed;
        Refuses([&] { mover.Step(-1); });
        Refuses([&] { mover.Step(1.01); });
        Refuses([&] { mover.Step(std::numeric_limits<double>::quiet_NaN()); });
        Need(mover.Position().y == 30, "Rejected step mutated state");
        ++passed;
        Refuses([&] { DecodeSelfLinearMoveH1023(Replace(bytes, "h1023", "h1024")); });
        ++passed;
        auto truncated = bytes;
        truncated.pop_back();
        Refuses([&] { DecodeSelfLinearMoveH1023(truncated); });
        ++passed;
        auto duplicate = bytes;
        const auto second = bytes.begin() + 18;
        duplicate.push_back('\n');
        duplicate.insert(duplicate.end(), second, bytes.end());
        Refuses([&] { DecodeSelfLinearMoveH1023(duplicate); });
        ++passed;
        Refuses([&] { DecodeSelfLinearMoveH1023(Replace(bytes, "^131", "^130")); });
        ++passed;
        Refuses([&] { DecodeSelfLinearMoveH1023(Replace(bytes, "Me", "Other")); });
        ++passed;
        Refuses([&] { DecodeSelfLinearMoveH1023(Replace(bytes, "25", "0")); });
        Refuses([&] { DecodeSelfLinearMoveH1023(Replace(bytes, "25", "nan")); });
        ++passed;
        Refuses([&] { DecodeSelfLinearMoveH1023(Replace(bytes, "mTime", "unexpected")); });
        ++passed;
        const std::string timed = "mMover|Me|mDestination|finish|mSpeed|25|mTime|1|mAccelTime|0|mDecelTime|0|"
                                  "mMaxSpeed|0|mMaxTime|0|mUseCustomSounds|false|";
        Refuses([&] { DecodeSelfLinearMoveH1023(Fixture(timed)); });
        ++passed;
        Refuses([&] { DecodeSelfLinearMoveH1023(Replace(bytes, "false", "true")); });
        ++passed;
        auto bad = bytes;
        bad[18] = '\t';
        Refuses([&] { DecodeSelfLinearMoveH1023(bad); });
        ++passed;
        LinearMoverLimits limits;
        limits.sourceBytes = 8;
        Refuses([&] { DecodeSelfLinearMoveH1023(bytes, limits); });
        limits = LinearMoverLimits();
        limits.tokenBytes = 3;
        Refuses([&] { DecodeSelfLinearMoveH1023(bytes, limits); });
        limits = LinearMoverLimits();
        limits.records = 1;
        Refuses([&] { DecodeSelfLinearMoveH1023(bytes, limits); });
        limits = LinearMoverLimits();
        limits.tokens = 2;
        Refuses([&] { DecodeSelfLinearMoveH1023(bytes, limits); });
        ++passed;
        auto missing = entities;
        missing.entities.clear();
        Refuses([&] { ResolveSelfLinearMove(configuration, missing, {0, 0, 0}); });
        entities.entities.push_back(entities.entities[0]);
        Refuses([&] { ResolveSelfLinearMove(configuration, entities, {0, 0, 0}); });
        ++passed;
        entities = Targets();
        EntityProperty bind;
        bind.key = "bind";
        bind.text = "parent";
        entities.entities[0].properties.push_back(bind);
        Refuses([&] { ResolveSelfLinearMove(configuration, entities, {0, 0, 0}); });
        ++passed;
        entities = Targets();
        entities.entities[0].properties[1].numbers = {1, 2};
        Refuses([&] { ResolveSelfLinearMove(configuration, entities, {0, 0, 0}); });
        ++passed;
        Refuses([&] { TranslateConfirmedRider({std::numeric_limits<double>::infinity(), 0, 0}, step); });
        ++passed;
        std::cout << "Linear mover self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Linear mover test failed after " << passed << ": " << error.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009
#ifdef WOLF2009_LINEAR_MOVER_TEST_MAIN
#include "Archive.h"
#include <fstream>
#include <iterator>
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            return wolf2009::RunLinearMoverTests();
        std::ifstream brain(argv[1], std::ios::binary);
        if (!brain)
            throw std::runtime_error("Cannot open private BRAI fixture");
        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(brain),
                                               std::istreambuf_iterator<char>()};
        const auto configuration = wolf2009::DecodeSelfLinearMoveH1023(bytes);
        std::ifstream package(argv[2], std::ios::binary);
        if (!package)
            throw std::runtime_error("Cannot open private MPK fixture");
        const auto archive = wolf2009::Archive::Scan(package);
        std::vector<unsigned char> entities;
        unsigned count = 0;
        for (std::size_t c = 0; c < archive.Chunks().size(); c++)
            if (archive.Chunks()[c].type == "ENTS")
                for (std::size_t e = 0; e < archive.Chunks()[c].entries.size(); e++) {
                    entities = archive.ReadEntry(package, c, e);
                    ++count;
                }
        if (count != 1)
            throw std::runtime_error("Private fixture needs exactly one ENTS entry");
        const auto definition = wolf2009::ResolveSelfLinearMove(
            configuration, wolf2009::DecodeEntities4(entities), {256.38, -60.02, 256});
        wolf2009::LinearMover mover(definition);
        mover.Activate();
        auto rider = wolf2009::LinearMoverVec3(258, -218.51, 297.34);
        for (unsigned i = 0; i < 120; i++)
            rider = wolf2009::TranslateConfirmedRider(rider, mover.Step(1.0 / 60));
        std::cout << "speed=" << configuration.speed << " destination=" << configuration.destinationName
                  << " predicted2sMoverDY=" << mover.Position().y + 60.02
                  << " observedMoverDY=50.43 predicted2sRiderDY=" << rider.y + 218.51
                  << " observedRiderDY=50.42\n";
        for (unsigned i = 0; i < 720; i++)
            rider = wolf2009::TranslateConfirmedRider(rider, mover.Step(1.0 / 60));
        if (!mover.Arrived() || mover.Position().x != definition.destination.x ||
            mover.Position().y != definition.destination.y || mover.Position().z != definition.destination.z)
            throw std::runtime_error("Real mover did not clamp exactly to ENTS destination");
        const auto stopped = mover.Step(1);
        if (stopped.delta.x != 0 || stopped.delta.y != 0 || stopped.delta.z != 0)
            throw std::runtime_error("Real mover drifted after arrival");
        std::cout << "endpoint=" << mover.Position().x << "," << mover.Position().y << ","
                  << mover.Position().z << " observed=256.38,239.98,256 rider=" << rider.x << "," << rider.y
                  << "," << rider.z << " observedRider=258,81.51,297.34 held1sDelta=0\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif

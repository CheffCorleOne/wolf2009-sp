// SPDX-License-Identifier: GPL-3.0-or-later
#include "DoorAnimation.h"
#include "StreamMounts.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool success, const char *message) {
    if (!success)
        throw std::runtime_error(message);
}
template <typename Function> void Reject(Function function) {
    try {
        function();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid animation input was accepted");
}
} // namespace
int main(int argc, char **argv) {
    try {
        Require(argc == 2, "Expected a trainyard.mpk path");
        wolf2009::StreamMounts mounts([&](const std::string &) {
            return std::unique_ptr<std::istream>(new std::ifstream(argv[1], std::ios::binary));
        });
        mounts.Mount("trainyard.mpk");
        const char *map = "maps/game/trainyard/trainyard";
        const auto skel = mounts.ReadUnique("SKEL", map);
        auto hkxa = mounts.ReadUnique("HKXA", map);
        const auto anim = wolf2009::DoorAnimation::Load(hkxa, skel);
        Require(std::abs(anim.Duration() - 181. / 24) < .00001, "Wrong door clip duration");
        Require(anim.Slide(-1) == 0 && std::abs(anim.Slide(100) + 59.24313) < .0001,
                "Wrong slide endpoints");
        // PC sampling returns world Y at the joint minus its bound entity Y.
        // 13.016 is an alignment fitted to this reference run only.
        const double observations[][2] = {
            {13.283001, -73.88},  {14.316001, -74.68},  {14.833001, -84.35}, {15.35, -88.88},
            {16.383001, -91.13},  {16.900002, -101.49}, {17.416, -117.73},   {17.933001, -127.26},
            {18.450001, -131.58}, {18.966002, -133.12}};
        double error = 0;
        for (const auto &point : observations) {
            error = std::max(error, std::abs(anim.LocalX(point[0] - 13.016) - point[1]));
        }
        Require(error <= .005, "Joint trajectory differs from the original PC trace");
        Reject([&] { anim.LocalX(std::numeric_limits<double>::infinity()); });
        Reject([&] { wolf2009::DoorAnimation::Load(hkxa, hkxa); });
        hkxa.pop_back();
        Reject([&] { wolf2009::DoorAnimation::Load(hkxa, skel); });
        std::cout << "Door motion duration=" << anim.Duration()
                  << " slide=" << anim.Slide(anim.Duration()) << " max PC trace error=" << error << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

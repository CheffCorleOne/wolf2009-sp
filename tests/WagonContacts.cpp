// SPDX-License-Identifier: GPL-3.0-or-later
#include "ModelConvexCollision.h"
#include "StreamMounts.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
using namespace wolf2009;
struct Wall {
    std::uint32_t shape;
    int direction;
    double rayY, boxY;
};

// Original VM casts on an isolated trainyard with the boxcar stationary at its spawn.
// These reference positions and this model's two face IDs belong only to this test.
void CheckWall(const ModelConvexCollision &model, const Wall &wall) {
    const auto hull = std::find_if(
        model.localHulls.begin(), model.localHulls.end(),
        [&](const ConvexContactHull &candidate) { return candidate.sourceShapeOffset == wall.shape; });
    if (hull == model.localHulls.end())
        throw std::runtime_error("Missing measured end panel");
    const auto face =
        std::find_if(hull->planes.begin(), hull->planes.end(), [&](const ConvexContactPlane &plane) {
            return plane.normal.y * wall.direction < -.999999;
        });
    if (face == hull->planes.end())
        throw std::runtime_error("Missing measured end face");

    ConvexContactQualification witness;
    witness.instanceId = "starting_boxcar_model";
    witness.verifiedQueryMask = CollisionPlayerMask;
    witness.sourceShapeOffset = wall.shape;
    witness.sourceRegion = hull->sourceRegion;
    witness.planeIndex = static_cast<std::size_t>(face - hull->planes.begin());
    const CollisionVec3 origin(256.3800048828125, -80.0199966430664, 256);
    for (bool box : {false, true}) {
        const double expected = box ? wall.boxY : wall.rayY;
        const CollisionVec3 start(origin.x, origin.y, origin.z + (box ? 77.34 : 90));
        const auto contact = SweepConvexContact(*hull, witness, origin, start,
                                                box ? CollisionVec3(16, 16, 36) : CollisionVec3(),
                                                {0, wall.direction * 512., 0}, CollisionPlayerMask, box);
        std::cout << (box ? "box" : "ray") << " panel=" << wall.shape << " PC=" << expected
                  << " native=" << std::setprecision(10) << contact.center.y << '\n';
        if (contact.status != ConvexContactStatus::ProvenFaceHit || contact.sceneClearanceVerified ||
            std::abs(contact.center.y - expected) > .005 ||
            std::abs(contact.normal.y + wall.direction) > .00001)
            throw std::runtime_error("Contact differs from the original VM measurement");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected the path of the original trainyard.mpk");
        wolf2009::StreamMounts mounts([&](const std::string &) {
            return std::unique_ptr<std::istream>(new std::ifstream(argv[1], std::ios::binary));
        });
        mounts.Mount("trainyard.mpk");
        const auto hkxs = mounts.ReadUnique("HKXS", "maps/game/trainyard/trainyard");
        const auto model = wolf2009::DecodeModelConvexCollision(hkxs, "models/vehicles/box_car_a");
        if (model.geometryComplete || model.contentsComplete)
            throw std::runtime_error("Partial model was reported complete");
        CheckWall(model, {4965936, -1, -284.91, -266.95});
        CheckWall(model, {4966320, 1, 124.90, 106.94});
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

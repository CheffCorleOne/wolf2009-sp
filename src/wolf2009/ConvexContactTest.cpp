// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConvexContact.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
bool Near(double a, double b) { return std::abs(a - b) < 1e-7; }
template <class F> void Reject(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    Need(failed, "Invalid convex input accepted");
}
ConvexContactHull Cube() {
    ConvexContactHull h;
    h.sourceShapeOffset = 17;
    h.sourceRegion = 123;
    h.radius = 2;
    for (int i = 0; i < 8; ++i)
        h.vertices.push_back({i & 1 ? 10. : -10., i & 2 ? 10. : -10., i & 4 ? 10. : -10.});
    for (int i = 0; i < 3; ++i)
        for (int sign = -1; sign <= 1; sign += 2) {
            ConvexContactPlane p;
            p.normal = {i == 0 ? double(sign) : 0, i == 1 ? double(sign) : 0, i == 2 ? double(sign) : 0};
            p.distance = -10;
            h.planes.push_back(p);
        }
    return h;
}
ConvexContactQualification Qualified() {
    ConvexContactQualification q;
    q.instanceId = "synthetic/platform";
    q.sourceShapeOffset = 17;
    q.sourceRegion = 123;
    q.verifiedQueryMask = CollisionPlayerMask;
    q.planeIndex = 2;
    return q;
}
ConvexContactResult Sweep(const ConvexContactHull &h, CollisionVec3 p, CollisionVec3 half,
                          CollisionVec3 d, bool radius = true) {
    auto q = Qualified();
    if (d.y < 0)
        q.planeIndex = 3;
    return SweepConvexContact(h, q, {}, p, half, d, CollisionPlayerMask, radius);
}
} // namespace
int RunConvexContactTests() {
    int passed = 0;
    try {
        const ConvexContactHull h = Cube();
        {
            const auto r = Sweep(h, {0, -30, 0}, {1, 3, 2}, {0, 40, 0});
            Need(r.status == ConvexContactStatus::ProvenFaceHit && Near(r.fraction, .375) &&
                     Near(r.center.y, -15) && Near(r.hullContact.y, -10) && Near(r.normal.y, -1) &&
                     !r.sceneClearanceVerified,
                 "Rounded AABB face hit mismatch");
            ++passed;
        }
        {
            const auto r = Sweep(h, {0, 30, 0}, {1, 3, 2}, {0, -40, 0});
            Need(r.status == ConvexContactStatus::ProvenFaceHit && Near(r.center.y, 15) &&
                     Near(r.normal.y, 1),
                 "Reverse face hit mismatch");
            ++passed;
        }
        {
            const auto r = Sweep(h, {0, -30, 0}, {}, {0, 40, 0}, false);
            Need(r.status == ConvexContactStatus::ProvenFaceHit && Near(r.fraction, .5) &&
                     Near(r.center.y, -10),
                 "Core point ray used radius");
            ++passed;
        }
        {
            const auto r = Sweep(h, {0, -30, 0}, {}, {0, 40, 0});
            Need(r.status == ConvexContactStatus::ProvenFaceHit && Near(r.center.y, -12),
                 "Rounded point sweep omitted radius");
            ++passed;
        }
        {
            const auto r = Sweep(h, {11, -30, 0}, {}, {0, 40, 0});
            Need(r.status == ConvexContactStatus::IndeterminateEdge && !r.sceneClearanceVerified,
                 "Offset planes invented rounded edge hit");
            ++passed;
        }
        {
            const auto r = Sweep(h, {10, -30, 0}, {}, {0, 40, 0});
            Need(r.status == ConvexContactStatus::IndeterminateEdge,
                 "Exact face edge certified strict interior");
            ++passed;
        }
        {
            const auto r = Sweep(h, {30, -30, 0}, {1, 1, 1}, {0, 40, 0});
            Need(r.status == ConvexContactStatus::NoHitForThisHull && !r.sceneClearanceVerified,
                 "Single hull miss claimed scene clearance");
            ++passed;
        }
        {
            const auto r = Sweep(h, {0, -30, 0}, {1, 1, 1}, {0, -20, 0});
            Need(r.status == ConvexContactStatus::NoHitForThisHull, "Away movement hit");
            ++passed;
        }
        {
            const auto r = Sweep(h, {0, 0, 0}, {1, 1, 1}, {});
            Need(r.status == ConvexContactStatus::IndeterminateInitialOverlap,
                 "Zero-delta overlap certified clear");
            ++passed;
        }
        {
            const auto r = Sweep(h, {0, -13, 0}, {1, 1, 1}, {0, 0, 20});
            Need(r.status == ConvexContactStatus::IndeterminateInitialOverlap,
                 "Boundary tangency silently cleared");
            ++passed;
        }
        {
            const auto r = Sweep(h, {0, -30, 0}, {1, 1, 1}, {});
            Need(r.status == ConvexContactStatus::NoHitForThisHull, "Separated zero-delta query hit");
            ++passed;
        }
        {
            auto q = Qualified();
            q.sourceShapeOffset = 18;
            Need(SweepConvexContact(h, q, {}, {0, -30, 0}, {}, {0, 40, 0}).status ==
                     ConvexContactStatus::Unqualified,
                 "Wrong shape witness accepted");
            ++passed;
        }
        {
            auto q = Qualified();
            q.sourceRegion = 124;
            Need(SweepConvexContact(h, q, {}, {0, -30, 0}, {}, {0, 40, 0}).status ==
                     ConvexContactStatus::Unqualified,
                 "Different source region borrowed witness");
            ++passed;
        }
        {
            auto q = Qualified();
            q.planeIndex = 3;
            Need(SweepConvexContact(h, q, {}, {0, -30, 0}, {}, {0, 40, 0}).status ==
                     ConvexContactStatus::Unqualified,
                 "Different face borrowed witness");
            ++passed;
        }
        {
            auto q = Qualified();
            q.verifiedQueryMask = CollisionMonsterClip;
            Need(SweepConvexContact(h, q, {}, {0, -30, 0}, {}, {0, 40, 0}).status ==
                     ConvexContactStatus::Unqualified,
                 "Different mask borrowed witness");
            ++passed;
        }
        {
            auto q = Qualified();
            q.instanceId.clear();
            Need(SweepConvexContact(h, q, {}, {0, -30, 0}, {}, {0, 40, 0}).status ==
                     ConvexContactStatus::Unqualified,
                 "Absent entity accepted");
            ++passed;
        }
        {
            auto q = Qualified();
            const auto r =
                SweepConvexContact(h, q, {100, 200, 300}, {100, 170, 300}, {1, 3, 2}, {0, 40, 0});
            Need(r.status == ConvexContactStatus::ProvenFaceHit && Near(r.center.y, 185) &&
                     Near(r.hullContact.z, 300),
                 "Runtime translation altered model face");
            ++passed;
        }
        {
            auto broken = h;
            broken.planes[0].normal.x = -2;
            Reject([&] { Sweep(broken, {0, -30, 0}, {}, {0, 40, 0}); });
            ++passed;
        }
        {
            auto broken = h;
            broken.vertices[0].x = -20;
            Reject([&] { Sweep(broken, {0, -30, 0}, {}, {0, 40, 0}); });
            ++passed;
        }
        {
            auto broken = h;
            broken.radius = -1;
            Reject([&] { Sweep(broken, {0, -30, 0}, {}, {0, 40, 0}); });
            ++passed;
        }
        {
            auto broken = h;
            broken.vertices[0].x = std::numeric_limits<double>::infinity();
            Reject([&] { Sweep(broken, {0, -30, 0}, {}, {0, 40, 0}); });
            ++passed;
        }
        {
            Reject([&] { Sweep(h, {0, -30, 0}, {-1, 1, 1}, {0, 40, 0}); });
            ++passed;
        }
        {
            ConvexContactLimits l;
            l.vertices = 7;
            Reject([&] {
                SweepConvexContact(h, Qualified(), {}, {0, -30, 0}, {}, {0, 40, 0}, CollisionPlayerMask,
                                   true, l);
            });
            ++passed;
        }
        // A deliberately incomplete plane set still cannot manufacture a hit:
        // candidate q must belong to the actual source-vertex face polygon.
        {
            auto cut = h;
            cut.vertices.clear();
            for (int z = -1; z <= 1; z += 2) {
                cut.vertices.push_back({-10, -10, 10. * z});
                cut.vertices.push_back({10, -10, 10. * z});
                cut.vertices.push_back({-10, 10, 10. * z});
            }
            const auto r = Sweep(cut, {9, 30, 0}, {}, {0, -40, 0});
            Need(r.status == ConvexContactStatus::IndeterminateEdge,
                 "Missing plane caused fictitious face hit");
            ++passed;
        }
        {
            auto rotated = h;
            const double s = std::sqrt(.5);
            const auto rotate = [&](CollisionVec3 v) {
                return CollisionVec3(s * (v.x - v.y), s * (v.x + v.y), v.z);
            };
            for (auto &v : rotated.vertices)
                v = rotate(v);
            for (auto &p : rotated.planes)
                p.normal = rotate(p.normal);
            const auto r = SweepConvexContact(rotated, Qualified(), {}, {30 * s, -30 * s, 0}, {1, 1, 1},
                                              {-40 * s, 40 * s, 0});
            Need(r.status == ConvexContactStatus::ProvenFaceHit &&
                     Near(r.fraction, (30 - 12 - std::sqrt(2.)) / 40) && Near(r.normal.x, s) &&
                     Near(r.normal.y, -s),
                 "Oblique flat face support incorrect");
            ++passed;
        }
        std::cout << "Convex contact tests: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Convex contact tests failed after " << passed << " checks: " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

// SPDX-License-Identifier: GPL-3.0-or-later
#include "MovingSupport.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool Near(double a, double b) {
    return std::abs(a - b) < 1e-8;
}
template <class F> void Reject(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    Need(failed, "Invalid platform input accepted");
}
MovingSupportGeometry Geometry() {
    MovingSupportGeometry g;
    g.instanceId = "synthetic/platform";
    g.geometryComplete = true;
    MovingSupportBox b;
    b.box.minimum = {-50, -100, -4};
    b.box.maximum = {50, 100, 0};
    b.box.sourceShapeOffset = 123;
    b.box.contents = 3;
    b.verifiedQueryMask = CollisionPlayerMask;
    b.topFaceVerified = true;
    g.localBoxes.push_back(b);
    return g;
}
MovingSupportPose Pose(CollisionVec3 translation, std::uint64_t sample) {
    MovingSupportPose p;
    p.translation = translation;
    p.sample = sample;
    return p;
}
MovingSupportWitness Witness() {
    MovingSupportWitness w;
    w.contactEstablished = true;
    w.boxIndex = 0;
    return w;
}
MovingSupportContact Contact(const MovingSupportWorld &w, CollisionVec3 feet = {0, 0, 0}) {
    const MovingSupportResult r = EstablishMovingSupport(w, feet, 16, 72, Witness());
    Need(r.status == MovingSupportStatus::ContactEstablished && r.contact.valid,
         "Synthetic support setup failed");
    return r.contact;
}
void NoCandidate(const MovingSupportResult &r, MovingSupportStatus status, CollisionVec3 unchanged) {
    Need(r.status == status && !r.hasCandidate && !r.clearanceVerified &&
             Near(r.candidateFeet.x, unchanged.x) && Near(r.candidateFeet.y, unchanged.y) &&
             Near(r.candidateFeet.z, unchanged.z),
         "Rejected carry changed rider or claimed clearance");
}
} // namespace
int RunMovingSupportTests() {
    int passed = 0;
    try {
        const MovingSupportGeometry geometry = Geometry();
        const MovingSupportWorld first = PlaceMovingSupport(geometry, Pose({}, 1)),
                                 next = PlaceMovingSupport(geometry, Pose({0, 10, 0}, 2));
        Need(next.boxes.size() == 1 && next.boxes[0].box.minimum.y == -90 &&
                 next.boxes[0].box.maximum.y == 110 && next.boxes[0].box.contents == 3 &&
                 next.boxes[0].box.contentsKnown && next.boxes[0].box.sourceShapeOffset == 123,
             "Local box placement/metadata failed");
        Need(geometry.localBoxes[0].box.minimum.y == -100, "Placement mutated local geometry");
        ++passed;
        MovingSupportContact c = Contact(first);
        Need(c.localFeet.x == 0 && c.localFeet.y == 0 && c.gap == 0 && c.sample == 1,
             "Established contact coordinates wrong");
        ++passed;
        MovingSupportResult r = TransportMovingSupport(first, next, c, {0, 0, 0}, true, .1);
        Need(r.hasCandidate && r.status == MovingSupportStatus::CandidateOnly && r.candidateFeet.y == 10 &&
                 r.platformDelta.y == 10 && r.contact.sample == 2 && r.contact.localFeet.y == 0 &&
                 !r.clearanceVerified,
             "Translation carry candidate failed");
        ++passed;
        NoCandidate(TransportMovingSupport(first, next, c, {}, false, .1),
                    MovingSupportStatus::NoContactWitness, {});
        MovingSupportWitness w = Witness();
        w.contactEstablished = false;
        NoCandidate(EstablishMovingSupport(first, {}, 16, 72, w), MovingSupportStatus::NoContactWitness, {});
        ++passed;
        MovingSupportWorld wrong = next;
        wrong.instanceId = "other/platform";
        NoCandidate(TransportMovingSupport(first, wrong, c, {}, true, .1), MovingSupportStatus::WrongSupport,
                    {});
        MovingSupportContact bad = c;
        bad.instanceId = "other/platform";
        NoCandidate(TransportMovingSupport(first, next, bad, {}, true, .1), MovingSupportStatus::WrongSupport,
                    {});
        ++passed;
        bad = c;
        bad.boxIndex = 1;
        NoCandidate(TransportMovingSupport(first, next, bad, {}, true, .1), MovingSupportStatus::WrongSupport,
                    {});
        bad = c;
        bad.sourceShapeOffset = 999;
        NoCandidate(TransportMovingSupport(first, next, bad, {}, true, .1), MovingSupportStatus::WrongSupport,
                    {});
        ++passed;
        wrong = next;
        wrong.sample = 1;
        NoCandidate(TransportMovingSupport(first, wrong, c, {}, true, .1), MovingSupportStatus::StaleSample,
                    {});
        bad = c;
        bad.sample = 0;
        NoCandidate(TransportMovingSupport(first, next, bad, {}, true, .1), MovingSupportStatus::StaleSample,
                    {});
        ++passed;
        NoCandidate(TransportMovingSupport(first, next, c, {66, 0, 0}, true, .1),
                    MovingSupportStatus::ContactLost, {66, 0, 0});
        NoCandidate(TransportMovingSupport(first, next, c, {0, 116, 0}, true, .1),
                    MovingSupportStatus::ContactLost, {0, 116, 0});
        ++passed;
        NoCandidate(TransportMovingSupport(first, next, c, {0, 0, 1}, true, .1),
                    MovingSupportStatus::ContactLost, {0, 0, 1});
        NoCandidate(TransportMovingSupport(first, next, c, {0, 0, -1}, true, .1),
                    MovingSupportStatus::ContactLost, {0, 0, -1});
        ++passed;
        r = TransportMovingSupport(first, next, c, {5, 6, 0}, true, .1);
        Need(r.hasCandidate && r.candidateFeet.x == 5 && r.candidateFeet.y == 16 &&
                 r.contact.localFeet.y == 6,
             "Independent relative rider movement lost");
        ++passed;
        MovingSupportGeometry partial = geometry;
        partial.geometryComplete = false;
        MovingSupportWorld p0 = PlaceMovingSupport(partial, Pose({}, 1)),
                           p1 = PlaceMovingSupport(partial, Pose({0, 10, 0}, 2));
        w = Witness();
        NoCandidate(EstablishMovingSupport(p0, {}, 16, 72, w), MovingSupportStatus::PartialGeometry, {});
        w.qualifiedPartialContact = true;
        r = EstablishMovingSupport(p0, {}, 16, 72, w);
        Need(r.contact.valid && !r.geometryComplete && !r.hasCandidate && !r.clearanceVerified,
             "Qualified partial contact claimed walkability");
        r = TransportMovingSupport(p0, p1, r.contact, {}, true, .1);
        Need(r.hasCandidate && !r.geometryComplete && !r.clearanceVerified,
             "Qualified partial carry claimed full clearance");
        ++passed;
        NoCandidate(TransportMovingSupport(first, p1, c, {}, true, .1), MovingSupportStatus::PartialGeometry,
                    {});
        ++passed;
        MovingSupportGeometry unknown = geometry;
        unknown.localBoxes[0].box.contentsKnown = false;
        unknown.localBoxes[0].box.contents = 0;
        unknown.localBoxes[0].verifiedQueryMask = 0;
        MovingSupportWorld u0 = PlaceMovingSupport(unknown, Pose({}, 1));
        NoCandidate(EstablishMovingSupport(u0, {}, 16, 72, Witness()), MovingSupportStatus::UnverifiedMask,
                    {});
        unknown.localBoxes[0].verifiedQueryMask = CollisionPlayerMask;
        u0 = PlaceMovingSupport(unknown, Pose({}, 1));
        r = EstablishMovingSupport(u0, {}, 16, 72, Witness());
        Need(r.contact.valid && !u0.boxes[0].box.contentsKnown && u0.boxes[0].box.contents == 0,
             "Runtime mask qualification invented raw contents");
        ++passed;
        w = Witness();
        w.queryMask = CollisionSolid;
        NoCandidate(EstablishMovingSupport(first, {}, 16, 72, w), MovingSupportStatus::UnverifiedMask, {});
        unknown = geometry;
        unknown.localBoxes[0].topFaceVerified = false;
        u0 = PlaceMovingSupport(unknown, Pose({}, 1));
        NoCandidate(EstablishMovingSupport(u0, {}, 16, 72, Witness()),
                    MovingSupportStatus::UnverifiedSupportFace, {});
        ++passed;
        w = Witness();
        w.maximumGap = 2;
        r = EstablishMovingSupport(first, {0, 0, 1.5}, 16, 72, w);
        Need(r.contact.valid && r.contact.gap == 1.5 && r.candidateFeet.z == 1.5,
             "Explicit support gap was snapped or rejected");
        r = TransportMovingSupport(first, next, r.contact, {0, 0, 1.5}, true, .1);
        Need(r.hasCandidate && r.candidateFeet.z == 1.5, "Gap changed during carry");
        w.maximumGap = 1;
        NoCandidate(EstablishMovingSupport(first, {0, 0, 1.5}, 16, 72, w), MovingSupportStatus::ContactLost,
                    {0, 0, 1.5});
        ++passed;
        MovingSupportWorld far = PlaceMovingSupport(geometry, Pose({0, 129, 0}, 2));
        NoCandidate(TransportMovingSupport(first, far, c, {}, true, 1), MovingSupportStatus::ExcessiveMotion,
                    {});
        NoCandidate(TransportMovingSupport(first, next, c, {}, true, .001),
                    MovingSupportStatus::ExcessiveMotion, {});
        ++passed;
        wrong = next;
        wrong.boxes[0].box.minimum.x += 1;
        NoCandidate(TransportMovingSupport(first, wrong, c, {}, true, .1),
                    MovingSupportStatus::ChangedGeometry, {});
        wrong = next;
        wrong.boxes[0].box.contents = 1;
        NoCandidate(TransportMovingSupport(first, wrong, c, {}, true, .1),
                    MovingSupportStatus::ChangedGeometry, {});
        ++passed;
        MovingSupportWorld raised = PlaceMovingSupport(geometry, Pose({0, 0, 5}, 2));
        r = TransportMovingSupport(first, raised, c, {}, true, .1);
        Need(r.hasCandidate && r.candidateFeet.z == 5 && !r.clearanceVerified,
             "Vertical carry falsely claimed overhead clearance");
        const MovingSupportWorld stationary = PlaceMovingSupport(geometry, Pose({}, 2));
        r = TransportMovingSupport(first, stationary, c, {}, true, .1);
        Need(r.hasCandidate && r.platformDelta.x == 0 && r.platformDelta.y == 0 && r.platformDelta.z == 0,
             "Stationary support changed rider");
        ++passed;
        bad = c;
        bad.gap = 1;
        NoCandidate(TransportMovingSupport(first, next, bad, {}, true, .1), MovingSupportStatus::ContactLost,
                    {});
        bad = c;
        bad.localFeet = {1000, 0, 0};
        NoCandidate(TransportMovingSupport(first, next, bad, {}, true, .1), MovingSupportStatus::ContactLost,
                    {});
        ++passed;
        NoCandidate(TransportMovingSupport(first, next, c, {}, true, 0), MovingSupportStatus::InvalidInput,
                    {});
        NoCandidate(TransportMovingSupport(first, next, c, {}, true, 3), MovingSupportStatus::InvalidInput,
                    {});
        NoCandidate(EstablishMovingSupport(first, {}, -1, 72, Witness()), MovingSupportStatus::InvalidInput,
                    {});
        ++passed;
        MovingSupportGeometry malformed = geometry;
        malformed.localBoxes[0].box.maximum.x = -100;
        Reject([&] { PlaceMovingSupport(malformed, Pose({}, 1)); });
        malformed = geometry;
        malformed.localBoxes[0].box.contents = CollisionMonsterClip;
        Reject([&] { PlaceMovingSupport(malformed, Pose({}, 1)); });
        ++passed;
        MovingSupportLimits limits;
        limits.boxes = 0;
        Reject([&] { PlaceMovingSupport(geometry, Pose({}, 1), limits); });
        limits = MovingSupportLimits();
        limits.coordinateGameUnits = 1;
        Reject([&] { PlaceMovingSupport(geometry, Pose({}, 1), limits); });
        Reject([&] {
            PlaceMovingSupport(geometry, Pose({(std::numeric_limits<double>::quiet_NaN)(), 0, 0}, 1));
        });
        ++passed;
        std::cout << "Moving support self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Moving support self-test failed after " << passed << " checks: " << e.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

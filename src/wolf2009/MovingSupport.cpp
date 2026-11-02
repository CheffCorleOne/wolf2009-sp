// SPDX-License-Identifier: GPL-3.0-or-later
#include "MovingSupport.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
CollisionVec3 Add(CollisionVec3 a, CollisionVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
CollisionVec3 Sub(CollisionVec3 a, CollisionVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double Length(CollisionVec3 a) {
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
bool Finite(CollisionVec3 v, double bound) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= bound &&
           std::abs(v.y) <= bound && std::abs(v.z) <= bound;
}
bool Near(CollisionVec3 a, CollisionVec3 b) {
    return std::abs(a.x - b.x) <= 1e-7 && std::abs(a.y - b.y) <= 1e-7 && std::abs(a.z - b.z) <= 1e-7;
}
bool LimitsValid(const MovingSupportLimits &l) {
    return l.boxes > 0 && std::isfinite(l.coordinateGameUnits) && l.coordinateGameUnits > 0 &&
           l.coordinateGameUnits <= 1e10 && std::isfinite(l.maximumContactGap) && l.maximumContactGap >= 0 &&
           l.maximumContactGap <= 100 && std::isfinite(l.maximumMotion) && l.maximumMotion >= 0 &&
           l.maximumMotion <= 1e6 && std::isfinite(l.maximumSpeed) && l.maximumSpeed >= 0 &&
           l.maximumSpeed <= 1e6 && std::isfinite(l.maximumElapsedSeconds) && l.maximumElapsedSeconds > 0 &&
           l.maximumElapsedSeconds <= 60;
}
bool IdValid(const std::string &id) {
    if (id.empty() || id.size() > 256)
        return false;
    for (std::size_t i = 0; i < id.size(); ++i)
        if (static_cast<unsigned char>(id[i]) < 32 || static_cast<unsigned char>(id[i]) > 126)
            return false;
    return true;
}
bool BoxValid(const MovingSupportBox &b, const MovingSupportLimits &l) {
    return Finite(b.box.minimum, l.coordinateGameUnits) && Finite(b.box.maximum, l.coordinateGameUnits) &&
           b.box.minimum.x < b.box.maximum.x && b.box.minimum.y < b.box.maximum.y &&
           b.box.minimum.z < b.box.maximum.z &&
           (!b.verifiedQueryMask || !b.box.contentsKnown || (b.box.contents & b.verifiedQueryMask) != 0);
}
bool WorldValid(const MovingSupportWorld &w, const MovingSupportLimits &l) {
    if (!IdValid(w.instanceId) || !Finite(w.translation, l.coordinateGameUnits) || w.boxes.empty() ||
        w.boxes.size() > l.boxes)
        return false;
    for (std::size_t i = 0; i < w.boxes.size(); ++i)
        if (!BoxValid(w.boxes[i], l))
            return false;
    return true;
}
bool RiderValid(CollisionVec3 feet, double half, double height, const MovingSupportLimits &l) {
    return Finite(feet, l.coordinateGameUnits) && std::isfinite(half) && half > 0 && half <= 10000 &&
           std::isfinite(height) && height > 0 && height <= 10000;
}
bool Supported(const StaticCollisionBox &box, CollisionVec3 feet, double half, double maxGap, double &gap) {
    const double overlapX =
        (std::min)(feet.x + half, box.maximum.x) - (std::max)(feet.x - half, box.minimum.x);
    const double overlapY =
        (std::min)(feet.y + half, box.maximum.y) - (std::max)(feet.y - half, box.minimum.y);
    gap = feet.z - box.maximum.z;
    return overlapX > 1e-8 && overlapY > 1e-8 && gap >= -1e-7 && gap <= maxGap + 1e-7;
}
bool SameGeometry(const MovingSupportWorld &a, const MovingSupportWorld &b, CollisionVec3 delta) {
    if (a.boxes.size() != b.boxes.size())
        return false;
    for (std::size_t i = 0; i < a.boxes.size(); ++i) {
        const MovingSupportBox &x = a.boxes[i];
        const MovingSupportBox &y = b.boxes[i];
        if (x.box.sourceShapeOffset != y.box.sourceShapeOffset || x.box.contents != y.box.contents ||
            x.box.contentsKnown != y.box.contentsKnown || x.verifiedQueryMask != y.verifiedQueryMask ||
            x.topFaceVerified != y.topFaceVerified || !Near(Add(x.box.minimum, delta), y.box.minimum) ||
            !Near(Add(x.box.maximum, delta), y.box.maximum))
            return false;
    }
    return true;
}
} // namespace
MovingSupportWorld PlaceMovingSupport(const MovingSupportGeometry &geometry, const MovingSupportPose &pose,
                                      const MovingSupportLimits &limits) {
    Need(LimitsValid(limits), "Invalid moving-support limits");
    Need(IdValid(geometry.instanceId) && Finite(pose.translation, limits.coordinateGameUnits) &&
             !geometry.localBoxes.empty() && geometry.localBoxes.size() <= limits.boxes,
         "Invalid moving-support geometry/pose");
    MovingSupportWorld result;
    result.instanceId = geometry.instanceId;
    result.translation = pose.translation;
    result.sample = pose.sample;
    result.geometryComplete = geometry.geometryComplete;
    result.boxes.reserve(geometry.localBoxes.size());
    for (std::size_t i = 0; i < geometry.localBoxes.size(); ++i) {
        Need(BoxValid(geometry.localBoxes[i], limits), "Invalid local support box/qualification");
        MovingSupportBox box = geometry.localBoxes[i];
        box.box.minimum = Add(box.box.minimum, pose.translation);
        box.box.maximum = Add(box.box.maximum, pose.translation);
        Need(BoxValid(box, limits), "Placed support box exceeds coordinate budget");
        result.boxes.push_back(box);
    }
    return result;
}
MovingSupportResult EstablishMovingSupport(const MovingSupportWorld &platform, CollisionVec3 feet,
                                           double half, double height, const MovingSupportWitness &witness,
                                           const MovingSupportLimits &limits) {
    MovingSupportResult out;
    out.candidateFeet = feet;
    out.geometryComplete = platform.geometryComplete;
    if (!LimitsValid(limits) || !WorldValid(platform, limits) || !RiderValid(feet, half, height, limits) ||
        !std::isfinite(witness.maximumGap) || witness.maximumGap < 0 ||
        witness.maximumGap > limits.maximumContactGap)
        return out;
    if (!witness.contactEstablished) {
        out.status = MovingSupportStatus::NoContactWitness;
        return out;
    }
    if (!platform.geometryComplete && !witness.qualifiedPartialContact) {
        out.status = MovingSupportStatus::PartialGeometry;
        return out;
    }
    if (witness.boxIndex >= platform.boxes.size()) {
        out.status = MovingSupportStatus::WrongSupport;
        return out;
    }
    const MovingSupportBox &box = platform.boxes[witness.boxIndex];
    if (!box.topFaceVerified) {
        out.status = MovingSupportStatus::UnverifiedSupportFace;
        return out;
    }
    if (!witness.queryMask || box.verifiedQueryMask != witness.queryMask) {
        out.status = MovingSupportStatus::UnverifiedMask;
        return out;
    }
    double gap = 0;
    if (!Supported(box.box, feet, half, witness.maximumGap, gap)) {
        out.status = MovingSupportStatus::ContactLost;
        return out;
    }
    out.contact.valid = true;
    out.contact.instanceId = platform.instanceId;
    out.contact.sample = platform.sample;
    out.contact.boxIndex = witness.boxIndex;
    out.contact.sourceShapeOffset = box.box.sourceShapeOffset;
    out.contact.queryMask = witness.queryMask;
    out.contact.localFeet = Sub(feet, platform.translation);
    out.contact.halfWidth = half;
    out.contact.height = height;
    out.contact.gap = gap;
    out.contact.maximumGap = witness.maximumGap;
    out.contact.qualifiedPartialContact = witness.qualifiedPartialContact;
    out.status = MovingSupportStatus::ContactEstablished;
    return out;
}
MovingSupportResult TransportMovingSupport(const MovingSupportWorld &before, const MovingSupportWorld &after,
                                           const MovingSupportContact &contact, CollisionVec3 feet,
                                           bool stillEstablished, double seconds,
                                           const MovingSupportLimits &limits) {
    MovingSupportResult out;
    out.candidateFeet = feet;
    out.geometryComplete = before.geometryComplete && after.geometryComplete;
    if (!LimitsValid(limits) || !WorldValid(before, limits) || !WorldValid(after, limits) || !contact.valid ||
        !RiderValid(feet, contact.halfWidth, contact.height, limits) ||
        !Finite(contact.localFeet, limits.coordinateGameUnits) || !std::isfinite(contact.gap) ||
        !std::isfinite(contact.maximumGap) || contact.maximumGap < 0 ||
        contact.maximumGap > limits.maximumContactGap || !std::isfinite(seconds) || seconds <= 0 ||
        seconds > limits.maximumElapsedSeconds)
        return out;
    if (!stillEstablished) {
        out.status = MovingSupportStatus::NoContactWitness;
        return out;
    }
    if (before.instanceId != after.instanceId || contact.instanceId != before.instanceId ||
        contact.boxIndex >= before.boxes.size() || contact.boxIndex >= after.boxes.size()) {
        out.status = MovingSupportStatus::WrongSupport;
        return out;
    }
    if (contact.sample != before.sample || after.sample <= before.sample) {
        out.status = MovingSupportStatus::StaleSample;
        return out;
    }
    if (!out.geometryComplete && !contact.qualifiedPartialContact) {
        out.status = MovingSupportStatus::PartialGeometry;
        return out;
    }
    const CollisionVec3 delta = Sub(after.translation, before.translation);
    const double distance = Length(delta);
    if (!std::isfinite(distance) || distance > limits.maximumMotion ||
        distance / seconds > limits.maximumSpeed) {
        out.status = MovingSupportStatus::ExcessiveMotion;
        return out;
    }
    if (!SameGeometry(before, after, delta)) {
        out.status = MovingSupportStatus::ChangedGeometry;
        return out;
    }
    const MovingSupportBox &box = before.boxes[contact.boxIndex];
    if (box.box.sourceShapeOffset != contact.sourceShapeOffset) {
        out.status = MovingSupportStatus::WrongSupport;
        return out;
    }
    if (!box.topFaceVerified) {
        out.status = MovingSupportStatus::UnverifiedSupportFace;
        return out;
    }
    if (!contact.queryMask || box.verifiedQueryMask != contact.queryMask) {
        out.status = MovingSupportStatus::UnverifiedMask;
        return out;
    }
    double oldGap = 0, anchorGap = 0, newGap = 0;
    if (!Supported(box.box, Add(contact.localFeet, before.translation), contact.halfWidth, contact.maximumGap,
                   anchorGap) ||
        std::abs(anchorGap - contact.gap) > 1e-6 ||
        !Supported(box.box, feet, contact.halfWidth, contact.maximumGap, oldGap) ||
        std::abs(oldGap - contact.gap) > 1e-6) {
        out.status = MovingSupportStatus::ContactLost;
        return out;
    }
    const CollisionVec3 candidate = Add(feet, delta);
    if (!RiderValid(candidate, contact.halfWidth, contact.height, limits))
        return out;
    if (!Supported(after.boxes[contact.boxIndex].box, candidate, contact.halfWidth, contact.maximumGap,
                   newGap) ||
        std::abs(newGap - oldGap) > 1e-6) {
        out.status = MovingSupportStatus::ContactLost;
        return out;
    }
    out.contact = contact;
    out.contact.sample = after.sample;
    out.contact.localFeet = Sub(candidate, after.translation);
    out.candidateFeet = candidate;
    out.platformDelta = delta;
    out.hasCandidate = true;
    out.status = MovingSupportStatus::CandidateOnly;
    return out;
}
} // namespace wolf2009

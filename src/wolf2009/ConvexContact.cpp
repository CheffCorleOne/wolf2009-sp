// SPDX-License-Identifier: GPL-3.0-or-later
// Independent elementary convex mathematics; no proprietary physics code.
#include "ConvexContact.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
double Dot(CollisionVec3 a, CollisionVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CollisionVec3 Add(CollisionVec3 a, CollisionVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
CollisionVec3 Sub(CollisionVec3 a, CollisionVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CollisionVec3 Mul(CollisionVec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
CollisionVec3 Cross(CollisionVec3 a, CollisionVec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool Finite(CollisionVec3 v, double m) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= m &&
           std::abs(v.y) <= m && std::abs(v.z) <= m;
}
double Sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
struct P {
    double x, y;
};
double Cross2(P a, P b, P c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
// Membership in the convex polygon made from the original face vertices is
// a constructive witness. Membership in all offset halfspaces alone is not.
bool InsideFace(const ConvexContactHull &h, const ConvexContactPlane &plane, CollisionVec3 q,
                const ConvexContactLimits &l) {
    CollisionVec3 axis = std::abs(plane.normal.x) < .8 ? CollisionVec3(1, 0, 0) : CollisionVec3(0, 1, 0);
    CollisionVec3 u = Cross(plane.normal, axis);
    u = Mul(u, 1 / std::sqrt(Dot(u, u)));
    const CollisionVec3 v = Cross(plane.normal, u);
    std::vector<P> points;
    for (std::size_t i = 0; i < h.vertices.size(); ++i)
        if (std::abs(Dot(plane.normal, h.vertices[i]) + plane.distance) <= l.geometryTolerance)
            points.push_back({Dot(u, h.vertices[i]), Dot(v, h.vertices[i])});
    if (points.size() < 3)
        return false;
    std::sort(points.begin(), points.end(),
              [](P a, P b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    points.erase(
        std::unique(points.begin(), points.end(), [](P a, P b) { return a.x == b.x && a.y == b.y; }),
        points.end());
    if (points.size() < 3)
        return false;
    std::vector<P> poly;
    for (std::size_t i = 0; i < points.size(); ++i) {
        while (poly.size() >= 2 && Cross2(poly[poly.size() - 2], poly.back(), points[i]) <= 0)
            poly.pop_back();
        poly.push_back(points[i]);
    }
    const std::size_t lower = poly.size();
    for (std::size_t i = points.size() - 1; i > 0; --i) {
        const P p = points[i - 1];
        while (poly.size() > lower && Cross2(poly[poly.size() - 2], poly.back(), p) <= 0)
            poly.pop_back();
        poly.push_back(p);
    }
    if (poly.size() < 4)
        return false;
    poly.pop_back();
    const P p = {Dot(u, q), Dot(v, q)};
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const P a = poly[i], b = poly[(i + 1) % poly.size()];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (length <= l.geometryTolerance || Cross2(a, b, p) <= l.faceInteriorMargin * length)
            return false;
    }
    return true;
}
} // namespace
ConvexContactResult SweepConvexContact(const ConvexContactHull &h,
                                       const ConvexContactQualification &qualification,
                                       CollisionVec3 translation, CollisionVec3 center,
                                       CollisionVec3 half, CollisionVec3 delta, std::uint32_t mask,
                                       bool includeRadius, const ConvexContactLimits &l) {
    Need(std::isfinite(l.coordinate) && l.coordinate > 0 && std::isfinite(l.geometryTolerance) &&
             l.geometryTolerance > 0 && std::isfinite(l.faceInteriorMargin) &&
             l.faceInteriorMargin >= l.geometryTolerance,
         "Invalid convex contact limits");
    Need(h.vertices.size() >= 4 && h.vertices.size() <= l.vertices && h.planes.size() >= 4 &&
             h.planes.size() <= l.planes,
         "Convex contact array budget exceeded");
    Need(std::isfinite(h.radius) && h.radius >= 0 && h.radius <= l.coordinate, "Invalid convex radius");
    Need(Finite(translation, l.coordinate) && Finite(center, l.coordinate) &&
             Finite(delta, l.coordinate) && Finite(half, l.coordinate) && half.x >= 0 && half.y >= 0 &&
             half.z >= 0,
         "Invalid convex query coordinates");
    for (std::size_t i = 0; i < h.vertices.size(); ++i)
        Need(Finite(h.vertices[i], l.coordinate), "Invalid convex vertex");
    std::vector<ConvexContactPlane> planes = h.planes;
    for (std::size_t i = 0; i < planes.size(); ++i) {
        ConvexContactPlane &p = planes[i];
        Need(Finite(p.normal, 2) && std::isfinite(p.distance) && std::abs(p.distance) <= l.coordinate,
             "Invalid convex plane");
        const double length = std::sqrt(Dot(p.normal, p.normal));
        Need(std::abs(length - 1) <= .00001, "Convex plane normal is not unit length");
        p.normal = Mul(p.normal, 1 / length);
        p.distance /= length;
        for (std::size_t j = 0; j < h.vertices.size(); ++j)
            Need(Dot(p.normal, h.vertices[j]) + p.distance <= l.geometryTolerance,
                 "Convex vertex outside source plane");
    }
    ConvexContactResult out;
    out.sourceShapeOffset = h.sourceShapeOffset;
    if (qualification.instanceId.empty() || qualification.instanceId.size() > 4096 || !mask ||
        qualification.verifiedQueryMask != mask || !qualification.sourceShapeOffset ||
        qualification.sourceShapeOffset != h.sourceShapeOffset || !qualification.sourceRegion ||
        qualification.sourceRegion != h.sourceRegion || qualification.planeIndex >= planes.size())
        return out;
    const CollisionVec3 start = Sub(center, translation);
    Need(Finite(start, l.coordinate), "Convex local query exceeds budget");
    const double radius = includeRadius ? h.radius : 0;
    double enter = 0, leave = 1;
    std::size_t face = planes.size();
    bool initial = true;
    for (std::size_t i = 0; i < planes.size(); ++i) {
        const CollisionVec3 n = planes[i].normal;
        const double support =
            std::abs(n.x) * half.x + std::abs(n.y) * half.y + std::abs(n.z) * half.z + radius;
        const double distance = Dot(n, start) + planes[i].distance - support, velocity = Dot(n, delta);
        if (distance > l.geometryTolerance)
            initial = false;
        if (std::abs(velocity) < 1e-12) {
            if (distance > l.geometryTolerance) {
                out.status = ConvexContactStatus::NoHitForThisHull;
                return out;
            }
            continue;
        }
        const double t = -distance / velocity;
        if (velocity < 0) {
            if (t > enter || (face == planes.size() && t >= -l.geometryTolerance)) {
                enter = std::max(0.0, t);
                face = i;
            }
        } else
            leave = std::min(leave, t);
        if (enter > leave + l.geometryTolerance) {
            out.status = ConvexContactStatus::NoHitForThisHull;
            return out;
        }
    }
    if (initial) {
        out.status = ConvexContactStatus::IndeterminateInitialOverlap;
        return out;
    }
    if (face == planes.size() || enter > 1 || leave < 0 || enter > leave) {
        out.status = ConvexContactStatus::NoHitForThisHull;
        return out;
    }
    if (face != qualification.planeIndex)
        return out;
    out.status = ConvexContactStatus::IndeterminateEdge;
    out.fraction = enter;
    const CollisionVec3 n = planes[face].normal, localCenter = Add(start, Mul(delta, enter));
    const CollisionVec3 support(Sign(n.x) * half.x, Sign(n.y) * half.y, Sign(n.z) * half.z);
    const CollisionVec3 q = Sub(Sub(localCenter, support), Mul(n, radius));
    if (std::abs(Dot(n, q) + planes[face].distance) > l.geometryTolerance ||
        !InsideFace(h, planes[face], q, l))
        return out;
    out.status = ConvexContactStatus::ProvenFaceHit;
    out.normal = n;
    out.center = Add(center, Mul(delta, enter));
    out.hullContact = Add(q, translation);
    return out;
}
} // namespace wolf2009

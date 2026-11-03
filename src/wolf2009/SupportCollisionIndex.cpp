// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupportCollisionIndex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool value, const char *reason) { if (!value) throw std::runtime_error(reason); }
double Get(CollisionVec3 p, int k) { return k == 0 ? p.x : k == 1 ? p.y : p.z; }
void Set(CollisionVec3 &p, int k, double v) { if (k == 0) p.x = v; else if (k == 1) p.y = v; else p.z = v; }
struct Bounds { CollisionVec3 lo, hi; };
Bounds Union(const Bounds &a, const Bounds &b) {
    Bounds out;
    for (int k = 0; k < 3; ++k) {
        Set(out.lo,k,std::min(Get(a.lo,k),Get(b.lo,k)));
        Set(out.hi,k,std::max(Get(a.hi,k),Get(b.hi,k)));
    }
    return out;
}
bool Intersects(const Bounds &a, const Bounds &b) {
    for (int k = 0; k < 3; ++k)
        if (Get(a.lo,k) > Get(b.hi,k) || Get(a.hi,k) < Get(b.lo,k)) return false;
    return true;
}
bool Finite(const Bounds &a) {
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(Get(a.lo,k)) || !std::isfinite(Get(a.hi,k)) || Get(a.lo,k) > Get(a.hi,k)) return false;
    return true;
}
bool SweptBounds(const RoundedConvexShape &shape, const CollisionRigidTransform &from,
                 const CollisionRigidTransform &to, const RoundedConvexLimits &limits, Bounds &out) {
    const auto motion = RoundedConvexMotionBounds(shape,from,to,limits);
    if (!motion.valid) return false;
    out = {motion.minimum,motion.maximum};
    return Finite(out);
}
void Budget(std::size_t &used, std::size_t count, std::size_t width, std::size_t limit) {
    Need(width && used <= limit && count <= (limit-used)/width,"Support index memory budget exceeded");
    used += count*width;
}
} // namespace
struct SupportCollisionIndex::Impl {
    struct Shape {
        RoundedConvexShape physical;
        CollisionRigidTransform pose;
        SupportCollisionIdentity source;
        SupportCollisionFilter filter;
        SupportCollisionKind kind;
        HavokCylinderGeometryMetadata cylinder;
        bool response = false;
        Bounds bounds;
    };
    struct Node { Bounds bounds; std::size_t begin = 0, end = 0, front = 0, back = 0; bool leaf = true; };
    std::vector<Shape> shapes;
    std::vector<std::size_t> order;
    std::vector<Node> nodes;
    SupportCollisionIndexLimits limits;
    bool sourceComplete;
    explicit Impl(const SupportCollisionWorld &world, const SupportCollisionIndexLimits &l)
        : limits(l), sourceComplete(world.selectedGeometryComplete && world.selectedHierarchyComplete) {
        Need(l.leafShapes > 0 && l.depth > 0 && l.depth <= 64 && l.shapes > 0 && l.corePoints > 0 && l.bytes > 0,
             "Invalid support index limits");
        Need(world.shapes.size() <= l.shapes,"Support index shape budget exceeded");
        std::size_t used = 0, points = 0;
        Budget(used,world.shapes.size(),sizeof(Shape)+sizeof(std::size_t)+2*sizeof(Node),l.bytes);
        for (const auto &source : world.shapes) {
            Need(points <= l.corePoints && source.corePoints.size() <= l.corePoints-points,
                 "Support index core-point budget exceeded");
            points += source.corePoints.size();
            Budget(used,source.corePoints.size(),sizeof(CollisionVec3),l.bytes);
        }
        shapes.reserve(world.shapes.size()); order.resize(world.shapes.size());
        std::iota(order.begin(),order.end(),std::size_t(0));
        for (const auto &source : world.shapes) {
            Shape shape;
            shape.physical.corePoints = source.corePoints; shape.physical.radius = source.radius;
            shape.pose = source.placement; shape.source = source.source; shape.filter = source.filter;
            shape.kind = source.kind; shape.cylinder = source.cylinder;
            shape.response = source.kind != SupportCollisionKind::Cylinder &&
                (source.kind != SupportCollisionKind::Triangle || source.triangle.contactMetadataQualified);
            const auto bounds = RoundedConvexBounds(shape.physical,shape.pose,l.geometry);
            Need(bounds.valid,"Support index shape/pose is invalid for the canonical solver");
            shape.bounds = {bounds.minimum,bounds.maximum}; shapes.push_back(std::move(shape));
        }
        nodes.reserve(world.shapes.size()*2);
        if (!shapes.empty()) Build(0,shapes.size(),0);
    }
    std::size_t Build(std::size_t begin, std::size_t end, std::size_t depth) {
        const auto id = nodes.size(); nodes.push_back(Node());
        Bounds bounds = shapes[order[begin]].bounds;
        for (std::size_t i = begin+1; i < end; ++i) bounds = Union(bounds,shapes[order[i]].bounds);
        nodes[id].bounds=bounds; nodes[id].begin=begin; nodes[id].end=end;
        if (end-begin <= limits.leafShapes || depth+1 >= limits.depth) return id;
        int axis=0;
        for (int k=1;k<3;++k)
            if (Get(bounds.hi,k)-Get(bounds.lo,k) > Get(bounds.hi,axis)-Get(bounds.lo,axis)) axis=k;
        const auto middle=begin+(end-begin)/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](std::size_t a,std::size_t b) {
            const double ca=.5*Get(shapes[a].bounds.lo,axis)+.5*Get(shapes[a].bounds.hi,axis);
            const double cb=.5*Get(shapes[b].bounds.lo,axis)+.5*Get(shapes[b].bounds.hi,axis);
            return ca < cb || (ca == cb && a < b);
        });
        const auto front=Build(begin,middle,depth+1), back=Build(middle,end,depth+1);
        nodes[id].leaf=false; nodes[id].front=front; nodes[id].back=back; return id;
    }
    SupportCollisionQueryResult Query(const RoundedConvexShape &actor, const CollisionRigidTransform &from,
        const CollisionRigidTransform &to, const SupportCollisionQueryFilter &filter,
        const SupportCollisionQueryLimits &q, bool overlap) const {
        SupportCollisionQueryResult out;
        if (!q.candidates || (q.clearancePolicy != SupportClearancePolicy::Strict &&
                             q.clearancePolicy != SupportClearancePolicy::NonBlockingPhysical)) return out;
        Bounds bounds;
        if (overlap) {
            const auto physical = RoundedConvexBounds(actor,from,q.geometry);
            if (!physical.valid) return out;
            bounds = {physical.minimum,physical.maximum};
        } else if (!SweptBounds(actor,from,to,q.geometry,bounds)) return out;
        std::vector<std::size_t> stack;
        if (!nodes.empty()) stack.push_back(0);
        bool exceeded=false;
        while (!stack.empty() && !exceeded) {
            const auto index=stack.back(); stack.pop_back(); const auto &node=nodes[index];
            if (!Intersects(bounds,node.bounds)) continue;
            if (!node.leaf) { stack.push_back(node.back); stack.push_back(node.front); continue; }
            for (std::size_t i=node.begin;i<node.end;++i) {
                const auto shapeIndex=order[i]; const auto &shape=shapes[shapeIndex];
                if (!Intersects(bounds,shape.bounds)) continue;
                const auto choice=filter.Select(shape.source,shape.filter);
                if (choice == SupportFilterDecision::Exclude) { ++out.excluded; continue; }
                if (choice != SupportFilterDecision::Include) { ++out.unknownFilters; continue; }
                if (out.tested == q.candidates) { exceeded=true; break; }
                ++out.tested;
                const auto result=overlap ? OverlapRoundedConvex(actor,from,shape.physical,shape.pose,q.geometry) :
                    CastRoundedConvex(actor,from,to,shape.physical,shape.pose,shape.pose,q.geometry);
                if (result.status == RoundedConvexStatus::Invalid) { out.status=SupportCollisionQueryStatus::Invalid; return out; }
                if (!overlap && q.clearancePolicy == SupportClearancePolicy::NonBlockingPhysical &&
                    q.geometry.targetSeparationGameUnits > 0 &&
                    (result.status == RoundedConvexStatus::Indeterminate ||
                     (result.status == RoundedConvexStatus::InitialOverlap &&
                      result.reason == RoundedConvexReason::WithinTargetSeparation))) {
                    auto physicalLimits = q.geometry;
                    physicalLimits.targetSeparationGameUnits = 0;
                    ++out.physicalSeparationRetries;
                    const auto physical = CastRoundedConvex(actor,from,to,shape.physical,shape.pose,shape.pose,physicalLimits);
                    // An uncertain skin cast is not evidence. Only the new,
                    // independently bounded physical trajectory can qualify.
                    if (physical.status == RoundedConvexStatus::Separated &&
                        physical.reason == RoundedConvexReason::SeparationCertificate &&
                        physical.separationLowerBoundGameUnits > 0) {
                        ++out.physicalSeparationFallbacks;
                        continue;
                    }
                }
                if (result.status == RoundedConvexStatus::Indeterminate) {
                    if (!out.uncertainQueries) { out.firstUncertain=result; out.firstUncertainShapeIndex=shapeIndex; }
                    ++out.uncertainQueries; continue;
                }
                if (result.status == RoundedConvexStatus::Separated) continue;
                if (result.status != RoundedConvexStatus::Hit && result.status != RoundedConvexStatus::InitialOverlap) {
                    ++out.uncertainQueries; continue;
                }
                const double fraction=result.status == RoundedConvexStatus::InitialOverlap ? 0 : result.fraction;
                if (!std::isfinite(fraction) || fraction < 0 || fraction > 1) { ++out.uncertainQueries; continue; }
                const double previous=out.candidate.status == RoundedConvexStatus::InitialOverlap ? 0 : out.candidate.fraction;
                if (!out.hasCandidate || fraction < previous || (fraction == previous && shapeIndex < out.shapeIndex)) {
                    out.hasCandidate=true; out.candidate=result; out.source=shape.source;
                    out.shapeIndex=shapeIndex; out.responseMetadataQualified=shape.response;
                    out.sourceKind=shape.kind; out.cylinder=shape.cylinder;
                }
            }
        }
        if (!sourceComplete) out.status=SupportCollisionQueryStatus::IncompleteSource;
        else if (exceeded || out.unknownFilters || out.uncertainQueries) out.status=SupportCollisionQueryStatus::Indeterminate;
        else {
            out.selectedBodyQueryComplete=true;
            if (!out.hasCandidate) out.status=SupportCollisionQueryStatus::Separated;
            else if (out.candidate.status == RoundedConvexStatus::InitialOverlap) out.status=SupportCollisionQueryStatus::InitialOverlap;
            else out.status=SupportCollisionQueryStatus::Hit;
        }
        return out;
    }
};
SupportCollisionIndex::SupportCollisionIndex(const SupportCollisionWorld &world,const SupportCollisionIndexLimits &limits)
    : impl_(new Impl(world,limits)) {}
SupportCollisionIndex::~SupportCollisionIndex() = default;
SupportCollisionIndex::SupportCollisionIndex(SupportCollisionIndex &&) noexcept = default;
SupportCollisionIndex &SupportCollisionIndex::operator=(SupportCollisionIndex &&) noexcept = default;
std::size_t SupportCollisionIndex::ShapeCount() const { return impl_ ? impl_->shapes.size() : 0; }
SupportCollisionQueryResult SupportCollisionIndex::Cast(const RoundedConvexShape &actor,
    const CollisionRigidTransform &from,const CollisionRigidTransform &to,const SupportCollisionQueryFilter &filter,
    const SupportCollisionQueryLimits &limits) const {
    return impl_ ? impl_->Query(actor,from,to,filter,limits,false) : SupportCollisionQueryResult();
}
SupportCollisionQueryResult SupportCollisionIndex::Overlap(const RoundedConvexShape &actor,
    const CollisionRigidTransform &pose,const SupportCollisionQueryFilter &filter,
    const SupportCollisionQueryLimits &limits) const {
    return impl_ ? impl_->Query(actor,pose,pose,filter,limits,true) : SupportCollisionQueryResult();
}
} // namespace wolf2009

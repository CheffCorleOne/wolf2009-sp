// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored checked leaf-format reader; no Havok SDK/runtime.
#include "HavokPrimitiveSupport.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#ifdef __FAST_MATH__
#error "HavokPrimitiveSupport requires strict IEEE floating-point semantics"
#endif

namespace wolf2009 {
namespace {
void Need(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
double Float(std::uint32_t bits) {
    float value;
    static_assert(sizeof(value) == 4 && std::numeric_limits<float>::is_iec559,
                  "Primitive support requires IEEE32 float");
    std::memcpy(&value, &bits, 4);
    return value;
}
std::uint32_t HeaderWord(const std::array<unsigned char,96>& raw,std::size_t at) {
    Need(at<=92,"Cylinder raw field exceeds header"); std::uint32_t word=0;
    for (int k=0;k<4;++k) word|=std::uint32_t(raw[at+k])<<(8*k);
    return word;
}
float Rounded(float value) { volatile float result=value; return result; }
float Product(float a,float b) { volatile float result=a*b; return result; }
float Sum(float a,float b) { volatile float result=a+b; return result; }
void CheckProfile(HavokCylinderGeometryProfile profile) {
    Need(profile==HavokCylinderGeometryProfile::Unresolved ||
         profile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope,
         "Unknown cylinder geometry interpretation profile");
}
std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) {
    for (int k = 0; k < 8; ++k) {
        hash ^= static_cast<unsigned char>(value >> (k * 8));
        hash *= 1099511628211ull;
    }
    return hash;
}
struct CheckedShape {
    const char *type = NULL, *reason = "";
    std::size_t headerBytes = 0, points = 0;
    CollisionVec3 core[32];
    std::array<std::uint16_t,32> virtualVertexIds{};
    HavokCylinderGeometryMetadata cylinder;
    std::uint32_t radiusBits = 0;
    double radius = 0;
    bool radiusFinite = false, resolved = false;
};
struct Reader : detail::HavokPack {
    const HavokPrimitiveSupportLimits &limits;
    HavokCylinderGeometryProfile cylinderProfile;
    HavokPrimitiveSupportDocument result;
    Reader(const std::vector<unsigned char> &bytes, const HavokPrimitiveSupportLimits &l,
           HavokCylinderGeometryProfile profile)
        : HavokPack(bytes, l.sourceBytes, l.objects, l.fixups), limits(l), cylinderProfile(profile) {
        result.cylinderProfile=profile;
        result.sourceFingerprint = 14695981039346656037ull;
        for (const auto byte : bytes) {
            result.sourceFingerprint ^= byte;
            result.sourceFingerprint *= 1099511628211ull;
        }
        result.sourceBytes = bytes.size();
    }
    void Budget(std::size_t count, std::size_t width) {
        Need(width && result.decodedBytes <= limits.decodedBytes &&
                 count <= (limits.decodedBytes - result.decodedBytes) / width,
             "Primitive support decoded-byte budget exceeded");
        result.decodedBytes += count * width;
    }
    CollisionVec3 Point(std::size_t at) const {
        const CollisionVec3 p(F(at) / .0254, F(at + 4) / .0254, F(at + 8) / .0254);
        Need(std::abs(p.x) <= limits.coordinateGameUnits &&
                 std::abs(p.y) <= limits.coordinateGameUnits &&
                 std::abs(p.z) <= limits.coordinateGameUnits,
             "Primitive support coordinate budget exceeded");
        return p;
    }
    CheckedShape Check(const HavokPrimitiveSelection &selected) const {
        CheckedShape shape;
        switch (selected.kind) {
        case HavokPrimitiveKind::Sphere:
            shape.type = "hkpSphereShape"; shape.headerBytes = 32; shape.points = 1;
            shape.resolved = true;
            break;
        case HavokPrimitiveKind::Capsule:
            shape.type = "hkpCapsuleShape"; shape.headerBytes = 64; shape.points = 2;
            shape.resolved = true;
            break;
        case HavokPrimitiveKind::Cylinder:
            shape.type = "hkpCylinderShape"; shape.headerBytes = 96;
            shape.reason = "unresolved-cylinder-support-profile";
            break;
        default:
            throw std::runtime_error("Unknown primitive support selection kind");
        }
        Need(Type(selected.shapeOffset) == shape.type,
             "Primitive support selection has a different exact object type");
        const std::size_t at = selected.shapeOffset;
        DataRange(at, shape.headerBytes);
        shape.radiusBits = DU(at + 16);
        const auto radius = Float(shape.radiusBits);
        shape.radiusFinite = std::isfinite(radius);
        if (selected.kind==HavokPrimitiveKind::Cylinder) {
            shape.cylinder.rawCylinderRadiusBits=DU(at+20);
            shape.cylinder.rawBaseRadiusFactorBits=DU(at+24);
            if (cylinderProfile!=HavokCylinderGeometryProfile::Unresolved) {
                std::array<unsigned char,96> raw;
                std::copy(bytes.begin()+sections[2].absolute+at,
                          bytes.begin()+sections[2].absolute+at+96,raw.begin());
                const auto generated=BuildHavokCylinderVirtualCore(raw,cylinderProfile,limits);
                shape.points=32; shape.resolved=true; shape.radius=generated.radiusGameUnits;
                shape.cylinder=generated.metadata; shape.virtualVertexIds=generated.vertexIds;
                std::copy(generated.corePoints.begin(),generated.corePoints.end(),shape.core);
                shape.reason="retail-default16-virtual-envelope-not-native-selection";
                return shape;
            }
        }
        if (!shape.resolved) return shape;
        Need(shape.radiusFinite, "Nonfinite primitive support radius");
        shape.radius = radius / .0254;
        Need(shape.radius >= 0 && shape.radius <= limits.maximumRadiusGameUnits,
             "Primitive support radius budget exceeded");
        if (selected.kind == HavokPrimitiveKind::Capsule) {
            shape.core[0] = Point(at + 32);
            shape.core[1] = Point(at + 48);
        }
        return shape;
    }
    HavokPrimitiveSupportDocument Decode(const std::vector<HavokPrimitiveSelection> &selection) {
        Need(selection.size() <= limits.shapes, "Primitive support selection budget exceeded");
        std::set<std::uint32_t> seen;
        // Check geometry and every retained allocation size before reserving
        // output storage. Temporary uniqueness bookkeeping is bounded by shapes.
        for (const auto &selected : selection) {
            Need(seen.insert(selected.shapeOffset).second,
                 "Duplicate primitive support selection offset");
            const auto checked = Check(selected);
            Budget(1, sizeof(HavokPrimitiveSupport));
            Budget(checked.points, sizeof(CollisionVec3));
            if (selected.kind==HavokPrimitiveKind::Cylinder && checked.resolved)
                Budget(32,sizeof(std::uint16_t));
            Budget(std::strlen(checked.type) + 1, 1);
            Budget(std::strlen(checked.reason) + 1, 1);
        }
        result.shapes.reserve(selection.size());
        for (const auto &selected : selection) {
            const auto checked = Check(selected);
            HavokPrimitiveSupport shape;
            shape.kind = selected.kind;
            shape.status = checked.resolved ? HavokPrimitiveSupportStatus::Resolved
                                            : HavokPrimitiveSupportStatus::Unresolved;
            shape.sourceShapeOffset = selected.shapeOffset;
            shape.sourceRegion = Mix(Mix(result.sourceFingerprint, selected.shapeOffset),
                                     static_cast<std::uint64_t>(selected.kind));
            if (!shape.sourceRegion) shape.sourceRegion = 1;
            shape.sourceType = checked.type;
            shape.reason = checked.reason;
            shape.rawHeaderBytes = checked.headerBytes;
            const auto at = sections[2].absolute + std::size_t(selected.shapeOffset);
            std::copy(bytes.begin() + at, bytes.begin() + at + checked.headerBytes,
                      shape.rawHeader.begin());
            shape.rawRadiusBits = checked.radiusBits;
            shape.radiusFinite = checked.radiusFinite;
            shape.radiusKnown = checked.resolved;
            shape.radiusGameUnits = checked.radius;
            shape.corePoints.assign(checked.core, checked.core + checked.points);
            shape.cylinder=checked.cylinder;
            if (selected.kind==HavokPrimitiveKind::Cylinder && checked.resolved)
                shape.virtualVertexIds.assign(checked.virtualVertexIds.begin(),checked.virtualVertexIds.end());
            result.shapes.push_back(std::move(shape));
        }
        return std::move(result);
    }
};
} // namespace
HavokCylinderVirtualCore BuildHavokCylinderVirtualCore(const std::array<unsigned char,96>& raw,
    HavokCylinderGeometryProfile profile,const HavokPrimitiveSupportLimits& limits) {
    Need(profile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope,
         "Cylinder geometry requires the explicit verified virtual-envelope profile");
    Need(std::isfinite(limits.coordinateGameUnits) && limits.coordinateGameUnits>0 &&
         std::isfinite(limits.maximumRadiusGameUnits) && limits.maximumRadiusGameUnits>=0,
         "Invalid cylinder virtual-core limits");
    HavokCylinderVirtualCore result;
    auto& metadata=result.metadata; metadata.profile=profile;
    metadata.rawCylinderRadiusBits=HeaderWord(raw,20); metadata.rawBaseRadiusFactorBits=HeaderWord(raw,24);
    metadata.quantizationKBits=0x402bfe8du; metadata.virtualVertexStepBits=0x3ebe8495u;
    metadata.observedRoundThresholdBits=0x3efae14eu; metadata.hasRuntimeThresholdObservation=true;
    metadata.matchesNativeSupportSelection=false;
    const double radius=Float(HeaderWord(raw,16));
    const float cylRadius=static_cast<float>(Float(metadata.rawCylinderRadiusBits));
    Need(std::isfinite(radius) && radius>=0 && radius/.0254<=limits.maximumRadiusGameUnits &&
         std::isfinite(cylRadius) && cylRadius>=0,"Invalid cylinder radius");
    result.radiusGameUnits=radius/.0254;
    float a[3],b[3],p1[3],p2[3];
    for (int k=0;k<3;++k) {
        a[k]=static_cast<float>(Float(HeaderWord(raw,32+k*4)));
        b[k]=static_cast<float>(Float(HeaderWord(raw,48+k*4)));
        p1[k]=static_cast<float>(Float(HeaderWord(raw,64+k*4)));
        p2[k]=static_cast<float>(Float(HeaderWord(raw,80+k*4)));
        Need(std::isfinite(a[k]) && std::isfinite(b[k]) && std::isfinite(p1[k]) && std::isfinite(p2[k]),
             "Nonfinite cylinder endpoint/perpendicular XYZ");
    }
    const float step=static_cast<float>(Float(metadata.virtualVertexStepBits));
    for (std::size_t n=0;n<32;++n) {
        const auto id=static_cast<std::uint16_t>((n/2)*16+n%2); result.vertexIds[n]=id;
        const float t=Rounded(static_cast<float>((double(id&15)+.5)*double(step)));
        const float squareRemainder=Rounded(static_cast<float>(1-double(t)*double(t)));
        Need(squareRemainder>=0,"Cylinder virtual profile has an invalid square root");
        const float u=Rounded(static_cast<float>(std::sqrt(double(squareRemainder))));
        float c1=id&16?u:t,c2=id&16?t:u;
        if (!(id&32)) c1=Rounded(-c1);
        if (!(id&64)) c2=Rounded(-c2);
        double xyz[3];
        for (int k=0;k<3;++k) {
            const float mixed=Sum(Product(p1[k],c1),Product(p2[k],c2));
            const float point=Sum(Product(mixed,cylRadius),id&128?a[k]:b[k]);
            xyz[k]=double(point)/.0254;
            Need(std::isfinite(xyz[k]) && std::abs(xyz[k])<=limits.coordinateGameUnits,
                 "Cylinder virtual point exceeds coordinate budget");
        }
        result.corePoints[n]={xyz[0],xyz[1],xyz[2]};
    }
    return result;
}
HavokPrimitiveSupportDocument DecodeHavokPrimitiveSupport(
    const std::vector<unsigned char> &hkxs, const std::vector<HavokPrimitiveSelection> &selection,
    const HavokPrimitiveSupportLimits &limits) {
    return DecodeHavokPrimitiveSupport(hkxs,selection,HavokCylinderGeometryProfile::Unresolved,limits);
}
HavokPrimitiveSupportDocument DecodeHavokPrimitiveSupport(
    const std::vector<unsigned char>& hkxs,const std::vector<HavokPrimitiveSelection>& selection,
    HavokCylinderGeometryProfile profile,const HavokPrimitiveSupportLimits& limits) {
    CheckProfile(profile);
    Need(limits.sourceBytes >= 208 && limits.objects && limits.fixups && limits.shapes &&
             limits.decodedBytes && std::isfinite(limits.coordinateGameUnits) &&
             limits.coordinateGameUnits > 0 && std::isfinite(limits.maximumRadiusGameUnits) &&
             limits.maximumRadiusGameUnits >= 0,
         "Invalid primitive support limits");
    return Reader(hkxs, limits,profile).Decode(selection);
}
} // namespace wolf2009

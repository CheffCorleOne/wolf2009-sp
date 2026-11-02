// SPDX-License-Identifier: GPL-3.0-or-later
#include "DoorAnimation.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wolf2009 {
namespace {
using detail::HavokPack;
void Require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
const char *const DoorSkeleton = "models/props/train_door/train_door_anim_skel.hkx";
const char *const DoorClip = "models/props/train_door/srp_tyd_nml_train_door_anim.hkx";
const char *const IdleClip = "models/props/train_door/train_door_pose.hkx";
const char *const EndClip = "models/props/train_door/train_door_end.hkx";
std::array<double, 4> Normalize(std::array<double, 4> q) {
    double n = 0;
    for (double v : q) {
        Require(std::isfinite(v), "Nonfinite door quaternion");
        n += v * v;
    }
    Require(n > .999 && n < 1.001, "Door quaternion is not unit length");
    for (double &v : q)
        v /= std::sqrt(n);
    return q;
}
std::array<double, 3> Rotate(const std::array<double, 4> &q, const std::array<double, 3> &p) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const std::array<double, 3> t = {
        {2 * (y * p[2] - z * p[1]), 2 * (z * p[0] - x * p[2]), 2 * (x * p[1] - y * p[0])}};
    return {{p[0] + w * t[0] + y * t[2] - z * t[1], p[1] + w * t[1] + z * t[0] - x * t[2],
             p[2] + w * t[2] + x * t[1] - y * t[0]}};
}

std::uint32_t Variant(const HavokPack &p, const char *name, const char *type) {
    const auto root = p.U(28);
    const auto count = p.DU(root + 4);
    Require(count > 0 && count <= 64, "Unexpected root variant count");
    const auto array = p.Pointer(root);
    p.ArrayRange(array, count, 16);
    std::uint32_t found = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto slot = array + 16 * i;
        if (p.DataString(p.Pointer(slot)) != name)
            continue;
        const auto object = p.Pointer(slot + 8);
        Require(!found && object && p.DataString(p.Pointer(slot + 4)) == type && p.Type(object) == type,
                "Ambiguous or mistyped animation variant");
        found = object;
    }
    Require(found != 0, "Missing named animation variant");
    return found;
}
DoorJointPose VerifySkeleton(const HavokPack &pack) {
    const auto container = Variant(pack, "Animation Container", "hkaAnimationContainer");
    const auto count = pack.DU(container + 4), array = pack.Pointer(container);
    Require(count > 0 && count <= 4096, "Skeleton table exceeds limit");
    pack.ArrayRange(array, count, 4);
    bool found = false;
    DoorJointPose pose;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto object = pack.Pointer(array + i * 4);
        Require(pack.Type(object) == "hkaSkeleton", "Unexpected skeleton object");
        if (pack.DataString(pack.Pointer(object)) != DoorSkeleton)
            continue;
        Require(!found && pack.DU(object + 8) == 2 && pack.DU(object + 16) == 2 &&
                    pack.DU(object + 24) == 2,
                "Invalid two-bone door skeleton");
        const auto parents = pack.Pointer(object + 4), bones = pack.Pointer(object + 12);
        Require(pack.DU(parents) == 0x0000ffff, "Unexpected door joint hierarchy");
        for (unsigned j = 0; j < 2; ++j) {
            const auto bone = pack.Pointer(bones + 4 * j);
            Require(pack.Type(bone) == "hkaBone" &&
                        pack.DataString(pack.Pointer(bone)) == (j ? "door_joint" : "origin"),
                    "Door joint order mismatch");
        }
        const auto reference = pack.Pointer(object + 20);
        pack.ArrayRange(reference, 2, 48);
        std::array<double, 3> child;
        for (unsigned j = 0; j < 3; ++j) {
            Require(std::abs(pack.F(reference + 4 * j)) < 1e-6, "Door skeleton root is translated");
            Require(std::abs(pack.F(reference + 32 + 4 * j) - 1) < 1e-5 &&
                        std::abs(pack.F(reference + 80 + 4 * j) - 1) < 1e-5,
                    "Door skeleton nonunit scale");
            child[j] = pack.F(reference + 48 + 4 * j);
        }
        std::array<double, 4> childRotation;
        for (unsigned j = 0; j < 4; ++j) {
            pose.rotation[j] = pack.F(reference + 16 + 4 * j);
            childRotation[j] = pack.F(reference + 64 + 4 * j);
        }
        pose.rotation = Normalize(pose.rotation);
        childRotation = Normalize(childRotation);
        Require(std::abs(childRotation[0]) + std::abs(childRotation[1]) + std::abs(childRotation[2]) <
                        1e-6 &&
                    std::abs(std::abs(childRotation[3]) - 1) < 1e-6,
                "Unsupported rotated door child reference");
        pose.translation = Rotate(pose.rotation, child);
        found = true;
    }
    Require(found, "Door skeleton absent");
    return pose;
}
struct Clip {
    std::uint32_t data, floatStart, frames;
    double duration, frameDuration;
};
Clip FindClip(const HavokPack &pack, const char *name) {
    const auto anims = Variant(pack, "Animation Container", "hkaAnimationContainer");
    const auto names = Variant(pack, "animNames", "rvHkStringContainer");
    const auto count = pack.DU(anims + 12);
    Require(count > 0 && count <= 4096 && pack.DU(names + 4) == count, "Animation name table mismatch");
    const auto objects = pack.Pointer(anims + 8), strings = pack.Pointer(names);
    pack.ArrayRange(objects, count, 4);
    pack.ArrayRange(strings, count, 4);
    std::uint32_t object = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pack.DataString(pack.Pointer(strings + 4 * i)) != name)
            continue;
        Require(!object, "Duplicate door animation name");
        object = pack.Pointer(objects + 4 * i);
    }
    Require(object && pack.Type(object) == "hkaSplineSkeletalAnimation" && pack.DU(object + 8) == 5 &&
                pack.DU(object + 16) == 2 && pack.DU(object + 20) == 6,
            "Unknown door animation layout");
    const auto frames = pack.DU(object + 36);
    Require(frames >= 2 && frames <= 256 && pack.DU(object + 40) == 1 && pack.DU(object + 44) == 256 &&
                pack.DU(object + 48) == 16 && pack.DU(object + 68) == 1 && pack.DU(object + 80) == 1,
            "Unsupported door spline block layout");
    const auto data = pack.Pointer(object + 112), bytes = pack.DU(object + 116);
    const auto floatStart = pack.DU(pack.Pointer(object + 76));
    Require(bytes <= 65536 && floatStart >= 36 && floatStart <= bytes &&
                pack.DU(pack.Pointer(object + 64)) == 0,
            "Invalid door spline block offsets");
    pack.DataRange(data, bytes);
    const auto duration = pack.F(object + 12), frameDuration = pack.F(object + 60);
    Require(frameDuration > 0 && frameDuration <= 1 &&
                std::abs(duration - (frames - 1) * frameDuration) < .0001,
            "Door animation timing disagrees with frame count");
    return {data, floatStart, frames, duration, frameDuration};
}
unsigned Byte(const HavokPack &p, std::uint32_t pos) {
    p.DataRange(pos, 1);
    return p.bytes[p.sections[2].absolute + pos];
}
unsigned U16(const HavokPack &p, std::uint32_t pos) { return Byte(p, pos) | (Byte(p, pos + 1) << 8); }
std::array<double, 4> PackedRotation(const HavokPack &p, std::uint32_t pos) {
    p.DataRange(pos, 5);
    std::uint64_t bits = 0;
    for (unsigned i = 0; i < 5; ++i)
        bits |= std::uint64_t(Byte(p, pos + i)) << (8 * i);
    Require((bits >> 39) == 0, "Door quaternion reserved bit");
    const unsigned omitted = unsigned((bits >> 36) & 3);
    std::array<double, 4> q;
    double sum = 0;
    unsigned j = 0;
    // Twelve-bit smallest-three coordinates centered on 2047. The omitted
    // component index/sign occupy bits36..38. PC angles corroborate the
    // distinct idle/moving rotations (89.94/90.06 degrees).
    for (unsigned i = 0; i < 4; ++i)
        if (i != omitted) {
            const unsigned v = unsigned((bits >> (12 * j++)) & 4095);
            Require(v <= 4094, "Invalid packed door quaternion");
            q[i] = (double(v) - 2047) * (std::sqrt(.5) / 2047);
            sum += q[i] * q[i];
        }
    Require(sum <= 1, "Packed door quaternion norm invalid");
    q[omitted] = std::sqrt(1 - sum) * ((bits & (std::uint64_t(1) << 38)) ? -1 : 1);
    return Normalize(q);
}
DoorJointPose Endpoint(const HavokPack &p, const char *name) {
    const Clip clip = FindClip(p, name);
    // Root translation is identity. Its five-byte rotation follows the
    // aligned 16-byte mask table. Child XYZ are three literal floats.
    Require(clip.frames == 2 && p.DU(clip.data) == 0x00090045 && p.DU(clip.data + 4) == 0x00000745,
            "Unsupported door endpoint mask");
    DoorJointPose pose;
    pose.rotation = PackedRotation(p, clip.data + 16);
    const std::array<double, 3> child = {
        {p.F(clip.data + 24), p.F(clip.data + 28), p.F(clip.data + 32)}};
    pose.translation = Rotate(pose.rotation, child);
    return pose;
}
} // namespace
DoorAnimation DoorAnimation::Load(const std::vector<unsigned char> &hkxa,
                                  const std::vector<unsigned char> &skel) {
    const auto reference = VerifySkeleton(HavokPack(skel, 64u << 20, 100000, 1000000));
    const HavokPack p(hkxa, 64u << 20, 100000, 1000000);
    const Clip clip = FindClip(p, DoorClip);
    // Identity root translation; packed static root rotation; dynamic X,
    // literal child YZ, identity child rotation and unit scales.
    Require(p.DU(clip.data) == 0x000f0045 && p.DU(clip.data + 4) == 0x00401645,
            "Unsupported door movement mask");
    const auto n = U16(p, clip.data + 24) + 1, degree = Byte(p, clip.data + 26);
    Require(degree == 1 && n >= 2 && n <= 256, "Unsupported spline degree/control count");
    const auto knotCount = n + 2, extrema = (27 + knotCount + 3) & ~3u;
    Require(extrema + 16 + 2 * n <= clip.floatStart, "Door spline overlaps other tracks");
    DoorAnimation anim;
    anim.reference_ = reference;
    anim.closed_ = Endpoint(p, IdleClip);
    anim.end_ = Endpoint(p, EndClip);
    anim.moving_.rotation = PackedRotation(p, clip.data + 16);
    // Retained in root space here; SamplePose composes into model space.
    anim.moving_.translation = {{0, p.F(clip.data + extrema + 8), p.F(clip.data + extrema + 12)}};
    anim.duration_ = clip.duration;
    anim.frameDuration_ = clip.frameDuration;
    for (unsigned i = 0; i < knotCount; ++i)
        anim.knots_.push_back(Byte(p, clip.data + 27 + i));
    Require(std::is_sorted(anim.knots_.begin(), anim.knots_.end()) && anim.knots_[0] == 0 &&
                anim.knots_[1] == 0 && anim.knots_[n] == clip.frames - 1 &&
                anim.knots_[n + 1] == clip.frames - 1,
            "Invalid clamped door spline knots");
    for (unsigned i = 2; i <= n; ++i)
        Require(anim.knots_[i] > anim.knots_[i - 1], "Repeated interior door spline knot");
    const double lo = p.F(clip.data + extrema), hi = p.F(clip.data + extrema + 4);
    Require(lo <= hi && std::abs(lo) < 1000000 && std::abs(hi) < 1000000, "Invalid door spline range");
    for (unsigned i = 0; i < n; ++i)
        anim.values_.push_back(lo + (hi - lo) * U16(p, clip.data + extrema + 16 + 2 * i) / 65535.);
    Require(std::abs(anim.closed_.rotation[1]) + std::abs(anim.closed_.rotation[2]) < 1e-6 &&
                std::abs(anim.moving_.rotation[1]) + std::abs(anim.moving_.rotation[2]) < 1e-6 &&
                std::abs(anim.end_.rotation[1]) + std::abs(anim.end_.rotation[2]) < 1e-6,
            "Unsupported door root axis");
    Require(std::abs(anim.ClosedX() - anim.closed_.translation[0]) < .001 &&
                std::abs(anim.OpenX() - anim.end_.translation[0]) < .001,
            "Door clip endpoints disagree");
    return anim;
}
double DoorAnimation::LocalX(double seconds) const {
    Require(!values_.empty() && std::isfinite(seconds), "Invalid door animation sample");
    if (seconds <= 0)
        return ClosedX();
    const double frame = seconds / frameDuration_;
    if (seconds >= duration_ || frame >= knots_[values_.size()])
        return OpenX();
    const auto k = std::upper_bound(knots_.begin(), knots_.end(), frame) - knots_.begin() - 1;
    const double t = (frame - knots_[k]) / (knots_[k + 1] - knots_[k]);
    return values_[k - 1] + (values_[k] - values_[k - 1]) * t;
}
DoorJointPose DoorAnimation::SamplePose(double seconds) const {
    DoorJointPose pose = moving_;
    pose.translation[0] = LocalX(seconds);
    pose.translation = Rotate(pose.rotation, pose.translation);
    return pose;
}
} // namespace wolf2009

// SPDX-License-Identifier: GPL-3.0-or-later
#include "GreeterAnimation.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace wolf2009 {
namespace {
using detail::HavokPack;
const char *const ClipName = "models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx";
const char *const SkelName = "models/characters/_humanoid_male/humanoid_male.hkx";
const char *const JointNames[] = {
    "origin", "hips", "l_upleg", "l_knee", "l_ankle", "l_ball", "r_upleg", "r_knee", "r_ankle",
    "r_ball", "waist", "stomach", "chest", "shoulders", "l_clavicle", "l_uparm", "l_bicep",
    "l_elbow", "l_forarm", "l_wrist_ik", "l_index_1", "l_index_2", "l_middle_1", "l_middle_2",
    "l_pinky_1", "l_pinky_2", "l_ring_1", "l_ring_2", "l_thumb_1", "l_thumb_2", "l_wpn_ctrl",
    "neck", "head", "head_channel", "inview_camera_ctrl", "r_clavicle", "r_uparm", "r_bicep",
    "r_elbow", "r_forarm", "r_wrist_ik", "r_index_1", "r_index_2", "r_middle_1", "r_middle_2",
    "r_pinky_1", "r_pinky_2", "r_ring_1", "r_ring_2", "r_thumb_1", "r_thumb_2", "r_wpn_ctrl"};

void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::array<double, 4> Unit(std::array<double, 4> q) {
    double norm = 0;
    for (double x : q) {
        Need(std::isfinite(x), "Nonfinite greeter quaternion");
        norm += x * x;
    }
    Need(std::isfinite(norm) && norm > 1e-12, "Degenerate greeter quaternion");
    const double length = std::sqrt(norm);
    for (double &x : q)
        x /= length;
    return q;
}

std::array<double, 4> Multiply(const std::array<double, 4> &a, const std::array<double, 4> &b) {
    return {{a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1],
             a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
             a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3],
             a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2]}};
}

GreeterJointPose Compose(const GreeterJointPose &parent, const GreeterJointPose &child) {
    const auto &q = parent.rotation;
    const auto &v = child.translation;
    const std::array<double, 3> t{{2*(q[1]*v[2] - q[2]*v[1]),
                                  2*(q[2]*v[0] - q[0]*v[2]),
                                  2*(q[0]*v[1] - q[1]*v[0])}};
    GreeterJointPose result;
    result.translation = {{parent.translation[0] + v[0] + q[3]*t[0] + q[1]*t[2] - q[2]*t[1],
                            parent.translation[1] + v[1] + q[3]*t[1] + q[2]*t[0] - q[0]*t[2],
                            parent.translation[2] + v[2] + q[3]*t[2] + q[0]*t[1] - q[1]*t[0]}};
    result.rotation = Unit(Multiply(q, child.rotation));
    return result;
}

std::uint32_t Variant(const HavokPack &pack, const char *name, const char *type,
                       const GreeterAnimationLimits &limits) {
    const auto root = pack.U(28), count = pack.DU(root + 4), array = pack.Pointer(root);
    Need(count && count <= limits.tableEntries, "Greeter root variant table limit");
    pack.ArrayRange(array, count, 16);
    std::uint32_t found = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto slot = array + 16*i;
        if (pack.DataString(pack.Pointer(slot)) != name)
            continue;
        const auto object = pack.Pointer(slot + 8);
        Need(!found && object && pack.DataString(pack.Pointer(slot + 4)) == type &&
                 pack.Type(object) == type, "Ambiguous/mistyped greeter named variant");
        found = object;
    }
    Need(found, "Missing greeter named variant");
    return found;
}

unsigned Byte(const HavokPack &pack, std::uint32_t at) {
    pack.DataRange(at, 1);
    return pack.bytes[pack.sections[2].absolute + at];
}

unsigned U16(const HavokPack &pack, std::uint32_t at) {
    pack.DataRange(at, 2);
    return Byte(pack, at) | Byte(pack, at + 1) << 8;
}

std::vector<GreeterJoint> Skeleton(const HavokPack &pack, const GreeterAnimationLimits &limits) {
    const auto container = Variant(pack, "Animation Container", "hkaAnimationContainer", limits);
    const auto count = pack.DU(container + 4), array = pack.Pointer(container);
    Need(count && count <= limits.tableEntries, "Greeter skeleton table limit");
    pack.ArrayRange(array, count, 4);
    std::uint32_t selected = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto object = pack.Pointer(array + 4*i);
        Need(pack.Type(object) == "hkaSkeleton", "Unexpected skeleton table object");
        if (pack.DataString(pack.Pointer(object)) != SkelName)
            continue;
        Need(!selected, "Duplicate greeter skeleton name");
        selected = object;
    }
    Need(selected && pack.DU(selected + 8) == 52 && pack.DU(selected + 16) == 52 &&
             pack.DU(selected + 24) == 52, "Expected named 52-joint humanoid skeleton");
    const auto parents = pack.Pointer(selected + 4), bones = pack.Pointer(selected + 12);
    const auto reference = pack.Pointer(selected + 20);
    pack.ArrayRange(parents, 52, 2);
    pack.ArrayRange(bones, 52, 4);
    pack.ArrayRange(reference, 52, 48);
    std::vector<GreeterJoint> result;
    for (unsigned i = 0; i < 52; ++i) {
        const auto object = pack.Pointer(bones + 4*i);
        Need(pack.Type(object) == "hkaBone", "Unexpected greeter bone object");
        GreeterJoint joint;
        joint.name = pack.DataString(pack.Pointer(object));
        Need(joint.name == JointNames[i], "Unsupported greeter joint name/order");
        const auto parent = U16(pack, parents + 2*i);
        joint.parent = parent == 65535 ? -1 : int(parent);
        Need(i == 0 ? joint.parent == -1 : joint.parent >= 0 && joint.parent < int(i),
             "Greeter hierarchy is not a single ordered tree");
        const auto pose = reference + i*48;
        for (unsigned axis = 0; axis < 3; ++axis) {
            joint.referenceLocal.translation[axis] = pack.F(pose + 4*axis);
            Need(std::abs(joint.referenceLocal.translation[axis]) < 1000000 &&
                     std::abs(pack.F(pose + 32 + 4*axis) - 1) < 1e-5,
                 "Unsupported greeter reference translation/scale");
        }
        double norm = 0;
        for (unsigned axis = 0; axis < 4; ++axis) {
            const double value = pack.F(pose + 16 + 4*axis);
            joint.referenceLocal.rotation[axis] = value;
            norm += value*value;
        }
        Need(std::abs(norm - 1) < .001, "Nonunit greeter reference quaternion");
        joint.referenceLocal.rotation = Unit(joint.referenceLocal.rotation);
        joint.referenceModel = joint.parent < 0 ? joint.referenceLocal :
            Compose(result[std::size_t(joint.parent)].referenceModel, joint.referenceLocal);
        result.push_back(joint);
    }
    return result;
}

struct Curve {
    unsigned degree = 0;
    std::vector<unsigned> knots;
    std::vector<std::array<double, 4>> controls;
};

struct Track {
    Curve translation, rotation;
};

class Cursor {
    const HavokPack &pack_;
    const GreeterAnimationLimits &limits_;
    const std::uint32_t base_, end_;
    std::uint32_t position_;
    std::size_t controls_ = 0;

  public:
    Cursor(const HavokPack &pack, std::uint32_t base, std::uint32_t end,
           const GreeterAnimationLimits &limits) : pack_(pack), limits_(limits), base_(base),
                                                  end_(end), position_(216) {}
    void Range(std::size_t size) const {
        Need(position_ <= end_ && size <= end_ - position_, "Greeter curve exceeds transform area");
    }
    unsigned B() {
        Range(1);
        return Byte(pack_, base_ + position_++);
    }
    unsigned H() {
        const unsigned low = B();
        return low | B() << 8;
    }
    double F() {
        Range(4);
        const double value = pack_.F(base_ + position_);
        position_ += 4;
        Need(std::abs(value) < 1000000, "Excessive greeter transform component");
        return value;
    }
    void Align() {
        while (position_ % 4)
            Need(B() == 0, "Nonzero greeter curve alignment padding");
    }
    Curve Header(bool dynamic) {
        Curve curve;
        unsigned count = 1;
        if (dynamic) {
            count = H() + 1;
            curve.degree = B();
            Need((curve.degree == 1 || curve.degree == 3) && count > curve.degree &&
                     count <= 256 && count <= limits_.curveControls,
                 "Unsupported greeter spline degree/control count");
            for (unsigned i = 0; i < count + curve.degree + 1; ++i) {
                const auto knot = B();
                Need(knot <= 181 && (curve.knots.empty() || knot >= curve.knots.back()),
                     "Invalid greeter spline knot order/range");
                curve.knots.push_back(knot);
            }
            for (unsigned i = 0; i <= curve.degree; ++i)
                Need(curve.knots[i] == 0 && curve.knots[curve.knots.size() - 1 - i] == 181,
                     "Greeter spline is not clamped to the observed frame domain");
        }
        Need(controls_ <= limits_.totalControls && count <= limits_.totalControls - controls_,
             "Greeter total curve control budget");
        controls_ += count;
        curve.controls.resize(count);
        return curve;
    }
    Curve Vector(unsigned mask) {
        Need(mask == 0 || mask == 2 || mask == 3 || mask == 6 || mask == 7 || mask == 0x70,
             "Unsupported greeter translation mask");
        const bool dynamic = (mask & 0x70) != 0;
        Curve curve = Header(dynamic);
        if (dynamic)
            Align();
        std::array<double, 4> literal{{0, 0, 0, 0}}, low{{0, 0, 0, 0}}, high{{0, 0, 0, 0}};
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (mask & (1u << (axis + 4))) {
                low[axis] = F();
                high[axis] = F();
                Need(low[axis] <= high[axis], "Inverted greeter translation range");
            } else if (mask & (1u << axis)) {
                literal[axis] = F();
            }
        }
        for (auto &point : curve.controls) {
            point = literal;
            for (unsigned axis = 0; axis < 3; ++axis)
                if (mask & (1u << (axis + 4)))
                    point[axis] = low[axis] + (high[axis] - low[axis])*H()/65535.0;
        }
        Align();
        return curve;
    }
    std::array<double, 4> Quaternion() {
        std::uint64_t bits = 0;
        for (unsigned i = 0; i < 5; ++i)
            bits |= std::uint64_t(B()) << (8*i);
        Need(!(bits >> 39), "Reserved greeter quaternion bit is set");
        const unsigned omitted = unsigned((bits >> 36) & 3);
        std::array<double, 4> q;
        double norm = 0;
        unsigned component = 0;
        for (unsigned axis = 0; axis < 4; ++axis) {
            if (axis == omitted)
                continue;
            const unsigned value = unsigned((bits >> (12*component++)) & 4095);
            Need(value <= 4094, "Invalid greeter packed quaternion coordinate");
            q[axis] = (double(value) - 2047)*std::sqrt(.5)/2047;
            norm += q[axis]*q[axis];
        }
        Need(norm <= 1, "Invalid greeter packed quaternion norm");
        q[omitted] = std::sqrt(1 - norm)*((bits & (std::uint64_t(1) << 38)) ? -1 : 1);
        return Unit(q);
    }
    Curve Rotation(unsigned mask) {
        Need(mask == 0 || mask == 9 || mask == 15 || mask == 0x90 || mask == 0xc0 || mask == 0xf0,
             "Unsupported greeter rotation mask");
        Curve curve = Header((mask & 0xf0) != 0);
        for (auto &point : curve.controls)
            point = mask ? Quaternion() : std::array<double, 4>{{0, 0, 0, 1}};
        Align();
        return curve;
    }
    void End() const {
        Need(position_ == end_, "Greeter transforms do not consume declared transform area");
    }
};

std::array<double, 4> Sample(const Curve &curve, double frame) {
    if (!curve.degree || frame <= curve.knots[curve.degree])
        return curve.controls.front();
    if (frame >= curve.knots[curve.controls.size()])
        return curve.controls.back();
    const auto span = std::size_t(std::upper_bound(curve.knots.begin(), curve.knots.end(), frame) -
                                  curve.knots.begin() - 1);
    Need(span >= curve.degree && span < curve.controls.size(), "Greeter spline span out of range");
    std::array<std::array<double, 4>, 4> work;
    for (unsigned i = 0; i <= curve.degree; ++i)
        work[i] = curve.controls[span - curve.degree + i];
    for (unsigned level = 1; level <= curve.degree; ++level) {
        for (unsigned i = curve.degree; i >= level; --i) {
            const auto knot = span - curve.degree + i;
            const double denominator = curve.knots[knot + curve.degree - level + 1] - curve.knots[knot];
            const double weight = denominator ? (frame - curve.knots[knot])/denominator : 0;
            for (unsigned axis = 0; axis < 4; ++axis)
                work[i][axis] = work[i - 1][axis]*(1 - weight) + work[i][axis]*weight;
        }
    }
    return work[curve.degree];
}
} // namespace

struct GreeterAnimation::Impl {
    std::string resource = ClipName, skeleton = SkelName;
    std::vector<GreeterJoint> joints;
    std::vector<Track> tracks;
    double duration = 0, frameDuration = 0;
    std::size_t frames = 0, auxiliaryBytes = 0;
};

GreeterAnimation GreeterAnimation::Load(const std::vector<unsigned char> &mapHkxa,
                                       const std::vector<unsigned char> &globalSkel,
                                       const GreeterAnimationLimits &limits) {
    std::shared_ptr<Impl> impl(new Impl);
    impl->joints = Skeleton(HavokPack(globalSkel, limits.sourceBytes, limits.objects, limits.fixups), limits);
    const HavokPack pack(mapHkxa, limits.sourceBytes, limits.objects, limits.fixups);
    const auto container = Variant(pack, "Animation Container", "hkaAnimationContainer", limits);
    const auto names = Variant(pack, "animNames", "rvHkStringContainer", limits);
    const auto count = pack.DU(container + 12), objects = pack.Pointer(container + 8);
    const auto strings = pack.Pointer(names);
    Need(count && count <= limits.tableEntries && pack.DU(names + 4) == count,
         "Greeter animation name/object table mismatch");
    pack.ArrayRange(objects, count, 4);
    pack.ArrayRange(strings, count, 4);
    std::uint32_t object = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pack.DataString(pack.Pointer(strings + 4*i)) != ClipName)
            continue;
        Need(!object, "Duplicate greeter clip name");
        object = pack.Pointer(objects + 4*i);
    }
    Need(object && pack.Type(object) == "hkaSplineSkeletalAnimation" && pack.DU(object + 8) == 5 &&
             pack.DU(object + 16) == 52 && pack.DU(object + 20) == 6,
         "Unsupported named greeter animation class/track profile");
    Need(pack.DU(object + 36) == 182 && pack.DU(object + 40) == 1 && pack.DU(object + 44) == 256 &&
             pack.DU(object + 48) == 216 && pack.DU(object + 68) == 1 && pack.DU(object + 80) == 1,
         "Unsupported greeter animation block/frame profile");
    impl->frames = 182;
    impl->duration = pack.F(object + 12);
    impl->frameDuration = pack.F(object + 60);
    Need(std::abs(impl->frameDuration - 1.0/24) < 1e-7 &&
             std::abs(impl->duration - 181*impl->frameDuration) < 1e-5,
         "Greeter animation frame timing mismatch");
    const auto data = pack.Pointer(object + 112), bytes = pack.DU(object + 116);
    const auto transformEnd = pack.DU(pack.Pointer(object + 76));
    Need(bytes <= limits.compressedClipBytes && transformEnd >= 216 && transformEnd < bytes &&
             pack.DU(pack.Pointer(object + 64)) == 0, "Invalid greeter spline block range");
    pack.DataRange(data, bytes);
    for (unsigned i = 208; i < 216; ++i)
        Need(Byte(pack, data + i) == (i < 214 ? 0x12u : 0u), "Unsupported greeter auxiliary mask profile");
    Cursor cursor(pack, data, transformEnd, limits);
    for (unsigned i = 0; i < 52; ++i) {
        Need(Byte(pack, data + 4*i) == 0x45 && Byte(pack, data + 4*i + 3) == 0,
             "Unsupported greeter quantization/animated scale");
        Track track;
        track.translation = cursor.Vector(Byte(pack, data + 4*i + 1));
        track.rotation = cursor.Rotation(Byte(pack, data + 4*i + 2));
        impl->tracks.push_back(track);
    }
    cursor.End();
    impl->auxiliaryBytes = bytes - transformEnd;
    return GreeterAnimation(impl);
}

const std::string &GreeterAnimation::ResourceName() const { return impl_->resource; }
const std::string &GreeterAnimation::SkeletonName() const { return impl_->skeleton; }
const std::vector<GreeterJoint> &GreeterAnimation::Joints() const { return impl_->joints; }
double GreeterAnimation::Duration() const { return impl_->duration; }
double GreeterAnimation::FrameDuration() const { return impl_->frameDuration; }
std::size_t GreeterAnimation::FrameCount() const { return impl_->frames; }
std::size_t GreeterAnimation::OpaqueAuxiliaryBytes() const { return impl_->auxiliaryBytes; }

GreeterPose GreeterAnimation::SamplePose(double seconds) const {
    Need(std::isfinite(seconds), "Nonfinite greeter sample time");
    GreeterPose result;
    result.seconds = std::max(0.0, std::min(seconds, impl_->duration));
    const double frame = result.seconds >= impl_->duration ? 181.0 :
                                                             result.seconds/impl_->frameDuration;
    for (std::size_t i = 0; i < impl_->joints.size(); ++i) {
        GreeterJointPose local;
        const auto translation = Sample(impl_->tracks[i].translation, frame);
        std::copy(translation.begin(), translation.begin() + 3, local.translation.begin());
        local.rotation = Unit(Sample(impl_->tracks[i].rotation, frame));
        const int parent = impl_->joints[i].parent;
        result.local.push_back(local);
        result.model.push_back(parent < 0 ? local : Compose(result.model[std::size_t(parent)], local));
    }
    return result;
}

bool GreeterAnimationPlayback::Start(double committedStartTime) {
    Need(std::isfinite(committedStartTime) && committedStartTime >= 0, "Invalid greeter start clock");
    if (started_)
        return false;
    start_ = committedStartTime;
    started_ = true;
    return true;
}

GreeterPlaybackPose GreeterAnimationPlayback::SampleAt(double clock) const {
    Need(std::isfinite(clock) && clock >= 0, "Invalid greeter playback clock");
    GreeterPlaybackPose result;
    if (!started_)
        return result;
    Need(clock >= start_ && std::isfinite(clock - start_), "Greeter clock precedes committed start");
    result.committedStartTime = start_;
    result.elapsed = clock - start_;
    result.status = result.elapsed >= animation_.Duration() ? GreeterPlaybackStatus::Finished :
                                                            GreeterPlaybackStatus::Playing;
    result.pose = animation_.SamplePose(result.elapsed);
    return result;
}
} // namespace wolf2009

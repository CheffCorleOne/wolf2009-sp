// SPDX-License-Identifier: GPL-3.0-or-later
#include "GreeterAnimation.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;

void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

void Near(double a, double b, double epsilon = 1e-6) {
    if (std::abs(a - b) > epsilon)
        throw std::runtime_error("Greeter pose numerical mismatch: " + std::to_string(a) +
                                 " expected " + std::to_string(b));
}

void Refuses(const std::function<void()> &action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Unsupported greeter input accepted");
}

void Set(Bytes &bytes, std::size_t at, std::uint32_t value) {
    Need(at <= bytes.size() && 4 <= bytes.size() - at, "Synthetic greeter write bounds");
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<unsigned char>(value >> (8*i));
}

void Append(Bytes &bytes, std::uint32_t value) {
    const auto at = bytes.size();
    bytes.resize(at + 4);
    Set(bytes, at, value);
}

void Float(Bytes &bytes, std::size_t at, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    Set(bytes, at, bits);
}

void PushFloat(Bytes &bytes, float value) {
    const auto at = bytes.size();
    bytes.resize(at + 4);
    Float(bytes, at, value);
}

void Pad(Bytes &bytes) {
    while (bytes.size() % 4)
        bytes.push_back(0);
}

struct Pack {
    Bytes data;
    std::map<std::uint32_t, std::uint32_t> pointers;
    std::map<std::uint32_t, std::string> objects;
    std::uint32_t Allocate(unsigned size) {
        Pad(data);
        const auto at = std::uint32_t(data.size());
        data.resize(data.size() + size);
        return at;
    }
    std::uint32_t String(const std::string &value) {
        const auto at = std::uint32_t(data.size());
        data.insert(data.end(), value.begin(), value.end());
        data.push_back(0);
        return at;
    }
    void Pointer(std::uint32_t from, std::uint32_t to) { pointers[from] = to; }
    Bytes Finish() {
        Pad(data);
        Bytes classes;
        std::map<std::string, std::uint32_t> names;
        for (const auto &object : objects) {
            if (names.count(object.second))
                continue;
            names[object.second] = std::uint32_t(classes.size());
            classes.insert(classes.end(), object.second.begin(), object.second.end());
            classes.push_back(0);
        }
        const auto dataSize = std::uint32_t(data.size());
        for (const auto &pointer : pointers) {
            Append(data, pointer.first);
            Append(data, pointer.second);
        }
        const auto global = std::uint32_t(data.size());
        for (const auto &object : objects) {
            Append(data, object.first);
            Append(data, 0);
            Append(data, names.at(object.second));
        }
        const auto end = std::uint32_t(data.size());
        Bytes result(208);
        Set(result, 0, 0x57e0e057u);
        Set(result, 4, 0x10c0c010u);
        Set(result, 12, 5);
        result[16] = 4; result[17] = 1; result[19] = 1;
        Set(result, 20, 3); Set(result, 24, 2);
        Set(result, 36, names.at("hkRootLevelContainer"));
        const std::string version = "Havok-5.6.0-r1";
        std::copy(version.begin(), version.end(), result.begin() + 40);
        const char *sections[] = {"__classnames__", "__types__", "__data__"};
        for (unsigned i = 0; i < 3; ++i) {
            const std::string name = sections[i];
            std::copy(name.begin(), name.end(), result.begin() + 64 + i*48);
        }
        Set(result, 84, 208);
        for (unsigned i = 24; i <= 44; i += 4)
            Set(result, 64 + i, std::uint32_t(classes.size()));
        const auto absolute = std::uint32_t(208 + classes.size());
        Set(result, 132, absolute);
        Set(result, 180, absolute);
        Set(result, 184, dataSize); Set(result, 188, global); Set(result, 192, global);
        Set(result, 196, end); Set(result, 200, end); Set(result, 204, end);
        result.insert(result.end(), classes.begin(), classes.end());
        result.insert(result.end(), data.begin(), data.end());
        return result;
    }
};

void Root(Pack &pack, bool names) {
    Need(pack.Allocate(8) == 0, "Synthetic root offset");
    pack.objects[0] = "hkRootLevelContainer";
    const auto array = pack.Allocate(names ? 32 : 16);
    Set(pack.data, 4, names ? 2 : 1);
    pack.Pointer(0, array);
    pack.Pointer(array, pack.String("Animation Container"));
    pack.Pointer(array + 4, pack.String("hkaAnimationContainer"));
    pack.Pointer(array + 8, pack.Allocate(32));
    pack.objects[pack.pointers.at(array + 8)] = "hkaAnimationContainer";
    if (names) {
        pack.Pointer(array + 16, pack.String("animNames"));
        pack.Pointer(array + 20, pack.String("rvHkStringContainer"));
        pack.Pointer(array + 24, pack.Allocate(8));
        pack.objects[pack.pointers.at(array + 24)] = "rvHkStringContainer";
    }
}

std::vector<std::string> Names() {
    std::istringstream text(
        "origin hips l_upleg l_knee l_ankle l_ball r_upleg r_knee r_ankle r_ball waist stomach chest "
        "shoulders l_clavicle l_uparm l_bicep l_elbow l_forarm l_wrist_ik l_index_1 l_index_2 "
        "l_middle_1 l_middle_2 l_pinky_1 l_pinky_2 l_ring_1 l_ring_2 l_thumb_1 l_thumb_2 l_wpn_ctrl "
        "neck head head_channel inview_camera_ctrl r_clavicle r_uparm r_bicep r_elbow r_forarm "
        "r_wrist_ik r_index_1 r_index_2 r_middle_1 r_middle_2 r_pinky_1 r_pinky_2 r_ring_1 r_ring_2 "
        "r_thumb_1 r_thumb_2 r_wpn_ctrl");
    std::vector<std::string> result;
    std::string name;
    while (text >> name)
        result.push_back(name);
    return result;
}

struct SkeletonFixture {
    Pack pack;
    std::uint32_t skeleton, parents, reference;
    SkeletonFixture() {
        Root(pack, false);
        const auto container = pack.pointers.at(pack.pointers.at(0) + 8);
        const auto array = pack.Allocate(4);
        skeleton = pack.Allocate(40);
        pack.Pointer(container, array); Set(pack.data, container + 4, 1);
        pack.Pointer(array, skeleton); pack.objects[skeleton] = "hkaSkeleton";
        pack.Pointer(skeleton, pack.String("models/characters/_humanoid_male/humanoid_male.hkx"));
        parents = pack.Allocate(104);
        const auto bones = pack.Allocate(208);
        reference = pack.Allocate(52*48);
        pack.Pointer(skeleton + 4, parents); Set(pack.data, skeleton + 8, 52);
        pack.Pointer(skeleton + 12, bones); Set(pack.data, skeleton + 16, 52);
        pack.Pointer(skeleton + 20, reference); Set(pack.data, skeleton + 24, 52);
        pack.data[parents] = 255; pack.data[parents + 1] = 255;
        pack.data[parents + 4] = 1; // joint 2 is child of joint 1
        const auto names = Names();
        Need(names.size() == 52, "Synthetic joint count");
        for (unsigned i = 0; i < 52; ++i) {
            const auto bone = pack.Allocate(8);
            pack.objects[bone] = "hkaBone";
            pack.Pointer(bones + 4*i, bone);
            pack.Pointer(bone, pack.String(names[i]));
            Float(pack.data, reference + 48*i, i ? 1.f : 0.f);
            Float(pack.data, reference + 48*i + 28, 1);
            for (unsigned axis = 0; axis < 3; ++axis)
                Float(pack.data, reference + 48*i + 32 + 4*axis, 1);
        }
    }
};

void Quaternion(Bytes &data, bool ninety) {
    std::uint64_t bits = (std::uint64_t(3) << 36) | 2047u | (std::uint64_t(2047) << 12) |
                         (std::uint64_t(ninety ? 4094 : 2047) << 24);
    for (unsigned i = 0; i < 5; ++i)
        data.push_back(static_cast<unsigned char>(bits >> (8*i)));
}

void CurveHeader(Bytes &data, unsigned degree, unsigned count) {
    data.push_back(static_cast<unsigned char>(count - 1));
    data.push_back(0);
    data.push_back(static_cast<unsigned char>(degree));
    for (unsigned i = 0; i <= degree; ++i)
        data.push_back(0);
    for (unsigned i = 0; i <= degree; ++i)
        data.push_back(181);
}

struct ClipFixture {
    Pack pack;
    std::uint32_t object, data, floatOffset, quaternionOffset, vectorHeader;
    ClipFixture() {
        Root(pack, true);
        const auto variants = pack.pointers.at(0), container = pack.pointers.at(variants + 8);
        const auto names = pack.pointers.at(variants + 24);
        const auto array = pack.Allocate(4), strings = pack.Allocate(4);
        object = pack.Allocate(128);
        pack.Pointer(container + 8, array); Set(pack.data, container + 12, 1);
        pack.Pointer(names, strings); Set(pack.data, names + 4, 1);
        pack.Pointer(array, object); pack.objects[object] = "hkaSplineSkeletalAnimation";
        pack.Pointer(strings, pack.String(
            "models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx"));
        Set(pack.data, object + 8, 5); Float(pack.data, object + 12, 181.f/24);
        Set(pack.data, object + 16, 52); Set(pack.data, object + 20, 6);
        Set(pack.data, object + 36, 182); Set(pack.data, object + 40, 1);
        Set(pack.data, object + 44, 256); Set(pack.data, object + 48, 216);
        Float(pack.data, object + 60, 1.f/24);
        const auto blockOffset = pack.Allocate(4);
        floatOffset = pack.Allocate(4);
        pack.Pointer(object + 64, blockOffset); Set(pack.data, object + 68, 1);
        pack.Pointer(object + 76, floatOffset); Set(pack.data, object + 80, 1);
        Bytes curves(216);
        for (unsigned i = 0; i < 52; ++i) {
            curves[4*i] = 0x45;
            curves[4*i + 1] = i == 0 ? 0 : (i == 1 || i == 3) ? 0x70 : 7;
            curves[4*i + 2] = i == 0 ? 0xf0 : 0;
        }
        for (unsigned i = 208; i < 214; ++i)
            curves[i] = 0x12;
        for (unsigned i = 0; i < 52; ++i) {
            if (i == 1 || i == 3) {
                if (i == 1)
                    vectorHeader = std::uint32_t(curves.size());
                CurveHeader(curves, i == 1 ? 1 : 3, i == 1 ? 2 : 4);
                Pad(curves);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    PushFloat(curves, 0);
                    PushFloat(curves, i == 1 ? float((axis + 1)*10) : 30.f);
                }
                const unsigned count = i == 1 ? 2 : 4;
                for (unsigned point = 0; point < count; ++point)
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        const unsigned value = point*65535/(count - 1);
                        curves.push_back(static_cast<unsigned char>(value));
                        curves.push_back(static_cast<unsigned char>(value >> 8));
                    }
                Pad(curves);
            } else if (i != 0) {
                PushFloat(curves, 1); PushFloat(curves, 0); PushFloat(curves, 0);
            }
            if (i == 0) {
                CurveHeader(curves, 3, 4);
                quaternionOffset = std::uint32_t(curves.size());
                for (unsigned point = 0; point < 4; ++point)
                    Quaternion(curves, point >= 2);
                Pad(curves);
            }
        }
        Set(pack.data, floatOffset, std::uint32_t(curves.size()));
        curves.resize(curves.size() + 8); // bounded opaque auxiliary bytes
        data = pack.Allocate(unsigned(curves.size()));
        std::copy(curves.begin(), curves.end(), pack.data.begin() + data);
        pack.Pointer(object + 112, data); Set(pack.data, object + 116, std::uint32_t(curves.size()));
    }
};

GreeterAnimation Fixture() {
    ClipFixture clip;
    SkeletonFixture skel;
    return GreeterAnimation::Load(clip.pack.Finish(), skel.pack.Finish());
}
} // namespace

int RunGreeterAnimationTests() {
    unsigned tests = 0;
    const auto test = [&tests](const std::function<void()> &action) { action(); ++tests; };
    test([] {
        const auto animation = Fixture();
        Need(animation.Joints().size() == 52 && animation.FrameCount() == 182 &&
                 animation.OpaqueAuxiliaryBytes() == 8 && animation.Joints()[32].name == "head",
             "Greeter clip metadata mismatch");
        Near(animation.Joints()[2].referenceModel.translation[0], 2);
        const auto start = animation.SamplePose(-1), end = animation.SamplePose(100);
        Near(start.local[1].translation[0], 0);
        Near(end.local[1].translation[0], 10);
        Near(end.local[1].translation[1], 20);
        Near(end.local[1].translation[2], 30);
        Near(end.model[1].translation[0], -20);
        Near(end.model[1].translation[1], 10);
    });
    test([] {
        const auto animation = Fixture();
        const auto pose = animation.SamplePose(90.5*animation.FrameDuration());
        Near(pose.local[1].translation[0], 5);
        Near(pose.local[1].translation[1], 10);
        Near(pose.local[3].translation[0], 15);
        Near(pose.model[1].translation[0], -5/std::sqrt(2.0));
        Near(pose.model[1].translation[1], 15/std::sqrt(2.0));
        Near(pose.model[2].translation[0], -4/std::sqrt(2.0));
        Near(pose.model[2].translation[1], 16/std::sqrt(2.0));
        Near(pose.local[0].rotation[2], std::sin(std::acos(-1.0)/8));
    });
    test([] {
        const auto animation = Fixture();
        for (unsigned frame = 0; frame < 182; ++frame) {
            const auto pose = animation.SamplePose(frame*animation.FrameDuration());
            for (const auto &joint : pose.model) {
                double norm = 0;
                for (double x : joint.rotation)
                    norm += x*x;
                Near(norm, 1);
            }
        }
    });
    test([] {
        GreeterAnimationPlayback playback(Fixture());
        Need(!playback.Started() && playback.SampleAt(10).status == GreeterPlaybackStatus::NotStarted,
             "Greeter playback fabricated start");
        Need(playback.Start(13) && !playback.Start(20), "Greeter playback accepted restart");
        const auto active = playback.SampleAt(14), done = playback.SampleAt(30);
        Need(active.status == GreeterPlaybackStatus::Playing && done.status == GreeterPlaybackStatus::Finished,
             "Greeter playback status mismatch");
        Near(active.elapsed, 1); Near(active.committedStartTime, 13);
        Refuses([&] { playback.SampleAt(12); });
    });
    test([] {
        const auto animation = Fixture();
        Refuses([&] { animation.SamplePose(std::numeric_limits<double>::quiet_NaN()); });
        Refuses([&] { animation.SamplePose(std::numeric_limits<double>::infinity()); });
        GreeterAnimationPlayback playback(animation);
        Refuses([&] { playback.Start(-1); });
        Refuses([&] { playback.SampleAt(std::numeric_limits<double>::infinity()); });
    });
    for (unsigned altered = 0; altered < 23; ++altered) {
        test([altered] {
            ClipFixture clip;
            SkeletonFixture skel;
            if (altered == 0) Set(clip.pack.data, clip.object + 16, 51);
            if (altered == 1) Set(clip.pack.data, clip.object + 36, 181);
            if (altered == 2) Set(clip.pack.data, clip.object + 40, 2);
            if (altered == 3) Float(clip.pack.data, clip.object + 60, .05f);
            if (altered == 4) clip.pack.data[clip.data] = 0;
            if (altered == 5) clip.pack.data[clip.data + 3] = 1;
            if (altered == 6) clip.pack.data[clip.data + 4] = 0x46;
            if (altered == 7) clip.pack.data[clip.data + 208] = 0;
            if (altered == 8) clip.pack.data[clip.data + clip.vectorHeader + 2] = 2;
            if (altered == 9) clip.pack.data[clip.data + clip.quaternionOffset + 4] |= 0x80;
            if (altered == 10) Set(clip.pack.data, clip.floatOffset, 216);
            if (altered == 11) Set(clip.pack.data, clip.object + 116, 0xffffffffu);
            if (altered == 12) skel.pack.data[skel.parents + 4] = 2;
            if (altered == 13) Float(skel.pack.data, skel.reference + 32, 2);
            if (altered == 14) Set(skel.pack.data, skel.skeleton + 8, 51);
            if (altered == 15) clip.pack.pointers.erase(clip.object + 76);
            if (altered == 16) clip.pack.data[clip.data + clip.vectorHeader + 3] = 1;
            if (altered == 17) clip.pack.data[clip.data + clip.quaternionOffset] = 255;
            if (altered == 17) clip.pack.data[clip.data + clip.quaternionOffset + 1] |= 15;
            if (altered == 18) clip.pack.data[clip.data + 2] = 0x80;
            if (altered == 19) Set(clip.pack.data, clip.floatOffset,
                                   std::uint32_t(clip.pack.data.size() - clip.data));
            if (altered == 20) clip.pack.objects[clip.object] = "unsupportedAnimationClass";
            if (altered == 21) Float(skel.pack.data, skel.reference,
                                     std::numeric_limits<float>::quiet_NaN());
            if (altered == 22) {
                const auto name = skel.pack.pointers.at(skel.skeleton);
                skel.pack.data[name] = 'X';
            }
            Refuses([&] { GreeterAnimation::Load(clip.pack.Finish(), skel.pack.Finish()); });
        });
    }
    for (unsigned limit = 0; limit < 7; ++limit) {
        test([limit] {
            ClipFixture clip;
            SkeletonFixture skel;
            GreeterAnimationLimits limits;
            if (limit == 0) limits.sourceBytes = 100;
            if (limit == 1) limits.objects = 1;
            if (limit == 2) limits.fixups = 1;
            if (limit == 3) limits.tableEntries = 0;
            if (limit == 4) limits.curveControls = 1;
            if (limit == 5) limits.totalControls = 1;
            if (limit == 6) limits.compressedClipBytes = 100;
            Refuses([&] { GreeterAnimation::Load(clip.pack.Finish(), skel.pack.Finish(), limits); });
        });
    }
    std::cout << "Greeter animation tests passed: " << tests
              << " (loaded joint poses; explicit start clock; auxiliary data opaque)\n";
    return 0;
}
} // namespace wolf2009

#ifdef WOLF2009_GREETER_ANIMATION_TEST_MAIN
#include <fstream>
#include <iterator>
namespace {
std::vector<unsigned char> Read(const char *path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open caller's private animation fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int Real(int argc, char **argv) {
    const auto animation = wolf2009::GreeterAnimation::Load(Read(argv[1]), Read(argv[2]));
    std::size_t points = 0;
    double maximumError = 0;
    if (argc == 4) {
        // Caller-supplied read-only measurements; no commercial fixtures or
        // mission coordinates are included in the public source tree.
        std::ifstream csv(argv[3]);
        if (!csv)
            throw std::runtime_error("Cannot open private model-joint measurement CSV");
        std::string line;
        while (std::getline(csv, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            if (line.size() > 512)
                throw std::runtime_error("Excessive measurement line");
            for (char &c : line)
                if (c == ',')
                    c = ' ';
            std::istringstream row(line);
            double seconds, x, y, z;
            std::size_t joint;
            std::string extra;
            if (!(row >> seconds >> joint >> x >> y >> z) || (row >> extra) || joint >= 52 ||
                !std::isfinite(seconds) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                throw std::runtime_error("Malformed private joint measurement row");
            const auto pose = animation.SamplePose(seconds).model[joint];
            const auto &p = pose.translation;
            const double error = std::sqrt((p[0]-x)*(p[0]-x) + (p[1]-y)*(p[1]-y) + (p[2]-z)*(p[2]-z));
            if (error > .012)
                throw std::runtime_error("Greeter model joint differs from supplied PC measurement");
            maximumError = std::max(maximumError, error);
            ++points;
        }
        if (!points)
            throw std::runtime_error("No supplied PC measurement rows");
    }
    std::cout << "Real greeter joint fixture PASS frames=" << animation.FrameCount()
              << " joints=" << animation.Joints().size() << " measuredPoints=" << points
              << " maxModelPositionError=" << maximumError << " auxiliaryBytes="
              << animation.OpaqueAuxiliaryBytes() << " startBlendImplemented=0\n";
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 1)
            return wolf2009::RunGreeterAnimationTests();
        if (argc == 3 || argc == 4)
            return Real(argc, argv);
        throw std::runtime_error("Usage: greeter-test [map.HKXA global.SKEL [model-joints.csv]]");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif

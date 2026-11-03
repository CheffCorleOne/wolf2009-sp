// SPDX-License-Identifier: GPL-3.0-or-later
#include "DoorModel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
bool Near(double a, double b) { return std::abs(a - b) < 1e-5; }
void Text(Bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); }
void U(Bytes &b, std::uint32_t u) {
    for (unsigned i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(u >> (i * 8)));
}
void F(Bytes &b, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    U(b, u);
}
void Set(Bytes &b, std::size_t at, std::uint32_t u) {
    Need(at + 4 <= b.size(), "Synthetic pack writer range");
    for (unsigned i = 0; i < 4; ++i)
        b[at + i] = static_cast<unsigned char>(u >> (8 * i));
}
void PutFloat(Bytes &b, std::size_t at, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    Set(b, at, u);
}
struct Pack {
    Bytes data, classes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pointers, objects;
    std::map<std::string, std::uint32_t> types;
    std::uint32_t Allocate(unsigned bytes) {
        const auto p = data.size();
        data.resize(p + bytes);
        return std::uint32_t(p);
    }
    std::uint32_t String(const std::string &s) {
        const auto p = Allocate(unsigned(s.size() + 1));
        std::copy(s.begin(), s.end(), data.begin() + p);
        return p;
    }
    std::uint32_t Type(const std::string &s) {
        const auto i = types.find(s);
        if (i != types.end())
            return i->second;
        const auto p = classes.size();
        Text(classes, s);
        classes.push_back(0);
        types[s] = std::uint32_t(p);
        return std::uint32_t(p);
    }
    std::uint32_t Object(const std::string &type, unsigned bytes) {
        const auto p = Allocate(bytes);
        objects.push_back({p, Type(type)});
        return p;
    }
    void Pointer(unsigned source, unsigned target) { pointers.push_back({source, target}); }
    void NamedVariant(unsigned slot, const std::string &name, const std::string &type, unsigned object) {
        Pointer(slot, String(name));
        Pointer(slot + 4, String(type));
        Pointer(slot + 8, object);
    }
    Bytes Finish(unsigned root) {
        while (data.size() % 16)
            data.push_back(0);
        const unsigned local = unsigned(data.size());
        for (const auto &p : pointers) {
            U(data, p.first);
            U(data, p.second);
        }
        while (data.size() % 16)
            data.push_back(255);
        const unsigned global = unsigned(data.size()), virtuals = global;
        for (const auto &o : objects) {
            U(data, o.first);
            U(data, 0);
            U(data, o.second);
        }
        while (data.size() % 16)
            data.push_back(255);
        const unsigned end = unsigned(data.size());
        Bytes out(208);
        Set(out, 0, 0x57e0e057);
        Set(out, 4, 0x10c0c010);
        Set(out, 12, 5);
        out[16] = 4;
        out[17] = 1;
        out[18] = 0;
        out[19] = 1;
        Set(out, 20, 3);
        Set(out, 24, 2);
        Set(out, 28, root);
        Set(out, 32, 0);
        Set(out, 36, types.at("hkRootLevelContainer"));
        const std::string version = "Havok-5.6.0-r1";
        std::copy(version.begin(), version.end(), out.begin() + 40);
        while (classes.size() % 16)
            classes.push_back(0);
        const unsigned dataStart = 208 + unsigned(classes.size());
        const char *names[] = {"__classnames__", "__types__", "__data__"};
        for (unsigned i = 0; i < 3; ++i) {
            const unsigned h = 64 + 48 * i;
            const std::string n = names[i];
            std::copy(n.begin(), n.end(), out.begin() + h);
            Set(out, h + 20, i == 0 ? 208 : dataStart);
            if (i == 0)
                for (unsigned j = 24; j <= 44; j += 4)
                    Set(out, h + j, unsigned(classes.size()));
            if (i == 2) {
                Set(out, h + 24, local);
                Set(out, h + 28, global);
                Set(out, h + 32, virtuals);
                Set(out, h + 36, end);
                Set(out, h + 40, end);
                Set(out, h + 44, end);
            }
        }
        out.insert(out.end(), classes.begin(), classes.end());
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }
};
struct AnimationFixture {
    Bytes hkxa, skel;
    unsigned idle = 0, move = 0, reference = 0;
};
AnimationFixture SyntheticAnimation() {
    AnimationFixture f;
    const std::string prefix = "models/props/train_door/";
    Pack p;
    const auto root = p.Object("hkRootLevelContainer", 16), variants = p.Allocate(32),
               container = p.Object("hkaAnimationContainer", 32),
               names = p.Object("rvHkStringContainer", 8), array = p.Allocate(12),
               strings = p.Allocate(12);
    p.Pointer(root, variants);
    Set(p.data, root + 4, 2);
    p.NamedVariant(variants, "Animation Container", "hkaAnimationContainer", container);
    p.NamedVariant(variants + 16, "animNames", "rvHkStringContainer", names);
    p.Pointer(container + 8, array);
    Set(p.data, container + 12, 3);
    p.Pointer(names, strings);
    Set(p.data, names + 4, 3);
    const char *files[] = {"train_door_pose.hkx", "srp_tyd_nml_train_door_anim.hkx",
                           "train_door_end.hkx"};
    for (unsigned i = 0; i < 3; ++i) {
        const auto obj = p.Object("hkaSplineSkeletalAnimation", 128), blocks = p.Allocate(4),
                   floats = p.Allocate(4), payload = p.Allocate(96);
        p.Pointer(array + 4 * i, obj);
        p.Pointer(strings + 4 * i, p.String(prefix + files[i]));
        Set(p.data, obj + 8, 5);
        PutFloat(p.data, obj + 12, 1.f / 24);
        Set(p.data, obj + 16, 2);
        Set(p.data, obj + 20, 6);
        Set(p.data, obj + 36, 2);
        Set(p.data, obj + 40, 1);
        Set(p.data, obj + 44, 256);
        Set(p.data, obj + 48, 16);
        PutFloat(p.data, obj + 60, 1.f / 24);
        p.Pointer(obj + 64, blocks);
        Set(p.data, obj + 68, 1);
        p.Pointer(obj + 76, floats);
        Set(p.data, obj + 80, 1);
        p.Pointer(obj + 112, payload);
        Set(p.data, obj + 116, 96);
        Set(p.data, payload, i == 1 ? 0x000f0045 : 0x00090045);
        Set(p.data, payload + 4, i == 1 ? 0x00401645 : 0x00000745);
        // Generated nearby X-axis rotations, deliberately distinct from the
        // original asset. Three coordinates plus omitted-component index.
        const std::uint64_t q = i == 1 ? (2047ull | (2047ull << 12) | (4092ull << 24))
                                       : (4092ull | (2047ull << 12) | (2047ull << 24) | (3ull << 36));
        for (unsigned j = 0; j < 5; ++j)
            p.data[payload + 16 + j] = static_cast<unsigned char>(q >> (8 * j));
        if (i == 1) {
            f.move = payload;
            Set(p.data, floats, 52);
            p.data[payload + 24] = 1;
            p.data[payload + 26] = 1;
            p.data[payload + 27] = 0;
            p.data[payload + 28] = 0;
            p.data[payload + 29] = 1;
            p.data[payload + 30] = 1;
            PutFloat(p.data, payload + 32, -4);
            PutFloat(p.data, payload + 36, 5);
            PutFloat(p.data, payload + 40, 7);
            PutFloat(p.data, payload + 44, 11);
            p.data[payload + 48] = 255;
            p.data[payload + 49] = 255;
        } else {
            if (i == 0)
                f.idle = payload;
            Set(p.data, floats, 36);
            PutFloat(p.data, payload + 24, i == 0 ? 5.f : -4.f);
            PutFloat(p.data, payload + 28, 7);
            PutFloat(p.data, payload + 32, 11);
        }
    }
    f.hkxa = p.Finish(root);
    Pack s;
    const auto sr = s.Object("hkRootLevelContainer", 16), sv = s.Allocate(16),
               sc = s.Object("hkaAnimationContainer", 32), sa = s.Allocate(4),
               skeleton = s.Object("hkaSkeleton", 32), parents = s.Allocate(4), bones = s.Allocate(8),
               reference = s.Allocate(96);
    s.Pointer(sr, sv);
    Set(s.data, sr + 4, 1);
    s.NamedVariant(sv, "Animation Container", "hkaAnimationContainer", sc);
    s.Pointer(sc, sa);
    Set(s.data, sc + 4, 1);
    s.Pointer(sa, skeleton);
    s.Pointer(skeleton, s.String(prefix + "train_door_anim_skel.hkx"));
    s.Pointer(skeleton + 4, parents);
    Set(s.data, parents, 0x0000ffff);
    Set(s.data, skeleton + 8, 2);
    s.Pointer(skeleton + 12, bones);
    Set(s.data, skeleton + 16, 2);
    s.Pointer(skeleton + 20, reference);
    Set(s.data, skeleton + 24, 2);
    for (unsigned j = 0; j < 2; ++j) {
        const auto bone = s.Object("hkaBone", 8);
        s.Pointer(bones + 4 * j, bone);
        s.Pointer(bone, s.String(j ? "door_joint" : "origin"));
        PutFloat(s.data, reference + 48 * j + 28, 1);
        for (unsigned k = 0; k < 3; ++k)
            PutFloat(s.data, reference + 48 * j + 32 + 4 * k, 1);
    }
    PutFloat(s.data, reference + 48, 10);
    PutFloat(s.data, reference + 52, 20);
    PutFloat(s.data, reference + 56, 30);
    f.reference = reference;
    f.skel = s.Finish(sr);
    return f;
}
unsigned DataStart(const Bytes &b) {
    return unsigned(b[180]) | (unsigned(b[181]) << 8) | (unsigned(b[182]) << 16) |
           (unsigned(b[183]) << 24);
}
struct Fixture {
    Bytes bytes;
    std::size_t blend = 0, position = 0, index = 0;
};
Fixture Synthetic() {
    Fixture f;
    auto &b = f.bytes;
    Text(b, "MemSize 1000 MD5RVersion 1 Joint[2]{\"origin\" -1 0 0 0 0 0 0 \"door_joint\" 0 10 20 30 0 "
            "0 0 } VertexBuffer[3]{ VertexBuffer{VertexFormat{Position 3 BlendIndex} SystemMemory "
            "Vertex[3]{}} VertexBuffer{VertexFormat{Position 3 BlendIndexSmall Normal Tangent Binormal "
            "DiffuseColor TexCoord 2 0} LoadVertexFormat{Position 3 BlendIndex Normal Tangent Binormal "
            "DiffuseColor TexCoord 2 0} SystemMemory VideoMemory Vertex[3]{bin 192\n");
    f.position = b.size();
    f.blend = b.size() + 12;
    for (unsigned i = 0; i < 3; ++i) {
        F(b, 10.f + (i == 1));
        F(b, 20.f + (i == 2));
        F(b, 30);
        U(b, 0);
        for (unsigned j = 0; j < 9; ++j)
            F(b, float(j % 4 == 0));
        U(b, 0x78563412);
        F(b, float(i));
        F(b, .5f);
    }
    Text(b, "}} VertexBuffer{VertexFormat{Position 3 BlendIndex} VideoMemory Vertex[6]{}}} "
            "IndexBuffer[2]{IndexBuffer{SystemMemory BitDepth 16 Index[3]{bin 6\n");
    b.insert(b.end(), {0, 0, 1, 0, 2, 0});
    Text(b, "}} IndexBuffer{SystemMemory VideoMemory BitDepth 16 Index[3]{bin 6\n");
    f.index = b.size();
    b.insert(b.end(), {0, 0, 1, 0, 2, 0});
    Text(b, "}}} SilhouetteEdge[1]{bin 16\n");
    b.insert(b.end(), 16, 0);
    Text(b, "} Mesh[1]{Mesh{Material\"synthetic/metal\" // private generated data\n SilTraceBuffers 0 0 "
            "DrawBuffers 1 1 ShadowVolumeBuffers 2 -1 PrimBatch[1]{PrimBatch{Transform[1]{1} "
            "SilTraceIndexedTriList 0 3 0 1 DrawIndexedTriList 0 3 0 1 ShadowVerts 0 SilhouetteEdge 0 "
            "1}}}} Bounds 10 20 30 11 21 30");
    return f;
}
void Replace(Bytes &b, const std::string &from, const std::string &to) {
    std::string s(b.begin(), b.end());
    const auto p = s.find(from);
    Need(p != std::string::npos, "Synthetic replacement missing");
    s.replace(p, from.size(), to);
    b.assign(s.begin(), s.end());
}
template <class F> void Reject(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    Need(rejected, "Unsupported door data accepted");
}
} // namespace
int RunDoorModelTests() {
    unsigned passed = 0;
    try {
        const auto f = Synthetic();
        const auto model = DecodeRigidDoorModel1(f.bytes, "synthetic/door");
        Need(model.bindGeometry.vertices[0].diffuseColor == 0x78563412u,
             "Door model lost raw diffuse color");
        Need(model.bindGeometry.vertices.size() == 3 && model.bindGeometry.surfaces.size() == 1 &&
                 model.bindGeometry.surfaces[0].indices.size() == 3 &&
                 model.bindGeometry.surfaces[0].material == "synthetic/metal",
             "Door source attributes");
        ++passed;
        auto identity = PoseRigidDoor(model, model.bindJoint);
        Need(Near(identity.vertices[1].x, 11) && Near(identity.vertices[2].y, 21) &&
                 Near(identity.vertices[1].u, 1),
             "Bind pose was applied twice");
        ++passed;
        DoorJointPose pose;
        pose.translation = {{100, 200, 300}};
        auto placed = PoseRigidDoor(model, pose);
        Need(Near(placed.vertices[0].x, 100) && Near(placed.vertices[1].x, 101) &&
                 Near(placed.vertices[2].y, 201),
             "Rigid inverse-bind translation");
        ++passed;
        pose.rotation = {{0, 0, std::sqrt(.5), std::sqrt(.5)}};
        placed = PoseRigidDoor(model, pose);
        Need(Near(placed.vertices[1].x, 100) && Near(placed.vertices[1].y, 201) &&
                 Near(placed.vertices[2].x, 99),
             "Rigid inverse-bind rotation");
        ++passed;
        Need(placed.surfaces[0].indices == model.bindGeometry.surfaces[0].indices &&
                 Near(placed.vertices[2].v, .5),
             "Posing changed UV/topology");
        ++passed;
        {
            const auto matrix = RigidDoorSkinMatrix(model.bindJoint, pose);
            const auto &v = model.bindGeometry.vertices[2];
            Need(Near(matrix[0] * v.x + matrix[4] * v.y + matrix[8] * v.z + matrix[12],
                      placed.vertices[2].x) &&
                     Near(matrix[1] * v.x + matrix[5] * v.y + matrix[9] * v.z + matrix[13],
                          placed.vertices[2].y) &&
                     matrix[15] == 1,
                 "GPU column-major inverse bind matrix");
            ++passed;
        }
        {
            auto b = f.bytes;
            b[f.blend] = 1;
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            b[f.index] = 3;
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            b.resize(b.size() - 7);
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            Replace(b, "bin 192\n", "bin 193\n");
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            Replace(b, "Transform[1]{1}", "Transform[1]{0}");
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            Replace(b, "DrawBuffers 1 1", "DrawBuffers 0 1");
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            Replace(b, "Bounds 10 20 30 11 21 30", "Bounds 11 20 30 10 21 30");
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            Replace(b, "Bounds 10 20 30 11 21 30", "Bounds 10 20 30 10 21 30");
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            Text(b, " Unsupported 1");
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            auto b = f.bytes;
            b[f.position] = 0;
            b[f.position + 1] = 0;
            b[f.position + 2] = 0xc0;
            b[f.position + 3] = 0x7f;
            Reject([&] { DecodeRigidDoorModel1(b, "synthetic/door"); });
            ++passed;
        }
        {
            GeometryLimits l;
            l.vertices = 2;
            Reject([&] { DecodeRigidDoorModel1(f.bytes, "synthetic/door", l); });
            ++passed;
        }
        {
            GeometryLimits l;
            l.bufferBytes = 100;
            Reject([&] { DecodeRigidDoorModel1(f.bytes, "synthetic/door", l); });
            ++passed;
        }
        {
            auto bad = pose;
            bad.rotation = {{0, 0, 0, 2}};
            Reject([&] { PoseRigidDoor(model, bad); });
            ++passed;
        }
        {
            auto bad = pose;
            bad.translation[0] = std::numeric_limits<double>::quiet_NaN();
            Reject([&] { PoseRigidDoor(model, bad); });
            ++passed;
        }
        {
            Bytes empty;
            Reject([&] { DoorAnimation::Load(empty, empty); });
            ++passed;
        }
        const auto af = SyntheticAnimation();
        const auto animation = DoorAnimation::Load(af.hkxa, af.skel);
        Need(Near(animation.ClosedX(), 5) && Near(animation.OpenX(), -4) &&
                 Near(animation.LocalX(animation.Duration() / 2), .5),
             "Synthetic door movement curve");
        ++passed;
        const auto door = DoorModel::Load(f.bytes, "synthetic/door", animation);
        Need(Near(door.ClosedGeometry().vertices[0].x, animation.ClosedPose().translation[0]) &&
                 Near(door.ClosedGeometry().vertices[0].y, animation.ClosedPose().translation[1]),
             "Closed mesh did not use inverse bind");
        ++passed;
        const auto active = animation.SamplePose(0), late = animation.SamplePose(100);
        Need(Near(late.translation[0], -4) && Near(animation.SamplePose(-100).translation[0], 5) &&
                 active.rotation != animation.ClosedPose().rotation,
             "Active/end/idle phases conflated");
        ++passed;
        Need(Near(animation.EndIdlePose().translation[0], -4) &&
                 animation.EndIdlePose().rotation == animation.ClosedPose().rotation,
             "Explicit end idle pose");
        ++passed;
        Need(std::abs(door.ClosedGeometry().vertices[0].x - model.bindGeometry.vertices[0].x) > 1,
             "Bind incorrectly assumed idle");
        ++passed;
        {
            Reject([&] { animation.SamplePose(std::numeric_limits<double>::quiet_NaN()); });
            ++passed;
        }
        {
            auto b = af.hkxa;
            b[DataStart(b) + af.idle + 20] |= 128;
            Reject([&] { DoorAnimation::Load(b, af.skel); });
            ++passed;
        }
        {
            auto b = af.hkxa;
            b[DataStart(b) + af.move + 29] = 0;
            Reject([&] { DoorAnimation::Load(b, af.skel); });
            ++passed;
        }
        {
            auto b = af.hkxa;
            b[DataStart(b) + af.move + 2] = 0;
            Reject([&] { DoorAnimation::Load(b, af.skel); });
            ++passed;
        }
        {
            auto b = af.skel;
            PutFloat(b, DataStart(b) + af.reference + 32, 2);
            Reject([&] { DoorAnimation::Load(af.hkxa, b); });
            ++passed;
        }
        {
            auto b = af.skel;
            PutFloat(b, DataStart(b) + af.reference + 64, .1f);
            Reject([&] { DoorAnimation::Load(af.hkxa, b); });
            ++passed;
        }
        {
            auto b = af.hkxa;
            b.pop_back();
            Reject([&] { DoorAnimation::Load(b, af.skel); });
            ++passed;
        }
        {
            auto b = f.bytes;
            Replace(b, "0 10 20 30 0 0 0", "0 11 20 30 0 0 0");
            Reject([&] { DoorModel::Load(b, "synthetic/door", animation); });
            ++passed;
        }
        {
            auto b = af.hkxa;
            PutFloat(b, DataStart(b) + af.move + 40, std::numeric_limits<float>::infinity());
            Reject([&] { DoorAnimation::Load(b, af.skel); });
            ++passed;
        }
        {
            auto b = af.hkxa;
            b[DataStart(b) + af.idle + 18] ^= 1;
            Reject([&] { DoorAnimation::Load(b, af.skel); });
            ++passed;
        }
        {
            GeometryLimits l;
            l.sourceBytes = 3;
            Reject([&] { DecodeRigidDoorModel1(f.bytes, "synthetic/door", l); });
            ++passed;
        }
        std::cout << "DoorModel: " << passed << "/37 passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "DoorModel failed after " << passed << ": " << e.what() << "\n";
        return 1;
    }
}
} // namespace wolf2009

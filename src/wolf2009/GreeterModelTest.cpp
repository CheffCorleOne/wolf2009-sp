// SPDX-License-Identifier: GPL-3.0-or-later
#include "GreeterModel.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Near(double value, double expected) {
    Need(std::abs(value - expected) < 1e-5, "Greeter CPU skinning numerical mismatch");
}
void Refuses(const std::function<void()> &action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    Need(refused, "Unsupported skinned body accepted");
}
void Text(Bytes &bytes, const std::string &text) { bytes.insert(bytes.end(), text.begin(), text.end()); }
void Float(Bytes &bytes, std::size_t at, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    Need(at + 4 <= bytes.size(), "Synthetic vertex write range");
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<unsigned char>(bits >> (8 * i));
}
std::vector<GreeterJoint> Skeleton() {
    std::vector<GreeterJoint> result(52);
    for (unsigned i = 0; i < 52; ++i) {
        result[i].name = "joint_" + std::to_string(i);
        result[i].parent = i ? 0 : -1;
    }
    return result;
}
struct Fixture {
    Bytes data;
    std::size_t vertices = 0, indices = 0;
    explicit Fixture(bool shiftedDraw = false) {
        Text(data, "MemSize 1024\nMD5RVersion 1\nJoint [52] {\n");
        for (unsigned i = 0; i < 52; ++i)
            Text(data, "\"joint_" + std::to_string(i) + "\" " + (i ? "0" : "-1") +
                           (i == 2 ? " 10 0 0 0 0 0\n" : " 0 0 0 0 0 0\n"));
        Text(data, "}\nVertexBuffer [1] { VertexBuffer { VertexFormat { Position 3 BlendIndex "
                   "BlendWeight 3 4 Normal Tangent DiffuseColor TexCoord 2 0 } SystemMemory "
                   "VideoMemory Vertex [" +
                       std::string(shiftedDraw ? "4] { bin 256\n" : "3] { bin 192\n"));
        vertices = data.size() + (shiftedDraw ? 64 : 0);
        data.resize(data.size() + (shiftedDraw ? 256 : 192));
        for (unsigned i = 0; i < 3; ++i) {
            Float(data, vertices + 64 * i, i ? 1.f : 10.f);
            Float(data, vertices + 64 * i + 56, .25f);
            Float(data, vertices + 64 * i + 60, .75f);
            data[vertices + 64 * i + 52] = 0x12;
            data[vertices + 64 * i + 53] = 0x34;
            data[vertices + 64 * i + 54] = 0x56;
            data[vertices + 64 * i + 55] = 0x78;
        }
        data[vertices + 13] = 1;
        data[vertices + 15] = 1;
        Float(data, vertices + 16, -.25f);
        Float(data, vertices + 20, .25f);
        Float(data, vertices + 24, .25f);
        data[vertices + 64 + 12] = 1;
        Float(data, vertices + 64 + 16, -1);
        Float(data, vertices + 128 + 16, 1);
        Text(data,
             "\n} } } IndexBuffer [1] { IndexBuffer { SystemMemory BitDepth 16 Index [3] { bin 6\n");
        indices = data.size();
        if (shiftedDraw)
            data.insert(data.end(), {1, 0, 2, 0, 3, 0});
        else
            data.insert(data.end(), {0, 0, 1, 0, 2, 0});
        Text(data, "\n} } } SilhouetteEdge [0] { } LevelOfDetail [1] { 200 } Mesh [1] { Mesh { "
                   "LevelOfDetail 0 Material \"materials/body\" SilTraceBuffers 0 0 DrawBuffers 0 0 "
                   "ShadowVolumeBuffers -1 -1 PrimBatch [1] { PrimBatch { Transform [2] { 1 2 } "
                   "SilTraceIndexedTriList 0 3 0 1 DrawIndexedTriList " +
                       std::string(shiftedDraw ? "1" : "0") +
                       " 3 0 1 ShadowVerts 0 "
                       "SilhouetteEdge 0 0 } } } } Bounds -10 -10 -10 20 20 20\n");
    }
};
Bytes Replace(const Bytes &source, const std::string &before, const std::string &after) {
    std::string value(source.begin(), source.end());
    const auto at = value.find(before);
    Need(at != std::string::npos, "Missing synthetic body token");
    value.replace(at, before.size(), after);
    return {value.begin(), value.end()};
}
GreeterBodyGeometry Decode(const Fixture &fixture) {
    return DecodeGreeterBody1(fixture.data, "models/test/body", Skeleton(), 0);
}
} // namespace

int RunGreeterModelTests() {
    unsigned passed = 0;
    const auto test = [&passed](const std::function<void()> &action) {
        action();
        ++passed;
    };
    test([] {
        const auto body = Decode(Fixture());
        Need(body.bindGeometry.vertices.size() == 3 && body.bindGeometry.surfaces.size() == 1 &&
                 body.bindGeometry.surfaces[0].indices.size() == 3 && body.levelOfDetail == 0,
             "Body geometry extraction mismatch");
        Need(body.influences[0].mirroredTangent && body.influences[0].joints[0] == 1 &&
                 body.influences[0].joints[1] == 2,
             "Palette/sign extraction mismatch");
        for (double weight : body.influences[0].weights)
            Near(weight, .25);
        GreeterPose pose;
        pose.model = body.bindModelJoints;
        const auto identity = PoseGreeterBody(body, pose);
        for (unsigned i = 0; i < 3; ++i)
            Near(identity.vertices[i].x, body.bindGeometry.vertices[i].x);
        pose.model[1].translation[0] = 2;
        pose.model[2].translation[0] = 14;
        const auto result = PoseGreeterBody(body, pose);
        Near(result.vertices[0].x, 13);
        Near(result.vertices[1].x, 5);
        Near(result.vertices[2].x, 3);
        Near(result.vertices[0].u, .25);
        Need(body.bindGeometry.vertices[0].diffuseColor == 0x78563412u &&
                 result.vertices[0].diffuseColor == 0x78563412u,
             "Skinning changed raw diffuse color");
        Need(result.surfaces[0].material == "materials/body", "Skinning changed material identity");
    });
    test([] {
        auto body = Decode(Fixture());
        GreeterPose pose;
        pose.model = body.bindModelJoints;
        pose.model[1].rotation = {{0, 0, std::sqrt(.5), std::sqrt(.5)}};
        const auto result = PoseGreeterBody(body, pose);
        Near(result.vertices[2].x, 0);
        Near(result.vertices[2].y, 1);
    });
    test([] {
        const auto body = Decode(Fixture(true));
        Need(body.bindGeometry.vertices.size() == 3 &&
                 body.bindGeometry.surfaces[0].indices == std::vector<std::uint32_t>({0, 1, 2}),
             "Absolute MD5R index did not map to selected batch output range");
        Near(body.bindGeometry.vertices[0].x, 10);
    });
    test([] {
        Fixture fixture;
        Float(fixture.data, fixture.vertices + 16, -0.f);
        const auto body = Decode(fixture);
        Need(!body.influences[0].mirroredTangent, "Shader's zero sign treated as negative handedness");
        Near(body.influences[0].weights[0], 0);
        Near(body.influences[0].weights[3], .5);
    });
    for (const auto &change : std::vector<std::pair<std::string, std::string>>{
             {"MD5RVersion 1", "MD5RVersion 2"},
             {"Joint [52]", "Joint [51]"},
             {"joint_1\" 0", "joint_1\" 1"},
             {"DrawBuffers 0 0", "DrawBuffers 1 0"},
             {"Transform [2] { 1 2", "Transform [2] { 1 1"},
             {"Transform [2] { 1 2", "Transform [2] { 1 52"},
             {"BlendWeight 3 4", "BlendWeight 4 4"},
             {"BitDepth 16", "BitDepth 32"},
             {"DrawIndexedTriList 0 3 0 1", "DrawIndexedTriList 0 4 0 1"},
             {"DrawIndexedTriList 0 3 0 1", "DrawIndexedTriList 0 3 1 1"},
             {"200 } Mesh", "0 } Mesh"},
             {"Bounds -10", "Bounds 100"},
             {"bin 192", "bin 193"}}) {
        test([change] {
            const auto data = Replace(Fixture().data, change.first, change.second);
            Refuses([&] { DecodeGreeterBody1(data, "models/test/body", Skeleton(), 0); });
        });
    }
    for (unsigned altered = 0; altered < 5; ++altered) {
        test([altered] {
            Fixture fixture;
            if (altered == 0)
                Float(fixture.data, fixture.vertices + 20, -1);
            if (altered == 1)
                Float(fixture.data, fixture.vertices + 16, 2);
            if (altered == 2)
                fixture.data[fixture.vertices + 12] = 2;
            if (altered == 3)
                Float(fixture.data, fixture.vertices, std::numeric_limits<float>::quiet_NaN());
            if (altered == 4)
                fixture.data[fixture.indices] = 3;
            Refuses([&] { Decode(fixture); });
        });
    }
    test([] {
        const Fixture fixture;
        Refuses([&] { DecodeGreeterBody1(fixture.data, "models/test/body", Skeleton(), 1); });
        Refuses([&] { DecodeGreeterBody1(fixture.data, "../body", Skeleton(), 0); });
        auto skeleton = Skeleton();
        skeleton[1].name = "different";
        Refuses([&] { DecodeGreeterBody1(fixture.data, "models/test/body", skeleton, 0); });
    });
    for (unsigned limit = 0; limit < 8; ++limit) {
        test([limit] {
            const Fixture fixture;
            GeometryLimits limits;
            if (limit == 0)
                limits.sourceBytes = 10;
            if (limit == 1)
                limits.vertices = 2;
            if (limit == 2)
                limits.triangles = 0;
            if (limit == 3)
                limits.surfaces = 0;
            if (limit == 4)
                limits.syntaxNodes = 3;
            if (limit == 5)
                limits.bufferBytes = 100;
            if (limit == 6)
                limits.totalBufferBytes = 100;
            if (limit == 7)
                limits.models = 0;
            Refuses(
                [&] { DecodeGreeterBody1(fixture.data, "models/test/body", Skeleton(), 0, limits); });
        });
    }
    for (unsigned altered = 0; altered < 5; ++altered) {
        test([altered] {
            auto body = Decode(Fixture());
            GreeterPose pose;
            pose.model = body.bindModelJoints;
            if (altered == 0)
                pose.model.clear();
            if (altered == 1)
                body.influences.clear();
            if (altered == 2)
                body.influences[0].weights[0] = -1;
            if (altered == 3)
                pose.model[1].rotation[3] = 0;
            if (altered == 4)
                body.bindGeometry.surfaces[0].indices[0] = 3;
            Refuses([&] { PoseGreeterBody(body, pose); });
        });
    }
    test([] {
        GreeterHeadAttachment head;
        head.skeleton.resize(27);
        head.idleLocal.resize(27);
        for (unsigned i = 0; i < 27; ++i) {
            head.skeleton[i].name = "head_joint_" + std::to_string(i);
            head.skeleton[i].parent = i ? int(i - 1) : -1;
        }
        head.actorJoints = {{12, 13, 31, 32}};
        head.idleLocal[4].translation = {{1, 0, 0}};
        GreeterPose actor;
        actor.model.resize(52);
        actor.model[12].translation = {{0, 0, 50}};
        actor.model[13].translation = {{0, 0, 55}};
        actor.model[31].translation = {{0, 0, 60}};
        actor.model[32].translation = {{0, 0, 64}};
        actor.model[32].rotation = {{0, 0, std::sqrt(.5), std::sqrt(.5)}};
        const auto pose = PoseGreeterHeadCurrentActor(head, actor);
        Near(pose.model[0].translation[2], 50);
        Near(pose.model[1].translation[2], 55);
        Near(pose.model[2].translation[2], 60);
        Near(pose.model[3].translation[2], 64);
        Near(pose.model[4].translation[0], 0);
        Near(pose.model[4].translation[1], 1);
        Near(pose.model[4].translation[2], 64);
        Near(pose.local[2].translation[2], 5);
        Need(!head.originalCopyTimingVerified && !head.facialAnimationSupported,
             "Head prototype claimed original timing/facial support");
    });
    test([] {
        GreeterHeadAttachment head;
        head.source = Decode(Fixture());
        head.source.bindModelJoints.resize(27);
        head.source.jointNames.resize(27);
        head.skeleton.resize(27);
        head.idleLocal.resize(27);
        for (unsigned i = 0; i < 27; ++i)
            head.skeleton[i].parent = i ? 0 : -1;
        head.actorJoints = {{12, 13, 31, 32}};
        GreeterPose actor;
        actor.model.resize(52);
        actor.model[12].translation[0] = 100;
        actor.model[13].translation[0] = 2;
        actor.model[31].translation[0] = 14;
        const auto geometry = PoseGreeterHeadGeometryCurrentActor(head, actor);
        Near(geometry.vertices[0].x, 13);
        Near(geometry.vertices[1].x, 5);
        Near(geometry.vertices[2].x, 3);
    });
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        test([invalid] {
            GreeterHeadAttachment head;
            head.skeleton.resize(27);
            head.idleLocal.resize(27);
            for (unsigned i = 0; i < 27; ++i)
                head.skeleton[i].parent = i ? 0 : -1;
            GreeterPose actor;
            actor.model.resize(52);
            if (invalid == 0)
                head.idleLocal.clear();
            if (invalid == 1)
                head.actorJoints[0] = 52;
            if (invalid == 2)
                head.skeleton[4].parent = 4;
            if (invalid == 3)
                actor.model[0].rotation[3] = 0;
            Refuses([&] { PoseGreeterHeadCurrentActor(head, actor); });
        });
    }
    test([] { Refuses([] { LoadGreeterHead1({}, "models/test/head", {}, {}, {}, 0); }); });
    std::cout << "Greeter body tests passed: " << passed
              << " (CPU skinning; head current-pose prototype; no face animation)\n";
    return 0;
}
} // namespace wolf2009

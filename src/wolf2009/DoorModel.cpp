// SPDX-License-Identifier: GPL-3.0-or-later
#include "DoorModel.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool b, const char *text) {
    if (!b)
        throw std::runtime_error(text);
}
struct Token {
    std::string text;
    std::size_t offset = 0, bytes = 0;
    bool binary = false, quoted = false;
};
class Reader {
    const Bytes &b;
    std::size_t p = 0, tokens = 0;

  public:
    Token t;
    explicit Reader(const Bytes &data) : b(data) { Next(); }
    void Next() {
        for (;;) {
            while (p < b.size() && (b[p] == ' ' || b[p] == '\t' || b[p] == '\r' || b[p] == '\n'))
                ++p;
            if (p + 1 < b.size() && b[p] == '/' && b[p + 1] == '/') {
                while (p < b.size() && b[p] != '\n')
                    ++p;
            } else
                break;
        }
        t = Token();
        if (p == b.size())
            return;
        Need(++tokens <= 100000, "Door model syntax token limit");
        if (b[p] == '"') {
            ++p;
            t.quoted = true;
            while (p < b.size() && b[p] != '"') {
                Need(b[p] >= 32 && b[p] <= 126 && t.text.size() < 4096,
                     "Door model quoted token invalid");
                t.text += char(b[p++]);
            }
            Need(p < b.size(), "Door model unterminated quote");
            ++p;
            return;
        }
        if (std::strchr("{}[]", b[p])) {
            t.text = char(b[p++]);
            return;
        }
        while (p < b.size() && b[p] > 32 && !std::strchr("{}[]\"", b[p])) {
            Need(b[p] <= 126 && t.text.size() < 4096, "Door model text token invalid");
            t.text += char(b[p++]);
        }
        Need(!t.text.empty(), "Door model invalid token");
        if (t.text != "bin")
            return;
        while (p < b.size() && (b[p] == ' ' || b[p] == '\t'))
            ++p;
        std::size_t n = 0, digits = 0;
        while (p < b.size() && b[p] >= '0' && b[p] <= '9') {
            Need(++digits <= 10 && n <= ((8u << 20) - unsigned(b[p] - '0')) / 10,
                 "Door binary size limit");
            n = n * 10 + unsigned(b[p++] - '0');
        }
        Need(digits > 0, "Door binary count absent");
        if (p < b.size() && b[p] == '\r')
            ++p;
        Need(p < b.size() && b[p++] == '\n' && n <= b.size() - p, "Door binary range invalid");
        t.binary = true;
        t.offset = p;
        t.bytes = n;
        p += n;
    }
    bool Is(const char *s) const { return !t.binary && !t.quoted && t.text == s; }
    void Eat(const char *s) {
        Need(Is(s), "Unsupported door model structure");
        Next();
    }
    double Real() {
        Need(!t.binary && !t.quoted && !t.text.empty() && t.text.size() <= 64,
             "Door numeric token invalid");
        errno = 0;
        char *e = NULL;
        double v = std::strtod(t.text.c_str(), &e);
        Need(e != t.text.c_str() && *e == 0 && errno != ERANGE && std::isfinite(v) && std::abs(v) <= 1e8,
             "Door numeric value invalid");
        Next();
        return v;
    }
    int Integer(int lo, int hi) {
        const double v = Real();
        Need(v >= lo && v <= hi && std::floor(v) == v, "Door count/index invalid");
        return int(v);
    }
    std::size_t Count(const char *s, std::size_t cap) {
        Eat(s);
        Eat("[");
        const auto n = Integer(0, int(cap));
        Eat("]");
        Eat("{");
        return std::size_t(n);
    }
    std::string Quote() {
        Need(t.quoted && !t.text.empty(), "Door name absent");
        const auto s = t.text;
        Next();
        return s;
    }
    Token Payload() {
        Token v = t;
        if (t.binary)
            Next();
        else {
            v = Token();
        }
        return v;
    }
};
double Float(const Bytes &b, std::size_t p) {
    Need(p <= b.size() && b.size() - p >= 4, "Door vertex read truncated");
    const std::uint32_t u = std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) |
                            (std::uint32_t(b[p + 2]) << 16) | (std::uint32_t(b[p + 3]) << 24);
    float f;
    std::memcpy(&f, &u, 4);
    Need(std::isfinite(f) && std::abs(f) <= 1e8, "Door nonfinite/excessive vertex component");
    return f;
}
std::uint32_t Unsigned(const Bytes &b, std::size_t p) {
    Need(p <= b.size() && b.size() - p >= 4, "Door blend read truncated");
    return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) | (std::uint32_t(b[p + 2]) << 16) |
           (std::uint32_t(b[p + 3]) << 24);
}
void Format(Reader &r, bool render, bool runtime) {
    r.Eat("{");
    r.Eat("Position");
    r.Eat("3");
    r.Eat(render && runtime ? "BlendIndexSmall" : "BlendIndex");
    if (render) {
        r.Eat("Normal");
        r.Eat("Tangent");
        r.Eat("Binormal");
        r.Eat("DiffuseColor");
        r.Eat("TexCoord");
        r.Eat("2");
        r.Eat("0");
    }
    r.Eat("}");
}
std::array<double, 4> Quaternion(std::array<double, 4> q) {
    double n = 0;
    for (double v : q) {
        Need(std::isfinite(v), "Door quaternion nonfinite");
        n += v * v;
    }
    Need(n > .999 && n < 1.001, "Door quaternion must be normalized");
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
} // namespace

RigidDoorGeometry DecodeRigidDoorModel1(const Bytes &data, const std::string &name,
                                        const GeometryLimits &limits) {
    Need(!name.empty() && name.size() <= 4096 && data.size() <= limits.sourceBytes &&
             data.size() <= (8u << 20),
         "Door model name/source limit");
    Reader r(data);
    r.Eat("MemSize");
    r.Integer(0, 1 << 28);
    r.Eat("MD5RVersion");
    r.Eat("1");
    Need(r.Count("Joint", 2) == 2, "Door requires two rigid joints");
    RigidDoorGeometry result;
    Need(r.Quote() == "origin", "Door root joint unsupported");
    Need(r.Integer(-1, 0) == -1, "Door root parent invalid");
    for (unsigned j = 0; j < 3; ++j)
        Need(std::abs(r.Real()) < 1e-6, "Door root translated");
    std::array<double, 4> root = {{0, 0, 0, 0}};
    double sum = 0;
    for (unsigned j = 0; j < 3; ++j) {
        root[j] = r.Real();
        sum += root[j] * root[j];
    }
    Need(sum <= 1, "Door root quaternion invalid");
    root[3] = -std::sqrt(1 - sum);
    result.jointName = r.Quote();
    Need(result.jointName == "door_joint" && r.Integer(0, 0) == 0, "Door child joint unsupported");
    for (unsigned j = 0; j < 3; ++j)
        result.bindJoint.translation[j] = r.Real();
    sum = 0;
    for (unsigned j = 0; j < 3; ++j) {
        result.bindJoint.rotation[j] = r.Real();
        sum += result.bindJoint.rotation[j] * result.bindJoint.rotation[j];
    }
    Need(sum <= 1, "Door bind quaternion invalid");
    result.bindJoint.rotation[3] = -std::sqrt(1 - sum);
    r.Eat("}");
    double rootDot = 0;
    for (unsigned j = 0; j < 4; ++j)
        rootDot += root[j] * result.bindJoint.rotation[j];
    Need(std::abs(rootDot) > .999999, "Door root/child bind orientation differs");
    Need(r.Count("VertexBuffer", 3) == 3, "Door vertex buffer profile unsupported");
    std::size_t counts[3] = {0, 0, 0};
    Token vertices;
    for (unsigned i = 0; i < 3; ++i) {
        r.Eat("VertexBuffer");
        r.Eat("{");
        r.Eat("VertexFormat");
        Format(r, i == 1, true);
        if (i == 1) {
            r.Eat("LoadVertexFormat");
            Format(r, true, false);
        }
        if (r.Is("SystemMemory"))
            r.Next();
        if (r.Is("VideoMemory"))
            r.Next();
        counts[i] = r.Count("Vertex", std::min<std::size_t>(limits.vertices, 100000));
        Need(counts[i] > 0, "Door empty vertex declaration");
        const auto bin = r.Payload();
        if (i == 1) {
            Need(bin.binary && bin.bytes == counts[i] * 64, "Door render buffer byte count");
            vertices = bin;
        } else
            Need(!bin.binary, "Door nonrender payload unsupported");
        r.Eat("}");
        r.Eat("}");
    }
    r.Eat("}");
    Need(r.Count("IndexBuffer", 2) == 2, "Door index buffer profile unsupported");
    Token indices[2];
    std::size_t ic[2];
    for (unsigned i = 0; i < 2; ++i) {
        r.Eat("IndexBuffer");
        r.Eat("{");
        if (r.Is("SystemMemory"))
            r.Next();
        if (r.Is("VideoMemory"))
            r.Next();
        r.Eat("BitDepth");
        r.Eat("16");
        ic[i] = r.Count("Index", std::min<std::size_t>(limits.triangles, 100000) * 3);
        indices[i] = r.Payload();
        Need(indices[i].binary && indices[i].bytes == ic[i] * 2, "Door index byte count");
        r.Eat("}");
        r.Eat("}");
    }
    r.Eat("}");
    const auto edges = r.Count("SilhouetteEdge", 100000);
    const auto edgePayload = r.Payload();
    Need(edgePayload.binary && edgePayload.bytes == edges * 16, "Door silhouette bytes invalid");
    r.Eat("}");
    Need(r.Count("Mesh", 1) == 1 && limits.surfaces >= 1 && limits.models >= 1,
         "Door render surface count");
    r.Eat("Mesh");
    r.Eat("{");
    r.Eat("Material");
    GeometrySurface surface;
    surface.model = name;
    surface.material = r.Quote();
    r.Eat("SilTraceBuffers");
    r.Eat("0");
    r.Eat("0");
    r.Eat("DrawBuffers");
    r.Eat("1");
    r.Eat("1");
    r.Eat("ShadowVolumeBuffers");
    r.Eat("2");
    r.Eat("-1");
    Need(r.Count("PrimBatch", 1) == 1, "Door multiple rigid batches unsupported");
    r.Eat("PrimBatch");
    r.Eat("{");
    Need(r.Count("Transform", 1) == 1, "Door joint palette count");
    r.Eat("1");
    r.Eat("}");
    std::size_t triangles = 0;
    for (unsigned i = 0; i < 2; ++i) {
        r.Eat(i ? "DrawIndexedTriList" : "SilTraceIndexedTriList");
        r.Eat("0");
        Need(std::size_t(r.Integer(1, 100000)) == counts[i ? 1 : 0], "Door batch vertex range mismatch");
        r.Eat("0");
        const auto n = std::size_t(r.Integer(1, 100000));
        Need(n <= limits.triangles && n * 3 == ic[i], "Door batch index count mismatch");
        if (i)
            triangles = n;
    }
    r.Eat("ShadowVerts");
    r.Eat("0");
    r.Eat("SilhouetteEdge");
    r.Eat("0");
    Need(std::size_t(r.Integer(0, 100000)) == edges, "Door silhouette count mismatch");
    r.Eat("}");
    r.Eat("}");
    r.Eat("}");
    r.Eat("}");
    r.Eat("Bounds");
    double bounds[6];
    for (double &v : bounds)
        v = r.Real();
    Need(r.t.text.empty() && !r.t.binary, "Door model trailing fields unsupported");
    for (unsigned j = 0; j < 3; ++j)
        Need(bounds[j] <= bounds[j + 3], "Door model reversed bounds");
    Need(vertices.bytes <= limits.bufferBytes &&
             vertices.bytes + indices[0].bytes + indices[1].bytes + edgePayload.bytes <=
                 limits.totalBufferBytes,
         "Door buffer budget");
    result.bindGeometry.vertices.reserve(counts[1]);
    for (std::size_t i = 0; i < counts[1]; ++i) {
        const auto at = vertices.offset + i * 64;
        Need(Unsigned(data, at + 12) == 0, "Door vertex references unsupported palette entry");
        for (unsigned j = 0; j < 3; ++j) {
            const auto v = Float(data, at + 4 * j);
            Need(v >= bounds[j] - .002 && v <= bounds[j + 3] + .002,
                 "Door position outside declared bounds");
        }
        for (unsigned j = 4; j < 13; ++j)
            (void)Float(data, at + 4 * j);
        GeometryVertex v = {float(Float(data, at)), float(Float(data, at + 4)),
                            float(Float(data, at + 8)), float(Float(data, at + 56)),
                            float(Float(data, at + 60))};
        result.bindGeometry.vertices.push_back(v);
    }
    for (unsigned b = 0; b < 2; ++b)
        for (std::size_t i = 0; i < ic[b]; ++i) {
            const auto p = indices[b].offset + i * 2;
            const unsigned ix = data[p] | (unsigned(data[p + 1]) << 8);
            Need(ix < counts[b ? 1 : 0], "Door index outside vertex buffer");
            if (b)
                surface.indices.push_back(ix);
        }
    Need(surface.indices.size() == triangles * 3, "Door surface index count");
    result.bindGeometry.surfaces.push_back(std::move(surface));
    result.bindGeometry.models = 1;
    return result;
}
std::array<float, 16> RigidDoorSkinMatrix(const DoorJointPose &bind, const DoorJointPose &pose) {
    const auto animated = Quaternion(pose.rotation);
    auto inverse = Quaternion(bind.rotation);
    for (unsigned i = 0; i < 3; ++i)
        inverse[i] = -inverse[i];
    for (unsigned i = 0; i < 3; ++i)
        Need(std::isfinite(pose.translation[i]) && std::abs(pose.translation[i]) <= 1e8 &&
                 std::isfinite(bind.translation[i]) && std::abs(bind.translation[i]) <= 1e8,
             "Door pose translation invalid");
    std::array<float, 16> matrix = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
    for (unsigned col = 0; col < 3; ++col) {
        std::array<double, 3> axis = {{0, 0, 0}};
        axis[col] = 1;
        axis = Rotate(animated, Rotate(inverse, axis));
        for (unsigned row = 0; row < 3; ++row)
            matrix[col * 4 + row] = float(axis[row]);
    }
    auto offset = Rotate(animated, Rotate(inverse, bind.translation));
    for (unsigned row = 0; row < 3; ++row)
        matrix[12 + row] = float(pose.translation[row] - offset[row]);
    return matrix;
}
GeometryScene PoseRigidDoor(const RigidDoorGeometry &source, const DoorJointPose &pose) {
    // Compute in double precision here, independently of the float GPU matrix.
    const auto animated = Quaternion(pose.rotation);
    auto inverse = Quaternion(source.bindJoint.rotation);
    for (unsigned i = 0; i < 3; ++i)
        inverse[i] = -inverse[i];
    (void)RigidDoorSkinMatrix(source.bindJoint, pose);
    GeometryScene output = source.bindGeometry;
    for (auto &v : output.vertices) {
        std::array<double, 3> p = {{v.x - source.bindJoint.translation[0],
                                    v.y - source.bindJoint.translation[1],
                                    v.z - source.bindJoint.translation[2]}};
        p = Rotate(animated, Rotate(inverse, p));
        v.x = float(p[0] + pose.translation[0]);
        v.y = float(p[1] + pose.translation[1]);
        v.z = float(p[2] + pose.translation[2]);
        Need(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z),
             "Door posed vertex overflow");
    }
    return output;
}
DoorModel::DoorModel(RigidDoorGeometry source, DoorAnimation animation)
    : source_(std::move(source)), animation_(std::move(animation)) {}
DoorModel DoorModel::Load(const Bytes &bytes, const std::string &name, const DoorAnimation &animation,
                          const GeometryLimits &limits) {
    DoorModel model(DecodeRigidDoorModel1(bytes, name, limits), animation);
    const auto &reference = animation.ReferenceBindPose();
    double dot = 0;
    for (unsigned j = 0; j < 3; ++j)
        Need(std::abs(reference.translation[j] - model.source_.bindJoint.translation[j]) < .001,
             "Door MODL/SKEL bind translation mismatch");
    for (unsigned j = 0; j < 4; ++j)
        dot += reference.rotation[j] * model.source_.bindJoint.rotation[j];
    Need(std::abs(dot) > .999999, "Door MODL/SKEL bind orientation mismatch");
    model.closedGeometry_ = PoseRigidDoor(model.source_, animation.ClosedPose());
    return model;
}
DoorJointPose DoorModel::SamplePose(double seconds) const { return animation_.SamplePose(seconds); }
GeometryScene DoorModel::SampleGeometry(double seconds) const {
    return PoseRigidDoor(source_, SamplePose(seconds));
}
} // namespace wolf2009

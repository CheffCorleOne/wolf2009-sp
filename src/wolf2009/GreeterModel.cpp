// SPDX-License-Identifier: GPL-3.0-or-later
#include "GreeterModel.h"
#include "HavokPack.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;

void Need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

struct Token {
    std::string text;
    bool quoted = false, binary = false;
    std::size_t offset = 0, bytes = 0;
};

class Reader {
    const Bytes &data_;
    const GeometryLimits &limits_;
    std::size_t position_ = 0, tokens_ = 0, binaryBytes_ = 0;

  public:
    Token token;
    Reader(const Bytes &data, const GeometryLimits &limits) : data_(data), limits_(limits) { Next(); }
    void Next() {
        for (;;) {
            while (position_ < data_.size() && (data_[position_] == ' ' || data_[position_] == '\t' ||
                                                data_[position_] == '\r' || data_[position_] == '\n'))
                ++position_;
            if (position_ + 1 < data_.size() && data_[position_] == '/' && data_[position_ + 1] == '/') {
                while (position_ < data_.size() && data_[position_] != '\n')
                    ++position_;
            } else {
                break;
            }
        }
        token = Token();
        if (position_ == data_.size())
            return;
        Need(tokens_ < limits_.syntaxNodes, "Greeter model syntax budget");
        ++tokens_;
        if (data_[position_] == '"') {
            ++position_;
            token.quoted = true;
            while (position_ < data_.size() && data_[position_] != '"') {
                Need(data_[position_] >= 32 && data_[position_] <= 126 && token.text.size() < 4096,
                     "Invalid greeter model quoted token");
                token.text += char(data_[position_++]);
            }
            Need(position_ < data_.size(), "Unterminated greeter model quote");
            ++position_;
            return;
        }
        if (std::strchr("{}[]", data_[position_])) {
            token.text = char(data_[position_++]);
            return;
        }
        while (position_ < data_.size() && data_[position_] > 32 &&
               !std::strchr("{}[]\"", data_[position_])) {
            Need(data_[position_] <= 126 && token.text.size() < 4096, "Invalid greeter model token");
            token.text += char(data_[position_++]);
        }
        Need(!token.text.empty(), "Invalid greeter model syntax byte");
        if (token.text != "bin")
            return;
        while (position_ < data_.size() && (data_[position_] == ' ' || data_[position_] == '\t'))
            ++position_;
        std::size_t size = 0, digits = 0;
        while (position_ < data_.size() && data_[position_] >= '0' && data_[position_] <= '9') {
            const unsigned digit = data_[position_++] - '0';
            Need(++digits <= 10 && size <= ((8u << 20) - digit) / 10, "Greeter binary size budget");
            size = size * 10 + digit;
        }
        Need(digits && size <= limits_.bufferBytes, "Missing/excessive greeter binary size");
        Need(binaryBytes_ <= limits_.totalBufferBytes && size <= limits_.totalBufferBytes - binaryBytes_,
             "Greeter total binary buffer budget");
        binaryBytes_ += size;
        while (position_ < data_.size() && (data_[position_] == ' ' || data_[position_] == '\t'))
            ++position_;
        if (position_ < data_.size() && data_[position_] == '\r')
            ++position_;
        Need(position_ < data_.size() && data_[position_++] == '\n' && size <= data_.size() - position_,
             "Truncated greeter model binary range");
        token.binary = true;
        token.offset = position_;
        token.bytes = size;
        position_ += size;
    }
    bool Is(const char *text) const { return !token.binary && !token.quoted && token.text == text; }
    void Eat(const char *text) {
        Need(Is(text), "Unsupported greeter MD5R structure");
        Next();
    }
    double Real() {
        Need(!token.binary && !token.quoted && !token.text.empty() && token.text.size() <= 64,
             "Invalid greeter numeric token");
        char *end = NULL;
        errno = 0;
        const double value = std::strtod(token.text.c_str(), &end);
        Need(end != token.text.c_str() && !*end && errno != ERANGE && std::isfinite(value) &&
                 std::abs(value) <= 1e8,
             "Invalid/nonfinite greeter number");
        Next();
        return value;
    }
    std::size_t Unsigned(std::size_t cap) {
        const double value = Real();
        Need(value >= 0 && value <= double(cap) && value <= 2147483647.0 && std::floor(value) == value,
             "Greeter model count/index budget");
        return std::size_t(value);
    }
    int Signed(int low, int high) {
        const double value = Real();
        Need(value >= low && value <= high && std::floor(value) == value,
             "Invalid greeter signed index");
        return int(value);
    }
    std::size_t Count(const char *text, std::size_t cap) {
        Eat(text);
        Eat("[");
        const auto count = Unsigned(cap);
        Eat("]");
        Eat("{");
        return count;
    }
    std::string Quote() {
        Need(token.quoted && !token.text.empty(), "Missing quoted greeter model name");
        const auto value = token.text;
        Next();
        return value;
    }
    Token Payload() {
        Token result;
        if (token.binary) {
            result = token;
            Next();
        } else if (Is("file")) {
            Next();
            result.text = Quote();
        }
        return result;
    }
};

std::array<double, 4> Unit(std::array<double, 4> q) {
    double norm = 0;
    for (double value : q) {
        Need(std::isfinite(value), "Nonfinite greeter skin quaternion");
        norm += value * value;
    }
    Need(std::abs(norm - 1) < .001, "Nonunit greeter skin quaternion");
    for (double &value : q)
        value /= std::sqrt(norm);
    return q;
}

std::array<double, 3> Rotate(const std::array<double, 4> &q, const std::array<double, 3> &p) {
    const std::array<double, 3> t{{2 * (q[1] * p[2] - q[2] * p[1]), 2 * (q[2] * p[0] - q[0] * p[2]),
                                   2 * (q[0] * p[1] - q[1] * p[0])}};
    return {{p[0] + q[3] * t[0] + q[1] * t[2] - q[2] * t[1],
             p[1] + q[3] * t[1] + q[2] * t[0] - q[0] * t[2],
             p[2] + q[3] * t[2] + q[0] * t[1] - q[1] * t[0]}};
}

std::uint32_t U32(const Bytes &data, std::size_t at) {
    Need(at <= data.size() && 4 <= data.size() - at, "Truncated greeter vertex field");
    return std::uint32_t(data[at]) | std::uint32_t(data[at + 1]) << 8 |
           std::uint32_t(data[at + 2]) << 16 | std::uint32_t(data[at + 3]) << 24;
}

double F32(const Bytes &data, std::size_t at) {
    const auto bits = U32(data, at);
    float value;
    static_assert(sizeof(float) == 4, "IEEE32 required");
    std::memcpy(&value, &bits, 4);
    Need(std::isfinite(value) && std::abs(value) < 1e8, "Invalid greeter vertex component");
    return value;
}

std::vector<std::string> Format(Reader &reader) {
    reader.Eat("{");
    std::vector<std::string> result;
    while (!reader.Is("}")) {
        Need(!reader.token.binary && !reader.token.quoted && !reader.token.text.empty() &&
                 result.size() < 64,
             "Invalid/excessive greeter vertex format");
        result.push_back(reader.token.text);
        reader.Next();
    }
    reader.Eat("}");
    return result;
}

struct Buffer {
    std::vector<std::string> format, storedFormat;
    std::size_t count = 0, bits = 0;
    Token data;
};

std::vector<Buffer> Buffers(Reader &reader, bool vertices, const GeometryLimits &limits) {
    const char *name = vertices ? "VertexBuffer" : "IndexBuffer";
    const auto count = reader.Count(name, 64);
    std::vector<Buffer> result;
    for (std::size_t i = 0; i < count; ++i) {
        reader.Eat(name);
        reader.Eat("{");
        Buffer buffer;
        if (vertices) {
            reader.Eat("VertexFormat");
            buffer.format = Format(reader);
            if (reader.Is("LoadVertexFormat")) {
                reader.Next();
                buffer.storedFormat = Format(reader);
                Need(!buffer.storedFormat.empty(), "Empty greeter stored vertex format");
            }
        }
        while (reader.Is("SystemMemory") || reader.Is("VideoMemory") || reader.Is("DynamicMemory"))
            reader.Next();
        if (!vertices) {
            reader.Eat("BitDepth");
            buffer.bits = reader.Unsigned(32);
        }
        buffer.count = reader.Count(vertices ? "Vertex" : "Index",
                                    vertices ? limits.vertices : limits.triangles * 3);
        buffer.data = reader.Payload();
        reader.Eat("}");
        reader.Eat("}");
        result.push_back(buffer);
    }
    reader.Eat("}");
    return result;
}

void Name(const std::string &name) {
    Need(!name.empty() && name.size() <= 4096 && name.front() != '/' && name.back() != '/',
         "Invalid greeter model virtual name");
    std::string component;
    for (unsigned char c : name) {
        Need(c >= 33 && c <= 126 && c != ':', "Unsafe greeter model virtual name byte");
        if (c == '/' || c == '\\') {
            Need(!component.empty() && component != "." && component != "..", "Unsafe greeter path");
            component.clear();
        } else {
            component += char(c);
        }
    }
    Need(!component.empty() && component != "." && component != "..", "Unsafe greeter path tail");
}

void Draw(GreeterBodyGeometry &body, const Bytes &data, const Buffer &vb, const Buffer &ib,
          const std::array<std::size_t, 4> &draw, const std::vector<std::uint16_t> &palette,
          const std::string &name, const std::string &material, const GeometryLimits &limits,
          bool head) {
    const std::vector<std::string> expected{
        "Position", "3",       "BlendIndex",   "BlendWeight", "3", "4",
        "Normal",   "Tangent", "DiffuseColor", "TexCoord",    "2", "0"};
    const std::vector<std::string> rigid{"Position", "3",        "BlendIndex",   "Normal",
                                         "Tangent",  "Binormal", "DiffuseColor", "TexCoord",
                                         "2",        "0"};
    const auto &format = vb.storedFormat.empty() ? vb.format : vb.storedFormat;
    const bool weighted = format == expected;
    Need(weighted || (head && format == rigid), "Unsupported greeter render vertex format");
    Need(vb.data.binary && vb.data.text == "bin" && ib.data.binary && ib.bits == 16 &&
             vb.count <= limits.bufferBytes / 64 && vb.data.bytes == vb.count * 64 &&
             ib.count <= limits.bufferBytes / 2 && ib.data.bytes == ib.count * 2,
         "Greeter body requires coherent inline64/16bit render buffers");
    Need(draw[1] && draw[3] && draw[0] <= vb.count && draw[1] <= vb.count - draw[0] &&
             draw[2] <= ib.count && draw[3] <= (ib.count - draw[2]) / 3,
         "Greeter primitive range exceeds buffers");
    Need(body.bindGeometry.vertices.size() <= limits.vertices &&
             draw[1] <= limits.vertices - body.bindGeometry.vertices.size() &&
             body.bindGeometry.vertices.size() + draw[1] <= 0xffffffffu &&
             body.bindGeometry.surfaces.size() < limits.surfaces,
         "Greeter geometry output budget");
    const auto base = std::uint32_t(body.bindGeometry.vertices.size());
    for (std::size_t i = 0; i < draw[1]; ++i) {
        const auto at = vb.data.offset + (draw[0] + i) * 64;
        GeometryVertex vertex = {float(F32(data, at)),      float(F32(data, at + 4)),
                                 float(F32(data, at + 8)),  float(F32(data, at + 56)),
                                 float(F32(data, at + 60)), U32(data, at + 52)};
        GreeterInfluence influence;
        const double storedFirst = weighted ? F32(data, at + 16) : 1;
        influence.mirroredTangent = storedFirst < 0;
        influence.weights = {{std::abs(storedFirst), weighted ? F32(data, at + 20) : 0,
                              weighted ? F32(data, at + 24) : 0, 0}};
        const double sum = influence.weights[0] + influence.weights[1] + influence.weights[2];
        Need(influence.weights[1] >= 0 && influence.weights[2] >= 0 && sum <= 1 + 1e-6,
             "Invalid greeter blend weights");
        influence.weights[3] = std::max(0.0, 1 - sum);
        const double total = sum + influence.weights[3];
        for (unsigned component = 0; component < 4; ++component) {
            const unsigned index = data[at + 12 + component];
            Need(index < palette.size(), "Greeter vertex palette index out of range");
            influence.joints[component] = palette[index];
            influence.weights[component] /= total;
        }
        body.bindGeometry.vertices.push_back(vertex);
        body.influences.push_back(influence);
    }
    GeometrySurface surface;
    surface.model = name;
    surface.material = material;
    for (std::size_t i = 0; i < draw[3] * 3; ++i) {
        const auto at = ib.data.offset + (draw[2] + i) * 2;
        const unsigned index = unsigned(data[at]) | unsigned(data[at + 1]) << 8;
        Need(index >= draw[0] && index - draw[0] < draw[1],
             "Greeter draw index exceeds declared absolute vertex range");
        surface.indices.push_back(base + std::uint32_t(index - draw[0]));
    }
    body.bindGeometry.surfaces.push_back(surface);
}
} // namespace

static GreeterSkinGeometry DecodeSkin1(const Bytes &data, const std::string &resourceName,
                                       const std::vector<GreeterJoint> &animationSkeleton,
                                       std::size_t levelOfDetail, const GeometryLimits &limits,
                                       bool head, SkinnedLodPolicy lodPolicy) {
    Name(resourceName);
    Need(lodPolicy == SkinnedLodPolicy::Authored ||
             lodPolicy == SkinnedLodPolicy::SingleResourceWithoutLodRecords ||
             lodPolicy == SkinnedLodPolicy::RootAuthoredMeshUnspecifiedSlotV1,
         "Unsupported skinned LOD policy");
    const bool authoredLod = lodPolicy == SkinnedLodPolicy::Authored;
    const bool rootSlot = lodPolicy == SkinnedLodPolicy::RootAuthoredMeshUnspecifiedSlotV1;
    Need(authoredLod || levelOfDetail == 0, "Skinned compatibility policy requires slot0");
    const std::size_t jointCount = animationSkeleton.size();
    Need(jointCount>0 && jointCount<=256 && limits.models >= 1 &&
             data.size() <= limits.sourceBytes && data.size() <= (8u << 20) &&
             limits.triangles <= (std::numeric_limits<std::size_t>::max)() / 3,
         "Greeter body source/skeleton/count budget");
    Reader reader(data, limits);
    reader.Eat("MemSize");
    reader.Unsigned(1u << 30);
    reader.Eat("MD5RVersion");
    reader.Eat("1");
    Need(reader.Count("Joint", jointCount) == jointCount, "Greeter model joint count mismatch");
    GreeterBodyGeometry body;
    body.levelOfDetail = levelOfDetail;
    body.lodPolicy = lodPolicy;
    body.sourceHasLodRecords = authoredLod || rootSlot;
    body.selectionUsesCompatibilitySlot = !authoredLod;
    for (unsigned i = 0; i < jointCount; ++i) {
        const auto name = reader.Quote();
        const int parent = reader.Signed(-1, int(jointCount) - 1);
        Need(name == animationSkeleton[i].name && parent == animationSkeleton[i].parent &&
                 (i == 0 ? parent == -1 : parent >= 0 && parent < int(i)),
             "Greeter body/animation joint identity or hierarchy differs");
        GreeterJointPose bind;
        for (double &value : bind.translation)
            value = reader.Real();
        double sum = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            bind.rotation[axis] = reader.Real();
            sum += bind.rotation[axis] * bind.rotation[axis];
        }
        Need(sum <= 1 + 1e-6, "Invalid greeter MD5R bind quaternion");
        bind.rotation[3] = -std::sqrt(std::max(0.0, 1 - sum));
        bind.rotation = Unit(bind.rotation);
        body.jointNames.push_back(name);
        body.bindModelJoints.push_back(bind);
    }
    reader.Eat("}");
    const auto vertexBuffers = Buffers(reader, true, limits),
               indexBuffers = Buffers(reader, false, limits);
    reader.Count("SilhouetteEdge", limits.triangles * 3);
    reader.Payload();
    reader.Eat("}");
    std::size_t lods = 0;
    if (authoredLod || rootSlot) {
        lods = reader.Count("LevelOfDetail", 32);
        Need(lods && levelOfDetail < lods, "Requested greeter LOD not declared");
        Need(!rootSlot || lods == 1, "Root-slot policy requires exactly one authored root LOD");
        double previous = 0;
        for (std::size_t i = 0; i < lods; ++i) {
            const double distance = reader.Real();
            Need(distance > previous, "Invalid greeter LOD distances");
            previous = distance;
            body.sourceLodDistances.push_back(distance);
        }
        reader.Eat("}");
    } else {
        Need(!reader.Is("LevelOfDetail"), "No-LOD policy rejects authored root LOD records");
    }
    const auto meshes = reader.Count("Mesh", limits.surfaces);
    std::size_t triangleCount = 0, unspecifiedMeshLods = 0;
    for (std::size_t m = 0; m < meshes; ++m) {
        reader.Eat("Mesh");
        reader.Eat("{");
        std::size_t lod = 0; // Explicit compatibility selection, not an absent authored value.
        SkinnedMeshLodFact lodFact;
        if (authoredLod || (rootSlot && reader.Is("LevelOfDetail"))) {
            reader.Eat("LevelOfDetail");
            lod = reader.Unsigned(lods - 1);
            lodFact.hasLevelOfDetail = true;
            lodFact.levelOfDetail = lod;
        } else {
            Need(!reader.Is("LevelOfDetail"), "No-LOD policy rejects authored mesh LOD fields");
            ++unspecifiedMeshLods;
        }
        body.sourceMeshLods.push_back(lodFact);
        reader.Eat("Material");
        const auto material = reader.Quote();
        Name(material);
        reader.Eat("SilTraceBuffers");
        reader.Signed(-1, 63);
        reader.Signed(-1, 63);
        reader.Eat("DrawBuffers");
        const auto vb = reader.Unsigned(63), ib = reader.Unsigned(63);
        Need(vb < vertexBuffers.size() && ib < indexBuffers.size(), "Greeter DrawBuffers out of range");
        reader.Eat("ShadowVolumeBuffers");
        reader.Signed(-1, 63);
        reader.Signed(-1, 63);
        const auto batches = reader.Count("PrimBatch", limits.surfaces);
        for (std::size_t b = 0; b < batches; ++b) {
            reader.Eat("PrimBatch");
            reader.Eat("{");
            const auto paletteCount = reader.Count("Transform", jointCount);
            Need(paletteCount, "Empty greeter joint palette");
            std::vector<std::uint16_t> palette;
            std::set<std::uint16_t> distinct;
            for (std::size_t j = 0; j < paletteCount; ++j) {
                const auto joint = std::uint16_t(reader.Unsigned(jointCount - 1));
                Need(distinct.insert(joint).second, "Duplicate greeter joint palette entry");
                palette.push_back(joint);
            }
            reader.Eat("}");
            reader.Eat("SilTraceIndexedTriList");
            for (unsigned i = 0; i < 4; ++i)
                reader.Unsigned(1u << 28);
            reader.Eat("DrawIndexedTriList");
            std::array<std::size_t, 4> draw;
            for (auto &value : draw)
                value = reader.Unsigned(1u << 28);
            reader.Eat("ShadowVerts");
            reader.Unsigned(1u << 28);
            reader.Eat("SilhouetteEdge");
            reader.Unsigned(1u << 28);
            reader.Unsigned(1u << 28);
            reader.Eat("}");
            if (lod == levelOfDetail) {
                Need(triangleCount <= limits.triangles && draw[3] <= limits.triangles - triangleCount,
                     "Greeter total triangle budget");
                triangleCount += draw[3];
                Draw(body, data, vertexBuffers[vb], indexBuffers[ib], draw, palette, resourceName,
                     material, limits, head);
            }
        }
        reader.Eat("}");
        reader.Eat("}");
        if (lod != levelOfDetail)
            ++body.bindGeometry.skippedMeshes;
    }
    Need(!rootSlot || unspecifiedMeshLods,
         "Root-slot policy requires at least one missing mesh LOD field");
    reader.Eat("}");
    reader.Eat("Bounds");
    std::array<double, 6> bounds;
    for (double &value : bounds)
        value = reader.Real();
    for (unsigned axis = 0; axis < 3; ++axis)
        Need(bounds[axis] <= bounds[axis + 3], "Inverted greeter model bounds");
    for (const auto &vertex : body.bindGeometry.vertices) {
        const double position[] = {vertex.x, vertex.y, vertex.z};
        for (unsigned axis = 0; axis < 3; ++axis)
            Need(position[axis] >= bounds[axis] - .001 && position[axis] <= bounds[axis + 3] + .001,
                 "Greeter bind vertex outside declared model bounds");
    }
    Need(reader.Is("") && !body.bindGeometry.surfaces.empty() && !body.bindGeometry.vertices.empty(),
         "Missing selected greeter LOD or trailing unsupported model data");
    body.bindGeometry.models = 1;
    return body;
}

static GeometryScene PoseSkin(const GreeterSkinGeometry &body, const GreeterPose &pose) {
    const auto count = body.bindModelJoints.size();
    Need(count > 0 && count <= 256 && body.jointNames.size() == count && pose.model.size() == count &&
             body.influences.size() == body.bindGeometry.vertices.size(),
         "Invalid greeter body pose shape");
    std::vector<GreeterJointPose> animated = pose.model, bind = body.bindModelJoints;
    for (std::size_t i = 0; i < count; ++i) {
        animated[i].rotation = Unit(animated[i].rotation);
        bind[i].rotation = Unit(bind[i].rotation);
        for (unsigned axis = 0; axis < 3; ++axis)
            Need(std::isfinite(animated[i].translation[axis]) &&
                     std::isfinite(bind[i].translation[axis]) &&
                     std::abs(animated[i].translation[axis]) < 1e8 &&
                     std::abs(bind[i].translation[axis]) < 1e8,
                 "Invalid greeter skin translation");
    }
    GeometryScene geometry = body.bindGeometry;
    for (std::size_t v = 0; v < geometry.vertices.size(); ++v) {
        const auto &source = body.bindGeometry.vertices[v];
        const auto &influence = body.influences[v];
        Need(std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.z),
             "Invalid greeter bind vertex");
        std::array<double, 3> result{{0, 0, 0}};
        double total = 0;
        for (unsigned j = 0; j < 4; ++j) {
            const auto joint = influence.joints[j];
            const double weight = influence.weights[j];
            Need(joint < count && std::isfinite(weight) && weight >= 0 && weight <= 1,
                 "Invalid greeter influence");
            total += weight;
            if (!weight)
                continue;
            const auto &b = bind[joint], &a = animated[joint];
            const auto &q = b.rotation;
            const auto local = Rotate({{-q[0], -q[1], -q[2], q[3]}},
                                      {{source.x - b.translation[0], source.y - b.translation[1],
                                        source.z - b.translation[2]}});
            const auto skinned = Rotate(a.rotation, local);
            for (unsigned axis = 0; axis < 3; ++axis)
                result[axis] += weight * (skinned[axis] + a.translation[axis]);
        }
        Need(std::abs(total - 1) < 1e-6, "Greeter influence weights do not sum to one");
        geometry.vertices[v].x = float(result[0]);
        geometry.vertices[v].y = float(result[1]);
        geometry.vertices[v].z = float(result[2]);
    }
    for (const auto &surface : geometry.surfaces)
        for (const auto index : surface.indices)
            Need(index < geometry.vertices.size(), "Invalid greeter surface index");
    return geometry;
}

GreeterBodyGeometry DecodeGreeterBody1(const Bytes &data, const std::string &resourceName,
                                       const std::vector<GreeterJoint> &animationSkeleton,
                                       std::size_t levelOfDetail, const GeometryLimits &limits) {
    Need(animationSkeleton.size()==52,"Expected52-joint greeter body skeleton");
    return DecodeSkin1(data, resourceName, animationSkeleton, levelOfDetail, limits, false, SkinnedLodPolicy::Authored);
}
GreeterSkinGeometry DecodeWeightedModel1(const Bytes& data,const std::string& resourceName,
    const std::vector<GreeterJoint>& skeleton,std::size_t lod,SkinnedVertexProfile profile,const GeometryLimits& limits){
    return DecodeWeightedModel1(data,resourceName,skeleton,lod,profile,SkinnedLodPolicy::Authored,limits);
}
GreeterSkinGeometry DecodeWeightedModel1(const Bytes& data,const std::string& resourceName,
    const std::vector<GreeterJoint>& skeleton,std::size_t lod,SkinnedVertexProfile profile,SkinnedLodPolicy lodPolicy,const GeometryLimits& limits){
    Need(profile==SkinnedVertexProfile::Weighted64||profile==SkinnedVertexProfile::WeightedOrRigid64,"unsupported weighted model declaration profile");
    return DecodeSkin1(data,resourceName,skeleton,lod,limits,profile==SkinnedVertexProfile::WeightedOrRigid64,lodPolicy);
}
GeometryScene PoseWeightedModel(const GreeterSkinGeometry& source,const GreeterPose& pose){return PoseSkin(source,pose);}

GeometryScene PoseGreeterBody(const GreeterBodyGeometry &body, const GreeterPose &pose) {
    Need(body.bindModelJoints.size() == 52, "Expected52-joint greeter body source");
    return PoseSkin(body, pose);
}

GeometryScene PoseGreeterSkinGeometry(const GreeterSkinGeometry &source, const GreeterPose &pose) {
    Need(source.bindModelJoints.size() == 27 || source.bindModelJoints.size() == 52,
         "Expected supported27/52-joint skin source");
    return PoseSkin(source, pose);
}

GreeterModel::GreeterModel(GreeterBodyGeometry source, GreeterAnimation animation)
    : source_(std::move(source)), animation_(std::move(animation)) {}

GreeterModel GreeterModel::Load(const Bytes &model, const std::string &resourceName,
                                const GreeterAnimation &animation, std::size_t levelOfDetail,
                                const GeometryLimits &limits) {
    return GreeterModel(
        DecodeGreeterBody1(model, resourceName, animation.Joints(), levelOfDetail, limits), animation);
}

GeometryScene GreeterModel::SampleGeometry(double seconds) const {
    return PoseGreeterBody(source_, animation_.SamplePose(seconds));
}

namespace {
using detail::HavokPack;
const char *const HeadSkeletonName =
    "models/characters/civilians/head_male_03/anims/head_civilian_male_03_mesh_skel.hkx";
const char *const HeadIdleName =
    "models/characters/civilians/head_male_03/anims/head_civilian_male_03_idle.hkx";

std::array<double, 4> Product(const std::array<double, 4> &a, const std::array<double, 4> &b) {
    return Unit({{a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                  a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                  a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]}});
}

GreeterJointPose Compose(const GreeterJointPose &parent, const GreeterJointPose &local) {
    GreeterJointPose result;
    const auto position = Rotate(parent.rotation, local.translation);
    for (unsigned i = 0; i < 3; ++i)
        result.translation[i] = parent.translation[i] + position[i];
    result.rotation = Product(parent.rotation, local.rotation);
    return result;
}

GreeterJointPose Relative(const GreeterJointPose &parent, const GreeterJointPose &model) {
    const auto &q = parent.rotation;
    const std::array<double, 4> inverse{{-q[0], -q[1], -q[2], q[3]}};
    GreeterJointPose result;
    result.translation = Rotate(inverse, {{model.translation[0] - parent.translation[0],
                                           model.translation[1] - parent.translation[1],
                                           model.translation[2] - parent.translation[2]}});
    result.rotation = Product(inverse, model.rotation);
    return result;
}

GreeterJointPose ValidPose(GreeterJointPose pose) {
    pose.rotation = Unit(pose.rotation);
    for (double value : pose.translation)
        Need(std::isfinite(value) && std::abs(value) < 1e8, "Invalid head placement component");
    return pose;
}

std::uint32_t HeadVariant(const HavokPack &pack, const char *name, const char *type) {
    const auto root = pack.U(28), count = pack.DU(root + 4), array = pack.Pointer(root);
    Need(count && count <= 4096, "Head variant table budget");
    pack.ArrayRange(array, count, 16);
    std::uint32_t found = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto slot = array + i * 16;
        if (pack.DataString(pack.Pointer(slot)) != name)
            continue;
        const auto object = pack.Pointer(slot + 8);
        Need(!found && object && pack.DataString(pack.Pointer(slot + 4)) == type &&
                 pack.Type(object) == type,
             "Ambiguous/mistyped head variant");
        found = object;
    }
    Need(found, "Missing named head variant");
    return found;
}

std::vector<GreeterJoint> HeadSkeleton(const HavokPack &pack) {
    const auto container = HeadVariant(pack, "Animation Container", "hkaAnimationContainer");
    const auto count = pack.DU(container + 4), array = pack.Pointer(container);
    Need(count && count <= 4096, "Head skeleton table budget");
    pack.ArrayRange(array, count, 4);
    std::uint32_t selected = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto object = pack.Pointer(array + i * 4);
        Need(pack.Type(object) == "hkaSkeleton", "Mistyped head skeleton table entry");
        if (pack.DataString(pack.Pointer(object)) != HeadSkeletonName)
            continue;
        Need(!selected, "Ambiguous head skeleton name");
        selected = object;
    }
    Need(selected && pack.DU(selected + 8) == 27 && pack.DU(selected + 16) == 27 &&
             pack.DU(selected + 24) == 27,
         "Expected named27-joint head skeleton");
    const auto parents = pack.Pointer(selected + 4), bones = pack.Pointer(selected + 12);
    const auto poses = pack.Pointer(selected + 20);
    pack.ArrayRange(parents, 27, 2);
    pack.ArrayRange(bones, 27, 4);
    pack.ArrayRange(poses, 27, 48);
    std::vector<GreeterJoint> result;
    std::set<std::string> names;
    for (unsigned i = 0; i < 27; ++i) {
        const auto bone = pack.Pointer(bones + 4 * i);
        Need(pack.Type(bone) == "hkaBone", "Mistyped head bone");
        GreeterJoint joint;
        joint.name = pack.DataString(pack.Pointer(bone));
        Need(!joint.name.empty() && names.insert(joint.name).second, "Ambiguous head bone name");
        const auto at = pack.sections[2].absolute + parents + 2 * i;
        const unsigned parent = unsigned(pack.bytes[at]) | unsigned(pack.bytes[at + 1]) << 8;
        joint.parent = parent == 65535 ? -1 : int(parent);
        Need(i == 0 ? joint.parent == -1 && joint.name == "origin"
                    : joint.parent >= 0 && joint.parent < int(i),
             "Invalid head hierarchy");
        for (unsigned axis = 0; axis < 3; ++axis) {
            joint.referenceLocal.translation[axis] = pack.F(poses + 48 * i + 4 * axis);
            Need(std::abs(pack.F(poses + 48 * i + 32 + 4 * axis) - 1) < 1e-5,
                 "Unsupported head reference scale");
        }
        for (unsigned axis = 0; axis < 4; ++axis)
            joint.referenceLocal.rotation[axis] = pack.F(poses + 48 * i + 16 + 4 * axis);
        joint.referenceLocal = ValidPose(joint.referenceLocal);
        joint.referenceModel =
            joint.parent < 0
                ? joint.referenceLocal
                : Compose(result[std::size_t(joint.parent)].referenceModel, joint.referenceLocal);
        result.push_back(joint);
    }
    Need(result[1].name == "shoulders" && result[2].name == "neck" && result[3].name == "head" &&
             result[1].parent == 0 && result[2].parent == 1 && result[3].parent == 2,
         "Unsupported named head copy hierarchy");
    return result;
}

std::vector<GreeterJointPose> HeadIdle(const HavokPack &pack) {
    const auto container = HeadVariant(pack, "Animation Container", "hkaAnimationContainer");
    const auto names = HeadVariant(pack, "animNames", "rvHkStringContainer");
    const auto count = pack.DU(container + 12), array = pack.Pointer(container + 8),
               strings = pack.Pointer(names);
    Need(count && count <= 4096 && pack.DU(names + 4) == count, "Head idle name table mismatch");
    pack.ArrayRange(array, count, 4);
    pack.ArrayRange(strings, count, 4);
    std::uint32_t object = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pack.DataString(pack.Pointer(strings + 4 * i)) != HeadIdleName)
            continue;
        Need(!object, "Duplicate head idle clip");
        object = pack.Pointer(array + 4 * i);
    }
    Need(object && pack.Type(object) == "hkaSplineSkeletalAnimation" && pack.DU(object + 8) == 5 &&
             pack.DU(object + 16) == 27 && pack.DU(object + 20) == 6 && pack.DU(object + 36) == 2 &&
             pack.DU(object + 40) == 1 && pack.DU(object + 44) == 256 && pack.DU(object + 48) == 116 &&
             pack.DU(object + 68) == 1 && pack.DU(object + 80) == 1,
         "Unsupported head idle class/frame/block profile");
    Need(std::abs(pack.F(object + 12) - 1.0 / 24) < 1e-7 &&
             std::abs(pack.F(object + 60) - 1.0 / 24) < 1e-7 && pack.DU(pack.Pointer(object + 64)) == 0,
         "Unsupported head idle timing/block offset");
    const auto data = pack.Pointer(object + 112), size = pack.DU(object + 116);
    const auto end = pack.DU(pack.Pointer(object + 76));
    Need(size <= 16384 && end >= 116 && end < size, "Invalid head idle transform range");
    pack.DataRange(data, size);
    const auto byte = [&pack, data, size](unsigned i) {
        Need(i < size, "Head idle mask/transform out of range");
        return unsigned(pack.bytes[pack.sections[2].absolute + data + i]);
    };
    for (unsigned i = 108; i < 116; ++i)
        Need(byte(i) == (i < 114 ? 3u : 0u), "Unsupported head idle auxiliary masks");
    unsigned cursor = 116;
    const auto align = [&] {
        while (cursor % 4) {
            Need(cursor < end && byte(cursor) == 0, "Head idle alignment padding");
            ++cursor;
        }
    };
    const auto real = [&] {
        Need(cursor <= end && 4 <= end - cursor, "Head idle component exceeds transform area");
        const double value = pack.F(data + cursor);
        cursor += 4;
        Need(std::abs(value) < 1000000, "Excessive head idle translation");
        return value;
    };
    std::vector<GreeterJointPose> result;
    for (unsigned i = 0; i < 27; ++i) {
        const auto t = byte(4 * i + 1), r = byte(4 * i + 2);
        Need(byte(4 * i) == 0x45 && byte(4 * i + 3) == 0 && (t == 0 || t == 2 || t == 6 || t == 7) &&
                 (r == 0 || r == 1 || r == 9 || r == 13),
             "Unsupported static head idle track mask");
        GreeterJointPose pose;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (t & (1u << axis))
                pose.translation[axis] = real();
        align();
        if (r) {
            Need(cursor <= end && 5 <= end - cursor, "Truncated head idle quaternion");
            std::uint64_t bits = 0;
            for (unsigned j = 0; j < 5; ++j)
                bits |= std::uint64_t(byte(cursor++)) << (8 * j);
            Need(!(bits >> 39), "Reserved head idle quaternion bit");
            const unsigned omitted = unsigned(bits >> 36) & 3;
            double sum = 0;
            unsigned component = 0;
            for (unsigned axis = 0; axis < 4; ++axis) {
                if (axis == omitted)
                    continue;
                const unsigned value = unsigned(bits >> (12 * component++)) & 4095;
                Need(value <= 4094, "Invalid head packed quaternion coordinate");
                pose.rotation[axis] = (double(value) - 2047) * std::sqrt(.5) / 2047;
                sum += pose.rotation[axis] * pose.rotation[axis];
            }
            Need(sum <= 1, "Invalid head packed quaternion norm");
            pose.rotation[omitted] = std::sqrt(1 - sum) * ((bits & (std::uint64_t(1) << 38)) ? -1 : 1);
        }
        align();
        result.push_back(ValidPose(pose));
    }
    Need(cursor == end &&
             std::abs(result[0].translation[0]) + std::abs(result[0].translation[1]) +
                     std::abs(result[0].translation[2]) <
                 1e-6 &&
             std::abs(result[0].rotation[0]) + std::abs(result[0].rotation[1]) +
                     std::abs(result[0].rotation[2]) <
                 1e-6,
         "Unsupported head idle root/transform suffix");
    return result;
}
} // namespace

GreeterHeadAttachment LoadGreeterHead1(const Bytes &model, const std::string &resourceName,
                                       const Bytes &mapHkxa, const Bytes &mapSkel,
                                       const std::vector<GreeterJoint> &actorSkeleton,
                                       std::size_t levelOfDetail, const GeometryLimits &limits) {
    Need(actorSkeleton.size() == 52, "Head attachment requires verified52-joint actor skeleton");
    GreeterHeadAttachment result;
    const auto sourceLimit = std::min(limits.sourceBytes, std::size_t(64u << 20));
    result.skeleton = HeadSkeleton(HavokPack(mapSkel, sourceLimit, 100000, 1000000));
    result.idleLocal = HeadIdle(HavokPack(mapHkxa, sourceLimit, 100000, 1000000));
    const char *required[] = {"chest", "shoulders", "neck", "head"};
    for (unsigned i = 0; i < 4; ++i) {
        std::size_t count = 0;
        for (std::size_t j = 0; j < actorSkeleton.size(); ++j)
            if (actorSkeleton[j].name == required[i]) {
                result.actorJoints[i] = j;
                ++count;
            }
        Need(count == 1, "Missing/ambiguous named actor head-copy joint");
    }
    result.source = DecodeSkin1(model, resourceName, result.skeleton, levelOfDetail, limits, true, SkinnedLodPolicy::Authored);
    return result;
}

GreeterPose PoseGreeterHeadCurrentActor(const GreeterHeadAttachment &head, const GreeterPose &actor) {
    Need(head.skeleton.size() == 27 && head.idleLocal.size() == 27 && actor.model.size() == 52,
         "Invalid current-actor head pose shape");
    GreeterPose result;
    result.seconds = actor.seconds;
    for (std::size_t i = 0; i < head.skeleton.size(); ++i) {
        const int parent = head.skeleton[i].parent;
        Need(i == 0 ? parent == -1 : parent >= 0 && std::size_t(parent) < i,
             "Invalid head pose hierarchy");
        GreeterJointPose local = ValidPose(head.idleLocal[i]), model;
        if (i <= 3) {
            Need(head.actorJoints[i] < actor.model.size(), "Actor head-copy joint index out of range");
            model = ValidPose(actor.model[head.actorJoints[i]]);
            local = parent < 0 ? model : Relative(result.model[std::size_t(parent)], model);
        } else {
            model = Compose(result.model[std::size_t(parent)], local);
        }
        result.local.push_back(local);
        result.model.push_back(model);
    }
    return result;
}

GeometryScene PoseGreeterHeadGeometryCurrentActor(const GreeterHeadAttachment &head,
                                                  const GreeterPose &actor) {
    Need(head.source.bindModelJoints.size() == 27, "Expected27-joint skinned head source");
    return PoseSkin(head.source, PoseGreeterHeadCurrentActor(head, actor));
}
} // namespace wolf2009

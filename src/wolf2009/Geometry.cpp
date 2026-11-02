// SPDX-License-Identifier: GPL-3.0-or-later
#include "Geometry.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace wolf2009 { namespace {
typedef std::vector<unsigned char> Bytes;
void Check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
struct Node {
    enum Kind { Atom, Quoted, Block, Array, Round, Binary } kind;
    std::string value;
    std::vector<Node> children;
    std::size_t offset = 0, size = 0;
    explicit Node(Kind k = Atom) : kind(k) {}
};
std::size_t Number(const Node& node) {
    Check(node.kind == Node::Atom && !node.value.empty() && node.value.size() <= 20,
          "Expected unsigned integer");
    for (std::size_t i = 0; i < node.value.size(); ++i)
        Check(node.value[i] >= '0' && node.value[i] <= '9', "Invalid unsigned integer");
    errno = 0; char* end = NULL;
    const unsigned long long value = std::strtoull(node.value.c_str(), &end, 10);
    Check(errno != ERANGE && end && *end == 0 && value <= (std::numeric_limits<std::size_t>::max)(),
          "Integer exceeds host range");
    return std::size_t(value);
}
float Real(const Node& node) {
    Check(node.kind == Node::Atom && !node.value.empty() && node.value.size() <= 64,
          "Expected finite float");
    errno = 0; char* end = NULL;
    const float value = std::strtof(node.value.c_str(), &end);
    Check(end != node.value.c_str() && *end == 0 && errno != ERANGE && std::isfinite(value),
          "Invalid or non-finite float");
    return value;
}
class Syntax {
    const Bytes& data_; const GeometryLimits& limits_;
    std::size_t pos_ = 0, count_ = 0;
    void Space() {
        for (;;) {
            while (pos_ < data_.size() && (data_[pos_] == ' ' || data_[pos_] == '\t' || data_[pos_] == '\r' || data_[pos_] == '\n')) ++pos_;
            if (pos_ + 1 >= data_.size() || data_[pos_] != '/') return;
            if (data_[pos_ + 1] == '/') {
                pos_ += 2; while (pos_ < data_.size() && data_[pos_] != '\n') ++pos_;
            } else if (data_[pos_ + 1] == '*') {
                pos_ += 2;
                while (pos_ + 1 < data_.size() && !(data_[pos_] == '*' && data_[pos_ + 1] == '/')) ++pos_;
                Check(pos_ + 1 < data_.size(), "Unterminated block comment"); pos_ += 2;
            } else return;
        }
    }
    Node Word() {
        const std::size_t start = pos_;
        while (pos_ < data_.size() && data_[pos_] > 32 &&
               std::strchr("{}[]()\"", data_[pos_]) == NULL) {
            Check(data_[pos_] <= 126, "Non-text byte outside a binary block"); ++pos_;
        }
        Check(pos_ > start && pos_ - start <= 4096, "Invalid or excessive syntax token");
        Node out; out.value.assign(reinterpret_cast<const char*>(data_.data() + start), pos_ - start); return out;
    }
    std::vector<Node> List(unsigned char closing, unsigned depth) {
        Check(depth <= 64, "Syntax nesting exceeds limit");
        std::vector<Node> result;
        for (;;) {
            Space();
            if (pos_ == data_.size()) { Check(closing == 0, "Unterminated syntax block"); return result; }
            const unsigned char c = data_[pos_];
            if (c == '}' || c == ']' || c == ')') {
                Check(c == closing, "Mismatched syntax block terminator"); ++pos_; return result;
            }
            Check(++count_ <= limits_.syntaxNodes, "Syntax node limit exceeded");
            Node node;
            if (c == '{' || c == '[' || c == '(') {
                ++pos_; node.kind = c == '{' ? Node::Block : c == '[' ? Node::Array : Node::Round;
                node.children = List(c == '{' ? '}' : c == '[' ? ']' : ')', depth + 1);
            } else if (c == '"') {
                ++pos_; node.kind = Node::Quoted;
                bool closed = false;
                while (pos_ < data_.size()) {
                    unsigned char value = data_[pos_++];
                    if (value == '"') { closed = true; break; }
                    Check(value >= 32 && value != 127, "Control byte in quoted string");
                    if (value == '\\' && pos_ < data_.size() && (data_[pos_] == '"' || data_[pos_] == '\\')) value = data_[pos_++];
                    node.value.push_back(char(value)); Check(node.value.size() <= 4096, "Quoted string exceeds limit");
                }
                Check(closed, "Unterminated quoted string");
            } else {
                Check(c >= 33 && c <= 126, "Binary bytes outside a declared bin block");
                node = Word();
                if (node.value == "bin") {
                    while (pos_ < data_.size() && (data_[pos_] == ' ' || data_[pos_] == '\t')) ++pos_;
                    const Node size = Word();
                    node.kind = Node::Binary; node.value.clear(); node.size = Number(size);
                    while (pos_ < data_.size() && (data_[pos_] == ' ' || data_[pos_] == '\t')) ++pos_;
                    if (pos_ < data_.size() && data_[pos_] == '\r') ++pos_;
                    Check(pos_ < data_.size() && data_[pos_++] == '\n', "Binary header must end with newline");
                    Check(node.size <= limits_.bufferBytes && node.size <= data_.size() - pos_, "Truncated or excessive binary block");
                    node.offset = pos_; pos_ += node.size;
                }
            }
            result.push_back(std::move(node));
        }
    }
public:
    Syntax(const Bytes& data, const GeometryLimits& limits) : data_(data), limits_(limits) {}
    std::vector<Node> Parse() { return List(0, 0); }
};
bool Is(const Node& n, const std::string& value) { return n.kind == Node::Atom && n.value == value; }
struct Element { const Node* header; const Node* body; };
std::vector<Element> Elements(const std::vector<Node>& list, const std::string& key) {
    std::vector<Element> result;
    for (std::size_t i = 0; i < list.size(); ++i) if (Is(list[i], key)) {
        std::size_t j = i + 1; const Node* header = NULL;
        if (j < list.size() && (list[j].kind == Node::Array || list[j].kind == Node::Quoted)) header = &list[j++];
        Check(j < list.size() && list[j].kind == Node::Block, "Malformed " + key + " block");
        Element e = {header, &list[j]}; result.push_back(e); i = j;
    }
    return result;
}
Element One(const std::vector<Node>& list, const std::string& key) {
    const std::vector<Element> elements = Elements(list, key);
    Check(elements.size() == 1, "Expected exactly one " + key + " block"); return elements[0];
}
std::size_t Declared(const Element& element, std::size_t limit) {
    Check(element.header && element.header->kind == Node::Array && element.header->children.size() == 1,
          "Expected counted array");
    const std::size_t count = Number(element.header->children[0]);
    Check(count <= limit, "Declared count exceeds configured limit"); return count;
}
const Node* Scalar(const std::vector<Node>& list, const std::string& key, std::size_t n = 1) {
    const Node* result = NULL;
    for (std::size_t i = 0; i < list.size(); ++i) if (Is(list[i], key)) {
        Check(!result && n <= list.size() - i - 1, "Repeated or truncated " + key);
        result = &list[i + 1];
    }
    return result;
}
struct Buffer {
    std::size_t count = 0, bits = 0;
    std::string file;
    std::vector<std::string> format, loadFormat;
    bool hasLoadFormat = false;
    bool inlineData = false; std::size_t offset = 0, size = 0;
};
std::vector<Buffer> Buffers(const std::vector<Node>& root, bool vertex, const GeometryLimits& limits) {
    const std::string key = vertex ? "VertexBuffer" : "IndexBuffer";
    const Element container = One(root, key);
    const std::size_t count = Declared(container, 4096);
    const std::vector<Element> children = Elements(container.body->children, key);
    Check(children.size() == count, key + " count mismatch");
    std::vector<Buffer> result;
    for (std::size_t i = 0; i < children.size(); ++i) {
        Check(!children[i].header, "Unexpected buffer entry header");
        const std::vector<Node>& body = children[i].body->children;
        Buffer buffer;
        const Element contents = One(body, vertex ? "Vertex" : "Index");
        buffer.count = Declared(contents, vertex ? limits.vertices : limits.triangles * 3);
        if (vertex) {
            const Element format = One(body, "VertexFormat");
            for (std::size_t f = 0; f < format.body->children.size(); ++f) {
                Check(format.body->children[f].kind == Node::Atom, "Unexpected vertex format syntax");
                buffer.format.push_back(format.body->children[f].value);
            }
            const std::vector<Element> loadFormats = Elements(body, "LoadVertexFormat");
            Check(loadFormats.size() <= 1, "Repeated LoadVertexFormat");
            if (!loadFormats.empty()) {
                Check(!loadFormats[0].header, "Unexpected LoadVertexFormat header");
                buffer.hasLoadFormat = true;
                const std::vector<Node>& fields = loadFormats[0].body->children;
                for (std::size_t f = 0; f < fields.size(); ++f) {
                    Check(fields[f].kind == Node::Atom, "Unexpected load vertex format syntax");
                    buffer.loadFormat.push_back(fields[f].value);
                }
            }
        } else { const Node* bits = Scalar(body, "BitDepth"); Check(bits != NULL, "Missing index bit depth"); buffer.bits = Number(*bits); }
        const Node* file = Scalar(contents.body->children, "file");
        if (file) { Check(file->kind == Node::Quoted, "Buffer reference must be quoted"); buffer.file = file->value; }
        for (std::size_t j = 0; j < contents.body->children.size(); ++j) if (contents.body->children[j].kind == Node::Binary) {
            Check(!buffer.inlineData && buffer.file.empty(), "Multiple buffer data sources");
            buffer.inlineData = true; buffer.offset = contents.body->children[j].offset; buffer.size = contents.body->children[j].size;
        }
        result.push_back(buffer);
    }
    return result;
}
std::string Normalize(std::string path) {
    Check(!path.empty() && path.size() <= 4096, "Empty or excessive buffer reference");
    for (std::size_t i = 0; i < path.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(path[i]);
        Check(c >= 32 && c < 127 && c != ':', "Invalid virtual buffer reference");
        if (c == '\\') path[i] = '/'; else if (c >= 'A' && c <= 'Z') path[i] = char(c + 32);
    }
    Check(path[0] != '/', "Absolute buffer reference is unsupported");
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string part = path.substr(start, end == std::string::npos ? end : end - start);
        Check(!part.empty() && part != "." && part != "..", "Invalid virtual path component");
        if (end == std::string::npos) break; start = end + 1;
    }
    const std::string suffix = ".md5rbin";
    if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
        path.resize(path.size() - suffix.size());
    return path;
}
std::uint32_t U32(const unsigned char* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
float F32(const unsigned char* p) {
    const std::uint32_t bits = U32(p); float value;
    static_assert(sizeof(value) == 4 && std::numeric_limits<float>::is_iec559, "Requires IEEE binary32 float");
    std::memcpy(&value, &bits, 4); Check(std::isfinite(value), "Non-finite vertex component"); return value;
}
std::size_t Product(std::size_t a, std::size_t b, std::size_t limit, const char* message) {
    Check(b == 0 || a <= limit / b, message); return a * b;
}
const char* const renderFormat[] = {"Position", "4", "Normal", "Dec_11_11_10N", "Tangent", "Dec_11_11_10N", "Binormal", "Dec_11_11_10N", "DiffuseColor", "TexCoord", "2", "0"};
const char* const modelPackedFormat[] = {"Position", "4", "Normal", "Dec_10_10_10N", "Tangent", "Dec_10_10_10N", "Binormal", "Dec_10_10_10N", "DiffuseColor", "TexCoord", "2", "0"};
const char* const floatRenderFormat[] = {"Position", "4", "Normal", "Tangent", "Binormal", "DiffuseColor", "TexCoord", "2", "0"};

} // namespace

static GeometryScene DecodeGeometryResource(const Bytes& data, const std::string& resourceName,
                                             const BufferResolver& resolver, const GeometryLimits& limits,
                                             bool staticModel) {
    Check(data.size() <= limits.sourceBytes && limits.triangles <= (std::numeric_limits<std::size_t>::max)() / 3,
          "Geometry source or configured triangle budget exceeds range");
    std::vector<Node> root = Syntax(data, limits).Parse();
    for (std::size_t i = 0; i < root.size(); ++i)
        Check(!Is(root[i], "ZeroBasedIndices"), "ZeroBasedIndices variant is not supported by this decoder");
    const Node* version = Scalar(root, staticModel ? "MD5RVersion" : "MD5RProcVersion");
    Check(version && Number(*version) == (staticModel ? 1u : 7u),
          staticModel ? "Only static MD5RVersion 1 is supported" : "Only MD5RProcVersion 7 is supported");
    Check(Scalar(root, staticModel ? "MD5RProcVersion" : "MD5RVersion") == NULL, "Conflicting geometry resource versions");
    if (staticModel) for (std::size_t i = 0; i < root.size(); ++i)
        Check(!Is(root[i], "Joint"), "Skeletal MD5R Joint data is unsupported by the static decoder");
    std::vector<Buffer> vb = Buffers(root, true, limits), ib = Buffers(root, false, limits);
    if (staticModel) for (std::size_t i = 0; i < vb.size(); ++i) {
        for (std::size_t f = 0; f < vb[i].format.size(); ++f)
            Check(vb[i].format[f].compare(0, 5, "Blend") != 0, "Skinned MD5R vertex formats are unsupported");
        for (std::size_t f = 0; f < vb[i].loadFormat.size(); ++f)
            Check(vb[i].loadFormat[f].compare(0, 5, "Blend") != 0, "Skinned MD5R load vertex formats are unsupported");
    }
    std::size_t modelCount;
    std::vector<Element> modelEntries;
    Node staticName(Node::Quoted), staticBody(Node::Block);
    if (staticModel) {
        Check(limits.models >= 1, "Static model exceeds configured model limit");
        modelCount = 1; staticName.value = resourceName;
        staticBody.children.swap(root);
        const Element model = {&staticName, &staticBody}; modelEntries.push_back(model);
    } else {
        const Element models = One(root, "Model");
        modelCount = Declared(models, limits.models);
        modelEntries = Elements(models.body->children, "Model");
        Check(modelEntries.size() == modelCount, "Model count mismatch");
    }
    const std::string normalizedName = Normalize(resourceName);
    const std::size_t slash = normalizedName.find_last_of('/');
    const std::string directory = slash == std::string::npos ? "" : normalizedName.substr(0, slash + 1);
    GeometryScene scene; scene.models = modelCount;
    std::size_t resolvedBytes = 0, totalTriangles = 0;
    std::map<std::size_t, std::size_t> vertexBases;
    std::map<std::size_t, Bytes> indexData;
    const auto load = [&](const Buffer& buffer) -> Bytes {
        Bytes bytes;
        if (buffer.inlineData) bytes.assign(data.begin() + buffer.offset, data.begin() + buffer.offset + buffer.size);
        else {
            Check(!buffer.file.empty(), "Referenced render buffer has no data");
            std::string name = Normalize(buffer.file);
            if (name.find('/') == std::string::npos) name = directory + name;
            Check(bool(resolver), "Missing PBBF resolver"); bytes = resolver(name);
        }
        Check(bytes.size() <= limits.bufferBytes && resolvedBytes <= limits.totalBufferBytes &&
              bytes.size() <= limits.totalBufferBytes - resolvedBytes, "Resolved geometry buffer budget exceeded");
        resolvedBytes += bytes.size(); return bytes;
    };
    for (std::size_t m = 0; m < modelEntries.size(); ++m) {
        const Element& model = modelEntries[m];
        Check(model.header && model.header->kind == Node::Quoted, "Model must have a quoted name");
        const Element meshes = One(model.body->children, "Mesh");
        const std::size_t meshCount = Declared(meshes, limits.surfaces);
        const std::vector<Element> meshEntries = Elements(meshes.body->children, "Mesh");
        Check(meshEntries.size() == meshCount, "Mesh count mismatch");
        for (std::size_t s = 0; s < meshEntries.size(); ++s) {
            const std::vector<Node>& mesh = meshEntries[s].body->children;
            const Element batches = One(mesh, "PrimBatch");
            const std::size_t batchCount = Declared(batches, limits.surfaces);
            const std::vector<Element> batchEntries = Elements(batches.body->children, "PrimBatch");
            Check(batchEntries.size() == batchCount, "Primitive batch count mismatch");
            struct Draw { std::size_t firstVertex, vertexCount, firstIndex, triangles; };
            std::vector<Draw> draws;
            for (std::size_t b = 0; b < batchEntries.size(); ++b) {
                const std::vector<Node>& batch = batchEntries[b].body->children;
                const Node* draw = Scalar(batch, "DrawIndexedTriList", 4);
                if (draw) { Draw d = {Number(draw[0]), Number(draw[1]), Number(draw[2]), Number(draw[3])}; draws.push_back(d); }
                for (std::size_t n = 0; n < batch.size(); ++n)
                    if (batch[n].kind == Node::Atom && batch[n].value.compare(0, 4, "Draw") == 0)
                        Check(batch[n].value == "DrawIndexedTriList", "Unsupported primitive draw command");
            }
            if (draws.empty()) { ++scene.skippedMeshes; continue; }
            const Node* buffers = Scalar(mesh, "DrawBuffers", 2);
            Check(buffers != NULL, "Primitive has no DrawBuffers");
            const std::size_t vi = Number(buffers[0]), ii = Number(buffers[1]);
            Check(vi < vb.size() && ii < ib.size(), "DrawBuffers index exceeds declared arrays");
            const Buffer& vertices = vb[vi]; const Buffer& indices = ib[ii];
            const std::vector<std::string>& format = vertices.hasLoadFormat ? vertices.loadFormat : vertices.format;
            const bool packed = format == std::vector<std::string>(renderFormat, renderFormat + 12) ||
                (staticModel && format == std::vector<std::string>(modelPackedFormat, modelPackedFormat + 12));
            const bool unpacked = format == std::vector<std::string>(floatRenderFormat, floatRenderFormat + 9);
            Check(packed || unpacked, "Unsupported render vertex format");
            const std::size_t stride = packed ? 40 : 64, uvOffset = packed ? 32 : 56;
            Check(indices.bits == 16 || indices.bits == 32, "Only 16/32-bit indices are supported");
            if (!vertexBases.count(vi)) {
                const std::size_t bytesExpected = Product(vertices.count, stride, limits.bufferBytes, "Vertex buffer size overflow");
                Bytes bytes = load(vertices); Check(bytes.size() == bytesExpected, "Vertex buffer byte count mismatch");
                Check(scene.vertices.size() <= limits.vertices && vertices.count <= limits.vertices - scene.vertices.size() &&
                      scene.vertices.size() + vertices.count <= (std::numeric_limits<std::uint32_t>::max)(), "Total vertex limit exceeded");
                vertexBases[vi] = scene.vertices.size();
                for (std::size_t v = 0; v < vertices.count; ++v) {
                    const unsigned char* p = bytes.data() + v * stride;
                    GeometryVertex vertex = {F32(p), F32(p + 4), F32(p + 8), F32(p + uvOffset), F32(p + uvOffset + 4)};
                    if (unpacked) for (unsigned normalOffset = 16; normalOffset < 52; normalOffset += 4) F32(p + normalOffset);
                    F32(p + 12); scene.vertices.push_back(vertex);
                }
            }
            if (!indexData.count(ii)) {
                const std::size_t bytesExpected = Product(indices.count, indices.bits / 8, limits.bufferBytes, "Index buffer size overflow");
                Bytes bytes = load(indices); Check(bytes.size() == bytesExpected, "Index buffer byte count mismatch"); indexData[ii].swap(bytes);
            }
            GeometrySurface surface; surface.model = model.header->value;
            const Node* material = Scalar(mesh, "Material");
            Check(material && material->kind == Node::Quoted, "Missing quoted mesh material"); surface.material = material->value;
            Check(scene.surfaces.size() < limits.surfaces, "Surface limit exceeded");
            const Node* bounds = Scalar(mesh, "Bounds", 6);
            Check(bounds != NULL, "Missing mesh bounds");
            float box[6]; for (unsigned b = 0; b < 6; ++b) box[b] = Real(bounds[b]);
            for (unsigned b = 0; b < 3; ++b) Check(box[b] <= box[b + 3], "Inverted mesh bounds");
            for (std::size_t d = 0; d < draws.size(); ++d) {
                const Draw& draw = draws[d];
                Check(draw.firstVertex <= vertices.count && draw.vertexCount <= vertices.count - draw.firstVertex,
                      "Draw vertex range exceeds buffer");
                const std::size_t count = Product(draw.triangles, 3, limits.triangles * 3, "Triangle count overflow");
                Check(draw.firstIndex <= indices.count && count <= indices.count - draw.firstIndex, "Draw index range exceeds buffer");
                Check(totalTriangles <= limits.triangles && draw.triangles <= limits.triangles - totalTriangles, "Total triangle limit exceeded");
                totalTriangles += draw.triangles;
                const Bytes& bytes = indexData[ii];
                for (std::size_t j = 0; j < count; ++j) {
                    const unsigned char* p = bytes.data() + (draw.firstIndex + j) * (indices.bits / 8);
                    const std::uint32_t index = indices.bits == 16 ? std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) : U32(p);
                    Check(index >= draw.firstVertex && index - draw.firstVertex < draw.vertexCount, "Triangle index outside declared draw vertex range");
                    const std::size_t finalIndex = vertexBases[vi] + index;
                    const GeometryVertex& vertex = scene.vertices[finalIndex];
                    const float position[] = {vertex.x, vertex.y, vertex.z};
                    for (unsigned b = 0; b < 3; ++b)
                        Check(position[b] >= box[b] - 0.02f && position[b] <= box[b + 3] + 0.02f, "Vertex outside declared mesh bounds");
                    surface.indices.push_back(std::uint32_t(finalIndex));
                }
            }
            scene.surfaces.push_back(std::move(surface));
        }
    }
    return scene;
}
GeometryScene DecodeProc7(const Bytes& data, const std::string& resourceName,
                          const BufferResolver& resolver, const GeometryLimits& limits) {
    return DecodeGeometryResource(data, resourceName, resolver, limits, false);
}
GeometryScene DecodeStaticModel1(const Bytes& data, const std::string& resourceName,
                                 const BufferResolver& resolver, const GeometryLimits& limits) {
    return DecodeGeometryResource(data, resourceName, resolver, limits, true);
}
} // namespace wolf2009

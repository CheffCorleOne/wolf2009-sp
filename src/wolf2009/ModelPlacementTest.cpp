// SPDX-License-Identifier: GPL-3.0-or-later
#include "ModelPlacement.h"
#ifdef WOLF2009_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void Reject(F op) {
    try {
        op();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("Unsupported model placement accepted");
}
void Text(EntityRecord &e, const char *key, const std::string &value) {
    EntityProperty p;
    p.key = key;
    p.text = value;
    e.properties.push_back(p);
}
void Numbers(EntityRecord &e, const char *key, std::initializer_list<float> value) {
    EntityProperty p;
    p.key = key;
    p.kind = EntityProperty::Floats;
    p.numbers = value;
    e.properties.push_back(p);
}
EntityRecord Entity() {
    EntityRecord e;
    Text(e, "classname", "synthetic_class");
    Text(e, "name", "synthetic_entity");
    Text(e, "model", "synthetic/model.md5r");
    Numbers(e, "origin", {10, 20, 30});
    return e;
}
GeometryScene Triangle() {
    GeometryScene g;
    g.models = 1;
    g.vertices = {{0, 0, 0, 0, 0}, {1, 0, 0, .5, 0}, {0, 1, 0, 0, .5}};
    GeometrySurface s;
    s.model = "synthetic/model";
    s.material = "synthetic/material";
    s.indices = {0, 1, 2};
    g.surfaces.push_back(s);
    return g;
}
void U32(Bytes &b, std::uint32_t x) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(x >> (i * 8)));
}
void Float(Bytes &b, std::size_t at, float x) {
    std::uint32_t bits;
    std::memcpy(&bits, &x, 4);
    for (int i = 0; i < 4; ++i)
        b[at + i] = static_cast<unsigned char>(bits >> (i * 8));
}
void Pad(Bytes &b) {
    while (b.size() % 4)
        b.push_back(0);
}
Bytes Vertices() {
    Bytes b(192, 0);
    for (int i = 0; i < 3; ++i)
        Float(b, i * 64 + 12, 1);
    Float(b, 64, 1);
    Float(b, 128 + 4, 1);
    Float(b, 64 + 56, .5);
    Float(b, 128 + 60, .5);
    return b;
}
Bytes Model(bool external = false) {
    std::string s = "MemSize 1\nMD5RVersion 1\nVertexBuffer[1]{VertexBuffer{VertexFormat{Position 4 Normal "
                    "Tangent Binormal DiffuseColor TexCoord 2 0} Vertex[3]{";
    const Bytes vb = Vertices(), ib = {0, 0, 1, 0, 2, 0};
    if (external)
        s += "file \"vertices.MD5RBin\"";
    else {
        s += "bin 192\n";
        s.append(reinterpret_cast<const char *>(vb.data()), vb.size());
    }
    s += "}}}IndexBuffer[1]{IndexBuffer{BitDepth 16 Index[3]{";
    if (external)
        s += "file \"indices.MD5RBin\"";
    else {
        s += "bin 6\n";
        s.append(reinterpret_cast<const char *>(ib.data()), ib.size());
    }
    s += "}}}Mesh[1]{Mesh{Material \"synthetic/material\" DrawBuffers 0 0 "
         "PrimBatch[1]{PrimBatch{DrawIndexedTriList 0 3 0 1}} Bounds 0 0 0 1 1 0}}Bounds 0 0 0 1 1 0\n";
    return Bytes(s.begin(), s.end());
}
typedef std::pair<std::string, Bytes> Resource;
Bytes ChunkBytes(const char *type, const std::vector<Resource> &files) {
    Bytes plain;
    U32(plain, static_cast<std::uint32_t>(files.size()));
    for (std::size_t i = 0; i < files.size(); ++i) {
        plain.insert(plain.end(), files[i].first.begin(), files[i].first.end());
        plain.push_back(0);
        U32(plain, static_cast<std::uint32_t>(files[i].second.size()));
        U32(plain, static_cast<std::uint32_t>(files[i].second.size()));
    }
    Pad(plain);
    for (std::size_t i = 0; i < files.size(); ++i) {
        plain.insert(plain.end(), files[i].second.begin(), files[i].second.end());
        Pad(plain);
    }
    uLongf size = compressBound(static_cast<uLong>(plain.size()));
    Bytes compressed(size);
    Need(compress2(compressed.data(), &size, plain.data(), static_cast<uLong>(plain.size()), Z_BEST_SPEED) ==
             Z_OK,
         "Synthetic compression failed");
    compressed.resize(size);
    Bytes out;
    for (int i = 3; i >= 0; --i)
        out.push_back(type[i]);
    U32(out, static_cast<std::uint32_t>(plain.size()));
    U32(out, static_cast<std::uint32_t>(compressed.size()));
    out.insert(out.end(), compressed.begin(), compressed.end());
    return out;
}
struct Package {
    Bytes bytes;
    Package() {
        U32(bytes, 300);
    }
    void Add(const char *type, const std::vector<Resource> &files) {
        const Bytes c = ChunkBytes(type, files);
        bytes.insert(bytes.end(), c.begin(), c.end());
    }
    PlacedStaticModel Load(const EntityDocument &doc, const std::string &name = "synthetic_entity",
                           const ModelPlacementOptions &options = ModelPlacementOptions()) const {
        std::istringstream input(std::string(bytes.begin(), bytes.end()), std::ios::binary | std::ios::in);
        const Archive a = Archive::Scan(input);
        return LoadPlacedStaticModel1(a, input, doc, name, options);
    }
};
} // namespace
int RunModelPlacementTests() {
    int passed = 0;
    try {
        EntityRecord e = Entity();
        GeometryScene g = Triangle();
        PlacedStaticModel p = ApplyAuthoredModelPlacement(g, e, "synthetic/model");
        Need(p.geometry.vertices[1].x == 11 && p.geometry.vertices[1].y == 20 &&
                 p.geometry.vertices[2].z == 30 && g.vertices[1].x == 1,
             "Translation or input ownership failed");
        Need(p.authored.hasOrigin && !p.authored.hasRotation &&
                 p.rotationSource == ModelRotationSource::ImplicitIdentity && p.authoredBasePoseOnly,
             "Authored placement fields lost");
        ++passed;
        Need(p.geometry.vertices[1].u == .5 && p.geometry.surfaces[0].indices == g.surfaces[0].indices &&
                 p.geometry.surfaces[0].material == "synthetic/material",
             "UV/indices/material changed");
        ++passed;
        Numbers(e, "rotation", {0, 1, 0, -1, 0, 0, 0, 0, 1});
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model"); });
        ++passed;
        ModelPlacementOptions basis;
        basis.rotation = ModelRotationConvention::BasisRows;
        p = ApplyAuthoredModelPlacement(g, e, "synthetic/model", basis);
        Need(p.geometry.vertices[1].x == 10 && p.geometry.vertices[1].y == 21 &&
                 p.geometry.vertices[2].x == 9 && p.geometry.vertices[2].y == 20 && p.nonIdentityRotation,
             "Explicit basis-row rotation failed");
        ++passed;
        e = Entity();
        Numbers(e, "rotation", {1, 0, 0, 0, 1, 0, 0, 0, 1});
        Numbers(e, "angle", {90});
        p = ApplyAuthoredModelPlacement(g, e, "synthetic/model");
        Need(p.angleSupersededByMatrix && p.rotationSource == ModelRotationSource::ExplicitMatrix &&
                 p.authored.angle == 90,
             "Explicit matrix did not take precedence");
        ++passed;
        e = Entity();
        Numbers(e, "angle", {90});
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model", basis); });
        e = Entity();
        Numbers(e, "angle", {0});
        Need(ApplyAuthoredModelPlacement(g, e, "synthetic/model").rotationSource ==
                 ModelRotationSource::ZeroAngle,
             "Zero angle unsupported");
        ++passed;
        e = Entity();
        Numbers(e, "rotation", {2, 0, 0, 0, 1, 0, 0, 0, 1});
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model", basis); });
        e = Entity();
        Numbers(e, "rotation", {-1, 0, 0, 0, 1, 0, 0, 0, 1});
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model", basis); });
        ++passed;
        e = Entity();
        e.properties.pop_back();
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model"); });
        e = Entity();
        e.properties.back().numbers[0] = (std::numeric_limits<float>::infinity)();
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model"); });
        ++passed;
        e = Entity();
        Text(e, "skin", "synthetic/skin");
        Text(e, "bind", "synthetic_parent");
        Text(e, "brain", "synthetic/behavior.brain");
        Numbers(e, "frame", {0});
        p = ApplyAuthoredModelPlacement(g, e, "synthetic/model");
        Need(p.unresolvedParent && p.unresolvedAnimation && p.unresolvedBehavior && p.unresolvedSkin &&
                 p.bind == "synthetic_parent" && p.dynamicPropertyKeys.size() == 3 &&
                 p.geometry.vertices[1].x == 11,
             "Dynamic metadata applied or lost");
        ++passed;
        e = Entity();
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "another/model"); });
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "../synthetic/model"); });
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "C:/synthetic/model"); });
        ++passed;
        GeometryScene bad = g;
        bad.surfaces[0].indices[1] = 9;
        Reject([&] { ApplyAuthoredModelPlacement(bad, e, "synthetic/model"); });
        bad = g;
        bad.surfaces[0].indices.push_back(0);
        Reject([&] { ApplyAuthoredModelPlacement(bad, e, "synthetic/model"); });
        bad = g;
        bad.vertices[0].x = (std::numeric_limits<float>::quiet_NaN)();
        Reject([&] { ApplyAuthoredModelPlacement(bad, e, "synthetic/model"); });
        ++passed;
        ModelPlacementOptions limit;
        limit.geometry.vertices = 2;
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model", limit); });
        limit = ModelPlacementOptions();
        limit.geometry.triangles = 0;
        Reject([&] { ApplyAuthoredModelPlacement(g, e, "synthetic/model", limit); });
        ++passed;
        bad = g;
        bad.vertices[1].x = (std::numeric_limits<float>::max)();
        e.properties.back().numbers[0] = (std::numeric_limits<float>::max)();
        Reject([&] { ApplyAuthoredModelPlacement(bad, e, "synthetic/model"); });
        ++passed;
        EntityDocument doc;
        doc.entities.push_back(Entity());
        Package pack;
        pack.Add("MODL", {{"synthetic/model", Model()}});
        p = pack.Load(doc, "SYNTHETIC_ENTITY");
        Need(p.entityIndex == 0 && p.resourceName == "synthetic/model" && p.geometry.vertices.size() == 3 &&
                 p.geometry.vertices[1].x == 11,
             "Archive/ENTS/MODL placement integration failed");
        ++passed;
        Reject([&] { pack.Load(doc, ""); });
        Reject([&] { pack.Load(doc, "missing"); });
        doc.entities.push_back(doc.entities[0]);
        Reject([&] { pack.Load(doc); });
        doc.entities.pop_back();
        ++passed;
        Package duplicate;
        duplicate.Add("MODL", {{"synthetic/model", Model()}});
        duplicate.Add("MODL", {{"SYNTHETIC/MODEL.MD5R", Model()}});
        Reject([&] { duplicate.Load(doc); });
        Package missing;
        Reject([&] { missing.Load(doc); });
        ++passed;
        Text(doc.entities[0], "bind", "parent");
        EntityRecord parent;
        Text(parent, "name", "parent");
        doc.entities.push_back(parent);
        p = pack.Load(doc);
        Need(p.unresolvedParent && p.parentRecordFound && p.parentEntityIndex == 1 &&
                 !p.parentRecordAmbiguous,
             "Parent record diagnostic failed");
        doc.entities.push_back(parent);
        p = pack.Load(doc);
        Need(!p.parentRecordFound && p.parentRecordAmbiguous &&
                 p.parentEntityIndex == static_cast<std::size_t>(-1),
             "Ambiguous parent was resolved");
        doc.entities.resize(1);
        p = pack.Load(doc);
        Need(!p.parentRecordFound && !p.parentRecordAmbiguous && p.unresolvedParent,
             "Missing parent was resolved");
        ++passed;
        Package external;
        external.Add("MODL", {{"synthetic/model", Model(true)}});
        external.Add("PBBF", {{"synthetic/vertices", Vertices()}});
        external.Add("PBBF", {{"synthetic/indices", {0, 0, 1, 0, 2, 0}}});
        p = external.Load(doc);
        Need(p.geometry.vertices[1].x == 11 && p.geometry.surfaces.size() == 1,
             "Repeated PBBF chunks not resolved");
        ++passed;
        Package noBuffer;
        noBuffer.Add("MODL", {{"synthetic/model", Model(true)}});
        Reject([&] { noBuffer.Load(doc); });
        external.Add("PBBF", {{"SYNTHETIC/VERTICES.MD5RBIN", Vertices()}});
        Reject([&] { external.Load(doc); });
        ++passed;
        Bytes joints = Model();
        const std::string marker = "Joint[0]{}";
        joints.insert(joints.end(), marker.begin(), marker.end());
        Package skeletal;
        skeletal.Add("MODL", {{"synthetic/model", joints}});
        Reject([&] { skeletal.Load(doc); });
        ++passed;
        std::cout << "Model placement self-test: " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Model placement self-test failed after " << passed << " checks: " << error.what()
                  << '\n';
        return 1;
    }
}
} // namespace wolf2009

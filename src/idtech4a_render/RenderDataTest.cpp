// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderData.h"
#include "StreamMounts.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
namespace {
using namespace wolf2009;
using namespace wolf2009::idtech4a;
void Need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
Bytes ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    Need(bool(file), "could not open owner-supplied file");
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    Need(size >= 0 && size <= 64u * 1024u * 1024u, "test input outside bounded read range");
    file.seekg(0);
    Bytes bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), size);
    Need(bool(file), "short test read");
    return bytes;
}
void Reject(const std::function<void()>& work) {
    bool rejected = false;
    try { work(); } catch (const std::exception&) { rejected = true; }
    Need(rejected, "invalid adapter input accepted");
}
int SelfTest() {
    Need(RunGeometryTests() == 0, "common geometry/raw-colour self-tests");
    const ResourcePath path = ParseResourcePath("WOLF2009/Main/PROC/maps/game/empty/empty.proc");
    Need(path.mount == "main" && path.type == "proc" &&
         path.Path("PBBF", "maps/game/empty/world_vb1") ==
         "wolf2009/main/pbbf/maps/game/empty/world_vb1", "typed source path changed");
    for (const char* invalid : {"../map.proc", "wolf2009//proc/maps/map", "wolf2009/a/proc/../escape",
                                "wolf2009/a/proc/C:/file", "wolf2009/a/proc/a\"b",
                                "wolf2009/a/proc/map#_area0"}) {
        Reject([&] { ParseResourcePath(invalid); });
    }
    GeometryScene scene;
    scene.vertices = {{100,100,100,0,0}, {1,2,3,0.5f,0.75f}, {2,2,3,1,0}, {1,3,3,0,1}};
    GeometrySurface first; first.model = "_area0"; first.material = "test/simple"; first.indices = {3,1,2};
    GeometrySurface second = first; second.model = "_inline"; second.indices = {2,3,1};
    scene.surfaces = {first, second};
    const auto surfaces = CompactSurfaces(scene);
    Need(surfaces.size() == 2 && surfaces[0].vertices.size() == 3 &&
         surfaces[0].vertices[0].y == 3 && surfaces[0].vertices[1].u == 0.5f &&
         surfaces[0].indices == std::vector<std::uint32_t>({0,1,2}) &&
         surfaces[1].vertices[0].x == 2, "surface compaction or index order changed");
    scene.surfaces[0].indices[0] = 99;
    Reject([&] { CompactSurfaces(scene); });
    MaterialCatalog catalog;
    catalog.AddMaterialSource("synthetic.mtr",
        "test/simple { Image DiffuseMap textures/check } "
        "test/alpha { Image DiffuseMap textures/leaf AlphaTest 0.5 } "
        "test/complex { Technique UnsupportedTechnique Image DiffuseMap textures/check }");
    const DiffuseMaterial diffuse = BuildDiffuseMaterial(path, "test/simple", &catalog);
    Need(diffuse.supported && diffuse.image == "wolf2009/main/txtr/textures/check.dds" &&
         diffuse.declaration.find("map \"wolf2009/main/txtr/textures/check.dds\"") != std::string::npos,
         "native diffuse path missing source qualification");
    const auto alpha = BuildDiffuseMaterial(path, "test/alpha", &catalog);
    Need(alpha.supported && alpha.declaration.find("alphaTest 0.5") != std::string::npos,
         "native alpha test lost");
    const auto unsupported = BuildDiffuseMaterial(path, "test/complex", &catalog);
    Need(!unsupported.supported && unsupported.image.empty(), "unsupported shader rendered as opaque diffuse");
    const auto absent = BuildDiffuseMaterial(path, "test/simple", NULL);
    Need(!absent.supported && absent.declaration.find("_default") != std::string::npos, "missing catalog fallback");
    bool invoked = false;
    Reject([&] {
        const ResourcePath wrong = ParseResourcePath("wolf2009/a/txtr/textures/check.dds");
        LoadGeometry(wrong, [&](const std::string&) { invoked = true; return Bytes(); });
    });
    Need(!invoked, "wrong resource type was read as geometry");
    std::cout << "WOLF2009_RENDER_ADAPTER_SELFTEST_OK\n";
    return 0;
}
void CheckMap(const std::string& base, const char* label, const char* package,
              const MaterialCatalog& catalog) {
    StreamMounts mounts([&](const std::string&) {
        return std::unique_ptr<std::istream>(new std::ifstream(base + "/" + package, std::ios::binary));
    });
    mounts.Mount(label);
    std::ifstream file(base + "/" + package, std::ios::binary);
    const Archive archive = Archive::Scan(file);
    std::string proc;
    for (const Chunk& chunk : archive.Chunks()) if (chunk.type == "PROC") {
        for (const Entry& entry : chunk.entries) { Need(proc.empty(), "multiple PROC resources"); proc = entry.name; }
    }
    Need(!proc.empty(), "missing PROC resource");
    const ResourcePath path = ParseResourcePath(std::string("wolf2009/") + label + "/PROC/" + proc);
    const ResourceReader read = [&](const std::string& virtualName) {
        const ResourcePath selected = ParseResourcePath(virtualName);
        Need(selected.mount == label, "adapter escaped selected source");
        std::string name = selected.name;
        const char* suffix = selected.type == "proc" ? ".proc" : selected.type == "modl" ? ".md5r" :
                             selected.type == "txtr" ? ".dds" : ".md5rbin";
        const auto exact = mounts.Find(selected.type, name);
        if (exact.status == StreamLookupStatus::Unique) return mounts.Read(exact.matches[0]);
        const std::size_t n = std::string(suffix).size();
        if (name.size() >= n && name.compare(name.size() - n, n, suffix) == 0) name.resize(name.size() - n);
        return mounts.ReadUnique(selected.type, name);
    };
    const GeometryScene scene = LoadGeometry(path, read);
    const auto surfaces = CompactSurfaces(scene);
    std::size_t worldSurfaces = 0, triangles = 0, compactVertices = 0, diffuseSurfaces = 0, decodedTextures = 0;
    std::set<std::string> textures, materialNames;
    std::map<std::string, DiffuseMaterial> materials;
    for (const RenderSurface& surface : surfaces) {
        if (surface.model.compare(0, 5, "_area") != 0) continue;
        ++worldSurfaces; triangles += surface.indices.size() / 3; compactVertices += surface.vertices.size();
        Need(*std::max_element(surface.indices.begin(), surface.indices.end()) < surface.vertices.size(),
             "compacted native indices invalid");
        if (materials.find(surface.material) == materials.end()) {
            materials[surface.material] = BuildDiffuseMaterial(path, surface.material, &catalog);
        }
        const DiffuseMaterial& native = materials[surface.material];
        if (native.supported) {
            try {
                if (textures.insert(native.image).second) {
                    const DdsImage image = DecodeDdsMip0(read(native.image));
                    Need(image.rgba.size() == std::size_t(image.width) * image.height * 4,
                         "native RGBA allocation size mismatch");
                    ++decodedTextures;
                }
                ++diffuseSurfaces;
            } catch (const std::exception&) { textures.erase(native.image); }
        }
    }
    Need(worldSurfaces && triangles && diffuseSurfaces && decodedTextures, "real map has no supported native diffuse geometry");
    // Validate several actual static MODL resources through the exact same
    // normal-loader read/compact path. Skeletal formats are explicit rejections.
    std::size_t staticModels = 0, staticRejected = 0;
    for (const Chunk& chunk : archive.Chunks()) if (chunk.type == "MODL" && staticModels < 3) {
        for (const Entry& entry : chunk.entries) {
            if (staticModels >= 3) break;
            try {
                const ResourcePath model = ParseResourcePath(std::string("wolf2009/") + label + "/MODL/" + entry.name);
                const auto data = CompactSurfaces(LoadGeometry(model, read));
                if (!data.empty()) {
                    ++staticModels;
                    std::cout << "RENDER_ADAPTER_STATIC map=" << label << " resource=" << entry.name
                              << " surfaces=" << data.size() << "\n";
                }
            } catch (const std::exception&) { ++staticRejected; }
        }
    }
    std::cout << "RENDER_ADAPTER_MAP name=" << label << " source_models=" << scene.models
              << " world_surfaces=" << worldSurfaces << " triangles=" << triangles
              << " compact_vertices=" << compactVertices << " diffuse_surfaces=" << diffuseSurfaces
              << " decoded_diffuse_textures=" << decodedTextures << " static_models=" << staticModels
              << " unsupported_static_candidates=" << staticRejected << "\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") return SelfTest();
        Need(argc == 2, "usage: wolf2009-render-check --self-test | OWNER_SP_BASE");
        const std::string base = argv[1];
        const auto catalog = ReadMaterialCatalog(ReadFile(base + "/assets.pk4"));
        std::cout << "RENDER_ADAPTER_CATALOG materials=" << catalog->MaterialCount()
                  << " guides=" << catalog->GuideCount() << "\n";
        CheckMap(base, "empty", "maps/game/empty/empty.mpk", *catalog);
        CheckMap(base, "trainyard", "maps/game/trainyard/trainyard.mpk", *catalog);
        CheckMap(base, "mtw_ware", "maps/game/mtw/mtw_ware.mpk", *catalog);
        std::cout << "WOLF2009_RENDER_ADAPTER_REAL_DATA_OK scope=decoder-and-adapter-no-GL\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "RENDER_ADAPTER_FAIL " << error.what() << "\n"; return 1; }
}

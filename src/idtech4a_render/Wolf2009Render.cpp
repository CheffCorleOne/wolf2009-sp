// SPDX-License-Identifier: GPL-3.0-or-later
// Adapter only: all asset parsing is performed by the independent GPL core.
// STL must precede idStr's legacy C-library macro replacements.
#include "RenderData.h"
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "Wolf2009Render.h"
namespace {
using wolf2009::idtech4a::Bytes;
using wolf2009::idtech4a::ResourcePath;
using wolf2009::idtech4a::RenderSurface;
using wolf2009::idtech4a::DiffuseMaterial;
std::unique_ptr<wolf2009::MaterialCatalog> materialCatalog;
std::map<std::string, DiffuseMaterial> materials;
void Need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void Report(const char* operation, const char* path, const std::exception& error) {
    common->Warning("Wolf2009 renderer %s '%s': %s", operation, path ? path : "", error.what());
}
class EngineFile {
    idFile* file;
  public:
    explicit EngineFile(const char* path) : file(fileSystem->OpenFileRead(path, false)) {}
    ~EngineFile() { if (file) fileSystem->CloseFile(file); }
    idFile* operator->() const { return file; }
    operator bool() const { return file != NULL; }
};
Bytes Read(const std::string& path, std::size_t limit = 256u * 1024u * 1024u) {
    EngineFile file(path.c_str());
    Need(bool(file), "resource absent from selected engine source");
    const int length = file->Length();
    Need(length >= 0 && std::size_t(length) <= limit, "resource length exceeds renderer budget");
    Bytes bytes(length);
    Need(!length || file->Read(bytes.data(), length) == length, "short read through engine idFile");
    return bytes;
}
const DiffuseMaterial& Material(const ResourcePath& source, const RenderSurface& surface) {
    DiffuseMaterial value = wolf2009::idtech4a::BuildDiffuseMaterial(source, surface, materialCatalog.get());
    const std::string key = value.name;
    const auto previous = materials.find(key);
    if (previous != materials.end()) return previous->second;
    if (value.supported) {
        // Validate DDS even on a dedicated server with no GL. Native image
        // upload subsequently reopens the same source-qualified TXTR idFile.
        try { wolf2009::DecodeDdsMip0(Read(value.image, 64u * 1024u * 1024u)); }
        catch (const std::exception& error) {
            value.supported = false;
            value.reason = error.what();
            value.image.clear();
            value.declaration = "{\n noShadows\n { map _default }\n}\n";
        }
    }
    if (!value.supported) {
        common->Warning("Wolf2009 diffuse '%s': %s", surface.material.c_str(), value.reason.c_str());
    }
    idDecl* decl = declManager->CreateNewDecl(DECL_MATERIAL, value.name.c_str(), "<wolf2009-generated>");
    decl->SetText(value.declaration.c_str());
    decl->Invalidate();
    const idMaterial* native = declManager->FindMaterial(value.name.c_str());
    Need(native != NULL && native->GetState() != DS_DEFAULTED, "generated native material could not be parsed");
    return materials.insert(std::make_pair(key, std::move(value))).first->second;
}
void AddSurface(idRenderModelStatic* model, const RenderSurface& source, const ResourcePath& path, int id) {
    const DiffuseMaterial& material = Material(path, source);
    Need(source.vertices.size() <= INT_MAX && source.indices.size() <= INT_MAX, "surface exceeds native int range");
    // glIndex_t can be signed 16-bit on older mobile configurations.
    const std::size_t maxIndex = sizeof(glIndex_t) < 4 ? 32767u : std::size_t(INT_MAX);
    Need(source.vertices.size() <= maxIndex + 1, "surface exceeds native renderer index format");
    for (const wolf2009::GeometryVertex& v : source.vertices) {
        Need(std::isfinite(v.u * (material.supported ? material.binding.uvScale[0] : 1.0f)) &&
             std::isfinite(v.v * (material.supported ? material.binding.uvScale[1] : 1.0f)),
             "scaled native UV is non-finite");
    }
    srfTriangles_t* tri = model->AllocSurfaceTriangles(int(source.vertices.size()), int(source.indices.size()));
    tri->numVerts = int(source.vertices.size());
    tri->numIndexes = int(source.indices.size());
    tri->generateNormals = true;
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        const wolf2009::GeometryVertex& v = source.vertices[i];
        tri->verts[i].Clear();
        tri->verts[i].xyz.Set(v.x, v.y, v.z);
        const float u = v.u * (material.supported ? material.binding.uvScale[0] : 1.0f);
        const float t = v.v * (material.supported ? material.binding.uvScale[1] : 1.0f);
        tri->verts[i].st.Set(u, t);
        for (int c = 0; c < 4; ++c) tri->verts[i].color[c] = 255;
    }
    for (std::size_t i = 0; i < source.indices.size(); ++i) tri->indexes[i] = glIndex_t(source.indices[i]);
    R_BoundTriSurf(tri);
    modelSurface_t surface = {};
    surface.id = id;
    surface.shader = declManager->FindMaterial(material.name.c_str());
    surface.geometry = tri;
    model->AddSurface(surface);
}
class WorldModel : public idRenderModelStatic {
  public:
    WorldModel(const char* name, bool world) {
        InitEmpty(name);
        isStaticWorldModel = world;
    }
};
void Materials_f(const idCmdArgs& args) {
    if (args.Argc() != 2) {
        common->Printf("usage: wolf2009Materials <relative archive>\n");
        return;
    }
    // A failed startup/reload command must not silently retain a different
    // catalog. Existing native surfaces keep their declarations; future source
    // resolution gets the explicit checker fallback until a successful load.
    Wolf2009_ClearRenderCatalog();
    const bool loaded = Wolf2009_LoadMaterialCatalog(args.Argv(1));
    common->Printf("WOLF2009_MATERIALS path=%s loaded=%d\n", args.Argv(1), int(loaded));
}
}
void Wolf2009_InitRenderCommands() {
    cmdSystem->AddCommand("wolf2009Materials", Materials_f, CMD_FL_RENDERER,
                          "load the owner-supplied Wolf2009 material archive before map startup");
}
void Wolf2009_ShutdownRenderCommands() {
    cmdSystem->RemoveCommand("wolf2009Materials");
    Wolf2009_ClearRenderCatalog();
}
bool Wolf2009_IsProcPath(const char* path) { return wolf2009::idtech4a::HasResourceType(path, "PROC"); }
bool Wolf2009_IsStaticModelPath(const char* path) { return wolf2009::idtech4a::HasResourceType(path, "MODL"); }
bool Wolf2009_IsTexturePath(const char* path) { return wolf2009::idtech4a::HasResourceType(path, "TXTR"); }
bool Wolf2009_LoadMaterialCatalog(const char* path) {
    try {
        Need(path && *path && !Wolf2009_IsTexturePath(path), "missing material archive path");
        std::unique_ptr<wolf2009::MaterialCatalog> next = wolf2009::idtech4a::ReadMaterialCatalog(Read(path, 64u * 1024u * 1024u));
        materialCatalog.swap(next);
        materials.clear();
        common->Printf("Wolf2009 native materials: %u materials, %u guides\n",
                       unsigned(materialCatalog->MaterialCount()), unsigned(materialCatalog->GuideCount()));
        return true;
    } catch (const std::exception& error) { Report("catalog", path, error); return false; }
}
void Wolf2009_ClearRenderCatalog() { materials.clear(); materialCatalog.reset(); }
void Wolf2009_InlineModelName(const char* path, const char* model, idStr& out) {
    ResourcePath source = wolf2009::idtech4a::ParseResourcePath(path);
    idStr name = source.Path("PROC", source.name).c_str();
    name.SetFileExtension(".proc");
    out = name;
    out += "#";
    out += model;
}
bool Wolf2009_LoadStaticModel(idRenderModelStatic* model, const char* path) {
    try {
        Need(model && path, "missing native static model target/path");
        const ResourcePath source = wolf2009::idtech4a::ParseResourcePath(path);
        Need(source.type == "modl", "native static model requires MODL path");
        const auto scene = wolf2009::idtech4a::LoadGeometry(source, [](const std::string& p) { return Read(p); });
        const auto surfaces = wolf2009::idtech4a::CompactSurfaces(scene);
        Need(!surfaces.empty(), "static MODL contains no supported render surfaces");
        for (std::size_t i = 0; i < surfaces.size(); ++i) AddSurface(model, surfaces[i], source, int(i));
        common->Printf("Wolf2009 native static model: %s, %u surfaces\n", path, unsigned(surfaces.size()));
        return true;
    } catch (const std::exception& error) {
        if (model) model->PurgeModel();
        Report("model", path, error);
        return false;
    }
}
bool Wolf2009_LoadRenderWorld(idRenderWorldLocal* world, const char* path) {
    try {
        Need(world && path, "missing native world target/path");
        const ResourcePath source = wolf2009::idtech4a::ParseResourcePath(path);
        Need(source.type == "proc", "native render world requires PROC path");
        const auto scene = wolf2009::idtech4a::LoadGeometry(source, [](const std::string& p) { return Read(p); });
        const auto surfaces = wolf2009::idtech4a::CompactSurfaces(scene);
        std::map<std::string, std::unique_ptr<WorldModel> > next;
        idStr areaName;
        Wolf2009_InlineModelName(path, "_area0", areaName);
        next["_area0"].reset(new WorldModel(areaName.c_str(), true));
        std::size_t worldSurfaces = 0, triangles = 0;
        for (const RenderSurface& surface : surfaces) {
            // PROC submodels are local-space. Register them, but never place them
            // at origin; the game-side ENTS translator owns their placement.
            const bool area = surface.model.compare(0, 5, "_area") == 0;
            const std::string key = area ? "_area0" : surface.model;
            if (!next[key]) {
                idStr modelName;
                Wolf2009_InlineModelName(path, key.c_str(), modelName);
                next[key].reset(new WorldModel(modelName.c_str(), false));
            }
            WorldModel* model = next[key].get();
            AddSurface(model, surface, source, model->NumSurfaces());
            if (area) { ++worldSurfaces; triangles += surface.indices.size() / 3; }
        }
        Need(worldSurfaces != 0, "PROC contains no _area world render surfaces");
        for (auto& model : next) model.second->FinishSurfaces();
        // Parse and finish all resources first; malformed input leaves an
        // already loaded world intact. Commit only complete native models.
        world->FreeWorld();
        world->ClearWorld();
        world->numAreaNodes = 1;
        world->CommonChildrenArea_r(&world->areaNodes[0]);
        world->mapName = path;
        world->mapTimeStamp = 0; // Mount generations are not filesystem timestamps.
        for (auto& model : next) {
            model.second->SetLevelLoadReferenced(true);
            renderModelManager->AddModel(model.second.get());
            world->localModels.Append(model.second.release());
        }
        world->AddWorldModelEntities();
        world->ClearPortalStates();
        common->Printf("Wolf2009 native render world: %s, %u models, %u world surfaces, %u triangles; single area\n",
                       path, unsigned(next.size()), unsigned(worldSurfaces), unsigned(triangles));
        return true;
    } catch (const std::exception& error) { Report("world", path, error); return false; }
}
bool Wolf2009_LoadImage(const char* path, unsigned char** pic, int* width, int* height, ID_TIME_T* timestamp) {
    if (pic) *pic = NULL;
    if (width) *width = 0;
    if (height) *height = 0;
    if (timestamp) *timestamp = FILE_NOT_FOUND_TIMESTAMP;
    try {
        Need(Wolf2009_IsTexturePath(path), "native image requires explicit TXTR source");
        const wolf2009::DdsImage image = wolf2009::DecodeDdsMip0(Read(path, 64u * 1024u * 1024u));
        if (width) *width = int(image.width);
        if (height) *height = int(image.height);
        if (timestamp) *timestamp = 0;
        if (pic) {
            *pic = static_cast<unsigned char*>(R_StaticAlloc(int(image.rgba.size())));
            std::memcpy(*pic, image.rgba.data(), image.rgba.size());
        }
        return true;
    } catch (const std::exception& error) { Report("image", path, error); return false; }
}

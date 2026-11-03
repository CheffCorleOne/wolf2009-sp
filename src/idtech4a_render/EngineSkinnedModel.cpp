// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineSkinnedModelData.h"
#include "SkinCatalog.h"
#include "ZipArchive.h"
#include <climits>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "Wolf2009Render.h"
namespace {
using namespace wolf2009;
using namespace wolf2009::idtech4a;
SkinnedModelRequests requests;
struct ImportedDraw {
    std::string material;
    std::size_t vertices = 0, indices = 0;
    DiffuseMaterial profile;
    const idMaterial* native = NULL;
};
struct Appearance {
    std::string model, skin;
    std::shared_ptr<const Bytes> catalog;
};
std::map<std::string, Appearance> appearances;
std::map<std::uint64_t, std::shared_ptr<const Bytes>> appearanceCatalogs;
std::size_t appearanceCatalogBytes = 0;
void Need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
class AlignedMemory {
    void* value;
  public:
    explicit AlignedMemory(std::size_t bytes) : value(Mem_Alloc16(bytes)) {}
    ~AlignedMemory() { Mem_Free16(value); }
    void* Get() const { return value; }
  private:
    AlignedMemory(const AlignedMemory&);
    AlignedMemory& operator=(const AlignedMemory&);
};
class SourceLeases {
    std::map<std::string, unsigned long long> leases;
  public:
    SkinnedModelSourceInfo sources;
    // No retained geometry or weights: appearance registration reopens the
    // leased source and checks its byte identity against this resident model.
    std::vector<ImportedDraw> draws;
    ~SourceLeases() {
        for (const auto& lease : leases) Wolf2009_UnpinMount(lease.first.c_str(), lease.second);
    }
    void Pin(const std::string& path, std::uint64_t expected) {
        const auto label = ParseResourcePath(path).mount;
        const auto held = leases.find(label);
        if (held != leases.end()) { Need(held->second == expected, "inconsistent epochs for a shared skinned source mount"); return; }
        unsigned long long epoch = 0;
        Need(expected && Wolf2009_PinMount(label.c_str(), epoch), "skinned source mount could not be pinned");
        if (epoch != expected) { Wolf2009_UnpinMount(label.c_str(), epoch); throw std::runtime_error("stale skinned source mount epoch"); }
        try { leases.insert(std::make_pair(label, epoch)); }
        catch (...) { Wolf2009_UnpinMount(label.c_str(), epoch); throw; }
    }
    void Check() const {
        for (const auto& lease : leases) {
            unsigned long long epoch = 0;
            Need(Wolf2009_MountEpoch(lease.first.c_str(), epoch) && epoch == lease.second, "skinned source epoch changed while leased");
        }
    }
};
std::map<const idRenderModelMD5*, std::unique_ptr<SourceLeases>> residentSources;
class File {
    idFile* value;
  public:
    explicit File(const char* path) : value(fileSystem->OpenFileRead(path, false)) {}
    ~File() { if (value) fileSystem->CloseFile(value); }
    Bytes Read() {
        Need(value != NULL, "skinned resource absent from the explicitly selected source");
        const int length = value->Length();
        Need(length > 0 && length <= 64 * 1024 * 1024, "skinned resource byte budget");
        Bytes bytes(static_cast<std::size_t>(length));
        Need(value->Read(bytes.data(), length) == length, "short skinned source idFile read");
        return bytes;
    }
};
std::uint64_t Hash(const unsigned char* bytes, std::size_t count, std::uint64_t hash = UINT64_C(14695981039346656037)) {
    for (std::size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}
std::string Hex(std::uint64_t value) {
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[std::size_t(i)] = "0123456789abcdef"[value & 15]; value >>= 4; }
    return out;
}
bool SameIdentity(const SkinnedSourceIdentity& a, const SkinnedSourceIdentity& b) {
    return a.path == b.path && a.bytes == b.bytes && a.fingerprint == b.fingerprint;
}
bool SameMaterial(const DiffuseMaterial& a, const DiffuseMaterial& b) {
    return a.supported == b.supported && a.name == b.name && a.image == b.image && a.declaration == b.declaration &&
           a.nativeVertexColor == b.nativeVertexColor && a.identityWhiteVertexColor == b.identityWhiteVertexColor &&
           a.binding.uvScale[0] == b.binding.uvScale[0] && a.binding.uvScale[1] == b.binding.uvScale[1] &&
           a.binding.shaderClass == b.binding.shaderClass && a.binding.hasAlphaTest == b.binding.hasAlphaTest &&
           a.binding.alphaTest == b.binding.alphaTest;
}
idQuat NativeRotation(const GreeterJointPose& source) {
    double norm = 0;
    for (double value : source.rotation) { Need(std::isfinite(value), "nonfinite skinned supplied quaternion"); norm += value * value; }
    Need(std::abs(norm - 1) <= 1e-5, "nonunit skinned supplied quaternion");
    return idQuat(float(-source.rotation[0]), float(-source.rotation[1]), float(-source.rotation[2]), float(source.rotation[3]));
}
idVec3 NativeTranslation(const GreeterJointPose& source) {
    for (double value : source.translation) Need(std::isfinite(value) && std::abs(value) <= 1e8, "invalid skinned supplied translation");
    return idVec3(float(source.translation[0]), float(source.translation[1]), float(source.translation[2]));
}
void Register_f(const idCmdArgs& args) {
    if (args.Argc() != 5) {
        common->Printf("usage: wolf2009SkinnedModel <MODL path> <HKXA path> <SKEL path> <LOD>\n"); return;
    }
    const char* text = args.Argv(4);
    int lod = 0;
    if (!*text) { common->Warning("Wolf2009 skinned LOD is empty"); return; }
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || lod > 25 || (lod == 25 && *p > '5')) {
            common->Warning("Wolf2009 skinned LOD must be an explicit integer 0..255"); return;
        }
        lod = lod * 10 + *p - '0';
    }
    idStr name;
    const bool registered = Wolf2009_RegisterSkinnedModel(args.Argv(1), args.Argv(2), args.Argv(3), lod, name);
    common->Printf("WOLF2009_SKINNED_BINDING registered=%d model=%s\n", int(registered), name.c_str());
}
}
void Wolf2009_InitSkinnedModelCommands() {
    cmdSystem->AddCommand("wolf2009SkinnedModel", Register_f, CMD_FL_RENDERER,
                          "bind an explicit source MODL/HKXA/SKEL/LOD request to a native MD5 model");
}
void Wolf2009_ShutdownSkinnedModelCommands() {
    cmdSystem->RemoveCommand("wolf2009SkinnedModel"); requests.Clear();
    appearances.clear(); appearanceCatalogs.clear(); appearanceCatalogBytes = 0;
    // Model-manager Shutdown invokes this before deleting its models. Each
    // model keeps its mount pins until its own PurgeModel/destructor runs.
}
bool Wolf2009_RegisterSkinnedModel(const char* modl, const char* animation, const char* skeleton, int lod, idStr& name) {
    name.Clear();
    try {
        Need(modl && animation && skeleton && lod >= 0, "missing skinned source request");
        auto request = MakeSkinnedModelRequest(modl, animation, skeleton, std::size_t(lod));
        const std::array<std::string,3> paths{{request.model, request.animation, request.skeleton}};
        for (unsigned i = 0; i < 3; ++i) {
            unsigned long long epoch = 0;
            Need(Wolf2009_MountEpoch(ParseResourcePath(paths[i]).mount.c_str(), epoch) && epoch, "skinned binding source mount is absent");
            request.sourceEpochs[i] = epoch;
        }
        name = requests.Register(request).c_str();
        return true;
    } catch (const std::exception& error) { common->Warning("Wolf2009 skinned binding: %s", error.what()); return false; }
}
bool Wolf2009_IsSkinnedModelPath(const char* path) {
    // Recognize the namespace even when a request is missing. A stale/unknown
    // alias must fail explicitly instead of silently becoming a static model.
    return HasResourceType(path, "MD5R");
}
bool Wolf2009_LoadSkinnedModel(idRenderModelMD5* model, const char* name) {
    try {
        Need(model && name, "missing native skinned model target/name");
        SkinnedModelRequest request;
        Need(requests.Find(name, request), "unregistered or stale skinned model request");
        std::unique_ptr<SourceLeases> leases(new SourceLeases);
        leases->Pin(request.model, request.sourceEpochs[0]);
        leases->Pin(request.animation, request.sourceEpochs[1]);
        leases->Pin(request.skeleton, request.sourceEpochs[2]);
        const auto source = LoadSkinnedModel(request, [](const std::string& path) { return File(path.c_str()).Read(); });
        leases->Check();
        Need(model->ImportWolf2009(source), "native MD5 source import failed");
        leases->sources.request = request; leases->sources.model = source.modelSource;
        leases->sources.animation = source.animationSource; leases->sources.skeleton = source.skeletonSource;
        const auto materialSource = ParseResourcePath(request.model);
        for (const auto& surface : source.surfaces) {
            ImportedDraw draw; draw.material = surface.geometry.material; draw.vertices = surface.geometry.vertices.size();
            draw.indices = surface.geometry.indices.size(); draw.profile = Wolf2009_ResolveSkinnedMaterial(materialSource, surface.geometry);
            draw.native = declManager->FindMaterial(draw.profile.name.c_str()); leases->draws.push_back(std::move(draw));
        }
        residentSources[model] = std::move(leases);
        common->Printf("Wolf2009 native skinned model: %s, %u joints, %u surfaces, LOD %u\n", name,
                       unsigned(source.joints.size()), unsigned(source.surfaces.size()), unsigned(request.levelOfDetail));
        return true;
    } catch (const std::exception& error) {
        if (model) model->PurgeModel();
        common->Warning("Wolf2009 skinned model '%s': %s", name ? name : "", error.what()); return false;
    }
}
bool Wolf2009_GetSkinnedModelRequest(const char* name, wolf2009::idtech4a::SkinnedModelRequest& request) {
    try { return name && requests.Find(name, request); } catch (const std::exception&) { return false; }
}
void Wolf2009_ReleaseSkinnedModelSources(idRenderModelMD5* model) { residentSources.erase(model); }
bool Wolf2009_GetSkinnedModelSources(const idRenderModelMD5* model, wolf2009::idtech4a::SkinnedModelSourceInfo& out) {
    const auto found = residentSources.find(model);
    if (found == residentSources.end()) return false;
    out = found->second->sources; return true;
}
bool Wolf2009_RegisterSkinnedAppearance(const char* modelAlias, const char* skinName, const char* catalogPath, idStr& out) {
    out.Clear();
    try {
        Need(modelAlias && skinName && catalogPath, "missing explicit skinned appearance request");
        SkinnedModelRequest request;
        Need(requests.Find(modelAlias, request), "appearance has an unregistered model alias");
        const std::string modelName = SkinnedModelCacheName(request);
        SourceLeases* resident = NULL;
        for (const auto& item : residentSources) if (idStr::Icmp(item.first->Name(), modelName.c_str()) == 0) {
            Need(item.first->IsLoaded() && !item.first->IsDefaultModel(), "appearance requires a resident qualified model");
            resident = item.second.get(); break;
        }
        Need(resident && SameSkinnedModelRequest(resident->sources.request, request), "appearance model is purged or its source identity differs");
        resident->Check();
        const std::string catalogName = ParseResourcePath(std::string("wolf2009/catalog/CATL/") + catalogPath).name;
        const Bytes bytes = File(catalogName.c_str()).Read();
        const std::string sourceSkin = MaterialCatalog::NormalizeVirtualPath(skinName);
        const std::uint64_t catalogHash = Hash(bytes.data(), bytes.size());
        std::uint64_t hash = catalogHash;
        for (const std::string& field : {modelName, sourceSkin}) {
            hash = Hash(reinterpret_cast<const unsigned char*>(field.data()), field.size(), hash);
            const unsigned char separator = 0; hash = Hash(&separator, 1, hash);
        }
        const std::string alias = ParseResourcePath(request.model).Prefix() + "appearance/" + Hex(hash);
        const auto prior = appearances.find(alias);
        if (prior != appearances.end()) {
            Need(prior->second.model == modelName && prior->second.skin == sourceSkin && *prior->second.catalog == bytes,
                 "appearance hash collision with a different complete source key");
            out = alias.c_str(); return true;
        }
        Need(appearances.size() < 4096, "skinned appearance registration budget");
        const auto catalogPrior = appearanceCatalogs.find(catalogHash);
        Need(catalogPrior == appearanceCatalogs.end() || *catalogPrior->second == bytes, "appearance catalog fingerprint collision");
        Need(catalogPrior != appearanceCatalogs.end() || bytes.size() <= 128u * 1024u * 1024u - appearanceCatalogBytes,
             "skinned appearance catalog byte budget");
        const auto data = LoadSkinnedModel(request, [](const std::string& path) { return File(path.c_str()).Read(); });
        Need(SameIdentity(data.modelSource, resident->sources.model) && SameIdentity(data.animationSource, resident->sources.animation) &&
             SameIdentity(data.skeletonSource, resident->sources.skeleton) && data.surfaces.size() == resident->draws.size(),
             "appearance source bytes differ from the resident model");
        std::istringstream stream(std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::binary);
        const ZipArchive archive = ZipArchive::Scan(stream);
        const SkinCatalog skins = SkinCatalog::Read(stream, archive);
        GeometryScene names;
        for (const auto& draw : data.surfaces) {
            GeometrySurface surface; surface.material = draw.geometry.material; names.surfaces.push_back(std::move(surface));
        }
        const auto application = skins.Apply(sourceSkin, names);
        Need(application.matchedSurfaces == data.surfaces.size() && application.changedSurfaces == data.surfaces.size(),
             "appearance must explicitly remap every selected source draw");
        const auto catalog = ReadMaterialCatalog(bytes);
        struct Pending { RenderSurface geometry; DiffuseMaterial profile; const idMaterial* from; std::string clone; };
        std::vector<Pending> pending;
        std::map<const idMaterial*, std::size_t> fromShaders;
        const auto source = ParseResourcePath(request.model);
        for (std::size_t i = 0; i < data.surfaces.size(); ++i) {
            const auto& draw = data.surfaces[i].geometry; const auto& imported = resident->draws[i];
            Need(draw.material == imported.material && draw.vertices.size() == imported.vertices && draw.indices.size() == imported.indices &&
                 imported.native && imported.native->IsDrawn(), "appearance draw does not match imported metadata");
            Need(imported.profile.binding.resolved &&
                 (imported.profile.binding.shaderClass == MaterialShaderClass::OpaqueDiffuse ||
                  imported.profile.binding.shaderClass == MaterialShaderClass::AlphaTestDiffuse) &&
                 !imported.profile.binding.explicitColorModulate && !imported.profile.binding.explicitColorAdd,
                 "appearance source material has an unsupported imported profile");
            RenderSurface target = draw; target.material = names.surfaces[i].material;
            const auto provided = BuildDiffuseMaterial(source, target, catalog.get());
            const auto preview = Wolf2009_PreviewSkinnedMaterial(source, target);
            Need(provided.supported && preview.supported && SameMaterial(provided, preview),
                 "appearance material is unsupported or differs from the current renderer catalog");
            // Resolving an existing canonical material must not rewrite a
            // different profile already used by a resident shared mesh.
            for (const auto& live : residentSources) for (const auto& existing : live.second->draws)
                if (existing.profile.name == preview.name)
                    Need(SameMaterial(existing.profile, preview), "appearance would overwrite a resident imported material profile");
            Need(ParseResourcePath(preview.image).mount == source.mount && HasResourceType(preview.image.c_str(), "TXTR"),
                 "appearance diffuse escaped the explicit model source mount");
            for (unsigned axis = 0; axis < 2; ++axis)
                Need((imported.profile.supported ? imported.profile.binding.uvScale[axis] : 1.0f) == preview.binding.uvScale[axis],
                     "appearance would require rebaking shared UV attributes");
            for (const auto& vertex : draw.vertices)
                Need(NativeDiffuseColor(imported.profile, vertex.diffuseColor) == NativeDiffuseColor(preview, vertex.diffuseColor),
                     "appearance would require re-encoding shared RGBA attributes");
            DecodeDdsMip0(File(preview.image.c_str()).Read());
            const auto duplicate = fromShaders.find(imported.native);
            if (duplicate != fromShaders.end()) {
                Need(SameMaterial(pending[duplicate->second].profile, preview), "conflicting per-draw remaps of one native material");
                continue;
            }
            Pending value; value.geometry = std::move(target); value.profile = preview; value.from = imported.native;
            value.clone = alias + "/material/" + Hex(pending.size());
            Need(!declManager->FindType(DECL_MATERIAL, value.clone.c_str(), false), "appearance material namespace is already occupied");
            fromShaders[value.from] = pending.size(); pending.push_back(std::move(value));
        }
        Need(!pending.empty() && !declManager->FindType(DECL_SKIN, alias.c_str(), false), "appearance skin namespace is already occupied");
        resident->Check();
        // All source/epoch/catalog/remap/UV/RGBA/DDS qualification is complete.
        // Clone the existing qualified stage text under the immutable appearance
        // identity so later registrations cannot repaint an earlier appearance.
        std::ostringstream skinText; skinText << "{\n";
        for (const auto& value : pending) {
            const auto& resolved = Wolf2009_ResolveSkinnedMaterial(source, value.geometry);
            Need(SameMaterial(resolved, value.profile), "qualified appearance material changed during native registration");
            idDecl* clone = declManager->CreateNewDecl(DECL_MATERIAL, value.clone.c_str(), "<wolf2009-source-appearance>");
            clone->SetText(resolved.declaration.c_str()); clone->Invalidate();
            const idMaterial* native = declManager->FindMaterial(value.clone.c_str());
            Need(native && native->GetState() != DS_DEFAULTED, "native appearance material clone failed to parse");
            skinText << " \"" << value.from->GetName() << "\" \"" << value.clone << "\"\n";
        }
        skinText << "}\n";
        idDecl* declaration = declManager->CreateNewDecl(DECL_SKIN, alias.c_str(), "<wolf2009-source-appearance>");
        const std::string text = skinText.str(); declaration->SetText(text.c_str()); declaration->Invalidate();
        const idDeclSkin* nativeSkin = declManager->FindSkin(alias.c_str(), false);
        Need(nativeSkin && nativeSkin->GetState() != DS_DEFAULTED, "native appearance skin failed to parse");
        for (const auto& value : pending)
            Need(R_RemapShaderBySkin(value.from, nativeSkin, NULL) == declManager->FindMaterial(value.clone.c_str()),
                 "native appearance skin did not retain the authored material mapping");
        std::shared_ptr<const Bytes> retained;
        if (catalogPrior != appearanceCatalogs.end()) retained = catalogPrior->second;
        else { retained.reset(new Bytes(bytes)); appearanceCatalogs[catalogHash] = retained; appearanceCatalogBytes += bytes.size(); }
        Appearance appearance; appearance.model = modelName; appearance.skin = sourceSkin; appearance.catalog = retained;
        appearances.insert(std::make_pair(alias, std::move(appearance)));
        out = alias.c_str();
        common->Printf("Wolf2009 native appearance: %s skin=%s remaps=%u catalog_bytes=%u\n", alias.c_str(), sourceSkin.c_str(),
                       unsigned(pending.size()), unsigned(bytes.size()));
        return true;
    } catch (const std::exception& error) { common->Warning("Wolf2009 skinned appearance: %s", error.what()); return false; }
}
idRenderModelMD5::~idRenderModelMD5() { PurgeModel(); }
void Wolf2009_ToNativeModelJoints(const std::vector<wolf2009::GreeterJointPose>& source, idJointMat* destination, int count) {
    Need(count > 0 && std::size_t(count) == source.size() && destination != NULL, "native skinned joint destination mismatch");
    // Validate all before writing any destination joint.
    for (const auto& joint : source) { NativeRotation(joint); NativeTranslation(joint); }
    for (int i = 0; i < count; ++i) {
        destination[i].SetRotation(NativeRotation(source[std::size_t(i)]).ToMat3());
        destination[i].SetTranslation(NativeTranslation(source[std::size_t(i)]));
    }
}
bool idRenderModelMD5::ImportWolf2009(const wolf2009::idtech4a::SkinnedModelData& source) {
    Need(meshes.Num() == 0 && joints.Num() == 0 && defaultPose.Num() == 0, "native skinned import requires a purged/empty model");
    Need(!source.joints.empty() && source.joints.size() <= 256 && !source.surfaces.empty() && source.surfaces.size() <= 4096,
         "native skinned joint/surface budget");
    // All data came through the neutral adapter validator, but native integer,
    // index-width, UV/material and aligned-allocation constraints are separate.
    const std::size_t maxIndex = sizeof(glIndex_t) < 4 ? 32767u : std::size_t(INT_MAX);
    std::size_t totalVertices = 0, totalIndices = 0;
    for (const auto& surface : source.surfaces) {
        Need(!surface.geometry.vertices.empty() && surface.geometry.vertices.size() <= maxIndex + 1 &&
             surface.geometry.indices.size() <= INT_MAX && surface.geometry.indices.size() % 3 == 0 &&
             surface.weights.size() == surface.geometry.vertices.size(), "native skinned draw budget/array mismatch");
        for (std::uint32_t index : surface.geometry.indices) Need(index < surface.geometry.vertices.size(), "native skinned index range");
        Need(surface.geometry.vertices.size() <= std::size_t(INT_MAX) / 4, "native skinned weight count overflow");
        Need(surface.geometry.vertices.size() <= 1000000u - totalVertices && surface.geometry.indices.size() <= 6000000u - totalIndices,
             "native skinned aggregate geometry budget");
        totalVertices += surface.geometry.vertices.size(); totalIndices += surface.geometry.indices.size();
    }
    joints.SetNum(int(source.joints.size())); defaultPose.SetNum(joints.Num()); meshes.SetNum(int(source.surfaces.size()));
    AlignedMemory bindMemory(std::size_t(joints.Num()) * sizeof(idJointMat));
    idJointMat* bind = static_cast<idJointMat*>(bindMemory.Get());
    try {
        for (int i = 0; i < joints.Num(); ++i) {
            const auto& input = source.joints[std::size_t(i)];
            Need(i ? input.parent >= 0 && input.parent < i : input.parent == -1, "native skinned parent order");
            joints[i].name = input.name.c_str(); joints[i].parent = i ? &joints[input.parent] : NULL;
            defaultPose[i].q = NativeRotation(input.bindLocal); defaultPose[i].t = NativeTranslation(input.bindLocal);
            bind[i].SetRotation(NativeRotation(input.bindModel).ToMat3()); bind[i].SetTranslation(NativeTranslation(input.bindModel));
        }
        const auto materialSource = ParseResourcePath(source.request.model);
        for (int m = 0; m < meshes.Num(); ++m) {
            idMD5Mesh& mesh = meshes[m]; const auto& surface = source.surfaces[std::size_t(m)];
            const auto& material = Wolf2009_ResolveSkinnedMaterial(materialSource, surface.geometry);
            mesh.shader = declManager->FindMaterial(material.name.c_str());
            mesh.texCoords.SetNum(int(surface.geometry.vertices.size())); mesh.wolfVertexColors.SetNum(mesh.texCoords.Num());
            mesh.numTris = int(surface.geometry.indices.size() / 3); mesh.numWeights = mesh.texCoords.Num() * 4;
            mesh.scaledWeights = static_cast<idVec4*>(Mem_Alloc16(mesh.numWeights * sizeof(idVec4)));
            mesh.weightIndex = static_cast<int*>(Mem_Alloc16(mesh.numWeights * 2 * sizeof(int)));
            for (int v = 0; v < mesh.texCoords.Num(); ++v) {
                const auto& vertex = surface.geometry.vertices[std::size_t(v)];
                const float u = vertex.u * (material.supported ? material.binding.uvScale[0] : 1.0f);
                const float t = vertex.v * (material.supported ? material.binding.uvScale[1] : 1.0f);
                Need(std::isfinite(u) && std::isfinite(t), "scaled native skinned UV is nonfinite"); mesh.texCoords[v].Set(u, t);
                const auto rgba = NativeDiffuseColor(material, vertex.diffuseColor);
                for (int c = 0; c < 4; ++c) mesh.wolfVertexColors[v].rgba[c] = rgba[std::size_t(c)];
                double weightTotal = 0;
                for (int c = 0; c < 4; ++c) {
                    const auto& weight = surface.weights[std::size_t(v)][std::size_t(c)]; const int w = v * 4 + c;
                    Need(weight.joint < joints.Num() && std::isfinite(weight.weight) && weight.weight >= 0 && weight.weight <= 1, "native skinned influence range");
                    weightTotal += weight.weight;
                    for (int axis = 0; axis < 3; ++axis) {
                        Need(std::isfinite(weight.jointLocalPosition[std::size_t(axis)]), "native skinned offset nonfinite");
                        mesh.scaledWeights[w][axis] = weight.jointLocalPosition[std::size_t(axis)] * weight.weight;
                    }
                    mesh.scaledWeights[w].w = weight.weight;
                    mesh.weightIndex[w * 2] = int(weight.joint) * sizeof(idJointMat); mesh.weightIndex[w * 2 + 1] = c == 3 ? 1 : 0;
                }
                Need(std::abs(weightTotal - 1) <= 1e-5, "native skinned weights do not sum to one");
            }
            idList<int> indexes; indexes.SetNum(int(surface.geometry.indices.size()));
            for (int i = 0; i < indexes.Num(); ++i) indexes[i] = int(surface.geometry.indices[std::size_t(i)]);
            AlignedMemory vertexMemory(std::size_t(mesh.texCoords.Num()) * sizeof(idDrawVert));
            idDrawVert* vertices = static_cast<idDrawVert*>(vertexMemory.Get());
            for (int v = 0; v < mesh.texCoords.Num(); ++v) {
                vertices[v].Clear(); vertices[v].st = mesh.texCoords[v];
                for (int c = 0; c < 4; ++c) vertices[v].color[c] = mesh.wolfVertexColors[v].rgba[c];
            }
            mesh.TransformVerts(vertices, bind);
            mesh.deformInfo = R_BuildDeformInfo(mesh.texCoords.Num(), vertices, indexes.Num(), indexes.Ptr(), mesh.shader->UseUnsmoothedTangents());
            Need(mesh.deformInfo != NULL, "native skinned deform topology creation failed");
        }
        CalculateBounds(bind); defaulted = false; purged = false;
    } catch (...) { PurgeModel(); throw; }
    return true;
}
bool idRenderModelMD5::Wolf2009FrameBounds(const idJointMat* pose, int count, idBounds& out) {
    if (!pose || count != joints.Num() || count <= 0 || meshes.Num() <= 0 || purged) return false;
    for (int i = 0; i < count; ++i) for (int c = 0; c < 12; ++c) if (!std::isfinite(pose[i].ToFloatPtr()[c])) return false;
    out.Clear(); for (int i = 0; i < meshes.Num(); ++i) out += meshes[i].CalcBounds(pose);
    return !out.IsCleared();
}

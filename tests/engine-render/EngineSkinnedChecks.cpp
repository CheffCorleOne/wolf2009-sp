// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in native MD5 checks in the initialized GPL renderer, with no GL, world
// placement, game controller, animation-manager substitution or frame baking.
// STL must precede idStr's legacy macro replacements.
#include "EngineSkinnedModelData.h"
#include "SkinCatalog.h"
#include "ZipArchive.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "Wolf2009Render.h"
namespace {
using namespace wolf2009;
using namespace wolf2009::idtech4a;
const char* const ModelPath = "wolf2009/qaskin/MODL/models/characters/civilians/civ_male_04/anims/body_cm04.md5r";
const char* const AnimationPath = "wolf2009/qaskin/HKXA/maps/game/trainyard/trainyard";
const char* const SkeletonPath = "wolf2009/qaskel/SKEL/maps/game/global/global";
const char* const ModelPackage = "maps/game/trainyard/trainyard.mpk";
const char* const SkeletonPackage = "maps/game/global/global.spk";
const char* const CharacterPackage = "streampacks/allies_kreisau_agt02.spk";
const double PositionTolerance = 0.0002;
struct Stats {
    std::size_t checks = 0, poses = 0, corners = 0, cacheUpdates = 0;
    std::size_t diffuse = 0, fallbacks = 0, modulated = 0;
    double positionError = 0, hierarchyError = 0, uvError = 0, boundsError = 0;
};
void Require(Stats& stats, bool condition, const char* message) {
    ++stats.checks;
    if (!condition) throw std::runtime_error(message);
}
void Near(Stats& stats, double actual, double expected, double tolerance, double& maximum, const char* message) {
    const double error = std::abs(actual - expected);
    maximum = (std::max)(maximum, error);
    Require(stats, std::isfinite(actual) && std::isfinite(expected) && error <= tolerance, message);
}
class EngineFile {
    idFile* file;
  public:
    explicit EngineFile(const char* path) : file(fileSystem->OpenFileRead(path, false)) {}
    ~EngineFile() { if (file) fileSystem->CloseFile(file); }
    Bytes Read() const {
        if (!file) throw std::runtime_error("original skinned resource absent in the selected engine mount");
        const int bytes = file->Length();
        if (bytes <= 0 || bytes > 64 * 1024 * 1024) throw std::runtime_error("skinned QA source byte budget");
        Bytes out(static_cast<std::size_t>(bytes));
        if (file->Read(out.data(), bytes) != bytes) throw std::runtime_error("short actual engine skinned resource read");
        return out;
    }
};
Bytes Read(const std::string& path) { return EngineFile(path.c_str()).Read(); }
class JointBuffer {
    idJointMat* values;
  public:
    explicit JointBuffer(int count) : values(static_cast<idJointMat*>(Mem_Alloc16(std::size_t(count) * sizeof(idJointMat)))) {}
    ~JointBuffer() { Mem_Free16(values); }
    idJointMat* Get() const { return values; }
  private:
    JointBuffer(const JointBuffer&);
    JointBuffer& operator=(const JointBuffer&);
};
struct Resources {
    idRenderModel* a = NULL;
    idRenderModel* b = NULL;
    std::vector<idRenderModel*> models;
    bool skinMounted = false, skeletonMounted = false, characterMounted = false, catalog = false;
    bool originalCached = r_useCachedDynamicModels.GetBool();
    int originalShowSkel = r_showSkel.GetInteger();
    Resources() { r_useCachedDynamicModels.SetBool(true); r_showSkel.SetInteger(0); }
    void DeleteSnapshots() { delete a; a = NULL; delete b; b = NULL; }
    void Remember(idRenderModel* model) {
        if (model && model != renderModelManager->DefaultModel() &&
            std::find(models.begin(), models.end(), model) == models.end()) models.push_back(model);
    }
    void DeleteModels() {
        DeleteSnapshots();
        for (idRenderModel* model : models) {
            renderModelManager->RemoveModel(model);
            renderModelManager->FreeModel(model);
        }
        models.clear();
    }
    ~Resources() {
        DeleteModels();
        if (characterMounted && !Wolf2009_Unmount("qachar")) fprintf(stderr, "WOLF2009_NATIVE_SKINNED_CLEANUP_FAIL mount=qachar\n");
        if (skinMounted && !Wolf2009_Unmount("qaskin")) fprintf(stderr, "WOLF2009_NATIVE_SKINNED_CLEANUP_FAIL mount=qaskin\n");
        if (skeletonMounted && !Wolf2009_Unmount("qaskel")) fprintf(stderr, "WOLF2009_NATIVE_SKINNED_CLEANUP_FAIL mount=qaskel\n");
        if (catalog) Wolf2009_ClearRenderCatalog();
        r_useCachedDynamicModels.SetBool(originalCached); r_showSkel.SetInteger(originalShowSkel);
    }
};
std::uint64_t ByteHash(std::uint64_t value, std::uint32_t word) {
    for (unsigned i = 0; i < 4; ++i) value = (value ^ ((word >> (8*i)) & 255u)) * UINT64_C(1099511628211);
    return value;
}
std::uint64_t SnapshotHash(const idRenderModel* model) {
    std::uint64_t out = UINT64_C(14695981039346656037);
    for (int s = 0; s < model->NumSurfaces(); ++s) {
        const auto* tri = model->Surface(s)->geometry;
        out = ByteHash(out, std::uint32_t(tri->numVerts)); out = ByteHash(out, std::uint32_t(tri->numIndexes));
        for (int i = 0; i < tri->numIndexes; ++i) out = ByteHash(out, std::uint32_t(tri->indexes[i]));
        for (int i = 0; i < tri->numVerts; ++i) {
            const auto& vertex = tri->verts[i];
            for (float component : {vertex.xyz.x, vertex.xyz.y, vertex.xyz.z, vertex.st.x, vertex.st.y}) {
                std::uint32_t bits; std::memcpy(&bits, &component, sizeof(bits)); out = ByteHash(out, bits);
            }
            for (unsigned c = 0; c < 4; ++c) out = ByteHash(out, vertex.color[c]);
        }
    }
    return out;
}
bool SameSource(const SkinnedSourceIdentity& a, const SkinnedSourceIdentity& b) {
    return a.path == b.path && a.bytes == b.bytes && a.fingerprint == b.fingerprint;
}
void CheckSources(Stats& stats, const idRenderModelMD5* model, const SkinnedModelData& source) {
    SkinnedModelSourceInfo actual;
    Require(stats, Wolf2009_GetSkinnedModelSources(model, actual), "resident native model lost owning source identity");
    Require(stats, SameSkinnedModelRequest(actual.request, source.request) && SameSource(actual.model, source.modelSource) &&
            SameSource(actual.animation, source.animationSource) && SameSource(actual.skeleton, source.skeletonSource),
            "native resident source paths/epochs/byte identities differ from independent reads");
}
idRenderModelMD5* FindNative(Stats& stats, Resources& resources, const idStr& alias) {
    idRenderModel* model = renderModelManager->FindModel(alias);
    resources.Remember(model);
    Require(stats, model && model != renderModelManager->DefaultModel() && model->IsLoaded() && !model->IsDefaultModel(),
            "normal model manager did not load the original weighted MODL");
    auto* md5 = dynamic_cast<idRenderModelMD5*>(model);
    Require(stats, md5 && model->NumJoints() == 52 && model->IsDynamicModel() == DM_CACHED && !model->IsStaticWorldModel(),
            "weighted source was not an actual 52-joint native MD5 cached dynamic model");
    return md5;
}
void CheckBindHierarchy(Stats& stats, idRenderModelMD5* model, const SkinnedModelData& source) {
    const int count = model->NumJoints(); const idMD5Joint* joints = model->GetJoints();
    const idJointQuat* local = model->GetDefaultPose();
    Require(stats, count == 52 && local && joints && source.joints.size() == 52, "native default skeleton is incomplete");
    JointBuffer global(count), expected(count);
    std::vector<int> parents; std::vector<GreeterJointPose> sourceModel;
    for (int i = 0; i < count; ++i) {
        const auto& wanted = source.joints[std::size_t(i)];
        Require(stats, joints[i].name == wanted.name.c_str(), "native joint name/order differs from the model bind skeleton");
        Require(stats, wanted.parent < 0 ? joints[i].parent == NULL : joints[i].parent == &joints[wanted.parent],
                "native joint parent pointer differs from the decoded skeleton");
        parents.push_back(wanted.parent); sourceModel.push_back(wanted.bindModel);
    }
    SIMDProcessor->ConvertJointQuatsToJointMats(global.Get(), local, count);
    SIMDProcessor->TransformJoints(global.Get(), parents.data(), 1, count - 1);
    Wolf2009_ToNativeModelJoints(sourceModel, expected.Get(), count);
    for (int i = 0; i < count; ++i) for (int c = 0; c < 12; ++c) {
        Near(stats, global.Get()[i].ToFloatPtr()[c], expected.Get()[i].ToFloatPtr()[c],
             c % 4 == 3 ? PositionTolerance : 0.00002, stats.hierarchyError,
             "native local-default hierarchy does not reconstruct source model bind transforms");
    }
    idBounds nativeBind;
    Require(stats, model->Wolf2009FrameBounds(expected.Get(), count, nativeBind), "native default bind bounds unavailable");
    const idBounds storedBind = model->Bounds(NULL);
    for (unsigned axis = 0; axis < 3; ++axis) for (unsigned side = 0; side < 2; ++side)
        Near(stats, storedBind[side][axis], nativeBind[side][axis], PositionTolerance, stats.boundsError,
             "native model reference bounds differ from its decoded bind skeleton");
}
struct ExpectedMaterial {
    DiffuseMaterial profile;
    const idMaterial* native = NULL;
    const idMaterial* effective = NULL;
};
void CheckNativeStages(Stats& stats, const DiffuseMaterial& profile, const idMaterial* native) {
    int sourceStages = 0;
    for (int stage = 0; stage < native->GetNumStages(); ++stage) {
        const auto* value = native->GetStage(stage);
        if (!value->texture.image || value->texture.image->imgName.Find("/txtr/") < 0) continue;
        ++sourceStages;
        Require(stats, profile.supported && value->vertexColor == (profile.nativeVertexColor ? SVC_MODULATE : SVC_IGNORE),
                "native skinned diffuse stage colour mode/source qualification differs");
        Require(stats, value->texture.image->imgName.Icmp(profile.image.c_str()) == 0, "native skinned texture lost archive-qualified source identity");
    }
    Require(stats, profile.supported ? sourceStages > 0 : sourceStages == 0,
            "native skinned material source stages disagree with its supported/fallback status");
}
std::vector<ExpectedMaterial> Materials(Stats& stats, const SkinnedModelData& source) {
    std::vector<ExpectedMaterial> out;
    const auto path = ParseResourcePath(source.request.model);
    for (const auto& draw : source.surfaces) {
        ExpectedMaterial next; next.profile = Wolf2009_ResolveSkinnedMaterial(path, draw.geometry);
        next.native = declManager->FindMaterial(next.profile.name.c_str());
        next.effective = next.native;
        Require(stats, next.native && next.native->GetState() != DS_DEFAULTED, "native skinned material failed to parse");
        if (next.profile.supported) { ++stats.diffuse; if (next.profile.nativeVertexColor) ++stats.modulated; }
        else {
            ++stats.fallbacks;
            printf("WOLF2009_NATIVE_SKINNED_MATERIAL_UNSUPPORTED material=%s reason=%s\n",
                   draw.geometry.material.c_str(), next.profile.reason.c_str());
        }
        CheckNativeStages(stats, next.profile, next.native);
        out.push_back(std::move(next));
    }
    return out;
}
void CheckSnapshot(Stats& stats, const char* label, idRenderModelMD5* model, idRenderModel* snapshot,
                   const SkinnedModelData& data, const GreeterSkinGeometry& body, const GreeterPose& pose,
                   const idBounds& frameBounds, const std::vector<ExpectedMaterial>& materials, const idDeclSkin* skin) {
    Require(stats, snapshot && snapshot->IsDynamicModel() == DM_STATIC && snapshot->NumSurfaces() == int(data.surfaces.size()),
            "native MD5 did not produce all dynamic snapshot draws");
    const auto expected = PoseGreeterBody(body, pose);
    idBounds sourceBounds; sourceBounds.Clear();
    const idBounds nativeBounds = snapshot->Bounds();
    for (std::size_t s = 0; s < data.surfaces.size(); ++s) {
        const auto& draw = data.surfaces[s]; const auto& material = materials[s];
        const modelSurface_t* native = snapshot->Surface(int(s)); const srfTriangles_t* tri = native->geometry;
        Require(stats, native->id == int(s) && native->shader == material.native && tri && tri->verts && tri->indexes &&
                tri->numVerts >= int(draw.geometry.vertices.size()) && tri->numIndexes == int(draw.geometry.indices.size()),
                "native MD5 snapshot changed surface/material/index topology");
        Require(stats, R_RemapShaderBySkin(native->shader, skin, NULL) == material.effective,
                "actual native customSkin selection differs from the authored material remap");
        for (std::uint32_t vertex : draw.sourceVertices) {
            const auto& v = expected.vertices[vertex]; sourceBounds.AddPoint(idVec3(v.x, v.y, v.z));
        }
        // Native deform topology may duplicate a source vertex at a UV mirror
        // seam. Compare the ordered triangle corners rather than raw indices.
        for (std::size_t c = 0; c < draw.geometry.indices.size(); ++c) {
            const std::uint32_t compact = draw.geometry.indices[c];
            Require(stats, compact < draw.sourceVertices.size(), "prepared source corner mapping is invalid");
            const std::uint32_t source = draw.sourceVertices[compact];
            const int actualIndex = int(tri->indexes[c]);
            Require(stats, actualIndex >= 0 && actualIndex < tri->numVerts && source < expected.vertices.size(),
                    "native seam-adjusted corner index is invalid");
            const idDrawVert& actual = tri->verts[actualIndex]; const auto& wanted = expected.vertices[source];
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float coordinate = axis == 0 ? wanted.x : axis == 1 ? wanted.y : wanted.z;
                Near(stats, actual.xyz[axis], coordinate, PositionTolerance, stats.positionError,
                     "actual native weighted vertex diverges from source CPU skinning");
            }
            const auto& input = draw.geometry.vertices[compact];
            Near(stats, actual.st.x, input.u * (material.profile.supported ? material.profile.binding.uvScale[0] : 1.0f),
                 0.000001, stats.uvError, "native skinned U differs from the qualified source UV");
            Near(stats, actual.st.y, input.v * (material.profile.supported ? material.profile.binding.uvScale[1] : 1.0f),
                 0.000001, stats.uvError, "native skinned V differs from the qualified source UV");
            const auto rgba = NativeDiffuseColor(material.profile, input.diffuseColor);
            for (unsigned channel = 0; channel < 4; ++channel)
                Require(stats, actual.color[channel] == rgba[channel], "native dynamic snapshot lost qualified source RGBA");
            ++stats.corners;
        }
        for (int v = 0; v < tri->numVerts; ++v) for (unsigned axis = 0; axis < 3; ++axis) {
            const float position = tri->verts[v].xyz[axis];
            Require(stats, std::isfinite(position) && position >= frameBounds[0][axis] - PositionTolerance &&
                    position <= frameBounds[1][axis] + PositionTolerance, "native supplied-joint bounds do not enclose a deform vertex");
        }
    }
    Require(stats, !sourceBounds.IsCleared() && !frameBounds.IsCleared(), "skinned source/native pose bounds are empty");
    for (unsigned axis = 0; axis < 3; ++axis) for (unsigned side = 0; side < 2; ++side) {
        Near(stats, frameBounds[side][axis], sourceBounds[side][axis], PositionTolerance, stats.boundsError,
             "native skinned frame bounds differ from source referenced vertices");
        Near(stats, nativeBounds[side][axis], frameBounds[side][axis], PositionTolerance, stats.boundsError,
             "actual dynamic snapshot bounds differ from supplied-joint bounds");
    }
    Require(stats, model->NumJoints() == 52 && !model->IsDefaultModel(), "snapshot update changed shared native MD5 model state");
    ++stats.poses;
    printf("WOLF2009_NATIVE_SKINNED_POSE scope=%s seconds=%.9g surfaces=%d vertices=%d triangles=%d max_position_error=%.9g\n",
           label, pose.seconds, snapshot->NumSurfaces(), snapshot->Surface(0)->geometry->numVerts,
           snapshot->Surface(0)->geometry->numIndexes / 3, stats.positionError);
}
void PoseInstance(Stats& stats, const char* label, idRenderModelMD5* model, idRenderModel*& cached,
                  const SkinnedModelData& data, const GreeterSkinGeometry& body, const GreeterPose& pose,
                  const std::vector<ExpectedMaterial>& materials, const idDeclSkin* skin = NULL) {
    JointBuffer joints(model->NumJoints());
    Wolf2009_ToNativeModelJoints(pose.model, joints.Get(), model->NumJoints());
    renderEntity_t entity; std::memset(&entity, 0, sizeof(entity));
    entity.hModel = model; entity.axis.Identity(); entity.numJoints = model->NumJoints(); entity.joints = joints.Get();
    entity.customSkin = skin;
    for (int c = 0; c < 4; ++c) entity.shaderParms[c] = 1;
    Require(stats, model->Wolf2009FrameBounds(joints.Get(), model->NumJoints(), entity.bounds), "native supplied-joint frame bounds failed");
    idRenderModel* previous = cached; cached = NULL;
    cached = model->InstantiateDynamicModel(&entity, NULL, previous);
    Require(stats, !previous || cached == previous, "native MD5 cached snapshot was not reused");
    if (previous) ++stats.cacheUpdates;
    CheckSnapshot(stats, label, model, cached, data, body, pose, entity.bounds, materials, skin);
}
void CheckEpoch(Stats& stats, const char* mount, unsigned long long expected) {
    unsigned long long actual = 0;
    Require(stats, Wolf2009_MountEpoch(mount, actual) && actual == expected, "QA source mount epoch changed unexpectedly");
}
std::uint32_t Crc32(const unsigned char* bytes, std::size_t count) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}
class NativePixels {
  public:
    byte* pixels = NULL;
    ~NativePixels() { if (pixels) R_StaticFree(pixels); }
};
std::uint32_t DeclarationCrc(const idDecl* declaration) {
    std::vector<char> text(std::size_t(declaration->GetTextLength()) + 1);
    declaration->GetText(text.data());
    return Crc32(reinterpret_cast<const unsigned char*>(text.data()), std::size_t(declaration->GetTextLength()));
}
void CheckOriginalDiffuse(Stats& stats, const ExpectedMaterial& material) {
    Require(stats, material.profile.supported && ParseResourcePath(material.profile.image).mount == "qachar",
            "character detail skin did not resolve its own original diffuse texture");
    const Bytes bytes = Read(material.profile.image);
    const DdsImage source = DecodeDdsMip0(bytes);
    const std::uint32_t sourceCrc = Crc32(bytes.data(), bytes.size());
    const std::uint32_t decodedCrc = Crc32(source.rgba.data(), source.rgba.size());
    Require(stats, source.width == 1024 && source.height == 1024 && source.mipCount == 11 && source.format == "BC1/DXT1" &&
            bytes.size() == 699192 && sourceCrc == 0x2a7c694fu && decodedCrc == 0xdaf354cdu,
            "selected character detail TXTR metadata differs from the audited original diffuse");
    NativePixels actual; int width = 0, height = 0; ID_TIME_T timestamp = -1;
    R_LoadImage(material.profile.image.c_str(), &actual.pixels, &width, &height, &timestamp, true);
    Require(stats, actual.pixels && width == int(source.width) && height == int(source.height) && timestamp == 0,
            "actual native image loader did not decode the original character diffuse");
    const std::uint32_t nativeCrc = Crc32(actual.pixels, source.rgba.size());
    Require(stats, nativeCrc == decodedCrc && std::memcmp(actual.pixels, source.rgba.data(), source.rgba.size()) == 0,
            "native character diffuse pixels differ from the canonical original DDS decode");
    printf("WOLF2009_NATIVE_SKINNED_IMAGE source=%s bytes=%zu width=%d height=%d mips=%u format=%s "
           "source_crc32=%08x decoded_rgba_crc32=%08x native_rgba_crc32=%08x timestamp=%lld gpu=0\n",
           material.profile.image.c_str(), bytes.size(), width, height, unsigned(source.mipCount), source.format.c_str(),
           unsigned(sourceCrc), unsigned(decodedCrc), unsigned(nativeCrc), static_cast<long long>(timestamp));
}
void CheckCharacterSkin(Stats& stats, Resources& resources) {
    Require(stats, Wolf2009_Mount("qachar", CharacterPackage), "original character detail SPK could not be mounted");
    resources.characterMounted = true;
    const char* const path = "wolf2009/qachar/MODL/models/characters/civilians/civ_male_04/anims/body_cm04.md5r";
    idStr alias;
    Require(stats, Wolf2009_RegisterSkinnedModel(path, AnimationPath, SkeletonPath, 0, alias), "character detail source registration failed");
    SkinnedModelRequest request;
    Require(stats, Wolf2009_GetSkinnedModelRequest(alias, request), "character detail source request absent");
    const auto data = LoadSkinnedModel(request, Read);
    const auto clip = GreeterAnimation::Load(Read(request.animation), Read(request.skeleton));
    const auto body = DecodeGreeterBody1(Read(request.model), ParseResourcePath(request.model).name, clip.Joints(), 0);
    Require(stats, data.surfaces.size() == 1 && data.surfaces[0].geometry.vertices.size() == 1333 &&
            data.surfaces[0].geometry.indices.size() == 2110 * 3 && data.otherLodMeshes == 4,
            "original character detail LOD0 geometry differs from the audited source");
    idRenderModelMD5* model = FindNative(stats, resources, alias);
    CheckSources(stats, model, data); CheckBindHierarchy(stats, model, data);
    // The unskinned base 'midtown' image is absent in both original packages.
    // Select the existing authored skin; never substitute a generated texture
    // or pretend that loading the detail geometry alone resolves its material.
    const Bytes assets = Read("fixtures/wolf-materials.bin");
    std::istringstream stream(std::string(assets.begin(), assets.end()), std::ios::in | std::ios::binary);
    const ZipArchive archive = ZipArchive::Scan(stream);
    const SkinCatalog skins = SkinCatalog::Read(stream, archive);
    GeometryScene remapped = body.bindGeometry;
    const SkinApplication application = skins.Apply("resist_fighter_B", remapped);
    Require(stats, application.matchedSurfaces == data.surfaces.size() && application.changedSurfaces == data.surfaces.size(),
            "authored character skin did not map every selected source draw");
    SkinnedModelData materialData = data;
    Require(stats, remapped.surfaces.size() == data.surfaces.size(), "authored skin changed draw count");
    for (std::size_t s = 0; s < data.surfaces.size(); ++s) materialData.surfaces[s].geometry.material = remapped.surfaces[s].material;
    const auto baseMaterials = Materials(stats, data);
    GreeterPose bind; bind.model = body.bindModelJoints;
    resources.DeleteSnapshots();
    PoseInstance(stats, "character-default-A", model, resources.a, data, body, bind, baseMaterials);
    Require(stats, !baseMaterials[0].profile.supported, "source character default midtown fallback unexpectedly disappeared");
    const std::uint64_t meshA = SnapshotHash(resources.a);
    const std::uint32_t declarationA = DeclarationCrc(baseMaterials[0].native);
    const int materialCount = declManager->GetNumDecls(DECL_MATERIAL), skinCount = declManager->GetNumDecls(DECL_SKIN);
    for (const char* rejected : {"__wolf2009_qa_missing_skin__", "resist_fighter_A"}) {
        idStr invalid;
        Require(stats, !Wolf2009_RegisterSkinnedAppearance(alias, rejected, "fixtures/wolf-materials.bin", invalid) && invalid.IsEmpty(),
                "unknown or source-incompatible authored appearance was accepted");
        Require(stats, materialCount == declManager->GetNumDecls(DECL_MATERIAL) && skinCount == declManager->GetNumDecls(DECL_SKIN) &&
                SnapshotHash(resources.a) == meshA && DeclarationCrc(baseMaterials[0].native) == declarationA,
                "rejected authored appearance caused native declaration or shared-mesh effects");
    }
    idStr skinAlias;
    Require(stats, Wolf2009_RegisterSkinnedAppearance(alias, "resist_fighter_B", "fixtures/wolf-materials.bin", skinAlias),
            "production authored appearance registration failed");
    const idDeclSkin* nativeSkin = declManager->FindSkin(skinAlias, false);
    Require(stats, nativeSkin && nativeSkin->GetState() != DS_DEFAULTED, "production appearance did not create an ordinary native skin declaration");
    std::vector<ExpectedMaterial> materials;
    const auto catalog = ReadMaterialCatalog(assets);
    const auto materialSource = ParseResourcePath(data.request.model);
    for (std::size_t s = 0; s < data.surfaces.size(); ++s) {
        ExpectedMaterial target; target.profile = BuildDiffuseMaterial(materialSource, materialData.surfaces[s].geometry, catalog.get());
        target.native = baseMaterials[s].native; target.effective = R_RemapShaderBySkin(target.native, nativeSkin, NULL);
        const std::string clonePrefix = std::string(skinAlias.c_str()) + "/material/";
        Require(stats, target.profile.supported && target.effective && target.effective != target.native &&
                target.effective->GetState() != DS_DEFAULTED && target.effective->Coverage() == MC_OPAQUE &&
                std::string(target.effective->GetName()).compare(0, clonePrefix.size(), clonePrefix) == 0,
                "production character appearance did not select a distinct immutable qualified diffuse clone");
        Require(stats, nativeSkin->RemapShaderBySkin(target.native) == target.effective, "native skin mapping differs from the frontend selection");
        CheckNativeStages(stats, target.profile, target.effective); CheckOriginalDiffuse(stats, target);
        ++stats.diffuse; if (target.profile.nativeVertexColor) ++stats.modulated;
        materials.push_back(std::move(target));
    }
    Require(stats, SnapshotHash(resources.a) == meshA && DeclarationCrc(baseMaterials[0].native) == declarationA,
            "appearance B changed the shared mesh or original material of instance A");
    const std::uint32_t declarationB = DeclarationCrc(materials[0].effective);
    const int registeredMaterials = declManager->GetNumDecls(DECL_MATERIAL), registeredSkins = declManager->GetNumDecls(DECL_SKIN);
    idStr repeated;
    Require(stats, Wolf2009_RegisterSkinnedAppearance(alias, "resist_fighter_B", "fixtures/wolf-materials.bin", repeated) && repeated.Icmp(skinAlias) == 0 &&
            registeredMaterials == declManager->GetNumDecls(DECL_MATERIAL) && registeredSkins == declManager->GetNumDecls(DECL_SKIN) &&
            DeclarationCrc(materials[0].effective) == declarationB, "identical appearance registration changed its immutable native declarations");
    PoseInstance(stats, "character-authored-skin-B-bind", model, resources.b, data, body, bind, materials, nativeSkin);
    PoseInstance(stats, "character-authored-skin-B-fractional", model, resources.b, data, body,
                 clip.SamplePose(.271 * clip.Duration()), materials, nativeSkin);
    Require(stats, SnapshotHash(resources.a) == meshA && DeclarationCrc(baseMaterials[0].native) == declarationA &&
            DeclarationCrc(materials[0].effective) == declarationB &&
            R_RemapShaderBySkin(resources.a->Surface(0)->shader, NULL, NULL) == baseMaterials[0].native,
            "per-entity appearance selection repainted another native instance or declaration");
    Require(stats, !Wolf2009_Unmount("qachar"), "resident native character model did not retain its detail package lease");
    printf("WOLF2009_NATIVE_CHARACTER_SKIN_OK package=%s lod=0 joints=52 vertices=1333 triangles=2110 skin=%s "
           "native_custom_skin=1 production_appearance=1 qualified_diffuse=1 original_txtr=1 default_A_unchanged=1 "
           "unknown_skin_no_effects=1 idempotent=1 gpu=0\n", CharacterPackage, application.name.c_str());
}
}
bool Wolf2009_RunSkinnedChecks() {
    Stats stats;
    try {
        Require(stats, common->IsInitialized() && renderModelManager && !renderSystem->IsOpenGLRunning(),
                "skinned probe requires actual common initialization with no GL context");
        Resources resources;
        Require(stats, Wolf2009_Mount("qaskin", ModelPackage), "original skinned MPK could not be mounted"); resources.skinMounted = true;
        Require(stats, Wolf2009_Mount("qaskel", SkeletonPackage), "original global SKEL SPK could not be mounted"); resources.skeletonMounted = true;
        unsigned long long skinEpoch = 0, skeletonEpoch = 0;
        Require(stats, Wolf2009_MountEpoch("qaskin", skinEpoch) && skinEpoch && Wolf2009_MountEpoch("qaskel", skeletonEpoch) && skeletonEpoch,
                "skinned source epochs are unavailable");
        resources.catalog = true;
        Wolf2009_ClearRenderCatalog();
        Require(stats, Wolf2009_LoadMaterialCatalog("fixtures/wolf-materials.bin"), "native skinned material catalog failed to load");
        idStr alias;
        Require(stats, Wolf2009_RegisterSkinnedModel(ModelPath, AnimationPath, SkeletonPath, 0, alias), "explicit MODL/HKXA/SKEL/LOD registration failed");
        SkinnedModelRequest request;
        Require(stats, Wolf2009_GetSkinnedModelRequest(alias, request) && request.sourceEpochs[0] == skinEpoch &&
                request.sourceEpochs[1] == skinEpoch && request.sourceEpochs[2] == skeletonEpoch && request.levelOfDetail == 0,
                "native alias registration lost source epochs or explicit LOD");
        const auto data = LoadSkinnedModel(request, Read);
        const auto clip = GreeterAnimation::Load(Read(request.animation), Read(request.skeleton));
        const auto body = DecodeGreeterBody1(Read(request.model), ParseResourcePath(request.model).name, clip.Joints(), 0);
        Require(stats, data.joints.size() == 52 && data.surfaces.size() == 1 && data.surfaces[0].geometry.vertices.size() == 345 &&
                data.surfaces[0].geometry.indices.size() == 413 * 3 && data.otherLodMeshes == 0,
                "bounded original map LOD0 source fixture changed");
        idRenderModelMD5* model = FindNative(stats, resources, alias);
        CheckSources(stats, model, data); CheckBindHierarchy(stats, model, data);
        const auto materials = Materials(stats, data);
        Require(stats, !Wolf2009_Unmount("qaskin") && !Wolf2009_Unmount("qaskel"), "resident native MD5 did not hold all source leases");
        CheckEpoch(stats, "qaskin", skinEpoch); CheckEpoch(stats, "qaskel", skeletonEpoch);
        GreeterPose bind; bind.model = body.bindModelJoints;
        const GreeterPose independent = clip.SamplePose(.731 * clip.Duration());
        PoseInstance(stats, "independent-B", model, resources.b, data, body, independent, materials);
        const std::uint64_t unchangedB = SnapshotHash(resources.b);
        PoseInstance(stats, "bind-A", model, resources.a, data, body, bind, materials);
        Require(stats, resources.a != resources.b && SnapshotHash(resources.b) == unchangedB,
                "first native supplied-pose instance overwrote another snapshot");
        for (double seconds : {.5 * clip.FrameDuration(), 1.25 * clip.FrameDuration(), .271 * clip.Duration(),
                               .5 * clip.Duration(), clip.Duration() - .25 * clip.FrameDuration()}) {
            const auto pose = clip.SamplePose(seconds);
            PoseInstance(stats, "fractional-A", model, resources.a, data, body, pose, materials);
            Require(stats, resources.a != resources.b && SnapshotHash(resources.b) == unchangedB,
                    "cached source animation update changed an independently supplied native snapshot");
        }
        PoseInstance(stats, "cached-B", model, resources.b, data, body, clip.SamplePose(.125 * clip.Duration()), materials);
        const std::uint64_t unchangedA = SnapshotHash(resources.a);
        PoseInstance(stats, "cached-B-return", model, resources.b, data, body, independent, materials);
        Require(stats, SnapshotHash(resources.a) == unchangedA && SnapshotHash(resources.b) == unchangedB,
                "native two-instance skinning retained mutable shared pose state");
        resources.DeleteSnapshots();
        model->PurgeModel(); SkinnedModelSourceInfo none;
        Require(stats, !model->IsLoaded() && model->NumJoints() == 0 && !Wolf2009_GetSkinnedModelSources(model, none),
                "native MD5 purge retained source geometry/identity");
        Require(stats, FindNative(stats, resources, alias) == model, "same-epoch normal model-manager reload changed model identity");
        CheckSources(stats, model, data); CheckBindHierarchy(stats, model, data);
        PoseInstance(stats, "same-epoch-reload", model, resources.a, data, body, clip.SamplePose(clip.Duration()), materials);
        resources.DeleteSnapshots(); model->PurgeModel();
        Require(stats, Wolf2009_Unmount("qaskin"), "native MD5 purge did not release the model/HKXA source lease"); resources.skinMounted = false;
        Require(stats, Wolf2009_Unmount("qaskel"), "native MD5 purge did not release the skeleton source lease"); resources.skeletonMounted = false;
        Require(stats, Wolf2009_Mount("qaskin", ModelPackage), "source remount failed"); resources.skinMounted = true;
        Require(stats, Wolf2009_Mount("qaskel", SkeletonPackage), "skeleton remount failed"); resources.skeletonMounted = true;
        unsigned long long nextSkin = 0, nextSkeleton = 0;
        Require(stats, Wolf2009_MountEpoch("qaskin", nextSkin) && nextSkin != skinEpoch && Wolf2009_MountEpoch("qaskel", nextSkeleton) && nextSkeleton != skeletonEpoch,
                "remounted source reused an earlier epoch");
        Require(stats, renderModelManager->FindModel(alias) == model && model->IsDefaultModel() && model->NumJoints() == 0 &&
                !Wolf2009_GetSkinnedModelSources(model, none), "stale source alias loaded a remounted original resource");
        idStr fresh;
        Require(stats, Wolf2009_RegisterSkinnedModel(ModelPath, AnimationPath, SkeletonPath, 0, fresh) && fresh.Icmp(alias) != 0,
                "remounted source did not get a new native cache identity");
        idRenderModelMD5* freshModel = FindNative(stats, resources, fresh);
        Require(stats, freshModel != model, "fresh source epoch reused a stale native MD5 object");
        SkinnedModelRequest freshRequest;
        Require(stats, Wolf2009_GetSkinnedModelRequest(fresh, freshRequest), "fresh source binding was not retained");
        const auto freshData = LoadSkinnedModel(freshRequest, Read);
        CheckSources(stats, freshModel, freshData); CheckBindHierarchy(stats, freshModel, freshData);
        const auto freshMaterials = Materials(stats, freshData);
        PoseInstance(stats, "fresh-epoch-bind", freshModel, resources.a, freshData, body, bind, freshMaterials);
        Require(stats, !Wolf2009_Unmount("qaskin") && !Wolf2009_Unmount("qaskel"), "fresh resident MD5 lost its source leases");
        CheckCharacterSkin(stats, resources);
        resources.DeleteModels();
        Require(stats, Wolf2009_Unmount("qachar"), "normal native character destruction did not release detail package pins"); resources.characterMounted = false;
        Require(stats, Wolf2009_Unmount("qaskin"), "normal native MD5 destruction did not release model source pins"); resources.skinMounted = false;
        Require(stats, Wolf2009_Unmount("qaskel"), "normal native MD5 destruction did not release skeleton source pins"); resources.skeletonMounted = false;
        printf("WOLF2009_ACTUAL_ENGINE_SKINNED_QA_OK checks=%zu poses=%zu corners=%zu cache_updates=%zu joints=52 "
               "source_vertices=345 source_triangles=413 max_position_error=%.9g max_hierarchy_error=%.9g max_uv_error=%.9g "
               "max_bounds_error=%.9g diffuse_material_checks=%zu fallback_material_checks=%zu modulated_material_checks=%zu "
               "same_epoch_reload=1 stale_epoch_refused=1 new_epoch_loaded=1 source_leases_released=1 gpu=0\n",
               stats.checks, stats.poses, stats.corners, stats.cacheUpdates, stats.positionError, stats.hierarchyError,
               stats.uvError, stats.boundsError, stats.diffuse, stats.fallbacks, stats.modulated);
        return true;
    } catch (const std::exception& error) {
        fprintf(stderr, "WOLF2009_NATIVE_SKINNED_FAIL checks=%zu poses=%zu message=%s\n", stats.checks, stats.poses, error.what());
    } catch (...) {
        fprintf(stderr, "WOLF2009_NATIVE_SKINNED_FAIL checks=%zu poses=%zu message=native-engine-exception\n", stats.checks, stats.poses);
    }
    return false;
}

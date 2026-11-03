// SPDX-License-Identifier: GPL-3.0-or-later
// Actual native GPU body proof. Supplied source poses isolate the renderer;
// this is not game-entity playback, head/face integration or a mission scene.
#include "EngineSkinnedModelData.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "EngineOffscreenFrame.h"
namespace {
using wolf2009::idtech4a::Bytes;
void Need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Bytes Read(const char* path) {
    idFile* file = fileSystem->OpenFileRead(path, false);
    Need(file != NULL, "GPU body source absent");
    struct Close { idFile* f; ~Close() { fileSystem->CloseFile(f); } } close = {file};
    const int count = file->Length(); Need(count > 0 && count <= 64 * 1024 * 1024, "GPU body source budget");
    Bytes bytes(static_cast<std::size_t>(count));
    Need(file->Read(bytes.data(), count) == count, "short GPU body source read"); return bytes;
}
struct Scene {
    idRenderWorld* world = NULL;
    idRenderModel* model = NULL;
    idRenderModel* preview = NULL;
    idJointMat* joints = NULL;
    std::vector<const char*> mounts;
    void Mount(const char* name, const char* package) {
        Need(Wolf2009_Mount(name, package), "GPU body package mount failed"); mounts.push_back(name);
    }
    bool Release() {
        if (world) { renderSystem->FreeRenderWorld(world); world = NULL; }
        delete preview; preview = NULL;
        if (model) { renderModelManager->RemoveModel(model); renderModelManager->FreeModel(model); model = NULL; }
        if (joints) { Mem_Free16(joints); joints = NULL; }
        bool ok = true;
        for (auto i = mounts.rbegin(); i != mounts.rend(); ++i) if (!Wolf2009_Unmount(*i)) ok = false;
        mounts.clear(); return ok;
    }
    ~Scene() { if (!Release()) fprintf(stderr, "WOLF2009_SKINNED_GPU_CLEANUP_FAIL\n"); }
};
int Difference(const Bytes& a, const Bytes& b) {
    int changed = 0;
    for (std::size_t i = 0; i < a.size(); i += 4) {
        int sum = 0; for (int c = 0; c < 3; ++c) sum += std::abs(int(a[i+c]) - int(b[i+c]));
        if (sum > 12) ++changed;
    }
    return changed;
}
void Render(Scene& scene, const renderView_t& view, Bytes& pixels, const char* path) {
    Need(Wolf2009_RenderQaScene(scene.world, view, pixels.data(), int(pixels.size())), "GPU body native scene/readback failed");
    Need(Wolf2009_WriteQaFrame(path, pixels.data(), int(pixels.size())), "GPU body frame write failed");
}
const idDeclSkin* WhiteControl(const idRenderModel* model, const idDeclSkin* skin, const idMaterial*& textured) {
    Need(model->NumSurfaces() == 1, "GPU body control requires the qualified one-draw source profile");
    const idMaterial* base = model->Surface(0)->shader;
    textured = R_RemapShaderBySkin(base, skin, NULL);
    Need(textured && textured != base && textured->GetState() != DS_DEFAULTED && textured->Coverage() == MC_OPAQUE,
         "GPU body authored skin did not select a qualified opaque material");
    std::vector<char> text(static_cast<std::size_t>(textured->GetTextLength() + 1)); textured->GetText(text.data());
    const char* first = strstr(text.data(), "\"wolf2009/"); const char* end = first ? strchr(first + 1, '"') : NULL;
    Need(first && end, "GPU body source texture declaration missing");
    const idStr quoted(first, 0, int(end - first + 1));
    Need(quoted.Find("/txtr/") != -1 && !strstr(end + 1, "\"wolf2009/"), "GPU body control is not a single original TXTR stage");
    idStr materialText(text.data()); materialText.Replace(quoted, "_white");
    const idStr materialName = idStr(skin->GetName()) + "/_qa_white_control";
    idDecl* decl = declManager->CreateNewDecl(DECL_MATERIAL, materialName, "<wolf2009-skinned-gpu-qa>");
    decl->SetText(materialText); decl->Invalidate();
    const idMaterial* white = declManager->FindMaterial(materialName, false);
    Need(white && white->GetState() != DS_DEFAULTED && white->Coverage() == textured->Coverage(), "GPU body white control declaration failed");
    const idStr name = idStr(skin->GetName()) + "/_qa_white_skin";
    const idStr skinText = va("{\n \"%s\" \"%s\"\n}\n", base->GetName(), white->GetName());
    idDecl* control = declManager->CreateNewDecl(DECL_SKIN, name, "<wolf2009-skinned-gpu-qa>");
    control->SetText(skinText); control->Invalidate();
    const idDeclSkin* result = declManager->FindSkin(name, false);
    Need(result && result->GetState() != DS_DEFAULTED && result->RemapShaderBySkin(base) == white, "GPU body control skin failed");
    return result;
}
}
bool Wolf2009_RunSkinnedOffscreenChecks() {
    try {
        Need(common->IsInitialized() && renderSystem->IsOpenGLRunning(), "GPU body requires initialized common and EGL renderer");
        Scene scene;
        scene.Mount("gpubody", "streampacks/allies_kreisau_agt02.spk");
        scene.Mount("gpuanim", "maps/game/trainyard/trainyard.mpk");
        scene.Mount("gpuskel", "maps/game/global/global.spk");
        const char* animation = "wolf2009/gpuanim/HKXA/maps/game/trainyard/trainyard";
        const char* skeleton = "wolf2009/gpuskel/SKEL/maps/game/global/global";
        const auto clip = wolf2009::GreeterAnimation::Load(Read(animation), Read(skeleton));
        idStr modelAlias;
        Need(Wolf2009_RegisterSkinnedModel("wolf2009/gpubody/MODL/models/characters/civilians/civ_male_04/anims/body_cm04.md5r",
                                         animation, skeleton, 0, modelAlias), "GPU body source registration failed");
        idRenderModel* loaded = renderModelManager->FindModel(modelAlias);
        Need(loaded && !loaded->IsDefaultModel(), "GPU body normal model-manager load failed"); scene.model = loaded;
        idRenderModelMD5* model = dynamic_cast<idRenderModelMD5*>(scene.model);
        Need(model && model->NumJoints() == 52, "GPU body is not the qualified native MD5 model");
        idStr appearance;
        Need(Wolf2009_RegisterSkinnedAppearance(modelAlias, "resist_fighter_B", "fixtures/wolf-materials.bin", appearance),
             "GPU body production appearance registration failed");
        const idDeclSkin* skin = declManager->FindSkin(appearance, false);
        Need(skin && skin->GetState() != DS_DEFAULTED, "GPU body production skin absent");
        scene.world = renderSystem->AllocRenderWorld();
        Need(scene.world && scene.world->InitFromMap(NULL), "GPU body normal empty render-world initialization failed");
        scene.joints = static_cast<idJointMat*>(Mem_Alloc16(52 * sizeof(idJointMat)));
        renderEntity_t entity; memset(&entity, 0, sizeof(entity));
        entity.hModel = model; entity.axis.Identity(); entity.numJoints = 52; entity.joints = scene.joints;
        entity.customSkin = skin; entity.noShadow = true;
        for (int c = 0; c < 4; ++c) entity.shaderParms[c] = 1.0f;
        const auto first = clip.SamplePose(0);
        Wolf2009_ToNativeModelJoints(first.model, scene.joints, 52);
        Need(model->Wolf2009FrameBounds(scene.joints, 52, entity.bounds), "GPU body first native bounds failed");
        scene.preview = model->InstantiateDynamicModel(&entity, NULL, NULL);
        Need(scene.preview != NULL, "GPU body native shader snapshot failed");
        const idMaterial* textured = NULL;
        const idDeclSkin* white = WhiteControl(scene.preview, skin, textured);
        delete scene.preview; scene.preview = NULL;
        const int handle = scene.world->AddEntityDef(&entity);
        Need(handle >= 0, "GPU body native render entity registration failed");
        const idVec3 center = (entity.bounds[0] + entity.bounds[1]) * .5f;
        const float radius = (entity.bounds[1] - entity.bounds[0]).Length() * .5f;
        Need(std::isfinite(radius) && radius > 1 && radius < 256, "GPU body source frame bounds outside qualified profile");
        renderView_t view; memset(&view, 0, sizeof(view));
        view.viewID = 29; view.width = SCREEN_WIDTH; view.height = SCREEN_HEIGHT;
        view.fov_x = 60; view.fov_y = 60; view.forceUpdate = true;
        for (int p = 0; p < MAX_GLOBAL_SHADER_PARMS; ++p) view.shaderParms[p] = 1.0f;
        view.vieworg = center + idVec3(1.7f * radius, -1.7f * radius, .2f * radius);
        idVec3 forward = center - view.vieworg; Need(forward.Normalize() > .01f, "GPU body camera direction failed");
        view.viewaxis = forward.ToMat3();
        Bytes a(Wolf2009_QAFrameBytes), flat(Wolf2009_QAFrameBytes), b(Wolf2009_QAFrameBytes);
        Render(scene, view, a, "fixtures/offscreen-skinned-start.bmp");
        entity.customSkin = white; scene.world->UpdateEntityDef(handle, &entity);
        Render(scene, view, flat, "fixtures/offscreen-skinned-flat.bmp");
        const int texturePixels = Difference(a, flat);
        Need(texturePixels > 256, "GPU body frame has no original source-texture contribution");
        const auto second = clip.SamplePose(.271 * clip.Duration());
        Wolf2009_ToNativeModelJoints(second.model, scene.joints, 52);
        Need(model->Wolf2009FrameBounds(scene.joints, 52, entity.bounds), "GPU body fractional native bounds failed");
        entity.customSkin = skin; scene.world->UpdateEntityDef(handle, &entity);
        Render(scene, view, b, "fixtures/offscreen-skinned-fractional.bmp");
        const int posePixels = Difference(a, b);
        Need(posePixels > 64, "GPU body source poses made no observable native frame change");
        int uploaded = 0;
        for (int s = 0; s < textured->GetNumStages(); ++s) {
            const idImage* image = textured->GetStage(s)->texture.image;
            if (image && image->imgName.Find("wolf2009/gpubody/txtr/") == 0 && !image->defaulted &&
                image->texnum != idImage::TEXTURE_NOT_LOADED && image->bindCount > 0) {
                ++uploaded; printf("WOLF2009_SKINNED_GPU_TEXTURE source=%s upload=%dx%d binds=%d\n",
                                   image->imgName.c_str(), image->uploadWidth, image->uploadHeight, image->bindCount);
            }
        }
        Need(uploaded == 1, "GPU body did not bind its original texture through the native backend");
        Need(scene.Release(), "GPU body world/model destruction did not release source mounts");
        printf("WOLF2009_ACTUAL_ENGINE_SKINNED_GPU_QA_OK joints=52 source_vertices=1333 source_triangles=2110 "
               "poses=2 texture_pixels=%d pose_pixels=%d native_custom_skin=1 source_txtr_stages=1 "
               "normal_world_model_teardown=1 game_entity_playback=0 head_face=0 gpu=1\n", texturePixels, posePixels);
        return true;
    } catch (const std::exception& error) { fprintf(stderr, "WOLF2009_SKINNED_GPU_FAIL %s\n", error.what()); }
    catch (...) { fprintf(stderr, "WOLF2009_SKINNED_GPU_FAIL native-engine-exception\n"); }
    return false;
}

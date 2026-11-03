// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in checks inside an already initialized actual GPL engine. No GL context,
// game window, asset extraction, replacement renderer or stand-in engine types.
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "framework/Wolf2009Streams.h"
#include "Wolf2009Render.h"
#include <math.h>

namespace {
bool Check(bool value, const char* message) {
    if (!value) fprintf(stderr, "WOLF2009_NATIVE_RENDER_FAIL %s\n", message);
    return value;
}
bool Finite(float value) { return value == value && value > -1e30f && value < 1e30f; }
void MaterialsCommand(const char* archive) {
    // Common's startup argv path also uses tokenized command arguments. Paths
    // containing a hyphen must remain one argument, not console punctuation.
    idCmdArgs args;
    args.AppendArg("wolf2009Materials"); args.AppendArg(archive);
    cmdSystem->BufferCommandArgs(CMD_EXEC_NOW, args);
}
bool CheckImage(idRenderModel* model) {
    for (int s = 0; s < model->NumSurfaces(); ++s) {
        const idMaterial* material = model->Surface(s)->shader;
        char* text = new char[material->GetTextLength() + 1];
        material->GetText(text);
        char* begin = strstr(text, "\"wolf2009/");
        bool ok = false;
        if (begin) {
            ++begin;
            char* end = strchr(begin, '"');
            if (end) {
                *end = 0;
                byte* pixels = NULL; int width = 0, height = 0; ID_TIME_T timestamp = -1;
                R_LoadImage(begin, &pixels, &width, &height, &timestamp, true);
                ok = pixels != NULL && width > 0 && height > 0 && timestamp == 0;
                printf("WOLF2009_NATIVE_IMAGE path=%s width=%d height=%d timestamp=%lld\n",
                       begin, width, height, static_cast<long long>(timestamp));
                if (pixels) R_StaticFree(pixels);
            }
        }
        delete[] text;
        if (ok) return true;
    }
    return Check(false, "native R_LoadImage did not decode a world diffuse");
}
bool CheckSourceStages(const idMaterial* material, int expectedColor) {
    int sourceStages = 0;
    for (int s = 0; s < material->GetNumStages(); ++s) {
        const shaderStage_t* stage = material->GetStage(s);
        if (!stage->texture.image || stage->texture.image->imgName.Find("/txtr/") == -1) continue;
        ++sourceStages;
        if (!Check(stage->vertexColor == expectedColor,
                   "native source stage has the wrong vertex-colour mode")) return false;
    }
    return Check(sourceStages > 0, "supported native profile has no source texture stage");
}
bool CheckModel(idRenderModel* model, const char* scope, bool world,
                int expectedDiffuse = -1, int expectedWhite = -1, int expectedColor = -1) {
    if (!Check(model && model->IsLoaded() && !model->IsDefaultModel(), "native model absent/defaulted"))
        return false;
    if (!Check(model->IsStaticWorldModel() == world && model->IsDynamicModel() == DM_STATIC,
               "native model static/world classification")) return false;
    const idBounds bounds = model->Bounds();
    int vertices = 0, indexes = 0, diffuse = 0, whiteSurfaces = 0, colorSurfaces = 0, coloredVertices = 0;
    for (int axis = 0; axis < 3; ++axis) {
        if (!Check(Finite(bounds[0][axis]) && Finite(bounds[1][axis]) &&
                   bounds[0][axis] <= bounds[1][axis], "native culling bounds")) return false;
    }
    for (int s = 0; s < model->NumSurfaces(); ++s) {
        const modelSurface_t* surface = model->Surface(s);
        const srfTriangles_t* tri = surface->geometry;
        if (!Check(tri && tri->numVerts > 0 && tri->numIndexes % 3 == 0 && surface->shader &&
                   surface->shader->GetState() != DS_DEFAULTED, "native surface/material validity")) return false;
        const bool whiteProfile = strstr(surface->shader->GetName(), "/materials-identity-white/") != NULL;
        const bool colorProfile = strstr(surface->shader->GetName(), "/materials-vertex-color/") != NULL;
        if (whiteProfile) ++whiteSurfaces;
        if (colorProfile) ++colorSurfaces;
        for (int i = 0; i < tri->numIndexes; ++i) {
            if (!Check(tri->indexes[i] >= 0 && tri->indexes[i] < tri->numVerts, "native index bounds")) return false;
        }
        for (int v = 0; v < tri->numVerts; ++v) {
            const idDrawVert& vertex = tri->verts[v];
            if (!colorProfile) {
                for (int c = 0; c < 4; ++c)
                    if (!Check(vertex.color[c] == 255,
                               "unmodulated native profile uploaded a source colour")) return false;
            } else {
                if (vertex.color[0] != 255 || vertex.color[1] != 255 || vertex.color[2] != 255)
                    ++coloredVertices;
                if (surface->shader->Coverage() == MC_PERFORATED &&
                    !Check(vertex.color[3] == 255, "alpha-tested source profile has nonidentity vertex alpha"))
                    return false;
            }
            for (int axis = 0; axis < 3; ++axis) {
                if (!Check(Finite(vertex.xyz[axis]) && Finite(vertex.normal[axis]) &&
                           Finite(vertex.tangents[0][axis]) && Finite(vertex.tangents[1][axis]) &&
                           vertex.xyz[axis] >= bounds[0][axis] - 0.02f &&
                           vertex.xyz[axis] <= bounds[1][axis] + 0.02f,
                           "native vertex, derived normal/tangent or model bounds")) return false;
            }
        }
        const idMaterial* material = surface->shader;
        char* text = new char[material->GetTextLength() + 1];
        material->GetText(text);
        if (strstr(text, "/txtr/")) {
            ++diffuse;
            if (!CheckSourceStages(material, colorProfile ? SVC_MODULATE : SVC_IGNORE)) {
                delete[] text;
                return false;
            }
        } else if (!Check(!whiteProfile && !colorProfile, "qualified native profile became a checker fallback")) {
            delete[] text;
            return false;
        }
        delete[] text;
        vertices += tri->numVerts;
        indexes += tri->numIndexes;
    }
    printf("WOLF2009_NATIVE_MODEL scope=%s name=%s surfaces=%d vertices=%d triangles=%d diffuse_surfaces=%d "
           "bounds=(%g,%g,%g)-(%g,%g,%g) identity_white_surfaces=%d native_vertex_color_surfaces=%d "
           "nonwhite_rgb_vertices=%d\n", scope, model->Name(), model->NumSurfaces(), vertices,
           indexes / 3, diffuse, bounds[0][0], bounds[0][1], bounds[0][2],
           bounds[1][0], bounds[1][1], bounds[1][2], whiteSurfaces, colorSurfaces, coloredVertices);
    return Check(vertices > 0 && indexes > 0, "native model has no geometry") &&
           Check(expectedDiffuse < 0 || diffuse == expectedDiffuse, "native world diffuse coverage changed") &&
           Check(expectedWhite < 0 || whiteSurfaces == expectedWhite, "native identity-white coverage changed") &&
           Check(expectedColor < 0 || colorSurfaces == expectedColor, "native source-colour coverage changed") &&
           Check(expectedColor <= 0 || coloredVertices > 0, "native source-colour profiles contain only white RGB");
}
bool CheckVariantIsolation(idRenderModel* model) {
    const char* whitePrefix = "/materials-identity-white/";
    const char* regularPrefix = "/materials/";
    const char* colorPrefix = "/materials-vertex-color/";
    int pairs = 0, fallbackPairs = 0;
    for (int s = 0; s < model->NumSurfaces(); ++s) {
        const modelSurface_t* white = model->Surface(s);
        const char* qualified = strstr(white->shader->GetName(), whitePrefix);
        if (!qualified) continue;
        const char* authored = qualified + strlen(whitePrefix);
        for (int v = 0; v < white->geometry->numVerts; ++v)
            for (int c = 0; c < 4; ++c)
                if (!Check(white->geometry->verts[v].color[c] == 255,
                           "identity-white native colour is not the neutral constant")) return false;
        for (int other = 0; other < model->NumSurfaces(); ++other) {
            const idMaterial* material = model->Surface(other)->shader;
            const char* regular = strstr(material->GetName(), regularPrefix);
            const char* colored = strstr(material->GetName(), colorPrefix);
            if (colored && !strcmp(colored + strlen(colorPrefix), authored)) {
                if (!Check(material != white->shader && CheckSourceStages(material, SVC_MODULATE),
                           "coloured draw inherited the identity-white material/cache entry")) return false;
                ++pairs;
                continue;
            }
            if (!regular || strcmp(regular + strlen(regularPrefix), authored)) continue;
            char* text = new char[material->GetTextLength() + 1]; material->GetText(text);
            const bool fallback = strstr(text, "_default") && !strstr(text, "/txtr/");
            delete[] text;
            if (!Check(fallback && material != white->shader,
                       "coloured draw inherited the identity-white material/cache entry")) return false;
            ++fallbackPairs;
        }
    }
    printf("WOLF2009_NATIVE_MATERIAL_VARIANTS isolated_pairs=%d fallback_pairs=%d\n", pairs, fallbackPairs);
    return Check(pairs > 0, "real map did not exercise identity and modulated variants of a shared material");
}
bool CheckCatalogFallback(idRenderModel* model) {
    for (int s = 0; s < model->NumSurfaces(); ++s) {
        const idMaterial* material = model->Surface(s)->shader;
        char* text = new char[material->GetTextLength() + 1]; material->GetText(text);
        const bool fallback = strstr(text, "_default") && !strstr(text, "/txtr/");
        delete[] text;
        if (!Check(fallback, "failed catalog command silently retained the previous diffuse catalog")) return false;
    }
    return true;
}
bool Map(const char* label, const char* package, const char* resource,
         int expectedDiffuse, int expectedWhite, int expectedColor) {
    const bool expectDiffuse = expectedDiffuse > 0;
    if (!Check(Wolf2009_Mount(label, package), "real MPK mount")) return false;
    idStr path = va("wolf2009/%s/PROC/%s.proc", label, resource);
    idRenderWorld* world = renderSystem->AllocRenderWorld();
    if (!Check(world != NULL, "native AllocRenderWorld")) return false;
    bool ok = world->InitFromMap(path);
    idStr areaName;
    Wolf2009_InlineModelName(path, "_area0", areaName);
    idRenderModel* model = renderModelManager->CheckModel(areaName);
    ok = ok && Check(world->NumAreas() == 1 && world->PointInArea(vec3_origin) == 0,
                     "single-area native world") &&
         CheckModel(model, label, true, expectedDiffuse, expectedWhite, expectedColor) &&
         (expectDiffuse ? CheckImage(model) : CheckCatalogFallback(model));
    if (ok && expectedColor > 0) ok = CheckVariantIsolation(model);
    if (ok) {
        // Bad replacement source must fail without discarding the loaded world.
        idStr missing = va("wolf2009/%s/PROC/maps/missing.proc", label);
        ok = !world->InitFromMap(missing) &&
             renderModelManager->CheckModel(areaName) == model && world->NumAreas() == 1;
    }
    renderSystem->FreeRenderWorld(world);
    ok = ok && Check(renderModelManager->CheckModel(areaName) == NULL, "world model manager cleanup");
    ok = Wolf2009_Unmount(label) && ok;
    return ok;
}
}

bool Wolf2009_RunRenderChecks() {
    printf("WOLF2009_NATIVE_RENDER_SCOPE actual_common_renderWorld_modelManager_R_LoadImage GL=%d\n",
           renderSystem->IsOpenGLRunning());
    if (!Check(common->IsInitialized() && renderModelManager && !renderSystem->IsOpenGLRunning(),
               "probe requires common init and no GL")) return false;
    // Exercise the same registered startup command used by a normal host.
    // A missing command or load failure is exposed by the real map/image checks.
    Wolf2009_ClearRenderCatalog();
    MaterialsCommand("fixtures/wolf-materials.bin");
    if (!Map("qaempty", "maps/game/empty/empty.mpk", "maps/game/empty/empty", 2, 0, 0)) return false;
    MaterialsCommand("fixtures/missing-materials.bin");
    if (!Map("qafailed", "maps/game/empty/empty.mpk", "maps/game/empty/empty", 0, 0, 0)) return false;
    printf("WOLF2009_NATIVE_CATALOG_FAILCLOSED_OK\n");
    MaterialsCommand("fixtures/wolf-materials.bin");
    if (!Map("qatrain", "maps/game/trainyard/trainyard.mpk", "maps/game/trainyard/trainyard", 1318, 522, 743)) return false;
    if (!Map("qaware", "maps/game/mtw/mtw_ware.mpk", "maps/game/mtw/mtw_ware", 169, 79, 86)) return false;
    if (!Check(Wolf2009_Mount("qamodl", "maps/game/trainyard/trainyard.mpk"), "static MODL mount")) return false;
    const char* modelPaths[] = {
        "wolf2009/qamodl/MODL/models/props/mechanical/cannery_machinepart_06.md5r",
        "wolf2009/qamodl/MODL/models/weapons/dynamite/dynamite.md5r",
        "wolf2009/qamodl/MODL/models/architecture/train_station/station_arch_decals.md5r"
    };
    for (unsigned i = 0; i < sizeof(modelPaths) / sizeof(modelPaths[0]); ++i) {
        idRenderModel* model = renderModelManager->CheckModel(modelPaths[i]);
        if (!CheckModel(model, "normal-model-manager-entry", false)) return false;
        model->PurgeModel();
        if (!Check(renderModelManager->CheckModel(modelPaths[i]) == model && model->IsLoaded(), "purged model reload"))
            return false;
    }
    if (!Check(Wolf2009_Unmount("qamodl"), "static MODL unmount")) return false;
    Wolf2009_ClearRenderCatalog();
    printf("WOLF2009_ACTUAL_ENGINE_RENDER_QA_OK\n");
    return true;
}

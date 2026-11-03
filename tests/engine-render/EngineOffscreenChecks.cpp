// SPDX-License-Identifier: GPL-3.0-or-later
// Actual idTech4A++ GLES renderer on an explicit EGL pbuffer. No native window,
// input synthesis, standalone rasterizer, game-code substitution or asset export.
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "Wolf2009Render.h"
#include "EngineOffscreenFrame.h"
#include <math.h>

extern bool GLimp_Wolf2009EnablePbuffer(int width, int height);
#ifdef WOLF2009_SKINNED_QA
extern bool Wolf2009_RunSkinnedOffscreenChecks();
#endif
#ifdef WOLF2009_SOURCE_WEAPON_VIEW_QA
extern bool Wolf2009_RunSourceWeaponViewCoreChecks();
#endif
static bool wolf2009ZeroOpaqueAlpha = false;
void Wolf2009_InspectDrawState(const srfTriangles_t* tri) {
    const shaderProgram_t* program = backEnd.glState.currentProgram;
    if (wolf2009ZeroOpaqueAlpha && program == &depthFillShader && program->alphaTest >= 0)
        qglUniform1f(program->alphaTest, 0.0f);
    static int calls = 0;
    if (calls++ >= 12) return;
    GLint active = 0, depthFunc = 0, cull = 0, stencil = 0, texture0 = 0, sampler0 = -1, frontFace = 0;
    GLint vertexEnabled = 0, vertexSize = 0, vertexStride = 0, vertexBuffer = 0, depthBits = 0;
    GLint actualProgram = 0, drawFbo = 0, viewport[4] = {}, scissor[4] = {};
    GLboolean depthWrite = 0, colorWrite[4] = {};
    GLfloat alpha = -1, color[4] = {}, matrix[16] = {}, depthRange[2] = {};
    qglGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    qglGetIntegerv(GL_DEPTH_FUNC, &depthFunc); qglGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    qglGetBooleanv(GL_COLOR_WRITEMASK, colorWrite); qglGetIntegerv(GL_CULL_FACE_MODE, &cull);
    qglGetIntegerv(GL_FRONT_FACE, &frontFace); qglGetIntegerv(GL_DEPTH_BITS, &depthBits);
    qglGetIntegerv(GL_CURRENT_PROGRAM, &actualProgram); qglGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawFbo);
    qglGetIntegerv(GL_VIEWPORT, viewport); qglGetIntegerv(GL_SCISSOR_BOX, scissor);
    qglGetIntegerv(GL_STENCIL_FUNC, &stencil); qglGetFloatv(GL_DEPTH_RANGE, depthRange);
    qglActiveTexture(GL_TEXTURE0); qglGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0); qglActiveTexture(active);
    if (program) {
        if (program->alphaTest >= 0) qglGetUniformfv(program->program, program->alphaTest, &alpha);
        if (program->glColor >= 0) qglGetUniformfv(program->program, program->glColor, color);
        if (program->modelViewProjectionMatrix >= 0)
            qglGetUniformfv(program->program, program->modelViewProjectionMatrix, matrix);
        if (program->u_fragmentMap[0] >= 0)
            qglGetUniformiv(program->program, program->u_fragmentMap[0], &sampler0);
        if (program->attr_Vertex >= 0) {
            qglGetVertexAttribiv(program->attr_Vertex, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertexEnabled);
            qglGetVertexAttribiv(program->attr_Vertex, GL_VERTEX_ATTRIB_ARRAY_SIZE, &vertexSize);
            qglGetVertexAttribiv(program->attr_Vertex, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &vertexStride);
            qglGetVertexAttribiv(program->attr_Vertex, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vertexBuffer);
        }
    }
    printf("WOLF2009_OFFSCREEN_DRAW frame=%d call=%d program=%u is_zfill=%d indexes=%d "
           "active=0x%x cached_unit=%d texture0=%d white=%u sampler=%d alpha=%g color=(%g,%g,%g,%g) "
           "depth_enabled=%d depth_func=0x%x depth_write=%d depth_range=%g..%g color_write=%d%d%d%d "
           "cull_enabled=%d cull=0x%x stencil_enabled=%d stencil_func=0x%x "
           "front_face=0x%x depth_bits=%d vertex_attrib=%d enabled=%d size=%d stride=%d buffer=%d expected_vbo=%u "
           "actual_program=%d draw_fbo=%d viewport=%d,%d,%d,%d scissor=%d,%d,%d,%d "
           "mvp=(%g,%g,%g,%g;%g,%g,%g,%g) query_error=0x%x\n",
           tr.frameCount, calls, program ? unsigned(program->program) : 0, int(program == &depthFillShader),
           tri->numIndexes, active, backEnd.glState.currenttmu, texture0, unsigned(globalImages->whiteImage->texnum),
           sampler0, alpha, color[0], color[1], color[2], color[3], int(qglIsEnabled(GL_DEPTH_TEST)), depthFunc,
           int(depthWrite), depthRange[0], depthRange[1], int(colorWrite[0]), int(colorWrite[1]), int(colorWrite[2]),
           int(colorWrite[3]), int(qglIsEnabled(GL_CULL_FACE)), cull, int(qglIsEnabled(GL_STENCIL_TEST)), stencil,
           frontFace, depthBits, program ? program->attr_Vertex : -1, vertexEnabled, vertexSize, vertexStride,
           vertexBuffer, tri->ambientCache ? unsigned(tri->ambientCache->vbo) : 0,
           actualProgram, drawFbo, viewport[0], viewport[1], viewport[2], viewport[3],
           scissor[0], scissor[1], scissor[2], scissor[3],
           matrix[0], matrix[5], matrix[10], matrix[15], matrix[12], matrix[13], matrix[14], matrix[3],
           unsigned(qglGetError()));
    fflush(stdout);
}
void Wolf2009_InspectDepthStencilFramebuffer(const char* phase, idFramebuffer* fb, idImage* image) {
    const GLenum before = qglGetError();
    GLint binding = 0; qglGetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
    printf("WOLF2009_OFFSCREEN_FBO_PHASE phase=%s binding=%d object=%u color_rb=%u depth_rb=%u "
           "texture=%u is_texture=%d texture_size=%dx%d texture_format=0x%x prior_error=0x%x status=0x%x\n",
           phase, binding, unsigned(fb->frameBuffer), unsigned(fb->colorBuffers[0]), unsigned(fb->depthBuffer),
           unsigned(image->texnum), int(qglIsTexture(image->texnum)), image->uploadWidth, image->uploadHeight,
           image->internalFormat, unsigned(before), unsigned(qglCheckFramebufferStatus(GL_FRAMEBUFFER)));
    const GLenum targets[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    for (unsigned i = 0; binding && i < sizeof(targets) / sizeof(targets[0]); ++i) {
        GLint type = 0, name = 0;
        qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, targets[i], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
        if (type != GL_NONE)
            qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, targets[i], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
        printf("WOLF2009_OFFSCREEN_FBO_PHASE_ATTACHMENT phase=%s target=0x%x type=0x%x name=%d\n",
               phase, unsigned(targets[i]), type, name);
    }
    const GLenum after = qglGetError();
    if (after != GL_NO_ERROR) printf("WOLF2009_OFFSCREEN_FBO_PHASE_QUERY_ERROR 0x%x\n", unsigned(after));
    fflush(stdout);
}
namespace {
// Upstream depth/stencil textures are rounded to powers of two while their
// colour renderbuffers use the window size. Use a matching POT canvas so the
// normal FBO initialization is valid, including on the emulator's GLES3 driver.
const int width = 512, height = 512, pixelCount = width * height;
bool Check(bool value, const char* message) {
    if (!value) fprintf(stderr, "WOLF2009_OFFSCREEN_FAIL %s\n", message);
    return value;
}
void DiagnoseFramebuffer() {
    if (!qglGetIntegerv || !qglCheckFramebufferStatus || !qglGetFramebufferAttachmentParameteriv) return;
    GLint framebuffer = 0, savedRenderbuffer = 0, colorType = 0, colorName = 0, depthType = 0, depthName = 0;
    qglGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    qglGetIntegerv(GL_RENDERBUFFER_BINDING, &savedRenderbuffer);
    printf("WOLF2009_OFFSCREEN_FBO_DIAG binding=%d status=0x%x bound_rb=%d\n",
           framebuffer, unsigned(qglCheckFramebufferStatus(GL_FRAMEBUFFER)), savedRenderbuffer);
    if (!framebuffer) return;
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    for (unsigned i = 0; i < sizeof(attachments) / sizeof(attachments[0]); ++i) {
        GLint type = 0, name = 0, w = 0, h = 0, format = 0, samples = 0;
        qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachments[i], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
        if (type != GL_NONE)
            qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachments[i], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
        if (type == GL_RENDERBUFFER && name) {
            qglBindRenderbuffer(GL_RENDERBUFFER, name);
            qglGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
            qglGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
            qglGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
            qglGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
        }
        printf("WOLF2009_OFFSCREEN_FBO_ATTACHMENT target=0x%x type=0x%x name=%d size=%dx%d format=0x%x samples=%d\n",
               unsigned(attachments[i]), type, name, w, h, format, samples);
        if (i == 0) { colorType = type; colorName = name; }
        if (i == 1) { depthType = type; depthName = name; }
    }
    if (framebuffer && colorType == GL_RENDERBUFFER && depthType == GL_TEXTURE) {
        GLenum none = GL_NONE, color = GL_COLOR_ATTACHMENT0;
        qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
        qglDrawBuffers(1, &none); qglReadBuffer(GL_NONE);
        printf("WOLF2009_OFFSCREEN_FBO_CONTROL depth_texture_only=0x%x\n",
               unsigned(qglCheckFramebufferStatus(GL_FRAMEBUFFER)));
        qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorName);
        qglDrawBuffers(1, &color); qglReadBuffer(GL_COLOR_ATTACHMENT0);
        if (savedRenderbuffer) {
            qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, savedRenderbuffer);
            printf("WOLF2009_OFFSCREEN_FBO_CONTROL color_plus_depth_renderbuffer=0x%x\n",
                   unsigned(qglCheckFramebufferStatus(GL_FRAMEBUFFER)));
            qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthName, 0);
        }
    }
    qglBindRenderbuffer(GL_RENDERBUFFER, savedRenderbuffer);
    for (int i = 0; i < 8; ++i) {
        const GLenum error = qglGetError(); if (error == GL_NO_ERROR) break;
        printf("WOLF2009_OFFSCREEN_FBO_ERROR 0x%x\n", unsigned(error));
    }
    fflush(stdout);
}
struct Pixels {
    byte* data;
    Pixels() : data(new byte[pixelCount * 4]) { memset(data, 0, pixelCount * 4); }
    ~Pixels() { delete[] data; }
  private:
    Pixels(const Pixels&);
    Pixels& operator=(const Pixels&);
};
struct ScopedMap {
    const char* label;
    idRenderWorld* world;
    bool mounted;
    ScopedMap(const char* name) : label(name), world(NULL), mounted(false) {}
    ~ScopedMap() {
        if (world) renderSystem->FreeRenderWorld(world);
        if (mounted) Wolf2009_Unmount(label);
    }
};
struct RestoreShaders {
    idRenderModelStatic* model;
    idList<const idMaterial*> shaders;
    explicit RestoreShaders(idRenderModelStatic* source) : model(source) {
        for (int s = 0; s < model->surfaces.Num(); ++s) shaders.Append(model->surfaces[s].shader);
    }
    void Restore() {
        for (int s = 0; s < shaders.Num(); ++s) model->surfaces[s].shader = shaders[s];
    }
    ~RestoreShaders() { Restore(); }
};
void U16(byte* p, unsigned value) { p[0] = byte(value); p[1] = byte(value >> 8); }
void U32(byte* p, unsigned value) {
    for (int i = 0; i < 4; ++i) p[i] = byte(value >> (i * 8));
}
bool WriteBmp(const char* name, const Pixels& pixels) {
    // Runtime data is isolated under the host's private fs_savepath. Refuse an
    // existing output even there; every run gets a fresh test root.
    idFile* old = fileSystem->OpenFileRead(name, false);
    if (old) { fileSystem->CloseFile(old); return Check(false, "offscreen output already exists"); }
    const int stride = (width * 3 + 3) & ~3, bytes = 54 + stride * height;
    byte* image = new byte[bytes]; memset(image, 0, bytes);
    image[0] = 'B'; image[1] = 'M'; U32(image + 2, bytes); U32(image + 10, 54);
    U32(image + 14, 40); U32(image + 18, width); U32(image + 22, height);
    U16(image + 26, 1); U16(image + 28, 24); U32(image + 34, stride * height);
    U32(image + 38, 2835); U32(image + 42, 2835);
    // GL and positive-height BMP both store bottom rows first. BMP uses BGR.
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const byte* source = pixels.data + (y * width + x) * 4;
        byte* out = image + 54 + y * stride + x * 3;
        out[0] = source[2]; out[1] = source[1]; out[2] = source[0];
    }
    idFile* output = fileSystem->OpenFileWrite(name);
    const bool ok = output && output->Write(image, bytes) == bytes;
    if (output) fileSystem->CloseFile(output);
    delete[] image;
    return Check(ok, "native framebuffer BMP write failed");
}
bool Render(idRenderWorld* world, const renderView_t& view, Pixels& pixels) {
    int front = 0, back = 0;
    renderSystem->BeginFrame(width, height);
    world->RenderScene(&view);
    const int drawSurfaces = tr.primaryView ? tr.primaryView->numDrawSurfs : -1;
    printf("WOLF2009_OFFSCREEN_FRONTEND draw_surfaces=%d\n", drawSurfaces);
    if (tr.primaryView) {
        int clockwise = 0, counterClockwise = 0;
        for (int s = 0; s < tr.primaryView->numDrawSurfs; ++s) {
            const drawSurf_t* surf = tr.primaryView->drawSurfs[s];
            const srfTriangles_t* tri = surf->geo;
            for (int i = 0; i < tri->numIndexes; i += 3) {
                idPlane clip[3]; bool front = true;
                for (int v = 0; v < 3; ++v) {
                    idPlane eye;
                    R_TransformModelToClip(tri->verts[tri->indexes[i + v]].xyz,
                                          surf->space->modelViewMatrix, tr.primaryView->projectionMatrix, eye, clip[v]);
                    if (clip[v][3] <= .001f) front = false;
                    else for (int axis = 0; axis < 3; ++axis) clip[v][axis] /= clip[v][3];
                }
                if (!front) continue;
                bool outside = false;
                for (int axis = 0; axis < 3; ++axis) {
                    if ((clip[0][axis] < -1 && clip[1][axis] < -1 && clip[2][axis] < -1) ||
                        (clip[0][axis] > 1 && clip[1][axis] > 1 && clip[2][axis] > 1)) outside = true;
                }
                if (outside) continue;
                const float area = (clip[1][0] - clip[0][0]) * (clip[2][1] - clip[0][1]) -
                                   (clip[1][1] - clip[0][1]) * (clip[2][0] - clip[0][0]);
                if (area > .000001f) ++counterClockwise;
                if (area < -.000001f) ++clockwise;
            }
        }
        printf("WOLF2009_OFFSCREEN_PROJECTED_TRIANGLES clockwise=%d counterclockwise=%d\n",
               clockwise, counterClockwise);
    }
    renderSystem->EndFrame(&front, &back);
    qglFinish();
    GLint drawFramebuffer = -1, readFramebuffer = -1;
    qglGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawFramebuffer);
    qglGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    if (!Check(drawFramebuffer == 0 && readFramebuffer == 0, "readback is not the EGL pbuffer default framebuffer"))
        return false;
    qglPixelStorei(GL_PACK_ALIGNMENT, 4);
    qglReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data);
    const GLenum error = qglGetError();
    GLint cullMode = 0; qglGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    printf("WOLF2009_OFFSCREEN_FRAME front_ms=%d back_ms=%d draw_fbo=%d read_fbo=%d "
           "draw_elements=%d draw_indexes=%d cull_enabled=%d cull_mode=0x%x cull_cache=%d gl_error=0x%x\n",
           front, back, drawFramebuffer, readFramebuffer, backEnd.pc.c_drawElements,
           backEnd.pc.c_drawIndexes, int(qglIsEnabled(GL_CULL_FACE)), cullMode,
           backEnd.glState.faceCulling, unsigned(error));
    return Check(error == GL_NO_ERROR, "native framebuffer readback GL error");
}
bool HasSourceTexture(const idMaterial* material) {
    char* text = new char[material->GetTextLength() + 1]; material->GetText(text);
    const bool found = strstr(text, "/txtr/") != NULL;
    delete[] text;
    return found;
}
bool HasModulatedSourceStage(const idMaterial* material) {
    for (int s = 0; s < material->GetNumStages(); ++s) {
        const shaderStage_t* stage = material->GetStage(s);
        if (stage->vertexColor == SVC_MODULATE && stage->texture.image &&
            stage->texture.image->imgName.Find("/txtr/") != -1) return true;
    }
    return false;
}
bool LookAt(renderView_t& view, const idVec3& target) {
    idVec3 forward = target - view.vieworg;
    if (!Check(forward.Normalize() > .001f, "zero offscreen camera direction")) return false;
    // idVec3::ToMat3 preserves axis[0] verbatim. Normalize before constructing
    // the camera basis, then assert all three axes and handedness explicitly.
    view.viewaxis = forward.ToMat3();
    for (int axis = 0; axis < 3; ++axis) {
        if (!Check(idMath::Fabs(view.viewaxis[axis].LengthSqr() - 1.0f) < .001f,
                   "offscreen camera axis is not unit length")) return false;
        for (int other = axis + 1; other < 3; ++other)
            if (!Check(idMath::Fabs(view.viewaxis[axis] * view.viewaxis[other]) < .001f,
                       "offscreen camera axes are not orthogonal")) return false;
    }
    return Check((view.viewaxis[0].Cross(view.viewaxis[1]) * view.viewaxis[2]) > .999f,
                 "offscreen camera basis is not right-handed");
}
bool CameraForTexture(const idRenderModelStatic* model, renderView_t& view) {
    float largest = 0;
    idVec3 center, normal;
    const idMaterial* selectedMaterial = NULL;
    int selectedSurface = -1, selectedTriangle = -1;
    for (int s = 0; s < model->NumSurfaces(); ++s) {
        const modelSurface_t* surface = model->Surface(s);
        if (!HasSourceTexture(surface->shader) ||
            !strstr(surface->shader->GetName(), "/materials-vertex-color/") ||
            !HasModulatedSourceStage(surface->shader)) continue;
        const srfTriangles_t* tri = surface->geometry;
        for (int i = 0; i < tri->numIndexes; i += 3) {
            const idDrawVert& a = tri->verts[tri->indexes[i]];
            const idDrawVert& b = tri->verts[tri->indexes[i + 1]];
            const idDrawVert& c = tri->verts[tri->indexes[i + 2]];
            float brightest = 0, modulation = 0;
            for (int rgb = 0; rgb < 3; ++rgb) {
                const float mean = (int(a.color[rgb]) + int(b.color[rgb]) + int(c.color[rgb])) / 3.0f;
                brightest = Max(brightest, mean);
                modulation = Max(modulation, 255.0f - mean);
            }
            // Alpha-only changes and an entirely black triangle cannot prove
            // the visible RGB contribution of both colour and the source DDS.
            if (brightest < 48 || modulation < 32) continue;
            const float area = (b.xyz - a.xyz).Cross(c.xyz - a.xyz).Length();
            idVec3 direction = a.normal + b.normal + c.normal;
            if (area <= largest || direction.Normalize() < .01f) continue;
            largest = area; center = (a.xyz + b.xyz + c.xyz) / 3.0f; normal = direction;
            selectedMaterial = surface->shader;
            selectedSurface = s; selectedTriangle = i;
        }
    }
    if (!Check(largest > 1 && selectedMaterial,
               "map has no modulated source-colour diffuse triangle for the GL view")) return false;
    const float distance = Min(128.0f, Max(32.0f, float(sqrt(largest)) * .8f));
    view.vieworg = center + normal * distance;
    const srfTriangles_t* tri = model->Surface(selectedSurface)->geometry;
    const idDrawVert& sample = tri->verts[tri->indexes[selectedTriangle]];
    printf("WOLF2009_OFFSCREEN_CAMERA vertex_color_material=%s source_triangle_area2=%g "
           "sample_rgba=%u,%u,%u,%u position=(%g,%g,%g) target=(%g,%g,%g)\n", selectedMaterial->GetName(),
           largest, unsigned(sample.color[0]), unsigned(sample.color[1]), unsigned(sample.color[2]), unsigned(sample.color[3]),
           view.vieworg.x, view.vieworg.y, view.vieworg.z, center.x, center.y, center.z);
    return LookAt(view, center);
}
int CheckUploadedTextures(const idRenderModelStatic* model) {
    int bound = 0;
    for (int s = 0; s < model->NumSurfaces(); ++s) {
        const idMaterial* material = model->Surface(s)->shader;
        for (int stage = 0; stage < material->GetNumStages(); ++stage) {
            const idImage* image = material->GetStage(stage)->texture.image;
            if (!image || image->imgName.Find("/txtr/") == -1 || image->defaulted ||
                image->texnum == idImage::TEXTURE_NOT_LOADED || image->bindCount <= 0) continue;
            if (!bound) printf("WOLF2009_OFFSCREEN_TEXTURE source=%s texnum=%u upload=%dx%d binds=%d\n",
                               image->imgName.c_str(), unsigned(image->texnum), image->uploadWidth,
                               image->uploadHeight, image->bindCount);
            ++bound;
        }
    }
    return bound;
}
int ReplaceOnlySourceMaps(idRenderModelStatic* model, const char* label) {
    int replaced = 0;
    for (int s = 0; s < model->surfaces.Num(); ++s) {
        const idMaterial* material = model->surfaces[s].shader;
        char* text = new char[material->GetTextLength() + 1]; material->GetText(text);
        const char* begin = strstr(text, "\"wolf2009/");
        const char* end = begin ? strchr(begin + 1, '"') : NULL;
        if (begin && end && strstr(begin, "/txtr/")) {
            const idStr quoted(begin, 0, int(end - begin + 1));
            idStr native(text); native.Replace(quoted, "_white");
            const idStr name = va("_wolf2009_glqa_flat_%s_%d", label, s);
            idDecl* decl = declManager->CreateNewDecl(DECL_MATERIAL, name, "<wolf2009-glqa>");
            decl->SetText(native); decl->Invalidate();
            model->surfaces[s].shader = declManager->FindMaterial(name);
            ++replaced;
        }
        delete[] text;
    }
    return replaced;
}
int ReplaceOnlyVertexColor(idRenderModelStatic* model, const char* label) {
    int replaced = 0;
    for (int s = 0; s < model->surfaces.Num(); ++s) {
        const idMaterial* material = model->surfaces[s].shader;
        if (!strstr(material->GetName(), "/materials-vertex-color/") ||
            !HasModulatedSourceStage(material)) continue;
        char* text = new char[material->GetTextLength() + 1]; material->GetText(text);
        idStr native(text); delete[] text;
        if (native.Find("\n vertexColor\n") == -1) continue;
        // Keep source DDS, UVs, alpha test, culling and the opaque prepass.
        // Only the generated native modulation stage becomes constant white.
        native.Replace("\n vertexColor\n", "\n");
        const idStr name = va("_wolf2009_glqa_unmodulated_%s_%d", label, s);
        idDecl* decl = declManager->CreateNewDecl(DECL_MATERIAL, name, "<wolf2009-glqa>");
        decl->SetText(native); decl->Invalidate();
        const idMaterial* flat = declManager->FindMaterial(name);
        if (!flat || flat->GetState() == DS_DEFAULTED || HasModulatedSourceStage(flat)) return -1;
        model->surfaces[s].shader = flat;
        ++replaced;
    }
    return replaced;
}
void DepthLessDiagnostic(idRenderModelStatic* model, const char* label, bool twoSided) {
    for (int s = 0; s < model->surfaces.Num(); ++s) {
        const idMaterial* material = model->surfaces[s].shader;
        char* text = new char[material->GetTextLength() + 1]; material->GetText(text);
        const char* body = strchr(text, '{');
        if (body) {
            idStr native("{\n translucent\n");
            if (twoSided) native += "twoSided\n";
            native += body + 1;
            const idStr name = va("_wolf2009_glqa_cull_%s_%d_%d", label, s, int(twoSided));
            idDecl* decl = declManager->CreateNewDecl(DECL_MATERIAL, name, "<wolf2009-glqa>");
            decl->SetText(native); decl->Invalidate();
            model->surfaces[s].shader = declManager->FindMaterial(name);
        }
        delete[] text;
    }
}
void GuiDiagnostic() {
    // Separate renderer isolation control. This is never accepted as world
    // rendering evidence: it only distinguishes common GL state from 3D state.
    Pixels pixels;
    renderSystem->BeginFrame(width, height);
    renderSystem->SetColor(idVec4(1.0f, .1f, .1f, 1.0f));
    renderSystem->DrawStretchPic(80, 60, 480, 360, 0, 0, 1, 1, declManager->FindMaterial("_white"));
    renderSystem->EndFrame(NULL, NULL);
    qglFinish();
    qglReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data);
    int visible = 0;
    for (int i = 0; i < pixelCount; ++i) {
        const byte* a = pixels.data + i * 4;
        if (abs(int(a[0]) - 8) + abs(int(a[1]) - 13) + abs(int(a[2]) - 20) > 12) ++visible;
    }
    const byte* center = pixels.data + (height / 2 * width + width / 2) * 4;
    printf("WOLF2009_OFFSCREEN_GUI_CONTROL nonclear=%d center_rgba=%u,%u,%u,%u gl_error=0x%x\n",
           visible, unsigned(center[0]), unsigned(center[1]), unsigned(center[2]), unsigned(center[3]), unsigned(qglGetError()));
    WriteBmp("fixtures/offscreen-gui-control.bmp", pixels);
}
bool Map(const char* label, const char* package, const char* resource, bool empty) {
    ScopedMap map(label);
    map.mounted = Wolf2009_Mount(label, package);
    if (!Check(map.mounted, "offscreen real MPK mount")) return false;
    const idStr path = va("wolf2009/%s/PROC/%s.proc", label, resource);
    map.world = renderSystem->AllocRenderWorld();
    if (!Check(map.world && map.world->InitFromMap(path), "offscreen normal native world load")) return false;
    map.world->GenerateAllInteractions();
    idStr areaName; Wolf2009_InlineModelName(path, "_area0", areaName);
    idRenderModelStatic* model = dynamic_cast<idRenderModelStatic*>(renderModelManager->CheckModel(areaName));
    if (!Check(model && !model->IsDefaultModel(), "offscreen native area model lookup")) return false;
    renderView_t view; memset(&view, 0, sizeof(view));
    view.viewID = 7; view.width = SCREEN_WIDTH; view.height = SCREEN_HEIGHT;
    view.fov_x = 90; view.fov_y = 90; view.time = 0; view.forceUpdate = true;
    for (int i = 0; i < MAX_GLOBAL_SHADER_PARMS; ++i) view.shaderParms[i] = 1.0f;
    if (empty) {
        view.vieworg.Set(0, -96, 128);
        if (!LookAt(view, idVec3(0, 72, 96))) return false;
    } else if (!CameraForTexture(model, view)) return false;
    Pixels textured, flat;
    if (!Render(map.world, view, textured)) return false;
    const int uploaded = CheckUploadedTextures(model);
    if (!Check(uploaded > 0, "no real TXTR image bound by the native GL backend")) return false;
    RestoreShaders restore(model);
    const int replaced = ReplaceOnlySourceMaps(model, label);
    if (!Check(replaced > 0, "no source stages available for texture contribution control")) return false;
    if (!Render(map.world, view, flat)) return false;
    int changed = 0, texturedPixels = 0, minimum = 255, maximum = 0;
    for (int i = 0; i < pixelCount; ++i) {
        const byte* a = textured.data + i * 4; const byte* b = flat.data + i * 4;
        if (abs(int(a[0]) - 8) + abs(int(a[1]) - 13) + abs(int(a[2]) - 20) > 12) ++changed;
        if (abs(int(a[0]) - int(b[0])) + abs(int(a[1]) - int(b[1])) +
            abs(int(a[2]) - int(b[2])) > 12) ++texturedPixels;
        for (int c = 0; c < 3; ++c) { minimum = Min(minimum, int(a[c])); maximum = Max(maximum, int(a[c])); }
    }
    printf("WOLF2009_OFFSCREEN_MAP map=%s pixels=%d nonclear=%d source_texture_pixels=%d "
           "range=%d..%d uploaded_source_stages=%d controlled_stages=%d\n",
           label, pixelCount, changed, texturedPixels, minimum, maximum, uploaded, replaced);
    if (!changed) {
        restore.Restore();
        DepthLessDiagnostic(model, label, false);
        Pixels sameCull;
        if (Render(map.world, view, sameCull)) {
            int visible = 0;
            for (int i = 0; i < pixelCount; ++i) {
                const byte* a = sameCull.data + i * 4;
                if (abs(int(a[0]) - 8) + abs(int(a[1]) - 13) + abs(int(a[2]) - 20) > 12) ++visible;
            }
            printf("WOLF2009_OFFSCREEN_DEPTH_CONTROL same_cull_depth_less_nonclear=%d\n", visible);
            WriteBmp("fixtures/offscreen-depth-less-control.bmp", sameCull);
        }
        restore.Restore();
        wolf2009ZeroOpaqueAlpha = true;
        Pixels alphaControl;
        if (Render(map.world, view, alphaControl)) {
            int visible = 0;
            for (int i = 0; i < pixelCount; ++i) {
                const byte* a = alphaControl.data + i * 4;
                if (abs(int(a[0]) - 8) + abs(int(a[1]) - 13) + abs(int(a[2]) - 20) > 12) ++visible;
            }
            printf("WOLF2009_OFFSCREEN_ALPHA_CONTROL opaque_alpha_zero_nonclear=%d\n", visible);
            WriteBmp("fixtures/offscreen-alpha-control.bmp", alphaControl);
        }
        wolf2009ZeroOpaqueAlpha = false;
        // Diagnostic only: original geometry, UVs, texture and camera; change
        // the native cull mode and depth-equal prepass requirement, retaining
        // the primary failure. The texture stage and camera remain identical.
        restore.Restore();
        DepthLessDiagnostic(model, label, true);
        Pixels cullControl;
        if (Render(map.world, view, cullControl)) {
            int visible = 0;
            for (int i = 0; i < pixelCount; ++i) {
                const byte* a = cullControl.data + i * 4;
                if (abs(int(a[0]) - 8) + abs(int(a[1]) - 13) + abs(int(a[2]) - 20) > 12) ++visible;
            }
            printf("WOLF2009_OFFSCREEN_CULL_CONTROL map=%s two_sided_depth_less_nonclear=%d\n", label, visible);
            WriteBmp("fixtures/offscreen-cull-control.bmp", cullControl);
        }
        GuiDiagnostic();
    }
    if (!Check(changed > pixelCount / 20 && texturedPixels > 64 && maximum - minimum > 16,
               "frame lacks visible native geometry or real source texture contribution")) return false;
    const char* normalPath = empty ? "fixtures/offscreen-empty.bmp" : "fixtures/offscreen-warehouse.bmp";
    const char* flatPath = empty ? "fixtures/offscreen-empty-flat.bmp" : "fixtures/offscreen-warehouse-flat.bmp";
    if (!WriteBmp(normalPath, textured) || !WriteBmp(flatPath, flat)) return false;
    if (!empty) {
        restore.Restore();
        const int unmodulatedStages = ReplaceOnlyVertexColor(model, label);
        if (!Check(unmodulatedStages > 0, "no modulated source stages for vertex-colour contribution control"))
            return false;
        Pixels unmodulated;
        if (!Render(map.world, view, unmodulated)) return false;
        int colorPixels = 0;
        for (int i = 0; i < pixelCount; ++i) {
            const byte* a = textured.data + i * 4; const byte* b = unmodulated.data + i * 4;
            if (abs(int(a[0]) - int(b[0])) + abs(int(a[1]) - int(b[1])) +
                abs(int(a[2]) - int(b[2])) > 12) ++colorPixels;
        }
        printf("WOLF2009_OFFSCREEN_VERTEX_COLOR map=%s source_color_pixels=%d controlled_stages=%d\n",
               label, colorPixels, unmodulatedStages);
        if (!WriteBmp("fixtures/offscreen-warehouse-unmodulated.bmp", unmodulated)) return false;
        if (!Check(colorPixels > 64, "frame lacks visible native source vertex-colour contribution")) return false;
    }
    return true;
}
}
bool Wolf2009_RenderQaScene(idRenderWorld* world, const renderView_s& view, unsigned char* rgba, int bytes) {
    if (!Check(world && rgba && bytes == Wolf2009_QAFrameBytes, "invalid shared QA frame request")) return false;
    Pixels pixels;
    if (!Render(world, view, pixels)) return false;
    memcpy(rgba, pixels.data, bytes);
    return true;
}
bool Wolf2009_WriteQaFrame(const char* path, const unsigned char* rgba, int bytes) {
    if (!Check(path && rgba && bytes == Wolf2009_QAFrameBytes, "invalid shared QA frame output")) return false;
    Pixels pixels; memcpy(pixels.data, rgba, bytes);
    return WriteBmp(path, pixels);
}
bool Wolf2009_RunOffscreenChecks() {
    printf("WOLF2009_OFFSCREEN_SCOPE actual_native_GLES3_renderer EGL_pbuffer no_native_window no_input\n");
    if (!Check(common->IsInitialized() && !renderSystem->IsOpenGLRunning(), "offscreen common/GL entry state"))
        return false;
#ifdef _MULTITHREAD
    if (!Check(!multithreadActive, "offscreen host must set r_multithread=0 before common Init")) return false;
#endif
#ifdef _OPENGLES3
    if (!Check(USING_GLES3, "offscreen host must select GLES3 before common Init")) return false;
#endif
    if (!Check(GLimp_Wolf2009EnablePbuffer(width, height), "explicit pbuffer request")) return false;
    cvarSystem->SetCVarInteger("r_mode", -1);
    cvarSystem->SetCVarInteger("r_customWidth", width); cvarSystem->SetCVarInteger("r_customHeight", height);
    cvarSystem->SetCVarInteger("r_multithread", 0);
    cvarSystem->SetCVarInteger("r_swapInterval", 0);
    // The offscreen surface has no presentation target. Keep the native
    // EndFrame pipeline and read its current buffer before any EGL swap.
    cvarSystem->SetCVarInteger("r_frontBuffer", 1);
    cvarSystem->SetCVarInteger("r_multiSamples", 0);
    cvarSystem->SetCVarInteger("r_useShadowMapping", 0);
    cvarSystem->SetCVarInteger("r_skipPostProcess", 1);
    cvarSystem->SetCVarInteger("image_downSize", 0);
    cvarSystem->SetCVarInteger("image_usePrecompressedTextures", 0);
    cvarSystem->SetCVarInteger("image_useCompression", 0);
    cvarSystem->SetCVarInteger("r_ignoreGLErrors", 0);
    cvarSystem->SetCVarInteger("r_debugRenderToTexture", 1);
    cvarSystem->SetCVarString("r_clear", "0.03 0.05 0.08");
    try { renderSystem->InitOpenGL(); }
    catch (idException&) { DiagnoseFramebuffer(); throw; }
    if (!Check(renderSystem->IsOpenGLRunning() && renderSystem->GetScreenWidth() == width &&
               renderSystem->GetScreenHeight() == height, "native GLES initialization/resolution")) return false;
    printf("WOLF2009_OFFSCREEN_GL renderer=%s version=%s size=%dx%d\n",
           glConfig.renderer_string, glConfig.version_string, glConfig.vidWidth, glConfig.vidHeight);
    // Exercise the production attachment compatibility fix in both directions
    // through the real auxiliary renderer, without replacing its framebuffer.
    depthStencilRenderer.BeginRender();
    const GLenum renderbufferStatus = qglCheckFramebufferStatus(GL_FRAMEBUFFER);
    depthStencilRenderer.End();
    depthStencilRenderer.BeginBlit();
    const GLenum textureStatus = qglCheckFramebufferStatus(GL_FRAMEBUFFER);
    depthStencilRenderer.End();
    const GLenum attachmentError = qglGetError();
    printf("WOLF2009_OFFSCREEN_FBO_ROUNDTRIP renderbuffer=0x%x texture=0x%x error=0x%x\n",
           unsigned(renderbufferStatus), unsigned(textureStatus), unsigned(attachmentError));
    if (!Check(renderbufferStatus == GL_FRAMEBUFFER_COMPLETE && textureStatus == GL_FRAMEBUFFER_COMPLETE &&
               attachmentError == GL_NO_ERROR, "native packed depth/stencil attachment roundtrip")) return false;
    Wolf2009_ClearRenderCatalog();
    idCmdArgs materialArgs;
    materialArgs.AppendArg("wolf2009Materials"); materialArgs.AppendArg("fixtures/wolf-materials.bin");
    cmdSystem->BufferCommandArgs(CMD_EXEC_NOW, materialArgs);
    if (!Map("glqaempty", "maps/game/empty/empty.mpk", "maps/game/empty/empty", true)) return false;
    if (!Map("glqaware", "maps/game/mtw/mtw_ware.mpk", "maps/game/mtw/mtw_ware", false)) return false;
#ifdef WOLF2009_SKINNED_QA
    idFile* skinnedRequest = fileSystem->OpenFileRead("fixtures/skinned-checks.enabled", false);
    if (skinnedRequest) {
        fileSystem->CloseFile(skinnedRequest);
        if (!Wolf2009_RunSkinnedOffscreenChecks()) return false;
    }
#endif
#ifdef WOLF2009_SOURCE_WEAPON_VIEW_QA
    if (!Wolf2009_RunSourceWeaponViewCoreChecks()) return false;
#endif
    Wolf2009_ClearRenderCatalog();
    // The normal common::Shutdown owns renderer images, shaders, FBOs, frame
    // memory and GLimp teardown. Keep the context valid until that lifecycle.
    printf("WOLF2009_ACTUAL_ENGINE_OFFSCREEN_QA_OK renderer_live_for_common_shutdown=1\n");
    return true;
}

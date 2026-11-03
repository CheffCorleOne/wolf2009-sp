// SPDX-License-Identifier: GPL-3.0-or-later
// Asset-free check of the depth/stencil attachment transition used by idTech4A++.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstdlib>

namespace {
struct Lifetime {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint framebuffer = 0, color = 0, depth = 0, texture = 0;
    bool initialized = false, current = false;
    ~Lifetime() {
        if (current) {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteRenderbuffers(1, &color);
            glDeleteRenderbuffers(1, &depth);
            glDeleteTextures(1, &texture);
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(display, surface);
        if (initialized)
            eglTerminate(display);
    }
};
int Fail(const char *message) {
    std::fprintf(stderr, "WOLF2009_EGL_FAIL %s egl_error=0x%x gl_error=0x%x\n", message,
                 unsigned(eglGetError()), unsigned(glGetError()));
    return 1;
}
bool CompleteAndReadable(int iteration) {
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
        return false;
    const float red = iteration % 2 ? .75f : .25f;
    glClearColor(red, .5f, .25f, 1.f);
    glClearDepthf(1.f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    GLubyte pixel[4] = {};
    glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return glGetError() == GL_NO_ERROR && std::abs(int(pixel[0]) - (iteration % 2 ? 191 : 64)) <= 1 &&
           std::abs(int(pixel[1]) - 128) <= 1 && std::abs(int(pixel[2]) - 64) <= 1 && pixel[3] == 255;
}
void AttachTexture(GLuint texture) {
    // Explicitly clear both old attachment kinds before attaching the new one.
    // Combined replacement leaves stale state in the tested emulator translator.
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
}
void AttachRenderbuffer(GLuint depth) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
}
} // namespace

int main() {
    Lifetime lifetime;
    lifetime.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0, count = 0;
    if (!eglInitialize(lifetime.display, &major, &minor))
        return Fail("eglInitialize");
    lifetime.initialized = true;
    const EGLint attributes[] = {EGL_SURFACE_TYPE,
                                 EGL_PBUFFER_BIT,
                                 EGL_RENDERABLE_TYPE,
                                 EGL_OPENGL_ES3_BIT_KHR,
                                 EGL_RED_SIZE,
                                 8,
                                 EGL_GREEN_SIZE,
                                 8,
                                 EGL_BLUE_SIZE,
                                 8,
                                 EGL_ALPHA_SIZE,
                                 8,
                                 EGL_NONE};
    EGLConfig config;
    if (!eglChooseConfig(lifetime.display, attributes, &config, 1, &count) || count != 1)
        return Fail("no GLES3 pbuffer configuration");
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    lifetime.context = eglCreateContext(lifetime.display, config, EGL_NO_CONTEXT, contextAttributes);
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 128, EGL_HEIGHT, 128, EGL_NONE};
    lifetime.surface = eglCreatePbufferSurface(lifetime.display, config, surfaceAttributes);
    if (lifetime.context == EGL_NO_CONTEXT || lifetime.surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(lifetime.display, lifetime.surface, lifetime.surface, lifetime.context))
        return Fail("GLES3 context");
    lifetime.current = true;
    std::printf("WOLF2009_EGL_CONTEXT version=%s renderer=%s\n", glGetString(GL_VERSION),
                glGetString(GL_RENDERER));

    glGenFramebuffers(1, &lifetime.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, lifetime.framebuffer);
    glGenRenderbuffers(1, &lifetime.color);
    glBindRenderbuffer(GL_RENDERBUFFER, lifetime.color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 128, 128);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, lifetime.color);
    glGenRenderbuffers(1, &lifetime.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, lifetime.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 128, 128);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              lifetime.depth);
    if (!CompleteAndReadable(0))
        return Fail("initial packed renderbuffer");
    glGenTextures(1, &lifetime.texture);
    glBindTexture(GL_TEXTURE_2D, lifetime.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 128, 128, 0, GL_DEPTH_STENCIL,
                 GL_UNSIGNED_INT_24_8, NULL);
    if (glGetError() != GL_NO_ERROR)
        return Fail("packed texture allocation");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, lifetime.texture,
                           0);
    // Direct replacement is diagnostic only. Conforming drivers may support it.
    std::printf("WOLF2009_EGL_DIRECT_REPLACEMENT status=0x%x\n",
                unsigned(glCheckFramebufferStatus(GL_FRAMEBUFFER)));
    if (glGetError() != GL_NO_ERROR)
        return Fail("direct replacement generated GL error");
    for (int i = 0; i < 8; ++i) {
        AttachTexture(lifetime.texture);
        if (!CompleteAndReadable(i))
            return Fail("renderbuffer-to-texture transition/readback");
        AttachRenderbuffer(lifetime.depth);
        if (!CompleteAndReadable(i + 1))
            return Fail("texture-to-renderbuffer transition/readback");
    }
    std::printf("WOLF2009_EGL_ATTACHMENTS_QA_OK cycles=8 readbacks=17\n");
    return 0;
}

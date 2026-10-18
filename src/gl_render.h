// Windows-only (WGL) for now. The EGL/Linux path was removed to keep the binary small.
#pragma once
#ifndef GL_RENDER_H
#define GL_RENDER_H

#include "libretro.h"
#include <string.h>
#include <windows.h>
#include <GL/gl.h>

#ifndef GL_FRAMEBUFFER
#  define GL_FRAMEBUFFER          0x8D40
#endif
#ifndef GL_RENDERBUFFER
#  define GL_RENDERBUFFER         0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#  define GL_COLOR_ATTACHMENT0    0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#  define GL_DEPTH_ATTACHMENT     0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#  define GL_STENCIL_ATTACHMENT   0x8D20
#endif
#ifndef GL_DEPTH24_STENCIL8
#  define GL_DEPTH24_STENCIL8     0x88F0
#endif
#ifndef GL_DEPTH_COMPONENT24
#  define GL_DEPTH_COMPONENT24    0x81A6
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#  define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#  define GL_RGBA8                0x8058
#endif
#ifndef GL_BGRA
#  define GL_BGRA                 0x80E1
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#  define GL_PIXEL_PACK_BUFFER    0x88EB
#endif
#ifndef GL_STREAM_READ
#  define GL_STREAM_READ          0x88E1
#endif
#ifndef GL_MAP_READ_BIT
#  define GL_MAP_READ_BIT         0x0001
#endif
#ifndef GL_READ_FRAMEBUFFER
#  define GL_READ_FRAMEBUFFER     0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#  define GL_DRAW_FRAMEBUFFER     0x8CA9
#endif
#ifndef GL_COLOR_BUFFER_BIT
#  define GL_COLOR_BUFFER_BIT     0x00004000
#endif
#ifndef GL_NEAREST
#  define GL_NEAREST              0x2600
#endif

// Function pointers loaded as one contiguous table: keep order in sync with kGlNames in gl_render.cpp.
struct GLFuncs {
    void      (APIENTRY *Viewport)(GLint, GLint, GLsizei, GLsizei);
    void      (APIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void      (APIENTRY *Flush)(void);
    void      (APIENTRY *GenFramebuffers)(GLsizei, GLuint*);
    void      (APIENTRY *BindFramebuffer)(GLenum, GLuint);
    void      (APIENTRY *GenRenderbuffers)(GLsizei, GLuint*);
    void      (APIENTRY *BindRenderbuffer)(GLenum, GLuint);
    void      (APIENTRY *RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
    void      (APIENTRY *FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
    GLenum    (APIENTRY *CheckFramebufferStatus)(GLenum);
    void      (APIENTRY *DeleteFramebuffers)(GLsizei, const GLuint*);
    void      (APIENTRY *DeleteRenderbuffers)(GLsizei, const GLuint*);
    void      (APIENTRY *BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    void      (APIENTRY *GenBuffers)(GLsizei, GLuint*);
    void      (APIENTRY *DeleteBuffers)(GLsizei, const GLuint*);
    void      (APIENTRY *BindBuffer)(GLenum, GLuint);
    void      (APIENTRY *BufferData)(GLenum, ptrdiff_t, const void*, GLenum);
    void*     (APIENTRY *MapBufferRange)(GLenum, ptrdiff_t, ptrdiff_t, GLbitfield);
    GLboolean (APIENTRY *UnmapBuffer)(GLenum);
};

// Keep order in sync with kWglNames in gl_render.cpp.
struct WglFuncs {
    HGLRC (WINAPI *CreateContext)(HDC);
    BOOL  (WINAPI *MakeCurrent)(HDC, HGLRC);
    BOOL  (WINAPI *DeleteContext)(HGLRC);
    int   (WINAPI *ChoosePixelFormat_)(HDC, const PIXELFORMATDESCRIPTOR*);
    BOOL  (WINAPI *SetPixelFormat_)(HDC, int, const PIXELFORMATDESCRIPTOR*);
};

struct GLRenderContext {
    HWND  hwnd  = nullptr;
    HDC   hdc   = nullptr;
    HGLRC hglrc = nullptr;
    WglFuncs wgl_fn = {};
    GLFuncs  gl_fn  = {};

    GLuint fbo = 0, color_rb = 0, depth_rb = 0;
    GLuint fbo_blit = 0, color_rb_blit = 0;
    unsigned fbo_w = 0, fbo_h = 0;

    GLuint pbo[3] = {0, 0, 0};
    unsigned pbo_w = 0, pbo_h = 0;
    int pbo_idx = 0;
    bool pbo_valid[3] = {false, false, false};

    retro_hw_render_callback hw_render = {};
    bool active = false;
    bool needs_context_reset = false;

    bool createContext();
    void destroyContext();
    void makeCurrent();

    bool createFbo(unsigned w, unsigned h);
    void destroyFbo();
    void bindForCoreRender();
    bool readPixelsXRGB8888(uint8_t* out, unsigned w, unsigned h, unsigned out_w = 0, unsigned out_h = 0);
    uintptr_t getCurrentFramebuffer() const { return (uintptr_t)fbo; }

    static void* getProcAddress(const char* sym);
};

#endif
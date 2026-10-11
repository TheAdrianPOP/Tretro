#pragma once
#ifndef GL_RENDER_H
#define GL_RENDER_H

#include "libretro.h"

extern void tretro_log(const char* fmt, ...);

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <GL/gl.h>
#else
#  include <dlfcn.h>
#  include <EGL/egl.h>
#  include <EGL/eglext.h>
#  include <GLES3/gl3.h>
#endif

#ifndef GR_APIENTRY
#  if defined(_WIN32)
#    define GR_APIENTRY APIENTRY
#  else
#    define GR_APIENTRY
#  endif
#endif

typedef ptrdiff_t GR_GLintptr;
typedef ptrdiff_t GR_GLsizeiptr;

#ifndef GL_FRAMEBUFFER
#  define GL_FRAMEBUFFER               0x8D40
#  define GL_RENDERBUFFER              0x8D41
#  define GL_COLOR_ATTACHMENT0         0x8CE0
#  define GL_DEPTH_ATTACHMENT          0x8D00
#  define GL_STENCIL_ATTACHMENT        0x8D20
#  define GL_DEPTH24_STENCIL8          0x88F0
#  define GL_DEPTH_COMPONENT24         0x81A6
#  define GL_FRAMEBUFFER_COMPLETE      0x8CD5
#  define GL_CLAMP_TO_EDGE             0x812F
#  define GL_RGBA8                     0x8058
#endif

#ifndef GL_BGRA
#  define GL_BGRA                      0x80E1
#endif
#ifndef GL_DEPTH32F_STENCIL8
#  define GL_DEPTH32F_STENCIL8         0x8CAD
#endif
#ifndef GL_DEPTH_COMPONENT32F
#  define GL_DEPTH_COMPONENT32F        0x8CAC
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#  define GL_PIXEL_PACK_BUFFER         0x88EB
#  define GL_STREAM_READ               0x88E1
#  define GL_MAP_READ_BIT              0x0001
#endif
#ifndef GL_READ_FRAMEBUFFER
#  define GL_READ_FRAMEBUFFER          0x8CA8
#  define GL_DRAW_FRAMEBUFFER          0x8CA9
#  define GL_COLOR_BUFFER_BIT          0x00004000
#  define GL_NEAREST                   0x2600
#endif

typedef void* (*GLGetProcAddressFn)(const char* sym);

typedef void          (GR_APIENTRY *PFNGLGENTEXTURESPROC_GR)(GLsizei, GLuint*);
typedef void          (GR_APIENTRY *PFNGLBINDTEXTUREPROC_GR)(GLenum, GLuint);
typedef void          (GR_APIENTRY *PFNGLTEXIMAGE2DPROC_GR)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void          (GR_APIENTRY *PFNGLTEXPARAMETERIPROC_GR)(GLenum, GLenum, GLint);
typedef void          (GR_APIENTRY *PFNGLDELETETEXTURESPROC_GR)(GLsizei, const GLuint*);
typedef void          (GR_APIENTRY *PFNGLVIEWPORTPROC_GR)(GLint, GLint, GLsizei, GLsizei);
typedef void          (GR_APIENTRY *PFNGLPIXELSTOREIPROC_GR)(GLenum, GLint);
typedef void          (GR_APIENTRY *PFNGLREADPIXELSPROC_GR)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void          (GR_APIENTRY *PFNGLFLUSHPROC_GR)(void);
typedef const GLubyte*(GR_APIENTRY *PFNGLGETSTRINGPROC_GR)(GLenum);

typedef void   (GR_APIENTRY *PFNGLGENFRAMEBUFFERSPROC_GR)(GLsizei, GLuint*);
typedef void   (GR_APIENTRY *PFNGLBINDFRAMEBUFFERPROC_GR)(GLenum, GLuint);
typedef void   (GR_APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC_GR)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void   (GR_APIENTRY *PFNGLGENRENDERBUFFERSPROC_GR)(GLsizei, GLuint*);
typedef void   (GR_APIENTRY *PFNGLBINDRENDERBUFFERPROC_GR)(GLenum, GLuint);
typedef void   (GR_APIENTRY *PFNGLRENDERBUFFERSTORAGEPROC_GR)(GLenum, GLenum, GLsizei, GLsizei);
typedef void   (GR_APIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC_GR)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (GR_APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC_GR)(GLenum);
typedef void   (GR_APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC_GR)(GLsizei, const GLuint*);
typedef void   (GR_APIENTRY *PFNGLDELETERENDERBUFFERSPROC_GR)(GLsizei, const GLuint*);
typedef void   (GR_APIENTRY *PFNGLBLITFRAMEBUFFERPROC_GR)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

typedef void   (GR_APIENTRY *PFNGLGENBUFFERSPROC_GR)(GLsizei, GLuint*);
typedef void   (GR_APIENTRY *PFNGLDELETEBUFFERSPROC_GR)(GLsizei, const GLuint*);
typedef void   (GR_APIENTRY *PFNGLBINDBUFFERPROC_GR)(GLenum, GLuint);
typedef void   (GR_APIENTRY *PFNGLBUFFERDATAPROC_GR)(GLenum, GR_GLsizeiptr, const void*, GLenum);
typedef void*  (GR_APIENTRY *PFNGLMAPBUFFERRANGEPROC_GR)(GLenum, GR_GLintptr, GR_GLsizeiptr, GLbitfield);
typedef GLboolean (GR_APIENTRY *PFNGLUNMAPBUFFERPROC_GR)(GLenum);

#if defined(_WIN32)
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTPROC_GR)(HDC);
typedef BOOL  (WINAPI *PFNWGLMAKECURRENTPROC_GR)(HDC, HGLRC);
typedef BOOL  (WINAPI *PFNWGLDELETECONTEXTPROC_GR)(HGLRC);
typedef int   (WINAPI *PFNCHOOSEPIXELFORMATPROC_GR)(HDC, const PIXELFORMATDESCRIPTOR*);
typedef BOOL  (WINAPI *PFNSETPIXELFORMATPROC_GR)(HDC, int, const PIXELFORMATDESCRIPTOR*);
#else
typedef void*       (*PFNEGLGETPROCADDRESSPROC_GR)(const char*);
typedef EGLBoolean (*PFNEGLBINDAPIPROC_GR)(EGLenum);
typedef EGLBoolean (*PFNEGLCHOOSECONFIGPROC_GR)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLSurface (*PFNEGLCREATEPBUFFERSURFACEPROC_GR)(EGLDisplay, EGLConfig, const EGLint*);
typedef EGLContext (*PFNEGLCREATECONTEXTPROC_GR)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLBoolean (*PFNEGLMAKECURRENTPROC_GR)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*PFNEGLINITIALIZEPROC_GR)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*PFNEGLDESTROYCONTEXTPROC_GR)(EGLDisplay, EGLContext);
typedef EGLBoolean (*PFNEGLDESTROYSURFACEPROC_GR)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*PFNEGLTERMINATEPROC_GR)(EGLDisplay);
typedef const char*(*PFNEGLQUERYSTRINGPROC_GR)(EGLDisplay, EGLint);
typedef EGLDisplay (*PFNEGLGETDISPLAYPROC_GR)(EGLNativeDisplayType);
typedef EGLint     (*PFNEGLGETERRORPROC_GR)(void);
#endif

struct GLCoreFunctions {
    PFNGLGENTEXTURESPROC_GR    GenTextures    = nullptr;
    PFNGLBINDTEXTUREPROC_GR   BindTexture    = nullptr;
    PFNGLTEXIMAGE2DPROC_GR    TexImage2D     = nullptr;
    PFNGLTEXPARAMETERIPROC_GR TexParameteri = nullptr;
    PFNGLDELETETEXTURESPROC_GR DeleteTextures = nullptr;
    PFNGLVIEWPORTPROC_GR       Viewport       = nullptr;
    PFNGLPIXELSTOREIPROC_GR    PixelStorei    = nullptr;
    PFNGLREADPIXELSPROC_GR     ReadPixels     = nullptr;
    PFNGLFLUSHPROC_GR          Flush          = nullptr;
    PFNGLGETSTRINGPROC_GR      GetString      = nullptr;

    bool load(GLGetProcAddressFn getProc);
};

#if defined(_WIN32)
struct WglFunctions {
    PFNWGLCREATECONTEXTPROC_GR   CreateContext      = nullptr;
    PFNWGLMAKECURRENTPROC_GR     MakeCurrent        = nullptr;
    PFNWGLDELETECONTEXTPROC_GR   DeleteContext      = nullptr;
    PFNCHOOSEPIXELFORMATPROC_GR  ChoosePixelFormat_ = nullptr;
    PFNSETPIXELFORMATPROC_GR     SetPixelFormat_    = nullptr;

    bool load(GLGetProcAddressFn getProc);
};
#else
struct EglFunctions {
    PFNEGLGETPROCADDRESSPROC_GR       GetProcAddress       = nullptr;
    PFNEGLBINDAPIPROC_GR              BindAPI              = nullptr;
    PFNEGLCHOOSECONFIGPROC_GR         ChooseConfig         = nullptr;
    PFNEGLCREATEPBUFFERSURFACEPROC_GR CreatePbufferSurface = nullptr;
    PFNEGLCREATECONTEXTPROC_GR        CreateContext        = nullptr;
    PFNEGLMAKECURRENTPROC_GR          MakeCurrent          = nullptr;
    PFNEGLINITIALIZEPROC_GR           Initialize           = nullptr;
    PFNEGLDESTROYCONTEXTPROC_GR       DestroyContext       = nullptr;
    PFNEGLDESTROYSURFACEPROC_GR       DestroySurface       = nullptr;
    PFNEGLTERMINATEPROC_GR            Terminate            = nullptr;
    PFNEGLQUERYSTRINGPROC_GR          QueryString          = nullptr;
    PFNEGLGETDISPLAYPROC_GR           GetDisplay           = nullptr;
    PFNEGLGETERRORPROC_GR             GetError             = nullptr;

    bool load(GLGetProcAddressFn getProc);
};
#endif

struct FboFunctions {
    PFNGLGENFRAMEBUFFERSPROC_GR          GenFramebuffers          = nullptr;
    PFNGLBINDFRAMEBUFFERPROC_GR          BindFramebuffer          = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC_GR     FramebufferTexture2D     = nullptr;
    PFNGLGENRENDERBUFFERSPROC_GR         GenRenderbuffers         = nullptr;
    PFNGLBINDRENDERBUFFERPROC_GR         BindRenderbuffer         = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC_GR      RenderbufferStorage      = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC_GR  FramebufferRenderbuffer  = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC_GR   CheckFramebufferStatus   = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC_GR       DeleteFramebuffers       = nullptr;
    PFNGLDELETERENDERBUFFERSPROC_GR      DeleteRenderbuffers      = nullptr;
    PFNGLBLITFRAMEBUFFERPROC_GR          BlitFramebuffer          = nullptr;

    PFNGLGENBUFFERSPROC_GR               GenBuffers               = nullptr;
    PFNGLDELETEBUFFERSPROC_GR            DeleteBuffers            = nullptr;
    PFNGLBINDBUFFERPROC_GR               BindBuffer               = nullptr;
    PFNGLBUFFERDATAPROC_GR               BufferData               = nullptr;
    PFNGLMAPBUFFERRANGEPROC_GR           MapBufferRange           = nullptr;
    PFNGLUNMAPBUFFERPROC_GR              UnmapBuffer              = nullptr;

    bool has_pbo = false;

    bool load(GLGetProcAddressFn getProc);
};

struct GLRenderContext {
#if defined(_WIN32)
    HWND  hwnd  = nullptr;
    HDC   hdc   = nullptr;
    HGLRC hglrc = nullptr;
    WglFunctions wgl_fn;
#else
    EGLDisplay egl_dpy  = EGL_NO_DISPLAY;
    EGLContext egl_ctx  = EGL_NO_CONTEXT;
    EGLSurface egl_surf = EGL_NO_SURFACE;
    bool       using_gles = false;
    EglFunctions egl_fn;
#endif

    GLCoreFunctions gl_fn;
    FboFunctions fbo_fn;

    GLuint fbo       = 0;
    GLuint color_tex = 0;
    GLuint depth_rb  = 0;
    unsigned fbo_w = 0, fbo_h = 0;

    GLuint fbo_blit       = 0;
    GLuint color_tex_blit = 0;

    GLuint pbo[3] = {0, 0, 0};
    unsigned pbo_w = 0, pbo_h = 0;
    int pbo_idx = 0;
    bool pbo_valid[3] = {false, false, false};

    retro_hw_render_callback hw_render = {};
    bool active = false;
    bool needs_context_reset = false;

    uint8_t* scratch_buf = nullptr;
    size_t scratch_buf_cap = 0;

    ~GLRenderContext();

    bool wantsDesktopGL() const;
    bool createContext();
    void destroyContext();
    void makeCurrent();

#if !defined(_WIN32)
    bool acquireDisplay();
#endif

    bool createFbo(unsigned w, unsigned h);
    void destroyFbo();
    void bindForCoreRender();
    void processPixels(const void* __restrict src, uint8_t* __restrict out, unsigned w, unsigned h, bool desktop, bool flip_y);
    bool readPixelsXRGB8888(uint8_t* out, unsigned w, unsigned h, unsigned out_w = 0, unsigned out_h = 0);
    uintptr_t getCurrentFramebuffer() const;

    static void* getProcAddress(const char* sym);
};

#endif 
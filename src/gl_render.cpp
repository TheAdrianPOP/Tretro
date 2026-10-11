#include "gl_render.h"


bool GLCoreFunctions::load(GLGetProcAddressFn getProc) {
    GenTextures    = (PFNGLGENTEXTURESPROC_GR)   getProc("glGenTextures");
    BindTexture    = (PFNGLBINDTEXTUREPROC_GR)  getProc("glBindTexture");
    TexImage2D     = (PFNGLTEXIMAGE2DPROC_GR)   getProc("glTexImage2D");
    TexParameteri = (PFNGLTEXPARAMETERIPROC_GR)getProc("glTexParameteri");
    DeleteTextures = (PFNGLDELETETEXTURESPROC_GR)getProc("glDeleteTextures");
    Viewport       = (PFNGLVIEWPORTPROC_GR)      getProc("glViewport");
    PixelStorei    = (PFNGLPIXELSTOREIPROC_GR)   getProc("glPixelStorei");
    ReadPixels     = (PFNGLREADPIXELSPROC_GR)    getProc("glReadPixels");
    Flush          = (PFNGLFLUSHPROC_GR)         getProc("glFlush");
    GetString      = (PFNGLGETSTRINGPROC_GR)     getProc("glGetString");

    return GenTextures && BindTexture && TexImage2D && TexParameteri &&
           DeleteTextures && Viewport && PixelStorei && ReadPixels &&
           Flush && GetString;
}

#if defined(_WIN32)
bool WglFunctions::load(GLGetProcAddressFn getProc) {
    CreateContext      = (PFNWGLCREATECONTEXTPROC_GR)  getProc("wglCreateContext");
    MakeCurrent        = (PFNWGLMAKECURRENTPROC_GR)    getProc("wglMakeCurrent");
    DeleteContext      = (PFNWGLDELETECONTEXTPROC_GR)  getProc("wglDeleteContext");
    ChoosePixelFormat_ = (PFNCHOOSEPIXELFORMATPROC_GR) getProc("ChoosePixelFormat");
    SetPixelFormat_    = (PFNSETPIXELFORMATPROC_GR)    getProc("SetPixelFormat");
    return CreateContext && MakeCurrent && DeleteContext && ChoosePixelFormat_ && SetPixelFormat_;
}
#else
bool EglFunctions::load(GLGetProcAddressFn getProc) {
    GetProcAddress       = (PFNEGLGETPROCADDRESSPROC_GR)      getProc("eglGetProcAddress");
    BindAPI              = (PFNEGLBINDAPIPROC_GR)             getProc("eglBindAPI");
    ChooseConfig         = (PFNEGLCHOOSECONFIGPROC_GR)        getProc("eglChooseConfig");
    CreatePbufferSurface = (PFNEGLCREATEPBUFFERSURFACEPROC_GR)getProc("eglCreatePbufferSurface");
    CreateContext        = (PFNEGLCREATECONTEXTPROC_GR)       getProc("eglCreateContext");
    MakeCurrent          = (PFNEGLMAKECURRENTPROC_GR)         getProc("eglMakeCurrent");
    Initialize           = (PFNEGLINITIALIZEPROC_GR)          getProc("eglInitialize");
    DestroyContext       = (PFNEGLDESTROYCONTEXTPROC_GR)      getProc("eglDestroyContext");
    DestroySurface       = (PFNEGLDESTROYSURFACEPROC_GR)      getProc("eglDestroySurface");
    Terminate            = (PFNEGLTERMINATEPROC_GR)           getProc("eglTerminate");
    QueryString          = (PFNEGLQUERYSTRINGPROC_GR)         getProc("eglQueryString");
    GetDisplay           = (PFNEGLGETDISPLAYPROC_GR)          getProc("eglGetDisplay");
    GetError             = (PFNEGLGETERRORPROC_GR)            getProc("eglGetError");

    return GetProcAddress && BindAPI && ChooseConfig && CreatePbufferSurface &&
           CreateContext && MakeCurrent && Initialize && DestroyContext &&
           DestroySurface && Terminate && QueryString && GetDisplay && GetError;
}
#endif

bool FboFunctions::load(GLGetProcAddressFn getProc) {
    GenFramebuffers         = (PFNGLGENFRAMEBUFFERSPROC_GR)        getProc("glGenFramebuffers");
    BindFramebuffer         = (PFNGLBINDFRAMEBUFFERPROC_GR)        getProc("glBindFramebuffer");
    FramebufferTexture2D    = (PFNGLFRAMEBUFFERTEXTURE2DPROC_GR)   getProc("glFramebufferTexture2D");
    GenRenderbuffers        = (PFNGLGENRENDERBUFFERSPROC_GR)       getProc("glGenRenderbuffers");
    BindRenderbuffer        = (PFNGLBINDRENDERBUFFERPROC_GR)       getProc("glBindRenderbuffer");
    RenderbufferStorage     = (PFNGLRENDERBUFFERSTORAGEPROC_GR)    getProc("glRenderbufferStorage");
    FramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC_GR)getProc("glFramebufferRenderbuffer");
    CheckFramebufferStatus  = (PFNGLCHECKFRAMEBUFFERSTATUSPROC_GR) getProc("glCheckFramebufferStatus");
    DeleteFramebuffers      = (PFNGLDELETEFRAMEBUFFERSPROC_GR)     getProc("glDeleteFramebuffers");
    DeleteRenderbuffers     = (PFNGLDELETERENDERBUFFERSPROC_GR)    getProc("glDeleteRenderbuffers");
    BlitFramebuffer         = (PFNGLBLITFRAMEBUFFERPROC_GR)         getProc("glBlitFramebuffer");

    GenBuffers              = (PFNGLGENBUFFERSPROC_GR)             getProc("glGenBuffers");
    DeleteBuffers           = (PFNGLDELETEBUFFERSPROC_GR)          getProc("glDeleteBuffers");
    BindBuffer              = (PFNGLBINDBUFFERPROC_GR)             getProc("glBindBuffer");
    BufferData              = (PFNGLBUFFERDATAPROC_GR)             getProc("glBufferData");
    MapBufferRange          = (PFNGLMAPBUFFERRANGEPROC_GR)         getProc("glMapBufferRange");
    UnmapBuffer             = (PFNGLUNMAPBUFFERPROC_GR)            getProc("glUnmapBuffer");

    has_pbo = GenBuffers && DeleteBuffers && BindBuffer && BufferData && MapBufferRange && UnmapBuffer;

    return GenFramebuffers && BindFramebuffer && FramebufferTexture2D &&
           GenRenderbuffers && BindRenderbuffer && RenderbufferStorage &&
           FramebufferRenderbuffer && CheckFramebufferStatus &&
           DeleteFramebuffers && DeleteRenderbuffers;
}


GLRenderContext::~GLRenderContext() {
    delete[] scratch_buf;
}

bool GLRenderContext::wantsDesktopGL() const {
    return hw_render.context_type == RETRO_HW_CONTEXT_OPENGL ||
           hw_render.context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
}

uintptr_t GLRenderContext::getCurrentFramebuffer() const {
    return (uintptr_t)fbo;
}

#if defined(_WIN32)

bool GLRenderContext::createContext() {
    if (!wgl_fn.load(getProcAddress)) {
        tretro_log("[ERR] [gl] required WGL/GDI entry points missing");
        return false;
    }

    static const char* kClassName = "tretro_gl_hidden_wnd";
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(0, kClassName, "tretro-gl", WS_DISABLED,
                           0, 0, 1, 1, nullptr, nullptr,
                           wc.hInstance, nullptr);
    if (!hwnd) {
        tretro_log("[ERR] [gl] CreateWindowExA failed (%lu)", GetLastError());
        return false;
    }

    hdc = GetDC(hwnd);
    if (!hdc) {
        tretro_log("[ERR] [gl] GetDC failed");
        destroyContext();
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;

    int fmt = wgl_fn.ChoosePixelFormat_(hdc, &pfd);
    if (!fmt || !wgl_fn.SetPixelFormat_(hdc, fmt, &pfd)) {
        tretro_log("[ERR] [gl] SetPixelFormat failed");
        destroyContext();
        return false;
    }

    HGLRC legacy_ctx = wgl_fn.CreateContext(hdc);
    if (!legacy_ctx || !wgl_fn.MakeCurrent(hdc, legacy_ctx)) {
        tretro_log("[ERR] [gl] wglCreateContext (legacy) failed");
        destroyContext();
        return false;
    }

    typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC_GR)(HDC, HGLRC, const int*);
    auto wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC_GR)getProcAddress("wglCreateContextAttribsARB");

    unsigned want_major = hw_render.version_major ? hw_render.version_major : 3;
    unsigned want_minor = hw_render.version_major ? hw_render.version_minor : 3;
    bool want_core = (hw_render.context_type == RETRO_HW_CONTEXT_OPENGL_CORE);

    if (wglCreateContextAttribsARB) {
        const int WGL_CONTEXT_MAJOR_VERSION_ARB_ = 0x2091;
        const int WGL_CONTEXT_MINOR_VERSION_ARB_ = 0x2092;
        const int WGL_CONTEXT_PROFILE_MASK_ARB_  = 0x9126;
        const int WGL_CONTEXT_CORE_PROFILE_BIT_ARB_ = 0x00000001;
        const int WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB_ = 0x00000002;

        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB_, (int)want_major,
            WGL_CONTEXT_MINOR_VERSION_ARB_, (int)want_minor,
            WGL_CONTEXT_PROFILE_MASK_ARB_,
                want_core ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB_
                          : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB_,
            0
        };
        hglrc = wglCreateContextAttribsARB(hdc, nullptr, attribs);
    }

    if (hglrc) {
        wgl_fn.MakeCurrent(hdc, nullptr);
        wgl_fn.DeleteContext(legacy_ctx);
        wgl_fn.MakeCurrent(hdc, hglrc);
    } else {
        tretro_log("[wrn] [gl] wglCreateContextAttribsARB unavailable or failed for "
                   "GL %u.%u %s, falling back to legacy context",
                   want_major, want_minor, want_core ? "core" : "compat");
        hglrc = legacy_ctx;
    }

    if (!fbo_fn.load(getProcAddress) || !gl_fn.load(getProcAddress)) {
        tretro_log("[ERR] [gl] required GL framebuffers or core functions missing");
        destroyContext();
        return false;
    }

    tretro_log("[inf] [gl] headless WGL context ready (%s) | Renderer: %s (%s) | Async Readback: %s",
               (const char*)gl_fn.GetString(GL_VERSION),
               (const char*)gl_fn.GetString(GL_RENDERER),
               (const char*)gl_fn.GetString(GL_VENDOR),
               fbo_fn.has_pbo ? "ON" : "OFF");
    return true;
}

void GLRenderContext::destroyContext() {
    destroyFbo();
    delete[] scratch_buf;
    scratch_buf = nullptr;
    scratch_buf_cap = 0;
    if (hdc && hglrc && wgl_fn.MakeCurrent) wgl_fn.MakeCurrent(hdc, nullptr);
    if (hglrc && wgl_fn.DeleteContext) { wgl_fn.DeleteContext(hglrc); hglrc = nullptr; }
    if (hdc && hwnd) { ReleaseDC(hwnd, hdc); hdc = nullptr; }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
    active = false;
}

void GLRenderContext::makeCurrent() {
    if (hdc && hglrc && wgl_fn.MakeCurrent) wgl_fn.MakeCurrent(hdc, hglrc);
}

void* GLRenderContext::getProcAddress(const char* sym) {
    static HMODULE gl_dll = LoadLibraryA("opengl32.dll");
    static HMODULE gdi_dll = LoadLibraryA("gdi32.dll");
    if (!gl_dll) return nullptr;

    typedef PROC (WINAPI *PFNWGLGETPROCADDRESS)(LPCSTR);
    static auto wglProc = (PFNWGLGETPROCADDRESS)(void*)GetProcAddress(gl_dll, "wglGetProcAddress");

    void* p = nullptr;
    if (wglProc) p = (void*)wglProc(sym);
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)(-1)) {
        p = (void*)GetProcAddress(gl_dll, sym);
    }
    if (!p && gdi_dll) {
        p = (void*)GetProcAddress(gdi_dll, sym);
    }
    return p;
}

#else 

bool GLRenderContext::acquireDisplay() {
    const char* client_exts = egl_fn.QueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    bool has_platform_base = client_exts && strstr(client_exts, "EGL_EXT_platform_base");

    typedef EGLDisplay (EGLAPIENTRYP PFNEGLGETPLATFORMDISPLAYEXTPROC_LOCAL)(EGLenum, void*, const EGLint*);
    typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYDEVICESEXTPROC_LOCAL)(EGLint, EGLDeviceEXT*, EGLint*);

    auto tryInit = [this](EGLDisplay dpy, const char* how) -> bool {
        if (dpy == EGL_NO_DISPLAY) return false;
        EGLint maj = 0, min = 0;
        if (!egl_fn.Initialize(dpy, &maj, &min)) return false;
        egl_dpy = dpy;
        tretro_log("[inf] [gl] EGL display acquired via %s (EGL %d.%d)", how, maj, min);
        return true;
    };

    if (egl_dpy != EGL_NO_DISPLAY) return true;

    if (has_platform_base && client_exts && strstr(client_exts, "EGL_EXT_device_base")) {
        auto eglQueryDevicesEXT = (PFNEGLQUERYDEVICESEXTPROC_LOCAL)
            getProcAddress("eglQueryDevicesEXT");
        auto eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC_LOCAL)
            getProcAddress("eglGetPlatformDisplayEXT");
        if (eglQueryDevicesEXT && eglGetPlatformDisplayEXT) {
            EGLDeviceEXT devices[8];
            EGLint num_devices = 0;
            if (eglQueryDevicesEXT(8, devices, &num_devices) && num_devices > 0) {
                for (EGLint i = 0; i < num_devices; i++) {
                    EGLDisplay dpy = eglGetPlatformDisplayEXT(
                        EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
                    if (tryInit(dpy, "EGL_PLATFORM_DEVICE_EXT")) return true;
                }
            }
        }
    }

    if (has_platform_base && client_exts && strstr(client_exts, "EGL_MESA_platform_surfaceless")) {
        auto eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC_LOCAL)
            getProcAddress("eglGetPlatformDisplayEXT");
        if (eglGetPlatformDisplayEXT) {
            EGLDisplay dpy = eglGetPlatformDisplayEXT(
                EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
            if (tryInit(dpy, "EGL_PLATFORM_SURFACELESS_MESA")) return true;
        }
    }

    if (tryInit(egl_fn.GetDisplay(EGL_DEFAULT_DISPLAY), "EGL_DEFAULT_DISPLAY")) return true;

    return false;
}

bool GLRenderContext::createContext() {
    if (!egl_fn.load(getProcAddress)) {
        tretro_log("[ERR] [gl] required EGL entry points missing");
        return false;
    }

    using_gles = !wantsDesktopGL();

    if (!acquireDisplay()) {
        tretro_log("[ERR] [gl] could not acquire any usable EGL display");
        return false;
    }

    if (!egl_fn.BindAPI(using_gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) {
        tretro_log("[ERR] [gl] eglBindAPI(%s) failed — this EGL implementation "
                   "doesn't support that client API",
                   using_gles ? "GLES" : "desktop GL");
        return false;
    }

    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, (EGLint)(using_gles ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT),
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs = 0;
    if (!egl_fn.ChooseConfig(egl_dpy, cfg_attribs, &config, 1, &num_configs) ||
        num_configs < 1) {
        tretro_log("[ERR] [gl] eglChooseConfig failed for %s",
                   using_gles ? "GLES" : "desktop GL");
        return false;
    }

    EGLint pbuf_attribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    egl_surf = egl_fn.CreatePbufferSurface(egl_dpy, config, pbuf_attribs);
    if (egl_surf == EGL_NO_SURFACE) {
        tretro_log("[ERR] [gl] eglCreatePbufferSurface failed");
        return false;
    }

    if (using_gles) {
        EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        egl_ctx = egl_fn.CreateContext(egl_dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    } else {
        unsigned want_major = hw_render.version_major ? hw_render.version_major : 3;
        unsigned want_minor = hw_render.version_major ? hw_render.version_minor : 3;
        bool want_core = (hw_render.context_type == RETRO_HW_CONTEXT_OPENGL_CORE);

        EGLint ctx_attribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, (EGLint)want_major,
            EGL_CONTEXT_MINOR_VERSION, (EGLint)want_minor,
            EGL_CONTEXT_OPENGL_PROFILE_MASK,
                want_core ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
                          : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
            EGL_NONE
        };
        egl_ctx = egl_fn.CreateContext(egl_dpy, config, EGL_NO_CONTEXT, ctx_attribs);
        if (egl_ctx == EGL_NO_CONTEXT) {
            EGLint ctx_attribs_noprof[] = {
                EGL_CONTEXT_MAJOR_VERSION, (EGLint)want_major,
                EGL_CONTEXT_MINOR_VERSION, (EGLint)want_minor,
                EGL_NONE
            };
            egl_ctx = egl_fn.CreateContext(egl_dpy, config, EGL_NO_CONTEXT, ctx_attribs_noprof);
        }
    }

    if (egl_ctx == EGL_NO_CONTEXT) {
        tretro_log("[ERR] [gl] eglCreateContext failed for %s (EGL err 0x%x)",
                   using_gles ? "GLES" : "desktop GL", egl_fn.GetError());
        return false;
    }

    if (!egl_fn.MakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) {
        tretro_log("[ERR] [gl] eglMakeCurrent failed");
        return false;
    }

    if (!fbo_fn.load(getProcAddress) || !gl_fn.load(getProcAddress)) {
        tretro_log("[ERR] [gl] required framebuffer-object entry points missing");
        return false;
    }

    tretro_log("[inf] [gl] headless EGL context ready: %s (%s) | Renderer: %s (%s) | Async Readback: %s",
               using_gles ? "GLES" : "desktop GL",
               (const char*)gl_fn.GetString(GL_VERSION),
               (const char*)gl_fn.GetString(GL_RENDERER),
               (const char*)gl_fn.GetString(GL_VENDOR),
               fbo_fn.has_pbo ? "ON" : "OFF");
    return true;
}

void GLRenderContext::destroyContext() {
    destroyFbo();
    delete[] scratch_buf;
    scratch_buf = nullptr;
    scratch_buf_cap = 0;
    if (egl_dpy != EGL_NO_DISPLAY) {
        if (egl_fn.MakeCurrent) egl_fn.MakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_ctx  != EGL_NO_CONTEXT && egl_fn.DestroyContext)  egl_fn.DestroyContext(egl_dpy, egl_ctx);
        if (egl_surf != EGL_NO_SURFACE && egl_fn.DestroySurface)  egl_fn.DestroySurface(egl_dpy, egl_surf);
        if (egl_fn.Terminate) egl_fn.Terminate(egl_dpy);
    }
    egl_dpy  = EGL_NO_DISPLAY;
    egl_ctx  = EGL_NO_CONTEXT;
    egl_surf = EGL_NO_SURFACE;
    active = false;
}

void GLRenderContext::makeCurrent() {
    if (egl_fn.BindAPI) egl_fn.BindAPI(using_gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
    if (egl_fn.MakeCurrent) egl_fn.MakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx);
}

void* GLRenderContext::getProcAddress(const char* sym) {
    static void* egl_dll = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!egl_dll) egl_dll = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);

    typedef void* (*PFNEGLGETPROCADDRESS)(const char*);
    static auto eglProc = egl_dll ? (PFNEGLGETPROCADDRESS)dlsym(egl_dll, "eglGetProcAddress") : nullptr;

    void* p = nullptr;
    if (eglProc) p = eglProc(sym);
    if (!p) {
        if (egl_dll) p = dlsym(egl_dll, sym);
        if (!p) {
            static void* gl_dll = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
            if (!gl_dll) gl_dll = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
            if (!gl_dll) gl_dll = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
            if (!gl_dll) gl_dll = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);
            if (gl_dll) p = dlsym(gl_dll, sym);
        }
    }
    return p;
}

#endif 

bool GLRenderContext::createFbo(unsigned w, unsigned h) {
    if (w == 0 || h == 0) return false;
    if (fbo && fbo_w == w && fbo_h == h) return true;

    destroyFbo();

    gl_fn.GenTextures(1, &color_tex);
    gl_fn.BindTexture(GL_TEXTURE_2D, color_tex);
    gl_fn.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    bool want_depth = hw_render.depth || hw_render.stencil;
    if (want_depth) {
        fbo_fn.GenRenderbuffers(1, &depth_rb);
        fbo_fn.BindRenderbuffer(GL_RENDERBUFFER, depth_rb);

        GLenum depth_fmt;
        if (wantsDesktopGL()) {
            depth_fmt = hw_render.stencil ? GL_DEPTH32F_STENCIL8 : GL_DEPTH_COMPONENT32F;
        } else {
            depth_fmt = hw_render.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
        }
        fbo_fn.RenderbufferStorage(GL_RENDERBUFFER, depth_fmt, (GLsizei)w, (GLsizei)h);
    }

    fbo_fn.GenFramebuffers(1, &fbo);
    fbo_fn.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    fbo_fn.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, color_tex, 0);
    if (hw_render.depth) {
        fbo_fn.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                        GL_RENDERBUFFER, depth_rb);
    }
    if (hw_render.stencil) {
        fbo_fn.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER, depth_rb);
    }

    GLenum status = fbo_fn.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        tretro_log("[ERR] [gl] FBO incomplete (0x%x) at %ux%u", status, w, h);
        destroyFbo();
        return false;
    }

    if (fbo_fn.BlitFramebuffer) {
        gl_fn.GenTextures(1, &color_tex_blit);
        gl_fn.BindTexture(GL_TEXTURE_2D, color_tex_blit);
        gl_fn.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_fn.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        fbo_fn.GenFramebuffers(1, &fbo_blit);
        fbo_fn.BindFramebuffer(GL_FRAMEBUFFER, fbo_blit);
        fbo_fn.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D, color_tex_blit, 0);

        status = fbo_fn.CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            tretro_log("[ERR] [gl] Blit FBO incomplete (0x%x)", status);
            destroyFbo();
            return false;
        }
    }

    fbo_w = w;
    fbo_h = h;
    fbo_fn.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl_fn.BindTexture(GL_TEXTURE_2D, 0);
    tretro_log("[inf] [gl] offscreen FBO %ux%u ready", w, h);
    return true;
}

void GLRenderContext::destroyFbo() {
    if (fbo) { fbo_fn.DeleteFramebuffers(1, &fbo); fbo = 0; }
    if (color_tex && gl_fn.DeleteTextures) { gl_fn.DeleteTextures(1, &color_tex); color_tex = 0; }
    if (depth_rb) { fbo_fn.DeleteRenderbuffers(1, &depth_rb); depth_rb = 0; }
    if (fbo_blit) { fbo_fn.DeleteFramebuffers(1, &fbo_blit); fbo_blit = 0; }
    if (color_tex_blit && gl_fn.DeleteTextures) { gl_fn.DeleteTextures(1, &color_tex_blit); color_tex_blit = 0; }
    if (fbo_fn.has_pbo && pbo[0]) { fbo_fn.DeleteBuffers(3, pbo); pbo[0] = pbo[1] = pbo[2] = 0; }
    fbo_w = fbo_h = pbo_w = pbo_h = 0;
    pbo_valid[0] = pbo_valid[1] = pbo_valid[2] = false;
}

void GLRenderContext::bindForCoreRender() {
    if (!active || !fbo) return;
    makeCurrent();
    fbo_fn.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (gl_fn.Viewport) gl_fn.Viewport(0, 0, (GLsizei)fbo_w, (GLsizei)fbo_h);
}

void GLRenderContext::processPixels(const void* __restrict src, uint8_t* __restrict out, unsigned w, unsigned h, bool desktop, bool flip_y) {
    if (desktop) {
        if (!flip_y) {
            memcpy(out, src, (size_t)w * h * 4);
        } else {
            for (unsigned y = 0; y < h; y++) {
                unsigned src_y = h - 1 - y;
                memcpy(out + (size_t)y * w * 4, (const uint8_t*)src + (size_t)src_y * w * 4, w * 4);
            }
        }
    } else {
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC ivdep
#endif
        for (unsigned y = 0; y < h; y++) {
            unsigned src_y = flip_y ? (h - 1 - y) : y;
            const uint32_t* __restrict src_row = (const uint32_t*)((const uint8_t*)src + (size_t)src_y * w * 4);
            uint32_t* __restrict dst_row = (uint32_t*)(out + (size_t)y * w * 4);
            for (unsigned x = 0; x < w; x++) {
                uint32_t p = src_row[x];
                dst_row[x] = (p & 0x0000FF00) | ((p & 0x000000FF) << 16) | ((p & 0x00FF0000) >> 16) | 0xFF000000;
            }
        }
    }
}

bool GLRenderContext::readPixelsXRGB8888(uint8_t* out, unsigned w, unsigned h, unsigned out_w, unsigned out_h) {
    if (!fbo || !out) return false;

    bool desktop = wantsDesktopGL();
    GLenum read_fmt = desktop ? GL_BGRA : GL_RGBA;
    bool flip_y = hw_render.bottom_left_origin;

    bool scaled = out_w && out_h && out_w < w && out_h < h && fbo_fn.BlitFramebuffer && fbo_blit;
    unsigned rw = scaled ? out_w : w;
    unsigned rh = scaled ? out_h : h;
    size_t needed_size = (size_t)rw * rh * 4;

    bool use_gpu_blit = scaled || (flip_y && fbo_fn.BlitFramebuffer && fbo_blit);
    if (use_gpu_blit) {
        fbo_fn.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        fbo_fn.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_blit);
        fbo_fn.BlitFramebuffer(0, 0, (GLint)w, (GLint)h,
                               0, flip_y ? (GLint)rh : 0, (GLint)rw, flip_y ? 0 : (GLint)rh,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
        fbo_fn.BindFramebuffer(GL_FRAMEBUFFER, fbo_blit);
        flip_y = false;
    } else {
        fbo_fn.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    }

    if (gl_fn.PixelStorei) gl_fn.PixelStorei(GL_PACK_ALIGNMENT, 4);

    if (fbo_fn.has_pbo) {
        if (pbo_w != rw || pbo_h != rh) {
            if (!pbo[0]) fbo_fn.GenBuffers(3, pbo);
            for (int i = 0; i < 3; i++) {
                fbo_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
                fbo_fn.BufferData(GL_PIXEL_PACK_BUFFER, (GR_GLsizeiptr)needed_size, nullptr, GL_STREAM_READ);
            }
            pbo_w = rw;
            pbo_h = rh;
            pbo_valid[0] = pbo_valid[1] = pbo_valid[2] = false;
        }

        fbo_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, pbo[pbo_idx]);
        if (gl_fn.ReadPixels) gl_fn.ReadPixels(0, 0, (GLsizei)rw, (GLsizei)rh, read_fmt, GL_UNSIGNED_BYTE, 0);

        if (gl_fn.Flush) gl_fn.Flush();

        int next_idx = (pbo_idx + 1) % 3;
        int read_idx = (pbo_idx + 2) % 3;
        bool mapped = false;

        if (pbo_valid[read_idx]) {
            fbo_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, pbo[read_idx]);
            void* ptr = fbo_fn.MapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GR_GLsizeiptr)needed_size, GL_MAP_READ_BIT);
            if (ptr) {
                processPixels(ptr, out, rw, rh, desktop, flip_y);
                fbo_fn.UnmapBuffer(GL_PIXEL_PACK_BUFFER);
                mapped = true;
            }
        }

        pbo_valid[pbo_idx] = true;
        pbo_idx = next_idx;
        fbo_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        if (!mapped) {
            memset(out, 0, needed_size);
        }
        return true;
    }

    if (scratch_buf_cap < needed_size) {
        delete[] scratch_buf;
        scratch_buf = new uint8_t[needed_size];
        scratch_buf_cap = needed_size;
    }

    if (gl_fn.ReadPixels) gl_fn.ReadPixels(0, 0, (GLsizei)rw, (GLsizei)rh, read_fmt, GL_UNSIGNED_BYTE, scratch_buf);
    processPixels(scratch_buf, out, rw, rh, desktop, flip_y);

    return true;
}
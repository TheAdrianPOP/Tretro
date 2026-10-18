#include "gl_render.h"

// Names are loaded in one loop into the contiguous function-pointer structs.
// Order must match the members of GLFuncs / WglFuncs.
static const char kGlNames[] =
    "glViewport\0glReadPixels\0glFlush\0"
    "glGenFramebuffers\0glBindFramebuffer\0glGenRenderbuffers\0glBindRenderbuffer\0"
    "glRenderbufferStorage\0glFramebufferRenderbuffer\0glCheckFramebufferStatus\0"
    "glDeleteFramebuffers\0glDeleteRenderbuffers\0glBlitFramebuffer\0"
    "glGenBuffers\0glDeleteBuffers\0glBindBuffer\0glBufferData\0"
    "glMapBufferRange\0glUnmapBuffer\0";

static const char kWglNames[] =
    "wglCreateContext\0wglMakeCurrent\0wglDeleteContext\0"
    "ChoosePixelFormat\0SetPixelFormat\0";

static constexpr int kGlCount  = 19;
static constexpr int kWglCount = 5;
static_assert(sizeof(GLFuncs)  == kGlCount  * sizeof(void*), "GLFuncs layout");
static_assert(sizeof(WglFuncs) == kWglCount * sizeof(void*), "WglFuncs layout");

static bool loadTable(void** out, const char* names, int n) {
    bool ok = true;
    for (int i = 0; i < n; ++i) {
        out[i] = GLRenderContext::getProcAddress(names);
        ok &= (out[i] != nullptr);
        while (*names++) {}
    }
    return ok;
}

void* GLRenderContext::getProcAddress(const char* sym) {
    static HMODULE gl  = LoadLibraryA("opengl32.dll");
    static HMODULE gdi = LoadLibraryA("gdi32.dll");
    if (!gl) return nullptr;

    typedef PROC (WINAPI *PFNWGLGETPROCADDRESS)(LPCSTR);
    static auto wglProc = (PFNWGLGETPROCADDRESS)(void*)GetProcAddress(gl, "wglGetProcAddress");

    void* p = wglProc ? (void*)wglProc(sym) : nullptr;
    if ((uintptr_t)p <= 3 || p == (void*)-1) p = (void*)GetProcAddress(gl, sym);
    if (!p && gdi) p = (void*)GetProcAddress(gdi, sym);
    return p;
}

bool GLRenderContext::createContext() {
    if (!loadTable((void**)&wgl_fn, kWglNames, kWglCount)) return false;

    static const char kClass[] = "tretro_gl_hidden_wnd";
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(0, kClass, kClass, WS_DISABLED, 0, 0, 1, 1,
                           nullptr, nullptr, wc.hInstance, nullptr);
    hdc = hwnd ? GetDC(hwnd) : nullptr;

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;

    int fmt = hdc ? wgl_fn.ChoosePixelFormat_(hdc, &pfd) : 0;
    if (!fmt || !wgl_fn.SetPixelFormat_(hdc, fmt, &pfd)) { destroyContext(); return false; }

    // A legacy context is needed first to reach wglCreateContextAttribsARB.
    hglrc = wgl_fn.CreateContext(hdc);
    if (!hglrc || !wgl_fn.MakeCurrent(hdc, hglrc)) { destroyContext(); return false; }

    typedef HGLRC (WINAPI *PFNCREATEATTRIBS)(HDC, HGLRC, const int*);
    auto createAttribs = (PFNCREATEATTRIBS)getProcAddress("wglCreateContextAttribsARB");
    if (createAttribs) {
        unsigned maj = hw_render.version_major ? hw_render.version_major : 3;
        unsigned min = hw_render.version_major ? hw_render.version_minor : 3;
        const int attribs[] = {
            0x2091, (int)maj,                                           // CONTEXT_MAJOR_VERSION
            0x2092, (int)min,                                           // CONTEXT_MINOR_VERSION
            0x9126, hw_render.context_type == RETRO_HW_CONTEXT_OPENGL_CORE ? 1 : 2, // PROFILE_MASK
            0
        };
        if (HGLRC ctx = createAttribs(hdc, nullptr, attribs)) {
            wgl_fn.MakeCurrent(hdc, nullptr);
            wgl_fn.DeleteContext(hglrc);
            hglrc = ctx;
            wgl_fn.MakeCurrent(hdc, hglrc);
        }
    }

    // Needs GL 3.0-level FBO, blit and PBO support; otherwise HW render is declined.
    if (!loadTable((void**)&gl_fn, kGlNames, kGlCount)) { destroyContext(); return false; }
    return true;
}

void GLRenderContext::destroyContext() {
    if (gl_fn.DeleteFramebuffers) destroyFbo();
    if (hdc && hglrc) wgl_fn.MakeCurrent(hdc, nullptr);
    if (hglrc) { wgl_fn.DeleteContext(hglrc); hglrc = nullptr; }
    if (hdc && hwnd) { ReleaseDC(hwnd, hdc); hdc = nullptr; }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
    active = false;
}

void GLRenderContext::makeCurrent() {
    if (hdc && hglrc) wgl_fn.MakeCurrent(hdc, hglrc);
}

bool GLRenderContext::createFbo(unsigned w, unsigned h) {
    if (w == 0 || h == 0) return false;
    if (fbo && fbo_w == w && fbo_h == h) return true;

    destroyFbo();

    // Color goes into a renderbuffer (we only ever blit/read it), so no textures are needed.
    auto make = [&](GLuint& f, GLuint& color, bool with_depth) -> bool {
        gl_fn.GenRenderbuffers(1, &color);
        gl_fn.BindRenderbuffer(GL_RENDERBUFFER, color);
        gl_fn.RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei)w, (GLsizei)h);

        gl_fn.GenFramebuffers(1, &f);
        gl_fn.BindFramebuffer(GL_FRAMEBUFFER, f);
        gl_fn.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);

        if (with_depth && (hw_render.depth || hw_render.stencil)) {
            gl_fn.GenRenderbuffers(1, &depth_rb);
            gl_fn.BindRenderbuffer(GL_RENDERBUFFER, depth_rb);
            gl_fn.RenderbufferStorage(GL_RENDERBUFFER,
                                      hw_render.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
                                      (GLsizei)w, (GLsizei)h);
            if (hw_render.depth)   gl_fn.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,   GL_RENDERBUFFER, depth_rb);
            if (hw_render.stencil) gl_fn.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
        }
        return gl_fn.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };

    if (!make(fbo, color_rb, true) || !make(fbo_blit, color_rb_blit, false)) {
        destroyFbo();
        return false;
    }

    fbo_w = w;
    fbo_h = h;
    gl_fn.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void GLRenderContext::destroyFbo() {
    // Deleting name 0 is a no-op in GL, so unused slots are fine.
    const GLuint fbs[2] = { fbo, fbo_blit };
    const GLuint rbs[3] = { color_rb, color_rb_blit, depth_rb };
    if (fbo || fbo_blit)                           gl_fn.DeleteFramebuffers(2, fbs);
    if (color_rb || color_rb_blit || depth_rb)     gl_fn.DeleteRenderbuffers(3, rbs);
    if (pbo[0])                                    gl_fn.DeleteBuffers(3, pbo);

    fbo = fbo_blit = color_rb = color_rb_blit = depth_rb = 0;
    pbo[0] = pbo[1] = pbo[2] = 0;
    fbo_w = fbo_h = pbo_w = pbo_h = 0;
    pbo_valid[0] = pbo_valid[1] = pbo_valid[2] = false;
}

void GLRenderContext::bindForCoreRender() {
    if (!active || !fbo) return;
    makeCurrent();
    gl_fn.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_fn.Viewport(0, 0, (GLsizei)fbo_w, (GLsizei)fbo_h);
}

// Reads the frame as BGRA (== XRGB8888 in memory), optionally downscaled and/or flipped on the GPU.
// Results arrive one frame later via triple-buffered PBOs; the first frames are black.
bool GLRenderContext::readPixelsXRGB8888(uint8_t* out, unsigned w, unsigned h, unsigned out_w, unsigned out_h) {
    if (!fbo || !out) return false;

    const bool flip   = hw_render.bottom_left_origin;
    const bool scaled = out_w && out_h && out_w < w && out_h < h;
    const unsigned rw = scaled ? out_w : w;
    const unsigned rh = scaled ? out_h : h;
    const size_t   size = (size_t)rw * rh * 4;

    GLuint src = fbo;
    if (scaled || flip) {
        gl_fn.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        gl_fn.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_blit);
        gl_fn.BlitFramebuffer(0, 0, (GLint)w, (GLint)h,
                              0, flip ? (GLint)rh : 0, (GLint)rw, flip ? 0 : (GLint)rh,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        src = fbo_blit;
    }
    gl_fn.BindFramebuffer(GL_FRAMEBUFFER, src);

    if (pbo_w != rw || pbo_h != rh) {
        if (!pbo[0]) gl_fn.GenBuffers(3, pbo);
        for (int i = 0; i < 3; i++) {
            gl_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
            gl_fn.BufferData(GL_PIXEL_PACK_BUFFER, (ptrdiff_t)size, nullptr, GL_STREAM_READ);
        }
        pbo_w = rw;
        pbo_h = rh;
        pbo_valid[0] = pbo_valid[1] = pbo_valid[2] = false;
    }

    gl_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, pbo[pbo_idx]);
    gl_fn.ReadPixels(0, 0, (GLsizei)rw, (GLsizei)rh, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    gl_fn.Flush();

    const int read_idx = (pbo_idx + 2) % 3;
    bool mapped = false;
    if (pbo_valid[read_idx]) {
        gl_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, pbo[read_idx]);
        void* ptr = gl_fn.MapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (ptrdiff_t)size, GL_MAP_READ_BIT);
        if (ptr) {
            memcpy(out, ptr, size);
            gl_fn.UnmapBuffer(GL_PIXEL_PACK_BUFFER);
            mapped = true;
        }
    }

    pbo_valid[pbo_idx] = true;
    pbo_idx = (pbo_idx + 1) % 3;
    gl_fn.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    if (!mapped) memset(out, 0, size);
    return true;
}
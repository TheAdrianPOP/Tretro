#include "frontend.h"
#include "config_manager.h" 

#ifndef TRETRO_GPU_DOWNSCALE
#define TRETRO_GPU_DOWNSCALE 1
#endif

static inline int fast_min(int a, int b) { return a < b ? a : b; }
static inline int fast_max(int a, int b) { return a > b ? a : b; }
static inline int fast_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static constexpr const char* const raw_shadow_ramp[5] = {" ", "\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93", "\xe2\x96\x88"}; 
static constexpr char raw_ascii_ramp[] = " .'`^\",:;Il!i~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
static constexpr int ASCII_RAMP_SIZE = sizeof(raw_ascii_ramp) - 1;

struct RGB { uint8_t r, g, b; };

static constexpr RGB standard_16[16] = {
    {0,0,0}, {128,0,0}, {0,128,0}, {128,128,0}, {0,0,128}, {128,0,128}, {0,128,128}, {192,192,192},
    {128,128,128}, {255,0,0}, {0,255,0}, {255,255,0}, {0,0,255}, {255,0,255}, {0,255,255}, {255,255,255}
};

static constexpr uint8_t levels[6] = {0, 95, 135, 175, 215, 255};

static inline void set_space(ConsoleCell& c, RGBColor col) {
    c.character = ' ';
    c.color = col;
    c.bg_color = col;
    c.is_ui = false;
    c.is_border = false;
}

static inline void set_half_block(ConsoleCell& c, RGBColor fg, RGBColor bg) {
    c.character.data[0] = '\xe2';
    c.character.data[1] = '\x96';
    c.character.data[2] = '\x80';
    c.character.data[3] = '\0';
    c.color = fg;
    c.bg_color = bg;
    c.is_ui = false;
    c.is_border = false;
}

static inline void set_ascii(ConsoleCell& c, char ch, RGBColor fg, RGBColor bg) {
    c.character = ch;
    c.color = fg;
    c.bg_color = bg;
    c.is_ui = false;
    c.is_border = false;
}

static inline void set_glyph(ConsoleCell& c, const char* g, RGBColor fg, RGBColor bg) {
    c.character = ConsoleGlyph(g);
    c.color = fg;
    c.bg_color = bg;
    c.is_ui = false;
    c.is_border = false;
}

struct BlitParams {
    const uint8_t* src;
    size_t   pitch;
    uint32_t scale_x, scale_y;
    int      off_x, off_y;
    int      fit_w, fit_h;
    int      term_w;
    int      avail_h;
    int      max_sx, max_sy;
    RGBColor bezel;
    retro_pixel_format fmt;
    bool     use_pixel_mode;
    int      color_mode;
    bool     dither;
    bool     adjust;
    uint8_t  adj[256];
};

static inline void decode_pixel_fast(const uint8_t* row, int sx, retro_pixel_format fmt,
                                    uint8_t& r, uint8_t& g, uint8_t& b)
{
    if (fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
        uint32_t px = reinterpret_cast<const uint32_t*>(row)[sx];
        r = static_cast<uint8_t>(px >> 16);
        g = static_cast<uint8_t>(px >> 8);
        b = static_cast<uint8_t>(px);
    } else {
        uint16_t px = reinterpret_cast<const uint16_t*>(row)[sx];
        uint32_t b5 = px & 0x1F;
        b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));

        if (fmt == RETRO_PIXEL_FORMAT_RGB565) {
            uint32_t r5 = (px >> 11) & 0x1F;
            uint32_t g6 = (px >> 5)  & 0x3F;
            r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        } else { 
            uint32_t r5 = (px >> 10) & 0x1F;
            uint32_t g5 = (px >> 5)  & 0x1F;
            r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            g = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
        }
    }
}

static constexpr int get_bayer16(int x, int y) {
    x &= 15; y &= 15;
    int v = (((x & 1) ^ (y & 1)) << 1 | (x & 1)) << 6;
    v |= ((((x >> 1) & 1) ^ ((y >> 1) & 1)) << 1 | ((x >> 1) & 1)) << 4;
    v |= ((((x >> 2) & 1) ^ ((y >> 2) & 1)) << 1 | ((x >> 2) & 1)) << 2;
    v |= ((((x >> 3) & 1) ^ ((y >> 3) & 1)) << 1 | ((x >> 3) & 1));
    return v;
}

struct BayerTable { uint8_t v[256]; };
static constexpr BayerTable make_bayer_table() {
    BayerTable t = {};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            t.v[(y << 4) | x] = static_cast<uint8_t>(get_bayer16(x, y));
    return t;
}
static constexpr BayerTable kBayer = make_bayer_table();

static constexpr int ct_find_nearest_ansi(int r, int g, int b, int count) {
    int best_idx = 0;
    int best_dist = 10000000;
    int dr_g = (r - g) < 0 ? -(r - g) : (r - g);
    int dg_b = (g - b) < 0 ? -(g - b) : (g - b);
    int dr_b = (r - b) < 0 ? -(r - b) : (r - b);
    int max_diff = dr_g > dg_b ? (dr_g > dr_b ? dr_g : dr_b) : (dg_b > dr_b ? dg_b : dr_b);
    bool is_neutral = (max_diff < 30);

    for (int i = 0; i < count; i++) {
        if (count == 16 && (i == 7 || i == 8) && !is_neutral) continue;
        const RGB& c = standard_16[i];
        int dr = r - c.r, dg = g - c.g, db = b - c.b;
        int dist = dr*dr + dg*dg + db*db;
        if (count == 16 && (i == 7 || i == 8)) dist = (dist * 3) / 2;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}

struct DitherTables {
    uint8_t ansi16[3][3][3];
    uint8_t ansi8[3][3][3];
};

static constexpr DitherTables build_dither_tables() {
    DitherTables t = {};
    const int vals[3] = {0, 127, 255};
    for (int r = 0; r < 3; r++) {
        for (int g = 0; g < 3; g++) {
            for (int b = 0; b < 3; b++) {
                t.ansi16[r][g][b] = static_cast<uint8_t>(ct_find_nearest_ansi(vals[r], vals[g], vals[b], 16));
                t.ansi8[r][g][b]  = static_cast<uint8_t>(ct_find_nearest_ansi(vals[r], vals[g], vals[b], 8));
            }
        }
    }
    return t;
}

static constexpr DitherTables g_dither_grid = build_dither_tables();

static inline void dither_color_fast(const BlitParams& p, uint8_t& r, uint8_t& g, uint8_t& b, int x, int y) {
    const int color_mode = p.color_mode;
    bool is_ansi_limited = (color_mode == 2 || color_mode == 3);
    int num_steps = is_ansi_limited ? 2 : 5;
    int step_size = is_ansi_limited ? 127 : 51;

    int r_i = (r * num_steps * 257 + 257) >> 16;
    int g_i = (g * num_steps * 257 + 257) >> 16;
    int b_i = (b * num_steps * 257 + 257) >> 16;

    int r_offset = 0, g_offset = 0, b_offset = 0;

    if (p.dither) {
        int d = (kBayer.v[((y & 15) << 4) | (x & 15)] * step_size) >> 8;
        r_offset = (d >= step_size - (r - r_i * step_size));
        g_offset = (d >= step_size - (g - g_i * step_size));
        b_offset = (d >= step_size - (b - b_i * step_size));
    }

    int r_idx = fast_clamp(r_i + r_offset, 0, num_steps);
    int g_idx = fast_clamp(g_i + g_offset, 0, num_steps);
    int b_idx = fast_clamp(b_i + b_offset, 0, num_steps);

    if (is_ansi_limited) {
        int idx = (color_mode == 2) 
                  ? g_dither_grid.ansi16[r_idx][g_idx][b_idx]
                  : g_dither_grid.ansi8[r_idx][g_idx][b_idx];
        const RGB& c = standard_16[idx];
        r = c.r; g = c.g; b = c.b;
    } else {
        r = levels[r_idx]; g = levels[g_idx]; b = levels[b_idx];
    }
}

__attribute__((noinline))
static RGB sample_pixel(const BlitParams& p, const uint8_t* row_ptr, int sx, int tx, int py) {
    uint8_t r, g, b;
    decode_pixel_fast(row_ptr, sx, p.fmt, r, g, b);
    if (p.adjust) { r = p.adj[r]; g = p.adj[g]; b = p.adj[b]; }
    if (p.color_mode != 0) {
        dither_color_fast(p, r, g, b, tx, py);
    }
    return {r, g, b};
}

static inline int calc_luma(uint8_t r, uint8_t g, uint8_t b) {
    return (13933 * r + 46871 * g + 4732 * b) >> 16;
}

__attribute__((noinline))
static void blitRow(const BlitParams& p, int ty, ConsoleCell* row) {
    if (ty >= p.avail_h) {
        for (int i = 0; i < p.term_w; ++i) set_space(row[i], p.bezel);
        return;
    }

    int x_start = fast_clamp(p.off_x, 0, p.term_w);
    int x_end   = fast_clamp(p.off_x + p.fit_w, 0, p.term_w);

    int py_top = ty * 2;
    int py_bot = py_top + 1;
    bool in_top = p.use_pixel_mode && (py_top >= p.off_y && py_top < p.off_y + p.fit_h);
    bool in_bot = p.use_pixel_mode && (py_bot >= p.off_y && py_bot < p.off_y + p.fit_h);
    bool in_screen = !p.use_pixel_mode && (ty >= p.off_y && ty < p.off_y + p.fit_h);

    if (p.use_pixel_mode ? (!in_top && !in_bot) : !in_screen) {
        for (int i = 0; i < p.term_w; ++i) set_space(row[i], p.bezel);
        return;
    }

    for (int i = 0; i < x_start; ++i) set_space(row[i], p.bezel);
    for (int i = x_end; i < p.term_w; ++i) set_space(row[i], p.bezel);

    uint32_t sx_fp = static_cast<uint32_t>((x_start - p.off_x) * p.scale_x);

    if (p.use_pixel_mode) {
        int sy_top = in_top ? fast_clamp(static_cast<int>(((static_cast<int64_t>(py_top - p.off_y) * p.scale_y) >> 16)), 0, p.max_sy) : 0;
        int sy_bot = in_bot ? fast_clamp(static_cast<int>(((static_cast<int64_t>(py_bot - p.off_y) * p.scale_y) >> 16)), 0, p.max_sy) : 0;
        const uint8_t* row_top = p.src + sy_top * p.pitch;
        const uint8_t* row_bot = p.src + sy_bot * p.pitch;

        for (int tx = x_start; tx < x_end; ++tx) {
            int sx = fast_min(static_cast<int>(sx_fp >> 16), p.max_sx);
            sx_fp += p.scale_x;

            RGB t = {p.bezel.r, p.bezel.g, p.bezel.b};
            RGB b = t;
            if (in_top) t = sample_pixel(p, row_top, sx, tx, py_top);
            if (in_bot) b = sample_pixel(p, row_bot, sx, tx, py_bot);

            if (p.color_mode == 4) {
                int lum = (calc_luma(t.r, t.g, t.b) + calc_luma(b.r, b.g, b.b)) >> 1;
                int idx = fast_clamp((lum * 4 + 127) / 255, 0, 4);
                set_glyph(row[tx], raw_shadow_ramp[idx], RGBColor(0,0,0), p.bezel);
            } else {
                set_half_block(row[tx], RGBColor(t.r, t.g, t.b), RGBColor(b.r, b.g, b.b));
            }
        }
    } else {
        int sy = fast_clamp(static_cast<int>(((static_cast<int64_t>(ty - p.off_y) * p.scale_y) >> 16)), 0, p.max_sy);
        const uint8_t* row_src = p.src + sy * p.pitch;

        for (int tx = x_start; tx < x_end; ++tx) {
            int sx = fast_min(static_cast<int>(sx_fp >> 16), p.max_sx);
            sx_fp += p.scale_x;

            RGB pxl = sample_pixel(p, row_src, sx, tx, ty);
            int lum = calc_luma(pxl.r, pxl.g, pxl.b);
            int idx = fast_clamp((lum * (ASCII_RAMP_SIZE - 1) + 127) / 255, 0, ASCII_RAMP_SIZE - 1);
            RGBColor fg = (p.color_mode == 4) ? RGBColor(0,0,0) : RGBColor(pxl.r, pxl.g, pxl.b);
            set_ascii(row[tx], raw_ascii_ramp[idx], fg, p.bezel);
        }
    }
}

struct Layout {
    int  fit_w, fit_h;          
    int  avail_h;               
    int  term_px_w, term_px_h;
    bool pixel_mode;
};

static Layout computeLayout(unsigned src_w, unsigned src_h, int term_w, int term_h, float target_aspect)
{
    Layout L;
    L.pixel_mode = g_cfg.pixel_mode;
    bool use_pixel_mode = L.pixel_mode;

    int bar_h = 0;
    if (g_cfg.show_bottom_bar) {
        bool big_mode = (g_cfg.menu_scale == 0) ? (term_w >= 160 && term_h >= 50) : (g_cfg.menu_scale == 2);
        bar_h = big_mode ? 7 : 1;
    }
    int avail_h = (term_h > bar_h + 4) ? (term_h - bar_h) : term_h;

    int term_px_w = term_w;
    int term_px_h = use_pixel_mode ? (avail_h * 2) : avail_h;

    float aspect = target_aspect > 0.0f ? target_aspect : static_cast<float>(src_w) / static_cast<float>(src_h);

    int fit_w, fit_h;
    if (g_cfg.aspect_mode == 3) {
        fit_w = term_px_w;
        fit_h = term_px_h;
    } 
    else if (g_cfg.aspect_mode == 4) {
        fit_w = static_cast<int>(src_w);
        fit_h = static_cast<int>(src_h);
    } 
    else {
        float aspect_corrected = aspect;
        if (g_cfg.aspect_mode == 1) aspect_corrected = 4.0f / 3.0f;
        if (g_cfg.aspect_mode == 2) aspect_corrected = 8.0f / 7.0f;

        float font_aspect = (g_cfg.font_width == 1) ? 0.625f : 0.5f;
        aspect_corrected /= (use_pixel_mode ? (2.0f * font_aspect) : font_aspect);

        fit_w = term_px_w;
        fit_h = static_cast<int>(static_cast<float>(term_px_w) / aspect_corrected);

        if (fit_h > term_px_h) {
            fit_h = term_px_h;
            fit_w = static_cast<int>(static_cast<float>(term_px_h) * aspect_corrected);
        }
    }

    L.fit_w = fast_max(1, fit_w);
    L.fit_h = fast_max(1, fit_h);
    L.avail_h = avail_h;
    L.term_px_w = term_px_w;
    L.term_px_h = term_px_h;
    return L;
}

static void blitHalfBlock(Frontend&          f,
                          const void*        src,
                          unsigned           src_w,
                          unsigned           src_h,
                          size_t             pitch,
                          retro_pixel_format fmt,
                          ConsoleCell*       cells,
                          int                term_w,
                          int                term_h,
                          float              target_aspect)
{
    if (!src || !src_w || !src_h || !pitch || !term_w || !term_h) return;

    Layout L = computeLayout(src_w, src_h, term_w, term_h, target_aspect);
    bool use_pixel_mode = L.pixel_mode;
    int avail_h = L.avail_h, fit_w = L.fit_w, fit_h = L.fit_h;
    int term_px_w = L.term_px_w, term_px_h = L.term_px_h;

    BlitParams params;
    params.src            = reinterpret_cast<const uint8_t*>(src);
    params.pitch          = pitch;
    params.scale_x        = static_cast<uint32_t>((static_cast<uint64_t>(src_w) << 16) / fit_w);
    params.scale_y        = static_cast<uint32_t>((static_cast<uint64_t>(src_h) << 16) / fit_h);
    params.off_x          = (term_px_w - fit_w) / 2;
    params.off_y          = (term_px_h - fit_h) / 2;
    params.fit_w          = fit_w;
    params.fit_h          = fit_h;
    params.term_w         = term_w;
    params.avail_h        = avail_h;
    params.max_sx         = static_cast<int>(src_w - 1);
    params.max_sy         = static_cast<int>(src_h - 1);
    params.color_mode     = static_cast<int>(g_cfg.color_mode);
    params.dither         = g_cfg.dither_enabled;
    params.adjust         = !(g_cfg.brightness == 100 && g_cfg.contrast == 100);
    if (params.adjust) {
        int c_fac_fp = (g_cfg.contrast * 256) / 100;
        int b_off_fp = ((g_cfg.brightness - 100) * 653) >> 8;
        for (int i = 0; i < 256; ++i) {
            int v = (((i - 128) * c_fac_fp) >> 8) + 128 + b_off_fp;
            params.adj[i] = static_cast<uint8_t>(fast_clamp(v, 0, 255));
        }
    }
    params.bezel          = (params.color_mode == 4) ? RGBColor(0, 0, 0) : RGBColor(8, 12, 16);
    params.fmt            = fmt;
    params.use_pixel_mode = use_pixel_mode;

    f.viewport.off_x = params.off_x;
    f.viewport.off_y = params.off_y;
    f.viewport.fit_w = fit_w;
    f.viewport.fit_h = fit_h;

    f.threads.run(term_h, [&](int ty) {
        blitRow(params, ty, &cells[ty * term_w]);
    });
}

void video_refresh_cb(const void* data,
                      unsigned width, unsigned height,
                      size_t pitch)
{
    Frontend& f = Frontend::get();

    if (!width || !height || !f.term_w || !f.term_h) return;

    retro_pixel_format fmt = f.pixel_format;

    float aspect = f.av_info.geometry.aspect_ratio;
    if (aspect <= 0.0f) aspect = static_cast<float>(width) / static_cast<float>(height);

    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        if (!f.gl.active || !f.gl.fbo) return;

        unsigned out_w = width, out_h = height;
#if TRETRO_GPU_DOWNSCALE
        if (g_cfg.aspect_mode != 4) {   
            Layout L = computeLayout(width, height, f.term_w, f.term_h, aspect);
            if (L.fit_w < static_cast<int>(width) && L.fit_h < static_cast<int>(height)) {
                out_w = static_cast<unsigned>(L.fit_w);
                out_h = static_cast<unsigned>(L.fit_h);
            }
        }
#endif
        size_t need = static_cast<size_t>(out_w) * out_h * 4;
        if (f.hw_frame_buf.size() < need) f.hw_frame_buf.resize(need);

        if (!f.gl.readPixelsXRGB8888(f.hw_frame_buf.data(), width, height, out_w, out_h))
            return;

        data   = f.hw_frame_buf.data();
        pitch  = static_cast<size_t>(out_w) * 4;
        fmt    = RETRO_PIXEL_FORMAT_XRGB8888;
        width  = out_w;     
        height = out_h;
    } else if (!data || !pitch) {
        return;
    }

    ConsoleCell* cells = f.display.GetBuffer();
    if (!cells) return;

    PerfTimer blit_timer;
    blit_timer.start();

    blitHalfBlock(f, data, width, height, pitch,
                  fmt,
                  cells, f.term_w, f.term_h,
                  aspect);

    double blit_elapsed = blit_timer.getElapsedMs();
    f.perf.blit_ms = f.perf.blit_ms * 0.95f + static_cast<float>(blit_elapsed) * 0.05f;
}
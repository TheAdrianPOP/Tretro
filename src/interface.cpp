#include "interface.h"
#include "file_system.h"
#include "path_utils.h"
#include "core_download_manager.h"
#include "config_manager.h"
#include "autodetect.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

template<typename T> static inline void rt_swap(T& a, T& b) { T tmp = a; a = b; b = tmp; }
static inline float rt_fmod(float val, float mod) { return (mod > 0.0f) ? (val - (int)(val / mod) * mod) : 0.0f; }

struct StrToken { const char* p; int len; };
static int splitPipes(const char* s, StrToken* tokens, int max_tokens) {
    int count = 0;
    while (*s && count < max_tokens) {
        const char* end = strchr(s, '|');
        int len = end ? (int)(end - s) : (int)strlen(s);
        while (len > 0 && s[0] == ' ') { s++; len--; }
        while (len > 0 && s[len - 1] == ' ') len--;
        if (len > 0) tokens[count++] = { s, len };
        s = end ? end + 1 : s + strlen(s);
    }
    return count;
}

static inline void putCellChecked(ImDrawState& ctx, int x, int y, ConsoleGlyph g, RGBColor fg, RGBColor bg, bool border = false) {
    if ((unsigned)x < (unsigned)ctx.screen_w && (unsigned)y < (unsigned)ctx.screen_h) {
        auto& c = (*ctx.buffer)[y][x];
        c.character = g; c.color = fg; c.is_ui = true; c.is_border = border;
        if (!bg.isReset()) c.bg_color = bg;
    }
}

struct PackedFont { uint16_t glyphs[96]; };

static constexpr PackedFont make_packed_font() {
    constexpr uint8_t raw[96][5] = {
        {0,0,0,0,0}, {2,2,2,0,2}, {5,5,0,0,0}, {5,7,5,7,5}, {3,6,3,6,2}, {4,1,2,4,1}, {6,6,7,5,3}, {2,2,0,0,0},
        {2,4,4,4,2}, {4,2,2,2,4}, {0,5,2,5,0}, {0,2,7,2,0}, {0,0,0,2,4}, {0,0,7,0,0}, {0,0,0,0,2}, {1,1,2,4,4},
        {3,5,5,5,6}, {2,6,2,2,2}, {6,1,2,4,7}, {6,1,2,1,6}, {5,5,7,1,1}, {7,4,6,1,6}, {3,4,7,5,3}, {7,1,2,4,4},
        {7,5,7,5,7}, {7,5,7,1,6}, {2,0,2,0,0}, {2,0,2,4,0}, {1,2,4,2,1}, {0,7,0,7,0}, {4,2,1,2,4}, {7,1,2,0,2},
        {2,5,7,4,3}, {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6}, {7,4,7,4,7}, {7,4,7,4,4}, {3,4,7,5,3},
        {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,2}, {5,5,6,5,5}, {4,4,4,4,7}, {5,7,7,5,5}, {5,7,7,7,5}, {2,5,5,5,2},
        {6,5,6,4,4}, {2,5,5,7,3}, {6,5,7,6,5}, {3,4,2,1,6}, {7,2,2,2,2}, {5,5,5,5,3}, {5,5,5,2,2}, {5,5,7,7,5},
        {5,5,2,5,5}, {5,5,2,2,2}, {7,1,2,4,7}, {7,4,4,4,7}, {4,2,1,0,0}, {7,1,1,1,7}, {2,5,0,0,0}, {0,0,0,0,7},
        {2,1,0,0,0}, {0,6,3,5,7}, {4,6,5,5,6}, {0,3,4,4,3}, {1,3,5,5,3}, {0,3,5,6,3}, {1,2,7,2,2}, {0,3,5,7,1},
        {4,6,5,5,5}, {2,0,2,2,2}, {1,0,1,1,5}, {4,5,6,6,5}, {6,2,2,2,7}, {0,7,7,7,5}, {0,6,5,5,5}, {0,2,5,5,2},
        {6,5,5,6,4}, {3,5,5,3,1}, {0,3,4,4,4}, {0,3,6,3,6}, {2,7,2,2,3}, {0,5,5,5,3}, {0,5,5,7,2}, {0,5,7,7,7},
        {0,5,2,2,5}, {5,5,3,1,2}, {0,7,3,6,7}, {3,2,4,2,3}, {2,2,0,2,2}, {6,2,1,2,6}, {3,6,0,0,0}, {7,5,5,5,7},
    };
    PackedFont pf = {};
    for (int i = 0; i < 96; i++) {
        pf.glyphs[i] = (uint16_t)((raw[i][0] << 12) | (raw[i][1] << 9) | (raw[i][2] << 6) | (raw[i][3] << 3) | raw[i][4]);
    }
    return pf;
}
static constexpr PackedFont kFont3x5 = make_packed_font();

const int AppSettings::default_key_mappings[BIND_COUNT] = {
    'Z', KEY_SPACE, KEY_ENTER, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    'X', 'S', 'A', 'Q', 'W', -1, -1, -1, -1,
    'W', 'S', 'A', 'D',
    'I', 'K', 'J', 'L'
};

const int AppSettings::default_gamepad_mappings[BIND_COUNT] = {
    0, 9, 8, 12, 13, 14, 15, 1, 3, 2, 4, 5, 6, 7, 10, 11,
    16, 17, 18, 19,
    20, 21, 22, 23
};

void AppSettings::resetBindings() {
    memcpy(key_mappings, default_key_mappings, sizeof(key_mappings));
    memcpy(gamepad_mappings, default_gamepad_mappings, sizeof(gamepad_mappings));
}

AppSettings::AppSettings() : recent_cores{}, recent_content{} { resetBindings(); }

const char* const RetroUI::tab_names[4]       = { "FILE", "OPTIONS", "GAME", "CONSOLE" };
const char* const RetroUI::tab_names_short[4] = { "FILE", "OPTS", "GAME", "LOGS" };

static const char kResetCore[]  = "Reset Core";
static const char kCtrlR[]      = "Ctrl+R";
static const char kCtrlQ[]      = "Ctrl+Q";
static const char kF1[]         = "F1";
static const char kRecentLogs[] = " RECENT LOGS ";

struct LogColorTag { const char* tag; RGBColor col; };
static const LogColorTag kLogColors[] = {
    { "[ERR]", Colors::ERR }, { "[wrn]", Colors::WARN }, { "[dbg]", Colors::BAR_FILL }, { "[osd]", Colors::OK }
};

static inline RGBColor getLogColor(const char* log, bool mono, bool sel) {
    if (mono || sel) return sel ? Colors::TAB_ACTIVE : Colors::BG;
    if (log && log[0] == '[') {
        for (const auto& lc : kLogColors) if (strstr(log, lc.tag)) return lc.col;
    }
    return Colors::TEXT_DEF;
}

enum TipState { TIP_BROWSER = 0, TIP_PICKER, TIP_DOWNLOADER, TIP_NOGAME, TIP_DEFAULT, TIP_STATE_COUNT };

static const char* getBarTip(bool gp, int tip, int var) {
    if (gp) {
        switch (tip) {
            case TIP_BROWSER:    return RetroUI::status_gp_browser;
            case TIP_PICKER:
            case TIP_DOWNLOADER: return RetroUI::status_gp_picker;
            case TIP_NOGAME:     return RetroUI::status_gp_no_game;
            default:
                return (var == 2) ? RetroUI::status_gp_default_vshort
                     : (var == 1) ? RetroUI::status_gp_default_short
                                  : RetroUI::status_gp_default;
        }
    }
    switch (tip) {
        case TIP_BROWSER:    return (var > 0) ? RetroUI::status_browser_short    : RetroUI::status_browser;
        case TIP_PICKER:
        case TIP_DOWNLOADER: return (var > 0) ? RetroUI::status_picker_short : RetroUI::status_picker;
        case TIP_NOGAME:     return RetroUI::status_no_game;
        default:
            return (var == 2) ? RetroUI::status_default_vshort
                 : (var == 1) ? RetroUI::status_default_short
                              : RetroUI::status_default;
    }
}

static int         s_tab_scroll         = 0;
static int         s_opt_scroll_left    = 0;
static int         s_opt_scroll_right   = 0;
static MenuSec     s_last_section       = MenuSec::FILE_;
static int         s_win_top            = 0;
static int         s_win_bot            = 0;
static const char* s_sticky_label       = nullptr;
static RGBColor    s_sticky_color       = Colors::STATUS_ACT;
static int         s_sticky_y_offset    = 0;
static int         s_total_logs_written = 0;
static int         s_last_processed_log = 0;
static bool        s_input_submenu      = false;

static uint64_t    s_prev_key_states[8] = {0};
static uint16_t    s_prev_gp_buttons    = 0;
static int         s_prev_mx = -1, s_prev_my = -1;
static bool        s_prev_mb = false;
static float       s_prev_ax[6] = {};   // lx, ly, rx, ry, lt, rt
static bool        s_prev_view_menu = false, s_wait_view_menu_release = false, s_was_open = false;
static int         s_dl_act = 0, s_game_act = 2;

static inline RGBColor monoSelFg(bool sel) { return sel ? Colors::TAB_ACTIVE : Colors::BG; }
static inline int getTabWidth(const char* name, bool big) { return (int)(strlen(name) + (big ? 2 : 4)) * (big ? 4 : 1); }

void RetroUI::drawShadow(int sx, int sy, int sw, int sh) {
    if (!state.buffer) return;
    const bool mono = is_mono;
    RGBColor s_col = mono ? Colors::INACTIVE : Colors::SHADOW, bg_col = mono ? Colors::MENU_BG : Colors::SHADOW;
    ConsoleGlyph g = ConsoleGlyph(mono ? kShadowRamp[2] : " ");
    for (int y = sy + 1; y <= sy + sh; ++y) putCellChecked(state, sx + sw, y, g, s_col, bg_col);
    int by = sy + sh;
    for (int x = sx + 1; x <= sx + sw; ++x) putCellChecked(state, x, by, g, s_col, bg_col);
}

void RetroUI::fillRect(int sx, int sy, int sw, int sh, RGBColor bg) {
    const RGBColor black{0,0,0};
    fillCells(sx, sy, sw, sh, ConsoleGlyph(" "), Colors::MENU_BG, is_mono ? black : bg, 0, state.screen_h);
    if (!big_mode) return;

    // Big mode: 1-cell solid frame.
    const ConsoleGlyph edge(is_mono ? kShadowRamp[4] : " ");
    const RGBColor ecol = is_mono ? Colors::TAB_ACTIVE : Colors::BORDER, ebg = is_mono ? black : Colors::BORDER;
    const int H = state.screen_h;
    fillCells(sx, sy,           sw, 1, edge, ecol, ebg, 0, H, !is_mono);
    fillCells(sx, sy + sh - 1,  sw, 1, edge, ecol, ebg, 0, H, !is_mono);
    fillCells(sx,           sy, 1, sh, edge, ecol, ebg, 0, H, !is_mono);
    fillCells(sx + sw - 1,  sy, 1, sh, edge, ecol, ebg, 0, H, !is_mono);
}

void RetroUI::drawBorderRect(int x, int y, int w, int h, RGBColor col, RGBColor bg, int div_y) {
    if (!state.buffer || big_mode) return;
    struct Corner { int dx, dy; ConsoleGlyph BorderStyle::*g; };
    static const Corner corners[4] = {
        { 0, 0, &BorderStyle::tl }, { 1, 0, &BorderStyle::tr },
        { 0, 1, &BorderStyle::bl }, { 1, 1, &BorderStyle::br }
    };
    for (const auto& c : corners) putCellChecked(state, x + (c.dx ? w - 1 : 0), y + (c.dy ? h - 1 : 0), border_style.*(c.g), col, bg);
    for (int cx = x + 1; cx < x + w - 1; ++cx) {
        putCellChecked(state, cx, y,         border_style.h, col, bg);
        putCellChecked(state, cx, y + h - 1, border_style.h, col, bg);
        if (div_y > 0 && div_y < h - 1) putCellChecked(state, cx, y + div_y, border_style.div, col, bg);
    }
    for (int cy = y + 1; cy < y + h - 1; ++cy) {
        putCellChecked(state, x,         cy, border_style.v, col, bg);
        putCellChecked(state, x + w - 1, cy, border_style.v, col, bg);
    }
}

void RetroUI::drawOverlayBox(int bx, int by, int bw, int bh, const char* title, int div_y) {
    const bool mono = is_mono;
    drawShadow(bx, by, bw, bh);
    fillRect(bx, by, bw, bh, Colors::BG);

    RGBColor title_fg = mono ? Colors::TAB_ACTIVE : Colors::TITLE;
    int clip_x = bx + bw - 1;
    if (!big_mode) {
        drawBorderRect(bx, by, bw, bh, mono ? Colors::TAB_ACTIVE : Colors::BORDER, mono ? RGBColor{0,0,0} : Colors::BG, div_y);
        DrawNormalText(bx + (bw - (int)strlen(title)) / 2, by, title, title_fg, mono ? RGBColor{0,0,0} : Colors::BG, clip_x);
    } else {
        if (div_y > 0 && div_y < bh - 1) {
            int dy = by + div_y;
            RGBColor bcol = mono ? Colors::TAB_ACTIVE : Colors::BORDER;
            ConsoleGlyph g = mono ? ConsoleGlyph(kShadowRamp[4]) : ConsoleGlyph(" ");
            for (int cx = bx + 1; cx < bx + bw - 1; ++cx) putCellChecked(state, cx, dy, g, bcol, bcol);
        }
        int title_x = bx + (bw - (int)strlen(title) * 4) / 2;
        drawScaledText(title_x, by + 1, title, title_fg, {-1, -1, -1}, clip_x);
    }
}

void RetroUI::DrawNormalText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int clip_end_x) {
    if (!state.buffer || y < s_win_top || y >= s_win_bot || y >= state.screen_h) return;
    auto& row = (*state.buffer)[y];
    for (int i = 0; str[i] && x + i <= clip_end_x; ++i) {
        int cx = x + i;
        if ((unsigned)cx >= (unsigned)state.screen_w) continue;
        auto& c = row[cx];
        c.character = ConsoleGlyph(str[i]); c.color = fg; c.is_ui = true; c.is_border = false;
        if (!bg.isReset()) c.bg_color = bg;
    }
}

void RetroUI::DrawBigText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int clip_end_x) {
    const ConsoleGlyph solid(is_mono ? kShadowRamp[4] : " "), blank(" ");
    const bool has_bg = !bg.isReset();
    const int max_x = fast_min_t(state.screen_w, clip_end_x);

    for (; *str && x < clip_end_x; ++str, x += 4) {
        unsigned ch = (unsigned char)*str;
        if (ch < 32 || ch > 127) continue;
        const uint16_t glyph_bits = kFont3x5.glyphs[ch - 32];

        for (int row = 0; row < 5; row++) {
            int py = y + row;
            if (py < s_win_top || py >= s_win_bot || py >= state.screen_h) continue;
            auto& row_cells = (*state.buffer)[py];
            int bits = (glyph_bits >> ((4 - row) * 3)) & 7;

            for (int col = 0; col < 3; col++) {
                int px = x + col;
                if ((unsigned)px >= (unsigned)max_x) continue;
                auto& c = row_cells[px];
                if (bits & (4 >> col)) {
                    c.character = solid;
                    c.color     = fg;
                    c.bg_color  = is_mono ? (has_bg ? bg : c.bg_color) : fg;
                } else if (has_bg) {
                    c.character = blank; c.bg_color = bg;
                } else continue;
                c.is_ui = true; c.is_border = false;
            }
        }
    }
}

// Fills a clipped rect (rows limited to [y_min, y_max)) with one glyph/colour pair.
void RetroUI::fillCells(int x, int y, int w, int h, const ConsoleGlyph& g, RGBColor fg, RGBColor bg, int y_min, int y_max, bool border) {
    if (!state.buffer) return;
    const int y0 = fast_max_t(y_min, y), y1 = fast_min_t(fast_min_t(y_max, state.screen_h), y + h);
    const int x0 = fast_max_t(0, x),     x1 = fast_min_t(state.screen_w, x + w);
    for (int py = y0; py < y1; ++py) {
        auto& row = (*state.buffer)[py];
        for (int cx = x0; cx < x1; ++cx) {
            auto& c = row[cx];
            c.character = g; c.color = fg; c.bg_color = bg; c.is_ui = true; c.is_border = border;
        }
    }
}

void RetroUI::FillHighlight(int x, int y, int w_fill, int h_fill, RGBColor bg, bool sel) {
    if (is_mono && !sel) return;
    fillCells(x, y, w_fill, h_fill, ConsoleGlyph(is_mono ? kShadowRamp[1] : " "),
              is_mono ? Colors::TAB_ACTIVE : RGBColor{0,0,0}, is_mono ? RGBColor{0,0,0} : bg, s_win_top, s_win_bot);
}

int RetroUI::computeVisibleWindow(int py, int ph, int& scroll_offset) {
    int item_h = getItemHeight();
    int visible = fast_max_t(1, (py + ph - 1 - state.cursor_y) / item_h);
    if (menu.selected_index < scroll_offset) scroll_offset = menu.selected_index;
    if (menu.selected_index >= scroll_offset + visible) scroll_offset = menu.selected_index - visible + 1;
    if (scroll_offset < 0) scroll_offset = 0;
    return visible;
}

void RetroUI::renderStickyHeader() {
    if (!s_sticky_label) return;
    int draw_y = s_win_top + s_sticky_y_offset;
    highlightRow(draw_y, Colors::BG, true);
    drawScaledText(state.window_x + 2, draw_y, s_sticky_label, s_sticky_color);
}

void RetroUI::snapViewport(int& row_y, int item_h) {
    int top_limit = s_win_top + (s_sticky_label ? item_h : 0);
    if (row_y < top_limit) {
        int diff = top_limit - row_y;
        s_tab_scroll -= diff; state.cursor_y += diff; row_y += diff;
    } else if (row_y + item_h > s_win_bot) {
        int diff = (row_y + item_h) - s_win_bot;
        s_tab_scroll += diff; state.cursor_y -= diff; row_y -= diff;
    }
    if (s_tab_scroll < 0) s_tab_scroll = 0;
}

void RetroUI::DrawUIText(const char* label, RGBColor fg) {
    int item_h = getItemHeight(), row_y = state.cursor_y;
    if (label && label[0] == '[' && !browser.active && (section == MenuSec::GAME)) {
        if (row_y <= s_win_top) { s_sticky_label = label; s_sticky_color = fg; s_sticky_y_offset = 0; state.cursor_y += item_h; return; }
        else if (row_y < s_win_top + item_h) s_sticky_y_offset = row_y - (s_win_top + item_h);
    }
    if (row_y + item_h > s_win_top && row_y < s_win_bot) drawScaledText(state.window_x + 2, row_y, label, fg);
    state.cursor_y += item_h;
}

// Divider cell: the thin glyph normally, a solid block (shaded in mono) in big mode.
void RetroUI::putLine(int x, int y, const ConsoleGlyph& thin) {
    ConsoleGlyph g = !big_mode ? thin : ConsoleGlyph(is_mono ? kShadowRamp[4] : " ");
    putCellChecked(state, x, y, g, is_mono ? Colors::TAB_ACTIVE : Colors::BORDER,
                   is_mono ? RGBColor{0,0,0} : (big_mode ? Colors::BORDER : Colors::BG));
}

void RetroUI::DrawSeparator() {
    int item_h = getItemHeight(), row_y = state.cursor_y;
    if (row_y + item_h > s_win_top && row_y < s_win_bot) {
        int draw_y = big_mode ? row_y + 2 : row_y;
        int x_end = fast_min_t(state.screen_w, state.window_x + state.window_w - 1);
        for (int i = state.window_x + 1; i < x_end; ++i) putLine(i, draw_y, border_style.div);
    }
    state.cursor_y += item_h;
}

void RetroUI::DrawSectionHeader(const char* title) {
    DrawSeparator();
    DrawUIText(title, Colors::STATUS_ACT);
}

void RetroUI::drawScaledText(int x, int y, const char* str, RGBColor fg) {
    drawScaledText(x, y, str, fg, RGBColor{-1, -1, -1}, -1);
}

void RetroUI::drawScaledText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int end_x) {
    if (end_x < 0) end_x = state.window_w > 0 ? (state.window_x + state.window_w - 1) : state.screen_w;
    if (big_mode) DrawBigText(x, y + 1, str, fg, bg, end_x);
    else          DrawNormalText(x, y, str, fg, bg, end_x);
}

void RetroUI::drawScrollingText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int clip_end_x, bool sel) {
    if (!str || !str[0]) return;
    int char_w = cw(), avail = (clip_end_x - x) / char_w, len = (int)strlen(str);
    if (avail <= 0) return;

    if (sel && len > avail) {
        int diff = len - avail;
        float pause = 1.0f, speed = 4.0f, scroll_time = (float)diff / speed;
        float phase = rt_fmod(m_text_scroll_timer, pause + scroll_time + pause);
        int off = 0;
        if (phase > pause && phase < pause + scroll_time) off = (int)((phase - pause) * speed);
        else if (phase >= pause + scroll_time)           off = diff;
        drawScaledText(x, y, str + fast_clamp(off, 0, diff), fg, bg, clip_end_x);
    } else {
        drawScaledText(x, y, str, fg, bg, clip_end_x);
    }
}

bool RetroUI::handleRowHover(int target_idx, int& row_y, int& end_x) {
    row_y = state.cursor_y; end_x = state.window_x + state.window_w - 1;
    int item_h = getItemHeight();
    int top_limit = s_win_top + (s_sticky_label ? item_h : 0);
    if (row_y >= top_limit && row_y + item_h <= s_win_bot && !m_keyboard_gamepad_active && hitTest(state.window_x, row_y, state.window_w, item_h)) {
        if (m_mouse_moved || m_mouse_clicked) menu.selected_index = target_idx;
        if (m_mouse_clicked) m_enter = true;
    }
    return (menu.selected_index == target_idx);
}

RGBColor RetroUI::selFg(bool sel, RGBColor idle) const {
    return is_mono ? monoSelFg(sel) : (sel ? Colors::TAB_ACTIVE : idle);
}

RGBColor RetroUI::selBg(bool sel, RGBColor idle) const {
    return is_mono ? Colors::MENU_BG : (sel ? Colors::STATUS_ACT : idle);
}

// Paints the row highlight and returns the text color to use on it.
RGBColor RetroUI::rowStyle(int row_y, bool sel, RGBColor idle_fg) {
    highlightRow(row_y, selBg(sel, Colors::BG), sel);
    return selFg(sel, idle_fg);
}

void RetroUI::resetNav() { menu.selected_index = s_tab_scroll = 0; }

// Queues a core (and optionally content) load; null content keeps the previous one.
// close=false leaves the menu open (it closes itself once the load succeeds).
void RetroUI::requestLoad(const char* core, const char* content, bool close) {
    safeCopy(pending_core, core);
    if (content) safeCopy(pending_content, content);
    pending_load = true;
    if (close) { core_picker.active = false; open = false; }
}

bool RetroUI::MenuOption(const char* label, const char* val) {
    int row_y, end_x;
    int target_idx = menu.item_count++;
    bool sel = handleRowHover(target_idx, row_y, end_x);
    if (sel && !browser.active) snapViewport(row_y, getItemHeight());

    if (row_y + getItemHeight() > s_win_top && row_y < s_win_bot) {
        RGBColor fg = rowStyle(row_y, sel, Colors::TEXT_DEF);
        int val_len = (val && val[0]) ? (int)strlen(val) : 0;
        int char_w  = cw();
        int start_x = state.window_x + (big_mode ? 3 : 2);
        int lbl_end = end_x - 1;

        if (val_len > 0) {
            int total_chars = fast_max_t(1, (end_x - start_x) / char_w);
            int val_chars   = fast_min_t(val_len, fast_max_t(6, (total_chars * 2) / 5));
            int val_x       = end_x - val_chars * char_w - 2;
            lbl_end         = val_x - (big_mode ? 2 : 1);
            RGBColor val_fg = (is_mono || sel) ? Colors::TAB_ACTIVE : Colors::BAR_FILL;
            drawScrollingText(val_x, row_y, val, val_fg, {-1, -1, -1}, end_x - 1, sel);
        }
        drawScrollingText(start_x, row_y, label, fg, {-1, -1, -1}, lbl_end, sel);
    }
    state.cursor_y += getItemHeight();
    return sel;
}

bool RetroUI::optionCycleInt(int& val, int step, int min_val, int max_val, bool left_dir, bool right_dir) {
    if (!left_dir && !right_dir) return false;
    val = fast_clamp(val + (left_dir ? -step : step), min_val, max_val);
    return true;
}

bool RetroUI::optToggle(const char* lbl, bool& val) {
    if (MenuOption(lbl, val ? "ON" : "OFF") && isTriggered()) {
        val = !val; cfgSave(); return true;
    }
    return false;
}

bool RetroUI::optCycle(const char* lbl, const char* const names[], int& val, int count) {
    if (MenuOption(lbl, names[val]) && isTriggered()) {
        val = (val + (m_left ? count - 1 : 1)) % count;
        cfgSave(); return true;
    }
    return false;
}

bool RetroUI::optPct(const char* lbl, int& val, int step, int lo, int hi) {
    char buf[16]; snprintf(buf, sizeof(buf), "%d%%", val);
    if (MenuOption(lbl, buf) && isTriggered()) {
        optionCycleInt(val, step, lo, hi, m_left, m_right);
        cfgSave(); return true;
    }
    return false;
}

static void cycle_core_var(CoreVar& cv, bool next) {
    if (cv.raw.empty()) return;
    const char* cur = !cv.current.empty() ? cv.current.c_str() : cv.def.c_str();

    StrToken tokens[32];
    int count = splitPipes(cv.raw.c_str(), tokens, 32);
    if (count <= 1) return;

    int cur_idx = 0, cur_len = (int)strlen(cur);
    for (int i = 0; i < count; i++) {
        if (tokens[i].len == cur_len && memcmp(tokens[i].p, cur, cur_len) == 0) {
            cur_idx = i; break;
        }
    }
    int next_idx = next ? (cur_idx + 1) % count : (cur_idx - 1 + count) % count;
    Frontend::get().setVar(cv.key.c_str(), DynString(tokens[next_idx].p, (size_t)tokens[next_idx].len).c_str());
}

static void getCoreOptionDisplay(const CoreVar& cv, char* out_lbl, size_t lbl_sz, char* out_val, size_t val_sz) {
    const char* src_lbl = !cv.desc.empty() ? cv.desc.c_str() : cv.key.c_str();
    char clean_lbl[256]; size_t l = 0;
    while (src_lbl[l] && l + 1 < sizeof(clean_lbl)) { clean_lbl[l] = src_lbl[l]; l++; }
    clean_lbl[l] = '\0';

    char* paren = strrchr(clean_lbl, '(');
    if (paren && paren > clean_lbl) {
        char temp[32]; safeCopy(temp, paren, sizeof(temp));
        for (char* t = temp; *t; ++t) if (*t >= 'A' && *t <= 'Z') *t = (char)(*t + 32);
        if (strstr(temp, "restart")) {
            while (paren > clean_lbl && *(paren - 1) == ' ') paren--;
            *paren = '\0';
        }
    }

    if (cv.desc.empty()) {
        char title_lbl[256]; size_t tj = 0; bool cap = true;
        for (size_t ti = 0; clean_lbl[ti] && tj + 1 < sizeof(title_lbl); ++ti) {
            char c = clean_lbl[ti];
            if (c == '_' || c == '-') { title_lbl[tj++] = ' '; cap = true; }
            else {
                if (cap && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                title_lbl[tj++] = c; cap = false;
            }
        }
        title_lbl[tj] = '\0';
        snprintf(out_lbl, lbl_sz, "  %s", title_lbl);
    } else {
        snprintf(out_lbl, lbl_sz, "  %s", clean_lbl);
    }

    const char* cur = !cv.current.empty() ? cv.current.c_str() : cv.def.c_str();
    bool found_label = false;

    if (!cv.raw_labels.empty()) {
        StrToken raw_toks[32], lbl_toks[32];
        int r_count = splitPipes(cv.raw.c_str(), raw_toks, 32);
        int l_count = splitPipes(cv.raw_labels.c_str(), lbl_toks, 32);
        int cur_len = (int)strlen(cur), limit = fast_min_t(r_count, l_count);
        for (int i = 0; i < limit; ++i) {
            if (raw_toks[i].len == cur_len && memcmp(raw_toks[i].p, cur, cur_len) == 0) {
                int copy_len = fast_min_t(lbl_toks[i].len, (int)val_sz - 1);
                memcpy(out_val, lbl_toks[i].p, copy_len);
                out_val[copy_len] = '\0';
                found_label = true; break;
            }
        }
    }

    if (!found_label) {
        struct BoolNorm { const char* match; const char* rep; };
        static const BoolNorm kNorms[] = {
            { "enabled", "Enabled" }, { "true", "Enabled" },
            { "disabled", "Disabled" }, { "false", "Disabled" },
            { "auto", "Auto" }, { "default", "Default" }
        };
        for (const auto& n : kNorms) {
            if (!strcmp(cur, n.match)) { safeCopy(out_val, n.rep, val_sz); found_label = true; break; }
        }
        if (!found_label) {
            safeCopy(out_val, cur, val_sz);
            if (out_val[0] >= 'a' && out_val[0] <= 'z') out_val[0] = (char)(out_val[0] - 'a' + 'A');
        }
    }
}

static const char kBrowserErr[] = "[Error reading dir]";


// Directories first, then case-insensitive by name (directory names carry a trailing '/').
static bool browserBefore(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    bool dir_a = la > 0 && a[la - 1] == '/', dir_b = lb > 0 && b[lb - 1] == '/';
    return dir_a != dir_b ? dir_a : strcasecmp(a, b) < 0;
}

// "812 B", "64 KB", "1.5 MB", "2.3 GB"
static void fmtSize(char* out, size_t sz, size_t bytes) {
    if (bytes < 1024)               snprintf(out, sz, "%u B", (unsigned)bytes);
    else if (bytes < 1024 * 1024)   snprintf(out, sz, "%u KB", (unsigned)(bytes >> 10));
    else {
        const bool gb = bytes >= ((size_t)1 << 30);
        const unsigned long long t = (unsigned long long)bytes * 10 >> (gb ? 30 : 20);
        snprintf(out, sz, "%u.%u %s", (unsigned)(t / 10), (unsigned)(t % 10), gb ? "GB" : "MB");
    }
}

// Lists browser.current_dir, or the drives when it is empty.
void RetroUI::browserScan() {
    browser.files.clear();
    browser.num_files = menu.selected_index = menu.scroll_offset = 0;

#ifdef _WIN32
    if (!browser.current_dir[0]) {
        DWORD mask = GetLogicalDrives();
        for (int d = 0; d < 26; ++d) {
            if (!(mask & (1u << d))) continue;
            const char name[4] = { (char)('A' + d), ':', '/', '\0' };
            browser.files.push_back(DynString(name));
        }
        browser.num_files = (int)browser.files.size();
        return;
    }
#endif

    auto entries = FS::listDir(browser.current_dir);
    browser.files.push_back(DynString(".."));
    if (entries.empty() && !FS::isDir(browser.current_dir)) {
        browser.files.push_back(DynString(kBrowserErr));
        browser.num_files = 2; return;
    }
    for (const auto& e : entries) browser.files.push_back(e.isDirectory ? (e.name + "/") : e.name);

    // In-place shell sort after "..": entries are moved, never copied.
    const int n = (int)browser.files.size();
    for (int g = n / 2; g > 0; g /= 2) {
        for (int i = 1 + g; i < n; ++i) {
            for (int j = i; j - g >= 1 && browserBefore(browser.files[j].c_str(), browser.files[j - g].c_str()); j -= g) {
                DynString t(static_cast<DynString&&>(browser.files[j]));
                browser.files[j]     = static_cast<DynString&&>(browser.files[j - g]);
                browser.files[j - g] = static_cast<DynString&&>(t);
            }
        }
    }
    browser.num_files = n;
    browser.sizes.resize(n);
    for (int i = 0; i < n; ++i) browser.sizes[i] = (size_t)-1;
}

// Cuts a path down to its parent folder in place ("C:/x" -> "C:/", "/x" -> "/"). False if it has no separator.
static bool cutParent(char* p) {
    char* s = strrchr(p, '/');
    char* b = strrchr(p, '\\');
    if (b > s) s = b;
    if (!s) return false;
    s[(s == p || (s == p + 2 && p[1] == ':')) ? 1 : 0] = '\0';
    return true;
}

// Parent folder, or the drive list above a drive root.
void RetroUI::browserUp() {
    char* dir = browser.current_dir;
    if (!dir[0]) return;                                          // already at the drive list
    if (!strchr(dir, '/') && !strchr(dir, '\\') && !(dir[1] == ':' && !dir[2])) {
        char abs_dir[512];
        makePathAbsolute(dir, abs_dir, sizeof(abs_dir));          // "." has no visible parent
        safeCopy(dir, abs_dir, sizeof(browser.current_dir));
    }
    size_t len = strlen(dir);
    while (len > 3 && (dir[len - 1] == '/' || dir[len - 1] == '\\')) dir[--len] = '\0';

    if (len <= 3 && dir[1] == ':') {
        dir[0] = '\0';                                            // "C:/" -> drives
    } else if (!cutParent(dir)) {
        return;
    }
    browserScan();
}

// Opens a listed folder; `name` carries the trailing '/'.
void RetroUI::browserEnter(const char* name) {
    if (!browser.current_dir[0]) {
        safeCopy(browser.current_dir, name);                      // drive: "C:/"
    } else {
        char sub[256];
        safeCopy(sub, name);
        sub[strlen(sub) - 1] = '\0';
        safeCopy(browser.current_dir, FS::join(browser.current_dir, sub).c_str(), sizeof(browser.current_dir));
    }
    browserScan();
}

void RetroUI::clearPicker() {
    core_picker.active = core_picker.no_match = false;
    core_picker.matched_count = core_picker.num_candidates = 0;
    core_picker.candidates.clear(); core_picker.library_names.clear(); core_picker.ext_counts.clear();
}

// Finds cores for content. The three callers differ only in log text and menu handling:
//   Browse  - file browser / recent list: show picker if anything was found
//   Startup - command-line content: always resets selection and keeps the menu open
//   Drop    - dragged/pasted path: a single match loads immediately
void RetroUI::tryPickCoreForContent(const char* content_path, PickMode mode) {
    core_picker.candidates.clear(); core_picker.library_names.clear(); core_picker.ext_counts.clear();
    core_picker.no_match = false;

    int matched = 0;
    const int n = autodetectCoreMulti(content_path, core_picker.candidates, &matched,
                                      &core_picker.library_names, &core_picker.ext_counts);
    Frontend& f = Frontend::get();

    if (mode == PickMode::Drop) {
        if (n == 0) { f.logMessage("[ERR] No cores installed for '%s'", content_path); return; }
        if (matched == 1) {
            const char* fn = getPathFilename(content_path);
            requestLoad(core_picker.candidates[0].c_str(), content_path);
            f.logMessage("[loader] Loading '%s' with core '%s'", fn ? fn : content_path, core_picker.candidates[0].c_str());
            return;
        }
    }

    safeCopy(core_picker.content_path, content_path);
    core_picker.num_candidates = n;
    core_picker.matched_count  = matched;
    browser.active = pending_load = false;

    if (n > 0 || mode == PickMode::Startup) {
        core_picker.no_match = (matched == 0);
        core_picker.scroll_offset = menu.selected_index = 0;
    }
    if (n > 0) {
        core_picker.active = open = true;
        if (mode == PickMode::Startup) f.logMessage("[loader] Showing core picker for: %s", content_path);
    } else if (mode == PickMode::Startup) {
        f.logMessage("[ERR] No cores found for: %s", content_path);
        open = true;
    } else {
        f.logMessage("[ERR] No cores found for '%s'", content_path);
    }
}

RetroUI::RetroUI() : UI() {
    // The file browser starts in the folder of the last loaded content.
    char last[512];
    safeCopy(last, g_cfg.num_recent_content > 0 ? g_cfg.recent_content[0] : "");
    if (cutParent(last) && FS::isDir(last)) safeCopy(browser.current_dir, last);

    open = true;
    cmd_buffer[0] = '\0';
    cmd_active = false;
}

void RetroUI::updateAnim(float dt) {
    float dir = open ? 1.f : -1.f;
    anim_progress = fast_clamp(anim_progress + dir * dt / 0.25f, 0.f, 1.f);
}

void RetroUI::update(float dt, const Vec3& pos, const Vec3& dir, float fps, const Vec3& vel, bool grounded, bool show_ui) {
    fps_display_value = fps;
    m_text_scroll_timer += dt;
    if (menu.selected_index != m_prev_selected_idx) {
        m_text_scroll_timer = 0.0f;
        m_prev_selected_idx = menu.selected_index;
    }

    Frontend& f = Frontend::get();
    if (m_last_log_head == -1) {
        m_last_log_head = f.console_log_head;
        m_last_log_count = s_total_logs_written = s_last_processed_log = f.console_log_count;
    } else {
        int new_logs = (f.console_log_head == m_last_log_head)
            ? (f.console_log_count - m_last_log_count)
            : (((f.console_log_head - m_last_log_head + kMaxConsoleLogs) % kMaxConsoleLogs) + (f.console_log_count - m_last_log_count));

        if (new_logs > 0) {
            s_total_logs_written += new_logs;
            m_transient_log_ttl = (m_transient_log_ttl <= 0.0f) ? 4.0f : fast_min_t(m_transient_log_ttl, 3.6f);
        }
        m_last_log_head = f.console_log_head;
        m_last_log_count = f.console_log_count;
    }

    if (m_transient_log_ttl > 0.0f) {
        m_transient_log_ttl = fast_max_t(0.0f, m_transient_log_ttl - dt);
        if (m_transient_log_ttl == 0.0f) s_last_processed_log = s_total_logs_written;
    }
}

void RetroUI::handleBack() {
    if (m_binding_button_id != -1) { m_binding_button_id = -1; m_binding_wait_release = false; return; }
    if (browser.active)            { browser.active = browser.append_disc_mode = false; menu.selected_index = 0; return; }
    if (core_picker.active) { clearPicker(); menu.selected_index = 0; return; }
    if (downloader.active) { downloader.active = false; menu.selected_index = 0; return; }
    if (s_input_submenu)   { s_input_submenu = false; resetNav(); return; }
}

static inline char applyShift(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch;
    if (ch >= '0' && ch <= '9') return ")!@#$%^&*("[ch - '0'];
    const char* p = strchr("-=[]\\;',./`", ch);
    return p ? "_+{}|:\"<>?~"[p - "-=[]\\;',./`"] : ch;
}

static bool anyAxisBeyond(const float* a, float thr) {
    for (int i = 0; i < 6; ++i) if (a[i] < -thr || a[i] > thr) return true;
    return false;
}

void RetroUI::handleInput() {
    m_up = m_down = m_left = m_right = m_enter = m_tab = false;
    m_home = m_end = m_pgup = m_pgdn = false;
    if (!input_ptr) return;

    bool key_pressed[KEY_COUNT] = {false}, any_kbd_event = false;
    for (int k = 0; k < KEY_COUNT; ++k) {
        bool down = input_ptr->isKeyDown(k), prev = (s_prev_key_states[k >> 6] & (1ULL << (k & 63))) != 0;
        key_pressed[k] = down && !prev;
        if (key_pressed[k]) any_kbd_event = true;
        if (down) s_prev_key_states[k >> 6] |= (1ULL << (k & 63));
        else      s_prev_key_states[k >> 6] &= ~(1ULL << (k & 63));
    }

    m_mouse_x = (int)input_ptr->getMousePosition().x;
    m_mouse_y = (int)input_ptr->getMousePosition().y;
    m_mouse_moved = (m_mouse_x != s_prev_mx || m_mouse_y != s_prev_my);
    s_prev_mx = m_mouse_x; s_prev_my = m_mouse_y;

    bool mb = input_ptr->isMouseButtonDown(MOUSE_LEFT);
    m_mouse_clicked = (mb && !s_prev_mb);
    s_prev_mb = mb;

    bool gp_pressed[16] = {false}, any_gp_event = false;
    const GamepadState& gp = globalGamepad().state(g_cfg.p1_gamepad_index);
    float axes[6];
    padAxes(gp, axes);
    if (gp.connected) {
        for (int b = 0; b < 16; ++b) {
            bool btn_down = gp.buttons[b];
            gp_pressed[b] = btn_down && !(s_prev_gp_buttons & (1 << b));
            if (gp_pressed[b]) any_gp_event = true;
            if (btn_down) s_prev_gp_buttons |= (1 << b);
            else          s_prev_gp_buttons &= ~(1 << b);
        }
        if (anyAxisBeyond(axes, 0.35f)) any_gp_event = true;
    } else {
        s_prev_gp_buttons = 0;
    }

    m_is_gamepad_mode = any_gp_event ? true : ((any_kbd_event || m_mouse_moved || m_mouse_clicked) ? false : m_is_gamepad_mode);

    if (m_binding_button_id != -1) {
        if (m_binding_wait_release) {
            bool any_held = false;
            for (int k = 0; k < KEY_COUNT && !any_held; ++k) if (input_ptr->isKeyDown(k)) any_held = true;
            if (gp.connected && !any_held) {
                for (int b = 0; b < 16 && !any_held; ++b) if (gp.buttons[b]) any_held = true;
                if (anyAxisBeyond(axes, 0.4f)) any_held = true;
            }
            if (!any_held) {
                m_binding_wait_release = false;
                memcpy(s_prev_ax, axes, sizeof(axes));
            }
            return;
        }

        int target_key = -1, target_gp = -1;
        for (int k = 0; k < KEY_COUNT; ++k) if (key_pressed[k]) { target_key = k; break; }

        if (target_key == -1 && gp.connected) {
            for (int b = 0; b < 16; ++b) if (gp_pressed[b]) { target_gp = b; break; }
            if (target_gp == -1) {
                for (int id = 16; id < 26; ++id) {
                    int ai = kAxisOfBind[id - 16];
                    float cur = axes[ai], prev = s_prev_ax[ai];
                    bool pos = gpBindPositive(id);
                    if (pos ? (prev <= 0.4f && cur > 0.6f) : (prev >= -0.4f && cur < -0.6f)) { target_gp = id; break; }
                }
                memcpy(s_prev_ax, axes, sizeof(axes));
            }
        }

        if (target_key != -1) {
            g_cfg.key_mappings[m_binding_button_id] = (target_key == KEY_ESC || target_key == KEY_BACKSPACE) ? -1 : target_key;
            m_binding_button_id = -1; cfgSave(); Frontend::get().saveGameConfig();
        } else if (target_gp != -1) {
            g_cfg.gamepad_mappings[m_binding_button_id] = target_gp;
            m_binding_button_id = -1; cfgSave(); Frontend::get().saveGameConfig();
        }
        m_keyboard_gamepad_active = true;
        return;
    }

    if (cmd_active) {
        bool shift = input_ptr->isKeyDown(KEY_LSHIFT) || input_ptr->isKeyDown(KEY_RSHIFT) || input_ptr->isKeyDown(KEY_SHIFT);
        for (int k = 32; k <= 126; ++k) {
            if (key_pressed[k]) {
                size_t len = strlen(cmd_buffer);
                if (len < sizeof(cmd_buffer) - 1) {
                    char ch = (char)k;
                    cmd_buffer[len] = (ch >= 'A' && ch <= 'Z') ? (shift ? ch : (ch | 32)) : (shift ? applyShift(ch) : ch);
                    cmd_buffer[len + 1] = '\0';
                }
            }
        }
        if (key_pressed[KEY_BACKSPACE]) { size_t len = strlen(cmd_buffer); if (len > 0) cmd_buffer[len - 1] = '\0'; }
        if (key_pressed[KEY_ESC])       cmd_active = false;
        if (key_pressed[KEY_ENTER])     { executeCommand(cmd_buffer); cmd_buffer[0] = '\0'; cmd_active = false; }
        return;
    }

    bool kbd_prev_tab = key_pressed[KEY_Q] && !input_ptr->isKeyDown(KEY_CONTROL);
    bool view_menu_down = gp.connected && gp.isDown(GamepadButton::Back) && gp.isDown(GamepadButton::Start);

    if (open && !s_was_open) {
        if (view_menu_down) s_wait_view_menu_release = true;
        Frontend& fe = Frontend::get();
        if (!standalone && fe.core.loaded()) fe.saveSRAM();
    }
    s_was_open = open;
    if (s_wait_view_menu_release && !view_menu_down) s_wait_view_menu_release = false;

    bool gp_view_menu = view_menu_down && !s_prev_view_menu && !s_wait_view_menu_release;
    s_prev_view_menu = view_menu_down;

    bool no_ctrl = !input_ptr->isKeyDown(KEY_CONTROL);
    m_up    = key_pressed[KEY_UP]    || (no_ctrl && key_pressed[KEY_W]) || input_ptr->getMouseWheelDelta() > 0 || (gp_pressed[(int)GamepadButton::DPadUp]    || gp.leftY < -0.5f);
    m_down  = key_pressed[KEY_DOWN]  || (no_ctrl && key_pressed[KEY_S]) || input_ptr->getMouseWheelDelta() < 0 || (gp_pressed[(int)GamepadButton::DPadDown]  || gp.leftY > 0.5f);
    m_left  = key_pressed[KEY_LEFT]  || (no_ctrl && key_pressed[KEY_A]) || (gp_pressed[(int)GamepadButton::DPadLeft]  || gp.leftX < -0.5f);
    m_right = key_pressed[KEY_RIGHT] || (no_ctrl && key_pressed[KEY_D]) || (gp_pressed[(int)GamepadButton::DPadRight] || gp.leftX > 0.5f);
    m_enter = key_pressed[KEY_ENTER] || gp_pressed[(int)GamepadButton::A];
    m_tab   = key_pressed[KEY_E] || key_pressed[KEY_TAB] || gp_pressed[(int)GamepadButton::RBumper];

    if (key_pressed[KEY_ESC]) handleBack();
    if (gp_view_menu && open) { open = false; menu.selected_index = 0; }
    if (gp_pressed[(int)GamepadButton::B] && (browser.active || core_picker.active || downloader.active || s_input_submenu || m_binding_button_id != -1)) {
        handleBack(); menu.selected_index = 0;
    }
    if (kbd_prev_tab || gp_pressed[(int)GamepadButton::LBumper]) {
        section = (MenuSec)(((int)section + 3) % 4); resetNav();
    }
    if (input_ptr->isKeyDown(KEY_CONTROL) && key_pressed[KEY_Q]) wants_quit = true;

    m_home = key_pressed[KEY_HOME]; m_end = key_pressed[KEY_END];
    m_pgup = key_pressed[KEY_PGUP]; m_pgdn = key_pressed[KEY_PGDN];

    if (m_up || m_down || m_left || m_right || m_tab || m_enter || kbd_prev_tab || m_home || m_end || m_pgup || m_pgdn) m_keyboard_gamepad_active = true;
    if (m_mouse_moved || m_mouse_clicked) m_keyboard_gamepad_active = false;
}

void RetroUI::executeCommand(const char* cmd) {
    Frontend& f = Frontend::get();
    f.logMessage("[user] %s", cmd);
    if (!strcmp(cmd, "help"))  f.logMessage("[user] Commands: help, clear");
    else if (!strcmp(cmd, "clear")) f.console_log_count = f.console_log_head = 0;
}

void RetroUI::handleNavigation() {
    if (!open) return;
    if (m_up && --menu.selected_index < 0) menu.selected_index = last_item_count > 0 ? last_item_count - 1 : 0;
    if (m_down && ++menu.selected_index >= last_item_count) menu.selected_index = 0;
    const int last = fast_max_t(0, last_item_count - 1);
    if (m_home) menu.selected_index = 0;
    if (m_end)  menu.selected_index = last;
    if (m_pgup) menu.selected_index = fast_max_t(0, menu.selected_index - m_last_visible);
    if (m_pgdn) menu.selected_index = fast_min_t(last, menu.selected_index + m_last_visible);
    if (m_tab) { section = (MenuSec)(((int)section + 1) % 4); resetNav(); }

    bool can_arrow_tab = (section != MenuSec::OPTIONS && section != MenuSec::GAME && !browser.active && !downloader.active && !core_picker.active && !s_input_submenu);
    if (can_arrow_tab && (m_left || m_right)) {
        section = (MenuSec)(((int)section + (m_left ? 3 : 1)) % 4); menu.selected_index = 0;
    }
}

void RetroUI::calculatePopupGeometry(int w, int h, float anim, int& px, int& py, int& pw, int& ph) {
    if (big_mode) {
        pw = (w < 40) ? (w - 2) : fast_clamp((w * 85) / 100, 120, w - 8);
        ph = (h < 10) ? (h - 2) : fast_clamp(h - 18, 32, h - 16);
    } else {
        pw = fast_clamp(w - 2, 0, (section == MenuSec::OPTIONS) ? 88 : 64);
        ph = fast_clamp(h - 4, 0, 22);
    }
    px = (w - pw) / 2;
    py = (h + 2) + (int)((((h - ph) / 2) - (h + 2)) * anim);
}

void RetroUI::renderBackground(int w, int h) {
    const bool mono = is_mono;
    auto& buffer = *state.buffer;
    int dim_div = ((int)g_cfg.color_mode == 2) ? 2 : 4;

    for (int y = 0; y < h; ++y) {
        auto& row = buffer[y];
        for (int x = 0; x < w; ++x) {
            auto& cell = row[x];
            if (mono) {
                for (int l = 1; l <= 4; l++) {
                    if (!strcmp(cell.character.data, kShadowRamp[l])) { cell.character = ConsoleGlyph(kShadowRamp[l - 1]); break; }
                }
            } else {
                if (!cell.bg_color.isReset()) { cell.bg_color.r /= dim_div; cell.bg_color.g /= dim_div; cell.bg_color.b /= dim_div; }
                if (!cell.color.isReset())    { cell.color.r /= dim_div;    cell.color.g /= dim_div;    cell.color.b /= dim_div; }
            }
        }
    }
}

// Draws the bar at bot_y with centred status text.
void RetroUI::drawBottomBar(int w, int h, int bot_y, const char* status) {
    fillBarRow(bot_y, barH(), w, h);
    drawScaledText(fast_max_t(0, (w - (int)strlen(status) * cw()) / 2), bot_y, status, Colors::TEXT_DEF, {-1, -1, -1}, w);
}

UI_NOINLINE void RetroUI::renderInGameBottomBar(int w, int h, float ease_slide) {
    const int bot_h = barH();
    const int bot_y = fast_max_t(0, h - (int)(bot_h * ease_slide + 0.5f));
    if (bot_y >= h) return;

    static const char* const kGpBar[] = { status_gp_ingame, status_gp_ingame_short };
    static const char* const kKbBar[] = { status_ingame, status_ingame_short, status_ingame_vshort };
    const bool gp = m_is_gamepad_mode;
    const char* const* list = gp ? kGpBar : kKbBar;
    const int n = gp ? 2 : 3;

    const char* status = gp ? status_gp_ingame_nogame : status_ingame_nogame;
    if (!standalone && Frontend::get().core.loaded()) {
        if (gp) status = list[n - 1];
        for (int i = 0; i < n; ++i) {
            if (w >= (int)strlen(list[i]) * cw() + 2) { status = list[i]; break; }
        }
    }
    drawBottomBar(w, h, bot_y, status);

    if (m_mouse_clicked && hitTest(0, bot_y, w, bot_h)) {
        open = true;
        m_mouse_clicked = false;
    }
}

void RetroUI::renderBottomBar(int w, int h, float anim) {
    const int bot_h = barH();
    const int bot_y = fast_max_t(0, h - bot_h + (int)((1.f - anim) * bot_h + 0.5f));
    if (bot_y >= h) return;

    int tip_idx = TIP_DEFAULT;
    if (browser.active)                             tip_idx = TIP_BROWSER;
    else if (core_picker.active || s_input_submenu) tip_idx = TIP_PICKER;
    else if (downloader.active)                     tip_idx = TIP_DOWNLOADER;
    else if (standalone || !Frontend::get().core.loaded()) tip_idx = TIP_NOGAME;

    drawBottomBar(w, h, bot_y, getBarTip(m_is_gamepad_mode, tip_idx, !big_mode ? 0 : (w < 172 ? 2 : 1)));
}

void RetroUI::fillBarRow(int y_start, int bar_h, int w, int h) {
    const RGBColor bar_bg = is_mono ? Colors::MENU_BG : Colors::STATUS_BG;
    if (big_mode && !is_mono) fillCells(0, y_start, w, bar_h, ConsoleGlyph(" "), RGBColor{0,0,0}, bar_bg, 0, h);
    else fillCells(0, y_start, w, bar_h, ConsoleGlyph(kShadowRamp[2]), is_mono ? Colors::STATUS_KEY : bar_bg, RGBColor{0,0,0}, 0, h);
}

void RetroUI::renderTopBar(int w, int h, float anim) {
    const int top_h = barH(), top_y = (int)((anim - 1.f) * top_h);
    fillBarRow(top_y, top_h, w, h);

    const char* const* tabs_ptrs = (big_mode && w < 192) ? tab_names_short : tab_names;
    int total_w = 0;
    for (int i = 0; i < 4; ++i) total_w += getTabWidth(tabs_ptrs[i], big_mode);
    int tx = fast_max_t(2, (w - total_w) / 2);

    for (int i = 0; i < 4; ++i) {
        int tw = getTabWidth(tabs_ptrs[i], big_mode);
        if (m_mouse_clicked && !browser.active && hitTest(tx, top_y, tw, top_h)) {
            section = (MenuSec)i; resetNav();
        }

        bool sel = (section == (MenuSec)i);
        RGBColor fg = selFg(sel, Colors::TAB_IDLE);
        RGBColor bg = selBg(sel, Colors::STATUS_BG);
        // Tab cell, then its label padded by 1 (big) or 2 (normal) characters.
        fillCells(tx, top_y, tw, top_h, ConsoleGlyph((is_mono && (!big_mode || !sel)) ? kShadowRamp[1] : " "), fg, bg, 0, h);
        if (big_mode) DrawBigText(tx + 4, top_y + 1, tabs_ptrs[i], fg, {-1,-1,-1}, w);
        else          DrawNormalText(tx + 2, top_y, tabs_ptrs[i], fg, {-1,-1,-1}, w - 1);
        tx += tw;
    }
}

void RetroUI::beginSection(const char* title, int px, int py, int pw, int ph) {
    Begin("", px, py, pw, ph, Colors::BORDER, Colors::BG);
    drawSectionTitle(title);
    DrawSeparator();
    s_win_top = state.cursor_y;
    state.cursor_y -= s_tab_scroll;
}

void RetroUI::drawSectionTitle(const char* title) {
    int item_h = getItemHeight(), row_y = state.cursor_y;
    if (row_y + item_h > s_win_top && row_y < s_win_bot) {
        int char_w = cw();
        int tx = state.window_x + (state.window_w - ((int)strlen(title) * char_w)) / 2;
        drawScaledText(tx, row_y, title, Colors::SELECTED);
    }
    state.cursor_y += item_h;
}

UI_NOINLINE void RetroUI::renderBrowser(int px, int py, int pw, int ph) {
    Begin("", px, py, pw, ph, Colors::BORDER, Colors::BG);
    drawSectionTitle(browser.append_disc_mode ? "APPEND DISC IMAGE" : "SELECT CONTENT");
    DrawUIText("Directory:", Colors::LABEL);
    DrawUIText(browser.current_dir[0] ? browser.current_dir : "This PC", {200,200,200});
    DrawSeparator();

    s_win_top = state.cursor_y;
    int visible = computeVisibleWindow(py, ph, menu.scroll_offset);
    const int size_w = 9 * cw();                                  // room for "1023.9 MB"

    for (int i = 0; i < visible && menu.scroll_offset + i < browser.num_files; ++i) {
        int idx = menu.scroll_offset + i;
        const char* name = browser.files[idx].c_str();
        size_t nlen = strlen(name);
        bool is_dir = nlen > 0 && name[nlen - 1] == '/';
        bool is_file = !is_dir && strcmp(name, "..") && strcmp(name, kBrowserErr);

        char display[256];
        if (is_dir) {
            char temp[256]; memcpy(temp, name, nlen); temp[nlen - 1] = '\0';
            snprintf(display, sizeof(display), "  [%s]", temp);
        } else {
            snprintf(display, sizeof(display), "  %s", name);
        }

        int row_y, end_x;
        bool sel = handleRowHover(idx, row_y, end_x);
        RGBColor fg = rowStyle(row_y, sel, (is_dir ? Colors::BAR_FILL : Colors::TEXT_DEF));
        drawScrollingText(state.window_x + 2, row_y, display, fg, {-1, -1, -1}, end_x - (is_file ? size_w + 2 : 0), sel);

        if (is_file) {
            size_t& sz = browser.sizes[idx];
            if (sz == (size_t)-1) sz = FS::fileSize(FS::join(browser.current_dir, name).c_str());
            char buf[16];
            fmtSize(buf, sizeof(buf), sz);
            drawScaledText(end_x - 2 - (int)strlen(buf) * cw(), row_y, buf, sel ? fg : Colors::LABEL);
        }
        state.cursor_y += getItemHeight();

        if (sel && m_enter) {
            if (!strcmp(name, "..")) {
                browserUp();
                break;
            } else if (!strcmp(name, kBrowserErr)) {
                // not a real entry
            } else if (is_dir) {
                browserEnter(name);
                break;
            } else {
                char full_path[512];
                safeCopy(full_path, FS::join(browser.current_dir, name).c_str(), sizeof(full_path));
                if (browser.append_disc_mode) {
                    browser.active = browser.append_disc_mode = false;
                    Frontend& fe = Frontend::get();
                    if (fe.disk_control.appendImage(full_path)) fe.logMessage("[inf] Appended: %s", name);
                    else fe.logMessage("[ERR] Append failed: %s", name);
                    open = false;
                } else {
                    tryPickCoreForContent(full_path);
                }
            }
        }
    }
    menu.item_count = browser.num_files;
    End();
}

UI_NOINLINE void RetroUI::renderCorePicker(int px, int py, int pw, int ph) {
    Begin("", px, py, pw, ph, Colors::BORDER, Colors::BG);
    char title[256];
    snprintf(title, sizeof(title), "CHOOSE CORE FOR: %s", getPathFilename(core_picker.content_path));
    drawSectionTitle(title);

    static const char* const picker_hints[] = { "Multiple cores matched:", "Matched cores first:", "No match - select core:" };
    int hint = core_picker.no_match ? 2 : (core_picker.matched_count < core_picker.num_candidates ? 1 : 0);
    DrawUIText(picker_hints[hint], Colors::LABEL);
    DrawSeparator();

    int total_items = core_picker.num_candidates;
    s_win_top = state.cursor_y;
    int visible = computeVisibleWindow(py, ph, core_picker.scroll_offset);

    for (int i = 0; i < visible && core_picker.scroll_offset + i < total_items; ++i) {
        int idx = core_picker.scroll_offset + i;
        const char* path = core_picker.candidates[idx].c_str();
        const char* lib  = core_picker.library_names[idx].c_str();

        bool unmatched = (!core_picker.no_match && idx >= core_picker.matched_count);
        char display[160], core_name[128];
        CoreDownloadManager::stripCoreExtension(getPathFilename(path), core_name, sizeof(core_name));

        const char* prefix = unmatched ? "  [Unmatched] " : "  ";
        if (lib && lib[0]) {
            snprintf(display, sizeof(display), "%s%s  [%s]", prefix, lib, core_name);
        } else {
            char readable[128];
            CoreDownloadManager::buildCoreDisplayName(core_name, readable, sizeof(readable));
            snprintf(display, sizeof(display), "%s%s", prefix, readable);
        }

        int row_y, end_x;
        bool sel = handleRowHover(idx, row_y, end_x);
        RGBColor fg = rowStyle(row_y, sel, (unmatched ? Colors::INACTIVE : Colors::TEXT_DEF));
        drawScrollingText(state.window_x + 2, row_y, display, fg, {-1, -1, -1}, end_x, sel);
        state.cursor_y += getItemHeight();

        if (sel && m_enter) {
            m_enter = false;
            requestLoad(path, core_picker.content_path);
        }
    }
    menu.item_count = total_items;
    End();
}

struct BtnDef { const char* big_lbl; const char* lbl; uint8_t big_w; uint8_t w; };
static const BtnDef kDownloaderBtns[] = {
    { "Run",    "[ Run ]",    12, 7  },
    { "Update", "[ Update ]", 24, 10 },
    { "Remove", "[ Remove ]", 24, 10 },
};

UI_NOINLINE static void executeDownloaderAction(RetroUI& ui, CoreDownloadManager& downloader_mgr, int idx, int s_act, bool inst, const CoreInfo& core) {
    if (idx == 0) { downloader_mgr.refreshCoreList(); return; }
    if (downloader_mgr.isDownloadActive()) return;

    if (!inst) {
        downloader_mgr.startDownload(core);
    } else {
        int act = !core.local ? s_act : (s_act == 0 ? 0 : 2);
        switch (act) {
            case 0: {
                char path[512];
                downloader_mgr.getCoreFilePath(core.name, path, sizeof(path));
                ui.requestLoad(path);
                break;
            }
            case 1: downloader_mgr.startDownload(core); break;
            case 2: downloader_mgr.deleteCore(core.name); break;
        }
    }
}

UI_NOINLINE void RetroUI::renderDownloader(int px, int py, int pw, int ph) {
    CoreDownloadManager& downloader_mgr = CoreDownloadManager::get();
    downloader_mgr.resetDownloadStatus();

    Begin("", px, py, pw, ph, Colors::BORDER, Colors::BG);
    drawSectionTitle("DOWNLOAD/REMOVE CORES");

    if (!downloader_mgr.isCoreListLoading() && !downloader_mgr.isCoreListLoaded() && !downloader_mgr.hasCoreListFailed()) {
        downloader_mgr.refreshCoreList();
    }
    if (downloader_mgr.isCoreListLoading()) {
        DrawUIText("Fetching core list...", Colors::LABEL);
        End(); return;
    }
    if (downloader_mgr.hasCoreListFailed()) {
        DrawUIText("Failed to reach buildbot.", Colors::LABEL); DrawSeparator();
        if (MenuOption("Retry", "") && m_enter) { m_enter = false; downloader_mgr.refreshCoreList(); }
        End(); return;
    }

    if (downloader_mgr.isCoreListOffline()) {
        DrawUIText("Offline (local cores only):", Colors::LABEL);
    } else {
        DrawUIText("Select core:", Colors::LABEL);
    }
    DrawSeparator();

    int core_count = downloader_mgr.getCoreCount(), total_items = core_count + 1;
    s_win_top = state.cursor_y; int visible = computeVisibleWindow(py, ph, downloader.scroll_offset);
    int cur_idx = menu.selected_index;
    CoreInfo cur_sel = (cur_idx >= 1 && cur_idx - 1 < core_count) ? downloader_mgr.getCoreInfo(cur_idx - 1) : CoreInfo{};
    bool cur_inst = (cur_idx >= 1 && cur_idx - 1 < core_count) && downloader_mgr.isCoreInstalled(cur_sel.name);
    int cur_acts = !cur_inst ? 1 : (!cur_sel.local ? 3 : 2);

    if (!cur_inst) s_dl_act = 0;
    else if (m_left)  s_dl_act = (s_dl_act + cur_acts - 1) % cur_acts;
    else if (m_right) s_dl_act = (s_dl_act + 1) % cur_acts;
    if (s_dl_act >= cur_acts) s_dl_act = 0;

    for (int i = 0; i < visible && downloader.scroll_offset + i < total_items; ++i) {
        int idx = downloader.scroll_offset + i;
        int row_y, end_x;
        bool sel = handleRowHover(idx, row_y, end_x);
        CoreInfo cur_core = (idx > 0) ? downloader_mgr.getCoreInfo(idx - 1) : CoreInfo{};
        bool inst = (idx > 0) && downloader_mgr.isCoreInstalled(cur_core.name);
        const char* label = (idx == 0) ? "Refresh Core List" : cur_core.desc;
        RGBColor fg = rowStyle(row_y, sel, idx == 0 ? Colors::BAR_FILL : Colors::TEXT_DEF);

        // Row being downloaded: "1.2 MB / 3.4 MB" (just the received size if the total is unknown).
        const bool dl_row = idx > 0 && downloader_mgr.isDownloadActive() && !strcmp(downloader_mgr.getActiveCoreName(), cur_core.name);
        char s_str[40] = "";
        if (dl_row) {
            char got[16], tot[16];
            fmtSize(got, sizeof(got), (size_t)downloader_mgr.getDownloadedBytes());
            if (downloader_mgr.getDownloadTotal() > 0) {
                fmtSize(tot, sizeof(tot), (size_t)downloader_mgr.getDownloadTotal());
                snprintf(s_str, sizeof(s_str), "%s / %s", got, tot);
            } else {
                safeCopy(s_str, got);
            }
        }
        drawScaledText(state.window_x + (big_mode ? 10 : 4), row_y, label, fg, {-1, -1, -1},
                       dl_row ? end_x - (int)strlen(s_str) * cw() - 3 : -1);

        if (idx > 0) {
            if (dl_row) {
                drawScaledText(end_x - (int)strlen(s_str) * cw() - 2, row_y, s_str, Colors::OK);
                if (sel && !m_keyboard_gamepad_active && m_mouse_clicked) m_enter = false;
            } else if (!inst) {
                int bw = big_mode ? 32 : 12, bx = end_x - (big_mode ? 34 : 14);
                bool btn_hit = hitTest(bx, row_y, bw, getItemHeight());
                if (sel && !m_keyboard_gamepad_active && m_mouse_clicked) m_enter = btn_hit;
                RGBColor bcol = (sel && (btn_hit || m_keyboard_gamepad_active)) ? Colors::TAB_ACTIVE : Colors::BAR_FILL;
                drawScaledText(bx, row_y, big_mode ? "Download" : "[ Download ]", bcol);
            } else {
                int b_indices[3];
                int num_b = 0;
                if (!cur_core.local) {
                    b_indices[0] = 0; b_indices[1] = 1; b_indices[2] = 2;
                    num_b = 3;
                } else {
                    b_indices[0] = 0; b_indices[1] = 2;
                    num_b = 2;
                }

                int b_xs[3], b_ws[3], xr = end_x, spacing = big_mode ? 2 : 3;
                for (int d = num_b - 1; d >= 0; --d) {
                    int bi = b_indices[d];
                    b_ws[d] = big_mode ? kDownloaderBtns[bi].big_w : kDownloaderBtns[bi].w;
                    xr -= b_ws[d] + spacing; b_xs[d] = xr;
                }
                if (sel && !m_keyboard_gamepad_active) {
                    bool btn_hit = false;
                    for (int d = 0; d < num_b; ++d) {
                        if (hitTest(b_xs[d], row_y, b_ws[d], getItemHeight())) {
                            if (m_mouse_moved || m_mouse_clicked) s_dl_act = d;
                            btn_hit = true; break;
                        }
                    }
                    if (m_mouse_clicked) m_enter = btn_hit;
                }
                for (int d = 0; d < num_b; ++d) {
                    int bi = b_indices[d];
                    RGBColor bcol = (sel && s_dl_act == d) ? Colors::TAB_ACTIVE : Colors::INACTIVE;
                    drawScaledText(b_xs[d], row_y, big_mode ? kDownloaderBtns[bi].big_lbl : kDownloaderBtns[bi].lbl, bcol);
                }
            }
        }

        if (sel && m_enter) {
            m_enter = false;
            executeDownloaderAction(*this, downloader_mgr, idx, s_dl_act, inst, cur_core);
            if (idx == 0) { menu.item_count = total_items; End(); return; }
        }
        state.cursor_y += getItemHeight();
    }
    menu.item_count = total_items;
    End();
}

enum class FileAct : uint8_t { Browser, Downloader, Reset, Exit };
struct FileOpt { const char* label; const char* sc; FileAct act; };
static const FileOpt kFileOpts[] = {
    { "Load Content",          "",     FileAct::Browser    },
    { "Download/Remove Cores", "",     FileAct::Downloader },
    { kResetCore,              kCtrlR, FileAct::Reset      },
    { "Exit Emulator",         kCtrlQ, FileAct::Exit       }
};

UI_NOINLINE void RetroUI::renderFileSection(int px, int py, int pw, int ph) {
    AppSettings& cfg = g_cfg;
    beginSection("FILE", px, py, pw, ph);

    for (int i = 0; i < 4; ++i) {
        if (i == 2) DrawSeparator();
        const auto& fo = kFileOpts[i];
        if (MenuOption(fo.label, m_is_gamepad_mode ? "" : fo.sc) && m_enter) {
            m_enter = false;
            if (fo.act == FileAct::Browser) {
                browser.active = true; browser.append_disc_mode = false; browserScan();
            } else if (fo.act == FileAct::Downloader) {
                downloader.active = true; downloader.scroll_offset = menu.selected_index = 0;
            } else if (fo.act == FileAct::Reset && !standalone) {
                Frontend::get().core.reset(); open = false;
            } else if (fo.act == FileAct::Exit) {
                wants_quit = true;
            }
        }
    }

    struct RecentDef { const char* title; char (*arr)[512]; int count; bool is_core; };
    const RecentDef recents[2] = {
        { "Recent Cores:",   cfg.recent_cores,   cfg.num_recent_cores,   true  },
        { "Recent Content:", cfg.recent_content, cfg.num_recent_content, false }
    };
    for (const auto& r : recents) {
        DrawSeparator(); DrawUIText(r.title, Colors::LABEL);
        for (int i = 0; i < r.count; ++i) {
            if (MenuOption(getPathFilename(r.arr[i]), "") && m_enter) {
                if (r.is_core) requestLoad(r.arr[i], nullptr, false);
                else           { tryPickCoreForContent(r.arr[i]); }
            }
        }
    }
    End();
}

static const char* const rmodes[]     = {"Auto (GL)", "Software"};
static const char* const cmodes[]     = {"TrueColor", "ANSI-256", "ANSI-16", "ANSI-8", "Mono"};
static const char* const amodes[]     = {"Default", "4:3", "8:7", "Stretch", "1:1"};
static const char* const font_modes[] = {"Classic 1:2", "Modern Wide"};
static const char* const mscales[]    = {"Auto", "Normal", "Large"};

enum class OptKind : uint8_t { Header, SectionHeader, Toggle, Cycle, Pct };
enum class PostOptAction : uint8_t { None, UpdateAudio, UpdateRenderer };

static const char* const* const kChoiceSets[] = { rmodes, cmodes, amodes, font_modes, mscales };

struct AppOptionEntry {
    const char*   label;
    uint16_t      offset;
    OptKind       kind;
    uint8_t       step_or_count;
    uint8_t       min_val;
    uint8_t       max_val;
    PostOptAction post_action;
    uint8_t       choice;        // index into kChoiceSets
};

static const AppOptionEntry kAppOptions[] = {
    { " [ VIDEO ]", 0, OptKind::Header, 0, 0, 0, PostOptAction::None, 0 },
    { "  Renderer", (uint16_t)offsetof(AppSettings, renderer_mode), OptKind::Cycle, 2, 0, 0, PostOptAction::UpdateRenderer, 0 },
    { "  Color Mode", (uint16_t)offsetof(AppSettings, color_mode), OptKind::Cycle, 5, 0, 0, PostOptAction::None, 1 },
    { "  Pixel Mode", (uint16_t)offsetof(AppSettings, pixel_mode), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
    { "  Aspect Ratio", (uint16_t)offsetof(AppSettings, aspect_mode), OptKind::Cycle, 5, 0, 0, PostOptAction::None, 2 },
    { "  Terminal Font", (uint16_t)offsetof(AppSettings, font_width), OptKind::Cycle, 2, 0, 0, PostOptAction::None, 3 },
    { "  Show Statistics", (uint16_t)offsetof(AppSettings, show_stats_overlay), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
    { "  Show OSD Logs", (uint16_t)offsetof(AppSettings, show_log_overlay), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
    { "  Show Bottom Bar", (uint16_t)offsetof(AppSettings, show_bottom_bar), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
    { "  Dithering", (uint16_t)offsetof(AppSettings, dither_enabled), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
    { "  Brightness", (uint16_t)offsetof(AppSettings, brightness), OptKind::Pct, 10, 0, 200, PostOptAction::None, 0 },
    { "  Contrast", (uint16_t)offsetof(AppSettings, contrast), OptKind::Pct, 10, 0, 200, PostOptAction::None, 0 },
    { " [ AUDIO ]", 0, OptKind::SectionHeader, 0, 0, 0, PostOptAction::None, 0 },
    { "  Audio Output", (uint16_t)offsetof(AppSettings, audio_enabled), OptKind::Toggle, 0, 0, 0, PostOptAction::UpdateAudio, 0 },
    { "  Master Volume", (uint16_t)offsetof(AppSettings, volume_pct), OptKind::Pct, 5, 0, 100, PostOptAction::UpdateAudio, 0 },
    { " [ SYSTEM ]", 0, OptKind::SectionHeader, 0, 0, 0, PostOptAction::None, 0 },
    { "  Pause in Menu", (uint16_t)offsetof(AppSettings, pause_in_menu), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
    { "  Menu Scale", (uint16_t)offsetof(AppSettings, menu_scale), OptKind::Cycle, 3, 0, 0, PostOptAction::None, 4 },
    { "  Save on Exit", (uint16_t)offsetof(AppSettings, auto_save_exit), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
    { "  Load on Start", (uint16_t)offsetof(AppSettings, auto_load_start), OptKind::Toggle, 0, 0, 0, PostOptAction::None, 0 },
};

UI_NOINLINE void RetroUI::renderOptionsSection(int px, int py, int pw, int ph) {
    AppSettings& cfg = g_cfg;
    Begin("", px, py, pw, ph, Colors::BORDER, Colors::BG);
    drawSectionTitle("OPTIONS");
    DrawSeparator();

    int content_top = state.cursor_y, total_w = pw - 2;
    int col1_w = (total_w - 1) / 2, col1_x = px + 1, div_x = col1_x + col1_w;
    int col2_x = div_x + 1, col2_w = px + pw - 1 - col2_x;

    for (int y = content_top; y < py + ph - 1; ++y) putLine(div_x, y, border_style.v);

    int header_h = getItemHeight();
    drawScaledText(col1_x + (big_mode ? 3 : 2), content_top, "[ APP SETTINGS ]", Colors::STATUS_ACT, {-1,-1,-1}, div_x - 1);
    drawScaledText(col2_x + (big_mode ? 3 : 2), content_top, "[ CORE SETTINGS ]", Colors::STATUS_ACT, {-1,-1,-1}, px + pw - 2);

    int sep_y = content_top + header_h;
    for (int x = col1_x; x < px + pw - 1; ++x) putLine(x, sep_y, border_style.div);

    int list_top = sep_y + (big_mode ? 2 : 1);
    s_win_top = list_top; s_win_bot = py + ph - 1;

    state.window_x = col1_x; state.window_w = col1_w;
    s_tab_scroll = s_opt_scroll_left; state.cursor_y = list_top - s_opt_scroll_left;

    for (const auto& opt : kAppOptions) {
        if (opt.kind == OptKind::Toggle && opt.offset == offsetof(AppSettings, dither_enabled) && cfg.color_mode == ColorMode::TrueColor) {
            continue;
        }
        if (opt.kind == OptKind::Header)        { DrawUIText(opt.label, Colors::STATUS_ACT); continue; }
        if (opt.kind == OptKind::SectionHeader) { DrawSectionHeader(opt.label); continue; }

        char* base = (char*)&cfg;
        bool changed = false;
        if (opt.kind == OptKind::Toggle)       changed = optToggle(opt.label, *(bool*)(base + opt.offset));
        else if (opt.kind == OptKind::Cycle)  changed = optCycle(opt.label, kChoiceSets[opt.choice], *(int*)(base + opt.offset), opt.step_or_count);
        else if (opt.kind == OptKind::Pct)    changed = optPct(opt.label, *(int*)(base + opt.offset), opt.step_or_count, opt.min_val, opt.max_val);

        if (changed) {
            if (opt.post_action == PostOptAction::UpdateAudio && !standalone) {
                Frontend::get().audio.setMasterVolume(cfg.audio_enabled ? cfg.volume_pct / 100.f : 0.f);
            } else if (opt.post_action == PostOptAction::UpdateRenderer) {
                Frontend& fe = Frontend::get();
                if (!standalone && fe.core.loaded()) {
                    requestLoad(fe.core_path.c_str(), fe.content_path.c_str());
                }
            }
        }
    }
    s_opt_scroll_left = s_tab_scroll;

    state.window_x = col2_x; state.window_w = col2_w;
    s_tab_scroll = s_opt_scroll_right; state.cursor_y = list_top - s_opt_scroll_right;

    if (standalone) {
        DrawUIText("  Unavailable in standalone.", Colors::INACTIVE);
    } else {
        Frontend& f = Frontend::get();
        if (f.core_vars.empty()) {
            DrawUIText("  No core options.", Colors::INACTIVE);
        } else {
            for (size_t i = 0; i < f.core_vars.size(); ++i) {
                CoreVar& cv = f.core_vars[i];
                char key_lbl[128], val_str[64];
                getCoreOptionDisplay(cv, key_lbl, sizeof(key_lbl), val_str, sizeof(val_str));
                if (MenuOption(key_lbl, val_str) && isTriggered()) cycle_core_var(cv, !m_left);
            }
        }
    }
    s_opt_scroll_right = s_tab_scroll;

    if (s_opt_scroll_left > 0)  drawScaledText(col1_x + col1_w - (big_mode ? 6 : 2), content_top, "▲", Colors::LABEL);
    if (s_opt_scroll_right > 0) drawScaledText(px + pw - (big_mode ? 8 : 3), content_top, "▲", Colors::LABEL);

    state.window_x = px; state.window_w = pw;
    End();
}

static const char* const kBtnLabels[2][5] = {
    { "[ < ]", "[ > ]", "[ Load ]", "[ Save ]", "[ Delete ]" },
    { "<",     ">",     "Load",     "Save",     "Delete"     }
};
static const uint8_t kBtnW[2][5] = {
    { 5, 5, 8, 8, 10 },
    { 8, 8, 16, 16, 24 }
};

// Names live in one packed, NUL-separated blob (parallel to the id array).
static const char* nthName(const char* packed, int n) {
    while (n-- > 0) packed += strlen(packed) + 1;
    return packed;
}

static const char* getKeyName(int key) {
    if (key == -1) return "None";
    static char s_buf[16];

    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
        s_buf[0] = (char)key; s_buf[1] = '\0';
        return s_buf;
    }
    if (key >= KEY_F1 && key <= KEY_F12) {
        snprintf(s_buf, sizeof(s_buf), "F%d", key - KEY_F1 + 1);
        return s_buf;
    }
    if (key >= KEY_KP0 && key <= KEY_KP9) {
        snprintf(s_buf, sizeof(s_buf), "KP %d", key - KEY_KP0);
        return s_buf;
    }

    static const int16_t kKeyIds[] = {
        KEY_SPACE, KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_TAB, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_INSERT, KEY_DELETE, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_LSHIFT, KEY_RSHIFT,
        KEY_SHIFT, KEY_LCONTROL, KEY_RCONTROL, KEY_CONTROL, KEY_LALT, KEY_RALT, KEY_ALT, KEY_CAPSLOCK,
        KEY_NUMLOCK, KEY_SCROLLOCK, KEY_PRINT, KEY_PAUSE,
        KEY_GRAVE, KEY_MINUS, KEY_EQUALS, KEY_LBRACKET, KEY_RBRACKET,
        KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_COMMA,
        KEY_PERIOD, KEY_SLASH, KEY_KP_PERIOD, KEY_KP_DIVIDE,
        KEY_KP_MULTIPLY, KEY_KP_MINUS, KEY_KP_PLUS,
        KEY_KP_ENTER, KEY_KP_EQUALS
    };
    static const char kKeyNames[] =
        "SPACE\0ENTER\0ESC\0BACK\0TAB\0UP\0DOWN\0LEFT\0RIGHT\0"
        "INSERT\0DEL\0HOME\0END\0PGUP\0PGDN\0LSHIFT\0RSHIFT\0"
        "SHIFT\0LCTRL\0RCTRL\0CTRL\0LALT\0RALT\0ALT\0CAPS\0"
        "NUM\0SCROLL\0PRINT\0PAUSE\0"
        "`\0-\0=\0[\0]\0"
        "\\\0;\0'\0,\0"
        ".\0/\0KP .\0KP /\0"
        "KP *\0KP -\0KP +\0"
        "KP ENTER\0KP =";

    for (unsigned i = 0; i < sizeof(kKeyIds) / sizeof(kKeyIds[0]); ++i) {
        if (kKeyIds[i] == key) return nthName(kKeyNames, (int)i);
    }

    snprintf(s_buf, sizeof(s_buf), "Key %d", key);
    return s_buf;
}

static const char* getGpName(int btn_id) {
    if (btn_id == -1) return "None";
    static const char kNames[] =
        "Btn A\0Btn B\0Btn X\0Btn Y\0L Bumper\0R Bumper\0L Trigger\0R Trigger\0"
        "Start\0Back\0L Stick Click\0R Stick Click\0D-Up\0D-Down\0D-Left\0D-Right\0"
        "LS Up\0LS Down\0LS Left\0LS Right\0RS Up\0RS Down\0RS Left\0RS Right\0LT Axis\0RT Axis";
    return (btn_id >= 0 && btn_id < 26) ? nthName(kNames, btn_id) : "Unknown";
}

struct BindEntry { const char* label; uint8_t id; };
static const BindEntry kBtnBinds[] = {
    { "  D-Pad Up", BIND_JOYPAD_UP }, { "  D-Pad Down", BIND_JOYPAD_DOWN },
    { "  D-Pad Left", BIND_JOYPAD_LEFT }, { "  D-Pad Right", BIND_JOYPAD_RIGHT },
    { "  A Button", BIND_JOYPAD_A }, { "  B Button", BIND_JOYPAD_B },
    { "  X Button", BIND_JOYPAD_X }, { "  Y Button", BIND_JOYPAD_Y },
    { "  Start", BIND_JOYPAD_START }, { "  Select", BIND_JOYPAD_SELECT },
    { "  L Shoulder", BIND_JOYPAD_L }, { "  R Shoulder", BIND_JOYPAD_R },
    { "  L Trigger", BIND_JOYPAD_L2 }, { "  R Trigger", BIND_JOYPAD_R2 },
    { "  L Thumb Click", BIND_JOYPAD_L3 }, { "  R Thumb Click", BIND_JOYPAD_R3 },
};

static const BindEntry kLeftStickBinds[] = {
    { "  Left Stick Up", BIND_ANALOG_LEFT_UP }, { "  Left Stick Down", BIND_ANALOG_LEFT_DOWN },
    { "  Left Stick Left", BIND_ANALOG_LEFT_LEFT }, { "  Left Stick Right", BIND_ANALOG_LEFT_RIGHT },
};

static const BindEntry kRightStickBinds[] = {
    { "  Right Stick Up", BIND_ANALOG_RIGHT_UP }, { "  Right Stick Down", BIND_ANALOG_RIGHT_DOWN },
    { "  Right Stick Left", BIND_ANALOG_RIGHT_LEFT }, { "  Right Stick Right", BIND_ANALOG_RIGHT_RIGHT },
};

struct BindSection { const char* header; const BindEntry* entries; uint8_t count; };
static const BindSection kBindSections[] = {
    { "[ CONTROLS: D-PAD & BUTTONS ]", kBtnBinds,       (uint8_t)(sizeof(kBtnBinds) / sizeof(kBtnBinds[0])) },
    { "[ LEFT ANALOG STICK ]",         kLeftStickBinds,  (uint8_t)(sizeof(kLeftStickBinds) / sizeof(kLeftStickBinds[0])) },
    { "[ RIGHT ANALOG STICK ]",        kRightStickBinds, (uint8_t)(sizeof(kRightStickBinds) / sizeof(kRightStickBinds[0])) },
};

UI_NOINLINE void RetroUI::renderGameSection(int px, int py, int pw, int ph) {
    AppSettings& cfg = g_cfg;
    Frontend& f = Frontend::get();
    bool has_game = !standalone && f.core.loaded();
    if (!has_game) s_input_submenu = false;

    if (s_input_submenu && has_game) {
        beginSection("INPUT CONFIGURATION", px, py, pw, ph);
        DrawUIText("[ GENERAL SENSITIVITY ]", Colors::STATUS_ACT);
        optPct("  Cursor Sensitivity", cfg.mouse_sensitivity, 10, 50, 400);

        static const char* const kPadLabels[2] = { "  P1 Controller Device", "  P2 Controller Device" };
        for (int p = 0; p < 2; ++p) {
            int& pad = p ? cfg.p2_gamepad_index : cfg.p1_gamepad_index;
            char p_buf[16];
            snprintf(p_buf, sizeof(p_buf), "Pad #%d", pad + 1);
            if (MenuOption(kPadLabels[p], p_buf) && isTriggered()) {
                optionCycleInt(pad, 1, 0, 3, m_left, m_right); cfgSave();
            }
        }

        for (const auto& sec : kBindSections) {
            DrawSectionHeader(sec.header);
            for (uint8_t i = 0; i < sec.count; ++i) {
                char val[128]; uint8_t bid = sec.entries[i].id;
                if (m_binding_button_id == bid) {
                    snprintf(val, sizeof(val), m_is_gamepad_mode ? "PRESS BUTTON/STICK..." : "PRESS KEY/STICK...");
                } else {
                    snprintf(val, sizeof(val), "%s / %s", getKeyName(cfg.key_mappings[bid]), getGpName(cfg.gamepad_mappings[bid]));
                }
                if (MenuOption(sec.entries[i].label, val) && m_enter) {
                    m_enter = false; m_binding_button_id = bid; m_binding_wait_release = true;
                }
            }
        }

        DrawSectionHeader("[ PROFILE DEFAULTS ]");
        if (MenuOption("  Reset All Binds to Default", "") && m_enter) {
            m_enter = false;
            cfg.resetBindings();
            cfgSave(); Frontend::get().saveGameConfig();
        }
        renderStickyHeader();
        End();
        return;
    }

    beginSection("GAME", px, py, pw, ph);
    if (!has_game) {
        DrawUIText("[ QUICK LAUNCH ]", Colors::STATUS_ACT);
        PodVector<DetectedSave> saves;
        scanSaveStates(f.save_dir.c_str(), "", "", saves);
        if (saves.empty()) {
            DrawUIText("  No saved games found.", Colors::INACTIVE);
        } else {
            for (const auto& ds : saves) {
                char title[128]; snprintf(title, sizeof(title), "  %s", ds.display_name[0] ? ds.display_name : getPathFilename(ds.path));
                if (MenuOption(title, ds.time_buf) && m_enter) {
                    m_enter = false;
                    if (getMetaPaths(ds.path, pending_core, sizeof(pending_core), pending_content, sizeof(pending_content))) {
                        safeCopy(pending_state_to_load, ds.path, sizeof(pending_state_to_load));
                        pending_load = true; open = false;
                        f.logMessage("[inf] Quick launching %s...", ds.display_name);
                    }
                }
            }
        }
        End();
        return;
    }

    if (MenuOption("Resume Game", m_is_gamepad_mode ? "View+Menu" : kF1) && m_enter) open = false;
    if (MenuOption(kResetCore, m_is_gamepad_mode ? "" : kCtrlR) && m_enter) { f.core.reset(); open = false; }

    DrawSectionHeader("[ STATES ]");
    char path[512];
    getSlotPath(path, sizeof(path), f.save_dir.c_str(), f.content_path.c_str(), f.core_path.c_str(), cfg.default_slot);
    bool exists = FS::exists(path);

    PodVector<DetectedSave> cur_saves;
    scanSaveStates(f.save_dir.c_str(), f.content_path.c_str(), f.core_path.c_str(), cur_saves);

    const char* time_str = exists ? "Saved" : "(Empty)";
    for (const auto& s : cur_saves) {
        if (s.slot_num == cfg.default_slot && s.time_buf[0]) { time_str = s.time_buf; break; }
    }

    int row_y, end_x;
    bool sel = handleRowHover(menu.item_count++, row_y, end_x);
    if (sel && !browser.active) snapViewport(row_y, getItemHeight());

    int bm = big_mode ? 1 : 0;
    int xp = state.window_x + (bm ? 30 : 11), xn = xp + (bm ? 22 : 10);
    int bx_del = end_x - (bm ? 24 : 10);
    int bx_save = exists ? (bx_del - (bm ? 20 : 10)) : (end_x - (bm ? 16 : 8));
    int bx_load = bx_save - (bm ? 20 : 10);

    int b_ids[5] = { 0, 1, 2, 3, 4 }, b_xs[5] = { xp, xn, bx_load, bx_save, bx_del };
    int n_btns = exists ? 5 : 3;
    if (!exists) { b_ids[2] = 3; b_xs[2] = bx_save; }

    if (s_game_act >= n_btns) s_game_act = n_btns - 1;

    if (sel) {
        if (m_left)  s_game_act = (s_game_act + n_btns - 1) % n_btns;
        if (m_right) s_game_act = (s_game_act + 1) % n_btns;

        if (!m_keyboard_gamepad_active && (m_mouse_moved || m_mouse_clicked)) {
            for (int i = 0; i < n_btns; ++i) {
                if (hitTest(b_xs[i], row_y, kBtnW[bm][b_ids[i]], getItemHeight())) {
                    s_game_act = i; if (m_mouse_clicked) m_enter = true; break;
                }
            }
        }
        if (input_ptr) {
            for (int k = '0'; k <= '9'; ++k) {
                if (input_ptr->isKeyDown(k)) { cfg.default_slot = k - '0'; cfgSave(); break; }
            }
        }
    }

    if (row_y + getItemHeight() > s_win_top && row_y < s_win_bot) {
        RGBColor fg = rowStyle(row_y, sel, Colors::TEXT_DEF);

        drawScaledText(state.window_x + (bm ? 4 : 2), row_y, "  Slot", fg);
        char slot_c[2] = { (char)('0' + cfg.default_slot), 0 };
        drawScaledText(xp + (bm ? 10 : 6), row_y, slot_c, Colors::TAB_ACTIVE);
        drawScaledText(xn + (bm ? 12 : 7), row_y, time_str, Colors::LABEL, {-1,-1,-1}, b_xs[2] - 2);

        for (int i = 0; i < n_btns; ++i) {
            RGBColor col = (sel && s_game_act == i) ? Colors::TAB_ACTIVE : Colors::INACTIVE;
            drawScaledText(b_xs[i], row_y, kBtnLabels[bm][b_ids[i]], col);
        }

        if (sel && m_enter) {
            m_enter = false; int act = b_ids[s_game_act];
            if (act < 2) {
                cfg.default_slot = (cfg.default_slot + (act == 0 ? 9 : 1)) % 10; cfgSave();
            } else {
                if (act == 2)      f.loadState(path);
                else if (act == 3) f.saveState(path);
                else if (act == 4) {
                    remove(path);
                    char meta[520]; snprintf(meta, sizeof(meta), "%s.meta", path);
                    remove(meta);
                }
                static const char* const kSlotLogs[] = { "Loaded", "Saved", "Deleted" };
                f.logMessage("[inf] %s slot %d", kSlotLogs[act - 2], cfg.default_slot);
            }
        }
    }
    state.cursor_y += getItemHeight();

    if (f.disk_control.supported) {
        DrawSectionHeader("[ DISC CONTROL ]");
        if (MenuOption("  Tray Status", f.disk_control.getEject() ? "Open" : "Closed") && isTriggered()) {
            m_enter = false; f.disk_control.toggleEject();
            f.logMessage("[inf] Tray %s", f.disk_control.getEject() ? "open" : "closed");
        }

        unsigned num_discs = f.disk_control.getNumImages();
        if (num_discs > 0) {
            DrawUIText("  Discs:", Colors::LABEL);
            unsigned cur_disc = f.disk_control.getImageIndex();
            for (unsigned d = 0; d < num_discs; ++d) {
                char label[128] = "", item[48], val[140];
                f.disk_control.getImageLabel(d, label, sizeof(label));
                snprintf(item, sizeof(item), "    Disc %u/%u", d + 1, num_discs);
                snprintf(val,  sizeof(val),  "%s%s", label, (d == cur_disc) ? " [ACTIVE]" : "");

                if (MenuOption(item, val) && isTriggered()) {
                    m_enter = false;
                    if (d != cur_disc) {
                        f.disk_control.changeDisc(d);
                        char next_label[128] = "";
                        f.disk_control.getImageLabel(d, next_label, sizeof(next_label));
                        f.logMessage("[inf] Disc %u/%u: %s", d + 1, num_discs, next_label);
                    }
                }
            }
        } else {
            DrawUIText("  Active Disc: None", Colors::INACTIVE);
        }

        if (MenuOption("  Append Disc Image", "Browse...") && m_enter) {
            m_enter = false; browser.active = browser.append_disc_mode = true; browserScan();
        }
    }

    DrawSectionHeader("[ CONTROLS ]");
    if (MenuOption("  Inputs", "Configure...") && m_enter) {
        m_enter = false; s_input_submenu = true; resetNav();
    }
    renderStickyHeader();
    End();
}

UI_NOINLINE void RetroUI::renderConsoleSection(int px, int py, int pw, int ph) {
    const bool mono = is_mono;
    beginSection("CONSOLE", px, py, pw, ph);

    Frontend& f = Frontend::get();
    int item_h = getItemHeight(), original_win_bot = s_win_bot, reserve_h = big_mode ? 10 : 2;
    s_win_bot = original_win_bot - reserve_h;

    if (f.console_log_count == 0) {
        DrawUIText("No logs.", Colors::INACTIVE);
    } else {
        for (int i = 0; i < f.console_log_count; ++i) {
            int idx = (f.console_log_head + i) % kMaxConsoleLogs, row_y, end_x;
            int target_idx = menu.item_count++;
            bool sel = handleRowHover(target_idx, row_y, end_x);
            if (sel && !browser.active) snapViewport(row_y, item_h);

            if (row_y + item_h > s_win_top && row_y < s_win_bot) {
                RGBColor fg = getLogColor(f.console_logs[idx], mono, sel);
                RGBColor bg = mono ? RGBColor{-1,-1,-1} : (sel ? Colors::STATUS_ACT : Colors::BG);
                highlightRow(row_y, bg, sel);
                drawScaledText(state.window_x + 2, row_y, f.console_logs[idx], fg, bg, end_x);
            }
            state.cursor_y += item_h;
        }
    }

    s_win_bot = original_win_bot;
    state.cursor_y = s_win_bot - reserve_h;
    DrawSeparator();

    int draw_h = big_mode ? 7 : item_h;
    state.cursor_y = s_win_bot - draw_h;
    int row_y = state.cursor_y;

    int clear_w = big_mode ? 28 : 9, text_w = state.window_w - clear_w - 2;
    int clear_x = state.window_x + 1 + text_w;
    int textbox_idx = menu.item_count++, clear_idx = menu.item_count++;

    if (m_left && menu.selected_index == clear_idx)   { menu.selected_index = textbox_idx; m_left = false; }
    if (m_right && menu.selected_index == textbox_idx) { menu.selected_index = clear_idx;   m_right = false; }

    if (!m_keyboard_gamepad_active) {
        if (hitTest(state.window_x, row_y, text_w, item_h)) {
            if (m_mouse_moved || m_mouse_clicked) menu.selected_index = textbox_idx;
            if (m_mouse_clicked) cmd_active = true;
        } else if (hitTest(clear_x, row_y, clear_w, item_h)) {
            if (m_mouse_moved || m_mouse_clicked) menu.selected_index = clear_idx;
            if (m_mouse_clicked) m_enter = true;
        } else if (m_mouse_clicked) {
            cmd_active = false;
        }
    }

    auto renderBox = [&](int bx, int bw, int idx, const char* str, RGBColor def_fg) {
        bool sel = (menu.selected_index == idx);
        FillHighlight(bx, row_y, bw, draw_h, mono ? Colors::MENU_BG : (sel ? Colors::STATUS_ACT : Colors::BG), sel);
        drawScaledText(bx + (big_mode ? 4 : (idx == textbox_idx ? 2 : 1)), row_y, str,
                       mono ? monoSelFg(sel) : (sel ? Colors::TAB_ACTIVE : def_fg), {-1,-1,-1}, bx + bw - 1);
    };

    char cmd_display[160];
    if (cmd_active) snprintf(cmd_display, sizeof(cmd_display), "> %s_", cmd_buffer);
    else            snprintf(cmd_display, sizeof(cmd_display), "Message...");

    renderBox(state.window_x + 1, text_w, textbox_idx, cmd_display, Colors::TEXT_DEF);
    renderBox(clear_x, clear_w, clear_idx, big_mode ? "Clear" : "[Clear]", Colors::ERR);

    if (menu.selected_index == textbox_idx && m_enter) { m_enter = false; cmd_active = !cmd_active; }
    if (menu.selected_index == clear_idx && m_enter)   { m_enter = false; f.console_log_count = f.console_log_head = 0; }

    state.cursor_y += draw_h;
    End();
}

void RetroUI::Begin(const char* title, int x, int y, int w, int h, const RGBColor& border_col, const RGBColor& bg_col) {
    UI::Begin(x, y, w, h);
    state.cursor_x = x + 2; state.cursor_y = y + 1;

    RGBColor actual_bg = bg_col.isReset() ? Colors::BG : bg_col;
    fillRect(x, y, w, h, actual_bg);

    const bool mono = is_mono;
    if (!border_col.isReset() && !big_mode) {
        drawBorderRect(x, y, w, h, mono ? Colors::TAB_ACTIVE : border_col, mono ? RGBColor{0,0,0} : actual_bg);
    }
    if (title && title[0]) {
        int len = (int)strlen(title), tx = x + (w - len) / 2;
        RGBColor title_bg = mono ? RGBColor{0,0,0} : (big_mode ? border_col : actual_bg);
        for (int i = 0; i < len; ++i) UI::SetCell(tx + i, y, title[i], Colors::TITLE, title_bg, true, false);
    }
}

UI_NOINLINE void RetroUI::renderStatsOverlay(int w, int h, float ease_slide) {
    const bool mono = is_mono;
    const auto& p = Frontend::get().perf;
    float pure_core_ms = fast_max_t(0.0f, p.core_ms - p.blit_ms);
    const char* render_str = (g_cfg.renderer_mode == 1) ? "SW Only" : (Frontend::get().gl.active ? "Headless GL" : "Software");

    int box_w = big_mode ? 108 : 27, box_h = big_mode ? 60 : 12;
    int target_x = w - box_w - 2, offscreen_x = w + 2;
    int box_x = fast_max_t(0, offscreen_x + (int)((target_x - offscreen_x) * ease_slide));
    int box_y = (2 + box_h > h) ? 0 : 2;

    drawOverlayBox(box_x, box_y, box_w, box_h, " STATS ");

    RGBColor text_col = mono ? Colors::TAB_ACTIVE : Colors::TEXT_DEF;
    RGBColor sub_col  = mono ? Colors::TAB_ACTIVE : Colors::LABEL;

    int step   = big_mode ? 5 : 1;
    int ly     = big_mode ? (box_y + 8) : (box_y + 1);
    int draw_x = box_x + (big_mode ? 8 : 2);
    int clip_x = box_x + box_w - (big_mode ? 4 : 2);

    auto drawLine = [&](const char* text, RGBColor col) {
        drawScaledText(draw_x, ly, text, col, {-1, -1, -1}, clip_x);
        ly += step;
    };

    char buf[64];
    snprintf(buf, sizeof(buf), "FPS:     %5.1f / %-5.1f", p.fps, fps_display_value); drawLine(buf, text_col);
    snprintf(buf, sizeof(buf), "Frame:   %5.1f ms", p.frame_ms); drawLine(buf, text_col);

    static const char* const kSubLabels[4] = { " - Emul: ", " - Blit: ", " - Flush:", " - Pacing:" };
    const float kSubVals[4] = { pure_core_ms, p.blit_ms, p.render_ms, p.sleep_ms };
    for (int i = 0; i < 4; ++i) {
        snprintf(buf, sizeof(buf), "%-9s%5.1f ms", kSubLabels[i], kSubVals[i]);
        drawLine(buf, sub_col);
    }

    snprintf(buf, sizeof(buf), "Cells:   %4d / %-4d", (int)p.changed_cells, w * h); drawLine(buf, text_col);
    snprintf(buf, sizeof(buf), "Payload: %5.1f KB/f", p.bytes_sent / 1024.0f); drawLine(buf, text_col);
    snprintf(buf, sizeof(buf), "Terminal:%3dx%-3d", w, h); drawLine(buf, text_col);
    snprintf(buf, sizeof(buf), "Render:  %-14s", render_str); drawLine(buf, text_col);
}

UI_NOINLINE void RetroUI::renderLogOverlay(float ease_slide) {
    const bool mono = is_mono;
    Frontend& f = Frontend::get();
    int logs_to_show = fast_clamp(s_total_logs_written - s_last_processed_log, 0, fast_min_t(4, f.console_log_count));
    if (logs_to_show <= 0) return;

    float log_slide = 1.0f;
    if (m_transient_log_ttl < 0.4f) log_slide = m_transient_log_ttl / 0.4f;
    else if (4.0f - m_transient_log_ttl < 0.4f) log_slide = (4.0f - m_transient_log_ttl) / 0.4f;
    log_slide = fast_clamp(log_slide, 0.0f, 1.0f);
    float final_log_ease = ease_slide * (log_slide * log_slide * (3.0f - 2.0f * log_slide));
    if (final_log_ease <= 0.05f) return;

    int start_idx = fast_max_t(0, f.console_log_count - logs_to_show);
    int actual_show = f.console_log_count - start_idx;

    int log_box_w = big_mode ? 120 : 35, log_box_h = big_mode ? (actual_show + 2) * 6 : (actual_show + 2);
    int target_log_x = 2, offscreen_log_x = -log_box_w - 2;
    int log_box_x = offscreen_log_x + (int)((target_log_x - offscreen_log_x) * final_log_ease);

    drawOverlayBox(log_box_x, 2, log_box_w, log_box_h, kRecentLogs);

    int x_off = big_mode ? 8 : 2, step = big_mode ? 6 : 1;
    int cur_y = 2 + (barH()), clip_x = log_box_x + log_box_w - (big_mode ? 4 : 2);

    for (int i = 0; i < actual_show; ++i) {
        int idx = (f.console_log_head + start_idx + i) % kMaxConsoleLogs;
        const char* log = f.console_logs[idx];
        drawScaledText(log_box_x + x_off, cur_y, log, getLogColor(log, mono, false), {-1,-1,-1}, clip_x);
        cur_y += step;
    }
}

void RetroUI::render(PodVector<PodVector<ConsoleCell>>& buffer, int w, int h) {
    if (w <= 0 || h <= 0) return;
    AppSettings& cfg = g_cfg;
    state.buffer = &buffer; state.screen_w = w; state.screen_h = h; menu.item_count = 0;
    state.window_x = 0; state.window_w = w;

    if (section != s_last_section) {
        s_tab_scroll = s_opt_scroll_left = s_opt_scroll_right = 0;
        s_last_section = section;
        downloader.active = browser.active = core_picker.active = s_input_submenu = false;
    }

    is_mono = (cfg.color_mode == ColorMode::Monochrome);
#ifndef TRETRO_NO_BIG_UI
    big_mode = uiBigMode(w, h);
#endif
    s_win_top = 0; s_win_bot = h;

    handleInput();

    if (anim_progress <= 0.0f) {
        if (cfg.show_stats_overlay) renderStatsOverlay(w, h, 1.0f);
        if (cfg.show_log_overlay && m_transient_log_ttl > 0.0f) renderLogOverlay(1.0f);
        if (cfg.show_bottom_bar) renderInGameBottomBar(w, h, 1.0f);
        return;
    }

    float anim = 1.f - (1.f - anim_progress) * (1.f - anim_progress) * (1.f - anim_progress);
    float slide_factor = 1.0f - anim_progress;
    float ease_slide = slide_factor * slide_factor * slide_factor;

    float ingame_factor = fast_clamp(1.0f - anim_progress * 2.0f, 0.0f, 1.0f);
    float ease_ingame   = ingame_factor * ingame_factor * (3.0f - 2.0f * ingame_factor);

    float menu_factor   = fast_clamp((anim_progress - 0.5f) * 2.0f, 0.0f, 1.0f);
    float ease_menu     = menu_factor * menu_factor * (3.0f - 2.0f * menu_factor);

    handleNavigation();
    renderBackground(w, h);

    if (cfg.show_stats_overlay && ease_slide > 0.05f) renderStatsOverlay(w, h, ease_slide);
    if (cfg.show_log_overlay && m_transient_log_ttl > 0.0f) renderLogOverlay(ease_slide);

    int px, py, pw, ph;
    calculatePopupGeometry(w, h, anim, px, py, pw, ph);
    m_last_visible = fast_max_t(1, (ph - 4) / getItemHeight());

    if (m_mouse_clicked) {
        int top_h = barH();
        int bot_h = barH();
        bool in_top_bar = hitTest(0, (int)((anim - 1.f) * top_h), w, top_h);
        bool in_bot_bar = (ease_menu > 0.0f) && hitTest(0, h - bot_h + (int)((1.f - ease_menu) * bot_h + 0.5f), w, bot_h);
        if (!in_top_bar && !in_bot_bar && !hitTest(px, py, pw, ph)) {
            handleBack();
            if (open && !browser.active && !core_picker.active && !downloader.active && !s_input_submenu) open = false;
        }
    }

    s_win_top = py + 1; s_win_bot = py + ph - 1; s_sticky_label = nullptr; s_sticky_color = Colors::STATUS_ACT; s_sticky_y_offset = 0;
    drawShadow(px, py, pw, ph);
    fillRect(px, py, pw, ph, Colors::BG);

    if      (browser.active)      renderBrowser(px, py, pw, ph);
    else if (core_picker.active)  renderCorePicker(px, py, pw, ph);
    else if (downloader.active)   renderDownloader(px, py, pw, ph);
    else switch (section) {
        case MenuSec::FILE_:    renderFileSection(px, py, pw, ph);                 break;
        case MenuSec::OPTIONS:  renderOptionsSection(px, py, pw, ph);              break;
        case MenuSec::GAME:     renderGameSection(px, py, pw, ph);                 break;
        case MenuSec::CONSOLE:  renderConsoleSection(px, py, pw, ph); break;
        default: break;
    }

    s_win_top = 0; s_win_bot = h;
    last_item_count = menu.item_count;

    renderTopBar(w, h, anim);

    if (cfg.show_bottom_bar && ease_ingame > 0.001f) {
        renderInGameBottomBar(w, h, ease_ingame);
    }
    if (ease_menu > 0.001f) {
        renderBottomBar(w, h, ease_menu);
    }
}
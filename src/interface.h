#pragma once
#ifndef INTERFACE_H
#define INTERFACE_H

#include "ui.h"
#include "frontend.h"
#include "console_utils.h"
#include "vector.h"
#include "dyn_string.h"

#if defined(__GNUC__) || defined(__clang__)
#define UI_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define UI_NOINLINE __declspec(noinline)
#else
#define UI_NOINLINE
#endif

// Shade glyphs, lightest to darkest (mono dimming, bars, game video).
inline constexpr const char* const kShadowRamp[5] = { " ", "░", "▒", "▓", "█" };

inline constexpr int MAX_RECENT = 8;

constexpr int BIND_COUNT = 24;

enum InputBind {
    BIND_JOYPAD_B           = 0,
    BIND_JOYPAD_Y           = 1,
    BIND_JOYPAD_SELECT      = 2,
    BIND_JOYPAD_START       = 3,
    BIND_JOYPAD_UP          = 4,
    BIND_JOYPAD_DOWN        = 5,
    BIND_JOYPAD_LEFT        = 6,
    BIND_JOYPAD_RIGHT       = 7,
    BIND_JOYPAD_A           = 8,
    BIND_JOYPAD_X           = 9,
    BIND_JOYPAD_L           = 10,
    BIND_JOYPAD_R           = 11,
    BIND_JOYPAD_L2          = 12,
    BIND_JOYPAD_R2          = 13,
    BIND_JOYPAD_L3          = 14,
    BIND_JOYPAD_R3          = 15,
    BIND_ANALOG_LEFT_UP     = 16,
    BIND_ANALOG_LEFT_DOWN   = 17,
    BIND_ANALOG_LEFT_LEFT   = 18,
    BIND_ANALOG_LEFT_RIGHT  = 19,
    BIND_ANALOG_RIGHT_UP    = 20,
    BIND_ANALOG_RIGHT_DOWN  = 21,
    BIND_ANALOG_RIGHT_LEFT  = 22,
    BIND_ANALOG_RIGHT_RIGHT = 23
};

struct AppSettings {
    ColorMode color_mode          = ColorMode::TrueColor;
    bool      pixel_mode          = true;
    int       aspect_mode         = 0;
    int       font_width          = 1;
    int       menu_scale          = 0;
    bool      show_stats_overlay  = true;
    bool      show_log_overlay    = true;
    bool      show_bottom_bar     = true;
    int       volume_pct          = 100;
    bool      audio_enabled       = true;
    int       default_slot        = 1;
    bool      auto_save_exit      = false;
    bool      auto_load_start     = false;
    bool      pause_in_menu       = true;
    bool      dither_enabled      = true;
    int       brightness          = 100;
    int       contrast            = 100;
    int       renderer_mode       = 0;

    static const int default_key_mappings[BIND_COUNT];
    static const int default_gamepad_mappings[BIND_COUNT];

    int key_mappings[BIND_COUNT];       // filled from default_* by the constructor
    int gamepad_mappings[BIND_COUNT];

    int       mouse_sensitivity         = 180;
    int       default_mouse_sensitivity = 180;
    int       p1_gamepad_index          = 0;
    int       p2_gamepad_index          = 1;

    char recent_cores   [MAX_RECENT][512];
    char recent_content [MAX_RECENT][512];
    int  num_recent_cores   = 0;
    int  num_recent_content = 0;

    AppSettings();
    void resetBindings();   // key + gamepad maps back to defaults
};

extern AppSettings& get_cfg();
#define g_cfg get_cfg()

void cfgSave();
void cfgLoad();
void cfgPushRecent(char arr[][512], int& cnt, const char* path);

// Gamepad bindings 16..25 are analog axes. padAxes() order: lx, ly, rx, ry, lt, rt.
inline void padAxes(const GamepadState& gp, float* a) {
    a[0] = gp.leftX;  a[1] = gp.leftY;  a[2] = gp.rightX;
    a[3] = gp.rightY; a[4] = gp.leftTrigger; a[5] = gp.rightTrigger;
}
inline constexpr uint8_t kAxisOfBind[10] = { 1, 1, 0, 0, 3, 3, 2, 2, 4, 5 };   // id-16 -> axis index
inline constexpr bool gpBindPositive(int id) { return (id & 1) || id >= 24; }  // else negative direction

// Big menu mode (3x5 bitmap font). Auto picks it on large terminals.
// Build with -DTRETRO_NO_BIG_UI to compile it out entirely.
#ifdef TRETRO_NO_BIG_UI
inline constexpr bool uiBigMode(int, int) { return false; }
#else
inline bool uiBigMode(int w, int h) {
    const int s = get_cfg().menu_scale;
    return s == 0 ? (w >= 160 && h >= 50) : (s == 2);
}
#endif
inline constexpr int uiBarH(bool big) { return big ? 7 : 1; }

enum class MenuSec { FILE_, OPTIONS, GAME, CONSOLE };

struct FileBrowser {
    bool active           = false;
    bool append_disc_mode = false;
    char current_dir[512] = ".";     // "" = the drive list (Windows)
    PodVector<DynString> files;
    PodVector<size_t>    sizes;      // per entry, read lazily the first time a row is drawn
    int  num_files        = 0;
};

struct CoreDownloader {
    bool active        = false;
    int  scroll_offset = 0;
};

struct CorePicker {
    bool active         = false;
    bool no_match       = false;
    int  matched_count  = 0;
    char content_path[512] = "";
    PodVector<DynString> candidates;
    PodVector<DynString> library_names;
    PodVector<int>       ext_counts;
    int  num_candidates = 0;
    int  scroll_offset  = 0;
};

namespace Colors {
    inline constexpr RGBColor TEXT_DEF   = {220, 230, 250};
    inline constexpr RGBColor LABEL      = {150, 175, 205};
    inline constexpr RGBColor TITLE      = {180, 210, 255};
    inline constexpr RGBColor BG         = {20,  30,  50 };
    inline constexpr RGBColor BORDER     = {70,  120, 190};
    inline constexpr RGBColor WARN       = {255, 170, 80 };
    inline constexpr RGBColor OK         = {120, 255, 120};
    inline constexpr RGBColor ERR        = {255, 100, 100};
    inline constexpr RGBColor INACTIVE   = {100, 115, 135};
    inline constexpr RGBColor SELECTED   = {255, 255, 100};
    inline constexpr RGBColor MENU_BG    = {10,  15,  25 };
    inline constexpr RGBColor TAB_ACTIVE = {255, 255, 255};
    inline constexpr RGBColor TAB_IDLE   = {110, 135, 165};
    inline constexpr RGBColor SHADOW     = {5,   7,   12 };
    inline constexpr RGBColor BAR_FILL   = {100, 180, 255};
    inline constexpr RGBColor STATUS_BG  = {18,  38,  70 };
    inline constexpr RGBColor STATUS_KEY = {80,  95,  120};
    inline constexpr RGBColor STATUS_ACT = {40,  90,  180};
}

struct BorderStyle {
    ConsoleGlyph tl = "┌", tr = "┐", bl = "└", br = "┘";
    ConsoleGlyph h  = "─", v  = "│", div = "─";
};

struct MenuState {
    int selected_index = 0;
    int item_count     = 0;
    int scroll_offset  = 0;
};


class RetroUI : public UI {
public:
    bool    open            = false;
    bool    standalone      = true;
    MenuSec section         = MenuSec::FILE_;
    int     last_item_count = 0;

    char pending_core[512]          = "";
    char pending_content[512]       = "";
    char pending_state_to_load[512] = "";
    bool pending_load               = false;
    bool wants_quit                 = false;

    static const char* const tab_names[4];
    static const char* const tab_names_short[4];

    static constexpr const char* status_browser          = " [ENTER] Select [ESC] Close";
    static constexpr const char* status_browser_short    = " ENTER:Select ESC:Close";
    static constexpr const char* status_picker           = " [ENTER] Select [ESC] Back";
    static constexpr const char* status_picker_short     = " ENTER:Select ESC:Back";
    static constexpr const char* status_default          = " [ENTER] Select [Q/E] Switch Tab [F1] Resume";
    static constexpr const char* status_default_short    = " ENTER:Select Q/E:Tabs F1:Resume";
    static constexpr const char* status_default_vshort   = " ENT:Sel Q/E:Tabs F1:Resume";
    static constexpr const char* status_no_game          = " [ENTER] Select [Q/E] Switch Tab";

    static constexpr const char* status_gp_browser          = " (A) Select (B) Close";
    static constexpr const char* status_gp_picker           = " (A) Select (B) Back";
    static constexpr const char* status_gp_default          = " (A) Select (LB/RB) Switch Tab (View+Menu) Resume";
    static constexpr const char* status_gp_default_short    = " (A) Select (LB/RB) Tabs (View+Menu) Resume";
    static constexpr const char* status_gp_default_vshort   = " (A) Sel (LB/RB) Tabs (View+Menu) Resume";
    static constexpr const char* status_gp_no_game          = " (A) Select (LB/RB) Switch Tab";

    static constexpr const char* status_ingame          = " [F1] Menu  [Ctrl+P] Pause  [Ctrl+R] Reset  [Ctrl+1-9] Save  [Alt+1-9] Load  [Ctrl+Q] Quit";
    static constexpr const char* status_ingame_short    = " [F1] Menu  [Ctrl+P] Pause  [Ctrl+R] Reset  [Ctrl+Q] Quit";
    static constexpr const char* status_ingame_vshort   = " [F1] Menu  [Ctrl+P] Pause  [Ctrl+Q] Quit";
    static constexpr const char* status_ingame_nogame   = " [F1] Menu  [Ctrl+Q] Quit";

    static constexpr const char* status_gp_ingame       = " (View+Menu) / (Start+Back) Menu";
    static constexpr const char* status_gp_ingame_short = " (View+Menu) Menu";
    static constexpr const char* status_gp_ingame_nogame= " (View+Menu) Menu";

    FileBrowser    browser;
    CoreDownloader downloader;
    CorePicker     core_picker;
    float          anim_progress = 0.f;

    float          m_transient_log_ttl   = 0.0f;
    int            m_last_log_head       = -1;
    int            m_last_log_count      = -1;
    float          m_text_scroll_timer   = 0.0f;
    int            m_prev_selected_idx   = -1;

    char           cmd_buffer[128];
    bool           cmd_active            = false;
    void           executeCommand(const char* cmd);

    int  m_mouse_x = 0, m_mouse_y = 0;
    bool m_mouse_moved = false, m_mouse_clicked = false;
    bool m_enter = false, m_left = false, m_right = false, m_up = false, m_down = false, m_tab = false;
    bool m_home = false, m_end = false, m_pgup = false, m_pgdn = false;   // list jumps
    int  m_last_visible = 8;                                              // rows per page for PgUp/PgDn
    bool m_keyboard_gamepad_active = false;
    bool m_is_gamepad_mode         = false;

    bool is_mono  = false;
#ifdef TRETRO_NO_BIG_UI
    static constexpr bool big_mode = false;
#else
    bool big_mode = false;
#endif
    float fps_display_value = 0.0f;
    const InputManager* input_ptr = nullptr;
    BorderStyle border_style;
    MenuState   menu;

    RetroUI();

    inline int  getItemHeight() const { return big_mode ? 6 : 1; }
    inline int  cw() const { return big_mode ? 4 : 1; }
    inline int  barH() const { return uiBarH(big_mode); }
    inline bool hitTest(int x, int y, int w, int h) const {
        return (unsigned)(m_mouse_x - x) < (unsigned)w && (unsigned)(m_mouse_y - y) < (unsigned)h;
    }
    inline void highlightRow(int y, RGBColor bg, bool sel) {
        FillHighlight(state.window_x + 1, y, state.window_w - 2, getItemHeight(), bg, sel);
    }
    inline bool isTriggered() const { return m_left || m_right || m_enter; }

    void updateAnim(float dt);
    void handleBack();
    void DrawBigText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int clip_end_x);
    void DrawUIText(const char* label, RGBColor fg);
    void DrawSeparator();
    void DrawSectionHeader(const char* title);
    void browserScan();
    void browserUp();
    void browserEnter(const char* name);
    enum class PickMode { Browse, Startup, Drop };
    void tryPickCoreForContent(const char* content_path, PickMode mode = PickMode::Browse);
    void clearPicker();

    bool handleRowHover(int target_idx, int& row_y, int& end_x);
    RGBColor selFg(bool sel, RGBColor idle) const;
    RGBColor selBg(bool sel, RGBColor idle) const;
    RGBColor rowStyle(int row_y, bool sel, RGBColor idle_fg);
    void resetNav();
    void requestLoad(const char* core, const char* content = nullptr, bool close = true);
    bool MenuOption(const char* label, const char* val);

    void bindInput(const InputManager* inp) { input_ptr = inp; }
    void update(float dt, const Vec3& pos, const Vec3& dir, float fps, const Vec3& vel, bool grounded, bool show_ui);

    void Begin(const char* title, int x, int y, int w, int h, const RGBColor& border_col, const RGBColor& bg_col = {-1, -1, -1});
    void render(PodVector<PodVector<ConsoleCell>>& buffer, int w, int h);

private:
    void putLine(int x, int y, const ConsoleGlyph& thin);
    void drawShadow   (int sx, int sy, int sw, int sh);
    void fillRect     (int sx, int sy, int sw, int sh, RGBColor bg);
    void DrawNormalText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int clip_end_x);
    void fillCells(int x, int y, int w, int h, const ConsoleGlyph& g, RGBColor fg, RGBColor bg, int y_min, int y_max, bool border = false);
    void FillHighlight(int x, int y, int w_fill, int h_fill, RGBColor bg, bool sel);
    void drawOverlayBox(int bx, int by, int bw, int bh, const char* title, int div_y = -1);
    void drawBorderRect(int x, int y, int w, int h, RGBColor col, RGBColor bg, int div_y = -1);

    void handleInput();
    void handleNavigation();
    void calculatePopupGeometry(int w, int h, float anim, int& px, int& py, int& pw, int& ph);
    void renderBackground(int w, int h);
    void fillBarRow(int y_start, int bar_h, int w, int h);
    void drawBottomBar(int w, int h, int bot_y, const char* status);
    void renderTopBar(int w, int h, float anim);
    void renderBottomBar(int w, int h, float anim);

    int  computeVisibleWindow(int py, int ph, int& scroll_offset);
    void renderStickyHeader();
    void snapViewport(int& row_y, int item_h);

    void beginSection(const char* title, int px, int py, int pw, int ph);
    void drawSectionTitle(const char* title);
    UI_NOINLINE void renderBrowser(int px, int py, int pw, int ph);
    UI_NOINLINE void renderDownloader(int px, int py, int pw, int ph);
    UI_NOINLINE void renderCorePicker(int px, int py, int pw, int ph);
    UI_NOINLINE void renderFileSection(int px, int py, int pw, int ph);
    UI_NOINLINE void renderOptionsSection(int px, int py, int pw, int ph);
    UI_NOINLINE void renderGameSection(int px, int py, int pw, int ph);
    UI_NOINLINE void renderConsoleSection(int px, int py, int pw, int ph);
    UI_NOINLINE void renderInGameBottomBar(int w, int h, float ease_slide);

    UI_NOINLINE void renderStatsOverlay(int w, int h, float ease_slide);
    UI_NOINLINE void renderLogOverlay  (float ease_slide);

    void drawScaledText(int x, int y, const char* str, RGBColor fg);
    void drawScaledText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int end_x);
    void drawScrollingText(int x, int y, const char* str, RGBColor fg, RGBColor bg, int clip_end_x, bool sel);

    bool optionCycleInt(int& val, int step, int min_val, int max_val, bool left_dir, bool right_dir);
    bool optToggle(const char* lbl, bool& val);
    bool optCycle(const char* lbl, const char* const names[], int& val, int count);
    bool optPct(const char* lbl, int& val, int step, int lo, int hi);

    int  m_binding_button_id   = -1;
    bool m_binding_wait_release = false;
};

#endif
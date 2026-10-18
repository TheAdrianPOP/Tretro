#include "frontend.h"   
#include "perf_timer.h"
#include "runtime.h"
#include "interface.h"
#include "path_utils.h"
#include "autodetect.h"

#include <signal.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "win32_dispath.h"
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define NOINLINE      __attribute__((noinline))
  #define NOINLINE_COLD __attribute__((noinline, cold))
#else
  #define NOINLINE
  #define NOINLINE_COLD
#endif

static volatile bool g_quit = false;
static bool s_prev_gp_toggle = false;

static void onSignal(int) { g_quit = true; }

#ifdef _WIN32
static BOOL WINAPI WindowsCtrlHandler(DWORD fdwCtrlType) {
    if (fdwCtrlType == CTRL_C_EVENT || fdwCtrlType == CTRL_CLOSE_EVENT || fdwCtrlType == CTRL_BREAK_EVENT) {
        g_quit = true;
        return TRUE;
    }
    return FALSE;
}
#endif

#ifdef MEM_ANALYZE
extern "C" {
    void mem_get_stats(size_t* out_bytes, size_t* out_count);
    void mem_dump_leaks_to_file(const char* filepath, uint64_t since_alloc_id);
    void mem_update_background(float dt, float interval_sec, const char* filepath);
    void mem_report_leaks();
}
#endif

static inline void ewma(float& cur, float val) {
    cur = cur * 0.95f + val * 0.05f;
}

static void clearCellBuffer(ConsoleCell* buf, size_t count) {
    if (!buf) return;
    ConsoleCell blank{};
    for (size_t i = 0; i < count; ++i) buf[i] = blank;
}

#ifdef MEM_ANALYZE
static void drawTextOnBuffer(PodVector<PodVector<ConsoleCell>>& buf, int x, int y, const char* text, RGBColor fg, RGBColor bg) {
    if (y < 0 || y >= static_cast<int>(buf.size()) || !text) return;
    int row_w = static_cast<int>(buf[y].size());
    for (int i = 0; text[i] && (x + i) < row_w; ++i) {
        ConsoleCell& cell = buf[y][x + i];
        cell.character.data[0] = text[i];
        cell.character.data[1] = '\0';
        cell.color             = fg;
        cell.bg_color          = bg;
    }
}
#endif

static inline bool initCoreSafe(Frontend& f, const char* core, const char* content, const char* sys_dir = nullptr, const char* sav_dir = nullptr) {
#ifdef _WIN32
    StdRedirector redirect;
#endif
    return f.init(core, content, 
                  sys_dir ? sys_dir : f.system_dir.c_str(), 
                  sav_dir ? sav_dir : f.save_dir.c_str());
}

struct ParsedArgs {
    char core_path   [512]{};
    char content_path[512]{};
    char system_dir  [512]{};
    char save_dir    [512]{};

    struct Override { DynString key; DynString val; };
    PodVector<Override> overrides;
};

NOINLINE_COLD static bool parseArgs(int argc, char* argv[], ParsedArgs& a) {
    safeCopy(a.system_dir, "./system", sizeof(a.system_dir));
    safeCopy(a.save_dir,   "./saves",  sizeof(a.save_dir));

    char path_buf[512];

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg[0] == '-' && arg[1] && !arg[2]) {
            char opt = arg[1];
            if (opt == 'o' && i + 1 < argc) {
                const char* opt_val = argv[++i];
                const char* eq = strchr(opt_val, '=');
                if (eq) {
                    a.overrides.push_back({DynString(opt_val, (size_t)(eq - opt_val)), eq + 1});
                }
                continue;
            }

            char* dst = (opt == 'c') ? a.core_path :
                        (opt == 'f') ? a.content_path :
                        (opt == 's') ? a.system_dir :
                        (opt == 'S') ? a.save_dir : nullptr;

            if (dst && i + 1 < argc) {
                makePathAbsolute(argv[++i], dst, 512);
                continue;
            }
        } else if (arg[0] != '-') {
            makePathAbsolute(arg, path_buf, sizeof(path_buf));
            safeCopy(isCoreExt(getFileExtension(path_buf)) ? a.core_path : a.content_path, path_buf, 512);
        }
    }
    return true;
}

NOINLINE static void handlePendingLoad(
        Frontend& f, RetroUI& ui,
        bool& standalone_mode, float& target_fps,
        float& base_frame_time,
        PodVector<ConsoleCell>& pristine_frame)
{
    if (!ui.pending_load) return;
    ui.pending_load = false;
    f.shutdown();

    bool load_ok = false;
    if (ui.pending_core[0] != '\0') {
        load_ok = initCoreSafe(f, ui.pending_core, ui.pending_content[0] ? ui.pending_content : nullptr);
    }
    if (load_ok) {
        cfgPushRecent(g_cfg.recent_cores,   g_cfg.num_recent_cores,   ui.pending_core);
        if (ui.pending_content[0])
            cfgPushRecent(g_cfg.recent_content, g_cfg.num_recent_content, ui.pending_content);
        cfgSave();
        f.audio.setMasterVolume(g_cfg.audio_enabled ? g_cfg.volume_pct / 100.f : 0.f);
        standalone_mode = false;
        ui.standalone   = false;
        ui.open         = false;
        target_fps      = f.target_fps > 1.0f ? f.target_fps : 60.f;
        base_frame_time = 1.f / target_fps;
        if (!pristine_frame.empty()) {
            clearCellBuffer(pristine_frame.data(), pristine_frame.size());
        }

        if (ui.pending_state_to_load[0] == '\0' && g_cfg.auto_load_start) {
            getLatestSaveState(ui.pending_state_to_load, sizeof(ui.pending_state_to_load), f.save_dir.c_str(), f.content_path.c_str(), f.core_path.c_str());
        }
    } else {
        standalone_mode = true;
        ui.standalone   = true;
        ui.open         = true;
        ui.pending_state_to_load[0] = '\0';
    }
}

NOINLINE_COLD static void openCorePickerForContent(RetroUI& ui, const char* content_path, const char* argv0 = nullptr) {
    if (!content_path || !content_path[0]) return;

    ui.core_picker.candidates.clear();
    ui.core_picker.library_names.clear();
    ui.core_picker.ext_counts.clear();

    ui.core_picker.no_match = false;
    safeCopy(ui.core_picker.content_path, content_path, sizeof(ui.core_picker.content_path));

    int matched = 0;
    ui.core_picker.num_candidates = autodetectCoreMulti(
        content_path, argv0 ? argv0 : "", ui.core_picker.candidates,
        &matched, &ui.core_picker.library_names, &ui.core_picker.ext_counts);

    ui.core_picker.matched_count = matched;
    ui.core_picker.no_match = (matched == 0);
    ui.core_picker.scroll_offset = 0;
    ui.menu.selected_index = 0;
    ui.browser.active = false;
    ui.pending_load = false;

    if (ui.core_picker.num_candidates > 0) {
        ui.core_picker.active = true;
        ui.open = true;
        Frontend::get().logMessage("[loader] Showing core picker for: %s", content_path);
    } else {
        Frontend::get().logMessage("[ERR] No cores found for: %s", content_path);
        ui.open = true;
    }
}

NOINLINE_COLD static bool initFrontendAndCore(Frontend& f, RetroUI& ui, const ParsedArgs& args, const char* core_path, const char* argv0 = nullptr) {
    bool standalone_mode = (core_path == nullptr);
    const char* cnt_path = args.content_path[0] ? args.content_path : nullptr;

    if (!standalone_mode) {
        if (!initCoreSafe(f, core_path, cnt_path, args.system_dir, args.save_dir)) {
            standalone_mode = true;
            ui.standalone   = true;
            ui.open         = true;
            f.logMessage("[ERR] Init failed: '%s'", core_path);
        } else {
            for (size_t i = 0; i < args.overrides.size(); i++) {
                f.setVar(args.overrides[i].key.c_str(), args.overrides[i].val.c_str());
            }
            f.audio.setMasterVolume(g_cfg.audio_enabled ? g_cfg.volume_pct / 100.f : 0.f);
            ui.standalone = false;
            ui.open = false;

            if (g_cfg.auto_load_start) {
                getLatestSaveState(ui.pending_state_to_load, sizeof(ui.pending_state_to_load), f.save_dir.c_str(), f.content_path.c_str(), f.core_path.c_str());
            }
        }
    }

    if (standalone_mode) {
        ui.standalone = true;
        if (cnt_path) {
            openCorePickerForContent(ui, cnt_path, argv0);
        } else {
            ui.open = true;
        }
    }

    return standalone_mode;
}

NOINLINE_COLD static bool runStartup(int argc, char* argv[], Frontend& f, RetroUI& ui, bool& standalone_mode) {
    ParsedArgs args;
    if (!parseArgs(argc, argv, args)) return false;

#ifdef _WIN32
    setWorkDirToExeDir();
#else
    setWorkDirToExeDir(argv[0]);
#endif

    const char* c_path = args.core_path[0] ? args.core_path : nullptr;

    setupConsoleMode();
    ui.bindInput(&f.input);

    standalone_mode = initFrontendAndCore(f, ui, args, c_path, argv[0]);
    return true;
}

NOINLINE static void processInputAndShortcuts(Frontend& f, RetroUI& ui, bool standalone_mode, bool& paused) {
    f.input.update();
    extern void input_frame_update();
    input_frame_update();

    bool ctrl = f.input.isKeyDown(KEY_CONTROL);
    bool alt  = f.input.isKeyDown(KEY_ALT);

    const GamepadState& gp0 = globalGamepad().state(0);
    bool current_gp_toggle = gp0.connected && 
        ((gp0.isDown(GamepadButton::Start) && gp0.isDown(GamepadButton::Back)) ||
         (gp0.isDown(GamepadButton::LStick) && gp0.isDown(GamepadButton::RStick)));

    bool menu_toggle = f.input.isKeyPressed(KEY_F1) || 
                       (current_gp_toggle && !s_prev_gp_toggle);

    s_prev_gp_toggle = current_gp_toggle;

    if (menu_toggle) {
        ui.open = !ui.open;
    }

#ifdef MEM_ANALYZE
    if (f.input.isKeyPressed(KEY_F9) || (ctrl && f.input.isKeyPressed(KEY_M))) {
        mem_dump_leaks_to_file("memory_snapshot.txt", 0);
        f.logMessage("[mem] Memory snapshot saved to 'memory_snapshot.txt'");
    }
#endif

    if (ui.wants_quit || (ctrl && f.input.isKeyPressed(KEY_Q))) { 
        g_quit = true; 
        return; 
    }

    if (!ui.open && !standalone_mode) {
        if (ctrl && f.input.isKeyPressed(KEY_R)) { 
            f.core.reset(); 
            f.logMessage("[inf] Core reset");
        }
        if (ctrl && f.input.isKeyPressed(KEY_P)) { 
            paused = !paused; 
            f.logMessage(paused ? "[inf] Paused" : "[inf] Resumed");
        }
        
        int sl = -1;
        for (int i = 1; i <= 9; i++) {
            if (f.input.isKeyPressed('0' + i)) { sl = i; break; }
        }
        if (sl > 0) {
            char p[512];
            getSlotPath(p, sizeof(p), f.save_dir.c_str(), f.content_path.c_str(), f.core_path.c_str(), sl);
            if (ctrl && !alt) f.saveState(p);
            else if (alt && !ctrl) f.loadState(p);
        }
    }
}

static void syncConsoleBuffers(Frontend& f,
                               PodVector<ConsoleCell>& pristine_frame,
                               PodVector<PodVector<ConsoleCell>>& standalone_buffer,
                               int& width, int& height) {
    getConsoleSize(width, height);
    width = fast_max(40, width);
    height = fast_max(15, height);

    if (width == f.term_w && height == f.term_h) return;
    f.term_w = width;
    f.term_h = height;
    f.display.Resize(width, height);

    size_t total = (size_t)(width * height);
    pristine_frame.resize(total);
    clearCellBuffer(pristine_frame.data(), total);

    standalone_buffer.resize(height);
    for (int y = 0; y < height; ++y) {
        standalone_buffer[y].resize(width);
        clearCellBuffer(standalone_buffer[y].data(), (size_t)width);
    }
}

NOINLINE static void renderAndStepFrame(
        Frontend& f, RetroUI& ui,
        bool standalone_mode, bool paused,
        float frame_time, float target_fps,
        PodVector<ConsoleCell>& pristine_frame,
        PodVector<PodVector<ConsoleCell>>& standalone_buffer,
        int width, int height)
{
    const size_t total_cells = (size_t)width * height;
    const size_t frame_bytes = total_cells * sizeof(ConsoleCell);
    const size_t row_bytes   = (size_t)width * sizeof(ConsoleCell);

    ConsoleCell* d_buf = f.display.GetBuffer();
    double core_elapsed = 0.0;
    int changed_cells = 0;

    if (!standalone_mode) {
        if (!paused && !(g_cfg.pause_in_menu && ui.open)) {
            PerfTimer timer;
            timer.start();

            if (d_buf) memcpy(d_buf, pristine_frame.data(), frame_bytes);

            if (f.gl.active) f.gl.bindForCoreRender();
            f.core.run(); 

            audio_sample_flush();

            if (d_buf) {
                const ConsoleCell* prev = pristine_frame.data();
                for (size_t idx = 0; idx < total_cells; ++idx) {
                    const ConsoleCell& c_prev = prev[idx];
                    const ConsoleCell& c_curr = d_buf[idx];
                    
                    if (c_prev.color != c_curr.color || c_prev.bg_color != c_curr.bg_color ||
                        *(const uint32_t*)c_prev.character.data != *(const uint32_t*)c_curr.character.data) {
                        changed_cells++;
                    }
                }
                memcpy(pristine_frame.data(), d_buf, frame_bytes);
            }

            if (ui.pending_state_to_load[0] != '\0') {
                f.loadState(ui.pending_state_to_load);
                ui.pending_state_to_load[0] = '\0';
            }

            core_elapsed = timer.getElapsedMs();
        }
    }

    const ConsoleCell* p_frame = pristine_frame.data();
    for (int y = 0; y < height; ++y) {
        if (!standalone_mode) {
            memcpy(standalone_buffer[y].data(), &p_frame[y * width], row_bytes);
        } else {
            clearCellBuffer(standalone_buffer[y].data(), (size_t)width);
        }
    }

    PerfTimer timer;
    timer.start();

    ui.updateAnim(frame_time);
    ui.update(frame_time, Vec3{}, Vec3{}, target_fps, Vec3{}, false, true);
    ui.render(standalone_buffer, width, height);

#ifdef MEM_ANALYZE
    size_t mem_bytes = 0, mem_allocs = 0;
    mem_get_stats(&mem_bytes, &mem_allocs);
    f.perf.mem_mb     = static_cast<float>(mem_bytes) / (1024.0f * 1024.0f);
    f.perf.mem_allocs = static_cast<int>(mem_allocs);

    if (width > 40 && height > 2) {
        char hud_buf[64];
        size_t hud_len = 0;

        auto append_hud = [&](const char* s) {
            while (*s && hud_len + 1 < sizeof(hud_buf)) hud_buf[hud_len++] = *s++;
            hud_buf[hud_len] = '\0';
        };

        auto append_uint_hud = [&](uint64_t v) {
            if (v == 0) {
                if (hud_len + 1 < sizeof(hud_buf)) { hud_buf[hud_len++] = '0'; hud_buf[hud_len] = '\0'; }
                return;
            }
            char tmp[24]; int idx = 0;
            while (v > 0) { tmp[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; }
            while (idx > 0 && hud_len + 1 < sizeof(hud_buf)) { hud_buf[hud_len++] = tmp[--idx]; }
            hud_buf[hud_len] = '\0';
        };

        hud_buf[0] = '\0';
        append_hud("MEM: ");
        append_uint_hud(static_cast<uint64_t>(f.perf.mem_mb));
        append_hud(".");
        append_uint_hud(static_cast<uint64_t>(f.perf.mem_mb * 10.0f) % 10);
        append_hud(" MB (");
        append_uint_hud(static_cast<uint64_t>(f.perf.mem_allocs));
        append_hud(" allocs)");

        drawTextOnBuffer(standalone_buffer, 2, 0, hud_buf, {0, 255, 200}, {20, 20, 20});
    }
#endif

    if (d_buf) {
        for (int y = 0; y < height; ++y) {
            memcpy(&d_buf[y * width], standalone_buffer[y].data(), row_bytes);
        }
        GraphicsConfig gfx;
        gfx.color_mode = g_cfg.color_mode;
        gfx.pixel_mode = g_cfg.pixel_mode;
        f.display.Flush(width, height, gfx);
    }

    double render_elapsed = timer.getElapsedMs();

    ewma(f.perf.core_ms, (float)core_elapsed);
    ewma(f.perf.render_ms, (float)render_elapsed);
    ewma(f.perf.changed_cells, (float)changed_cells);
    ewma(f.perf.bytes_sent, (float)changed_cells * 47.0f);
}

NOINLINE static void paceFrameRate(
        Frontend& f,
        PerfTimer& frame_timer,
        float frame_time,
        bool standalone_mode,
        float& pacing_ms_out)
{
    PerfTimer pacing_timer;
    pacing_timer.start();

    double target_ms = (double)frame_time * 1000.0;
    double elapsed_ms = frame_timer.getElapsedMs();
    double sleep_ms = target_ms - elapsed_ms;

    int buffered = f.audio_buffered.load(rt_mo::relaxed);
    if (!standalone_mode && buffered < 1536) {
        sleep_ms = 0.0;
    }

    if (sleep_ms > 2.5) {
        platform_sleep((sleep_ms - 2.0) * 0.001);
    }

    while (frame_timer.getElapsedMs() < target_ms) {
#if defined(_MSC_VER)
        _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
        __asm__ __volatile__("pause");
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield");
#endif
    }

    pacing_ms_out = (float)pacing_timer.getElapsedMs();
    frame_timer.start();
}

NOINLINE_COLD static void autoSaveOnExit(Frontend& f) {
    if (f.core.loaded()) {
        f.saveGameConfig();
        if (g_cfg.auto_save_exit) {
            PodVector<DetectedSave> detected;
            scanSaveStates(f.save_dir.c_str(), f.content_path.c_str(), f.core_path.c_str(), detected);
            
            int max_slot = 0;
            if (!detected.empty()) {
                max_slot = detected[detected.size() - 1].slot_num;
                if (max_slot < 0) max_slot = 0;
            }
            
            char p[512];
            getSlotPath(p, sizeof(p), f.save_dir.c_str(), f.content_path.c_str(), f.core_path.c_str(), max_slot + 1);
            f.saveState(p);
        }
    }
}

NOINLINE_COLD static void cleanupPlatformAndSystem(Frontend& f) {
    cfgSave();
    f.shutdown();
    f.display.Resize(0, 0);
    f.term_w = 0;
    f.term_h = 0;
    restoreConsoleMode();
    clearConsole();

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

int main(int argc, char* argv[]) {
    runtime_init();
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

#ifdef _WIN32
    SetConsoleCtrlHandler(WindowsCtrlHandler, TRUE);
    timeBeginPeriod(1);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    cfgLoad();

    RetroUI* ui = new RetroUI();
    Frontend& f = Frontend::get();
    bool standalone_mode = false;

    if (!runStartup(argc, argv, f, *ui, standalone_mode)) {
        delete ui;
        cleanupPlatformAndSystem(f);
#ifdef MEM_ANALYZE
        mem_report_leaks();
#endif
        return 0;
    }

    float target_fps = (f.target_fps >= 1.0f && f.target_fps <= 1000.0f) ? f.target_fps : 60.0f;
    float base_frame_time = 1.0f / target_fps;
    bool paused = false;

    PerfTimer frame_timer; 
    frame_timer.start();

    PerfTimer loop_timer;
    loop_timer.start();

    PodVector<PodVector<ConsoleCell>> standalone_buffer;
    PodVector<ConsoleCell> pristine_frame;

    while (f.running && !g_quit) {
#ifdef _WIN32
        checkPastedPathWindows(f, *ui, argv[0]);
#endif

        processInputAndShortcuts(f, *ui, standalone_mode, paused);
        if (g_quit) break;

        if (!standalone_mode && f.target_fps >= 1.0f && f.target_fps <= 1000.0f) {
            if (f.target_fps != target_fps) {
                target_fps = f.target_fps;
                base_frame_time = 1.0f / target_fps;
            }
        }

        handlePendingLoad(f, *ui, standalone_mode, target_fps, base_frame_time, pristine_frame);

        int width = 80, height = 25;
        syncConsoleBuffers(f, pristine_frame, standalone_buffer, width, height);

        renderAndStepFrame(f, *ui, standalone_mode, paused, base_frame_time, target_fps, pristine_frame, standalone_buffer, width, height);

#ifdef MEM_ANALYZE
        mem_update_background(base_frame_time, 5.0f, "memory_live.txt");
#endif

        float pacing_elapsed = 0.0f;
        paceFrameRate(f, frame_timer, base_frame_time, standalone_mode, pacing_elapsed);

        ewma(f.perf.sleep_ms, pacing_elapsed);

        double total_elapsed = loop_timer.getElapsedMs();
        ewma(f.perf.frame_ms, (float)total_elapsed);
        
        float current_fps = (total_elapsed > 0.0) ? (1000.0f / (float)total_elapsed) : 0.0f;
        if (f.perf.fps == 0.0f) f.perf.fps = current_fps;
        else ewma(f.perf.fps, current_fps);
        
        loop_timer.start();
    }

    autoSaveOnExit(f);

    for (size_t y = 0; y < standalone_buffer.size(); ++y) {
        standalone_buffer[y].reset();
    }
    standalone_buffer.reset();
    pristine_frame.reset();

    delete ui;
    cleanupPlatformAndSystem(f);

#ifdef MEM_ANALYZE
    mem_report_leaks();
#endif

    return 0;
}
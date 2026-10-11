#pragma once
#ifndef FRONTEND_H
#define FRONTEND_H

#include "libretro.h"
#include "core_loader.h"
#include "resampler.h"
#include "gl_render.h"
#include "path_utils.h"

#include "runtime.h"
#include "display.h"
#include "audio.h"
#include "input.h"
#include "gamepad.h"
#include "threading.h"
#include "ring_buffer.h"
#include "dyn_string.h"
#include "perf_timer.h"
#include "file_system.h"
#include "console_utils.h"
#include "engine_config.h"
#include "render_utils.h"

static constexpr int kMaxConsoleLogs = 128;
static constexpr int kMaxLogLen      = 256;

struct CoreVar {
    DynString key;
    DynString desc;        
    DynString current;     
    DynString def;         
    DynString raw;         
    DynString raw_labels;  
};

struct CoreOptionOverride {
    DynString key;
    DynString value;
};

struct StereoFrame {
    int16_t left;
    int16_t right;
};

struct Viewport {
    int off_x = 0;
    int off_y = 0;
    int fit_w = 1;
    int fit_h = 1;
};

struct PerfMetrics {
    float fps           = 0.0f;
    float core_ms       = 0.0f;
    float blit_ms       = 0.0f;
    float render_ms     = 0.0f;
    float sleep_ms      = 0.0f;
    float frame_ms      = 0.0f;
    float changed_cells = 0.0f;
    float bytes_sent    = 0.0f;
#ifdef MEM_ANALYZE
    float mem_mb        = 0.0f;
    int   mem_allocs    = 0;
#endif
};

struct DiskControl {
    bool supported     = false;
    bool ext_interface = false;

    retro_set_eject_state_t     set_eject_state     = nullptr;
    retro_get_eject_state_t     get_eject_state     = nullptr;
    retro_get_image_index_t     get_image_index     = nullptr;
    retro_set_image_index_t     set_image_index     = nullptr;
    retro_get_num_images_t      get_num_images      = nullptr;
    retro_replace_image_index_t replace_image_index = nullptr;
    retro_add_image_index_t     add_image_index     = nullptr;
    retro_set_initial_image_t   set_initial_image   = nullptr;
    retro_get_image_path_t      get_image_path      = nullptr;
    retro_get_image_label_t     get_image_label     = nullptr;

    void     reset();
    void     setCallbacks(const void* cb, bool ext);
    bool     getEject() const;
    bool     setEject(bool ejected);
    bool     toggleEject();
    unsigned getNumImages() const;
    unsigned getImageIndex() const;
    bool     setImageIndex(unsigned index);
    bool     changeDisc(unsigned index);
    bool     getImageLabel(unsigned index, char* label, size_t len) const;
    bool     appendImage(const char* path);
};

struct Frontend {
    LibretroCore     core;
    Display          display;
    AudioSystem      audio;
    InputManager     input;
    ParallelExecutor threads;
    HermiteResampler resampler;

    Viewport    viewport;
    PerfMetrics perf;

    DiskControl      disk_control;

    DynRingBuffer<StereoFrame> audio_ring;

    GLRenderContext    gl;
    PodVector<uint8_t> hw_frame_buf;

    retro_pixel_format   pixel_format     = RETRO_PIXEL_FORMAT_0RGB1555;
    retro_system_av_info av_info          = {};
    float                target_fps       = 60.0f;
    int                  core_sample_rate = 44100;

    int term_w = 80;
    int term_h = 25;

    DynString core_path;
    DynString system_dir;
    DynString save_dir;
    DynString content_path;

    PodVector<CoreVar>            core_vars;
    PodVector<CoreOptionOverride> game_options;
    bool                          vars_updated     = false;
    bool                          supports_no_game = false;
    retro_keyboard_callback       keyboard_cb      = {};

    AtomicInt audio_underruns{0};
    AtomicInt audio_overflows{0};
    AtomicInt audio_buffered{0};

    char console_logs[kMaxConsoleLogs][kMaxLogLen];
    int  console_log_head  = 0;
    int  console_log_count = 0;

    PerfTimer frame_clock;
    bool      running = true;

    static Frontend& get();

    bool init(const char* core_path,
              const char* content_path,
              const char* system_dir,
              const char* save_dir);
    void shutdown();

    void loadGameConfig();
    void saveGameConfig();

    bool saveState(const char* path);
    bool loadState(const char* path);

    bool saveSRAM(const char* path = nullptr);
    bool loadSRAM(const char* path = nullptr);

    void logMessage(const char* fmt, ...);
    void logMessageV(const char* fmt, va_list args);

    void        setVar(const char* key, const char* value);
    const char* getVar(const char* key) const;
    void        registerVars(const retro_variable* vars);
    void        registerVarsV2(const retro_core_option_v2_definition* defs);

private:
    Frontend();
    Frontend(const Frontend&)            = delete;
    Frontend& operator=(const Frontend&) = delete;
};

bool    environment_cb        (unsigned cmd, void* data);
void    video_refresh_cb      (const void* data, unsigned width,
                               unsigned height, size_t pitch);
void    audio_sample_cb       (int16_t left, int16_t right);
size_t  audio_sample_batch_cb (const int16_t* data, size_t frames);
void    input_poll_cb         (void);
int16_t input_state_cb        (unsigned port, unsigned device,
                               unsigned index, unsigned id);

void    audio_stream_cb       (int16_t* out, int frames, void* ud);
bool    set_gamepad_rumble    (unsigned port, enum retro_rumble_effect effect, uint16_t strength);
void    audio_sample_flush    (void);

#endif 
#pragma once
#ifndef CORE_LOADER_H
#define CORE_LOADER_H

#include "libretro.h"

extern void tretro_log(const char* fmt, ...);

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN          
#  endif
#  include <windows.h>
   typedef HMODULE LibHandle;
#  define lib_open(p)   LoadLibraryA(p)
#  define lib_close(h)  FreeLibrary(h)
#  define lib_error()   "LoadLibrary failed"
   inline void* lib_sym(HMODULE h, const char* name) {
       return reinterpret_cast<void*>(GetProcAddress(h, name));
   }
#else
#  include <dlfcn.h>
   typedef void* LibHandle;
#  define lib_open(p)   dlopen(p, RTLD_LAZY)
#  define lib_sym(h,s)  dlsym(h, s)
#  define lib_close(h)  dlclose(h)
#  define lib_error()   dlerror()
#endif

typedef void     (*fn_retro_init)                      (void);
typedef void     (*fn_retro_deinit)                    (void);
typedef unsigned (*fn_retro_api_version)               (void);
typedef void     (*fn_retro_get_system_info)           (retro_system_info*);
typedef void     (*fn_retro_get_system_av_info)        (retro_system_av_info*);
typedef void     (*fn_retro_set_controller_port_device)(unsigned, unsigned);
typedef void     (*fn_retro_reset)                     (void);
typedef void     (*fn_retro_run)                       (void);
typedef size_t   (*fn_retro_serialize_size)            (void);
typedef bool     (*fn_retro_serialize)                 (void*, size_t);
typedef bool     (*fn_retro_unserialize)               (const void*, size_t);
typedef bool     (*fn_retro_load_game)                 (const retro_game_info*);
typedef bool     (*fn_retro_load_game_special)         (unsigned,
                                                         const retro_game_info*,
                                                         size_t);
typedef void     (*fn_retro_unload_game)               (void);
typedef unsigned (*fn_retro_get_region)                (void);
typedef void*    (*fn_retro_get_memory_data)           (unsigned);
typedef size_t   (*fn_retro_get_memory_size)           (unsigned);
typedef void     (*fn_retro_set_environment)           (retro_environment_t);
typedef void     (*fn_retro_set_video_refresh)         (retro_video_refresh_t);
typedef void     (*fn_retro_set_audio_sample)          (retro_audio_sample_t);
typedef void     (*fn_retro_set_audio_sample_batch)    (retro_audio_sample_batch_t);
typedef void     (*fn_retro_set_input_poll)            (retro_input_poll_t);
typedef void     (*fn_retro_set_input_state)           (retro_input_state_t);

#define LOAD_REQUIRED(name)                                          \
    name = reinterpret_cast<fn_retro_##name>(                       \
               lib_sym(handle, "retro_" #name));                    \
    if (!name) {                                                     \
        tretro_log("[core] missing symbol: retro_" #name);           \
        unload(); return false;                                      \
    }

#define LOAD_OPTIONAL(name)                                          \
    name = reinterpret_cast<fn_retro_##name>(                       \
               lib_sym(handle, "retro_" #name));

struct LibretroCore {
    LibHandle handle = nullptr;

    fn_retro_init                       init                       = nullptr;
    fn_retro_deinit                     deinit                     = nullptr;
    fn_retro_api_version                api_version                = nullptr;
    fn_retro_get_system_info            get_system_info            = nullptr;
    fn_retro_get_system_av_info         get_system_av_info         = nullptr;
    fn_retro_reset                      reset                      = nullptr;
    fn_retro_run                        run                        = nullptr;
    fn_retro_load_game                  load_game                  = nullptr;
    fn_retro_unload_game                unload_game                = nullptr;
    fn_retro_set_environment            set_environment            = nullptr;
    fn_retro_set_video_refresh          set_video_refresh          = nullptr;
    fn_retro_set_audio_sample           set_audio_sample           = nullptr;
    fn_retro_set_audio_sample_batch     set_audio_sample_batch     = nullptr;
    fn_retro_set_input_poll             set_input_poll             = nullptr;
    fn_retro_set_input_state            set_input_state            = nullptr;

    fn_retro_serialize_size             serialize_size             = nullptr;
    fn_retro_serialize                  serialize                  = nullptr;
    fn_retro_unserialize                unserialize                = nullptr;
    fn_retro_set_controller_port_device set_controller_port_device = nullptr;
    fn_retro_get_region                 get_region                 = nullptr;
    fn_retro_get_memory_data            get_memory_data            = nullptr;
    fn_retro_get_memory_size            get_memory_size            = nullptr;
    fn_retro_load_game_special          load_game_special          = nullptr;

    bool loaded() const { return handle != nullptr; }

    bool load(const char* path) {
        handle = lib_open(path);
        if (!handle) {
            tretro_log("[ERR] cannot open '%s': %s", path, lib_error());
            return false;
        }

        LOAD_REQUIRED(init)
        LOAD_REQUIRED(deinit)
        LOAD_REQUIRED(api_version)
        LOAD_REQUIRED(get_system_info)
        LOAD_REQUIRED(get_system_av_info)
        LOAD_REQUIRED(reset)
        LOAD_REQUIRED(run)
        LOAD_REQUIRED(load_game)
        LOAD_REQUIRED(unload_game)
        LOAD_REQUIRED(set_environment)
        LOAD_REQUIRED(set_video_refresh)
        LOAD_REQUIRED(set_audio_sample)
        LOAD_REQUIRED(set_audio_sample_batch)
        LOAD_REQUIRED(set_input_poll)
        LOAD_REQUIRED(set_input_state)

        LOAD_OPTIONAL(serialize_size)
        LOAD_OPTIONAL(serialize)
        LOAD_OPTIONAL(unserialize)
        LOAD_OPTIONAL(set_controller_port_device)
        LOAD_OPTIONAL(get_region)
        LOAD_OPTIONAL(get_memory_data)
        LOAD_OPTIONAL(get_memory_size)
        LOAD_OPTIONAL(load_game_special)

        return true;
    }

    void unload() {
        if (handle) { lib_close(handle); }
        *this = LibretroCore{};
    }
};

#undef LOAD_REQUIRED
#undef LOAD_OPTIONAL

#endif 
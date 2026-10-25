#pragma once
#ifndef CORE_LOADER_H
#define CORE_LOADER_H

#include "libretro.h"
#include <stddef.h>
#include <string.h>

extern void tretro_log(const char* fmt, ...);

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
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

// Loader views the function-pointer members below as one contiguous table.
typedef void* __attribute__((may_alias)) AliasPtr;

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

    // Number of leading members that must resolve; the rest are optional.
    static constexpr int kRequired = 15, kTotal = 23;

    bool load(const char* path) {
        handle = lib_open(path);
        if (!handle) {
            tretro_log("[ERR] cannot open '%s': %s", path, lib_error());
            return false;
        }

        // Same order as the members init..set_input_state, then serialize_size..load_game_special.
        static const char kNames[] =
            "init\0deinit\0api_version\0get_system_info\0get_system_av_info\0reset\0run\0"
            "load_game\0unload_game\0set_environment\0set_video_refresh\0set_audio_sample\0"
            "set_audio_sample_batch\0set_input_poll\0set_input_state\0"
            "serialize_size\0serialize\0unserialize\0set_controller_port_device\0get_region\0"
            "get_memory_data\0get_memory_size\0load_game_special\0";
        static_assert(offsetof(LibretroCore, load_game_special) == offsetof(LibretroCore, init) + (kTotal - 1) * sizeof(void*),
                      "function pointers must be contiguous");

        AliasPtr* slot = reinterpret_cast<AliasPtr*>(&init);
        const char* name = kNames;
        char sym[48] = "retro_";
        for (int i = 0; i < kTotal; ++i, ++slot) {
            size_t len = strlen(name) + 1;
            memcpy(sym + 6, name, len);
            *slot = lib_sym(handle, sym);
            if (!*slot && i < kRequired) {
                tretro_log("[core] missing symbol: %s", sym);
                unload();
                return false;
            }
            name += len;
        }
        return true;
    }

    void unload() {
        if (handle) { lib_close(handle); }
        *this = LibretroCore{};
    }
};


#endif
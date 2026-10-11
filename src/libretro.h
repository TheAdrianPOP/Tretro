#pragma once
#ifndef LIBRETRO_H
#define LIBRETRO_H


#include <stdint.h>
#include <stddef.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0,  
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,  
    RETRO_PIXEL_FORMAT_RGB565   = 2,  
    RETRO_PIXEL_FORMAT_UNKNOWN  = 0x7FFFFFFF
};

enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = 0x7FFFFFFF
};

enum retro_language {
    RETRO_LANGUAGE_ENGLISH    = 0,
    RETRO_LANGUAGE_DUMMY      = 0x7FFFFFFF
};

#define RETRO_ENVIRONMENT_EXPERIMENTAL         0x10000u

#define RETRO_ENVIRONMENT_SET_ROTATION         1u
#define RETRO_ENVIRONMENT_GET_OVERSCAN         2u
#define RETRO_ENVIRONMENT_GET_CAN_DUPE         3u
#define RETRO_ENVIRONMENT_SET_MESSAGE          6u
#define RETRO_ENVIRONMENT_SHUTDOWN             7u
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL 8u
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY  9u
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT     10u
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS 11u
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK 12u
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE 13u
#define RETRO_ENVIRONMENT_SET_HW_RENDER        14u
#define RETRO_ENVIRONMENT_GET_VARIABLE         15u
#define RETRO_ENVIRONMENT_SET_VARIABLES        16u
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE  17u
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME  18u
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH    19u
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK 21u
#define RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK   22u
#define RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE 23u
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE    27u
#define RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY 30u
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY   31u
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO   32u
#define RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK 33u
#define RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO   34u
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO  35u
#define RETRO_ENVIRONMENT_SET_GEOMETRY         37u
#define RETRO_ENVIRONMENT_GET_USERNAME         38u
#define RETRO_ENVIRONMENT_GET_LANGUAGE         39u

#define RETRO_ENVIRONMENT_GET_VFS_INTERFACE \
    (45u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE \
    (47u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_FASTFORWARDING \
    (49u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_INPUT_BITMASKS \
    (51u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION \
    (52u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS \
    (53u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL \
    (54u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY \
    (55u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER \
    (56u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION \
    (57u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE \
    (58u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 \
    (67u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL \
    (68u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK \
    (69u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_VARIABLE \
    (70u | RETRO_ENVIRONMENT_EXPERIMENTAL)

#define RETRO_HW_FRAME_BUFFER_VALID ((void*)-1)

enum retro_hw_context_type {
    RETRO_HW_CONTEXT_NONE             = 0,
    RETRO_HW_CONTEXT_OPENGL           = 1,
    RETRO_HW_CONTEXT_OPENGLES2        = 2,
    RETRO_HW_CONTEXT_OPENGL_CORE      = 3,
    RETRO_HW_CONTEXT_OPENGLES3        = 4,
    RETRO_HW_CONTEXT_OPENGLES_VERSION = 5,
    RETRO_HW_CONTEXT_VULKAN           = 6,
    RETRO_HW_CONTEXT_DUMMY            = 0x7FFFFFFF
};

#define RETRO_DEVICE_NONE     0u
#define RETRO_DEVICE_JOYPAD   1u
#define RETRO_DEVICE_MOUSE    2u
#define RETRO_DEVICE_KEYBOARD 3u
#define RETRO_DEVICE_LIGHTGUN 4u
#define RETRO_DEVICE_ANALOG   5u
#define RETRO_DEVICE_POINTER  6u

#define RETRO_DEVICE_ID_JOYPAD_B       0u
#define RETRO_DEVICE_ID_JOYPAD_Y       1u
#define RETRO_DEVICE_ID_JOYPAD_SELECT  2u
#define RETRO_DEVICE_ID_JOYPAD_START   3u
#define RETRO_DEVICE_ID_JOYPAD_UP      4u
#define RETRO_DEVICE_ID_JOYPAD_DOWN    5u
#define RETRO_DEVICE_ID_JOYPAD_LEFT    6u
#define RETRO_DEVICE_ID_JOYPAD_RIGHT   7u
#define RETRO_DEVICE_ID_JOYPAD_A       8u
#define RETRO_DEVICE_ID_JOYPAD_X       9u
#define RETRO_DEVICE_ID_JOYPAD_L      10u
#define RETRO_DEVICE_ID_JOYPAD_R      11u
#define RETRO_DEVICE_ID_JOYPAD_L2     12u
#define RETRO_DEVICE_ID_JOYPAD_R2     13u
#define RETRO_DEVICE_ID_JOYPAD_L3     14u
#define RETRO_DEVICE_ID_JOYPAD_R3     15u
#define RETRO_DEVICE_ID_JOYPAD_MASK  256u

#define RETRO_DEVICE_INDEX_ANALOG_LEFT   0u
#define RETRO_DEVICE_INDEX_ANALOG_RIGHT  1u
#define RETRO_DEVICE_INDEX_ANALOG_BUTTON 2u
#define RETRO_DEVICE_ID_ANALOG_X         0u
#define RETRO_DEVICE_ID_ANALOG_Y         1u

#define RETROK_KP0         256
#define RETROK_KP1         257
#define RETROK_KP2         258
#define RETROK_KP3         259
#define RETROK_KP4         260
#define RETROK_KP5         261
#define RETROK_KP6         262
#define RETROK_KP7         263
#define RETROK_KP8         264
#define RETROK_KP9         265
#define RETROK_KP_PERIOD   266
#define RETROK_KP_DIVIDE   267
#define RETROK_KP_MULTIPLY 268
#define RETROK_KP_MINUS    269
#define RETROK_KP_PLUS     270
#define RETROK_KP_ENTER    271
#define RETROK_KP_EQUALS   272
#define RETROK_UNKNOWN     0
#define RETROK_BACKSPACE   8
#define RETROK_TAB         9
#define RETROK_RETURN     13
#define RETROK_ESCAPE     27
#define RETROK_SPACE      32
#define RETROK_DELETE    127
#define RETROK_UP        273
#define RETROK_DOWN      274
#define RETROK_RIGHT     275
#define RETROK_LEFT      276
#define RETROK_INSERT    277
#define RETROK_HOME      278
#define RETROK_END       279
#define RETROK_PAGEUP    280
#define RETROK_PAGEDOWN  281
#define RETROK_F1        282
#define RETROK_F2        283
#define RETROK_F3        284
#define RETROK_F4        285
#define RETROK_F5        286
#define RETROK_F6        287
#define RETROK_F7        288
#define RETROK_F8        289
#define RETROK_F9        290
#define RETROK_F10       291
#define RETROK_F11       292
#define RETROK_F12       293
#define RETROK_LSHIFT    304
#define RETROK_RSHIFT    303
#define RETROK_LCTRL     306
#define RETROK_RCTRL     305
#define RETROK_LALT      308
#define RETROK_RALT      307

#define RETRO_MEMORY_SAVE_RAM 0
#define RETRO_MEMORY_RTC 1


struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float    aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool        need_fullpath;
    bool        block_extract;
};

struct retro_game_info {
    const char *path;
    const void *data;
    size_t      size;
    const char *meta;
};

typedef bool (*retro_set_eject_state_t)(bool ejected);
typedef bool (*retro_get_eject_state_t)(void);
typedef unsigned (*retro_get_image_index_t)(void);
typedef bool (*retro_set_image_index_t)(unsigned index);
typedef unsigned (*retro_get_num_images_t)(void);
typedef bool (*retro_replace_image_index_t)(unsigned index, const struct retro_game_info *info);
typedef bool (*retro_add_image_index_t)(void);
typedef bool (*retro_set_initial_image_t)(unsigned index, const char *path);
typedef bool (*retro_get_image_path_t)(unsigned index, char *path, size_t len);
typedef bool (*retro_get_image_label_t)(unsigned index, char *label, size_t len);

struct retro_disk_control_callback {
    retro_set_eject_state_t     set_eject_state;
    retro_get_eject_state_t     get_eject_state;
    retro_get_image_index_t     get_image_index;
    retro_set_image_index_t     set_image_index;
    retro_get_num_images_t      get_num_images;
    retro_replace_image_index_t replace_image_index;
    retro_add_image_index_t     add_image_index;
};

struct retro_disk_control_ext_callback {
    retro_set_eject_state_t     set_eject_state;
    retro_get_eject_state_t     get_eject_state;
    retro_get_image_index_t     get_image_index;
    retro_set_image_index_t     set_image_index;
    retro_get_num_images_t      get_num_images;
    retro_replace_image_index_t replace_image_index;
    retro_add_image_index_t     add_image_index;
    retro_set_initial_image_t   set_initial_image;
    retro_get_image_path_t      get_image_path;
    retro_get_image_label_t     get_image_label;
};

struct retro_message {
    const char *msg;
    unsigned    frames;
};

struct retro_log_callback {
    void (*log)(enum retro_log_level level, const char *fmt, ...);
};

struct retro_variable {
    const char *key;
    const char *value;
};

#define RETRO_NUM_CORE_OPTION_VALUES_MAX 128

struct retro_core_option_value {
    const char *value;
    const char *label;
};

struct retro_core_option_definition {
    const char                      *key;
    const char                      *desc;
    const char                      *info;
    struct retro_core_option_value   values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
    const char                      *default_value;
};

struct retro_core_options_intl {
    struct retro_core_option_definition *us;
    struct retro_core_option_definition *local;
};

struct retro_core_option_v2_category {
    const char *key;
    const char *desc;
    const char *info;
};

struct retro_core_option_v2_definition {
    const char                      *key;
    const char                      *desc;
    const char                      *desc_categorized;
    const char                      *info;
    const char                      *info_categorized;
    const char                      *category_key;
    struct retro_core_option_value   values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
    const char                      *default_value;
};

struct retro_core_options_v2 {
    struct retro_core_option_v2_category   *categories;
    struct retro_core_option_v2_definition *definitions;
};

struct retro_core_options_v2_intl {
    struct retro_core_options_v2 *us;
    struct retro_core_options_v2 *local;
};

struct retro_core_options_update_display_callback {
    bool (*update_visibility)(void);
};

struct retro_input_descriptor {
    unsigned    port;
    unsigned    device;
    unsigned    index;
    unsigned    id;
    const char *description;
};

struct retro_keyboard_callback {
    void (*callback)(bool down, unsigned keycode,
                     uint32_t character, uint16_t key_modifiers);
};

struct retro_hw_render_callback {
    unsigned context_type;          
    void   (*context_reset)(void);
    uintptr_t (*get_current_framebuffer)(void);
    void *(*get_proc_address)(const char *sym);
    bool   bottom_left_origin;
    unsigned version_major;
    unsigned version_minor;
    bool     cache_context;
    void   (*context_destroy)(void);
    bool     debug_context;
    bool     depth;                 
    bool     stencil;               
};

struct retro_frame_time_callback {
    void     (*callback)(int64_t usec);
    int64_t  reference;
};

struct retro_audio_callback {
    void (*callback)(void);
    void (*set_state)(bool enabled);
};

enum retro_rumble_effect {
    RETRO_RUMBLE_STRONG = 0,
    RETRO_RUMBLE_WEAK = 1,
    RETRO_RUMBLE_DUMMY = 0x7FFFFFFF
};

struct retro_rumble_interface {
    bool (*set_rumble_state)(unsigned port, enum retro_rumble_effect effect, uint16_t strength);
};

typedef bool     (*retro_environment_t)         (unsigned cmd, void *data);
typedef void     (*retro_video_refresh_t)       (const void *data,
                                                   unsigned width,
                                                   unsigned height,
                                                   size_t pitch);
typedef void     (*retro_audio_sample_t)        (int16_t left, int16_t right);
typedef size_t   (*retro_audio_sample_batch_t)  (const int16_t *data,
                                                   size_t frames);
typedef void     (*retro_input_poll_t)          (void);
typedef int16_t  (*retro_input_state_t)         (unsigned port,
                                                   unsigned device,
                                                   unsigned index,
                                                   unsigned id);

#ifdef __cplusplus
}
#endif
#endif 
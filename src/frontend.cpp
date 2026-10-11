#include "frontend.h"
#include "autodetect.h"
#include "path_utils.h"
#include "config_manager.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

extern void gameCfgLoad(const char* save_dir, const char* content_path, const char* core_path);
extern void gameCfgSave(const char* save_dir, const char* content_path, const char* core_path);


void DiskControl::reset() {
    *this = DiskControl{};
}

void DiskControl::setCallbacks(const void* cb, bool ext) {
    if (!cb) return;
    reset();
    supported     = true;
    ext_interface = ext;

    const auto* base = (const retro_disk_control_callback*)cb;
    set_eject_state     = base->set_eject_state;
    get_eject_state     = base->get_eject_state;
    get_image_index     = base->get_image_index;
    set_image_index     = base->set_image_index;
    get_num_images      = base->get_num_images;
    replace_image_index = base->replace_image_index;
    add_image_index     = base->add_image_index;

    if (ext) {
        const auto* ext_cb = (const retro_disk_control_ext_callback*)cb;
        set_initial_image = ext_cb->set_initial_image;
        get_image_path    = ext_cb->get_image_path;
        get_image_label   = ext_cb->get_image_label;
    }
}

bool DiskControl::getEject() const {
    return (supported && get_eject_state) ? get_eject_state() : false;
}

bool DiskControl::setEject(bool ejected) {
    return (supported && set_eject_state) ? set_eject_state(ejected) : false;
}

bool DiskControl::toggleEject() {
    return setEject(!getEject());
}

unsigned DiskControl::getNumImages() const {
    return (supported && get_num_images) ? get_num_images() : 0;
}

unsigned DiskControl::getImageIndex() const {
    return (supported && get_image_index) ? get_image_index() : 0;
}

bool DiskControl::setImageIndex(unsigned index) {
    return (supported && set_image_index) ? set_image_index(index) : false;
}

bool DiskControl::changeDisc(unsigned index) {
    if (!supported || !set_image_index) return false;
    const bool was_ejected = getEject();
    if (!was_ejected) setEject(true);
    const bool ok = setImageIndex(index);
    if (!was_ejected) setEject(false);
    return ok;
}

bool DiskControl::getImageLabel(unsigned index, char* label, size_t len) const {
    if (!label || len == 0) return false;
    label[0] = '\0';

    if (supported && get_image_label && get_image_label(index, label, len) && label[0] != '\0') {
        return true;
    }
    if (supported && get_image_path) {
        char path[512] = "";
        if (get_image_path(index, path, sizeof(path)) && path[0] != '\0') {
            const char* fn = getPathFilename(path);
            snprintf(label, len, "%s", fn ? fn : path);
            return true;
        }
    }
    snprintf(label, len, "Disc %u", index + 1);
    return true;
}

bool DiskControl::appendImage(const char* path) {
    if (!supported || !add_image_index || !replace_image_index) return false;

    const bool was_ejected = getEject();
    if (!was_ejected) setEject(true);

    const unsigned new_idx = getNumImages();
    if (!add_image_index()) {
        if (!was_ejected) setEject(false);
        return false;
    }

    retro_game_info info{};
    info.path = path;
    const bool ok = replace_image_index(new_idx, &info);
    if (ok) {
        setImageIndex(new_idx);
        setEject(false);
    } else if (!was_ejected) {
        setEject(false);
    }
    return ok;
}


void tretro_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Frontend::get().logMessageV(fmt, args);
    va_end(args);
}

static void retro_log_impl(retro_log_level level, const char* fmt, ...) {
    static const char* const prefix[] = {"[dbg]", "[inf]", "[wrn]", "[ERR]"};
    const int idx = (level >= 0 && level <= 3) ? (int)level : 3;

    char new_fmt[512];
    snprintf(new_fmt, sizeof(new_fmt), "%s %s", prefix[idx], fmt);

    va_list ap;
    va_start(ap, fmt);
    Frontend::get().logMessageV(new_fmt, ap);
    va_end(ap);
}

Frontend& Frontend::get() {
    static Frontend* instance = new Frontend();
    return *instance;
}

Frontend::Frontend()
    : threads(0)
    , audio_ring(16384)
    , audio_buffered(0)
{
    system_dir = "./system";
    save_dir   = "./saves";
}

void Frontend::logMessageV(const char* fmt, va_list args) {
    const int idx = (console_log_head + console_log_count) % kMaxConsoleLogs;
    char* dst = console_logs[idx];

    vsnprintf(dst, kMaxLogLen, fmt, args);
    dst[kMaxLogLen - 1] = '\0';
    int len = (int)strlen(dst);

    while (len > 0 && (dst[len - 1] == '\n' || dst[len - 1] == '\r')) {
        dst[--len] = '\0';
    }

    if (console_log_count < kMaxConsoleLogs) {
        console_log_count++;
    } else {
        console_log_head = (console_log_head + 1) % kMaxConsoleLogs;
    }
}

void Frontend::logMessage(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logMessageV(fmt, args);
    va_end(args);
}


void Frontend::loadGameConfig() {
    gameCfgLoad(save_dir.c_str(), content_path.c_str(), core_path.c_str());
}

void Frontend::saveGameConfig() {
    if (core.loaded()) {
        gameCfgSave(save_dir.c_str(), content_path.c_str(), core_path.c_str());
    }
}

void Frontend::setVar(const char* key, const char* value) {
    if (!key || !value) return;
    for (size_t i = 0; i < core_vars.size(); i++) {
        if (core_vars[i].key == key) {
            core_vars[i].current = value;
            vars_updated = true;
            break;
        }
    }

    bool found = false;
    for (size_t i = 0; i < game_options.size(); i++) {
        if (game_options[i].key == key) {
            game_options[i].value = value;
            found = true;
            break;
        }
    }
    if (!found) {
        game_options.push_back({DynString(key), DynString(value)});
    }
    saveGameConfig();
}

const char* Frontend::getVar(const char* key) const {
    if (!key) return nullptr;
    for (size_t i = 0; i < core_vars.size(); i++) {
        if (core_vars[i].key == key) {
            if (!core_vars[i].current.empty()) return core_vars[i].current.c_str();
            if (!core_vars[i].def.empty())     return core_vars[i].def.c_str();
            return nullptr;
        }
    }
    return nullptr;
}

static void upsertCoreVarRaw(Frontend& f, const char* key, const char* desc, const char* def_val, const char* raw_opts, const char* raw_labels = "") {
    if (!key) return;
    CoreVar* cv = nullptr;
    for (size_t j = 0; j < f.core_vars.size(); j++) {
        if (f.core_vars[j].key == key) {
            cv = &f.core_vars[j];
            break;
        }
    }
    if (!cv) {
        f.core_vars.push_back({});
        cv = &f.core_vars.back();
        cv->key = key;
    }
    if (desc && desc[0]) cv->desc = desc;
    cv->def = def_val  ? def_val  : "";
    cv->raw = raw_opts ? raw_opts : "";
    if (raw_labels && raw_labels[0]) cv->raw_labels = raw_labels;

    if (cv->current.empty()) {
        for (size_t k = 0; k < f.game_options.size(); ++k) {
            if (f.game_options[k].key == key) {
                cv->current = f.game_options[k].value;
                break;
            }
        }
    }
}

static void upsertCoreVar(Frontend& f, const char* key, const char* desc, const char* def_val, const retro_core_option_value* values) {
    if (!values) {
        upsertCoreVarRaw(f, key, desc, def_val, "");
        return;
    }
    DynString raw_buf;
    DynString label_buf;
    bool has_labels = false;
    for (int j = 0; values[j].value; j++) {
        if (j > 0) {
            raw_buf.append('|');
            label_buf.append('|');
        }
        raw_buf.append(values[j].value);
        if (values[j].label && values[j].label[0]) {
            label_buf.append(values[j].label);
            has_labels = true;
        } else {
            label_buf.append(values[j].value);
        }
    }
    upsertCoreVarRaw(f, key, desc, def_val, raw_buf.c_str(), has_labels ? label_buf.c_str() : "");
}

void Frontend::registerVars(const retro_variable* vars) {
    if (!vars) return;
    for (int i = 0; vars[i].key; i++) {
        const char* val = vars[i].value;
        const char* semi = val ? strchr(val, ';') : nullptr;
        if (!semi) {
            upsertCoreVarRaw(*this, vars[i].key, "", "", "");
            continue;
        }

        const char* d_start = val;
        while (*d_start == ' ') d_start++;
        const char* d_end = semi;
        while (d_end > d_start && *(d_end - 1) == ' ') d_end--;
        DynString desc(d_start, (size_t)(d_end - d_start));

        const char* raw = semi + 1;
        while (*raw == ' ') raw++;

        const char* pipe = strchr(raw, '|');
        if (pipe) {
            DynString def(raw, (size_t)(pipe - raw));
            upsertCoreVarRaw(*this, vars[i].key, desc.c_str(), def.c_str(), raw);
        } else {
            upsertCoreVarRaw(*this, vars[i].key, desc.c_str(), raw, raw);
        }
    }
}

void Frontend::registerVarsV2(const retro_core_option_v2_definition* defs) {
    if (!defs) return;
    for (int i = 0; defs[i].key; i++) {
        const char* desc = (defs[i].desc && defs[i].desc[0]) ? defs[i].desc : defs[i].desc_categorized;
        upsertCoreVar(*this, defs[i].key, desc, defs[i].default_value, defs[i].values);
    }
}


static uintptr_t getCurrentFramebufferCb() {
    return Frontend::get().gl.getCurrentFramebuffer();
}

static bool setupHwRender(Frontend& f, retro_hw_render_callback* hw) {
    if (g_cfg.renderer_mode == 1) {
        f.logMessage("[inf] [gl] Software rendering forced; HW render declined");
        return false;
    }
    if (!hw) return false;

    switch (hw->context_type) {
        case RETRO_HW_CONTEXT_OPENGL:
        case RETRO_HW_CONTEXT_OPENGL_CORE:
        case RETRO_HW_CONTEXT_OPENGLES2:
        case RETRO_HW_CONTEXT_OPENGLES3:
        case RETRO_HW_CONTEXT_OPENGLES_VERSION:
            break;
        default:
            f.logMessage("[wrn] [gl] Unsupported HW context %u", hw->context_type);
            return false;
    }

    if (!f.gl.active) {
        if (!f.gl.createContext()) {
            f.logMessage("[ERR] [gl] Failed to create headless GL context");
            return false;
        }
        f.gl.active = true;
    }

    f.gl.hw_render = *hw;
    hw->get_current_framebuffer = getCurrentFramebufferCb;
    hw->get_proc_address = GLRenderContext::getProcAddress;
    f.gl.needs_context_reset = true;
    return true;
}


bool environment_cb(unsigned cmd, void* data) {
    Frontend& f = Frontend::get();

    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool*)data = true;
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto fmt = *(const retro_pixel_format*)data;
        if (fmt != RETRO_PIXEL_FORMAT_0RGB1555 &&
            fmt != RETRO_PIXEL_FORMAT_XRGB8888 &&
            fmt != RETRO_PIXEL_FORMAT_RGB565) {
            f.logMessage("[wrn] [core] Unknown pixel format %u", (unsigned)fmt);
            return false;
        }
        f.pixel_format = fmt;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char**)data = f.system_dir.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char**)data = f.save_dir.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *(const char**)data = f.core_path.c_str();
        return true;

    case RETRO_ENVIRONMENT_GET_USERNAME:
        *(const char**)data = "player";
        return true;

    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned*)data = RETRO_LANGUAGE_ENGLISH;
        return true;

    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        f.supports_no_game = *(const bool*)data;
        return true;

    case RETRO_ENVIRONMENT_SHUTDOWN:
        f.running = false;
        return true;

    case RETRO_ENVIRONMENT_SET_MESSAGE:
        f.logMessage("[osd] %s", ((const retro_message*)data)->msg);
        return true;

    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((retro_log_callback*)data)->log = retro_log_impl;
        return true;

    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        f.disk_control.setCallbacks(data, false);
        if (data) f.logMessage("[inf] [disk] Disk control registered");
        return true;

    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        *(unsigned*)data = 1;
        return true;

    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        f.disk_control.setCallbacks(data, true);
        if (data) f.logMessage("[inf] [disk] Disk control ext registered");
        return true;

    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: {
        auto* rumble = (retro_rumble_interface*)data;
        if (!rumble) return false;
        rumble->set_rumble_state = set_gamepad_rumble;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
        const auto* cb = (const retro_keyboard_callback*)data;
        f.keyboard_cb = cb ? *cb : retro_keyboard_callback{};
        return true;
    }

    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        return setupHwRender(f, (retro_hw_render_callback*)data);

    case RETRO_ENVIRONMENT_SET_VARIABLES:
        f.registerVars((const retro_variable*)data);
        return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* var = (retro_variable*)data;
        if (!var || !var->key) return false;
        var->value = f.getVar(var->key);
        return var->value != nullptr;
    }

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool*)data   = f.vars_updated;
        f.vars_updated = false;
        return true;

    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned*)data = 2;
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        const auto* opts = (const retro_core_options_v2*)data;
        if (opts) f.registerVarsV2(opts->definitions);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        const auto* intl = (const retro_core_options_v2_intl*)data;
        if (intl && intl->us) f.registerVarsV2(intl->us->definitions);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
        const auto* defs = (const retro_core_option_definition*)data;
        if (defs) {
            for (int i = 0; defs[i].key; i++)
                upsertCoreVar(f, defs[i].key, defs[i].desc, defs[i].default_value, defs[i].values);
        }
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
        const auto* intl = (const retro_core_options_intl*)data;
        if (intl && intl->us)
            environment_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, intl->us);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_VARIABLE: {
        const auto* var = (const retro_variable*)data;
        if (var && var->key && var->value)
            f.setVar(var->key, var->value);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const auto* av = (const retro_system_av_info*)data;
        f.av_info = *av;
        const float fps = (float)av->timing.fps;
        if (fps >= 1.0f && fps <= 1000.0f) f.target_fps = fps;
        const int sr = (int)av->timing.sample_rate;
        if (sr >= 8000 && sr <= 192000) {
            f.core_sample_rate = sr;
            f.resampler.setRates(sr, kAudioSampleRate);
        }
        return true;
    }

    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        f.av_info.geometry = *(const retro_game_geometry*)data;
        return true;

    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *(bool*)data = false;
        return true;

    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(int*)data = 3;
        return true;

    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_ROTATION:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        return false;

    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        return true;

    default:
        return false;
    }
}


static void make_dir_safe(const char* dir) {
    if (dir[0] && strcmp(dir, ".") != 0 && strcmp(dir, "..") != 0) {
#ifdef _WIN32
        _mkdir(dir);
#else
        mkdir(dir, 0777);
#endif
    }
}

static void createDirRecursive(const char* path) {
    if (!path || !path[0]) return;
    char temp[512];
    const size_t len = strlen(path);
    if (len >= sizeof(temp)) return;
    memcpy(temp, path, len + 1);

    for (char* p = temp; *p; ++p) {
        if (*p == '\\') *p = '/';
        if (*p == '/' && p > temp) {
            *p = '\0';
            make_dir_safe(temp);
            *p = '/';
        }
    }
    make_dir_safe(temp);
}

static bool failInit(Frontend& f, const char* fmt, const char* arg = nullptr) {
    if (arg) f.logMessage(fmt, arg);
    else     f.logMessage("%s", fmt);
    f.core.deinit();
    f.core.unload();
    return false;
}

static void handleKeyCallback(bool down, int key_code, uint32_t character, void*) {
    extern void keyboard_callback_handler(bool down, int key_code, uint32_t character);
    keyboard_callback_handler(down, key_code, character);
}

static void checkFirmware(Frontend& f, const char* cp) {
    CoreInfoFirmware fw{};
    if (!parseFirmwareFromInfo(cp, &fw) || fw.count <= 0) return;

    int missing_req = 0, missing_opt = 0;
    for (int i = 0; i < fw.count; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", f.system_dir.c_str(), fw.entries[i].path);
        if (!pathExists(full_path)) {
            const bool opt = fw.entries[i].optional;
            const char* label = fw.entries[i].desc[0] ? fw.entries[i].desc : fw.entries[i].path;
            f.logMessage("[%s] [loader] Missing %s: %s",
                         opt ? "wrn" : "ERR",
                         opt ? "optional firmware" : "required firmware",
                         label);
            if (opt) ++missing_opt; else ++missing_req;
        }
    }
    if (missing_req > 0) {
        f.logMessage("[ERR] [loader] Missing %d required firmware files in system/", missing_req);
    } else if (missing_opt > 0) {
        f.logMessage("[inf] [loader] Required firmware present; %d optional missing", missing_opt);
    }
}

static void setupGlFbo(Frontend& f) {
    if (!f.gl.active) return;

    const unsigned fbo_w = f.av_info.geometry.max_width  ? f.av_info.geometry.max_width  : f.av_info.geometry.base_width;
    const unsigned fbo_h = f.av_info.geometry.max_height ? f.av_info.geometry.max_height : f.av_info.geometry.base_height;

    if (!f.gl.createFbo(fbo_w, fbo_h)) {
        f.logMessage("[ERR] [gl] Failed to create FBO; falling back to SW");
        f.gl.active = false;
        return;
    }

    f.hw_frame_buf.resize((size_t)fbo_w * fbo_h * 4);
    if (f.gl.needs_context_reset && f.gl.hw_render.context_reset) {
        f.gl.bindForCoreRender();
        f.gl.hw_render.context_reset();
        f.gl.needs_context_reset = false;
    }
}

bool Frontend::init(const char* cp,
                    const char* content,
                    const char* system_dir_,
                    const char* save_dir_)
{
    core_path    = cp;
    content_path = content ? content : "";
    system_dir   = system_dir_ ? system_dir_ : "./system";
    save_dir     = save_dir_   ? save_dir_   : "./saves";

    createDirRecursive(system_dir.c_str());
    createDirRecursive(save_dir.c_str());

    loadGameConfig();

    logMessage("[inf] [sys] Parallel executor active with %zu threads", threads.getThreadCount());

    checkFirmware(*this, cp);

    if (!core.load(cp)) return false;

    logMessage("[inf] [core] API: %u", core.api_version());

    core.set_environment       (environment_cb);
    core.set_video_refresh     (video_refresh_cb);
    core.set_audio_sample      (audio_sample_cb);
    core.set_audio_sample_batch(audio_sample_batch_cb);
    core.set_input_poll        (input_poll_cb);
    core.set_input_state       (input_state_cb);

    input.key_callback          = handleKeyCallback;
    input.key_callback_userdata = this;

    core.init();

    retro_system_info sinfo{};
    core.get_system_info(&sinfo);
    logMessage("[inf] [core] %s %s (need_fullpath=%d)",
               sinfo.library_name, sinfo.library_version, (int)sinfo.need_fullpath);

    bool load_ok = false;
    if (content && content[0]) {
        retro_game_info gi{};
        gi.path = content;

        if (sinfo.need_fullpath) {
            load_ok = core.load_game(&gi);
        } else {
            auto bytes = FS::readBytes(content);
            if (bytes.empty()) {
                return failInit(*this, "[ERR] [core] Cannot read content: %s", content);
            }
            gi.data = bytes.data();
            gi.size = bytes.size();
            load_ok = core.load_game(&gi);
        }
    } else if (supports_no_game) {
        load_ok = core.load_game(nullptr);
    } else {
        return failInit(*this, "[ERR] [core] '%s' requires content (-f <file>)", sinfo.library_name);
    }

    if (!load_ok) {
        return failInit(*this, "[ERR] [core] load_game failed");
    }

    core.get_system_av_info(&av_info);

    target_fps = (float)av_info.timing.fps;
    if (target_fps < 1.0f || target_fps > 1000.0f) {
        target_fps = 60.0f;
    }

    core_sample_rate = (int)av_info.timing.sample_rate;
    if (core_sample_rate < 8000 || core_sample_rate > 192000) {
        core_sample_rate = 44100;
    }

    logMessage("[inf] [core] %ux%u @ %.2ffps, audio: %dHz",
               av_info.geometry.base_width, av_info.geometry.base_height,
               (double)target_fps, core_sample_rate);

    loadSRAM();

    setupGlFbo(*this);

    audio_buffered.store(0, rt_mo::relaxed);
    resampler.setRates(core_sample_rate, kAudioSampleRate);
    audio.init();
    audio.setMasterVolume(1.0f);
    audio.setStreamCallback(audio_stream_cb, &audio_ring);

    getConsoleSize(term_w, term_h);
    if (term_w < 1) term_w = 80;
    if (term_h < 1) term_h = 25;
    display.Resize(term_w, term_h);

    GraphicsConfig default_cfg;
    prewarmRampCache(default_cfg.ascii_ramp);

    frame_clock.start();
    return true;
}

bool Frontend::saveState(const char* path) {
    if (!core.serialize_size || !core.serialize) return false;
    const size_t sz = core.serialize_size();
    if (!sz) return false;

    saveSRAM();

    PodVector<uint8_t> buf;
    buf.resize(sz);
    if (!core.serialize(buf.data(), sz) || !FS::writeBytes(path, buf.data(), sz)) {
        logMessage("[ERR] [state] Save state failed: %s", path);
        return false;
    }

    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s.meta", path);
    FILE* fmeta = fopen(meta_path, "w");
    if (fmeta) {
        char abs_core[512] = "";
        char abs_content[512] = "";
        makePathAbsolute(core_path.c_str(), abs_core, sizeof(abs_core));
        if (!content_path.empty()) {
            makePathAbsolute(content_path.c_str(), abs_content, sizeof(abs_content));
        }
        fprintf(fmeta, "core_path=%s\ncontent_path=%s\n", abs_core, abs_content);
        fclose(fmeta);
    }

    logMessage("[inf] [state] Saved %zu B -> %s", sz, path);
    return true;
}

bool Frontend::loadState(const char* path) {
    if (!core.unserialize) return false;
    auto buf = FS::readBytes(path);
    if (buf.empty()) {
        logMessage("[ERR] [state] Read failed: %s", path);
        return false;
    }

    const size_t expected = core.serialize_size ? core.serialize_size() : 0;
    if (expected > 0 && buf.size() != expected) {
        logMessage("[wrn] [state] State size mismatch (%zu != %zu)", buf.size(), expected);
    }

    if (!core.unserialize(buf.data(), buf.size())) {
        logMessage("[ERR] [state] Unserialize failed");
        return false;
    }
    logMessage("[inf] [state] Loaded %zu B <- %s", buf.size(), path);
    return true;
}

bool Frontend::saveSRAM(const char* custom_path) {
    if (!core.loaded() || !core.get_memory_data || !core.get_memory_size) return false;

    void* sram_data = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t sram_size = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);

    void* rtc_data = core.get_memory_data(RETRO_MEMORY_RTC);
    size_t rtc_size = rtc_data ? core.get_memory_size(RETRO_MEMORY_RTC) : 0;

    if ((!sram_data || sram_size == 0) && (!rtc_data || rtc_size == 0)) {
        return false;
    }

    char path[512];
    if (custom_path && custom_path[0]) {
        snprintf(path, sizeof(path), "%s", custom_path);
    } else if (!getSramPath(path, sizeof(path), save_dir.c_str(), content_path.c_str(), core_path.c_str())) {
        return false;
    }

    bool ok = true;
    if (sram_data && sram_size > 0) {
        if (FS::writeBytes(path, (const uint8_t*)sram_data, sram_size)) {
            logMessage("[inf] [sram] Saved %zu B SRAM -> %s", sram_size, path);
        } else {
            logMessage("[ERR] [sram] Failed writing SRAM to %s", path);
            ok = false;
        }
    }

    if (rtc_data && rtc_size > 0) {
        char rtc_path[512];
        snprintf(rtc_path, sizeof(rtc_path), "%s", path);
        char* dot = strrchr(rtc_path, '.');
        if (dot) {
            snprintf(dot, sizeof(rtc_path) - (size_t)(dot - rtc_path), ".rtc");
        } else {
            size_t cur_len = strlen(rtc_path);
            snprintf(rtc_path + cur_len, sizeof(rtc_path) - cur_len, ".rtc");
        }

        if (FS::writeBytes(rtc_path, (const uint8_t*)rtc_data, rtc_size)) {
            logMessage("[inf] [sram] Saved %zu B RTC -> %s", rtc_size, rtc_path);
        }
    }

    return ok;
}

bool Frontend::loadSRAM(const char* custom_path) {
    if (!core.loaded() || !core.get_memory_data || !core.get_memory_size) return false;

    void* sram_data = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t sram_size = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);

    void* rtc_data = core.get_memory_data(RETRO_MEMORY_RTC);
    size_t rtc_size = rtc_data ? core.get_memory_size(RETRO_MEMORY_RTC) : 0;

    if ((!sram_data || sram_size == 0) && (!rtc_data || rtc_size == 0)) {
        return false;
    }

    char path[512];
    if (custom_path && custom_path[0]) {
        snprintf(path, sizeof(path), "%s", custom_path);
    } else {
        if (!getSramPath(path, sizeof(path), save_dir.c_str(), content_path.c_str(), core_path.c_str())) {
            return false;
        }
        if (!fileExists(path) && !content_path.empty()) {
            const char* fn = getPathFilename(content_path.c_str());
            if (fn) {
                char cstem[128];
                const char* dot = strrchr(fn, '.');
                size_t l = dot ? (size_t)(dot - fn) : strlen(fn);
                if (l >= sizeof(cstem)) l = sizeof(cstem) - 1;
                memcpy(cstem, fn, l);
                cstem[l] = '\0';

                char alt[512];
                snprintf(alt, sizeof(alt), "%s/%s.srm", save_dir.c_str(), cstem);
                if (fileExists(alt)) {
                    snprintf(path, sizeof(path), "%s", alt);
                } else {
                    snprintf(alt, sizeof(alt), "%s/%s.sav", save_dir.c_str(), cstem);
                    if (fileExists(alt)) {
                        snprintf(path, sizeof(path), "%s", alt);
                    }
                }
            }
        }
    }

    bool loaded_any = false;
    if (sram_data && sram_size > 0 && fileExists(path)) {
        auto buf = FS::readBytes(path);
        if (!buf.empty()) {
            size_t to_copy = (buf.size() < sram_size) ? buf.size() : sram_size;
            memcpy(sram_data, buf.data(), to_copy);
            logMessage("[inf] [sram] Loaded %zu B SRAM <- %s", to_copy, path);
            loaded_any = true;
        }
    }

    if (rtc_data && rtc_size > 0) {
        char rtc_path[512];
        snprintf(rtc_path, sizeof(rtc_path), "%s", path);
        char* dot = strrchr(rtc_path, '.');
        if (dot) {
            snprintf(dot, sizeof(rtc_path) - (size_t)(dot - rtc_path), ".rtc");
        } else {
            size_t cur_len = strlen(rtc_path);
            snprintf(rtc_path + cur_len, sizeof(rtc_path) - cur_len, ".rtc");
        }

        if (fileExists(rtc_path)) {
            auto rtc_buf = FS::readBytes(rtc_path);
            if (!rtc_buf.empty()) {
                size_t to_copy = (rtc_buf.size() < rtc_size) ? rtc_buf.size() : rtc_size;
                memcpy(rtc_data, rtc_buf.data(), to_copy);
                logMessage("[inf] [sram] Loaded %zu B RTC <- %s", to_copy, rtc_path);
                loaded_any = true;
            }
        }
    }

    return loaded_any;
}

void Frontend::shutdown() {
    saveGameConfig();
    saveSRAM();
    disk_control.reset();

    if (gl.active && core.loaded() && gl.hw_render.context_destroy) {
        gl.bindForCoreRender();
        gl.hw_render.context_destroy();
    }

    if (core.loaded()) {
        core.unload_game();
        core.deinit();
        core.unload();
    }
    audio.shutdown();

    StereoFrame dummy;
    while (audio_ring.pop(dummy)) {}
    audio_buffered.store(0, rt_mo::relaxed);
    audio_sample_flush();

    if (gl.active) {
        gl.destroyContext();
    }
    gl.hw_render           = {};
    gl.needs_context_reset = false;
    hw_frame_buf.resize(0);

    pixel_format     = RETRO_PIXEL_FORMAT_0RGB1555;
    av_info          = {};
    target_fps       = 60.0f;
    core_sample_rate = 44100;

    resampler.setRates(44100, kAudioSampleRate);

    core_vars.clear();
    game_options.clear();
    vars_updated     = false;
    supports_no_game = false;
    running          = true;
    keyboard_cb      = {};
}
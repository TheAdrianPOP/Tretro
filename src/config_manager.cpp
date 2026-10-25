#include "config_manager.h"
#include "path_utils.h"

static constexpr const char* CFG_FILE = "./config.bin";

static AppSettings* s_cfg = nullptr;

AppSettings& get_cfg() {
    if (!s_cfg) {
        s_cfg = new AppSettings();
    }
    return *s_cfg;
}

struct CfgHeader {
    uint32_t magic;
    uint32_t version;
};

static constexpr uint32_t CFG_MAGIC   = 0x54524554u; 
static const uint32_t CFG_VERSION = static_cast<uint32_t>(sizeof(AppSettings));

static inline void sanitize_cfg(AppSettings& c) {
    if (static_cast<unsigned>(c.color_mode) > 4)    c.color_mode = ColorMode::TrueColor;
    if (static_cast<unsigned>(c.aspect_mode) > 4)   c.aspect_mode = 0;
    if (static_cast<unsigned>(c.brightness) > 200)  c.brightness = 100;
    if (static_cast<unsigned>(c.contrast) > 200)    c.contrast = 100;
    if (static_cast<unsigned>(c.volume_pct) > 200)  c.volume_pct = 100;
    if (static_cast<unsigned>(c.num_recent_cores) > MAX_RECENT)   c.num_recent_cores = 0;
    if (static_cast<unsigned>(c.num_recent_content) > MAX_RECENT) c.num_recent_content = 0;

    for (int i = 0; i < MAX_RECENT; i++) {
        c.recent_cores[i][511] = '\0';
        c.recent_content[i][511] = '\0';
    }
}


void cfgSave() {
    if (FILE* f = fopen(CFG_FILE, "wb")) {
        const CfgHeader hdr = { CFG_MAGIC, CFG_VERSION };
        fwrite(&hdr,       sizeof(hdr),          1, f);
        fwrite(&get_cfg(), sizeof(AppSettings), 1, f);
        fclose(f);
    }
}

void cfgLoad() {
    FILE* f = fopen(CFG_FILE, "rb");

    if (f) {
        CfgHeader hdr;
        if (fread(&hdr, sizeof(hdr), 1, f) == 1 &&
            hdr.magic == CFG_MAGIC &&
            hdr.version == CFG_VERSION) {
            AppSettings& cfg = get_cfg();
            if (fread(&cfg, sizeof(AppSettings), 1, f) == 1) {
                sanitize_cfg(cfg);
            }
        }
        fclose(f);
    }
}

void cfgPushRecent(char arr[][512], int& cnt, const char* path) {
    if (!path || !*path) return;
    if (cnt < 0) cnt = 0;
    else if (cnt > MAX_RECENT) cnt = MAX_RECENT;

    int idx = cnt;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(arr[i], path) == 0) {
            idx = i;
            break;
        }
    }

    if (idx == cnt) {
        if (cnt < MAX_RECENT) cnt++;
        else idx = MAX_RECENT - 1;
    }

    if (idx > 0) {
        memmove(arr[1], arr[0], idx * 512);
    }
    
    size_t len = strlen(path);
    if (len >= 511) len = 511;
    memcpy(arr[0], path, len);
    arr[0][len] = '\0';
}


struct GameCfgHeader {
    uint32_t magic;
    int      key_mappings[BIND_COUNT];
    int      gamepad_mappings[BIND_COUNT];
    int      mouse_sensitivity;
    int      p1_gamepad_index;
    int      p2_gamepad_index;
    uint32_t num_vars;
};

bool getGameConfigPath(char* out, size_t sz, const char* save_dir, const char* content_path, const char* core_path) {
    if (!out || sz == 0) return false;
    out[0] = '\0';
    char c_stem[128] = "", g_stem[128] = "";
    
    getPathStem(core_path, c_stem, sizeof(c_stem));
    getPathStem(content_path, g_stem, sizeof(g_stem));

    if (g_stem[0] && c_stem[0]) {
        snprintf(out, sz, "%s/%s.%s.cfg", save_dir ? save_dir : "./saves", g_stem, c_stem);
    } else if (c_stem[0]) {
        snprintf(out, sz, "%s/%s.cfg", save_dir ? save_dir : "./saves", c_stem);
    } else {
        return false;
    }
    return true;
}

void gameCfgSave(const char* save_dir, const char* content_path, const char* core_path) {
    char path[260];
    if (!getGameConfigPath(path, sizeof(path), save_dir, content_path, core_path)) return;

    FILE* f = fopen(path, "wb");
    if (!f) return;

    AppSettings& c = get_cfg();
    Frontend& fe   = Frontend::get();

    GameCfgHeader hdr;
    hdr.magic = CFG_MAGIC;
    memcpy(hdr.key_mappings, c.key_mappings, sizeof(hdr.key_mappings));
    memcpy(hdr.gamepad_mappings, c.gamepad_mappings, sizeof(hdr.gamepad_mappings));
    hdr.mouse_sensitivity = c.mouse_sensitivity;
    hdr.p1_gamepad_index  = c.p1_gamepad_index;
    hdr.p2_gamepad_index  = c.p2_gamepad_index;
    hdr.num_vars          = static_cast<uint32_t>(fe.core_vars.size());

    fwrite(&hdr, sizeof(hdr), 1, f);

    for (size_t i = 0; i < fe.core_vars.size(); ++i) {
        const auto& cv = fe.core_vars[i];
        const char* val = !cv.current.empty() ? cv.current.c_str() : (!cv.def.empty() ? cv.def.c_str() : "");
        uint16_t klen = static_cast<uint16_t>(cv.key.size());
        uint16_t vlen = static_cast<uint16_t>(strlen(val));

        fwrite(&klen, sizeof(klen), 1, f);
        fwrite(cv.key.c_str(), 1, klen, f);
        fwrite(&vlen, sizeof(vlen), 1, f);
        fwrite(val, 1, vlen, f);
    }
    fclose(f);
}

void gameCfgLoad(const char* save_dir, const char* content_path, const char* core_path) {
    AppSettings& c = get_cfg();
    Frontend& fe   = Frontend::get();

    c.resetBindings();
    c.mouse_sensitivity = c.default_mouse_sensitivity;
    fe.game_options.clear();

    char path[260];
    if (!getGameConfigPath(path, sizeof(path), save_dir, content_path, core_path)) return;

    FILE* f = fopen(path, "rb");
    if (!f) return;

    GameCfgHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) == 1 && hdr.magic == CFG_MAGIC) {
        memcpy(c.key_mappings, hdr.key_mappings, sizeof(c.key_mappings));
        memcpy(c.gamepad_mappings, hdr.gamepad_mappings, sizeof(c.gamepad_mappings));
        c.mouse_sensitivity = hdr.mouse_sensitivity;
        c.p1_gamepad_index  = hdr.p1_gamepad_index;
        c.p2_gamepad_index  = hdr.p2_gamepad_index;

        if (hdr.num_vars > 0) {
            fe.game_options.reserve(hdr.num_vars);
        }

        char kbuf[256], vbuf[256];
        for (uint32_t i = 0; i < hdr.num_vars; ++i) {
            uint16_t klen = 0, vlen = 0;
            if (fread(&klen, sizeof(klen), 1, f) != 1 || klen >= sizeof(kbuf)) break;
            if (fread(kbuf, 1, klen, f) != klen) break;
            kbuf[klen] = '\0';

            if (fread(&vlen, sizeof(vlen), 1, f) != 1 || vlen >= sizeof(vbuf)) break;
            if (fread(vbuf, 1, vlen, f) != vlen) break;
            vbuf[vlen] = '\0';

            fe.game_options.push_back({DynString(kbuf), DynString(vbuf)});
        }
    }
    fclose(f);

    for (size_t i = 0; i < fe.core_vars.size(); ++i) {
        for (size_t j = 0; j < fe.game_options.size(); ++j) {
            if (fe.core_vars[i].key == fe.game_options[j].key.c_str()) {
                fe.core_vars[i].current = fe.game_options[j].value;
                fe.vars_updated = true;
                break;
            }
        }
    }
}
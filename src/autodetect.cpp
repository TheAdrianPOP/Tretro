#include "autodetect.h"
#include "path_utils.h"
#include "frontend.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#  define L_EXT ".dll"
#elif defined(__APPLE__)
#  include <dlfcn.h>
#  define L_EXT ".dylib"
#else
#  include <dlfcn.h>
#  define L_EXT ".so"
#endif

static void join_path(char* dst, size_t sz, const char* dir, const char* file) {
    size_t dl = strlen(dir);
    if (dl >= sz) dl = sz - 1;
    memcpy(dst, dir, dl);
    safeCopy(dst + dl, file, sz - dl);
}

static char* trim_str(char* s, bool trim_quotes) {
    while (*s && (static_cast<unsigned char>(*s) <= ' ' || (trim_quotes && *s == '"'))) s++;
    char* end = s + strlen(s) - 1;
    while (end >= s && (static_cast<unsigned char>(*end) <= ' ' || (trim_quotes && *end == '"'))) {
        *end-- = '\0';
    }
    return s;
}

static bool parseInfoLine(char* line, char** out_key, char** out_val) {
    char* eq = strchr(line, '=');
    if (!eq) return false;
    *eq = '\0';

    *out_key = trim_str(line, false);
    if (!**out_key) return false;

    *out_val = trim_str(eq + 1, true);
    return true;
}

__attribute__((noinline))
static FILE* open_info_file(const char* core_path) {
    char buf[260];
    const char* base = getPathFilename(core_path);
    if (!base) return nullptr;

    snprintf(buf, sizeof(buf), "cores/info/%s", base);
    char* p = strrchr(buf, '.');
    if (p && static_cast<size_t>(p - buf + 6) < sizeof(buf)) {
        memcpy(p, ".info", 6);
    }
    FILE* f = fopen(buf, "r");
    if (!f) {
        snprintf(buf, sizeof(buf), "info/%s", base);
        p = strrchr(buf, '.');
        if (p && static_cast<size_t>(p - buf + 6) < sizeof(buf)) {
            memcpy(p, ".info", 6);
        }
        f = fopen(buf, "r");
    }
    return f;
}

bool parseFirmwareFromInfo(const char* core_path, CoreInfoFirmware* out) {
    if (!out) return false;
    FILE* f = open_info_file(core_path);
    if (!f) return false;

    memset(out, 0, sizeof(*out));
    int expected_count = -1;
    char line[512], *k, *v;

    while (fgets(line, sizeof(line), f)) {
        if (!parseInfoLine(line, &k, &v)) continue;
        if (strcmp(k, "firmware_count") == 0) {
            expected_count = atoi(v);
            continue;
        }
        if (strncmp(k, "firmware", 8) != 0) continue;

        k += 8;
        if (*k < '0' || *k > '9') continue;
        int idx = 0;
        while (*k >= '0' && *k <= '9') {
            idx = idx * 10 + (*k++ - '0');
        }
        if (idx >= kMaxCoreFirmware) continue;

        auto& entry = out->entries[idx];
        if (strcmp(k, "_path") == 0) {
            safeCopy(entry.path, v, sizeof(entry.path));
            if (idx >= out->count) out->count = idx + 1;
        } else if (strcmp(k, "_desc") == 0) {
            safeCopy(entry.desc, v, sizeof(entry.desc));
        } else if (strcmp(k, "_opt") == 0) {
            entry.optional = (v[0] == 't' || v[0] == '1');
        }
    }
    fclose(f);

    if (expected_count >= 0 && expected_count < out->count) {
        out->count = expected_count;
    }
    return out->count > 0;
}

bool getCoreMetadataFromInfo(const char* core_path, char* name, size_t n_sz, char* exts, size_t e_sz) {
    FILE* f = open_info_file(core_path);
    if (!f) return false;

    if (name && n_sz) name[0] = '\0';
    if (exts && e_sz) exts[0] = '\0';

    char line[512], *k, *v;
    bool found_any = false;
    while (fgets(line, sizeof(line), f)) {
        if (!parseInfoLine(line, &k, &v)) continue;
        if (name && n_sz && strcmp(k, "display_name") == 0) {
            safeCopy(name, v, n_sz);
            found_any = true;
        } else if (exts && e_sz && strcmp(k, "supported_extensions") == 0) {
            safeCopy(exts, v, e_sz);
            found_any = true;
        }
    }
    fclose(f);
    return found_any;
}

__attribute__((noinline))
static bool matchExt(const char* list, const char* target, int* count) {
    if (count) *count = 0;
    if (!list || !target) return false;
    if (*target == '.') target++;
    size_t tl = strlen(target);
    if (!tl) return false;

    bool matched = false;
    while (*list) {
        while (*list == '|') list++;
        if (!*list) break;
        const char* end = strchr(list, '|');
        const char* token = list;
        size_t len = end ? static_cast<size_t>(end - list) : strlen(list);
        list += (end ? (end - list) : strlen(list));

        while (len > 0 && (static_cast<unsigned char>(*token) <= ' ')) { token++; len--; }
        while (len > 0 && (static_cast<unsigned char>(token[len - 1]) <= ' ')) { len--; }

        if (len == 0 || (len == 1 && (token[0] == '/' || token[0] == '\\'))) {
            continue;
        }

        if (count) (*count)++;
        if (!matched && len == tl && strncasecmp(token, target, tl) == 0) {
            matched = true;
        }
    }
    return matched;
}

struct Candidate {
    char path[260];
    char lib_name[64];
    int  exts;
    bool match;
    int  score;
};

static inline bool isGenericMediaExt(const char* ext) {
    if (!ext) return false;
    if (*ext == '.') ext++;
    return !strcasecmp(ext, "iso") || !strcasecmp(ext, "cue") ||
           !strcasecmp(ext, "chd") || !strcasecmp(ext, "img") ||
           !strcasecmp(ext, "m3u") || !strcasecmp(ext, "m3u8") ||
           !strcasecmp(ext, "zip");
}

static inline bool isDosCore(const Candidate& c) {
    auto contains_dos = [](const char* s) {
        if (!s) return false;
        while (*s) {
            if ((s[0] == 'd' || s[0] == 'D') &&
                (s[1] == 'o' || s[1] == 'O') &&
                (s[2] == 's' || s[2] == 'S')) {
                return true;
            }
            s++;
        }
        return false;
    };
    return contains_dos(c.lib_name) || contains_dos(c.path);
}

static bool getZipInnerExtension(const char* zip_path, char* out_ext, size_t out_ext_sz) {
    if (!zip_path || !out_ext || out_ext_sz == 0) return false;
    out_ext[0] = '\0';

    FILE* f = fopen(zip_path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long fsize = ftell(f);
    if (fsize < 22) { fclose(f); return false; }

    unsigned char eocd[22];
    long eocd_off = -1;
    long search_start = fsize - 22;
    long search_end   = (fsize - 22 - 65535 > 0) ? fsize - 22 - 65535 : 0;

    for (long pos = search_start; pos >= search_end; --pos) {
        if (fseek(f, pos, SEEK_SET) != 0 || fread(eocd, 1, 22, f) < 22) break;
        if (eocd[0] == 0x50 && eocd[1] == 0x4b && eocd[2] == 0x05 && eocd[3] == 0x06) {
            eocd_off = pos;
            break;
        }
    }
    if (eocd_off < 0) { fclose(f); return false; }

    uint16_t cd_count = (uint16_t)(eocd[10] | (eocd[11] << 8));
    uint32_t cd_offset = (uint32_t)(eocd[16] | (eocd[17] << 8) | (eocd[18] << 16) | (eocd[19] << 24));
    if ((uint64_t)cd_offset >= (uint64_t)fsize) { fclose(f); return false; }
    if (fseek(f, (long)cd_offset, SEEK_SET) != 0) { fclose(f); return false; }

    char best_ext[32] = "";
    int best_prio = 0;

    char fn_buf[512];
    for (uint16_t i = 0; i < cd_count; ++i) {
        unsigned char hdr[46];
        if (fread(hdr, 1, 46, f) != 46 || hdr[0] != 0x50 || hdr[1] != 0x4b || hdr[2] != 0x01 || hdr[3] != 0x02) break;

        uint16_t fn_len = (uint16_t)(hdr[28] | (hdr[29] << 8));
        uint16_t extra_len = (uint16_t)(hdr[30] | (hdr[31] << 8));
        uint16_t comm_len = (uint16_t)(hdr[32] | (hdr[33] << 8));

        if (fn_len > 0 && fn_len < sizeof(fn_buf)) {
            if (fread(fn_buf, 1, fn_len, f) != fn_len) break;
            fn_buf[fn_len] = '\0';

            if (fn_buf[fn_len - 1] != '/' && fn_buf[fn_len - 1] != '\\' && fn_buf[0] != '.') {
                const char* dot = strrchr(fn_buf, '.');
                if (dot && dot[1]) {
                    const char* cur_ext = dot + 1;
                    int prio = 1;
                    if (!strcasecmp(cur_ext, "txt") || !strcasecmp(cur_ext, "nfo") ||
                        !strcasecmp(cur_ext, "diz") || !strcasecmp(cur_ext, "doc")) {
                        prio = 0;
                    } else if (!strcasecmp(cur_ext, "exe") || !strcasecmp(cur_ext, "com") ||
                               !strcasecmp(cur_ext, "bat")) {
                        prio = 2;
                    } else {
                        prio = 3; 
                    }

                    if (prio > best_prio) {
                        best_prio = prio;
                        safeCopy(best_ext, cur_ext, sizeof(best_ext));
                    }
                }
            }
        } else {
            if (fseek(f, fn_len, SEEK_CUR) != 0) break;
        }

        if (fseek(f, extra_len + comm_len, SEEK_CUR) != 0) break;
    }
    fclose(f);

    if (best_ext[0] && best_prio > 0) {
        safeCopy(out_ext, best_ext, out_ext_sz);
        return true;
    }
    return false;
}

__attribute__((noinline))
static void inspect_core_fallback(const char* full_path, const char* ext, Candidate& cand) {
    typedef void (*pfn_get_system_info)(retro_system_info*);

#ifdef _WIN32
    HMODULE h = LoadLibraryA(full_path);
    if (!h) return;
    auto get_info = reinterpret_cast<pfn_get_system_info>(reinterpret_cast<void*>(GetProcAddress(h, "retro_get_system_info")));
#else
    void* h = dlopen(full_path, RTLD_LAZY);
    if (!h) return;
    auto get_info = reinterpret_cast<pfn_get_system_info>(dlsym(h, "retro_get_system_info"));
#endif

    if (get_info) {
        retro_system_info info = {};
        get_info(&info);
        if (info.library_name && info.library_name[0]) {
            safeCopy(cand.lib_name, info.library_name, sizeof(cand.lib_name));
        }
        cand.match = matchExt(info.valid_extensions, ext, &cand.exts);
    }

#ifdef _WIN32
    FreeLibrary(h);
#else
    dlclose(h);
#endif
}

__attribute__((noinline))
static bool inspect_candidate(const char* full_path, const char* ext, Candidate& cand) {
    char exts[512];
    exts[0] = '\0';
    if (getCoreMetadataFromInfo(full_path, cand.lib_name, sizeof(cand.lib_name), exts, sizeof(exts))) {
        cand.match = matchExt(exts, ext, &cand.exts);
        return cand.match;
    }
    inspect_core_fallback(full_path, ext, cand);
    return cand.match;
}

__attribute__((noinline))
static void sort_candidates(const Candidate* candidates, uint8_t* idx, int count) {
    for (int i = 0; i < count; i++) idx[i] = static_cast<uint8_t>(i);

    for (int i = 0; i + 1 < count; i++) {
        for (int j = i + 1; j < count; j++) {
            const Candidate& a = candidates[idx[i]];
            const Candidate& b = candidates[idx[j]];
            bool swap_needed = false;
            if (b.match != a.match) {
                swap_needed = b.match; 
            } else if (a.match && b.match) {
                if (b.score != a.score) {
                    swap_needed = (b.score > a.score);
                } else {
                    swap_needed = (b.exts < a.exts); 
                }
            } else {
                swap_needed = (strcasecmp(b.lib_name, a.lib_name) < 0);
            }
            if (swap_needed) {
                uint8_t tm = idx[i];
                idx[i] = idx[j];
                idx[j] = tm;
            }
        }
    }
}

__attribute__((noinline))
static void emit_results(const Candidate* candidates, const uint8_t* idx, int count,
                         PodVector<DynString>& out_paths,
                         PodVector<DynString>* out_library_names,
                         PodVector<int>* out_ext_counts)
{
    for (int i = 0; i < count; i++) {
        const Candidate& cand = candidates[idx[i]];
        out_paths.push_back(DynString(cand.path));
        if (out_library_names) out_library_names->push_back(DynString(cand.lib_name));
        if (out_ext_counts) out_ext_counts->push_back(cand.exts);
    }
}

int autodetectCoreMulti(const char* content_path, const char* ,
                         PodVector<DynString>& out_paths,
                         int* out_matched_count,
                         PodVector<DynString>* out_library_names,
                         PodVector<int>* out_ext_counts)
{
    out_paths.clear();
    if (out_library_names) out_library_names->clear();
    if (out_ext_counts) out_ext_counts->clear();

    const char* ext = getFileExtension(content_path);
    char inner_ext[32] = "";
    bool is_zip = (ext && strcasecmp(ext, "zip") == 0);
    if (is_zip) {
        getZipInnerExtension(content_path, inner_ext, sizeof(inner_ext));
    }

    static const char* const dirs[] = { "./", "./cores/", "./system/", nullptr };

    Candidate candidates[MAX_CORE_CANDIDATES];
    int count = 0;
    int m_cnt = 0;
    const size_t el = sizeof(L_EXT) - 1;

    for (int d = 0; dirs[d] && count < MAX_CORE_CANDIDATES; d++) {
        for (const auto& e : FS::listDir(dirs[d])) {
            if (e.isDirectory || count >= MAX_CORE_CANDIDATES) continue;

            const char* fn = e.name.c_str();
            size_t fl = strlen(fn);
            if (fl <= el || strcasecmp(fn + fl - el, L_EXT) != 0) continue;

            char full_path[260];
            join_path(full_path, sizeof(full_path), dirs[d], fn);

            bool dup = false;
            for (int i = 0; i < count; i++) {
                if (strcmp(candidates[i].path, full_path) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            Candidate& cand = candidates[count++];
            safeCopy(cand.path, full_path, sizeof(cand.path));
            safeCopy(cand.lib_name, fn, sizeof(cand.lib_name));
            cand.exts = 0;
            cand.match = false;
            cand.score = 0;

            bool matched_inner = false;
            if (is_zip && inner_ext[0]) {
                if (inspect_candidate(full_path, inner_ext, cand)) {
                    cand.match = true;
                    matched_inner = true;
                    cand.score = 1000 + (100 - (cand.exts < 90 ? cand.exts : 90));
                }
            }

            if (!matched_inner && ext && ext[0]) {
                if (inspect_candidate(full_path, ext, cand)) {
                    cand.match = true;
                    int base = 100;
                    int specificity = (100 - (cand.exts < 90 ? cand.exts : 90));

                    if (isGenericMediaExt(ext) && isDosCore(cand)) {
                        cand.score = 10;
                    } else {
                        cand.score = base + specificity;
                    }
                }
            }

            if (cand.match) m_cnt++;
        }
    }

    uint8_t idx[MAX_CORE_CANDIDATES];
    sort_candidates(candidates, idx, count);
    emit_results(candidates, idx, count, out_paths, out_library_names, out_ext_counts);

    if (out_matched_count) *out_matched_count = m_cnt;
    return count;
}

const char* autodetectCore(const char* cp, const char* ep, char* b, size_t s) {
    if (!b || !s) return nullptr;
    PodVector<DynString> paths;
    int matched = 0;
    if (autodetectCoreMulti(cp, ep, paths, &matched, nullptr, nullptr) > 0 && matched > 0 && !paths.empty()) {
        safeCopy(b, paths[0].c_str(), s);
        return b;
    }
    return nullptr;
}
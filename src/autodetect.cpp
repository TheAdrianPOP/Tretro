// Windows-only for now.
#include "autodetect.h"
#include "path_utils.h"
#include "file_system.h"
#include "zip_util.h"

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

static const char L_EXT[] = ".dll";

/* ------------------------------------------------------------------ */
/* .info file parsing                                                 */
/* ------------------------------------------------------------------ */

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

static FILE* open_info_file(const char* core_path) {
    char stem[128], buf[192];
    getPathStem(core_path, stem, sizeof(stem));

    snprintf(buf, sizeof(buf), "cores/info/%s.info", stem);
    FILE* f = fopen(buf, "r");
    if (!f) {
        snprintf(buf, sizeof(buf), "info/%s.info", stem);
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

/* ------------------------------------------------------------------ */
/* Matching                                                           */
/* ------------------------------------------------------------------ */

// list is "a|b|c". Counts the real tokens and reports whether target is one of them.
static bool matchExt(const char* list, const char* target, int* count) {
    *count = 0;
    if (!list || !target) return false;
    if (*target == '.') target++;
    size_t tl = strlen(target);

    bool matched = false;
    while (*list) {
        const char* end = strchr(list, '|');
        size_t len = end ? static_cast<size_t>(end - list) : strlen(list);
        const char* tok = list;
        list += len;
        if (*list) list++;

        while (len > 0 && static_cast<unsigned char>(*tok) <= ' ') { tok++; len--; }
        while (len > 0 && static_cast<unsigned char>(tok[len - 1]) <= ' ') len--;
        if (len == 0 || (len == 1 && (*tok == '/' || *tok == '\\'))) continue;

        (*count)++;
        if (tl && len == tl && strncasecmp(tok, target, tl) == 0) matched = true;
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

static bool isGenericMediaExt(const char* ext) {
    static const char* const kGeneric[] = { "iso", "cue", "chd", "img", "m3u", "m3u8", "zip" };
    if (*ext == '.') ext++;
    for (const char* g : kGeneric) if (!strcasecmp(ext, g)) return true;
    return false;
}

static bool hasDos(const char* s) {
    for (; *s; ++s) {
        if ((s[0] | 32) == 'd' && (s[1] | 32) == 'o' && (s[2] | 32) == 's') return true;
    }
    return false;
}

static bool inspect_candidate(Candidate& cand, const char* ext) {
    char exts[512];
    if (getCoreMetadataFromInfo(cand.path, cand.lib_name, sizeof(cand.lib_name), exts, sizeof(exts))) {
        cand.match = matchExt(exts, ext, &cand.exts);
    }
    return cand.match;
}

/* ------------------------------------------------------------------ */
/* Zip: pick the most "game-like" file extension inside a .zip        */
/* ------------------------------------------------------------------ */

static int extPrio(const char* e) {
    static const char* const kLow[] = { "txt", "nfo", "diz", "doc" };
    static const char* const kMid[] = { "exe", "com", "bat" };
    for (const char* s : kLow) if (!strcasecmp(e, s)) return 0;
    for (const char* s : kMid) if (!strcasecmp(e, s)) return 2;
    return 3;
}

static bool getZipInnerExtension(const char* zip_path, char* out_ext, size_t out_ext_sz) {
    out_ext[0] = '\0';
    FILE* f = fopen(zip_path, "rb");
    if (!f) return false;

    // The end-of-central-directory record sits in the last bytes (after an optional short comment).
    unsigned char t[512];
    long fsize = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : -1;
    long tn = fsize < (long)sizeof(t) ? fsize : (long)sizeof(t);
    int best_prio = 0;
    char best_ext[32] = "";

    if (tn >= 22 && fseek(f, fsize - tn, SEEK_SET) == 0 && fread(t, 1, (size_t)tn, f) == (size_t)tn) {
        long p = tn - 22;
        while (p >= 0 && le32(t + p) != 0x06054b50UL) --p;

        if (p >= 0 && (long)le32(t + p + 16) < fsize && fseek(f, (long)le32(t + p + 16), SEEK_SET) == 0) {
            unsigned count = le16(t + p + 10);
            char fn[512];
            for (unsigned i = 0; i < count; ++i) {
                unsigned char hdr[46];
                if (fread(hdr, 1, 46, f) != 46 || le32(hdr) != 0x02014b50UL) break;

                unsigned fn_len  = le16(hdr + 28);
                unsigned skip    = le16(hdr + 30) + le16(hdr + 32);
                if (fn_len > 0 && fn_len < sizeof(fn)) {
                    if (fread(fn, 1, fn_len, f) != fn_len) break;
                    fn[fn_len] = '\0';

                    const char* dot = strrchr(fn, '.');
                    if (fn[fn_len - 1] != '/' && fn[fn_len - 1] != '\\' && fn[0] != '.' && dot && dot[1]) {
                        int prio = extPrio(dot + 1);
                        if (prio > best_prio) {
                            best_prio = prio;
                            safeCopy(best_ext, dot + 1, sizeof(best_ext));
                        }
                    }
                } else {
                    skip += fn_len;
                }
                if (fseek(f, (long)skip, SEEK_CUR) != 0) break;
            }
        }
    }
    fclose(f);

    if (best_prio > 0) safeCopy(out_ext, best_ext, out_ext_sz);
    return best_prio > 0;
}

/* ------------------------------------------------------------------ */
/* Detection                                                          */
/* ------------------------------------------------------------------ */

// true if b should be listed before a
static bool ranksBefore(const Candidate& b, const Candidate& a) {
    if (a.match != b.match) return b.match;
    if (!a.match) return strcasecmp(b.lib_name, a.lib_name) < 0;
    return (b.score != a.score) ? b.score > a.score : b.exts < a.exts;
}

int autodetectCoreMulti(const char* content_path,
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
    if (ext && strcasecmp(ext, "zip") == 0) getZipInnerExtension(content_path, inner_ext, sizeof(inner_ext));

    static const char* const dirs[] = { "./", "./cores/", "./system/", nullptr };

    Candidate candidates[MAX_CORE_CANDIDATES];
    int count = 0, m_cnt = 0;
    const size_t el = sizeof(L_EXT) - 1;

    for (int d = 0; dirs[d] && count < MAX_CORE_CANDIDATES; d++) {
        for (const auto& e : FS::listDir(dirs[d])) {
            if (e.isDirectory || count >= MAX_CORE_CANDIDATES) continue;

            const char* fn = e.name.c_str();
            size_t fl = strlen(fn);
            if (fl <= el || strcasecmp(fn + fl - el, L_EXT) != 0) continue;

            Candidate& cand = candidates[count++];
            snprintf(cand.path, sizeof(cand.path), "%s%s", dirs[d], fn);
            safeCopy(cand.lib_name, fn, sizeof(cand.lib_name));
            cand.exts = cand.score = 0;
            cand.match = false;

            // A zip is matched by the file type inside it first, then as a plain "zip".
            if (inner_ext[0] && inspect_candidate(cand, inner_ext)) {
                cand.score = 1000 + (100 - (cand.exts < 90 ? cand.exts : 90));
            } else if (ext && ext[0] && inspect_candidate(cand, ext)) {
                // Generic disc/archive types shouldn't prefer DOS cores.
                cand.score = (isGenericMediaExt(ext) && (hasDos(cand.lib_name) || hasDos(cand.path)))
                           ? 10 : 100 + (100 - (cand.exts < 90 ? cand.exts : 90));
            }
            if (cand.match) m_cnt++;
        }
    }

    uint8_t idx[MAX_CORE_CANDIDATES];
    for (int i = 0; i < count; i++) {
        uint8_t t = static_cast<uint8_t>(i);
        int j = i;
        while (j > 0 && ranksBefore(candidates[t], candidates[idx[j - 1]])) { idx[j] = idx[j - 1]; j--; }
        idx[j] = t;
    }

    for (int i = 0; i < count; i++) {
        const Candidate& cand = candidates[idx[i]];
        out_paths.push_back(DynString(cand.path));
        if (out_library_names) out_library_names->push_back(DynString(cand.lib_name));
        if (out_ext_counts) out_ext_counts->push_back(cand.exts);
    }

    if (out_matched_count) *out_matched_count = m_cnt;
    return count;
}
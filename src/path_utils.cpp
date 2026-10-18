#include "path_utils.h"
#include "file_system.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif
StdRedirector::StdRedirector() {
    hOrigStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    hOrigStderr = GetStdHandle(STD_ERROR_HANDLE);
    hNull = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hNull != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, hNull);
        SetStdHandle(STD_ERROR_HANDLE, hNull);
    }
}

StdRedirector::~StdRedirector() {
    if (hNull != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, hOrigStdout);
        SetStdHandle(STD_ERROR_HANDLE, hOrigStderr);
        CloseHandle(hNull);
    }
}

int drainConsoleCharsUTF8(HANDLE hInput, DWORD eventsAvail, char* out_utf8, int out_cap) {
    if (eventsAvail == 0 || out_cap <= 1) return 0;
    
    constexpr int kWbufCap = 1024;
    wchar_t wbuf[kWbufCap];
    int wlen = 0;
    int max_chars = (out_cap < kWbufCap) ? out_cap - 1 : kWbufCap - 1;

    DWORD remaining = eventsAvail;
    while (remaining > 0 && wlen < max_chars) {
        constexpr DWORD kChunk = 128;
        INPUT_RECORD evBuf[kChunk];
        DWORD toRead = (remaining < kChunk) ? remaining : kChunk;
        DWORD evRead = 0;
        if (!ReadConsoleInputW(hInput, evBuf, toRead, &evRead) || evRead == 0) break;
        remaining = (remaining > evRead) ? (remaining - evRead) : 0;

        for (DWORD i = 0; i < evRead && wlen < max_chars; ++i) {
            if (evBuf[i].EventType != KEY_EVENT || !evBuf[i].Event.KeyEvent.bKeyDown) continue;
            wchar_t wc = evBuf[i].Event.KeyEvent.uChar.UnicodeChar;
            if (wc == L'\r' || wc == L'\n' || wc == 0 || (wc < 32 && wc != L'\t')) continue;
            wbuf[wlen++] = wc;
        }

        if (remaining == 0) {
            DWORD avail = 0;
            if (GetNumberOfConsoleInputEvents(hInput, &avail) && avail > 0) {
                remaining = avail;
            }
        }
    }
    wbuf[wlen] = L'\0';

    int written = 0;
    if (wlen > 0) {
        written = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, out_utf8, out_cap - 1, nullptr, nullptr);
        if (written < 0) written = 0;
        out_utf8[written] = '\0';
    } else {
        out_utf8[0] = '\0';
    }
    return written;
}

void safeCopy(char* dst, const char* src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) { *dst = '\0'; return; }
    size_t i = 0;
    while (i + 1 < max_len && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void getPathStem(const char* path, char* out_buf, size_t out_sz) {
    if (out_sz == 0) return;
    out_buf[0] = '\0';
    const char* base = getPathFilename(path);
    if (!base) return;
    const char* dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out_buf, base, len);
    out_buf[len] = '\0';
}

static inline bool testPath(const char* cand, char* out_path, size_t out_sz) {
    resolvePath(cand, out_path, out_sz);
    if (fileExists(out_path)) return true;
    out_path[0] = '\0';
    return false;
}

static inline bool isStripChar(char c) {
    return c == ' ' || c == '"' || c == '\'' || c == '\r' || c == '\n' || c == '\t';
}

static char* trimStr(char* str) {
    while (*str && isspace((unsigned char)*str)) str++;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
    return str;
}


bool resolveShortcut(const char* , char* , size_t ) {
    return false;
}

void resolvePath(const char* input_path, char* output_buf, size_t output_sz) {
    if (!input_path || output_sz == 0) {
        if (output_sz > 0) output_buf[0] = '\0';
        return;
    }
    safeCopy(output_buf, input_path, output_sz);
}

const char* stripPathC(const char* str, char* out_buf, size_t out_sz) {
    if (out_sz == 0) return "";
    out_buf[0] = '\0';
    if (!str) return out_buf;

    while (*str && isStripChar(*str)) str++;
    size_t len = strlen(str);
    while (len > 0 && isStripChar(str[len - 1])) len--;

    if (len >= out_sz) len = out_sz - 1;
    memcpy(out_buf, str, len);
    out_buf[len] = '\0';
    return out_buf;
}

const char* getPathFilename(const char* path) {
    if (!path) return nullptr;
    const char* base = strrchr(path, '/');
    if (!base) base = strrchr(path, '\\');
    return base ? base + 1 : path;
}

const char* getFileExtension(const char* path) {
    const char* base = getPathFilename(path);
    if (!base) return nullptr;
    const char* dot = strrchr(base, '.');
    if (!dot || dot == base) return nullptr;
    return dot + 1;
}

void makePathAbsolute(const char* input_path, char* output_buf, size_t output_sz) {
    if (!input_path || output_sz == 0) {
        if (output_sz > 0) output_buf[0] = '\0';
        return;
    }
#ifdef _WIN32
    wchar_t win[MAX_PATH * 2], wout[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, input_path, -1, win, (int)(sizeof(win) / sizeof(win[0]))) > 0) {
        DWORD len = GetFullPathNameW(win, (DWORD)(sizeof(wout) / sizeof(wout[0])), wout, nullptr);
        if (len > 0 && len < (DWORD)(sizeof(wout) / sizeof(wout[0]))) {
            if (WideCharToMultiByte(CP_UTF8, 0, wout, -1, output_buf, (int)output_sz, nullptr, nullptr) > 0) {
                return;
            }
        }
    }
#else
    if (realpath(input_path, output_buf) != nullptr) return;
#endif
    safeCopy(output_buf, input_path, output_sz);
}

#ifdef _WIN32
void setWorkDirToExeDir(const char* ) {
    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        char* last_slash = strrchr(exe_path, '\\');
        if (!last_slash) last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            SetCurrentDirectoryA(exe_path);
        }
    }
}
#else
void setWorkDirToExeDir(const char* argv0) {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* dir = dirname(exe_path);
        chdir(dir);
    } else if (argv0) {
        safeCopy(exe_path, argv0, sizeof(exe_path));
        char* dir = dirname(exe_path);
        chdir(dir);
    }
}
#endif

bool fileExists(const char* path) {
    if (!path || !*path) return false;
#ifdef _WIN32
    wchar_t wpath[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)(sizeof(wpath)/sizeof(wpath[0]))) > 0) {
        DWORD attrs = GetFileAttributesW(wpath);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
#endif
}

bool pathExists(const char* path) {
    if (!path || !*path) return false;
#ifdef _WIN32
    wchar_t wpath[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)(sizeof(wpath)/sizeof(wpath[0]))) > 0) {
        DWORD attrs = GetFileAttributesW(wpath);
        if (attrs != INVALID_FILE_ATTRIBUTES) return true;
    }
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

// Case-insensitive; accepts nullptr (no extension).
bool isCoreExt(const char* ext) {
    return ext && (!strcasecmp(ext, "dll") || !strcasecmp(ext, "so") || !strcasecmp(ext, "dylib"));
}

static void unescapeUrl(char* s) {
    char* src = s;
    char* dst = s;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int v1 = hexVal(src[1]);
            int v2 = hexVal(src[2]);
            if (v1 >= 0 && v2 >= 0) {
                *dst++ = (char)((v1 << 4) | v2);
                src += 3;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

bool extractFirstValidPath(const char* raw, size_t raw_len, char* out_path, size_t out_sz) {
    if (!raw || raw_len == 0 || !out_path || out_sz == 0) return false;

    const char* p = raw;
    const char* end = raw + raw_len;
    char candidate[512];
    char stripped[512];

    while (p < end) {
        while (p < end && (isspace((unsigned char)*p) || *p == '&')) p++;
        if (p >= end) break;

        if (*p == '"' || *p == '\'') {
            char quote = *p++; 
            const char* start = p;
            while (p < end && *p != quote) p++;
            
            size_t len = (size_t)(p - start);
            if (len > 0 && len < sizeof(candidate)) {
                memcpy(candidate, start, len);
                candidate[len] = '\0';
                stripPathC(candidate, stripped, sizeof(stripped));

                if (strncmp(stripped, "file:///", 8) == 0) {
                    unescapeUrl(stripped + 8);
                    if (testPath(stripped + 8, out_path, out_sz)) return true;
                }
                if (testPath(stripped, out_path, out_sz)) return true;
                if (testPath(candidate, out_path, out_sz)) return true;
            }
            if (p < end) p++; 
        } 
        else {
            const char* line_end = p;
            while (line_end < end && *line_end != '\n' && *line_end != '\r' && *line_end != '\t') {
                line_end++;
            }
            
            size_t line_len = (size_t)(line_end - p);
            if (line_len > 0) {
                size_t copy_len = line_len < sizeof(candidate) - 1 ? line_len : sizeof(candidate) - 1;
                memcpy(candidate, p, copy_len);
                candidate[copy_len] = '\0';

                stripPathC(candidate, stripped, sizeof(stripped));
                if (strncmp(stripped, "file:///", 8) == 0) {
                    unescapeUrl(stripped + 8);
                    if (testPath(stripped + 8, out_path, out_sz)) return true;
                }
                if (testPath(stripped, out_path, out_sz)) return true;

                for (size_t i = 0; stripped[i] != '\0'; i++) {
                    if (stripped[i] == ' ') {
                        stripped[i] = '\0';
                        stripPathC(stripped, candidate, sizeof(candidate));
                        stripped[i] = ' ';

                        if (testPath(candidate, out_path, out_sz)) return true;
                    }
                }
            }
            p = line_end;
        }
    }
    return false;
}

void getSlotPath(char* buf, size_t sz, const char* save_dir, const char* content_path, const char* core_path, int slot) {
    char core_stem[128];
    char content_stem[128];
    getPathStem(core_path, core_stem, sizeof(core_stem));
    getPathStem(content_path, content_stem, sizeof(content_stem));

    if (content_stem[0] != '\0') {
        snprintf(buf, sz, "%s/%s.%s.state%d", save_dir, content_stem, core_stem, slot);
    } else if (core_stem[0] != '\0') {
        snprintf(buf, sz, "%s/%s.state%d", save_dir, core_stem, slot);
    } else {
        snprintf(buf, sz, "%s/state_%d.sav", save_dir, slot);
    }
}

bool getFileModTime(const char* path, char* out_buf, size_t out_sz) {
    if (!path || !out_buf || out_sz == 0) return false;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        FILETIME ftLocal;
        FileTimeToLocalFileTime(&data.ftLastWriteTime, &ftLocal);
        SYSTEMTIME st;
        if (FileTimeToSystemTime(&ftLocal, &st)) {
            snprintf(out_buf, out_sz, "%04d-%02d-%02d %02d:%02d:%02d",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond);
            return true;
        }
    }
    return false;
#else
    struct stat result;
    if (stat(path, &result) == 0) {
        time_t mod_time = result.st_mtime;
        struct tm* lt = localtime(&mod_time);
        if (lt) {
            strftime(out_buf, out_sz, "%Y-%m-%d %H:%M:%S", lt);
            return true;
        }
    }
    return false;
#endif
}

bool getMetaPaths(const char* state_path, char* core_out, size_t core_sz, char* content_out, size_t content_sz) {
    char meta_path[MAX_PATH];
    snprintf(meta_path, sizeof(meta_path), "%s.meta", state_path);
    FILE* f = fopen(meta_path, "r");
    if (!f) return false;

    core_out[0] = '\0';
    content_out[0] = '\0';

    char line[MAX_PATH];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char* key = trimStr(line);
        char* val = trimStr(eq + 1);

        if (strcmp(key, "core_path") == 0) {
            makePathAbsolute(val, core_out, core_sz);
        } else if (strcmp(key, "content_path") == 0 && val[0] != '\0') {
            makePathAbsolute(val, content_out, content_sz);
        }
    }
    fclose(f);
    return (core_out[0] != '\0');
}

static inline bool parseSlotNumber(const char* s, int& out_slot) {
    if (!s || !*s) return false;
    int val = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        val = val * 10 + (*s++ - '0');
    }
    out_slot = val;
    return true;
}

void scanSaveStates(const char* save_dir, const char* content_path, const char* core_path, PodVector<DetectedSave>& out_saves) {
    out_saves.clear();
    if (!save_dir || save_dir[0] == '\0') return;

    char core_stem[128];
    char content_stem[128];
    getPathStem(core_path, core_stem, sizeof(core_stem));
    getPathStem(content_path, content_stem, sizeof(content_stem));

    bool filter_active = (core_stem[0] != '\0');
    char prefix[256];
    size_t prefix_len = 0;

    if (filter_active) {
        int w = (content_stem[0] != '\0')
            ? snprintf(prefix, sizeof(prefix), "%s.%s.state", content_stem, core_stem)
            : snprintf(prefix, sizeof(prefix), "%s.state", core_stem);
        prefix_len = (w > 0) ? (size_t)w : 0;
    }

    PodVector<FS::DirEntry> entries = FS::listDir(save_dir);

    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        const char* fn = e.name.c_str();

        if (strstr(fn, ".meta")) continue;

        DetectedSave ds;
        int slot = -1;

        if (filter_active) {
            if (strncmp(fn, prefix, prefix_len) != 0 || !parseSlotNumber(fn + prefix_len, slot)) {
                continue;
            }
            snprintf(ds.display_name, sizeof(ds.display_name), "Slot %d", slot);
        } else {
            const char* state_ptr = strstr(fn, ".state");
            if (state_ptr && parseSlotNumber(state_ptr + 6, slot)) {
                const char* dot = strchr(fn, '.');
                if (dot && dot < state_ptr) {
                    snprintf(ds.display_name, sizeof(ds.display_name), "%.*s [%.*s] - Slot %d",
                             (int)(dot - fn), fn,
                             (int)(state_ptr - (dot + 1)), dot + 1, slot);
                } else {
                    snprintf(ds.display_name, sizeof(ds.display_name), "%.*s - Slot %d",
                             (int)(state_ptr - fn), fn, slot);
                }
            } else if (strncmp(fn, "state_", 6) == 0 && parseSlotNumber(fn + 6, slot)) {
                snprintf(ds.display_name, sizeof(ds.display_name), "Global - Slot %d", slot);
            } else {
                continue;
            }
        }

        snprintf(ds.path, sizeof(ds.path), "%s/%s", save_dir, fn);
        ds.slot_num = slot;
        getFileModTime(ds.path, ds.time_buf, sizeof(ds.time_buf));
        out_saves.push_back(ds);
    }

    int n = out_saves.size();
    for (int i = 1; i < n; ++i) {
        DetectedSave key = out_saves[i];
        int j = i - 1;
        while (j >= 0 && out_saves[j].slot_num > key.slot_num) {
            out_saves[j + 1] = out_saves[j];
            j--;
        }
        out_saves[j + 1] = key;
    }
}

bool getSramPath(char* buf, size_t sz, const char* save_dir, const char* content_path, const char* core_path) {
    if (!buf || sz == 0) return false;
    buf[0] = '\0';

    char core_stem[128] = "";
    char content_stem[128] = "";
    getPathStem(core_path, core_stem, sizeof(core_stem));
    getPathStem(content_path, content_stem, sizeof(content_stem));

    const char* dir = (save_dir && save_dir[0]) ? save_dir : "./saves";

    if (content_stem[0] != '\0' && core_stem[0] != '\0') {
        snprintf(buf, sz, "%s/%s.%s.srm", dir, content_stem, core_stem);
    } else if (content_stem[0] != '\0') {
        snprintf(buf, sz, "%s/%s.srm", dir, content_stem);
    } else if (core_stem[0] != '\0') {
        snprintf(buf, sz, "%s/%s.srm", dir, core_stem);
    } else {
        return false;
    }
    return true;
}

void getLatestSaveState(char* out_path, size_t sz, const char* save_dir, const char* content_path, const char* core_path) {
    PodVector<DetectedSave> detected;
    scanSaveStates(save_dir, content_path, core_path, detected);
    if (detected.size() == 0) {
        if (sz > 0) out_path[0] = '\0';
        return;
    }
    safeCopy(out_path, detected[detected.size() - 1].path, sz);
}
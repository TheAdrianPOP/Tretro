// Windows-only for now.
#include "core_download_manager.h"
#include "path_utils.h"
#include "file_system.h"
#include "zip_util.h"

#include <direct.h>
#include <windows.h>
#define strcasecmp _stricmp

struct AutoFile {
    FILE* f;
    AutoFile(const char* path, const char* mode) { f = fopen(path, mode); }
    ~AutoFile() { if (f) fclose(f); }
    operator FILE*() { return f; }
    operator bool() const { return f != nullptr; }
};

struct Mutex {
    CRITICAL_SECTION cs;
    Mutex()  { InitializeCriticalSection(&cs); }
    ~Mutex() { DeleteCriticalSection(&cs); }
    void lock()   { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
};

struct AutoInet {
    HINTERNET h;
    AutoInet(HINTERNET h) : h(h) {}
    ~AutoInet() { if (h && g_inet.InternetCloseHandle) g_inet.InternetCloseHandle(h); }
    operator HINTERNET() { return h; }
    operator bool() const { return h != nullptr; }
};

struct LockGuard {
    Mutex& m;
    explicit LockGuard(Mutex& mx) : m(mx) { m.lock(); }
    ~LockGuard() { m.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

static const char EXT_SO[]  = ".dll";
static const char EXT_ZIP[] = ".dll.zip";
static const char PLAT[]    = "windows/x86_64";
static const char HOST[]    = "buildbot.libretro.com";

WinInetFns g_inet{};

#ifndef HTTP_QUERY_STATUS_CODE
#  define HTTP_QUERY_STATUS_CODE 19
#endif
#ifndef HTTP_QUERY_FLAG_NUMBER
#  define HTTP_QUERY_FLAG_NUMBER 0x20000000
#endif

static inline bool inet_ready() {
    return g_inet.InternetOpenA && g_inet.InternetOpenUrlA &&
           g_inet.InternetReadFile && g_inet.InternetCloseHandle &&
           g_inet.HttpQueryInfoA;
}

/* ------------------------------------------------------------------ */
/* Zip                                                                */
/* ------------------------------------------------------------------ */

namespace {

struct Inf {
    const uint8_t* in; size_t n, p;
    uint8_t* out;      size_t cap, o;
    uint32_t buf; int cnt; bool err;
};
struct Huff { int cnt[16], sym[288]; };

__attribute__((noinline)) int bits(Inf& s, int need) {
    uint32_t v = s.buf;
    while (s.cnt < need) {
        if (s.p >= s.n) { s.err = true; return 0; }
        v |= (uint32_t)s.in[s.p++] << s.cnt;
        s.cnt += 8;
    }
    s.buf = v >> need;
    s.cnt -= need;
    return (int)(v & ((1u << need) - 1));
}

__attribute__((noinline)) bool build(Huff& h, const int* len, int n) {
    for (int i = 0; i < 16; i++) h.cnt[i] = 0;
    for (int i = 0; i < n; i++) h.cnt[len[i]]++;
    int offs[16];
    int left = 1;
    offs[1] = 0;
    for (int l = 1; l < 16; l++) {
        left = (left << 1) - h.cnt[l];
        if (left < 0) return false;
        if (l < 15) offs[l + 1] = offs[l] + h.cnt[l];
    }
    for (int i = 0; i < n; i++) if (len[i]) h.sym[offs[len[i]]++] = i;
    return true;
}

__attribute__((noinline)) int decode(Inf& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= bits(s, 1);
        int c = h.cnt[l];
        if (code - c < first) return h.sym[index + code - first];
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t kLens[29]  = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const uint8_t  kLExt[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const uint16_t kDists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const uint8_t  kDExt[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

// Deflate decoder: stored, fixed and dynamic blocks.
__attribute__((noinline)) bool inflateRaw(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
    Inf s{in, n, 0, out, cap, 0, 0, 0, false};
    int last;
    do {
        last = bits(s, 1);
        int type = bits(s, 2);
        if (s.err || type == 3) return false;

        if (type == 0) {                       // stored block
            s.buf = 0;
            s.cnt = 0;
            size_t len = bits(s, 16);
            if (len != (~bits(s, 16) & 0xFFFF) || s.err || s.o + len > s.cap) return false;
            while (len--) s.out[s.o++] = (uint8_t)bits(s, 8);
            if (s.err) return false;
            continue;
        }

        int l[320];
        Huff lc, dc;
        int nl = 288, nd = 30;

        if (type == 1) {
            for (int i = 0; i < 320; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : i < 288 ? 8 : 5;
        } else {
            static const uint8_t ord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            nl = bits(s, 5) + 257;
            nd = bits(s, 5) + 1;
            int nc = bits(s, 4) + 4;
            if (s.err || nl > 286 || nd > 30) return false;
            for (int i = 0; i < 19; i++) l[ord[i]] = i < nc ? bits(s, 3) : 0;
            if (s.err || !build(lc, l, 19)) return false;
            for (int i = 0; i < nl + nd && !s.err;) {
                int sym = decode(s, lc);
                if (sym < 0) return false;
                if (sym < 16) { l[i++] = sym; continue; }
                int prev = 0, rep;
                if (sym == 16) { if (!i) return false; prev = l[i - 1]; rep = 3 + bits(s, 2); }
                else if (sym == 17) rep = 3 + bits(s, 3);
                else                rep = 11 + bits(s, 7);
                if (i + rep > nl + nd) return false;
                while (rep--) l[i++] = prev;
            }
            if (s.err || !l[256]) return false;
        }
        if (!build(lc, l, nl) || !build(dc, l + nl, nd)) return false;

        for (;;) {
            int sym = decode(s, lc);
            if (s.err || sym < 0) return false;
            if (sym < 256) {
                if (s.o >= s.cap) return false;
                s.out[s.o++] = (uint8_t)sym;
            } else if (sym == 256) {
                break;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                size_t len = kLens[sym] + bits(s, kLExt[sym]);
                int ds = decode(s, dc);
                if (ds < 0 || ds >= 30) return false;
                size_t dist = kDists[ds] + bits(s, kDExt[ds]);
                if (s.err || dist > s.o || s.o + len > s.cap) return false;
                while (len--) { s.out[s.o] = s.out[s.o - dist]; s.o++; }
            }
        }
    } while (!last);
    return s.o == cap;
}

// Validates and extracts in one pass. Every entry is written by basename into dest.
// If expect is set, the zip must contain exactly one entry named "<expect>.dll".
bool unzipBuf(const uint8_t* z, size_t sz, const char* dest, const char* expect) {
    size_t p = sz - 22, lo = sz > 65557 ? sz - 65557 : 0;
    for (; le32(z + p) != 0x06054b50u; --p) if (p == lo) return false;

    uint32_t count = le16(z + p + 10);
    size_t pos = le32(z + p + 16);
    if (expect && count != 1) return false;
    int done = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (pos + 46 > sz || le32(z + pos) != 0x02014b50u) return false;
        uint16_t method = le16(z + pos + 10);
        uint32_t csize = le32(z + pos + 20), usize = le32(z + pos + 24);
        uint16_t fnl = le16(z + pos + 28), exl = le16(z + pos + 30), cml = le16(z + pos + 32);
        size_t loff = le32(z + pos + 42);
        if (pos + 46 + fnl > sz) return false;

        char name[260];
        const char* fn = (const char*)z + pos + 46;
        pos += 46u + fnl + exl + cml;
        if (fnl == 0 || fnl >= sizeof(name)) return false;
        memcpy(name, fn, fnl);
        name[fnl] = 0;
        if (name[fnl - 1] == '/' || name[fnl - 1] == '\\') continue;

        if (expect) {
            size_t el = strlen(expect);
            if (strncmp(name, expect, el) || strcmp(name + el, EXT_SO)) return false;
        }

        const char* base = name;
        for (const char* c = name; *c; ++c) if (*c == '/' || *c == '\\') base = c + 1;
        if (!*base || strstr(base, "..") || strchr(base, ':')) return false;

        if (loff + 30 > sz || le32(z + loff) != 0x04034b50u) return false;
        size_t ds = loff + 30 + le16(z + loff + 26) + le16(z + loff + 28);
        if (ds + csize > sz) return false;

        uint8_t* out = (uint8_t*)malloc(usize ? usize : 1);
        if (!out) return false;

        bool good = (method == 0 && csize == usize) ? (memcpy(out, z + ds, usize), true)
                  : (method == 8 && inflateRaw(z + ds, csize, out, usize));
        if (good) {
            char outp[PATH_SZ + NAME_SZ];
            snprintf(outp, sizeof(outp), "%s\\%s", dest, base);
            FILE* of = fopen(outp, "wb");
            good = of != nullptr;
            if (of) {
                good = !usize || fwrite(out, 1, usize, of) == usize;
                fclose(of);
                if (!good) remove(outp);
            }
        }
        free(out);
        if (!good) return false;
        done++;
    }
    return done > 0;
}

} // namespace

static bool unzip(const char* path, const char* dest, const char* expect) {
    AutoFile f(path, "rb");
    if (!f || fseek(f, 0, SEEK_END) != 0) return false;
    long sz = ftell(f);
    if (sz < 22 || fseek(f, 0, SEEK_SET) != 0) return false;
    uint8_t* z = (uint8_t*)malloc((size_t)sz);
    if (!z) return false;
    bool ok = fread(z, 1, (size_t)sz, f) == (size_t)sz && unzipBuf(z, (size_t)sz, dest, expect);
    free(z);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Paths                                                              */
/* ------------------------------------------------------------------ */

static char g_cores_dir[PATH_SZ];

static void resolve_cores_dir() {
    if (g_cores_dir[0]) return;
    char exe[PATH_SZ];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n && n < sizeof(exe)) {
        char* last = strrchr(exe, PATH_SEP);
        if (last) *last = 0;
        snprintf(g_cores_dir, sizeof(g_cores_dir), "%s%ccores", exe, PATH_SEP);
    } else {
        snprintf(g_cores_dir, sizeof(g_cores_dir), ".%ccores", PATH_SEP);
    }
}

static void ensure_cores_dir() {
    resolve_cores_dir();
    _mkdir(g_cores_dir);
}

static void ensure_info_dir() {
    ensure_cores_dir();
    char path[PATH_SZ];
    snprintf(path, sizeof(path), "%s%cinfo", g_cores_dir, PATH_SEP);
    _mkdir(path);
}

bool CoreDownloadManager::nameOk(const char* n) {
    if (!n || !*n) return false;
    for (const char* p = n; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return false;
    return strlen(n) < NAME_SZ;
}

void CoreDownloadManager::corePath(const char* n, const char* ext,
                                    char* out, size_t sz) const {
    resolve_cores_dir();
    snprintf(out, sz, "%s%c%s%s", g_cores_dir, PATH_SEP, n, ext);
}

CoreDownloadManager& CoreDownloadManager::get() {
    static CoreDownloadManager* s = new CoreDownloadManager;
    return *s;
}

CoreDownloadManager::CoreDownloadManager() {
    m_ls.start_mtx = new Mutex();
    m_dl.meta_mtx  = new Mutex();
    resolve_cores_dir();
    wininet_init();
}

CoreDownloadManager::~CoreDownloadManager() {
    delete m_ls.start_mtx;
    delete m_dl.meta_mtx;
}

void CoreDownloadManager::resetDownloadStatus() {
    if (!m_dl.active.load()) { m_dl.rx.store(0); m_dl.total.store(0); }
}

bool CoreDownloadManager::isCoreInstalled(const char* n) const {
    if (!nameOk(n)) return false;
    char p[PATH_SZ]; corePath(n, EXT_SO, p, sizeof(p));
    return FS::exists(p);
}

bool CoreDownloadManager::deleteCore(const char* n) {
    if (!nameOk(n)) return false;
    char p[PATH_SZ]; corePath(n, EXT_SO, p, sizeof(p));
    return remove(p) == 0;
}

void CoreDownloadManager::getCoreFilePath(const char* n, char* out, size_t sz) const {
    if (!nameOk(n)) { if (sz) out[0]=0; return; }
    corePath(n, EXT_SO, out, sz);
}

static void sanitise_display_name(char* s, size_t sz) {
    char* w = s;
    for (char* r = s; *r && (size_t)(w - s) < sz - 1; ++r) {
        unsigned char c = (unsigned char)*r;
        if (c == 0x1b) {
            while (*r && *r != 'm') ++r;
            if (!*r) break;
            continue;
        }
        if (c < 0x20 || c == 0x7f) continue;
        *w++ = *r;
    }
    *w = 0;
}

bool CoreDownloadManager::infoName(const char* n, char* out, size_t sz) {
    char path[PATH_SZ];
    snprintf(path, sizeof(path), "%s%cinfo%c%s.info", g_cores_dir, PATH_SEP, PATH_SEP, n);
    AutoFile f(path, "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* p = strstr(line, "display_name");
        if (!p) continue;
        if (!(p = strchr(p, '"'))) continue;
        char* e = strchr(++p, '"');
        if (!e) continue;

        size_t len = (size_t)(e - p);
        if (len >= sz) len = sz - 1;
        memcpy(out, p, len); out[len] = 0;
        sanitise_display_name(out, sz);
        if (out[0]) return true;
    }
    return false;
}

bool CoreDownloadManager::buildCoreDisplayName(const char* n, char* out, size_t sz) {
    if (!out || !sz) return false;
    if (infoName(n, out, sz)) return true;

    char tmp[NAME_SZ]; safeCopy(tmp, n ? n : "");
    size_t tl = strlen(tmp);
    if (tl > 9 && !memcmp(tmp + tl - 9, "_libretro", 9)) tmp[tl - 9] = 0;

    size_t oi = 0; bool cap = true;
    for (const char* s = tmp; *s && oi + 1 < sz; ++s) {
        if (*s == '_') { out[oi++] = ' '; cap = true; }
        else { out[oi++] = cap ? (char)toupper((unsigned char)*s) : *s; cap = false; }
    }
    out[oi] = 0; return oi > 0;
}

void CoreDownloadManager::stripCoreExtension(const char* fn, char* out, size_t sz) {
    safeCopy(out, fn ? fn : "", sz);
    size_t nl = strlen(out), el = strlen(EXT_SO);
    if (nl > el && !strcasecmp(out + nl - el, EXT_SO)) out[nl - el] = 0;
}

/* ------------------------------------------------------------------ */
/* HTTP (WinINet)                                                     */
/* ------------------------------------------------------------------ */

static HINTERNET open_url(AutoInet& session, const char* host, const char* path) {
    char url[256];
    snprintf(url, sizeof(url), "https://%s%s", host, path);
    if (g_inet.InternetSetOptionA) {
        DWORD timeout_ms = 30000;
        g_inet.InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
        g_inet.InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
    }
    return g_inet.InternetOpenUrlA(session, url, NULL, 0,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0);
}

bool CoreDownloadManager::httpGet(const char* host, const char* path,
                                   char* body, size_t cap, size_t& len) {
    len = 0;
    if (!inet_ready()) return false;
    AutoInet hSession(g_inet.InternetOpenA("CoreDownloader", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
    if (!hSession) return false;
    AutoInet hUrl(open_url(hSession, host, path));
    if (!hUrl) return false;

    DWORD got = 0;
    while (len + 1 < cap) {
        if (!g_inet.InternetReadFile(hUrl, body + len, (DWORD)(cap - 1 - len), &got) || got == 0) break;
        len += got;
    }
    body[len] = 0;
    return len > 0;
}

bool CoreDownloadManager::httpDl(const char* host, const char* path,
                                  const char* dest, bool update_progress) {
    if (!inet_ready()) return false;

    AutoInet hSession(g_inet.InternetOpenA("CoreDownloader", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
    if (!hSession) return false;
    AutoInet hUrl(open_url(hSession, host, path));
    if (!hUrl) return false;

    DWORD status = 0, status_sz = sizeof(status);
    if (!g_inet.HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                               &status, &status_sz, NULL) || status != 200)
        return false;

    long cl = -1;
    char cl_buf[64];
    DWORD cl_buf_sz = sizeof(cl_buf);
    if (g_inet.HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH, cl_buf, &cl_buf_sz, NULL))
        cl = strtol(cl_buf, nullptr, 10);

    if (update_progress) m_dl.total.store(cl > 0 ? (int)cl : 0);

    char* buf = static_cast<char*>(malloc(HTTP_BUFSZ));
    if (!buf) return false;

    AutoFile f(dest, "wb");
    if (!f) { free(buf); return false; }

    unsigned long long rx = 0;
    bool ok = true;

    while (true) {
        DWORD got = 0;
        if (!g_inet.InternetReadFile(hUrl, buf, HTTP_BUFSZ, &got)) { ok = false; break; }
        if (got == 0) break;

        if (fwrite(buf, 1, got, f) != got) { ok = false; break; }
        rx += got;
        if (update_progress) m_dl.rx.store((int)rx);
    }
    free(buf);

    if (ok && cl > 0 && rx != (unsigned long long)cl) ok = false;

    fclose(f.f); f.f = nullptr;
    if (!ok) remove(dest);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Core list + info                                                   */
/* ------------------------------------------------------------------ */

void CoreDownloadManager::refreshCoreList() {
    {
        LockGuard lg(*m_ls.start_mtx);
        if (m_ls.loading.load()) return;
        m_ls.loading.store(true);
    }
    m_ls.failed.store(false);
    m_ls.offline.store(false);
    Thread::spawn(sListThread, this);
}

bool CoreDownloadManager::download_and_extract_info() {
    ensure_info_dir();
    char idest[PATH_SZ], infodest[PATH_SZ];
    snprintf(idest,    sizeof(idest),    "%s%cinfo.zip", g_cores_dir, PATH_SEP);
    snprintf(infodest, sizeof(infodest), "%s%cinfo",     g_cores_dir, PATH_SEP);

    bool ok = httpDl(HOST, "/assets/frontend/info.zip", idest) &&
              unzip(idest, infodest, nullptr);
    remove(idest);
    return ok;
}

static void sort_core_list(CoreInfo* temp_e, int n) {
    for (int i = 1; i < n; ++i) {
        CoreInfo k = temp_e[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(temp_e[j].desc, k.desc) > 0) {
            temp_e[j + 1] = temp_e[j];
            --j;
        }
        temp_e[j + 1] = k;
    }
}

int CoreDownloadManager::mergeLocal(CoreInfo* e, int n) {
    resolve_cores_dir();
    const char* scan_dirs[] = { g_cores_dir, "cores", "./cores", nullptr };
    const size_t so_len = strlen(EXT_SO);

    for (int d = 0; scan_dirs[d] && n < MAX_CORES; ++d) {
        if (!FS::exists(scan_dirs[d])) continue;
        for (const auto& entry : FS::listDir(scan_dirs[d])) {
            if (entry.isDirectory || n >= MAX_CORES) continue;
            const char* fn = entry.name.c_str();
            size_t fl = strlen(fn);
            if (fl <= so_len || strcasecmp(fn + fl - so_len, EXT_SO) != 0) continue;

            size_t nl = fl - so_len;
            if (nl >= NAME_SZ) continue;

            char local_name[NAME_SZ];
            memcpy(local_name, fn, nl);
            local_name[nl] = 0;
            if (!nameOk(local_name)) continue;

            bool dup = false;
            for (int i = 0; i < n && !dup; ++i) {
                if (!strcmp(e[i].name, local_name)) { dup = true; break; }
            }
            if (dup) continue;

            safeCopy(e[n].name, local_name, NAME_SZ);
            buildCoreDisplayName(e[n].name, e[n].desc, DESC_SZ);
            e[n].local = true;
            ++n;
        }
    }
    return n;
}

void CoreDownloadManager::listThread() {
    char* body = (char*)malloc(INDEX_CAP);
    if (!body) { m_ls.failed.store(true); m_ls.loading.store(false); return; }

    int inactive_idx = 1 - (m_ls.state.load(rt_mo::relaxed) & 1);
    CoreInfo* temp_e = m_ls.e[inactive_idx];

    char url[256];
    snprintf(url, sizeof(url), "/nightly/%s/latest/.index-extended", PLAT);

    size_t blen = 0;
    int n = 0;
    bool http_ok = httpGet(HOST, url, body, INDEX_CAP, blen) && blen > 0;

    if (http_ok) {
        (void)download_and_extract_info();

        const size_t elen = strlen(EXT_ZIP);
        const char* line = body;

        while (*line && n < MAX_CORES) {
            const char* eol = strchr(line, '\n');
            if (!eol) eol = line + strlen(line);
            const char* next = *eol ? eol + 1 : eol;

            // Last whitespace-separated token on the line is the filename.
            const char* e = eol;
            while (e > line && isspace((unsigned char)e[-1])) --e;
            const char* fs = e;
            while (fs > line && !isspace((unsigned char)fs[-1])) --fs;
            size_t fl = (size_t)(e - fs);
            line = next;

            if (fl <= elen || memcmp(fs + fl - elen, EXT_ZIP, elen) != 0) continue;

            size_t nl = fl - elen;
            if (nl >= NAME_SZ) continue;

            memcpy(temp_e[n].name, fs, nl);
            temp_e[n].name[nl] = 0;
            if (!nameOk(temp_e[n].name)) continue;

            bool dup = false;
            for (int i = 0; i < n && !dup; ++i) dup = !strcmp(temp_e[i].name, temp_e[n].name);
            if (dup) continue;

            buildCoreDisplayName(temp_e[n].name, temp_e[n].desc, DESC_SZ);
            temp_e[n].local = false;
            ++n;
        }
    }

    free(body);

    n = mergeLocal(temp_e, n);

    if (!n) {
        m_ls.failed.store(true);
        m_ls.loading.store(false);
        return;
    }

    sort_core_list(temp_e, n);
    m_ls.offline.store(!http_ok);
    m_ls.state.store((n << 1) | inactive_idx, rt_mo::release);
    m_ls.loaded.store(true);
    m_ls.failed.store(false);
    m_ls.loading.store(false);
}

/* ------------------------------------------------------------------ */
/* Core download                                                      */
/* ------------------------------------------------------------------ */

void CoreDownloadManager::startDownload(const CoreInfo& c) {
    if (m_dl.active.exchange(true)) return;

    if (!nameOk(c.name)) {
        m_dl.active.store(false);
        return;
    }
    m_dl.rx.store(0);
    m_dl.total.store(0);
    {
        LockGuard lg(*m_dl.meta_mtx);
        safeCopy(m_dl.name, c.name, NAME_SZ);
    }
    Thread::spawn(sDlThread, this);
}

const char* CoreDownloadManager::getActiveCoreName() const {
    LockGuard lg(*m_dl.meta_mtx);
    return m_dl.name;
}

void CoreDownloadManager::dlThread() {
    ensure_cores_dir();
    char local_name[NAME_SZ];
    {
        LockGuard lg(*m_dl.meta_mtx);
        safeCopy(local_name, m_dl.name, sizeof(local_name));
    }

    char zip[PATH_SZ], url[256];
    corePath(local_name, EXT_ZIP, zip, sizeof(zip));
    snprintf(url, sizeof(url), "/nightly/%s/latest/%s%s", PLAT, local_name, EXT_ZIP);

    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        m_dl.rx.store(0);
        ok = httpDl(HOST, url, zip, true);
    }
    if (ok) unzip(zip, g_cores_dir, local_name);
    remove(zip);

    m_dl.active.store(false);
}

void CoreDownloadManager::sListThread(void* p) {
    static_cast<CoreDownloadManager*>(p)->listThread();
}

void CoreDownloadManager::sDlThread(void* p) {
    static_cast<CoreDownloadManager*>(p)->dlThread();
}
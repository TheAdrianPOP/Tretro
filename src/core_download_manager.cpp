#include "core_download_manager.h"
#include "path_utils.h"
#include "file_system.h"

#ifdef _WIN32
#  include <direct.h>
#  include <windows.h>
#  define strcasecmp  _stricmp
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

struct AutoFile {
    FILE* f;
    AutoFile(const char* path, const char* mode) { f = fopen(path, mode); }
    ~AutoFile() { if (f) fclose(f); }
    operator FILE*() { return f; }
    operator bool() const { return f != nullptr; }
};

#ifdef _WIN32
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
#else
#  include <pthread.h>
struct Mutex {
    pthread_mutex_t m;
    Mutex()  { pthread_mutex_init(&m, nullptr); }
    ~Mutex() { pthread_mutex_destroy(&m); }
    void lock()   { pthread_mutex_lock(&m); }
    void unlock() { pthread_mutex_unlock(&m); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
};

struct AutoPipe {
    int fd[2];
    AutoPipe() { if (pipe(fd) != 0) fd[0] = fd[1] = -1; }
    ~AutoPipe() { close(); }
    void close() {
        if (fd[0] != -1) ::close(fd[0]);
        if (fd[1] != -1) ::close(fd[1]);
        fd[0] = fd[1] = -1;
    }
    bool ok() const { return fd[0] != -1; }
};
#endif

struct LockGuard {
    Mutex& m;
    explicit LockGuard(Mutex& mx) : m(mx) { m.lock(); }
    ~LockGuard() { m.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

#ifdef _WIN32
static const char EXT_SO[]  = ".dll";
static const char EXT_ZIP[] = ".dll.zip";
static const char PLAT[]    = "windows/x86_64";
#elif defined(__APPLE__)
static const char EXT_SO[]  = ".dylib";
static const char EXT_ZIP[] = ".dylib.zip";
static const char PLAT[]    = "apple/osx/x86_64";
#else
static const char EXT_SO[]  = ".so";
static const char EXT_ZIP[] = ".so.zip";
static const char PLAT[]    = "linux/x86_64";
#endif

static const char HOST[]    = "buildbot.libretro.com";

#ifdef _WIN32
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
#endif

static inline int make_dir(const char* path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

#ifdef _WIN32
typedef LONG (WINAPI *pfnBCryptOpenAlgorithmProvider)(void**, const wchar_t*, const wchar_t*, ULONG);
typedef LONG (WINAPI *pfnBCryptCreateHash)(void*, void**, void*, ULONG, void*, ULONG, ULONG);
typedef LONG (WINAPI *pfnBCryptHashData)(void*, void*, ULONG, ULONG);
typedef LONG (WINAPI *pfnBCryptFinishHash)(void*, void*, ULONG, ULONG);
typedef LONG (WINAPI *pfnBCryptDestroyHash)(void*);
typedef LONG (WINAPI *pfnBCryptCloseAlgorithmProvider)(void*, ULONG);

static bool calculate_sha256(const char* filepath, char out_hex[65]) {
    HMODULE u = LoadLibraryA("bcrypt.dll");
    if (!u) return false;

    void* fns[6];
    static const char names[] =
        "BCryptOpenAlgorithmProvider\0"
        "BCryptCreateHash\0"
        "BCryptHashData\0"
        "BCryptFinishHash\0"
        "BCryptDestroyHash\0"
        "BCryptCloseAlgorithmProvider\0";

    const char* p = names;
    for (int i = 0; i < 6; ++i) {
        fns[i] = (void*)GetProcAddress(u, p);
        if (!fns[i]) { FreeLibrary(u); return false; }
        while (*p++) {}
    }

    auto fnOpen    = (pfnBCryptOpenAlgorithmProvider)fns[0];
    auto fnCreate  = (pfnBCryptCreateHash)fns[1];
    auto fnData    = (pfnBCryptHashData)fns[2];
    auto fnFinish  = (pfnBCryptFinishHash)fns[3];
    auto fnDestroy = (pfnBCryptDestroyHash)fns[4];
    auto fnClose   = (pfnBCryptCloseAlgorithmProvider)fns[5];

    void *hAlg = nullptr, *hHash = nullptr;
    if (fnOpen(&hAlg, L"SHA256", nullptr, 0) < 0) {
        FreeLibrary(u);
        return false;
    }

    bool ok = false;
    if (fnCreate(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) >= 0) {
        AutoFile f(filepath, "rb");
        if (f) {
            uint8_t buf[2048];   
            size_t n;
            ok = true;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                if (fnData(hHash, buf, (ULONG)n, 0) < 0) { ok = false; break; }
            }
            if (ok) {
                uint8_t digest[32];
                if (fnFinish(hHash, digest, 32, 0) >= 0) {
                    static const char hex[] = "0123456789abcdef";
                    for (int i = 0; i < 32; ++i) {
                        out_hex[i * 2]     = hex[(digest[i] >> 4) & 0xF];
                        out_hex[i * 2 + 1] = hex[digest[i] & 0xF];
                    }
                    out_hex[64] = 0;
                } else {
                    ok = false;
                }
            }
        }
        fnDestroy(hHash);
    }
    fnClose(hAlg, 0);
    FreeLibrary(u);
    return ok;
}
#else
static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static bool calculate_sha256(const char* filepath, char out_hex[65]) {
    AutoFile f(filepath, "rb");
    if (!f) return false;

    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    uint64_t total_bits = 0;
    size_t n, rem = 0;

    auto process_block = [&](const uint8_t* blk) {
        uint32_t w[16];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
        uint32_t a=h[0], b=h[1], c2=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; ++i) {
            if (i >= 16) {
                uint32_t s0 = rotr32(w[(i-15)&15], 7) ^ rotr32(w[(i-15)&15], 18) ^ (w[(i-15)&15] >> 3);
                uint32_t s1 = rotr32(w[(i-2)&15], 17) ^ rotr32(w[(i-2)&15], 19)  ^ (w[(i-2)&15] >> 10);
                w[i&15] = w[(i-16)&15] + s0 + w[(i-7)&15] + s1;
            }
            uint32_t s1  = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
            uint32_t ch  = (e&f) ^ (~e&g);
            uint32_t t1  = hh + s1 + ch + K[i] + w[i&15];
            uint32_t s0  = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
            uint32_t maj = (a&b) ^ (a&c2) ^ (b&c2);
            uint32_t t2  = s0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c2; c2=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c2; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    };

    while ((n = fread(buf + rem, 1, 64 - rem, f)) > 0) {
        rem += n;
        total_bits += (uint64_t)n * 8;
        if (rem == 64) { process_block(buf); rem = 0; }
    }

    buf[rem++] = 0x80;
    if (rem > 56) {
        memset(buf + rem, 0, 64 - rem);
        process_block(buf);
        rem = 0;
    }
    memset(buf + rem, 0, 56 - rem);
    for (int i = 7; i >= 0; --i) buf[56 + (7 - i)] = (uint8_t)(total_bits >> (i * 8));
    process_block(buf);

    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) {
        for (int j = 3; j >= 0; --j) {
            uint8_t byte = (uint8_t)(h[i] >> (j * 8));
            *out_hex++ = hex[(byte >> 4) & 0xF];
            *out_hex++ = hex[byte & 0xF];
        }
    }
    *out_hex = 0;
    return true;
}
#endif

static bool path_safe_for_cmdline(const char* p) {
    for (; *p; ++p) {
        if (strchr("\"'%^&|;<>$()", *p)) return false;
    }
    return true;
}

#ifdef _WIN32
static bool safe_exec(const char* exe, const char* args) {
    char cmdline[512];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", exe, args);

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return false;

    DWORD wait_res = WaitForSingleObject(pi.hProcess, 60000);
    if (wait_res == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return wait_res != WAIT_TIMEOUT && exit_code == 0;
}
#else
static bool safe_exec(const char* program, char* const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execv(program, argv);
        exit(127);
    }
    int status = 0;
    return (waitpid(pid, &status, 0) != -1) && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

static bool decompress_archive(const char* zip_path, const char* dest_dir) {
#ifdef _WIN32
    if (!path_safe_for_cmdline(zip_path) || !path_safe_for_cmdline(dest_dir)) return false;
    char args[512];
    snprintf(args, sizeof(args), "-xf \"%s\" -C \"%s\"", zip_path, dest_dir);
    if (!safe_exec("C:\\Windows\\System32\\tar.exe", args)) {
        snprintf(args, sizeof(args),
            "-NoProfile -NonInteractive -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"",
            zip_path, dest_dir);
        return safe_exec("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", args);
    }
    return true;
#else
    static const char* const unzip_candidates[] = { "/usr/bin/unzip", "/usr/local/bin/unzip", nullptr };
    for (int u = 0; unzip_candidates[u]; ++u) {
        if (access(unzip_candidates[u], X_OK) != 0) continue;
        char* const argv[] = {
            const_cast<char*>(unzip_candidates[u]), const_cast<char*>("-qo"),
            const_cast<char*>(zip_path), const_cast<char*>("-d"), const_cast<char*>(dest_dir), nullptr
        };
        return safe_exec(unzip_candidates[u], argv);
    }
    return false;
#endif
}

static inline uint32_t u32le(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline uint16_t u16le(const unsigned char* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1]<<8);
}

static bool isZipSafe(const char* path, const char* expected_core_name = nullptr) {
    AutoFile f(path, "rb");
    if (!f || fseek(f, 0, SEEK_END) != 0) return false;
    
    long fsize = ftell(f);
    if (fsize < 22) return false;

    unsigned char eocd[22];
    long eocd_off = -1;
    long search_start = fsize - 22;
    long search_end   = (fsize - 22 - 65535 > 0) ? fsize - 22 - 65535 : 0;

    for (long pos = search_start; pos >= search_end; --pos) {
        if (fseek(f, pos, SEEK_SET) != 0 || fread(eocd, 1, 22, f) < 22) break;
        if (eocd[0]==0x50 && eocd[1]==0x4b && eocd[2]==0x05 && eocd[3]==0x06) {
            eocd_off = pos;
            break;
        }
    }
    if (eocd_off < 0) return false;

    uint32_t cd_count = (uint32_t)u16le(eocd + 10);
    if ((expected_core_name && cd_count != 1) || (uint64_t)u32le(eocd + 16) >= (uint64_t)fsize) return false;
    if (fseek(f, (long)u32le(eocd + 16), SEEK_SET) != 0) return false;

    char fn_buf[NAME_SZ + 16];
    for (uint32_t i = 0; i < cd_count; ++i) {
        unsigned char hdr[46];
        if (fread(hdr, 1, 46, f) != 46 || hdr[0]!=0x50 || hdr[1]!=0x4b || hdr[2]!=0x01 || hdr[3]!=0x02) return false;
        if ((((uint32_t)u32le(hdr + 38) >> 16) & 0xF000) == 0xA000) return false;
        
        uint16_t fn_len = u16le(hdr + 28), extra_len = u16le(hdr + 30), comm_len = u16le(hdr + 32);

        if (fn_len == 0 || fn_len >= (uint16_t)sizeof(fn_buf)) {
            if (fseek(f, (long)fn_len + extra_len + comm_len, SEEK_CUR) != 0) return false;
            continue;
        }

        if (fread(fn_buf, 1, fn_len, f) != fn_len) return false;
        fn_buf[fn_len] = '\0';

        if (strstr(fn_buf, "..") || fn_buf[0]=='/' || fn_buf[0]=='\\' || strchr(fn_buf, ':')) return false;

        if (expected_core_name) {
            char expected_fn[NAME_SZ + 16];
            snprintf(expected_fn, sizeof(expected_fn), "%s%s", expected_core_name, EXT_SO);
            if (strcmp(fn_buf, expected_fn) != 0) return false;
        }

        if (fseek(f, (long)extra_len + comm_len, SEEK_CUR) != 0) return false;
    }

    return true;
}

#ifndef _WIN32
static const char* find_curl() {
    static const char* const candidates[] = { "/usr/bin/curl", "/usr/local/bin/curl", "/opt/homebrew/bin/curl", nullptr };
    for (int i = 0; candidates[i]; ++i)
        if (access(candidates[i], X_OK) == 0) return candidates[i];
    return nullptr;
}

static bool safe_spawn_curl(const char* url, char* body, size_t cap, size_t& len) {
    len = 0;
    const char* curl = find_curl();
    AutoPipe p;
    if (!curl || !p.ok()) return false;

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        ::close(p.fd[0]);
        dup2(p.fd[1], STDOUT_FILENO);
        ::close(p.fd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) { dup2(devnull, STDERR_FILENO); ::close(devnull); }

        char* const argv[] = {
            const_cast<char*>(curl), (char*)"-q", (char*)"--proto", (char*)"=https",
            (char*)"-sSf", (char*)"-L", (char*)"--tlsv1.2", (char*)"--connect-timeout", (char*)"15",
            (char*)"-m", (char*)"300", (char*)url, NULL
        };
        execv(curl, argv);
        exit(127);
    }

    ::close(p.fd[1]); p.fd[1] = -1;
    while (len + 1 < cap) {
        ssize_t n = read(p.fd[0], body + len, cap - 1 - len);
        if (n <= 0) break;
        len += n;
    }
    body[len] = 0;
    p.close();

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && len > 0;
}

static bool safe_spawn_curl_dl(const char* url, const char* dest, bool update_progress, float& progress_ref, const std::atomic<bool>& cancel_flag) {
    const char* curl = find_curl();
    if (!curl) return false;

    long cl = -1;
    if (update_progress) {
        AutoPipe p;
        if (p.ok()) {
            pid_t pid = fork();
            if (pid == 0) {
                ::close(p.fd[0]); dup2(p.fd[1], STDOUT_FILENO); ::close(p.fd[1]);
                int devnull = open("/dev/null", O_WRONLY);
                if (devnull != -1) dup2(devnull, STDERR_FILENO);

                char* const argv[] = {
                    const_cast<char*>(curl), (char*)"-sI", (char*)"-L", (char*)"--tlsv1.2", (char*)url, NULL
                };
                execv(curl, argv); exit(127);
            } else if (pid > 0) {
                ::close(p.fd[1]); p.fd[1] = -1;
                if (FILE* h_file = fdopen(p.fd[0], "r")) {
                    char line[256];
                    while (fgets(line, sizeof(line), h_file)) {
                        if (!strncasecmp(line, "content-length:", 15)) {
                            cl = strtol(line + 15, nullptr, 10);
                            break;
                        }
                    }
                    fclose(h_file); p.fd[0] = -1;
                }
                waitpid(pid, NULL, 0);
            }
        }
    }

    AutoPipe p;
    if (!p.ok()) return false;

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        ::close(p.fd[0]); dup2(p.fd[1], STDOUT_FILENO); ::close(p.fd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) dup2(devnull, STDERR_FILENO);

        char* const argv[] = {
            const_cast<char*>(curl), (char*)"-sSf", (char*)"-L", (char*)"--tlsv1.2",
            (char*)"--connect-timeout", (char*)"15", (char*)"-m", (char*)"300", (char*)url, NULL
        };
        execv(curl, argv); exit(127);
    }

    ::close(p.fd[1]); p.fd[1] = -1;
    AutoFile f_dest(dest, "wb");
    if (!f_dest) { waitpid(pid, NULL, 0); return false; }

    char buf[8192];
    long rx = 0;
    bool ok = true;

    while (true) {
        if (cancel_flag.load()) { ok = false; break; }
        ssize_t n = read(p.fd[0], buf, sizeof(buf));
        if (n <= 0) { ok = (n == 0); break; }
        if (fwrite(buf, 1, n, f_dest) != (size_t)n) { ok = false; break; }
        rx += n;
        if (update_progress && cl > 0) progress_ref = 100.f * (float)rx / (float)cl;
    }

    p.close();
    if (!ok) kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    if (ok && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) ok = false;

    if (!ok) remove(dest);
    return ok;
}
#endif

static char g_cores_dir[PATH_SZ];

static void resolve_cores_dir() {
    if (g_cores_dir[0]) return;
#ifdef _WIN32
    char exe[PATH_SZ];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n && n < sizeof(exe)) {
        char* last = strrchr(exe, PATH_SEP);
        if (last) *last = 0;
        snprintf(g_cores_dir, sizeof(g_cores_dir), "%s%ccores", exe, PATH_SEP);
    } else {
        snprintf(g_cores_dir, sizeof(g_cores_dir), ".%ccores", PATH_SEP);
    }
#else
    char buf[PATH_SZ];
    if (realpath("/proc/self/exe", buf)) {
        char* last = strrchr(buf, PATH_SEP);
        if (last) *last = 0;
        snprintf(g_cores_dir, sizeof(g_cores_dir), "%s%ccores", buf, PATH_SEP);
    } else if (getcwd(buf, sizeof(buf))) {
        snprintf(g_cores_dir, sizeof(g_cores_dir), "%s%ccores", buf, PATH_SEP);
    } else {
        snprintf(g_cores_dir, sizeof(g_cores_dir), ".%ccores", PATH_SEP);
    }
#endif
}

static void ensure_cores_dir() {
    resolve_cores_dir();
    make_dir(g_cores_dir);
}

static void ensure_info_dir() {
    ensure_cores_dir();
    char path[PATH_SZ];
    snprintf(path, sizeof(path), "%s%cinfo", g_cores_dir, PATH_SEP);
    make_dir(path);
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
#ifdef _WIN32
    if (!wininet_init() || !inet_ready()) {
    }
#endif
}

CoreDownloadManager::~CoreDownloadManager() {
    delete m_ls.start_mtx;
    delete m_dl.meta_mtx;
}

bool CoreDownloadManager::cancelPending() const {
    return m_dl.cancelled.load();
}

void CoreDownloadManager::resetDownloadStatus() {
    if (m_dl.active.load()) return; 

    m_dl.completed.store(false); 
    m_dl.failed.store(false);
    m_dl.cancelled.store(false); 
    m_dl.progress.store(0.f);
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

bool CoreDownloadManager::httpGet(const char* host, const char* path,
                                   char* body, size_t cap, size_t& len) {
    len = 0;
    char url[256];
    snprintf(url, sizeof(url), "https://%s%s", host, path);

#ifdef _WIN32
    if (!inet_ready()) return false;
    AutoInet hSession(g_inet.InternetOpenA("CoreDownloader", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
    if (!hSession) return false;
    
    if (g_inet.InternetSetOptionA) {
        DWORD timeout_ms = 30000;
        g_inet.InternetSetOptionA(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
        g_inet.InternetSetOptionA(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
    }
    AutoInet hUrl(g_inet.InternetOpenUrlA(hSession, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0));
    if (!hUrl) return false;

    bool ok = true;
    DWORD read = 0;
    while (len + 1 < cap) {
        if (cancelPending()) { ok = false; break; }
        if (!g_inet.InternetReadFile(hUrl, body + len, (DWORD)(cap - 1 - len), &read) || read == 0) break;
        len += read;
    }
    body[len] = 0;
    return ok && len > 0;
#else
    return safe_spawn_curl(url, body, cap, len);
#endif
}

bool CoreDownloadManager::httpDl(const char* host, const char* path,
                                  const char* dest, bool update_progress) {
    char url[256];
    snprintf(url, sizeof(url), "https://%s%s", host, path);

#ifdef _WIN32
    if (!inet_ready()) return false;

    AutoInet hSession(g_inet.InternetOpenA("CoreDownloader", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
    if (!hSession) return false;

    if (g_inet.InternetSetOptionA) {
        DWORD timeout_ms = 30000;
        g_inet.InternetSetOptionA(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
        g_inet.InternetSetOptionA(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
    }
    AutoInet hUrl(g_inet.InternetOpenUrlA(hSession, url, NULL, 0,
                                          INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0));
    if (!hUrl) return false;

    DWORD status = 0, status_sz = sizeof(status);
    if (!g_inet.HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                               &status, &status_sz, NULL) || status != 200)
        return false;

    long long cl = -1;
    char cl_buf[64];
    DWORD cl_buf_sz = sizeof(cl_buf);
    if (g_inet.HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH, cl_buf, &cl_buf_sz, NULL))
        cl = strtol(cl_buf, nullptr, 10);

    char* buf = static_cast<char*>(malloc(HTTP_BUFSZ));
    if (!buf) return false;

    AutoFile f(dest, "wb");
    if (!f) { free(buf); return false; }

    unsigned long long rx = 0;
    bool ok = true;

    while (true) {
        if (cancelPending()) { ok = false; break; }

        DWORD got = 0;
        if (!g_inet.InternetReadFile(hUrl, buf, HTTP_BUFSZ, &got)) { ok = false; break; }
        if (got == 0) break;                                   

        if (fwrite(buf, 1, got, f) != got) { ok = false; break; }
        rx += got;
        if (update_progress && cl > 0)
            m_dl.progress.store(100.f * (float)rx / (float)cl);
    }
    free(buf);

    if (ok && cl > 0 && rx != (unsigned long long)cl) ok = false;   

    fclose(f.f); f.f = nullptr;        
    if (!ok) remove(dest);
    return ok;
#else
    float prog = 0.f;
    bool ok = safe_spawn_curl_dl(url, dest, update_progress, prog, m_dl.cancelled);
    if (ok && update_progress) m_dl.progress.store(prog);
    return ok;
#endif
}

void CoreDownloadManager::refreshCoreList() {
    {
        LockGuard lg(*m_ls.start_mtx);
        if (m_ls.loading.load()) return;
        m_ls.loading.store(true);
    }
    m_ls.failed.store(false);
    Thread::spawn(sListThread, this);
}

bool CoreDownloadManager::download_and_extract_info() {
    ensure_info_dir();
    char idest[PATH_SZ], isum_path[PATH_SZ];
    snprintf(idest, sizeof(idest), "%s%cinfo.zip", g_cores_dir, PATH_SEP);
    snprintf(isum_path, sizeof(isum_path), "%s%cinfo.zip.sha256", g_cores_dir, PATH_SEP);
    
    bool info_ok = false;
    if (httpDl(HOST, "/assets/frontend/info.zip.sha256", isum_path)) {
        char expected_hex[65] = {};
        if (FILE* sf = fopen(isum_path, "r")) { (void)fgets(expected_hex, 65, sf); fclose(sf); }
        remove(isum_path);
        
        size_t hlen = strlen(expected_hex);
        while (hlen > 0 && isspace((unsigned char)expected_hex[hlen-1]))
            expected_hex[--hlen] = 0;
        
        if (hlen == 64 && httpDl(HOST, "/assets/frontend/info.zip", idest)) {
            char computed[65];
            if (calculate_sha256(idest, computed) && !strcmp(computed, expected_hex) && isZipSafe(idest)) {
                char infodest[PATH_SZ];
                snprintf(infodest, sizeof(infodest), "%s" PATH_SEPS "info", g_cores_dir);
                info_ok = decompress_archive(idest, infodest);
            }
        }
        remove(idest);
    }
    return info_ok;
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

void CoreDownloadManager::listThread() {
    char* body = (char*)malloc(INDEX_CAP);
    if (!body) { m_ls.failed.store(true); m_ls.loading.store(false); return; }

    int inactive_idx = 1 - (m_ls.state.load(rt_mo::relaxed) & 1);
    CoreInfo* temp_e = m_ls.e[inactive_idx];

    char url[256];
    snprintf(url, sizeof(url), "/nightly/%s/latest/.index-extended", PLAT);
    size_t blen = 0;
    if (!httpGet(HOST, url, body, INDEX_CAP, blen) || !blen) goto fail;
    (void)download_and_extract_info();

    {
        int n = 0;
        const char* line = body;
        const size_t elen = strlen(EXT_ZIP);
        while (*line && n < MAX_CORES) {
            const char* eol = strchr(line, '\n'); if (!eol) eol = line + strlen(line);
            
            const char* p = line;
            while (p < eol && !isspace((unsigned char)*p)) ++p;
            while (p < eol && isspace((unsigned char)*p)) ++p;

            {
                int hi = 0;
                while (p < eol && isxdigit((unsigned char)*p) && hi < 64)
                    temp_e[n].sha256[hi++] = (char)*p++;
                temp_e[n].sha256[hi] = 0;
                if (hi != 64) temp_e[n].sha256[0] = 0;
                while (p < eol && isxdigit((unsigned char)*p)) ++p;
            }
            while (p < eol && isspace((unsigned char)*p)) ++p;

            const char* fs = p;
            while (p < eol && !isspace((unsigned char)*p)) ++p;
            size_t fl = (size_t)(p - fs);
            if (!fl || fl <= elen || memcmp(fs + fl - elen, EXT_ZIP, elen)) {
                line = *eol ? eol + 1 : eol; continue;
            }

            size_t nl = fl - elen;
            if (nl >= NAME_SZ) { line = *eol ? eol + 1 : eol; continue; }

            memcpy(temp_e[n].name, fs, nl);
            temp_e[n].name[nl] = 0;
            if (!nameOk(temp_e[n].name)) { line = *eol ? eol + 1 : eol; continue; }

            bool dup = false;
            for (int i = 0; i < n && !dup; ++i) dup = !strcmp(temp_e[i].name, temp_e[n].name);
            if (dup) { line = *eol ? eol + 1 : eol; continue; }

            buildCoreDisplayName(temp_e[n].name, temp_e[n].desc, DESC_SZ);
            ++n;
            line = *eol ? eol + 1 : eol;
        }

        if (!n) goto fail;

        sort_core_list(temp_e, n);
        m_ls.state.store((n << 1) | inactive_idx, rt_mo::release);
        m_ls.loaded.store(true);
        m_ls.failed.store(false);
    }
    free(body); m_ls.loading.store(false); return;

fail:
    free(body); m_ls.failed.store(true); m_ls.loading.store(false);
}

void CoreDownloadManager::startDownload(const CoreInfo& c) {
    if (m_dl.active.exchange(true)) return;
    
    if (!nameOk(c.name)) {
        m_dl.failed.store(true);
        m_dl.active.store(false);
        return;
    }
    m_dl.progress.store(0.f);
    m_dl.completed.store(false);
    m_dl.failed.store(false);
    m_dl.cancelled.store(false);
    {
        LockGuard lg(*m_dl.meta_mtx);
        safeCopy(m_dl.name, c.name, NAME_SZ);
        safeCopy(m_dl.sha256, c.sha256, sizeof(m_dl.sha256));
    }
    Thread::spawn(sDlThread, this);
}

const char* CoreDownloadManager::getActiveCoreName() const {
    LockGuard lg(*m_dl.meta_mtx);
    return m_dl.name;
}

void CoreDownloadManager::dlThread() {
    ensure_cores_dir();
    char local_name[NAME_SZ], local_sha256[65];
    {
        LockGuard lg(*m_dl.meta_mtx);
        safeCopy(local_name, m_dl.name, sizeof(local_name));
        safeCopy(local_sha256, m_dl.sha256, sizeof(local_sha256));
    }

    char zip[PATH_SZ], url[256]; 
    corePath(local_name, EXT_ZIP, zip, sizeof(zip));
    snprintf(url, sizeof(url), "/nightly/%s/latest/%s%s", PLAT, local_name, EXT_ZIP);

    bool cancelled = false;
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        m_dl.progress.store(0.f);
        ok = httpDl(HOST, url, zip, true);
        if (ok || m_dl.cancelled.load()) break;
    }

    if (!ok) {
    } else if ((cancelled = m_dl.cancelled.load())) {
        ok = false;
    } else if (!isZipSafe(zip, local_name)) {
        ok = false;
    } else {
        char core_path[PATH_SZ];
        corePath(local_name, EXT_SO, core_path, sizeof(core_path));

        if (!decompress_archive(zip, g_cores_dir)) {
            ok = false;
        } else if (!FS::exists(core_path)) {
            ok = false;
        } else if (local_sha256[0]) {
            char computed[65];
            if (!calculate_sha256(core_path, computed) || strcmp(computed, local_sha256)) {
                ok = false;
            }
        }
        if (!ok) remove(core_path);
    }

    remove(zip);

    m_dl.progress.store(100.f);
    m_dl.completed.store(ok);
    m_dl.failed.store(!ok && !cancelled);
    m_dl.active.store(false);
}

void CoreDownloadManager::sListThread(void* p) {
    static_cast<CoreDownloadManager*>(p)->listThread();
}

void CoreDownloadManager::sDlThread(void* p) {
    static_cast<CoreDownloadManager*>(p)->dlThread();
}
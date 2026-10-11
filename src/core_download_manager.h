#ifndef CORE_DL_H
#define CORE_DL_H

#include "atomic.h"   
#include "threading.h"

struct Mutex;

#ifdef _WIN32
#  include "wininet.h"    
#  define PATH_SEP   '\\'
#  define PATH_SEPS  "\\"
#else
#  include <unistd.h>
#  define PATH_SEP   '/'
#  define PATH_SEPS  "/"
#endif

enum {
    MAX_CORES    = 512,
    NAME_SZ      = 128,
    DESC_SZ      = 192,
    PATH_SZ      = 260,       
    HTTP_BUFSZ   = 8192,
    INDEX_CAP    = 256 * 1024
};

struct CoreInfo {
    char name[NAME_SZ];
    char desc[DESC_SZ];
    char sha256[65];
};

class CoreDownloadManager {
public:
    static CoreDownloadManager& get();

    void refreshCoreList();
    bool isCoreListLoading()  const { return m_ls.loading.load(); }
    bool isCoreListLoaded()   const { return m_ls.loaded.load();  }
    bool hasCoreListFailed()  const { return m_ls.failed.load();  }
    int  getCoreCount()        const { return m_ls.state.load(rt_mo::acquire) >> 1; }
    
    CoreInfo getCoreInfo(int i) const {
        int state = m_ls.state.load(rt_mo::acquire);
        int count = state >> 1;
        int idx = state & 1;
        if (i < 0 || i >= count) {
            return CoreInfo{};
        }
        return m_ls.e[idx][i];
    }

    bool isCoreInstalled(const char* n) const;
    bool deleteCore(const char* n);
    void getCoreFilePath(const char* n, char* out, size_t sz) const;

    void  startDownload(const CoreInfo& c);
    bool  isDownloadActive()    const { return m_dl.active.load();    }
    bool  hasDownloadFailed()   const { return m_dl.failed.load();    }
    float getDownloadProgress() const { return m_dl.progress.load();  }
    void  resetDownloadStatus();
    const char* getActiveCoreName() const;

    static bool buildCoreDisplayName(const char* n, char* out, size_t sz);
    static void stripCoreExtension(const char* fn, char* out, size_t sz);

private:
    CoreDownloadManager();
    ~CoreDownloadManager();
    CoreDownloadManager(const CoreDownloadManager&) = delete;
    CoreDownloadManager& operator=(const CoreDownloadManager&) = delete;

    struct LS {
        CoreInfo  e[2][MAX_CORES];
        AtomicInt state{0};
        AtomicBool loading{false}, loaded{false}, failed{false};
        Mutex*    start_mtx;
    } m_ls;

    struct DS {
        AtomicBool  active{false}, completed{false},
                    failed{false}, cancelled{false};
        AtomicFloat progress{0.f};
        Mutex*      meta_mtx;
        char name[NAME_SZ]{};
        char sha256[65]{};
    } m_dl;

    static bool nameOk(const char* n);
    void        corePath(const char* n, const char* ext, char* out, size_t sz) const;
    static bool infoName(const char* n, char* out, size_t sz);

    bool httpGet(const char* host, const char* path,
                 char* body, size_t cap, size_t& len);
    bool httpDl (const char* host, const char* path, const char* dest, bool update_progress = false);
    bool cancelPending() const;
    bool download_and_extract_info();
    void listThread();
    void dlThread();

    static void sListThread(void*);
    static void sDlThread(void*);
};

#endif 
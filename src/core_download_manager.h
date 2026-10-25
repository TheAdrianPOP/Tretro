#ifndef CORE_DL_H
#define CORE_DL_H

#include "atomic.h"   
#include "threading.h"

struct Mutex;

#include "wininet.h"
#define PATH_SEP '\\'

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
    bool local;              // found in the cores folder but not listed on the buildbot
};

class CoreDownloadManager {
public:
    static CoreDownloadManager& get();

    void refreshCoreList();
    bool isCoreListLoading()  const { return m_ls.loading.load(); }
    bool isCoreListLoaded()   const { return m_ls.loaded.load();  }
    bool hasCoreListFailed()  const { return m_ls.failed.load();  }
    bool isCoreListOffline()  const { return m_ls.offline.load(); }   // buildbot unreachable, only local cores listed
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
    int   getDownloadedBytes()  const { return m_dl.rx.load();        }
    int   getDownloadTotal()    const { return m_dl.total.load();     }   // 0 = size unknown
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
        AtomicBool loading{false}, loaded{false}, failed{false}, offline{false};
        Mutex*    start_mtx;
    } m_ls;

    struct DS {
        AtomicBool  active{false};
        AtomicInt   rx{0};            // bytes received so far
        AtomicInt   total{0};         // Content-Length, 0 if the server did not say
        Mutex*      meta_mtx;
        char name[NAME_SZ]{};
    } m_dl;

    static bool nameOk(const char* n);
    void        corePath(const char* n, const char* ext, char* out, size_t sz) const;
    static bool infoName(const char* n, char* out, size_t sz);
    static int  mergeLocal(CoreInfo* e, int n);

    bool httpGet(const char* host, const char* path,
                 char* body, size_t cap, size_t& len);
    bool httpDl (const char* host, const char* path, const char* dest, bool update_progress = false);
    bool download_and_extract_info();
    void listThread();
    void dlThread();

    static void sListThread(void*);
    static void sDlThread(void*);
};

#endif
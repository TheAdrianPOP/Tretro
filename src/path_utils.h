#pragma once

#include "vector.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

struct DetectedSave {
    char path[MAX_PATH];
    char time_buf[64];
    char display_name[128];
    int slot_num;
};

void safeCopy(char* dst, const char* src, size_t max_len);

template <size_t N>
inline void safeCopy(char (&dst)[N], const char* src) {
    safeCopy(dst, src, N);
}
bool resolveShortcut(const char* shortcut_path, char* target_path, size_t target_sz);
void resolvePath(const char* input_path, char* output_buf, size_t output_sz);
const char* stripPathC(const char* str, char* out_buf, size_t out_sz);
const char* getPathFilename(const char* path);
const char* getFileExtension(const char* path);
void getPathStem(const char* path, char* out_buf, size_t out_sz); 
void makePathAbsolute(const char* input_path, char* output_buf, size_t output_sz);
void setWorkDirToExeDir(const char* argv0 = nullptr);
bool fileExists(const char* path);
bool pathExists(const char* path); 

bool isCoreExt(const char* lext);

bool extractFirstValidPath(const char* raw, size_t raw_len, char* out_path, size_t out_sz);
void getSlotPath(char* buf, size_t sz, const char* save_dir, const char* content_path, const char* core_path, int slot);
bool getSramPath(char* buf, size_t sz, const char* save_dir, const char* content_path, const char* core_path);
bool getFileModTime(const char* path, char* out_buf, size_t out_sz);
bool getMetaPaths(const char* state_path, char* core_out, size_t core_sz, char* content_out, size_t content_sz);
void scanSaveStates(const char* save_dir, const char* content_path, const char* core_path, PodVector<DetectedSave>& out_saves);
void getLatestSaveState(char* out_path, size_t sz, const char* save_dir, const char* content_path, const char* core_path);

#ifdef _WIN32
struct StdRedirector {
    HANDLE hNull;
    HANDLE hOrigStdout;
    HANDLE hOrigStderr;

    StdRedirector();
    ~StdRedirector();
};

int drainConsoleCharsUTF8(HANDLE hInput, DWORD eventsAvail, char* out_utf8, int out_cap);
#endif
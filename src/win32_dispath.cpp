#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "win32_dispath.h"
#include "path_utils.h"
#include "autodetect.h"

static inline unsigned long long fnv1a_64(const char* str) {
    unsigned long long hash = 14695981039346656037ULL;
    while (*str) {
        hash = (hash ^ (unsigned char)*str++) * 1099511628211ULL;
    }
    return hash;
}

static unsigned long long s_last_dispatched_hash = 0;
static DWORD              s_last_dispatched_time = 0;

void checkPastedPathWindows(Frontend& f, RetroUI& ui, const char* exe_path) {
    static bool s_drag_init = false;
    if (!s_drag_init) {
        s_drag_init = true;
        HWND hConsole = GetConsoleWindow();
        if (hConsole) {
            HMODULE hShell = LoadLibraryA("shell32.dll");
            if (hShell) {
                typedef VOID (WINAPI *pfnDragAcceptFiles)(HWND, BOOL);
                auto pDragAcceptFiles = (pfnDragAcceptFiles)(void*)GetProcAddress(hShell, "DragAcceptFiles");
                if (pDragAcceptFiles) pDragAcceptFiles(hConsole, TRUE);
            }

            HMODULE hUser = GetModuleHandleA("user32.dll");
            if (hUser) {
                typedef BOOL (WINAPI *pfnChangeFilter)(HWND, UINT, DWORD, void*);
                auto pChangeFilter = (pfnChangeFilter)(void*)GetProcAddress(hUser, "ChangeWindowMessageFilterEx");
                if (pChangeFilter) {
                    pChangeFilter(hConsole, WM_DROPFILES, 1 , nullptr);
                    pChangeFilter(hConsole, 0x0049 , 1 , nullptr);
                    pChangeFilter(hConsole, 0x004A , 1 , nullptr);
                }
            }
        }
    }

    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    if (hInput == INVALID_HANDLE_VALUE) return;

    DWORD numEvents = 0;
    if (!GetNumberOfConsoleInputEvents(hInput, &numEvents) || numEvents == 0) return;

    {
        constexpr DWORD kMaxPeek = 128;
        DWORD peekCount = (numEvents < kMaxPeek) ? numEvents : kMaxPeek;
        INPUT_RECORD peekBuf[128];
        DWORD peeked = 0;
        if (!PeekConsoleInputW(hInput, peekBuf, peekCount, &peeked) || peeked == 0) return;

        bool looksLikePath = false;
        int printable = 0;
        for (DWORD i = 0; i < peeked; ++i) {
            if (peekBuf[i].EventType != KEY_EVENT || !peekBuf[i].Event.KeyEvent.bKeyDown) continue;
            wchar_t wc = peekBuf[i].Event.KeyEvent.uChar.UnicodeChar;
            if (wc > 0 && wc != L'\r' && wc != L'\n') {
                if (wc == L'/' || wc == L'\\' || (printable > 0 && wc == L':')) {
                    looksLikePath = true;
                    break;
                }
                printable++;
            }
        }
        if (!looksLikePath) return;
    }

    char path_str[1024];
    int utf8_len = drainConsoleCharsUTF8(hInput, numEvents, path_str, sizeof(path_str));
    if (utf8_len <= 0) return;

    char found_path[512] = "";
    if (!extractFirstValidPath(path_str, (size_t)utf8_len, found_path, sizeof(found_path))) return;

    unsigned long long current_hash = fnv1a_64(found_path);
    DWORD now = GetTickCount();
    if (current_hash == s_last_dispatched_hash && (now - s_last_dispatched_time) < 1000) return;
    s_last_dispatched_hash = current_hash;
    s_last_dispatched_time = now;

    if (isCoreExt(getFileExtension(found_path))) {
        safeCopy(ui.pending_core, found_path, sizeof(ui.pending_core));
        ui.pending_content[0] = '\0';
        ui.pending_load = true;

        const char* fn = getPathFilename(found_path);
        f.logMessage("[loader] Loading core: %s", fn ? fn : found_path);
        return;
    }

    ui.tryPickCoreForContent(found_path, RetroUI::PickMode::Drop);
}

#endif
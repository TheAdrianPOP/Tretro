#ifndef WININET_H
#define WININET_H

#ifdef _WIN32
#include <windows.h>

typedef void* HINTERNET;

#define INTERNET_OPEN_TYPE_PRECONFIG    0
#define INTERNET_FLAG_RELOAD            0x80000000
#define INTERNET_FLAG_SECURE            0x00800000
#define HTTP_QUERY_CONTENT_LENGTH       5
#define INTERNET_OPTION_CONNECT_TIMEOUT 2
#define INTERNET_OPTION_RECEIVE_TIMEOUT 6

typedef HINTERNET (__stdcall *pfn_InternetOpenA)(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
typedef HINTERNET (__stdcall *pfn_InternetOpenUrlA)(HINTERNET, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
typedef BOOL      (__stdcall *pfn_InternetReadFile)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL      (__stdcall *pfn_InternetCloseHandle)(HINTERNET);
typedef BOOL      (__stdcall *pfn_HttpQueryInfoA)(HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD);
typedef BOOL      (__stdcall *pfn_InternetSetOptionA)(HINTERNET, DWORD, LPVOID, DWORD);

struct WinInetFns {
    pfn_InternetOpenA        InternetOpenA;
    pfn_InternetOpenUrlA     InternetOpenUrlA;
    pfn_InternetReadFile     InternetReadFile;
    pfn_InternetCloseHandle  InternetCloseHandle;
    pfn_HttpQueryInfoA       HttpQueryInfoA;
    pfn_InternetSetOptionA   InternetSetOptionA;  
};

extern WinInetFns g_inet;

static inline bool wininet_init() {
    HMODULE h = LoadLibraryExA("wininet.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) return false;

#define _LOAD(fn) (g_inet.fn = (pfn_##fn)(void(*)(void))GetProcAddress(h, #fn))
    _LOAD(InternetSetOptionA);
    bool ok = _LOAD(InternetOpenA) &&
              _LOAD(InternetOpenUrlA) &&
              _LOAD(InternetReadFile) &&
              _LOAD(InternetCloseHandle) &&
              _LOAD(HttpQueryInfoA);
#undef _LOAD

    return ok;
}

#endif 
#endif 
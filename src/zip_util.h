#pragma once
#ifndef ZIP_UTIL_H
#define ZIP_UTIL_H

// Little-endian readers shared by the ZIP code in autodetect.cpp and core_download_manager.cpp.
static inline unsigned le16(const unsigned char* p) { return p[0] | (p[1] << 8); }
static inline unsigned long le32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned long)p[3] << 24);
}

#endif
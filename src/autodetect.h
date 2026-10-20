#pragma once
#include "vector.h"
#include "dyn_string.h"

#define MAX_CORE_CANDIDATES 64

static constexpr int kMaxCoreFirmware = 16;

struct CoreFirmwareEntry {
    char path[128];
    char desc[128];
    bool optional;
};

struct CoreInfoFirmware {
    CoreFirmwareEntry entries[kMaxCoreFirmware];
    int               count;
};

bool parseFirmwareFromInfo(const char* core_path, CoreInfoFirmware* out);
bool getCoreMetadataFromInfo(const char* core_path, char* out_display_name, size_t disp_sz, char* out_exts, size_t exts_sz);

int autodetectCoreMulti(const char* content_path,
                         PodVector<DynString>& out_paths,
                         int* out_matched_count,
                         PodVector<DynString>* out_library_names = nullptr,
                         PodVector<int>* out_ext_counts = nullptr);
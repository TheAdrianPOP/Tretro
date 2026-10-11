#pragma once
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "interface.h"

AppSettings& get_cfg();
void cfgSave();
void cfgLoad();
void cfgPushRecent(char arr[][512], int& cnt, const char* path);

#ifndef g_cfg
#define g_cfg get_cfg()
#endif

bool getGameConfigPath(char* out, size_t sz, const char* save_dir, const char* content_path, const char* core_path);
void gameCfgLoad(const char* save_dir, const char* content_path, const char* core_path);
void gameCfgSave(const char* save_dir, const char* content_path, const char* core_path);

#endif 
#pragma once

#ifdef _WIN32

#include "frontend.h"
#include "interface.h"

void checkPastedPathWindows(Frontend& f, RetroUI& ui, const char* exe_path);

#endif 
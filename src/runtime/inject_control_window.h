#pragma once

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {

bool StartInjectControlWindow(std::string* error);
void StopInjectControlWindow();
bool IsInjectControlWindowRelated(HWND hwnd);

}  // namespace pal4::inject

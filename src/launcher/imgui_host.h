#pragma once

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject::launcher {

using ImGuiFrameCallback = bool (*)(HWND hwnd, void* context);

bool RunImGuiHost(
    const wchar_t* title,
    int client_width,
    int client_height,
    ImGuiFrameCallback frame_callback,
    void* context,
    std::wstring* error);

}  // namespace pal4::inject::launcher

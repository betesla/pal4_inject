#pragma once

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {

// Installs observe-only detours over the Miles RSX plugin's statically linked
// CRT failure path. The plugin is loaded after bootstrap, so callers should
// retry once GetModuleHandleA("mssrsx.m3d") becomes available.
bool InstallMssrsxTerminateHook(
    HMODULE module,
    void* replacement,
    void** original_trampoline,
    std::string* error);

bool InstallMssrsxAbortHook(
    HMODULE module,
    void* replacement,
    void** original_trampoline,
    std::string* error);

}  // namespace pal4::inject

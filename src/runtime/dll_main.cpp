#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "bootstrap.h"

namespace {

HANDLE g_ready_event_handle = nullptr;

}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        char ready_event_name[64];
        wsprintfA(ready_event_name, "Local\\PAL4InjectReady_%lu", GetCurrentProcessId());
        g_ready_event_handle = CreateEventA(nullptr, TRUE, FALSE, ready_event_name);
        HANDLE thread = CreateThread(nullptr, 0, &pal4::inject::RuntimeBootstrapThread, instance, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_ready_event_handle) {
            CloseHandle(g_ready_event_handle);
            g_ready_event_handle = nullptr;
        }
    }
    return TRUE;
}

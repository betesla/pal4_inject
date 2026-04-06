#include "cegui_bindings.h"

#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {
namespace {

std::string FormatWindowsError(const DWORD code) {
    char* buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string text;
    if (size && buffer) {
        text.assign(buffer, size);
        LocalFree(buffer);
    } else {
        text = "Windows error " + std::to_string(code);
    }
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

bool ResolveBinding(
    const HMODULE module,
    const char* symbol_name,
    FARPROC* out_proc,
    std::string* error) {
    *out_proc = GetProcAddress(module, symbol_name);
    if (*out_proc) {
        return true;
    }
    if (error) {
        *error = std::string("GetProcAddress failed for ") + symbol_name + ": " +
            FormatWindowsError(GetLastError());
    }
    return false;
}

}  // namespace

bool TryGetCeguiBindings(CeguiBindings* out, std::string* error) {
    if (!out) {
        if (error) {
            *error = "output binding pointer is null";
        }
        return false;
    }

    static std::mutex mutex;
    static bool resolved = false;
    static CeguiBindings cached{};

    std::scoped_lock lock(mutex);
    if (resolved) {
        *out = cached;
        return true;
    }

    const HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        if (error) {
            *error = "CEGUIBase.dll is not loaded";
        }
        return false;
    }

    FARPROC proc = nullptr;
    if (!ResolveBinding(module, "?getSingleton@System@CEGUI@@SAAAV12@XZ", &proc, error)) {
        return false;
    }
    cached.get_system_singleton = reinterpret_cast<CeguiBindings::GetSingletonFn>(proc);

    if (!ResolveBinding(module, "?getSingletonPtr@System@CEGUI@@SAPAV12@XZ", &proc, error)) {
        return false;
    }
    cached.get_system_singleton_ptr = reinterpret_cast<CeguiBindings::GetSingletonPtrFn>(proc);

    if (!ResolveBinding(module, "?injectMouseButtonDown@System@CEGUI@@QAE_NW4MouseButton@2@@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_button_down = reinterpret_cast<CeguiBindings::InjectMouseButtonFn>(proc);

    if (!ResolveBinding(module, "?injectMouseButtonUp@System@CEGUI@@QAE_NW4MouseButton@2@@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_button_up = reinterpret_cast<CeguiBindings::InjectMouseButtonFn>(proc);

    if (!ResolveBinding(module, "?injectMousePosition@System@CEGUI@@QAE_NMM@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_position = reinterpret_cast<CeguiBindings::InjectMousePositionFn>(proc);

    if (!ResolveBinding(module, "?injectMouseWheelChange@System@CEGUI@@QAE_NM@Z", &proc, error)) {
        return false;
    }
    cached.inject_mouse_wheel_change = reinterpret_cast<CeguiBindings::InjectMouseWheelFn>(proc);

    if (!ResolveBinding(module, "?injectChar@System@CEGUI@@QAE_NI@Z", &proc, error)) {
        return false;
    }
    cached.inject_char = reinterpret_cast<CeguiBindings::InjectCharFn>(proc);

    if (!ResolveBinding(module, "?injectKeyDown@System@CEGUI@@QAE_NI@Z", &proc, error)) {
        return false;
    }
    cached.inject_key_down = reinterpret_cast<CeguiBindings::InjectKeyFn>(proc);

    if (!ResolveBinding(module, "?injectKeyUp@System@CEGUI@@QAE_NI@Z", &proc, error)) {
        return false;
    }
    cached.inject_key_up = reinterpret_cast<CeguiBindings::InjectKeyFn>(proc);

    if (!ResolveBinding(module, "?requestRedraw@Window@CEGUI@@QBEXXZ", &proc, error)) {
        return false;
    }
    cached.request_redraw = reinterpret_cast<CeguiBindings::RequestRedrawFn>(proc);

    if (!ResolveBinding(module, "??_7EventArgs@CEGUI@@6B@", &proc, error)) {
        return false;
    }
    cached.event_args_vftable = proc;

    if (!ResolveBinding(module, "?EventNamespace@Renderer@CEGUI@@2VString@2@B", &proc, error)) {
        return false;
    }
    cached.renderer_event_namespace = proc;

    if (!ResolveBinding(module, "?EventDisplaySizeChanged@Renderer@CEGUI@@2VString@2@B", &proc, error)) {
        return false;
    }
    cached.renderer_event_display_size_changed = proc;

    resolved = true;
    *out = cached;
    return true;
}

}  // namespace pal4::inject

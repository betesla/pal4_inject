#include "cegui_font_hooks.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_bindings.h"
#include "pal4inject/cegui_font_resync.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using LoadFontFileFn = int (__cdecl*)(char*);

LoadFontFileFn g_original_load_font_file = nullptr;

struct ScopedCeguiString {
    const CeguiBindings* bindings = nullptr;
    OpaqueCeguiString storage{};
    bool constructed = false;

    ~ScopedCeguiString() {
        if (constructed && bindings && bindings->cegui_string_dtor) {
            bindings->cegui_string_dtor(&storage);
        }
    }
};

void AppendRuntimeDebugLog(const std::string_view line) {
    char temp_path[MAX_PATH];
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
        return;
    }

    std::string log_path = std::string(temp_path, temp_len);
    if (!log_path.empty() && log_path.back() != '\\') {
        log_path.push_back('\\');
    }
    log_path += "pal4_inject_runtime.log";

    HANDLE file = CreateFileA(
        log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    const std::string payload = std::string(line) + "\r\n";
    DWORD written = 0;
    WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    CloseHandle(file);
}

std::uintptr_t MainModuleBase() {
    auto& state = GetRuntimeState();
    std::uintptr_t base = state.MainModuleBase();
    if (base == 0) {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        state.SetMainModuleBase(base);
    }
    return base;
}

void* ResolveRuntimeData(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(ida::ResolveRuntimeAddress(base, ida_ea));
}

int* ReadGameConfigPointer() {
    auto* config_ptr_address =
        static_cast<int**>(ResolveRuntimeData(ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

std::string_view ExtractPathStem(std::string_view path) noexcept {
    const auto slash = path.find_last_of("\\/");
    if (slash != std::string_view::npos) {
        path.remove_prefix(slash + 1);
    }
    const auto dot = path.find_last_of('.');
    if (dot != std::string_view::npos) {
        path = path.substr(0, dot);
    }
    return path;
}

std::string FormatFloatCompact(const float value) {
    const float rounded = std::round(value);
    if (std::fabs(value - rounded) < 0.001F) {
        return std::to_string(static_cast<int>(rounded));
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

void LogFontEvent(const std::string_view text) {
    auto& state = GetRuntimeState();
    state.AppendEventLog(text);
    AppendRuntimeDebugLog(text);
}

std::string BuildFontSummary(
    const std::string_view font_name,
    const std::string_view file_name,
    const HookMode mode,
    const std::string_view action,
    const CeguiDynamicFontResyncTarget* target = nullptr,
    const std::string_view detail = {}) {
    std::ostringstream out;
    out
        << "hook=load_font_file"
        << " font=" << font_name
        << " file=" << file_name
        << " mode=" << ToString(mode)
        << " action=" << action;
    if (target) {
        out
            << " native=" << FormatFloatCompact(target->native_width)
            << "x" << FormatFloatCompact(target->native_height)
            << " notify=" << FormatFloatCompact(target->notify_width)
            << "x" << FormatFloatCompact(target->notify_height);
    }
    if (!detail.empty()) {
        out << " detail=" << detail;
    }
    return out.str();
}

bool BuildCeguiAnsiString(
    const CeguiBindings& bindings,
    const std::string_view text,
    ScopedCeguiString* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "CEGUI string output is null";
        }
        return false;
    }
    if (!bindings.cegui_string_ctor_from_ansi || !bindings.cegui_string_dtor) {
        if (error) {
            *error = "CEGUI string helpers are unavailable";
        }
        return false;
    }

    out->bindings = &bindings;
    const std::string ansi(text);
    bindings.cegui_string_ctor_from_ansi(&out->storage, ansi.c_str());
    out->constructed = true;
    return true;
}

}  // namespace

bool ApplyKnownDynamicFontResync(
    const std::string_view short_name,
    const CeguiWidescreenPlan& plan,
    std::string* error) {
    const auto canonical_name = CanonicalKnownDynamicUiFontName(short_name);
    if (canonical_name.empty()) {
        if (error) {
            *error = "font is not part of the known dynamic UI set";
        }
        return false;
    }

    const auto target = BuildKnownDynamicFontResyncTarget(canonical_name, plan);
    if (!target.apply) {
        if (error) {
            *error = "font resync is not needed for the current render plan";
        }
        return false;
    }

    CeguiBindings bindings{};
    if (!TryGetCeguiBindings(&bindings, error)) {
        return false;
    }
    if (!bindings.get_font_manager_singleton_ptr || !bindings.font_manager_get_font) {
        if (error) {
            *error = "FontManager bindings are unavailable";
        }
        return false;
    }
    if (!bindings.font_set_auto_scaling_enabled ||
        !bindings.font_set_native_resolution ||
        !bindings.font_notify_screen_resolution) {
        if (error) {
            *error = "font resync bindings are unavailable";
        }
        return false;
    }

    void* font_manager = bindings.get_font_manager_singleton_ptr();
    if (!font_manager) {
        if (error) {
            *error = "CEGUI FontManager singleton is null";
        }
        return false;
    }

    ScopedCeguiString font_name{};
    if (!BuildCeguiAnsiString(bindings, canonical_name, &font_name, error)) {
        return false;
    }

    void* font = bindings.font_manager_get_font(font_manager, &font_name.storage);
    if (!font) {
        if (error) {
            *error = std::string("FontManager::getFont returned null for ") +
                std::string(canonical_name);
        }
        return false;
    }

    const CeguiSizeValue native_resolution{
        target.native_width,
        target.native_height,
    };
    const CeguiSizeValue notify_resolution{
        target.notify_width,
        target.notify_height,
    };
    bindings.font_set_auto_scaling_enabled(font, true);
    bindings.font_set_native_resolution(font, native_resolution);
    bindings.font_notify_screen_resolution(font, notify_resolution);
    if (error) {
        error->clear();
    }
    return true;
}

int __cdecl Hook_LoadFontFile(char* file_name) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::load_font_file);

    if (!g_original_load_font_file) {
        const std::string error = "original LoadFontFile trampoline is null";
        state.SetHookError(HookId::load_font_file, error);
        state.SetLastError(error);
        state.SetLastFontSync(
            "hook=load_font_file action=trampoline_missing",
            false);
        LogFontEvent("hook=load_font_file action=trampoline_missing");
        return 0;
    }

    const int result = g_original_load_font_file(file_name);
    const std::string_view file_name_view =
        file_name ? std::string_view(file_name) : std::string_view("<null>");
    const auto known_font_name =
        CanonicalKnownDynamicUiFontName(ExtractPathStem(file_name_view));
    if (known_font_name.empty()) {
        return result;
    }

    const HookMode mode = state.GetHookMode(HookId::load_font_file);
    if (!result) {
        const std::string summary = BuildFontSummary(
            known_font_name,
            file_name_view,
            mode,
            "original_failed");
        state.SetLastFontSync(summary, false);
        LogFontEvent(summary);
        return result;
    }

    const int* config = ReadGameConfigPointer();
    if (!config) {
        const std::string error = "g_GameConfig pointer is null during font resync";
        const std::string summary = BuildFontSummary(
            known_font_name,
            file_name_view,
            mode,
            "config_missing");
        state.SetHookError(HookId::load_font_file, error);
        state.SetLastError(error);
        state.SetLastFontSync(summary, false);
        LogFontEvent(summary + " error=" + error);
        return result;
    }

    const auto plan = BuildCeguiWidescreenPlan(config[0], config[1]);
    const auto target = BuildKnownDynamicFontResyncTarget(known_font_name, plan);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        state.ClearHookError(HookId::load_font_file);
        const std::string summary = BuildFontSummary(
            known_font_name,
            file_name_view,
            mode,
            target.apply ? "observed_ready" : "observed_non_wide",
            &target);
        state.SetLastFontSync(summary, true);
        LogFontEvent(summary);
        return result;
    }

    if (!target.apply) {
        state.ClearHookError(HookId::load_font_file);
        const std::string summary = BuildFontSummary(
            known_font_name,
            file_name_view,
            mode,
            "skipped_non_wide",
            &target);
        state.SetLastFontSync(summary, true);
        LogFontEvent(summary);
        return result;
    }

    std::string error;
    const bool ok = ApplyKnownDynamicFontResync(known_font_name, plan, &error);
    if (!ok) {
        state.SetHookError(HookId::load_font_file, error);
        state.SetLastError(error);
    } else {
        state.ClearHookError(HookId::load_font_file);
    }

    const std::string summary = BuildFontSummary(
        known_font_name,
        file_name_view,
        mode,
        ok ? "resynced" : "resync_failed",
        &target,
        error);
    state.SetLastFontSync(summary, ok);
    LogFontEvent(summary);
    return result;
}

void* GetCeguiFontReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::load_font_file:
        return reinterpret_cast<void*>(&Hook_LoadFontFile);
    default:
        return nullptr;
    }
}

void SetCeguiFontOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::load_font_file:
        g_original_load_font_file =
            reinterpret_cast<LoadFontFileFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

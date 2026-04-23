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
#include "cegui_font_experiment.h"
#include "cegui_font_texture_registry.h"
#include "hook_logging.h"
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

float ReadFontHeightForDiagnostics(
    const CeguiBindings& bindings,
    void* font) noexcept {
    if (!font || !bindings.font_get_font_height) {
        return 0.0F;
    }
    return bindings.font_get_font_height(font, 1.0F);
}

float ReadLineSpacingForDiagnostics(
    const CeguiBindings& bindings,
    void* font) noexcept {
    if (!font || !bindings.font_get_line_spacing) {
        return 0.0F;
    }
    return bindings.font_get_line_spacing(font, 1.0F);
}

void LogFontEvent(const std::string_view text) {
    AppendHookEventLog(HookId::load_font_file, text);
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
            << "x" << FormatFloatCompact(target->notify_height)
            << " oversample=" << FormatFloatCompact(target->oversample_scale);
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
    const float font_height_before = ReadFontHeightForDiagnostics(bindings, font);
    const float line_spacing_before = ReadLineSpacingForDiagnostics(bindings, font);
    bindings.font_set_auto_scaling_enabled(font, true);
    bindings.font_set_native_resolution(font, native_resolution);
    bindings.font_notify_screen_resolution(font, notify_resolution);
    const float font_height_after = ReadFontHeightForDiagnostics(bindings, font);
    const float line_spacing_after = ReadLineSpacingForDiagnostics(bindings, font);
    const bool enable_oversample =
        canonical_name == "dialog_simsun" ||
        ((canonical_name == "system" || canonical_name == "systemBold") &&
            GetRuntimeState().SystemFontOversampleEnabled());
    std::string oversample_detail;
    if (enable_oversample) {
        ApplyDynamicFontOversampleExperiment(
            font,
            canonical_name == "dialog_simsun",
            &oversample_detail);
    }
    const float font_height_final = ReadFontHeightForDiagnostics(bindings, font);
    const float line_spacing_final = ReadLineSpacingForDiagnostics(bindings, font);
    if (error) {
        std::ostringstream detail;
        detail
            << "font_height_before=" << FormatFloatCompact(font_height_before)
            << " font_height_after_notify=" << FormatFloatCompact(font_height_after)
            << " font_height_final=" << FormatFloatCompact(font_height_final)
            << " line_spacing_before=" << FormatFloatCompact(line_spacing_before)
            << " line_spacing_after_notify=" << FormatFloatCompact(line_spacing_after)
            << " line_spacing_final=" << FormatFloatCompact(line_spacing_final);
        if (!oversample_detail.empty()) {
            detail << " " << oversample_detail;
        }
        *error = detail.str();
    }
    return true;
}

bool TryRememberKnownDynamicFontTexture(
    const std::string_view short_name,
    std::string* error) {
    const std::string atlas_name = BuildKnownDynamicUiFontAtlasName(short_name);
    if (atlas_name.empty()) {
        if (error) {
            *error = "font atlas name is unavailable";
        }
        return false;
    }

    CeguiBindings bindings{};
    if (!TryGetCeguiBindings(&bindings, error)) {
        return false;
    }
    if (!bindings.get_imageset_manager_singleton_ptr ||
        !bindings.imageset_manager_is_imageset_present ||
        !bindings.imageset_manager_get_imageset ||
        !bindings.imageset_get_texture) {
        if (error) {
            *error = "imageset bindings are unavailable";
        }
        return false;
    }

    void* imageset_manager = bindings.get_imageset_manager_singleton_ptr();
    if (!imageset_manager) {
        if (error) {
            *error = "CEGUI ImagesetManager singleton is null";
        }
        return false;
    }

    ScopedCeguiString atlas_name_string{};
    if (!BuildCeguiAnsiString(bindings, atlas_name, &atlas_name_string, error)) {
        return false;
    }

    if (!bindings.imageset_manager_is_imageset_present(
            imageset_manager,
            &atlas_name_string.storage)) {
        if (error) {
            *error = std::string("ImagesetManager::isImagesetPresent reported missing atlas ") +
                atlas_name;
        }
        return false;
    }

    void* imageset = bindings.imageset_manager_get_imageset(
        imageset_manager,
        &atlas_name_string.storage);
    if (!imageset) {
        if (error) {
            *error = std::string("ImagesetManager::getImageset returned null for ") + atlas_name;
        }
        return false;
    }

    void* texture = bindings.imageset_get_texture(imageset);
    if (!texture) {
        if (error) {
            *error = std::string("Imageset::getTexture returned null for ") + atlas_name;
        }
        return false;
    }

    RememberKnownDynamicFontTexture(short_name, texture);
    if (error) {
        error->clear();
    }
    return true;
}

bool ApplySystemFontOversamplePreferenceToLoadedFonts(
    const bool enabled,
    std::string* error) {
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

    void* font_manager = bindings.get_font_manager_singleton_ptr();
    if (!font_manager) {
        if (error) {
            *error = "CEGUI FontManager singleton is null";
        }
        return false;
    }

    std::string summary;
    for (const std::string_view font_name : {"system", "systemBold"}) {
        ScopedCeguiString font_name_string{};
        if (!BuildCeguiAnsiString(bindings, font_name, &font_name_string, error)) {
            return false;
        }
        void* font = bindings.font_manager_get_font(font_manager, &font_name_string.storage);
        if (!font) {
            continue;
        }

        std::string detail;
        const bool ok = enabled
            ? ApplyDynamicFontOversampleExperiment(font, false, &detail)
            : RestoreDynamicFontOversampleExperiment(font, &detail);
        if (!ok) {
            if (error) {
                *error = std::string(font_name) + ": " + detail;
            }
            return false;
        }
        if (!summary.empty()) {
            summary += ' ';
        }
        summary += std::string(font_name) + "{" + detail + "}";
    }

    if (error) {
        *error = summary;
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

    if (ok) {
        std::string atlas_error;
        if (!TryRememberKnownDynamicFontTexture(known_font_name, &atlas_error) &&
            !atlas_error.empty()) {
            LogFontEvent(summary + " atlas=" + atlas_error);
        }
    }
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

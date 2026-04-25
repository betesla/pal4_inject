#include "runtime_preferences.h"

#include "cegui_font_hooks.h"
#include "cegui_renderer_hooks.h"
#include "d3d9_quality_hooks.h"
#include "pal4inject/inject_settings.h"
#include "runtime_state.h"
#include "shadow_quality_hooks.h"

namespace pal4::inject {
namespace {

HookMode NormalizePersistedHookMode(const HookMode mode) {
    switch (mode) {
    case HookMode::observe_only:
    case HookMode::replace_with_fallback:
        return mode;
    case HookMode::mirror_compare:
    case HookMode::replace_strict:
        return HookMode::replace_with_fallback;
    }
    return HookMode::replace_with_fallback;
}

void RecordPersistenceError(const std::string& error) {
    if (error.empty()) {
        return;
    }
    auto& state = GetRuntimeState();
    state.SetLastError(error);
    state.AppendEventLog(std::string("settings_error=") + error);
}

}  // namespace

std::filesystem::path RuntimePreferencesPath() {
    return DefaultInjectSettingsPath();
}

HookMode ResolveEnabledHookMode(const HookId id) {
    const auto preferred =
        NormalizePersistedHookMode(GetRuntimeState().GetPreferredActiveHookMode(id));
    return preferred == HookMode::observe_only
        ? HookMode::replace_with_fallback
        : preferred;
}

bool SavePersistedRuntimePreferences(std::string* error) {
    InjectPersistedSettings settings{};
    std::string load_error;
    if (!LoadInjectPersistedSettings(RuntimePreferencesPath(), &settings, &load_error)) {
        settings = {};
    }
    settings.msaa_level = GetRuntimeState().GetMsaaLevel();
    settings.shadow_resolution = GetRuntimeState().GetShadowResolution();
    settings.ui_texture_filter = GetRuntimeState().GetUiTextureFilter();
    settings.vr_mode = GetRuntimeState().GetVrMode();
    settings.dialog_font_hd_enabled = GetRuntimeState().DialogFontHdEnabled();
    settings.system_font_oversample_enabled =
        GetRuntimeState().SystemFontOversampleEnabled();
    settings.gamepad_enabled = GetRuntimeState().GamepadEnabled();
    settings.gamepad_log_enabled = GetRuntimeState().GamepadLogEnabled();
    settings.hooks.clear();
    for (const auto& status : GetRuntimeState().CopyHookStatuses()) {
        settings.hooks.push_back({
            status.id,
            status.mode,
            status.preferred_active_mode,
            status.log_enabled,
        });
    }
    return SaveInjectPersistedSettings(RuntimePreferencesPath(), settings, error);
}

void ApplyGamepadEnabledPreference(
    const bool enabled,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetGamepadEnabled(enabled);
    if (update_last_ui_event) {
        state.SetLastUiEvent(std::string("inject_control:gamepad=") + (enabled ? "on" : "off"));
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyGamepadLogPreference(
    const bool enabled,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetGamepadLogEnabled(enabled);
    if (update_last_ui_event) {
        state.SetLastUiEvent(std::string("inject_control:gamepad_log=") + (enabled ? "on" : "off"));
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyHookModePreference(
    const HookId id,
    const HookMode mode,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    const HookMode normalized_mode = NormalizePersistedHookMode(mode);
    state.SetHookMode(id, normalized_mode);
    if (id == HookId::cegui_renderer_constructor_2) {
        ApplyCeguiRendererHookMode(normalized_mode);
    } else if (id == HookId::d3d9_set_present_parameters) {
        std::string error;
        const auto requested_level =
            normalized_mode == HookMode::observe_only
            ? MsaaLevel::off
            : state.GetMsaaLevel();
        if (!ApplyRequestedMsaaLevel(requested_level, &error) &&
            state.MainModuleBase() != 0) {
            RecordPersistenceError(error);
        }
    }
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:") + ToString(id) + "=" + ToString(normalized_mode));
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyMsaaPreference(
    const MsaaLevel level,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetMsaaLevel(level);

    std::string error;
    const auto hook_mode = state.GetHookMode(HookId::d3d9_set_present_parameters);
    const auto requested_level =
        hook_mode == HookMode::observe_only
        ? MsaaLevel::off
        : level;
    if (!ApplyRequestedMsaaLevel(requested_level, &error) &&
        state.MainModuleBase() != 0) {
        RecordPersistenceError(error);
    }

    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:msaa=") + ToString(level));
    }
    if (persist) {
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyShadowResolutionPreference(
    const ShadowResolution resolution,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetShadowResolution(resolution);

    std::string error;
    if (!ApplyShadowResolutionGlobals(resolution, &error) &&
        state.MainModuleBase() != 0) {
        RecordPersistenceError(error);
    }

    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:shadow_resolution=") + ToString(resolution));
    }
    if (persist) {
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyUiTextureFilterPreference(
    const UiTextureFilter filter,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetUiTextureFilter(filter);
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:ui_texture_filter=") + ToString(filter));
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyVrModePreference(
    const VrMode mode,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetVrMode(mode);
    const HookMode target_mode =
        mode == VrMode::seated_experimental
        ? ResolveEnabledHookMode(HookId::camera_update_matrix)
        : HookMode::observe_only;
    state.SetHookMode(HookId::camera_update_matrix, target_mode);
    state.SetHookMode(
        HookId::game_render_frame,
        mode == VrMode::seated_experimental
            ? ResolveEnabledHookMode(HookId::game_render_frame)
            : HookMode::observe_only);
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:vr_mode=") + ToString(mode));
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyDialogFontHdPreference(
    const bool enabled,
    const bool persist,
    const bool update_last_ui_event,
    const bool apply_live_fonts) {
    auto& state = GetRuntimeState();
    state.SetDialogFontHdEnabled(enabled);
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:dialog_font_hd=") +
            (enabled ? "on" : "off"));
    }
    if (apply_live_fonts) {
        std::string error;
        if (!ApplyDialogFontHdPreferenceToLoadedFonts(enabled, &error) &&
            !error.empty()) {
            RecordPersistenceError(error);
        }
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplySystemFontOversamplePreference(
    const bool enabled,
    const bool persist,
    const bool update_last_ui_event,
    const bool apply_live_fonts) {
    auto& state = GetRuntimeState();
    state.SetSystemFontOversampleEnabled(enabled);
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:system_font_oversample=") +
            (enabled ? "on" : "off"));
    }
    if (apply_live_fonts) {
        std::string error;
        if (!ApplySystemFontOversamplePreferenceToLoadedFonts(enabled, &error) &&
            !error.empty()) {
            RecordPersistenceError(error);
        }
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyHookLogPreference(
    const HookId id,
    const bool enabled,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetHookLogEnabled(id, enabled);
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("inject_control:log:") + ToString(id) + "=" + (enabled ? "on" : "off"));
    }
    if (persist) {
        std::string error;
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

bool LoadPersistedRuntimePreferences(std::string* error) {
    InjectPersistedSettings settings{};
    if (!LoadInjectPersistedSettings(RuntimePreferencesPath(), &settings, error)) {
        return false;
    }

    ApplySystemFontOversamplePreference(
        settings.system_font_oversample_enabled,
        false,
        false,
        false);
    ApplyDialogFontHdPreference(
        settings.dialog_font_hd_enabled,
        false,
        false,
        false);
    ApplyShadowResolutionPreference(settings.shadow_resolution, false, false);
    ApplyGamepadEnabledPreference(settings.gamepad_enabled, false, false);
    ApplyGamepadLogPreference(settings.gamepad_log_enabled, false, false);
    ApplyUiTextureFilterPreference(settings.ui_texture_filter, false, false);
    for (const auto& hook : settings.hooks) {
        GetRuntimeState().SetPreferredActiveHookMode(
            hook.id,
            NormalizePersistedHookMode(hook.active_mode));
        GetRuntimeState().SetHookLogEnabled(hook.id, hook.log_enabled);
        ApplyHookModePreference(
            hook.id,
            NormalizePersistedHookMode(hook.mode),
            false,
            false);
    }
    ApplyVrModePreference(settings.vr_mode, false, false);
    ApplyMsaaPreference(settings.msaa_level, false, false);
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

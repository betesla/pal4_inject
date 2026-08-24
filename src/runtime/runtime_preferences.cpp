#include "runtime_preferences.h"

#include "cegui_renderer_hooks.h"
#include "d3d9_quality_hooks.h"
#include "pal4inject/inject_settings.h"
#include "pal4inject/dialogue_voice_volume.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

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
    const auto preferred = GetRuntimeState().GetPreferredActiveHookMode(id);
    return preferred == HookMode::observe_only
        ? HookMode::replace_with_fallback
        : preferred;
}

bool SavePersistedRuntimePreferences(std::string* error) {
    InjectPersistedSettings settings{};
    const auto settings_path = RuntimePreferencesPath();
    if (std::filesystem::exists(settings_path) &&
        !LoadInjectPersistedSettings(settings_path, &settings, error)) {
        return false;
    }
    settings.msaa_level = GetRuntimeState().GetMsaaLevel();
    settings.bink_scaling_mode = GetRuntimeState().GetBinkScalingMode();
    settings.gi_talk_volume = GetRuntimeState().GetGiTalkVolume();
    settings.gamepad_enabled = GetRuntimeState().GamepadEnabled();
    settings.gamepad_log_enabled = GetRuntimeState().GamepadLogEnabled();
    settings.gamepad_modern_controls = GetRuntimeState().GamepadModernControls();
    settings.gamepad_invert_camera_y = GetRuntimeState().GamepadInvertCameraY();
    settings.gamepad_preserve_free_camera =
        GetRuntimeState().GamepadPreserveFreeCamera();
    settings.gamepad_run_threshold = GetRuntimeState().GamepadRunThreshold();
    settings.gamepad_fast_run_threshold =
        GetRuntimeState().GamepadFastRunThreshold();
    settings.gamepad_camera_sensitivity =
        GetRuntimeState().GamepadCameraSensitivity();
    settings.gamepad_mapping = GetRuntimeState().GetGamepadMapping();
    settings.borderless_window = GetRuntimeState().BorderlessWindowEnabled();
    settings.borderless_monitor = GetRuntimeState().BorderlessMonitor();
    settings.hooks.clear();
    for (const auto& status : GetRuntimeState().CopyHookStatuses()) {
        settings.hooks.push_back({
            status.id,
            status.mode,
            status.preferred_active_mode,
            status.log_enabled,
        });
    }
    return SaveInjectPersistedSettings(settings_path, settings, error);
}

void ApplyHookModePreference(
    const HookId id,
    const HookMode mode,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    state.SetHookMode(id, mode);
    if (id == HookId::cegui_renderer_constructor_2) {
        ApplyCeguiRendererHookMode(mode);
    } else if (id == HookId::d3d9_set_present_parameters) {
        std::string error;
        const auto requested_level =
            (mode == HookMode::observe_only || mode == HookMode::mirror_compare)
            ? MsaaLevel::off
            : state.GetMsaaLevel();
        if (!ApplyRequestedMsaaLevel(requested_level, &error) &&
            state.MainModuleBase() != 0) {
            RecordPersistenceError(error);
        }
    }
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("runtime_preferences:") + ToString(id) + "=" + ToString(mode));
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
        (hook_mode == HookMode::observe_only || hook_mode == HookMode::mirror_compare)
        ? MsaaLevel::off
        : level;
    if (!ApplyRequestedMsaaLevel(requested_level, &error) &&
        state.MainModuleBase() != 0) {
        RecordPersistenceError(error);
    }

    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("runtime_preferences:msaa=") + ToString(level));
    }
    if (persist) {
        if (!SavePersistedRuntimePreferences(&error)) {
            RecordPersistenceError(error);
        }
    }
}

void ApplyGiTalkVolumePreference(
    const float volume,
    const bool persist,
    const bool update_last_ui_event) {
    auto& state = GetRuntimeState();
    const float normalized = ClampGiTalkVolume(volume);
    state.SetGiTalkVolume(normalized);
    if (update_last_ui_event) {
        state.SetLastUiEvent(
            std::string("runtime_preferences:gi_talk_volume=") +
            std::to_string(normalized));
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
            std::string("runtime_preferences:log:") + ToString(id) + "=" + (enabled ? "on" : "off"));
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
    auto settings_path = RuntimePreferencesPath();
    const auto legacy_path = LegacyInjectPanelSettingsPath();
    if (!std::filesystem::exists(settings_path) && std::filesystem::exists(legacy_path)) {
        settings_path = legacy_path;
    }
    if (!LoadInjectPersistedSettings(settings_path, &settings, error)) {
        return false;
    }

    for (const auto& hook : settings.hooks) {
        GetRuntimeState().SetPreferredActiveHookMode(hook.id, hook.active_mode);
        GetRuntimeState().SetHookLogEnabled(hook.id, hook.log_enabled);
        ApplyHookModePreference(hook.id, hook.mode, false, false);
    }
    ApplyHookModePreference(
        HookId::process_inputs,
        settings.gamepad_enabled
            ? HookMode::replace_with_fallback
            : HookMode::observe_only,
        false,
        false);
    GetRuntimeState().SetBinkScalingMode(settings.bink_scaling_mode);
    GetRuntimeState().SetGiTalkVolume(settings.gi_talk_volume);
    GetRuntimeState().SetGamepadMapping(settings.gamepad_mapping);
    GetRuntimeState().SetGamepadLogEnabled(settings.gamepad_log_enabled);
    GetRuntimeState().SetGamepadModernControls(settings.gamepad_modern_controls);
    GetRuntimeState().SetGamepadInvertCameraY(settings.gamepad_invert_camera_y);
    GetRuntimeState().SetGamepadPreserveFreeCamera(
        settings.gamepad_preserve_free_camera);
    GetRuntimeState().SetGamepadRunThreshold(settings.gamepad_run_threshold);
    GetRuntimeState().SetGamepadFastRunThreshold(
        settings.gamepad_fast_run_threshold);
    GetRuntimeState().SetGamepadCameraSensitivity(
        settings.gamepad_camera_sensitivity);
    GetRuntimeState().SetGamepadEnabled(settings.gamepad_enabled);
    GetRuntimeState().SetBorderlessWindowEnabled(settings.borderless_window);
    GetRuntimeState().SetBorderlessMonitor(settings.borderless_monitor);
    ApplyMsaaPreference(settings.msaa_level, false, false);
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

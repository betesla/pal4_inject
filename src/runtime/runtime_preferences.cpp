#include "runtime_preferences.h"

#include "cegui_renderer_hooks.h"
#include "d3d9_quality_hooks.h"
#include "pal4inject/inject_settings.h"
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
    settings.msaa_level = GetRuntimeState().GetMsaaLevel();
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
            std::string("inject_control:") + ToString(id) + "=" + ToString(mode));
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
            std::string("inject_control:msaa=") + ToString(level));
    }
    if (persist) {
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

    for (const auto& hook : settings.hooks) {
        GetRuntimeState().SetPreferredActiveHookMode(hook.id, hook.active_mode);
        GetRuntimeState().SetHookLogEnabled(hook.id, hook.log_enabled);
        ApplyHookModePreference(hook.id, hook.mode, false, false);
    }
    ApplyMsaaPreference(settings.msaa_level, false, false);
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

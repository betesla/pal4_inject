#pragma once

#include <filesystem>
#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

std::filesystem::path RuntimePreferencesPath();
HookMode ResolveEnabledHookMode(HookId id);
void ApplyHookModePreference(
    HookId id,
    HookMode mode,
    bool persist,
    bool update_last_ui_event);
void ApplyHookLogPreference(
    HookId id,
    bool enabled,
    bool persist,
    bool update_last_ui_event);
void ApplyMsaaPreference(
    MsaaLevel level,
    bool persist,
    bool update_last_ui_event);
void ApplyUiTextureFilterPreference(
    UiTextureFilter filter,
    bool persist,
    bool update_last_ui_event);
bool LoadPersistedRuntimePreferences(std::string* error);
bool SavePersistedRuntimePreferences(std::string* error);
void ApplyGamepadEnabledPreference(
    bool enabled,
    bool persist,
    bool update_last_ui_event);
void ApplyGamepadLogPreference(
    bool enabled,
    bool persist,
    bool update_last_ui_event);

}  // namespace pal4::inject

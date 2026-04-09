#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pal4inject/types.h"

namespace pal4::inject {

struct PersistedHookSetting {
    HookId id = HookId::process_ui_event;
    HookMode mode = HookMode::observe_only;
    HookMode active_mode = HookMode::replace_with_fallback;
};

struct InjectPersistedSettings {
    MsaaLevel msaa_level = MsaaLevel::off;
    std::vector<PersistedHookSetting> hooks;
};

std::filesystem::path DefaultInjectSettingsPath();
std::string FormatInjectPersistedSettings(const InjectPersistedSettings& settings);
bool ParseInjectPersistedSettings(
    std::string_view text,
    InjectPersistedSettings* out,
    std::string* error);
bool LoadInjectPersistedSettings(
    const std::filesystem::path& path,
    InjectPersistedSettings* out,
    std::string* error);
bool SaveInjectPersistedSettings(
    const std::filesystem::path& path,
    const InjectPersistedSettings& settings,
    std::string* error);

}  // namespace pal4::inject

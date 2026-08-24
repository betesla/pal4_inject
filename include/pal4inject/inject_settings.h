#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pal4inject/gamepad.h"
#include "pal4inject/types.h"

namespace pal4::inject {

struct PersistedHookSetting {
    HookId id = HookId::process_ui_event;
    HookMode mode = HookMode::observe_only;
    HookMode active_mode = HookMode::replace_with_fallback;
    bool log_enabled = false;
};

struct InjectPersistedSettings {
    ScriptMode script_mode = ScriptMode::csb;
    MsaaLevel msaa_level = MsaaLevel::off;
    BinkScalingMode bink_scaling_mode = BinkScalingMode::fit;
    float gi_talk_volume = 1.0F;
    bool gamepad_enabled = true;
    bool gamepad_log_enabled = false;
    bool gamepad_modern_controls = true;
    bool gamepad_invert_camera_y = false;
    bool gamepad_preserve_free_camera = false;
    float gamepad_run_threshold = 0.62F;
    float gamepad_fast_run_threshold = 0.88F;
    float gamepad_camera_sensitivity = 120.0F;
    Xbox360GamepadMapping gamepad_mapping = DefaultXbox360GamepadMapping();
    bool borderless_window = false;
    std::string borderless_monitor;
    std::vector<PersistedHookSetting> hooks;
};

std::filesystem::path DefaultInjectSettingsPath();
std::filesystem::path LegacyInjectPanelSettingsPath();
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

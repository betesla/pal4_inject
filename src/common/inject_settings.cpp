#include "pal4inject/inject_settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

#include "pal4inject/runtime_paths.h"
#include "pal4inject/dialogue_voice_volume.h"

namespace pal4::inject {
namespace {

constexpr int kSettingsVersion = 11;
constexpr int kMinSupportedSettingsVersion = 1;
constexpr int kMaxSupportedSettingsVersion = 11;

std::string TrimAscii(const std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

}  // namespace

std::filesystem::path DefaultInjectSettingsPath() {
    auto root = InjectDataDirectory();
    root /= "inject_settings.ini";
    return root;
}

std::filesystem::path LegacyInjectPanelSettingsPath() {
    auto root = InjectDataDirectory();
    root /= "inject_panel_settings.ini";
    return root;
}

std::string FormatInjectPersistedSettings(const InjectPersistedSettings& settings) {
    std::ostringstream out;
    out << "version=" << kSettingsVersion << '\n';
    out << "script_mode=" << ToString(settings.script_mode) << '\n';
    out << "msaa_level=" << ToString(settings.msaa_level) << '\n';
    out << "bink_scaling_mode=" << ToString(settings.bink_scaling_mode) << '\n';
    out << "gi_talk_volume=" << std::fixed << std::setprecision(3)
        << ClampGiTalkVolume(settings.gi_talk_volume) << '\n';
    out << "gamepad_enabled=" << (settings.gamepad_enabled ? "1" : "0") << '\n';
    out << "gamepad_log_enabled=" <<
        (settings.gamepad_log_enabled ? "1" : "0") << '\n';
    out << "gamepad_modern_controls=" <<
        (settings.gamepad_modern_controls ? "1" : "0") << '\n';
    out << "gamepad_invert_camera_x=" <<
        (settings.gamepad_invert_camera_x ? "1" : "0") << '\n';
    out << "gamepad_invert_camera_y=" <<
        (settings.gamepad_invert_camera_y ? "1" : "0") << '\n';
    out << "gamepad_preserve_free_camera=" <<
        (settings.gamepad_preserve_free_camera ? "1" : "0") << '\n';
    out << "gamepad_run_threshold=" << std::fixed << std::setprecision(3)
        << std::clamp(settings.gamepad_run_threshold, 0.05F, 0.95F) << '\n';
    out << "gamepad_fast_run_threshold=" << std::fixed << std::setprecision(3)
        << std::clamp(
               settings.gamepad_fast_run_threshold,
               std::clamp(settings.gamepad_run_threshold, 0.05F, 0.95F) + 0.01F,
               1.0F)
        << '\n';
    out << "gamepad_camera_sensitivity=" << std::fixed << std::setprecision(1)
        << std::clamp(settings.gamepad_camera_sensitivity, 20.0F, 360.0F) << '\n';
    out << "borderless_window=" << (settings.borderless_window ? "1" : "0") << '\n';
    out << "borderless_monitor=" << settings.borderless_monitor << '\n';

    std::map<int, PersistedHookSetting> sorted_hooks;
    for (const auto& hook : settings.hooks) {
        sorted_hooks[static_cast<int>(hook.id)] = hook;
    }

    for (const auto& [_, hook] : sorted_hooks) {
        out << "hook." << ToString(hook.id) << ".mode=" << ToString(hook.mode) << '\n';
        out << "hook." << ToString(hook.id) << ".active_mode=" << ToString(hook.active_mode) << '\n';
        out << "hook." << ToString(hook.id) << ".log_enabled=" << (hook.log_enabled ? "1" : "0") << '\n';
    }
    return out.str();
}

bool ParseInjectPersistedSettings(
    const std::string_view text,
    InjectPersistedSettings* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "output settings pointer is null";
        }
        return false;
    }

    *out = {};
    if (error) {
        error->clear();
    }

    std::map<HookId, PersistedHookSetting> hooks;
    std::istringstream input{std::string(text)};
    std::string line;
    int version = 0;
    while (std::getline(input, line)) {
        const auto trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const auto equals = trimmed.find('=');
        if (equals == std::string::npos) {
            if (error) {
                *error = "invalid settings line: missing '='";
            }
            return false;
        }

        const auto key = TrimAscii(trimmed.substr(0, equals));
        const auto value = TrimAscii(trimmed.substr(equals + 1));
        if (key == "version") {
            version = std::atoi(value.c_str());
            continue;
        }
        if (key == "script_mode") {
            ScriptMode script_mode = ScriptMode::inherit;
            if (!TryParseScriptMode(value, &script_mode) ||
                script_mode == ScriptMode::inherit) {
                if (error) {
                    *error = "invalid script_mode value: " + value;
                }
                return false;
            }
            out->script_mode = script_mode;
            continue;
        }
        if (key == "msaa_level") {
            if (!TryParseMsaaLevel(value, &out->msaa_level)) {
                if (error) {
                    *error = "invalid msaa_level value: " + value;
                }
                return false;
            }
            continue;
        }
        if (key == "bink_scaling_mode") {
            if (!TryParseBinkScalingMode(value, &out->bink_scaling_mode)) {
                if (error) {
                    *error = "invalid bink_scaling_mode value: " + value;
                }
                return false;
            }
            continue;
        }
        if (key == "gi_talk_volume") {
            if (!TryParseGiTalkVolume(value, &out->gi_talk_volume)) {
                if (error) {
                    *error = "invalid gi_talk_volume value: " + value;
                }
                return false;
            }
            continue;
        }
        if (key == "gamepad_enabled" || key == "gamepad_log_enabled" ||
            key == "gamepad_modern_controls" ||
            key == "gamepad_invert_camera_x" ||
            key == "gamepad_invert_camera_y" ||
            key == "gamepad_preserve_free_camera") {
            bool* flag = nullptr;
            if (key == "gamepad_enabled") {
                flag = &out->gamepad_enabled;
            } else if (key == "gamepad_log_enabled") {
                flag = &out->gamepad_log_enabled;
            } else if (key == "gamepad_modern_controls") {
                flag = &out->gamepad_modern_controls;
            } else if (key == "gamepad_invert_camera_x") {
                flag = &out->gamepad_invert_camera_x;
            } else if (key == "gamepad_invert_camera_y") {
                flag = &out->gamepad_invert_camera_y;
            } else {
                flag = &out->gamepad_preserve_free_camera;
            }
            if (value == "1" || value == "true" || value == "on") {
                *flag = true;
            } else if (value == "0" || value == "false" || value == "off") {
                *flag = false;
            } else {
                if (error) {
                    *error = "invalid " + key + " value: " + value;
                }
                return false;
            }
            continue;
        }
        if (key == "gamepad_run_threshold" ||
            key == "gamepad_fast_run_threshold" ||
            key == "gamepad_camera_sensitivity") {
            char* end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (!end || end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
                if (error) {
                    *error = "invalid " + key + " value: " + value;
                }
                return false;
            }
            if (key == "gamepad_run_threshold") {
                out->gamepad_run_threshold = std::clamp(parsed, 0.05F, 0.95F);
            } else if (key == "gamepad_fast_run_threshold") {
                out->gamepad_fast_run_threshold = std::clamp(parsed, 0.06F, 1.0F);
            } else {
                out->gamepad_camera_sensitivity =
                    std::clamp(parsed, 20.0F, 360.0F);
            }
            continue;
        }
        constexpr std::string_view kGamepadBindingPrefix = "gamepad.binding.";
        if (key.rfind(kGamepadBindingPrefix, 0) == 0) {
            Xbox360Button button{};
            GamepadAction action{};
            if (!TryParseXbox360Button(
                    std::string_view(key).substr(kGamepadBindingPrefix.size()),
                    &button)) {
                if (error) {
                    *error = "unknown gamepad button in settings: " + key;
                }
                return false;
            }
            if (!TryParseGamepadAction(value, &action)) {
                if (error) {
                    *error = "invalid gamepad action value: " + value;
                }
                return false;
            }
            SetGamepadBinding(&out->gamepad_mapping, button, action);
            continue;
        }
        if (key == "borderless_window") {
            if (value == "1" || value == "true" || value == "on") {
                out->borderless_window = true;
            } else if (value == "0" || value == "false" || value == "off") {
                out->borderless_window = false;
            } else {
                if (error) {
                    *error = "invalid borderless_window value: " + value;
                }
                return false;
            }
            continue;
        }
        if (key == "borderless_monitor") {
            out->borderless_monitor = value;
            continue;
        }
        if (key.rfind("hook.", 0) != 0) {
            continue;
        }

        const auto suffix = key.substr(5);
        const auto suffix_dot = suffix.rfind('.');
        if (suffix_dot == std::string::npos) {
            if (error) {
                *error = "invalid hook settings key: " + key;
            }
            return false;
        }

        const auto hook_name = suffix.substr(0, suffix_dot);
        HookId hook_id{};
        if (!TryParseHookId(hook_name, &hook_id)) {
            if (error) {
                *error = "unknown hook id in settings: " + hook_name;
            }
            return false;
        }

        auto& setting = hooks[hook_id];
        setting.id = hook_id;
        const auto field_name = suffix.substr(suffix_dot + 1);
        if (field_name == "mode") {
            if (!TryParseHookMode(value, &setting.mode)) {
                if (error) {
                    *error = "invalid hook mode value: " + value;
                }
                return false;
            }
        } else if (field_name == "active_mode") {
            if (!TryParseHookMode(value, &setting.active_mode)) {
                if (error) {
                    *error = "invalid hook active_mode value: " + value;
                }
                return false;
            }
        } else if (field_name == "log_enabled") {
            if (value == "1" || value == "true" || value == "on") {
                setting.log_enabled = true;
            } else if (value == "0" || value == "false" || value == "off") {
                setting.log_enabled = false;
            } else {
                if (error) {
                    *error = "invalid hook log_enabled value: " + value;
                }
                return false;
            }
        }
    }

    if (version != 0 &&
        (version < kMinSupportedSettingsVersion || version > kMaxSupportedSettingsVersion)) {
        if (error) {
            *error = "unsupported settings version: " + std::to_string(version);
        }
        return false;
    }

    // Version 11 makes the documented Xbox 360 layout fixed and read-only.
    // Continue accepting historical binding lines so old files load, but do
    // not carry custom mappings into the runtime while editing is unsupported.
    out->gamepad_mapping = DefaultXbox360GamepadMapping();

    out->gamepad_run_threshold =
        std::clamp(out->gamepad_run_threshold, 0.05F, 0.95F);
    out->gamepad_fast_run_threshold = std::clamp(
        out->gamepad_fast_run_threshold,
        out->gamepad_run_threshold + 0.01F,
        1.0F);

    for (const auto& [_, setting] : hooks) {
        out->hooks.push_back(setting);
    }
    return true;
}

bool LoadInjectPersistedSettings(
    const std::filesystem::path& path,
    InjectPersistedSettings* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "output settings pointer is null";
        }
        return false;
    }

    *out = {};
    if (!std::filesystem::exists(path)) {
        if (error) {
            error->clear();
        }
        return true;
    }

    std::ifstream input(path);
    if (!input) {
        if (error) {
            *error = "failed to open settings file: " + path.string();
        }
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return ParseInjectPersistedSettings(buffer.str(), out, error);
}

bool SaveInjectPersistedSettings(
    const std::filesystem::path& path,
    const InjectPersistedSettings& settings,
    std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) {
            *error = "failed to create settings directory: " + path.parent_path().string();
        }
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) {
            *error = "failed to open settings file for write: " + path.string();
        }
        return false;
    }

    output << FormatInjectPersistedSettings(settings);
    output.close();
    if (!output) {
        if (error) {
            *error = "failed to flush settings file: " + path.string();
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

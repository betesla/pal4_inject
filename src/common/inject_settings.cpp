#include "pal4inject/inject_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {
namespace {

constexpr int kSettingsVersion = 1;

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

std::filesystem::path ResolveLocalAppDataRoot() {
    char buffer[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", buffer, MAX_PATH);
    if (len != 0 && len < MAX_PATH) {
        return std::filesystem::path(std::string(buffer, len));
    }

    char temp_path[MAX_PATH]{};
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len != 0 && temp_len < MAX_PATH) {
        return std::filesystem::path(std::string(temp_path, temp_len));
    }
    return std::filesystem::current_path();
}

}  // namespace

std::filesystem::path DefaultInjectSettingsPath() {
    auto root = ResolveLocalAppDataRoot();
    root /= "PAL4Inject";
    root /= "inject_panel_settings.ini";
    return root;
}

std::string FormatInjectPersistedSettings(const InjectPersistedSettings& settings) {
    std::ostringstream out;
    out << "version=" << kSettingsVersion << '\n';
    out << "msaa_level=" << ToString(settings.msaa_level) << '\n';

    std::map<int, PersistedHookSetting> sorted_hooks;
    for (const auto& hook : settings.hooks) {
        sorted_hooks[static_cast<int>(hook.id)] = hook;
    }

    for (const auto& [_, hook] : sorted_hooks) {
        out << "hook." << ToString(hook.id) << ".mode=" << ToString(hook.mode) << '\n';
        out << "hook." << ToString(hook.id) << ".active_mode=" << ToString(hook.active_mode) << '\n';
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
        if (key == "msaa_level") {
            if (!TryParseMsaaLevel(value, &out->msaa_level)) {
                if (error) {
                    *error = "invalid msaa_level value: " + value;
                }
                return false;
            }
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
        }
    }

    if (version != 0 && version != kSettingsVersion) {
        if (error) {
            *error = "unsupported settings version: " + std::to_string(version);
        }
        return false;
    }

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

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/bug_report.h"
#include "pal4inject/inject_settings.h"

namespace pal4::inject::launcher {

struct Resolution {
    int width = 800;
    int height = 600;

    bool operator==(const Resolution& other) const noexcept {
        return width == other.width && height == other.height;
    }

    bool operator<(const Resolution& other) const noexcept {
        return width != other.width ? width < other.width : height < other.height;
    }
};

struct GameDisplayConfig {
    int fullscreen = 0;
    int widescreen = 0;
    int width = 800;
    int height = 600;
    int sync = 1;
    std::vector<std::uint8_t> original_data;
};

struct MonitorDisplayInfo {
    std::string device_name;
    std::string display_name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool primary = false;
    std::vector<Resolution> resolutions;
};

struct LauncherUiState {
    std::filesystem::path game_exe;
    std::filesystem::path runtime_dll;
    std::filesystem::path config_path;
    std::filesystem::path inject_settings_path;
    ScriptMode script_mode = ScriptMode::csb;
    GameDisplayConfig display;
    InjectPersistedSettings inject_settings;
    BugReportData bug_report;
    std::vector<MonitorDisplayInfo> monitors;
    std::vector<Resolution> common_resolutions;
    std::vector<Resolution> display_resolutions;
    bool accepted = false;
};

using CheckForUpdatesCallback = void (*)(HWND owner, bool quiet_if_current_or_failed);
using OpenBugReportCallback = bool (*)(
    HWND owner,
    const std::string& title,
    const std::string& body,
    std::wstring* error);

void SynchronizeAutomaticWidescreen(LauncherUiState* state) noexcept;

bool RunLauncherUi(
    LauncherUiState* state,
    CheckForUpdatesCallback check_for_updates,
    OpenBugReportCallback open_bug_report,
    std::wstring* error);

}  // namespace pal4::inject::launcher

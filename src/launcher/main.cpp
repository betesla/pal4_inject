#include <array>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pal4inject/inject_settings.h"
#include "pal4inject/launcher.h"
#include "pal4inject_build_info.h"

#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>

namespace {

std::filesystem::path CurrentExecutableDirectory() {
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return len == 0
        ? std::filesystem::current_path()
        : std::filesystem::path(std::string(buffer, len)).parent_path();
}

std::filesystem::path DefaultRuntimeDllPath() {
    const auto exe_dir = CurrentExecutableDirectory();
    const auto packaged_dll = exe_dir / "pal4_inject" / "runtime.dll";
    if (std::filesystem::exists(packaged_dll)) {
        return packaged_dll;
    }
    return exe_dir / "runtime.dll";
}

void PrintUsage() {
    std::cout
        << "Usage: PAL4_inject (--game-root <path> | --exe <path>) "
        << "[--dll <path>] [--script-mode cs|csb] [--ready-timeout-ms <ms>] [--no-resume]\n";
}

constexpr int kRadioCsId = 1001;
constexpr int kRadioCsbId = 1002;
constexpr int kLaunchButtonId = 1003;
constexpr int kMinimizeButtonId = 1004;
constexpr int kResolutionTabId = 1005;
constexpr int kCommonResolutionListId = 1006;
constexpr int kDisplayResolutionListId = 1007;
constexpr int kWidthEditId = 1008;
constexpr int kHeightEditId = 1009;
constexpr int kWindowedRadioId = 1010;
constexpr int kFullscreenRadioId = 1011;
constexpr int kWidescreenCheckId = 1012;
constexpr int kVsyncCheckId = 1013;
constexpr int kCheckUpdateButtonId = 1014;
constexpr int kAuthorLinkId = 1015;
constexpr int kMsaaComboId = 1016;
constexpr int kShadowResolutionComboId = 1017;
constexpr int kUiTextureFilterCheckId = 1018;
constexpr int kSystemFontOversampleCheckId = 1019;
constexpr int kDialogFontHdCheckId = 1020;
constexpr int kLauncherTabId = 1021;
constexpr int kStopButtonId = 1022;
constexpr int kRestartButtonId = 1023;
constexpr int kProcessStatusId = 1024;
constexpr int kStrategySummaryId = 1025;
constexpr UINT kLauncherRefreshTimerId = 1;
constexpr UINT kAutoCheckUpdateMessage = WM_APP + 10;
constexpr const wchar_t kGiteeLatestReleaseUrl[] = L"https://gitee.com/api/v5/repos/betesla/pal4_inject/releases/latest";
constexpr const wchar_t kGiteeReleasePageUrl[] = L"https://gitee.com/betesla/pal4_inject/releases";
constexpr const wchar_t kGitHubLatestReleaseUrl[] = L"https://api.github.com/repos/betesla/pal4_inject/releases/latest";
constexpr const wchar_t kGitHubReleasePageUrl[] = L"https://github.com/betesla/pal4_inject/releases/latest";
constexpr const wchar_t kAuthorHomepageUrl[] = L"https://space.bilibili.com/109801988";

struct Resolution {
    int width = 800;
    int height = 600;
};

bool operator<(const Resolution& lhs, const Resolution& rhs) {
    if (lhs.width != rhs.width) {
        return lhs.width < rhs.width;
    }
    return lhs.height < rhs.height;
}

bool operator==(const Resolution& lhs, const Resolution& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

struct GameConfig {
    int fullscreen = 0;
    int widescreen = 0;
    int width = 800;
    int height = 600;
    int sync = 1;
    std::vector<std::uint8_t> original_data;
};

enum class LauncherPage : std::uint8_t {
    startup = 0,
    display,
    graphics,
};

struct GuiLaunchState {
    std::filesystem::path game_exe;
    std::filesystem::path runtime_dll;
    std::filesystem::path config_path;
    pal4::inject::ScriptMode script_mode = pal4::inject::ScriptMode::csb;
    pal4::inject::InjectPersistedSettings inject_settings;
    GameConfig config;
    std::vector<Resolution> common_resolutions;
    std::vector<Resolution> display_resolutions;
    HWND common_list = nullptr;
    HWND display_list = nullptr;
    HWND width_edit = nullptr;
    HWND height_edit = nullptr;
    HWND resolution_tab = nullptr;
    HWND msaa_combo = nullptr;
    HWND shadow_resolution_combo = nullptr;
    HWND windowed_radio = nullptr;
    HWND fullscreen_radio = nullptr;
    HWND widescreen_check = nullptr;
    HWND vsync_check = nullptr;
    HWND ui_filter_check = nullptr;
    HWND system_font_check = nullptr;
    HWND dialog_font_check = nullptr;
    HWND launcher_tab = nullptr;
    std::array<HWND, 3> page_panels{};
    HWND process_status = nullptr;
    HWND strategy_summary = nullptr;
    HWND launch_button = nullptr;
    HWND stop_button = nullptr;
    HWND restart_button = nullptr;
    bool auto_update_check_started = false;
    pal4::inject::InjectedProcess managed_process;
    DWORD last_process_id = 0;
};

struct ReleaseInfo {
    std::string tag_name;
    std::wstring html_url;
    std::string body;
    std::wstring source_name;
};

std::wstring WideFromUtf8(const std::string& text);

bool StartsWith(const std::wstring_view text, const std::wstring_view prefix) {
    return text.size() >= prefix.size() &&
        text.substr(0, prefix.size()) == prefix;
}

std::wstring ShortenPathForDisplay(const std::filesystem::path& path) {
    const std::wstring text = path.wstring();
    constexpr std::size_t kMaxLength = 72;
    if (text.size() <= kMaxLength) {
        return text;
    }
    return text.substr(0, 24) + L"..." + text.substr(text.size() - 45);
}

HWND CreateLabel(
    const HWND parent,
    const std::wstring& text,
    const int x,
    const int y,
    const int width,
    const int height,
    const HFONT font = nullptr) {
    const HWND hwnd = CreateWindowExW(
        0,
        L"STATIC",
        text.c_str(),
        WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        height,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    return hwnd;
}

HFONT CreateUnderlineFontFrom(const HFONT base_font) {
    LOGFONTW log_font{};
    if (!GetObjectW(base_font, sizeof(log_font), &log_font)) {
        return nullptr;
    }
    log_font.lfUnderline = TRUE;
    return CreateFontIndirectW(&log_font);
}

void ShowGuiError(const std::wstring& error) {
    MessageBoxW(
        nullptr,
        error.empty() ? L"启动失败。" : error.c_str(),
        L"PAL4 注入启动器",
        MB_ICONERROR | MB_OK);
}

pal4::inject::ScriptMode NormalizeLauncherScriptMode(
    const pal4::inject::ScriptMode mode) {
    return mode == pal4::inject::ScriptMode::cs
        ? pal4::inject::ScriptMode::cs
        : pal4::inject::ScriptMode::csb;
}

pal4::inject::InjectPersistedSettings LoadPersistedLauncherInjectSettings() {
    pal4::inject::InjectPersistedSettings settings{};
    std::string error;
    if (!pal4::inject::LoadInjectPersistedSettings(
            pal4::inject::DefaultInjectSettingsPath(),
            &settings,
            &error)) {
        settings = {};
    }
    settings.launcher_script_mode = NormalizeLauncherScriptMode(settings.launcher_script_mode);
    return settings;
}

void SavePersistedLauncherInjectSettings(const pal4::inject::InjectPersistedSettings& input) {
    const auto settings_path = pal4::inject::DefaultInjectSettingsPath();
    auto settings = input;
    settings.launcher_script_mode = NormalizeLauncherScriptMode(settings.launcher_script_mode);
    std::string error;
    pal4::inject::SaveInjectPersistedSettings(settings_path, settings, &error);
}

std::array<pal4::inject::MsaaLevel, 4> BuildMsaaLevels() {
    return {
        pal4::inject::MsaaLevel::off,
        pal4::inject::MsaaLevel::x2,
        pal4::inject::MsaaLevel::x4,
        pal4::inject::MsaaLevel::x8,
    };
}

std::array<pal4::inject::ShadowResolution, 4> BuildShadowResolutions() {
    return {
        pal4::inject::ShadowResolution::x64,
        pal4::inject::ShadowResolution::x128,
        pal4::inject::ShadowResolution::x256,
        pal4::inject::ShadowResolution::x512,
    };
}

template <typename TEnum, std::size_t N>
int FindEnumIndex(
    const std::array<TEnum, N>& values,
    const TEnum value,
    const int fallback = 0) {
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        if (values[static_cast<std::size_t>(i)] == value) {
            return i;
        }
    }
    return fallback;
}

void PopulateMsaaCombo(const HWND combo, const pal4::inject::MsaaLevel selected) {
    const auto values = BuildMsaaLevels();
    for (const auto value : values) {
        const auto label = WideFromUtf8(pal4::inject::ToString(value));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, FindEnumIndex(values, selected), 0);
}

void PopulateShadowResolutionCombo(
    const HWND combo,
    const pal4::inject::ShadowResolution selected) {
    const auto values = BuildShadowResolutions();
    for (const auto value : values) {
        const std::wstring label =
            WideFromUtf8(pal4::inject::ToString(value)) +
            L"x" +
            WideFromUtf8(pal4::inject::ToString(value));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, FindEnumIndex(values, selected), 0);
}

pal4::inject::MsaaLevel MsaaLevelFromComboSelection(const HWND combo) {
    const auto values = BuildMsaaLevels();
    const int selected = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (selected < 0 || selected >= static_cast<int>(values.size())) {
        return pal4::inject::MsaaLevel::off;
    }
    return values[static_cast<std::size_t>(selected)];
}

pal4::inject::ShadowResolution ShadowResolutionFromComboSelection(const HWND combo) {
    const auto values = BuildShadowResolutions();
    const int selected = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (selected < 0 || selected >= static_cast<int>(values.size())) {
        return pal4::inject::ShadowResolution::x64;
    }
    return values[static_cast<std::size_t>(selected)];
}

std::wstring WideFromUtf8(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        required);
    return wide;
}

std::optional<std::string> ExtractJsonStringField(
    const std::string& json,
    const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    const auto key_pos = json.find(marker);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto colon_pos = json.find(':', key_pos + marker.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    auto quote_pos = json.find('"', colon_pos + 1);
    if (quote_pos == std::string::npos) {
        return std::nullopt;
    }

    std::string value;
    for (std::size_t index = quote_pos + 1; index < json.size(); ++index) {
        const char ch = json[index];
        if (ch == '\\' && index + 1 < json.size()) {
            const char escaped = json[++index];
            switch (escaped) {
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case '\\':
            case '"':
            case '/':
                value.push_back(escaped);
                break;
            default:
                value.push_back(escaped);
                break;
            }
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return std::nullopt;
}

std::optional<std::string> ExtractTopLevelJsonStringField(
    const std::string& json,
    const std::string& key) {
    int depth = 0;
    bool in_string = false;
    bool escape = false;

    for (std::size_t index = 0; index < json.size(); ++index) {
        const char ch = json[index];
        if (in_string) {
            if (escape) {
                escape = false;
                continue;
            }
            if (ch == '\\') {
                escape = true;
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            if (depth != 1) {
                in_string = true;
                continue;
            }

            const std::size_t key_begin = index + 1;
            std::size_t key_end = key_begin;
            bool key_escape = false;
            for (; key_end < json.size(); ++key_end) {
                const char key_ch = json[key_end];
                if (key_escape) {
                    key_escape = false;
                    continue;
                }
                if (key_ch == '\\') {
                    key_escape = true;
                    continue;
                }
                if (key_ch == '"') {
                    break;
                }
            }
            if (key_end >= json.size()) {
                return std::nullopt;
            }

            const std::string current_key = json.substr(key_begin, key_end - key_begin);
            index = key_end;
            if (current_key != key) {
                in_string = false;
                continue;
            }

            std::size_t value_pos = key_end + 1;
            while (value_pos < json.size() && std::isspace(static_cast<unsigned char>(json[value_pos]))) {
                ++value_pos;
            }
            if (value_pos >= json.size() || json[value_pos] != ':') {
                return std::nullopt;
            }
            ++value_pos;
            while (value_pos < json.size() && std::isspace(static_cast<unsigned char>(json[value_pos]))) {
                ++value_pos;
            }
            if (value_pos >= json.size() || json[value_pos] != '"') {
                return std::nullopt;
            }

            std::string value;
            bool value_escape = false;
            for (std::size_t value_index = value_pos + 1; value_index < json.size(); ++value_index) {
                const char value_ch = json[value_index];
                if (value_escape) {
                    switch (value_ch) {
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 'r':
                        value.push_back('\r');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    case '\\':
                    case '"':
                    case '/':
                        value.push_back(value_ch);
                        break;
                    default:
                        value.push_back(value_ch);
                        break;
                    }
                    value_escape = false;
                    continue;
                }
                if (value_ch == '\\') {
                    value_escape = true;
                    continue;
                }
                if (value_ch == '"') {
                    return value;
                }
                value.push_back(value_ch);
            }
            return std::nullopt;
        }

        if (ch == '{' || ch == '[') {
            ++depth;
        } else if ((ch == '}' || ch == ']') && depth > 0) {
            --depth;
        }
    }
    return std::nullopt;
}

std::wstring BuildPreferredReleaseUrl(
    const wchar_t* fallback_page_url,
    const std::wstring_view source_name,
    const std::string& tag_name,
    const std::string& payload) {
    const auto top_level_html_url = ExtractTopLevelJsonStringField(payload, "html_url");
    if (top_level_html_url && !top_level_html_url->empty()) {
        return WideFromUtf8(*top_level_html_url);
    }

    if (source_name == L"Gitee" && fallback_page_url && *fallback_page_url) {
        std::wstring release_page = fallback_page_url;
        if (!tag_name.empty() && !StartsWith(release_page, L"https://gitee.com/")) {
            return release_page;
        }
        if (!tag_name.empty()) {
            if (!release_page.empty() && release_page.back() == L'/') {
                release_page.pop_back();
            }
            return release_page + L"/tag/" + WideFromUtf8(tag_name);
        }
    }

    const auto asset_download_url = ExtractJsonStringField(payload, "browser_download_url");
    if (asset_download_url && !asset_download_url->empty()) {
        return WideFromUtf8(*asset_download_url);
    }

    return fallback_page_url ? std::wstring(fallback_page_url) : std::wstring();
}

bool FetchLatestReleaseInfo(
    const wchar_t* api_url,
    const wchar_t* fallback_page_url,
    const wchar_t* source_name,
    ReleaseInfo* out,
    std::wstring* error) {
    if (!out) {
        return false;
    }
    HINTERNET internet = InternetOpenW(
        L"PAL4_inject",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr,
        nullptr,
        0);
    if (!internet) {
        if (error) {
            *error = L"无法初始化网络连接。";
        }
        return false;
    }

    HINTERNET request = InternetOpenUrlW(
        internet,
        api_url,
        L"Accept: application/json\r\nUser-Agent: PAL4_inject\r\n",
        0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE,
        0);
    if (!request) {
        InternetCloseHandle(internet);
        if (error) {
            *error = std::wstring(L"无法连接 ") + source_name + L" Releases。";
        }
        return false;
    }

    std::string payload;
    char buffer[4096]{};
    DWORD read = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read != 0) {
        payload.append(buffer, buffer + read);
    }

    InternetCloseHandle(request);
    InternetCloseHandle(internet);

    const auto tag_name = ExtractJsonStringField(payload, "tag_name");
    if (!tag_name || tag_name->empty()) {
        if (error) {
            *error = std::wstring(source_name) + L" 未找到最新版本信息。请确认仓库已经发布 Release。";
        }
        return false;
    }

    out->tag_name = *tag_name;
    out->html_url = BuildPreferredReleaseUrl(
        fallback_page_url,
        source_name,
        out->tag_name,
        payload);
    out->body = ExtractJsonStringField(payload, "body").value_or("");
    if (out->body.empty()) {
        out->body = ExtractJsonStringField(payload, "description").value_or("");
    }
    out->source_name = source_name;
    return true;
}

bool TryFetchLatestReleaseWithFallback(
    ReleaseInfo* release,
    std::wstring* error) {
    if (!release) {
        return false;
    }
    std::wstring gitee_error;
    if (FetchLatestReleaseInfo(
            kGiteeLatestReleaseUrl,
            kGiteeReleasePageUrl,
            L"Gitee",
            release,
            &gitee_error)) {
        return true;
    }

    std::wstring github_error;
    if (FetchLatestReleaseInfo(
            kGitHubLatestReleaseUrl,
            kGitHubReleasePageUrl,
            L"GitHub",
            release,
            &github_error)) {
        return true;
    }

    if (error) {
        *error =
            L"无法获取最新版本信息。\n\nGitee: " + gitee_error +
            L"\nGitHub: " + github_error;
    }
    return false;
}

bool IsPossiblyNewerRelease(const ReleaseInfo& release) {
    return !release.tag_name.empty() && release.tag_name != pal4::inject::kPal4InjectVersion;
}

void ShowReleaseUpdatePrompt(const HWND owner, const ReleaseInfo& release) {
    const std::string current = pal4::inject::kPal4InjectVersion;
    const std::string build = pal4::inject::kPal4InjectBuildId;
    std::wstring message =
        L"发现可能的新版本（来源：" + release.source_name + L"）。\n\n当前版本：" + WideFromUtf8(current) +
        L"\n当前构建：" + WideFromUtf8(build) +
        L"\n最新版本：" + WideFromUtf8(release.tag_name);
    if (!release.body.empty()) {
        auto body = WideFromUtf8(release.body);
        if (body.size() > 700) {
            body = body.substr(0, 700) + L"\n...";
        }
        message += L"\n\n更新说明：\n" + body;
    }
    message += L"\n\n是否打开下载页面？";

    const int choice = MessageBoxW(
        owner,
        message.c_str(),
        L"检查更新",
        MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON1);
    if (choice == IDYES) {
        const std::wstring url = release.html_url.empty()
            ? std::wstring(kGiteeReleasePageUrl)
            : release.html_url;
        ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void CheckForUpdates(const HWND owner, const bool quiet_if_current_or_failed) {
    ReleaseInfo release{};
    std::wstring error;
    if (!TryFetchLatestReleaseWithFallback(&release, &error)) {
        if (quiet_if_current_or_failed) {
            return;
        }
        MessageBoxW(
            owner,
            error.c_str(),
            L"检查更新",
            MB_ICONWARNING | MB_OK);
        return;
    }

    if (!IsPossiblyNewerRelease(release)) {
        if (quiet_if_current_or_failed) {
            return;
        }
        const std::wstring message =
            L"当前已经是最新版本。\n\n当前版本：" + WideFromUtf8(pal4::inject::kPal4InjectVersion) +
            L"\n当前构建：" + WideFromUtf8(pal4::inject::kPal4InjectBuildId);
        MessageBoxW(owner, message.c_str(), L"检查更新", MB_ICONINFORMATION | MB_OK);
        return;
    }

    ShowReleaseUpdatePrompt(owner, release);
}

DWORD WINAPI AutoCheckForUpdatesThreadProc(LPVOID parameter) {
    const HWND hwnd = reinterpret_cast<HWND>(parameter);
    ReleaseInfo* release = new ReleaseInfo();
    std::wstring error;
    if (!TryFetchLatestReleaseWithFallback(release, &error) ||
        !IsPossiblyNewerRelease(*release)) {
        delete release;
        return 0;
    }
    PostMessageW(hwnd, kAutoCheckUpdateMessage, 0, reinterpret_cast<LPARAM>(release));
    return 0;
}

void StartAutoUpdateCheck(const HWND hwnd, GuiLaunchState* state) {
    if (!state || state->auto_update_check_started) {
        return;
    }
    state->auto_update_check_started = true;
    HANDLE thread = CreateThread(nullptr, 0, &AutoCheckForUpdatesThreadProc, hwnd, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

std::vector<Resolution> BuildCommonResolutions() {
    return {
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1280, 800},
        {1366, 768},
        {1600, 900},
        {1920, 1080},
        {2560, 1440},
        {3440, 1440},
        {3840, 2160},
        {5120, 2160},
    };
}

std::vector<Resolution> EnumeratePrimaryDisplayResolutions() {
    std::set<Resolution> unique;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD index = 0; EnumDisplaySettingsW(nullptr, index, &mode); ++index) {
        const auto width = static_cast<int>(mode.dmPelsWidth);
        const auto height = static_cast<int>(mode.dmPelsHeight);
        if (width >= 800 && height >= 600) {
            unique.insert({width, height});
        }
    }
    return {unique.begin(), unique.end()};
}

std::wstring FormatResolution(const Resolution& resolution) {
    return std::to_wstring(resolution.width) + L" x " + std::to_wstring(resolution.height);
}

std::optional<std::size_t> FindConfigValueOffset(
    const std::vector<std::uint8_t>& data,
    const char* key) {
    const auto key_length = std::strlen(key);
    if (key_length == 0 || data.size() < key_length + 4) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index + key_length + 4 <= data.size(); ++index) {
        if (std::memcmp(data.data() + index, key, key_length) == 0) {
            return index + key_length;
        }
    }
    return std::nullopt;
}

int ReadConfigInt(
    const std::vector<std::uint8_t>& data,
    const char* key,
    const int fallback) {
    const auto offset = FindConfigValueOffset(data, key);
    if (!offset || *offset + 4 > data.size()) {
        return fallback;
    }
    const auto base = *offset;
    return static_cast<int>(
        static_cast<std::uint32_t>(data[base]) |
        (static_cast<std::uint32_t>(data[base + 1]) << 8U) |
        (static_cast<std::uint32_t>(data[base + 2]) << 16U) |
        (static_cast<std::uint32_t>(data[base + 3]) << 24U));
}

void WriteConfigInt(
    std::vector<std::uint8_t>* data,
    const char* key,
    const int value) {
    if (!data) {
        return;
    }
    const auto offset = FindConfigValueOffset(*data, key);
    if (!offset || *offset + 4 > data->size()) {
        return;
    }
    const auto encoded = static_cast<std::uint32_t>(value);
    const auto base = *offset;
    (*data)[base] = static_cast<std::uint8_t>(encoded & 0xFFU);
    (*data)[base + 1] = static_cast<std::uint8_t>((encoded >> 8U) & 0xFFU);
    (*data)[base + 2] = static_cast<std::uint8_t>((encoded >> 16U) & 0xFFU);
    (*data)[base + 3] = static_cast<std::uint8_t>((encoded >> 24U) & 0xFFU);
}

std::vector<std::uint8_t> BuildDefaultConfigData() {
    const std::uint8_t bytes[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        'f', 'u', 'l', 'l', 's', 'c', 'r', 'e', 'e', 'n', 0, 0, 0, 0,
        'h', 'e', 'i', 'g', 'h', 't', 0, 0, 0, 0,
        's', 'y', 'n', 'c', 1, 0, 0, 0,
        'w', 'i', 'd', 'e', 's', 'c', 'r', 'e', 'e', 'n', 0, 0, 0, 0,
        'w', 'i', 'd', 't', 'h', 0x20, 0x03, 0, 0,
    };
    return {std::begin(bytes), std::end(bytes)};
}

GameConfig LoadGameConfig(const std::filesystem::path& path) {
    GameConfig config{};
    std::ifstream in(path, std::ios::binary);
    if (in) {
        config.original_data.assign(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }
    if (config.original_data.empty()) {
        config.original_data = BuildDefaultConfigData();
    }

    config.fullscreen = ReadConfigInt(config.original_data, "fullscreen", config.fullscreen);
    config.height = ReadConfigInt(config.original_data, "height", config.height);
    config.sync = ReadConfigInt(config.original_data, "sync", config.sync);
    config.widescreen = ReadConfigInt(config.original_data, "widescreen", config.widescreen);
    config.width = ReadConfigInt(config.original_data, "width", config.width);
    return config;
}

bool SaveGameConfig(
    const std::filesystem::path& path,
    const GameConfig& config,
    std::wstring* error) {
    auto data = config.original_data.empty()
        ? BuildDefaultConfigData()
        : config.original_data;
    WriteConfigInt(&data, "fullscreen", config.fullscreen ? 1 : 0);
    WriteConfigInt(&data, "height", config.height);
    WriteConfigInt(&data, "sync", config.sync ? 1 : 0);
    WriteConfigInt(&data, "widescreen", config.widescreen ? 1 : 0);
    WriteConfigInt(&data, "width", config.width);

    if (std::filesystem::exists(path)) {
        const auto backup_path = path.wstring() + L".bak";
        std::error_code copy_error;
        std::filesystem::copy_file(
            path,
            backup_path,
            std::filesystem::copy_options::overwrite_existing,
            copy_error);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) {
            *error = L"无法写入 config.cfg。";
        }
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
    if (!out) {
        if (error) {
            *error = L"保存 config.cfg 时发生错误。";
        }
        return false;
    }
    return true;
}

int GetIntFromEdit(const HWND edit, const int fallback) {
    wchar_t buffer[32]{};
    GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
    wchar_t* end = nullptr;
    const long value = std::wcstol(buffer, &end, 10);
    if (end == buffer || value <= 0 || value > 20000) {
        return fallback;
    }
    return static_cast<int>(value);
}

void SetEditInt(const HWND edit, const int value) {
    SetWindowTextW(edit, std::to_wstring(value).c_str());
}

void ApplyResolutionToState(
    GuiLaunchState* state,
    const Resolution& resolution) {
    if (!state) {
        return;
    }
    state->config.width = resolution.width;
    state->config.height = resolution.height;
    SetEditInt(state->width_edit, resolution.width);
    SetEditInt(state->height_edit, resolution.height);
}

void PopulateResolutionList(
    const HWND list,
    const std::vector<Resolution>& resolutions,
    const GameConfig& config) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int selected = LB_ERR;
    for (std::size_t index = 0; index < resolutions.size(); ++index) {
        const auto label = FormatResolution(resolutions[index]);
        const auto item = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(index));
        if (resolutions[index].width == config.width && resolutions[index].height == config.height) {
            selected = item;
        }
    }
    if (selected != LB_ERR) {
        SendMessageW(list, LB_SETCURSEL, selected, 0);
    }
}

void ShowResolutionTab(GuiLaunchState* state, const int tab_index) {
    if (!state) {
        return;
    }
    ShowWindow(state->common_list, tab_index == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(state->display_list, tab_index == 1 ? SW_SHOW : SW_HIDE);
}

void InsertResolutionTab(const HWND tab, const int index, const wchar_t* title) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(title);
    SendMessageW(tab, TCM_INSERTITEMW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&item));
}

int LauncherPageIndex(const LauncherPage page) noexcept {
    return static_cast<int>(page);
}

RECT GetLauncherTabContentRect(const HWND tab) {
    RECT rect{};
    if (!tab) {
        return rect;
    }
    GetClientRect(tab, &rect);
    TabCtrl_AdjustRect(tab, FALSE, &rect);
    return rect;
}

void ShowLauncherPage(GuiLaunchState* state, const int tab_index) {
    if (!state) {
        return;
    }
    for (std::size_t i = 0; i < state->page_panels.size(); ++i) {
        if (state->page_panels[i]) {
            ShowWindow(state->page_panels[i], static_cast<int>(i) == tab_index ? SW_SHOW : SW_HIDE);
        }
    }
}

HWND CreateReadOnlyNote(
    const HWND parent,
    const std::wstring& text,
    const int x,
    const int y,
    const int width,
    const int height,
    const int control_id = 0) {
    const HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        x,
        y,
        width,
        height,
        parent,
        control_id == 0 ? nullptr : reinterpret_cast<HMENU>(control_id),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return hwnd;
}

bool IsManagedProcessRunning(const GuiLaunchState& state, DWORD* exit_code = nullptr) {
    if (!state.managed_process.process_info.hProcess) {
        if (exit_code) {
            *exit_code = STILL_ACTIVE;
        }
        return false;
    }

    DWORD code = 0;
    if (!GetExitCodeProcess(state.managed_process.process_info.hProcess, &code)) {
        code = 0;
    }
    if (exit_code) {
        *exit_code = code;
    }
    return code == STILL_ACTIVE;
}

void ReleaseManagedProcess(GuiLaunchState* state) {
    if (!state) {
        return;
    }
    state->managed_process.Close();
}

std::wstring BuildGraphicsPresetSummary(const GuiLaunchState& state) {
    std::wstring summary = L"\u5f53\u524d\u542f\u52a8\u524d\u7b56\u7565\uff1a";
    summary += L"\r\n- \u6297\u952f\u9f7f\uff1a";
    summary += WideFromUtf8(pal4::inject::ToString(state.inject_settings.msaa_level));
    summary += L" | \u7528\u6765\u51cf\u5c11\u6a21\u578b\u8fb9\u7f18\u952f\u9f7f";
    summary += L"\r\n- \u4eba\u7269\u9634\u5f71\uff1a";
    summary += WideFromUtf8(pal4::inject::ToString(state.inject_settings.shadow_resolution));
    summary += L"x | \u7528\u6765\u8ba9\u4eba\u7269\u5f71\u5b50\u4e0d\u518d\u7cca\u6210\u4e00\u56e2";
    summary += L"\r\n- UI \u91c7\u6837\uff1a";
    summary += state.inject_settings.ui_texture_filter == pal4::inject::UiTextureFilter::nearest
        ? L"\u50cf\u7d20\u8fb9\u7f18\u66f4\u786c"
        : L"\u539f\u7248\u7ebf\u6027\u8fc7\u6ee4";
    summary += L"\r\n- \u7cfb\u7edf\u5b57\u4f53\uff1a";
    summary += state.inject_settings.system_font_oversample_enabled
        ? L"\u542f\u7528\u9ad8\u6e05\u5b9e\u9a8c"
        : L"\u4fdd\u6301\u539f\u7248";
    summary += L"\r\n- \u5bf9\u767d\u5b57\u4f53\uff1a";
    summary += state.inject_settings.dialog_font_hd_enabled
        ? L"\u542f\u7528 dialog_simsun \u9ad8\u6e05 A/B"
        : L"\u4fdd\u6301\u539f\u7248";
    return summary;
}

void SyncPreviewSelectionsFromControls(GuiLaunchState* state) {
    if (!state) {
        return;
    }
    if (state->msaa_combo) {
        state->inject_settings.msaa_level = MsaaLevelFromComboSelection(state->msaa_combo);
    }
    if (state->shadow_resolution_combo) {
        state->inject_settings.shadow_resolution =
            ShadowResolutionFromComboSelection(state->shadow_resolution_combo);
    }
    if (state->ui_filter_check) {
        state->inject_settings.ui_texture_filter =
            SendMessageW(state->ui_filter_check, BM_GETCHECK, 0, 0) == BST_CHECKED
            ? pal4::inject::UiTextureFilter::nearest
            : pal4::inject::UiTextureFilter::linear;
    }
    if (state->system_font_check) {
        state->inject_settings.system_font_oversample_enabled =
            SendMessageW(state->system_font_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (state->dialog_font_check) {
        state->inject_settings.dialog_font_hd_enabled =
            SendMessageW(state->dialog_font_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
}

std::wstring BuildProcessStatusText(const GuiLaunchState& state) {
    if (IsManagedProcessRunning(state)) {
        return L"\u6e38\u620f\u72b6\u6001\uff1a\u8fd0\u884c\u4e2d | PID " + std::to_wstring(state.last_process_id) +
            L" | \u53ef\u4ece\u8fd9\u91cc\u76f4\u63a5\u505c\u6b62\u6216\u91cd\u542f";
    }
    if (state.last_process_id != 0) {
        return L"\u6e38\u620f\u72b6\u6001\uff1a\u672c\u6b21\u6700\u540e\u4e00\u6b21\u8fd0\u884c PID " +
            std::to_wstring(state.last_process_id) + L" \u5df2\u7ed3\u675f";
    }
    return L"\u6e38\u620f\u72b6\u6001\uff1a\u672a\u542f\u52a8 | \u8fd9\u4e2a\u542f\u52a8\u5668\u4f1a\u4e00\u76f4\u4fdd\u7559\uff0c\u5173\u95ed\u7a97\u53e3\u53ea\u4f1a\u6700\u5c0f\u5316";
}

void RefreshLauncherStatus(HWND hwnd, GuiLaunchState* state) {
    if (!hwnd || !state) {
        return;
    }
    SyncPreviewSelectionsFromControls(state);
    if (state->process_status) {
        SetWindowTextW(state->process_status, BuildProcessStatusText(*state).c_str());
    }
    if (state->strategy_summary) {
        SetWindowTextW(state->strategy_summary, BuildGraphicsPresetSummary(*state).c_str());
    }

    const bool running = IsManagedProcessRunning(*state);
    if (!running && state->managed_process.process_info.hProcess) {
        ReleaseManagedProcess(state);
    }
    if (state->launch_button) {
        EnableWindow(state->launch_button, running ? FALSE : TRUE);
    }
    if (state->stop_button) {
        EnableWindow(state->stop_button, running ? TRUE : FALSE);
    }
    if (state->restart_button) {
        EnableWindow(state->restart_button, state->last_process_id != 0 ? TRUE : FALSE);
    }
}

void MinimizeLauncherWindow(const HWND hwnd) {
    ShowWindow(hwnd, SW_MINIMIZE);
}

bool SaveLauncherSelections(GuiLaunchState* state, std::wstring* error) {
    if (!state) {
        if (error) {
            *error = L"\u542f\u52a8\u5668\u72b6\u6001\u4e3a\u7a7a\u3002";
        }
        return false;
    }
    if (!std::filesystem::exists(state->game_exe)) {
        if (error) {
            *error = L"\u6ca1\u6709\u627e\u5230 PAL4.exe\u3002\n\n\u8bf7\u628a PAL4_inject.exe \u653e\u5230 PAL4.exe \u6240\u5728\u76ee\u5f55\u3002";
        }
        return false;
    }
    if (!std::filesystem::exists(state->runtime_dll)) {
        if (error) {
            *error = L"\u6ca1\u6709\u627e\u5230 pal4_inject\\\\runtime.dll\u3002\n\n\u8bf7\u786e\u8ba4 pal4_inject \u6587\u4ef6\u5939\u5df2\u590d\u5236\u5b8c\u6574\u3002";
        }
        return false;
    }

    state->config.width = GetIntFromEdit(state->width_edit, state->config.width);
    state->config.height = GetIntFromEdit(state->height_edit, state->config.height);
    state->config.fullscreen =
        SendMessageW(state->fullscreen_radio, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
    state->config.widescreen =
        SendMessageW(state->widescreen_check, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
    state->config.sync =
        SendMessageW(state->vsync_check, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;

    if (!SaveGameConfig(state->config_path, state->config, error)) {
        return false;
    }

    state->inject_settings.launcher_script_mode =
        NormalizeLauncherScriptMode(state->script_mode);
    if (state->msaa_combo) {
        state->inject_settings.msaa_level = MsaaLevelFromComboSelection(state->msaa_combo);
    }
    if (state->shadow_resolution_combo) {
        state->inject_settings.shadow_resolution =
            ShadowResolutionFromComboSelection(state->shadow_resolution_combo);
    }
    state->inject_settings.ui_texture_filter =
        SendMessageW(state->ui_filter_check, BM_GETCHECK, 0, 0) == BST_CHECKED
        ? pal4::inject::UiTextureFilter::nearest
        : pal4::inject::UiTextureFilter::linear;
    state->inject_settings.system_font_oversample_enabled =
        SendMessageW(state->system_font_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state->inject_settings.dialog_font_hd_enabled =
        SendMessageW(state->dialog_font_check, BM_GETCHECK, 0, 0) == BST_CHECKED;

    SavePersistedLauncherInjectSettings(state->inject_settings);
    return true;
}

pal4::inject::LaunchOptions BuildGuiLaunchOptions(const GuiLaunchState& state) {
    pal4::inject::LaunchOptions options;
    options.executable_path = state.game_exe;
    options.dll_path = state.runtime_dll;
    options.script_mode = state.script_mode;
    return options;
}

struct CloseRequestContext {
    DWORD process_id = 0;
};

BOOL CALLBACK PostCloseToProcessWindowProc(HWND hwnd, LPARAM lparam) {
    auto* context = reinterpret_cast<CloseRequestContext*>(lparam);
    if (!context) {
        return TRUE;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == context->process_id && GetWindow(hwnd, GW_OWNER) == nullptr) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

bool StopManagedGame(HWND hwnd, GuiLaunchState* state) {
    if (!state || !state->managed_process.process_info.hProcess) {
        RefreshLauncherStatus(hwnd, state);
        return true;
    }
    if (!IsManagedProcessRunning(*state)) {
        ReleaseManagedProcess(state);
        RefreshLauncherStatus(hwnd, state);
        return true;
    }

    CloseRequestContext context{state->last_process_id};
    EnumWindows(&PostCloseToProcessWindowProc, reinterpret_cast<LPARAM>(&context));
    const DWORD wait_rc = WaitForSingleObject(state->managed_process.process_info.hProcess, 2000);
    if (wait_rc == WAIT_TIMEOUT) {
        if (!TerminateProcess(state->managed_process.process_info.hProcess, 0)) {
            ShowGuiError(L"\u505c\u6b62\u6e38\u620f\u5931\u8d25\u3002");
            return false;
        }
        WaitForSingleObject(state->managed_process.process_info.hProcess, 3000);
    }

    ReleaseManagedProcess(state);
    RefreshLauncherStatus(hwnd, state);
    return true;
}

bool LaunchManagedGame(HWND hwnd, GuiLaunchState* state, const bool restart_existing) {
    if (!state) {
        return false;
    }

    std::wstring save_error;
    if (!SaveLauncherSelections(state, &save_error)) {
        ShowGuiError(save_error);
        return false;
    }

    if (restart_existing && !StopManagedGame(hwnd, state)) {
        return false;
    }
    if (!restart_existing && IsManagedProcessRunning(*state)) {
        RefreshLauncherStatus(hwnd, state);
        MinimizeLauncherWindow(hwnd);
        return true;
    }

    pal4::inject::InjectedProcess process{};
    const auto result = pal4::inject::LaunchInjectedProcess(BuildGuiLaunchOptions(*state), &process);
    if (!result.ok) {
        ShowGuiError(WideFromUtf8(result.error));
        process.Close();
        return false;
    }

    state->managed_process.Close();
    state->managed_process = process;
    state->last_process_id = result.process_id;
    RefreshLauncherStatus(hwnd, state);
    MinimizeLauncherWindow(hwnd);
    return true;
}

void BuildStartupPage(const HWND panel, GuiLaunchState* state, const HFONT default_font) {
    CreateLabel(
        panel,
        L"\u8fd9\u4e00\u9875\u51b3\u5b9a\u6e38\u620f\u5728\u8fdb\u7a0b\u6062\u590d\u4e4b\u524d\u8981\u7528\u54ea\u4e00\u5957\u811a\u672c\u548c\u542f\u52a8\u7b56\u7565\u3002\u8fd9\u7c7b\u9009\u9879\u90fd\u5e94\u5728\u8fdb\u6e38\u620f\u524d\u5b9a\u597d\u3002",
        16,
        16,
        620,
        40,
        default_font);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"\u811a\u672c\u8bfb\u53d6\u65b9\u5f0f",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        16,
        66,
        652,
        110,
        panel,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    const HWND cs_radio = CreateWindowExW(
        0,
        L"BUTTON",
        L"CS \u6587\u672c\u811a\u672c\uff1a\u9002\u5408\u4fee\u5267\u60c5\u3001\u5bf9\u8bdd\u548c\u8fd0\u884c\u5feb\u901f A/B",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        32,
        96,
        604,
        22,
        panel,
        reinterpret_cast<HMENU>(kRadioCsId),
        GetModuleHandleW(nullptr),
        nullptr);
    const HWND csb_radio = CreateWindowExW(
        0,
        L"BUTTON",
        L"CSB \u539f\u7248\u7f16\u8bd1\u811a\u672c\uff1a\u9002\u5408\u666e\u901a\u6e38\u73a9\u3001\u56de\u5f52\u6d4b\u8bd5\u548c\u5bf9\u7167\u539f\u7248",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        32,
        126,
        604,
        22,
        panel,
        reinterpret_cast<HMENU>(kRadioCsbId),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(cs_radio, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageW(csb_radio, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageW(
        state->script_mode == pal4::inject::ScriptMode::cs ? cs_radio : csb_radio,
        BM_SETCHECK,
        BST_CHECKED,
        0);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"\u542f\u52a8\u5668\u4f7f\u7528\u65b9\u5f0f",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        16,
        188,
        652,
        126,
        panel,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    CreateReadOnlyNote(
        panel,
        L"- \u542f\u52a8\u6e38\u620f\uff1a\u5148\u4fdd\u5b58\u5f53\u524d\u8bbe\u7f6e\uff0c\u518d\u7528\u8fd9\u4e9b\u8bbe\u7f6e\u6ce8\u5165\u5e76\u542f\u52a8 PAL4\u3002\r\n"
        L"- \u505c\u6b62\u6e38\u620f\uff1a\u4f18\u5148\u5c1d\u8bd5\u6e29\u548c\u5173\u95ed\uff0c\u4e0d\u884c\u518d\u5f3a\u5236\u7ed3\u675f\u3002\r\n"
        L"- \u91cd\u542f\u6e38\u620f\uff1a\u505c\u6389\u5f53\u524d\u8fd9\u4e00\u5c40\uff0c\u7136\u540e\u7528\u6700\u65b0\u542f\u52a8\u524d\u8bbe\u5b9a\u518d\u8d77\u4e00\u5c40\u3002\r\n"
        L"- \u5173\u95ed\u7a97\u53e3\u6216\u70b9\u201c\u6700\u5c0f\u5316\u201d\u90fd\u4e0d\u4f1a\u9000\u51fa\u542f\u52a8\u5668\uff0c\u53ea\u4f1a\u6536\u5230\u4efb\u52a1\u680f\u3002",
        32,
        216,
        620,
        80);
}

void BuildDisplayPage(const HWND panel, GuiLaunchState* state, const HFONT default_font) {
    CreateLabel(
        panel,
        L"\u628a\u5206\u8fa8\u7387\u3001\u5bbd\u5c4f\u3001\u7a97\u53e3/\u5168\u5c4f\u3001\u5782\u76f4\u540c\u6b65\u90fd\u653e\u5728\u540c\u4e00\u9875\uff0c\u907f\u514d\u8fdb\u6e38\u620f\u540e\u624d\u53d1\u73b0\u753b\u9762\u57fa\u7ebf\u4e0d\u5bf9\u3002",
        16,
        16,
        620,
        38,
        default_font);

    state->resolution_tab = CreateWindowExW(
        0,
        WC_TABCONTROLW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        16,
        62,
        328,
        170,
        panel,
        reinterpret_cast<HMENU>(kResolutionTabId),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(state->resolution_tab, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    InsertResolutionTab(state->resolution_tab, 0, L"\u5e38\u7528\u5206\u8fa8\u7387");
    InsertResolutionTab(state->resolution_tab, 1, L"\u663e\u793a\u5668\u652f\u6301");

    state->common_list = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
        28,
        94,
        304,
        122,
        panel,
        reinterpret_cast<HMENU>(kCommonResolutionListId),
        GetModuleHandleW(nullptr),
        nullptr);
    state->display_list = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | LBS_NOTIFY | WS_VSCROLL,
        28,
        94,
        304,
        122,
        panel,
        reinterpret_cast<HMENU>(kDisplayResolutionListId),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(state->common_list, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageW(state->display_list, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    PopulateResolutionList(state->common_list, state->common_resolutions, state->config);
    PopulateResolutionList(state->display_list, state->display_resolutions, state->config);
    ShowResolutionTab(state, 0);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"\u663e\u793a\u65b9\u5f0f",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        360,
        62,
        308,
        170,
        panel,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    CreateLabel(panel, L"\u5bbd", 378, 92, 22, 20, default_font);
    state->width_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        std::to_wstring(state->config.width).c_str(),
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
        408,
        89,
        72,
        24,
        panel,
        reinterpret_cast<HMENU>(kWidthEditId),
        GetModuleHandleW(nullptr),
        nullptr);
    CreateLabel(panel, L"\u9ad8", 492, 92, 22, 20, default_font);
    state->height_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        std::to_wstring(state->config.height).c_str(),
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
        522,
        89,
        72,
        24,
        panel,
        reinterpret_cast<HMENU>(kHeightEditId),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(state->width_edit, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageW(state->height_edit, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);

    state->windowed_radio = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u7a97\u53e3\u5316\uff1a\u4fbf\u4e8e\u5207\u6362\u548c\u8c03\u8bd5",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        378,
        126,
        222,
        22,
        panel,
        reinterpret_cast<HMENU>(kWindowedRadioId),
        GetModuleHandleW(nullptr),
        nullptr);
    state->fullscreen_radio = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u5168\u5c4f\uff1a\u9002\u5408\u6b63\u5e38\u6e38\u73a9\u548c\u6027\u80fd\u5bf9\u7167",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        378,
        152,
        238,
        22,
        panel,
        reinterpret_cast<HMENU>(kFullscreenRadioId),
        GetModuleHandleW(nullptr),
        nullptr);
    state->widescreen_check = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u5bbd\u5c4f UI \u5c45\u4e2d\uff1a\u907f\u514d 4:3 \u83dc\u5355\u88ab\u6a2a\u5411\u62c9\u5bbd",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        378,
        180,
        252,
        22,
        panel,
        reinterpret_cast<HMENU>(kWidescreenCheckId),
        GetModuleHandleW(nullptr),
        nullptr);
    state->vsync_check = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u5782\u76f4\u540c\u6b65\uff1a\u51cf\u5c11\u753b\u9762\u64d5\u88c2",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        378,
        206,
        210,
        22,
        panel,
        reinterpret_cast<HMENU>(kVsyncCheckId),
        GetModuleHandleW(nullptr),
        nullptr);
    for (const HWND control : {state->windowed_radio, state->fullscreen_radio, state->widescreen_check, state->vsync_check}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    }
    SendMessageW(
        state->config.fullscreen ? state->fullscreen_radio : state->windowed_radio,
        BM_SETCHECK,
        BST_CHECKED,
        0);
    SendMessageW(state->widescreen_check, BM_SETCHECK, state->config.widescreen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(state->vsync_check, BM_SETCHECK, state->config.sync ? BST_CHECKED : BST_UNCHECKED, 0);
}

void BuildGraphicsPage(const HWND panel, GuiLaunchState* state, const HFONT default_font) {
    CreateLabel(
        panel,
        L"\u753b\u9762\u3001\u9634\u5f71\u3001UI \u91c7\u6837\u548c\u5b57\u4f53\u8fd9\u7c7b\u9009\u9879\u57fa\u672c\u90fd\u9700\u8981\u5728\u8fdb\u6e38\u620f\u524d\u786e\u5b9a\uff0c\u6240\u4ee5\u7edf\u4e00\u653e\u5728\u542f\u52a8\u5668\u91cc\u8bbe\u7f6e\u3002",
        16,
        16,
        620,
        38,
        default_font);

    CreateWindowExW(
        0,
        L"BUTTON",
        L"\u753b\u9762\u4e0e\u6587\u5b57\u589e\u5f3a",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        16,
        62,
        652,
        248,
        panel,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    CreateLabel(panel, L"\u6297\u952f\u9f7f", 34, 96, 90, 20, default_font);
    state->msaa_combo = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        138,
        93,
        96,
        220,
        panel,
        reinterpret_cast<HMENU>(kMsaaComboId),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(state->msaa_combo, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    PopulateMsaaCombo(state->msaa_combo, state->inject_settings.msaa_level);
    CreateLabel(panel, L"\u76ee\u7684\uff1a\u51cf\u5c11\u6a21\u578b\u3001\u5706\u8fb9\u548c\u7ebf\u6761\u7684\u952f\u9f7f", 252, 96, 386, 20, default_font);

    CreateLabel(panel, L"\u4eba\u7269\u9634\u5f71", 34, 132, 90, 20, default_font);
    state->shadow_resolution_combo = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        138,
        129,
        110,
        220,
        panel,
        reinterpret_cast<HMENU>(kShadowResolutionComboId),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(state->shadow_resolution_combo, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    PopulateShadowResolutionCombo(state->shadow_resolution_combo, state->inject_settings.shadow_resolution);
    CreateLabel(panel, L"\u76ee\u7684\uff1a\u8ba9 *_shadow.dff \u7684\u8f6e\u5ed3\u66f4\u6e05\u6670\uff0c\u4e0d\u518d\u7cca\u6210\u4e00\u56e2", 252, 132, 386, 20, default_font);

    state->ui_filter_check = CreateWindowExW(
        0,
        L"BUTTON",
        L"UI \u50cf\u7d20\u91c7\u6837\uff1a\u8ba9\u90e8\u5206 UI \u7eb9\u7406\u4fdd\u6301\u66f4\u786c\u7684\u50cf\u7d20\u611f",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        34,
        170,
        560,
        22,
        panel,
        reinterpret_cast<HMENU>(kUiTextureFilterCheckId),
        GetModuleHandleW(nullptr),
        nullptr);
    state->system_font_check = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u7cfb\u7edf\u5b57\u4f53\u9ad8\u6e05\u5b9e\u9a8c\uff1a\u63d0\u5347 menu / HUD \u7528\u7684 system \u548c systemBold \u6e05\u6670\u5ea6",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        34,
        202,
        590,
        22,
        panel,
        reinterpret_cast<HMENU>(kSystemFontOversampleCheckId),
        GetModuleHandleW(nullptr),
        nullptr);
    state->dialog_font_check = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u5bf9\u767d\u5b57\u4f53\u9ad8\u6e05\u5b9e\u9a8c\uff1a\u53ea\u9488\u5bf9 dialog_simsun\uff0c\u9002\u5408\u505a\u5bf9\u767d\u70b9\u51fb / \u6392\u7248 A/B",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        34,
        234,
        610,
        22,
        panel,
        reinterpret_cast<HMENU>(kDialogFontHdCheckId),
        GetModuleHandleW(nullptr),
        nullptr);
    for (const HWND control : {state->ui_filter_check, state->system_font_check, state->dialog_font_check}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    }
    SendMessageW(
        state->ui_filter_check,
        BM_SETCHECK,
        state->inject_settings.ui_texture_filter == pal4::inject::UiTextureFilter::nearest
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);
    SendMessageW(
        state->system_font_check,
        BM_SETCHECK,
        state->inject_settings.system_font_oversample_enabled ? BST_CHECKED : BST_UNCHECKED,
        0);
    SendMessageW(
        state->dialog_font_check,
        BM_SETCHECK,
        state->inject_settings.dialog_font_hd_enabled ? BST_CHECKED : BST_UNCHECKED,
        0);

    CreateReadOnlyNote(
        panel,
        L"\u8bf4\u660e\uff1a\u8fd9\u4e9b\u9009\u9879\u4f1a\u5199\u5165 pal4_inject\\\\inject_panel_settings.ini\u3002\u9ad8\u6e05\u6587\u5b57\u7c7b\u8bbe\u7f6e\u5efa\u8bae\u91cd\u542f\u6e38\u620f\u540e\u518d\u770b\u6548\u679c\uff0c\u907f\u514d\u5c06\u8fd0\u884c\u4e2d\u70ed\u5207\u8bef\u5224\u6210\u771f\u5b9e\u6548\u679c\u3002",
        34,
        270,
        610,
        36);
}

LRESULT CALLBACK LauncherPagePanelProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
    case WM_NOTIFY: {
        const HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root && root != hwnd) {
            return SendMessageW(root, message, wparam, lparam);
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void BuildLauncherPanels(const HWND hwnd, GuiLaunchState* state, const HFONT default_font) {
    state->launcher_tab = CreateWindowExW(
        0,
        WC_TABCONTROLW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        24,
        92,
        700,
        392,
        hwnd,
        reinterpret_cast<HMENU>(kLauncherTabId),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(state->launcher_tab, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);

    InsertResolutionTab(state->launcher_tab, 0, L"\u542f\u52a8\u524d\u51c6\u5907");
    InsertResolutionTab(state->launcher_tab, 1, L"\u663e\u793a\u4e0e\u5206\u8fa8\u7387");
    InsertResolutionTab(state->launcher_tab, 2, L"\u753b\u9762\u4e0e\u6587\u5b57");

    const RECT page_rect = GetLauncherTabContentRect(state->launcher_tab);
    for (auto& panel : state->page_panels) {
        panel = CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE,
            page_rect.left,
            page_rect.top,
            page_rect.right - page_rect.left,
            page_rect.bottom - page_rect.top,
            state->launcher_tab,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        SetWindowLongPtrW(panel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&LauncherPagePanelProc));
    }

    BuildStartupPage(state->page_panels[LauncherPageIndex(LauncherPage::startup)], state, default_font);
    BuildDisplayPage(state->page_panels[LauncherPageIndex(LauncherPage::display)], state, default_font);
    BuildGraphicsPage(state->page_panels[LauncherPageIndex(LauncherPage::graphics)], state, default_font);
    ShowLauncherPage(state, LauncherPageIndex(LauncherPage::startup));
}

LRESULT CALLBACK LaunchWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<GuiLaunchState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = reinterpret_cast<GuiLaunchState*>(create ? create->lpCreateParams : nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        HFONT default_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT title_font = CreateFontW(
            26,
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS,
            L"Microsoft YaHei UI");
        HFONT link_font = CreateUnderlineFontFrom(default_font);
        SetPropW(hwnd, L"PAL4InjectTitleFont", title_font);
        SetPropW(hwnd, L"PAL4InjectLinkFont", link_font);

        CreateLabel(hwnd, L"PAL4 注入启动器", 24, 18, 260, 34, title_font);
        const std::wstring version_text =
            L"版本 " + WideFromUtf8(pal4::inject::kPal4InjectVersion) + L"  |  作者：";
        CreateLabel(hwnd, version_text, 292, 25, 144, 20, default_font);
        const HWND author_link = CreateWindowExW(
            0,
            L"STATIC",
            L"B站 @北风7P",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            438,
            25,
            94,
            20,
            hwnd,
            reinterpret_cast<HMENU>(kAuthorLinkId),
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(
            author_link,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(link_font ? link_font : default_font),
            TRUE);
        CreateLabel(
            hwnd,
            L"\u628a\u201c\u5fc5\u987b\u8fdb\u6e38\u620f\u524d\u51b3\u5b9a\u201d\u7684\u8bbe\u7f6e\u7edf\u4e00\u653e\u5728\u8fd9\u91cc\uff0c\u6ce8\u5165\u9762\u677f\u53ea\u4fdd\u7559\u771f\u6b63\u9700\u8981\u8fd0\u884c\u65f6\u5207\u6362\u7684\u529f\u80fd\u3002",
            24,
            54,
            690,
            24,
            default_font);

        BuildLauncherPanels(hwnd, state, default_font);

        CreateWindowExW(
            0,
            L"BUTTON",
            L"\u5f53\u524d\u4f1a\u8bdd",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            24,
            498,
            700,
            92,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        state->process_status = CreateLabel(hwnd, L"", 40, 526, 664, 20, default_font);
        CreateLabel(hwnd, L"\u6e38\u620f\u7a0b\u5e8f", 40, 552, 70, 20, default_font);
        CreateLabel(hwnd, ShortenPathForDisplay(state->game_exe), 116, 552, 580, 20, default_font);
        CreateLabel(hwnd, L"\u914d\u7f6e\u6587\u4ef6", 40, 574, 70, 20, default_font);
        CreateLabel(hwnd, ShortenPathForDisplay(state->config_path), 116, 574, 580, 20, default_font);

        state->strategy_summary = CreateReadOnlyNote(hwnd, L"", 24, 600, 474, 88, kStrategySummaryId);

        state->launch_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"\u542f\u52a8\u6e38\u620f",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            514,
            606,
            98,
            30,
            hwnd,
            reinterpret_cast<HMENU>(kLaunchButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        state->stop_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"\u505c\u6b62\u6e38\u620f",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            620,
            606,
            98,
            30,
            hwnd,
            reinterpret_cast<HMENU>(kStopButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        state->restart_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"\u91cd\u542f\u6e38\u620f",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            514,
            646,
            98,
            30,
            hwnd,
            reinterpret_cast<HMENU>(kRestartButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND update_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"\u68c0\u67e5\u66f4\u65b0",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            620,
            646,
            98,
            30,
            hwnd,
            reinterpret_cast<HMENU>(kCheckUpdateButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND minimize_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"\u6700\u5c0f\u5316",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            620,
            682,
            98,
            28,
            hwnd,
            reinterpret_cast<HMENU>(kMinimizeButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        for (const HWND control : {
                 state->launch_button,
                 state->stop_button,
                 state->restart_button,
                 update_button,
                 minimize_button}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        }

        SetTimer(hwnd, kLauncherRefreshTimerId, 800, nullptr);
        StartAutoUpdateCheck(hwnd, state);
        RefreshLauncherStatus(hwnd, state);
        return 0;
    }
    case WM_TIMER:
        if (wparam == kLauncherRefreshTimerId) {
            RefreshLauncherStatus(hwnd, state);
            return 0;
        }
        break;
    case kAutoCheckUpdateMessage: {
        ReleaseInfo* release = reinterpret_cast<ReleaseInfo*>(lparam);
        if (release) {
            ShowReleaseUpdatePrompt(hwnd, *release);
            delete release;
        }
        return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header && header->idFrom == kResolutionTabId && header->code == TCN_SELCHANGE) {
            const int selected = TabCtrl_GetCurSel(state->resolution_tab);
            ShowResolutionTab(state, selected);
            return 0;
        }
        if (header && header->idFrom == kLauncherTabId && header->code == TCN_SELCHANGE) {
            const int selected = TabCtrl_GetCurSel(state->launcher_tab);
            ShowLauncherPage(state, selected);
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        const int control_id = LOWORD(wparam);
        const int notify_code = HIWORD(wparam);
        if (control_id == kAuthorLinkId && notify_code == STN_CLICKED) {
            ShellExecuteW(hwnd, L"open", kAuthorHomepageUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (control_id == kRadioCsId) {
            state->script_mode = pal4::inject::ScriptMode::cs;
            RefreshLauncherStatus(hwnd, state);
            return 0;
        }
        if (control_id == kRadioCsbId) {
            state->script_mode = pal4::inject::ScriptMode::csb;
            RefreshLauncherStatus(hwnd, state);
            return 0;
        }
        if ((control_id == kCommonResolutionListId || control_id == kDisplayResolutionListId) &&
            notify_code == LBN_SELCHANGE) {
            const HWND list = control_id == kCommonResolutionListId ? state->common_list : state->display_list;
            const auto& resolutions = control_id == kCommonResolutionListId
                ? state->common_resolutions
                : state->display_resolutions;
            const int selected = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
            if (selected != LB_ERR) {
                const auto index = static_cast<std::size_t>(SendMessageW(list, LB_GETITEMDATA, selected, 0));
                if (index < resolutions.size()) {
                    ApplyResolutionToState(state, resolutions[index]);
                }
            }
            return 0;
        }
        if (control_id == kCheckUpdateButtonId) {
            CheckForUpdates(hwnd, false);
            return 0;
        }
        if (control_id == kLaunchButtonId && notify_code == BN_CLICKED) {
            LaunchManagedGame(hwnd, state, false);
            return 0;
        }
        if (control_id == kStopButtonId && notify_code == BN_CLICKED) {
            StopManagedGame(hwnd, state);
            return 0;
        }
        if (control_id == kRestartButtonId && notify_code == BN_CLICKED) {
            LaunchManagedGame(hwnd, state, true);
            return 0;
        }
        if (control_id == kMinimizeButtonId && notify_code == BN_CLICKED) {
            MinimizeLauncherWindow(hwnd);
            return 0;
        }
        if (control_id == kMsaaComboId && notify_code == CBN_SELCHANGE) {
            state->inject_settings.msaa_level = MsaaLevelFromComboSelection(state->msaa_combo);
            RefreshLauncherStatus(hwnd, state);
            return 0;
        }
        if (control_id == kShadowResolutionComboId && notify_code == CBN_SELCHANGE) {
            state->inject_settings.shadow_resolution =
                ShadowResolutionFromComboSelection(state->shadow_resolution_combo);
            RefreshLauncherStatus(hwnd, state);
            return 0;
        }
        if ((control_id == kUiTextureFilterCheckId ||
             control_id == kSystemFontOversampleCheckId ||
             control_id == kDialogFontHdCheckId ||
             control_id == kWindowedRadioId ||
             control_id == kFullscreenRadioId ||
             control_id == kWidescreenCheckId ||
             control_id == kVsyncCheckId) &&
            notify_code == BN_CLICKED) {
            RefreshLauncherStatus(hwnd, state);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        MinimizeLauncherWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        KillTimer(hwnd, kLauncherRefreshTimerId);
        ReleaseManagedProcess(state);
        if (const HANDLE title_font = GetPropW(hwnd, L"PAL4InjectTitleFont")) {
            RemovePropW(hwnd, L"PAL4InjectTitleFont");
            DeleteObject(title_font);
        }
        if (const HANDLE link_font = GetPropW(hwnd, L"PAL4InjectLinkFont")) {
            RemovePropW(hwnd, L"PAL4InjectLinkFont");
            DeleteObject(link_font);
        }
        PostQuitMessage(0);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        const HWND child = reinterpret_cast<HWND>(lparam);
        if (child && GetDlgCtrlID(child) == kAuthorLinkId) {
            const HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, RGB(0, 90, 180));
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        break;
    }
    case WM_SETCURSOR: {
        if (reinterpret_cast<HWND>(wparam) == GetDlgItem(hwnd, kAuthorLinkId)) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
            return TRUE;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int RunGuiLauncher() {
    GuiLaunchState state{};
    const auto install_dir = CurrentExecutableDirectory();
    state.game_exe = install_dir / "PAL4.exe";
    state.runtime_dll = install_dir / "pal4_inject" / "runtime.dll";
    state.config_path = install_dir / "config.cfg";
    state.inject_settings = LoadPersistedLauncherInjectSettings();
    state.script_mode = NormalizeLauncherScriptMode(state.inject_settings.launcher_script_mode);
    state.config = LoadGameConfig(state.config_path);
    state.common_resolutions = BuildCommonResolutions();
    state.display_resolutions = EnumeratePrimaryDisplayResolutions();
    const Resolution current_resolution{state.config.width, state.config.height};
    if (std::find(state.common_resolutions.begin(), state.common_resolutions.end(), current_resolution) ==
        state.common_resolutions.end()) {
        state.common_resolutions.push_back(current_resolution);
        std::sort(state.common_resolutions.begin(), state.common_resolutions.end());
    }
    if (std::find(state.display_resolutions.begin(), state.display_resolutions.end(), current_resolution) ==
        state.display_resolutions.end()) {
        state.display_resolutions.push_back(current_resolution);
        std::sort(state.display_resolutions.begin(), state.display_resolutions.end());
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &LaunchWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"PAL4InjectLauncherWindow";
    window_class.hCursor = LoadCursorW(
        nullptr,
        reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32512)));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&window_class);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&controls);

    const int width = 764;
    const int height = 770;
    const int screen_width = GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYSCREEN);
    const HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        window_class.lpszClassName,
        L"PAL4 注入启动器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (screen_width - width) / 2,
        (screen_height - height) / 2,
        width,
        height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (!hwnd) {
        ShowGuiError(L"\u542f\u52a8\u5668\u7a97\u53e3\u521b\u5efa\u5931\u8d25\u3002");
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    pal4::inject::LaunchOptions options;
    options.dll_path = DefaultRuntimeDllPath();
    const bool gui_mode = argc == 1;

    if (gui_mode) {
        return RunGuiLauncher();
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--game-root" && i + 1 < argc) {
            options.game_root = argv[++i];
        } else if (arg == "--exe" && i + 1 < argc) {
            options.executable_path = argv[++i];
        } else if (arg == "--dll" && i + 1 < argc) {
            options.dll_path = argv[++i];
        } else if (arg == "--script-mode" && i + 1 < argc) {
            if (!pal4::inject::TryParseScriptMode(argv[++i], &options.script_mode) ||
                options.script_mode == pal4::inject::ScriptMode::inherit) {
                std::cerr << "invalid --script-mode, expected cs or csb\n";
                return 1;
            }
        } else if (arg == "--ready-timeout-ms" && i + 1 < argc) {
            options.ready_timeout_ms = static_cast<DWORD>(std::stoul(argv[++i]));
        } else if (arg == "--no-resume") {
            options.resume_after_ready = false;
        } else if (arg == "--arg" && i + 1 < argc) {
            options.child_args.push_back(argv[++i]);
        } else {
            PrintUsage();
            return 1;
        }
    }

    if (options.game_root.empty() && options.executable_path.empty()) {
        PrintUsage();
        return 1;
    }

    pal4::inject::InjectedProcess process{};
    const auto result = pal4::inject::LaunchInjectedProcess(options, &process);
    if (!result.ok) {
        if (gui_mode) {
            ShowGuiError(std::wstring(result.error.begin(), result.error.end()));
        }
        std::cerr << result.error << "\n";
        return 1;
    }

    std::cout
        << "ok pid=" << result.process_id
        << " pipe=" << result.pipe_name
        << " ready_event=" << result.ready_event_name
        << " script_mode=" << pal4::inject::ToString(result.script_mode)
        << " resumed=" << (options.resume_after_ready ? 1 : 0)
        << "\n";

    process.Close();
    return 0;
}

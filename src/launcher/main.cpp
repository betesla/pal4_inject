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
constexpr int kExitButtonId = 1004;
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

struct GuiLaunchState {
    std::filesystem::path game_exe;
    std::filesystem::path runtime_dll;
    std::filesystem::path config_path;
    pal4::inject::ScriptMode script_mode = pal4::inject::ScriptMode::csb;
    GameConfig config;
    std::vector<Resolution> common_resolutions;
    std::vector<Resolution> display_resolutions;
    HWND common_list = nullptr;
    HWND display_list = nullptr;
    HWND width_edit = nullptr;
    HWND height_edit = nullptr;
    HWND resolution_tab = nullptr;
    bool auto_update_check_started = false;
    bool accepted = false;
};

struct ReleaseInfo {
    std::string tag_name;
    std::wstring html_url;
    std::string body;
    std::wstring source_name;
};

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
    out->html_url = WideFromUtf8(ExtractJsonStringField(payload, "html_url").value_or(""));
    // Do not fall back to a generic "url" field: Gitee may expose API/user URLs there.
    if (out->html_url.empty()) {
        out->html_url = fallback_page_url;
    }
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
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        SetPropW(hwnd, L"PAL4InjectTitleFont", title_font);
        SetPropW(hwnd, L"PAL4InjectLinkFont", link_font);

        CreateLabel(hwnd, L"PAL4 注入启动器", 24, 20, 360, 34, title_font);
        const std::wstring version_text =
            L"版本 " + WideFromUtf8(pal4::inject::kPal4InjectVersion) + L"  |  作者：";
        CreateLabel(hwnd, version_text, 354, 27, 144, 20, default_font);
        const HWND author_link = CreateWindowExW(
            0,
            L"STATIC",
            L"B站 @北风7P",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            500,
            27,
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
            L"请选择脚本模式后启动游戏。启动后可在游戏内按 Ctrl+J 显示或隐藏注入面板。",
            26,
            58,
            560,
            22,
            default_font);

        CreateWindowExW(
            0,
            L"BUTTON",
            L"脚本模式",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            24,
            96,
            548,
            92,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

        const HWND cs_radio = CreateWindowExW(
            0,
            L"BUTTON",
            L"CS 脚本：用于读取 .cs 文本脚本，适合脚本调试和快速迭代",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            44,
            122,
            500,
            22,
            hwnd,
            reinterpret_cast<HMENU>(kRadioCsId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND csb_radio = CreateWindowExW(
            0,
            L"BUTTON",
            L"CSB 脚本：使用游戏原始编译脚本，适合普通运行和验证",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            44,
            150,
            500,
            22,
            hwnd,
            reinterpret_cast<HMENU>(kRadioCsbId),
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(cs_radio, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(csb_radio, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(csb_radio, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindowExW(
            0,
            WC_TABCONTROLW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            24,
            204,
            300,
            148,
            hwnd,
            reinterpret_cast<HMENU>(kResolutionTabId),
            GetModuleHandleW(nullptr),
            nullptr);
        state->resolution_tab = GetDlgItem(hwnd, kResolutionTabId);
        SendMessageW(state->resolution_tab, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        InsertResolutionTab(state->resolution_tab, 0, L"常用分辨率");
        InsertResolutionTab(state->resolution_tab, 1, L"主显示器支持");

        state->common_list = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
            36,
            234,
            276,
            104,
            hwnd,
            reinterpret_cast<HMENU>(kCommonResolutionListId),
            GetModuleHandleW(nullptr),
            nullptr);
        state->display_list = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            L"",
            WS_CHILD | LBS_NOTIFY | WS_VSCROLL,
            36,
            234,
            276,
            104,
            hwnd,
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
            L"显示设置",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            340,
            204,
            232,
            148,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        CreateLabel(hwnd, L"宽", 358, 232, 22, 20, default_font);
        state->width_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            std::to_wstring(state->config.width).c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            386,
            229,
            62,
            24,
            hwnd,
            reinterpret_cast<HMENU>(kWidthEditId),
            GetModuleHandleW(nullptr),
            nullptr);
        CreateLabel(hwnd, L"高", 458, 232, 22, 20, default_font);
        state->height_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            std::to_wstring(state->config.height).c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            486,
            229,
            62,
            24,
            hwnd,
            reinterpret_cast<HMENU>(kHeightEditId),
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(state->width_edit, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(state->height_edit, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);

        const HWND windowed_radio = CreateWindowExW(
            0,
            L"BUTTON",
            L"窗口化",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            358,
            264,
            82,
            22,
            hwnd,
            reinterpret_cast<HMENU>(kWindowedRadioId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND fullscreen_radio = CreateWindowExW(
            0,
            L"BUTTON",
            L"全屏",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            450,
            264,
            82,
            22,
            hwnd,
            reinterpret_cast<HMENU>(kFullscreenRadioId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND widescreen_check = CreateWindowExW(
            0,
            L"BUTTON",
            L"启用宽屏",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            358,
            294,
            92,
            22,
            hwnd,
            reinterpret_cast<HMENU>(kWidescreenCheckId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND vsync_check = CreateWindowExW(
            0,
            L"BUTTON",
            L"垂直同步",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            458,
            294,
            92,
            22,
            hwnd,
            reinterpret_cast<HMENU>(kVsyncCheckId),
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(windowed_radio, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(fullscreen_radio, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(widescreen_check, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(vsync_check, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(
            state->config.fullscreen ? fullscreen_radio : windowed_radio,
            BM_SETCHECK,
            BST_CHECKED,
            0);
        SendMessageW(widescreen_check, BM_SETCHECK, state->config.widescreen ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(vsync_check, BM_SETCHECK, state->config.sync ? BST_CHECKED : BST_UNCHECKED, 0);

        CreateLabel(hwnd, L"游戏程序", 28, 366, 80, 20, default_font);
        CreateLabel(hwnd, ShortenPathForDisplay(state->game_exe), 108, 366, 456, 20, default_font);
        CreateLabel(hwnd, L"配置文件", 28, 392, 80, 20, default_font);
        CreateLabel(hwnd, ShortenPathForDisplay(state->config_path), 108, 392, 456, 20, default_font);
        CreateLabel(
            hwnd,
            L"发布安装：把 PAL4_inject.exe 和 pal4_inject 文件夹复制到 PAL4.exe 所在目录。",
            28,
            422,
            540,
            20,
            default_font);

        const HWND launch_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"启动游戏",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            316,
            456,
            110,
            32,
            hwnd,
            reinterpret_cast<HMENU>(kLaunchButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND update_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"检查更新",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            436,
            456,
            90,
            32,
            hwnd,
            reinterpret_cast<HMENU>(kCheckUpdateButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND exit_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"退出",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            536,
            456,
            54,
            32,
            hwnd,
            reinterpret_cast<HMENU>(kExitButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(launch_button, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(update_button, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(exit_button, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        StartAutoUpdateCheck(hwnd, state);
        return 0;
    }
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
            return 0;
        }
        if (control_id == kRadioCsbId) {
            state->script_mode = pal4::inject::ScriptMode::csb;
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
        if (control_id == kLaunchButtonId) {
            if (!std::filesystem::exists(state->game_exe)) {
                ShowGuiError(L"没有找到 PAL4.exe。\n\n请把 PAL4_inject.exe 放到 PAL4.exe 所在目录。");
                return 0;
            }
            if (!std::filesystem::exists(state->runtime_dll)) {
                ShowGuiError(L"没有找到 pal4_inject\\runtime.dll。\n\n请确认 pal4_inject 文件夹已复制完整。");
                return 0;
            }
            state->config.width = GetIntFromEdit(state->width_edit, state->config.width);
            state->config.height = GetIntFromEdit(state->height_edit, state->config.height);
            state->config.fullscreen = SendMessageW(GetDlgItem(hwnd, kFullscreenRadioId), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
            state->config.widescreen = SendMessageW(GetDlgItem(hwnd, kWidescreenCheckId), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
            state->config.sync = SendMessageW(GetDlgItem(hwnd, kVsyncCheckId), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
            std::wstring save_error;
            if (!SaveGameConfig(state->config_path, state->config, &save_error)) {
                ShowGuiError(save_error);
                return 0;
            }
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (control_id == kExitButtonId) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
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

bool ConfigureGuiLaunch(pal4::inject::LaunchOptions* options) {
    if (!options) {
        return false;
    }

    GuiLaunchState state{};
    const auto install_dir = CurrentExecutableDirectory();
    state.game_exe = install_dir / "PAL4.exe";
    state.runtime_dll = install_dir / "pal4_inject" / "runtime.dll";
    state.config_path = install_dir / "config.cfg";
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

    const int width = 620;
    const int height = 550;
    const int screen_width = GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYSCREEN);
    const HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        window_class.lpszClassName,
        L"PAL4 注入启动器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (screen_width - width) / 2,
        (screen_height - height) / 2,
        width,
        height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (!hwnd) {
        ShowGuiError(L"启动器窗口创建失败。");
        return false;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (!state.accepted) {
        return false;
    }

    options->executable_path = state.game_exe;
    options->dll_path = state.runtime_dll;
    options->script_mode = state.script_mode;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    pal4::inject::LaunchOptions options;
    options.dll_path = DefaultRuntimeDllPath();
    const bool gui_mode = argc == 1;

    if (gui_mode && !ConfigureGuiLaunch(&options)) {
        return 1;
    }

    if (!gui_mode) {
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

#include "inject_control_window.h"

#include <array>
#include <string>
#include <string_view>
#include <cwchar>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#endif

#include "d3d9_quality_hooks.h"
#include "pal4inject/inject_control_panel.h"
#include "pal4inject/script_mode_override.h"
#include "pal4inject_build_info.h"
#include "runtime_preferences.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

constexpr wchar_t kWindowClassName[] = L"PAL4InjectControlPanel";
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr int kPanelWidth = 1080;
constexpr int kPanelHeight = 620;
constexpr int kWindowMargin = 12;
constexpr int kFooterHeight = 54;
constexpr int kPageMargin = 14;
constexpr int kSummaryLabelWidth = 90;
constexpr int kNameWidth = 230;
constexpr int kToggleWidth = 72;
constexpr int kComboWidth = 210;
constexpr int kStatusWidth = 490;
constexpr int kRowHeight = 28;
constexpr int kSectionHeight = 22;
constexpr int kPageTitleHeight = 22;
constexpr int kPageNoteHeight = 36;
constexpr int kColumnHeaderHeight = 20;
constexpr int kHookToggleBaseId = 1000;
constexpr int kHookComboBaseId = 2000;
constexpr int kMsaaComboId = 3000;
constexpr int kMsaaStatusId = 3001;
constexpr int kOverviewStatusId = 3002;
constexpr int kScriptModeStatusId = 3003;
constexpr int kFooterLabelId = 4000;
constexpr int kShutdownButtonId = 4001;
constexpr int kTabControlId = 4100;
constexpr int kToggleHotkeyId = 1;
constexpr UINT kForceCloseMessage = WM_APP + 1;

struct PanelRowRuntime {
    InjectControlPanelRow row{};
    HWND label = nullptr;
    HWND toggle = nullptr;
    HWND combo = nullptr;
    HWND status = nullptr;
};

using PageArray = std::array<InjectControlPanelPage, 5>;

constexpr PageArray kPageOrder{
    InjectControlPanelPage::overview,
    InjectControlPanelPage::input_ui,
    InjectControlPanelPage::script_text,
    InjectControlPanelPage::render_visual,
    InjectControlPanelPage::camera,
};

HANDLE g_control_thread = nullptr;
DWORD g_control_thread_id = 0;
HWND g_control_hwnd = nullptr;
HWND g_owner_game_hwnd = nullptr;
HWND g_tab_hwnd = nullptr;
std::array<HWND, kPageOrder.size()> g_page_panels{};
HWND g_overview_status = nullptr;
HWND g_script_mode_status = nullptr;
HWND g_msaa_combo = nullptr;
HWND g_msaa_status = nullptr;
bool g_follow_game_window = true;

int PageIndex(const InjectControlPanelPage page) noexcept {
    return static_cast<int>(page);
}

void ApplyFont(HWND hwnd);

std::wstring WideFromNarrow(const std::string_view text) {
    if (text.empty()) {
        return {};
    }

    auto convert = [text](const UINT code_page, const DWORD flags) -> std::wstring {
        const int required = MultiByteToWideChar(
            code_page,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0);
        if (required <= 0) {
            return {};
        }

        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        const int converted = MultiByteToWideChar(
            code_page,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            static_cast<int>(wide.size()));
        if (converted <= 0) {
            return {};
        }
        return wide;
    };

    std::wstring wide = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!wide.empty()) {
        return wide;
    }
    wide = convert(CP_ACP, 0);
    if (!wide.empty()) {
        return wide;
    }

    std::wstring fallback;
    fallback.reserve(text.size());
    for (const unsigned char ch : text) {
        fallback.push_back(static_cast<wchar_t>(ch));
    }
    return fallback;
}

std::wstring WideFromUnsigned(const std::uint64_t value) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%llu", static_cast<unsigned long long>(value));
    return buffer;
}

std::wstring BuildHookStateLabel(const HookStatus& status) {
    if (!status.installed) {
        return L"\u672a\u5b89\u88c5";
    }
    if (status.mode == HookMode::observe_only) {
        return L"\u4ec5\u89c2\u5bdf";
    }
    return L"\u5df2\u542f\u7528";
}

std::wstring BuildReadyLabel(const bool ready) {
    return ready ? L"\u5c31\u7eea" : L"\u672a\u5c31\u7eea";
}

std::wstring BuildSwitchLabel(const bool enabled) {
    return enabled ? L"\u5df2\u5f00\u542f" : L"\u672a\u5f00\u542f";
}

std::wstring BuildScriptModeLabel(const ScriptMode mode) {
    switch (mode) {
    case ScriptMode::inherit:
        return L"\u6cbf\u7528\u539f\u59cb\u503c";
    case ScriptMode::cs:
        return L"CS \u6587\u672c";
    case ScriptMode::csb:
        return L"CSB \u5b57\u8282\u7801";
    }
    return L"\u672a\u77e5";
}

std::wstring DescribeMsaaTypeWide(const std::uint32_t type) {
    switch (type) {
    case 0:
    case 1:
        return L"\u5173";
    case 2:
        return L"2x";
    case 4:
        return L"4x";
    case 8:
        return L"8x";
    default:
        return WideFromUnsigned(type) + L"x";
    }
}

std::wstring BuildPanelOverviewText() {
    auto& state = GetRuntimeState();
    std::wstring text = L"\u5f15\u5bfc=";
    text += BuildReadyLabel(state.BootstrapReady());
    text += L" | Hook=";
    text += BuildReadyLabel(state.HooksReady());
    text += L" | \u901a\u4fe1=";
    text += BuildReadyLabel(state.PipeReady());
    text += L" | \u5d29\u6e83\u6355\u83b7=";
    text += BuildSwitchLabel(state.CrashHandlerReady());
    return text;
}

std::wstring BuildPanelScriptModeText() {
    auto& state = GetRuntimeState();
    std::string error;
    const auto requested_mode = LoadInheritedScriptModeOverride(&error);

    std::wstring text = L"\u8bf7\u6c42=";
    text += BuildScriptModeLabel(requested_mode.value_or(ScriptMode::inherit));

    std::uint32_t current_flag = 0;
    if (TryReadLocalScriptModeFlag(state.MainModuleBase(), &current_flag)) {
        text += L" | \u5b9e\u9645=";
        text += BuildScriptModeLabel(ScriptModeFromCsbFlag(current_flag));
        text += L" | flag=";
        text += WideFromUnsigned(current_flag);
    } else {
        text += L" | \u5b9e\u9645=\u8bfb\u53d6\u5931\u8d25";
    }

    if (!error.empty()) {
        text += L" | \u73af\u5883\u9519\u8bef=";
        text += WideFromNarrow(error);
    }
    return text;
}

std::wstring BuildLocalizedHookStatusText(const HookStatus* status) {
    if (!status) {
        return L"\u72b6\u6001\u672a\u77e5";
    }

    std::wstring text = BuildHookStateLabel(*status);
    text += L" | \u8c03\u7528=";
    text += WideFromUnsigned(status->call_count);
    if (!status->last_error.empty()) {
        text += L" | \u9519\u8bef=";
        text += WideFromNarrow(status->last_error);
    }
    return text;
}

std::wstring BuildLocalizedMsaaStatusText(
    const RuntimeState& state,
    const HookStatus* msaa_hook) {
    D3d9MsaaSnapshot snapshot{};
    std::wstring text;
    if (msaa_hook && msaa_hook->mode == HookMode::observe_only) {
        text = L"\u8986\u5199\u5df2\u5173\u95ed | ";
    }

    if (BuildD3d9MsaaSnapshot(&snapshot)) {
        text += L"\u76ee\u6807 ";
        text += WideFromNarrow(ToString(snapshot.desired_level));
        text += L" | \u5b9e\u9645 ";
        text += DescribeMsaaTypeWide(snapshot.applied_type);
        if (snapshot.applied_quality > 0) {
            text += L" q";
            text += WideFromUnsigned(snapshot.applied_quality);
        }
        text += L" | \u4e0a\u9650 ";
        text += DescribeMsaaTypeWide(snapshot.max_supported_type);
    } else {
        text += L"\u76ee\u6807 ";
        text += WideFromNarrow(ToString(state.GetMsaaLevel()));
        text += L" | \u7b49\u5f85 D3D9";
    }

    if (msaa_hook) {
        text += L" | \u8c03\u7528=";
        text += WideFromUnsigned(msaa_hook->call_count);
        if (msaa_hook->call_count == 0) {
            text += L" | \u7b49\u5f85\u4e0b\u6b21 reset";
        }
        if (!msaa_hook->last_error.empty()) {
            text += L" | \u9519\u8bef=";
            text += WideFromNarrow(msaa_hook->last_error);
        }
    }
    return text;
}

std::wstring BuildPageNote(const InjectControlPanelPage page) {
    switch (page) {
    case InjectControlPanelPage::overview:
        return L"\u67e5\u770b\u5f53\u524d\u6ce8\u5165\u72b6\u6001\uff0c\u5e76\u4ece\u4e0a\u65b9\u9875\u7b7e\u5207\u6362\u5230\u5176\u4ed6\u529f\u80fd\u9762\u677f\u3002";
    case InjectControlPanelPage::input_ui:
        return L"\u96c6\u4e2d\u7ba1\u7406\u83dc\u5355\u3001\u952e\u76d8\u3001\u8f93\u5165\u8bbe\u5907\u4e0e\u7126\u70b9\u4fdd\u62a4\u76f8\u5173 Hook\u3002";
    case InjectControlPanelPage::script_text:
        return L"\u67e5\u770b\u5bf9\u767d\u3001\u5b57\u4f53\u540c\u6b65\u4e0e\u5267\u672c\u6267\u884c\u76f8\u5173 Hook\u3002";
    case InjectControlPanelPage::render_visual:
        return L"\u96c6\u4e2d\u7ba1\u7406\u5bbd\u5c4f\u3001HUD\u3001\u6218\u6597 UI \u4e0e MSAA \u76f8\u5173\u529f\u80fd\u3002";
    case InjectControlPanelPage::camera:
        return L"\u5355\u72ec\u7ba1\u7406\u76f8\u673a\u89d2\u5ea6\u3001\u4fef\u4ef0\u4fdd\u62a4\u548c\u89c6\u89d2\u5b89\u5168\u76f8\u5173 Hook\u3002";
    }
    return {};
}

std::string FormatWindowsError(const DWORD code) {
    char* buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string text;
    if (size && buffer) {
        text.assign(buffer, size);
        LocalFree(buffer);
    } else {
        text = "Windows error " + std::to_string(code);
    }
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

std::vector<PanelRowRuntime>& PanelRows() {
    static std::vector<PanelRowRuntime> rows;
    return rows;
}

HWND CreateStaticControl(
    const HWND parent,
    const std::wstring_view text,
    const int x,
    const int y,
    const int width,
    const int height,
    const int control_id = 0,
    const DWORD style = WS_CHILD | WS_VISIBLE) {
    const HWND control = CreateWindowExW(
        0,
        L"STATIC",
        text.empty() ? L"" : text.data(),
        style,
        x,
        y,
        width,
        height,
        parent,
        control_id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
        nullptr,
        nullptr);
    ApplyFont(control);
    return control;
}

HWND CreateButtonControl(
    const HWND parent,
    const std::wstring_view text,
    const int x,
    const int y,
    const int width,
    const int height,
    const int control_id,
    const DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX) {
    const HWND control = CreateWindowExW(
        0,
        L"BUTTON",
        text.data(),
        style,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
        nullptr,
        nullptr);
    ApplyFont(control);
    return control;
}

HWND CreateComboControl(
    const HWND parent,
    const int x,
    const int y,
    const int width,
    const int height,
    const int control_id) {
    const HWND control = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
        nullptr,
        nullptr);
    ApplyFont(control);
    return control;
}

const HookStatus* FindStatus(
    const std::vector<HookStatus>& statuses,
    const HookId id) {
    for (const auto& status : statuses) {
        if (status.id == id) {
            return &status;
        }
    }
    return nullptr;
}

std::array<MsaaLevel, 4> BuildMsaaLevels() {
    return {
        MsaaLevel::off,
        MsaaLevel::x2,
        MsaaLevel::x4,
        MsaaLevel::x8,
    };
}

void PopulateMsaaCombo(const HWND combo) {
    for (const auto level : BuildMsaaLevels()) {
        const auto text = WideFromNarrow(ToString(level));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void PopulateHookModeCombo(const HWND combo) {
    for (const auto mode : BuildInjectControlPanelModes()) {
        const auto text = BuildInjectControlPanelModeLabel(mode);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.data()));
    }
}

int FindMsaaLevelIndex(const MsaaLevel level) noexcept {
    const auto levels = BuildMsaaLevels();
    for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
        if (levels[static_cast<std::size_t>(i)] == level) {
            return i;
        }
    }
    return 0;
}

MsaaLevel MsaaLevelFromIndex(const int index) noexcept {
    const auto levels = BuildMsaaLevels();
    if (index < 0 || index >= static_cast<int>(levels.size())) {
        return MsaaLevel::off;
    }
    return levels[static_cast<std::size_t>(index)];
}

RECT GetPageContentRect() {
    RECT rect{};
    if (!g_tab_hwnd) {
        return rect;
    }
    GetClientRect(g_tab_hwnd, &rect);
    TabCtrl_AdjustRect(g_tab_hwnd, FALSE, &rect);
    return rect;
}

void UpdatePageVisibility() {
    if (!g_tab_hwnd) {
        return;
    }
    const int selected = TabCtrl_GetCurSel(g_tab_hwnd);
    for (std::size_t i = 0; i < g_page_panels.size(); ++i) {
        if (g_page_panels[i]) {
            ShowWindow(g_page_panels[i], static_cast<int>(i) == selected ? SW_SHOW : SW_HIDE);
        }
    }
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id != GetCurrentProcessId()) {
        return TRUE;
    }
    wchar_t class_name[128]{};
    GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)));
    if (std::wstring_view(class_name) == kWindowClassName) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(lparam) = hwnd;
    return FALSE;
}

HWND FindGameWindow() {
    if (g_owner_game_hwnd && IsWindow(g_owner_game_hwnd)) {
        return g_owner_game_hwnd;
    }
    HWND hwnd = nullptr;
    EnumWindows(&EnumWindowsProc, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

void EnsureGameWindowOwner(const HWND hwnd) {
    const HWND game_window = FindGameWindow();
    if (!game_window || game_window == hwnd) {
        return;
    }
    if (g_owner_game_hwnd != game_window) {
        g_owner_game_hwnd = game_window;
        SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(game_window));
    }
}

void ApplyFont(const HWND hwnd) {
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

bool IsComboInteractionActive(const HWND combo) {
    if (!combo || !IsWindow(combo)) {
        return false;
    }
    if (SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != 0) {
        return true;
    }
    return GetFocus() == combo;
}

void RefreshPanelPlacement(const HWND hwnd) {
    if (!g_follow_game_window) {
        return;
    }

    EnsureGameWindowOwner(hwnd);
    const HWND game_window = FindGameWindow();
    if (!game_window) {
        return;
    }

    RECT game_rect{};
    if (!GetWindowRect(game_window, &game_rect)) {
        return;
    }

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        game_rect.right - kPanelWidth - kWindowMargin,
        game_rect.top + 40,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void UpdateHookRowDisplay(
    const PanelRowRuntime& runtime,
    const HookStatus* status) {
    const bool installed = status && status->installed;
    const bool allow_toggle = runtime.row.allow_mode_change && installed;
    EnableWindow(runtime.toggle, allow_toggle);

    const bool combo_interaction_active = IsComboInteractionActive(runtime.combo);
    if (!combo_interaction_active) {
        EnableWindow(runtime.combo, allow_toggle);
    }

    if (!status) {
        SendMessageW(runtime.toggle, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(runtime.status, L"\u72b6\u6001\u672a\u77e5");
        return;
    }

    SendMessageW(
        runtime.toggle,
        BM_SETCHECK,
        status->mode == HookMode::observe_only ? BST_UNCHECKED : BST_CHECKED,
        0);

    if (!combo_interaction_active) {
        const int expected_index = FindInjectControlPanelModeIndex(status->mode);
        const int current_index = static_cast<int>(
            SendMessageW(runtime.combo, CB_GETCURSEL, 0, 0));
        if (current_index != expected_index) {
            SendMessageW(runtime.combo, CB_SETCURSEL, expected_index, 0);
        }
    }

    SetWindowTextW(runtime.status, BuildLocalizedHookStatusText(status).c_str());
}

void RefreshPanelContent(const HWND hwnd) {
    auto& state = GetRuntimeState();
    const auto statuses = state.CopyHookStatuses();

    const bool msaa_combo_interaction = IsComboInteractionActive(g_msaa_combo);
    if (g_msaa_combo && !msaa_combo_interaction) {
        const int expected_index = FindMsaaLevelIndex(state.GetMsaaLevel());
        const int current_index = static_cast<int>(
            SendMessageW(g_msaa_combo, CB_GETCURSEL, 0, 0));
        if (current_index != expected_index) {
            SendMessageW(g_msaa_combo, CB_SETCURSEL, expected_index, 0);
        }
    }

    if (g_msaa_status) {
        const auto* msaa_hook = FindStatus(statuses, HookId::d3d9_set_present_parameters);
        SetWindowTextW(g_msaa_status, BuildLocalizedMsaaStatusText(state, msaa_hook).c_str());
    }

    for (const auto& runtime : PanelRows()) {
        UpdateHookRowDisplay(runtime, FindStatus(statuses, runtime.row.id));
    }

    if (g_overview_status) {
        SetWindowTextW(g_overview_status, BuildPanelOverviewText().c_str());
    }
    if (g_script_mode_status) {
        SetWindowTextW(g_script_mode_status, BuildPanelScriptModeText().c_str());
    }

    std::wstring footer =
        L"Ctrl+F10 \u663e\u793a/\u9690\u85cf | \u914d\u7f6e=" + RuntimePreferencesPath().wstring() +
        L" | \u5d29\u6e83\u6355\u83b7=" + BuildSwitchLabel(state.CrashHandlerReady()) +
        L" | build=" + WideFromNarrow(kPal4InjectBuildId);
    if (const HWND footer_label = GetDlgItem(hwnd, kFooterLabelId)) {
        SetWindowTextW(footer_label, footer.c_str());
    }
}

void BuildOverviewPage(const HWND panel) {
    int y = kPageMargin;
    CreateStaticControl(panel, BuildInjectControlPanelPageLabel(InjectControlPanelPage::overview), kPageMargin, y, 140, kPageTitleHeight);
    y += kPageTitleHeight;

    CreateStaticControl(panel, BuildPageNote(InjectControlPanelPage::overview), kPageMargin, y, 860, kPageNoteHeight);
    y += kPageNoteHeight + 8;

    CreateStaticControl(panel, L"\u4e3b\u72b6\u6001", kPageMargin, y + 4, kSummaryLabelWidth, 20);
    g_overview_status = CreateStaticControl(panel, L"", kPageMargin + kSummaryLabelWidth, y + 4, 860, 20, kOverviewStatusId);
    y += kRowHeight;

    CreateStaticControl(panel, L"\u811a\u672c\u6a21\u5f0f", kPageMargin, y + 4, kSummaryLabelWidth, 20);
    g_script_mode_status = CreateStaticControl(panel, L"", kPageMargin + kSummaryLabelWidth, y + 4, 900, 20, kScriptModeStatusId);
    y += kRowHeight + 10;

    CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        kPageMargin,
        y,
        980,
        8,
        panel,
        nullptr,
        nullptr,
        nullptr);
    y += 16;

    CreateStaticControl(
        panel,
        L"\u63d0\u793a\uff1a\u5df2\u6309\u201c\u6982\u89c8 / \u8f93\u5165\u4e0e\u754c\u9762 / \u811a\u672c\u4e0e\u6587\u672c / \u6e32\u67d3\u4e0e\u753b\u9762 / \u76f8\u673a\u201d\u62c6\u5206\u9875\u7b7e\uff0c"
        L"\u8bf7\u4ece\u4e0a\u65b9\u5207\u6362\u3002",
        kPageMargin,
        y,
        920,
        40);
}

int BuildHookPageHeader(
    const HWND panel,
    const InjectControlPanelPage page,
    int y,
    const bool show_msaa_block) {
    CreateStaticControl(panel, BuildInjectControlPanelPageLabel(page), kPageMargin, y, 180, kPageTitleHeight);
    y += kPageTitleHeight;

    CreateStaticControl(panel, BuildPageNote(page), kPageMargin, y, 860, kPageNoteHeight);
    y += kPageNoteHeight + 8;

    if (show_msaa_block) {
        CreateStaticControl(panel, L"\u6297\u952f\u9f7f MSAA", kPageMargin, y + 4, kNameWidth, 20);
        g_msaa_combo = CreateComboControl(
            panel,
            kPageMargin + kNameWidth + kToggleWidth,
            y,
            kComboWidth,
            240,
            kMsaaComboId);
        PopulateMsaaCombo(g_msaa_combo);
        g_msaa_status = CreateStaticControl(
            panel,
            L"",
            kPageMargin + kNameWidth + kToggleWidth + kComboWidth + 10,
            y + 4,
            kStatusWidth,
            20,
            kMsaaStatusId);
        y += kRowHeight + 6;

        CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            kPageMargin,
            y,
            980,
            8,
            panel,
            nullptr,
            nullptr,
            nullptr);
        y += 12;
    }

    CreateStaticControl(panel, L"\u529f\u80fd", kPageMargin, y, kNameWidth, kColumnHeaderHeight);
    CreateStaticControl(panel, L"\u542f\u7528", kPageMargin + kNameWidth, y, kToggleWidth, kColumnHeaderHeight);
    CreateStaticControl(
        panel,
        L"\u6a21\u5f0f",
        kPageMargin + kNameWidth + kToggleWidth,
        y,
        kComboWidth,
        kColumnHeaderHeight);
    CreateStaticControl(
        panel,
        L"\u8fd0\u884c\u72b6\u6001",
        kPageMargin + kNameWidth + kToggleWidth + kComboWidth + 10,
        y,
        kStatusWidth,
        kColumnHeaderHeight);
    return y + kColumnHeaderHeight + 4;
}

void BuildHookRowsForPage(
    const HWND panel,
    const InjectControlPanelPage page,
    int y) {
    const auto panel_rows = BuildInjectControlPanelRows();
    for (const auto& row : panel_rows) {
        if (row.page != page) {
            continue;
        }

        const HWND label = CreateStaticControl(panel, row.label, kPageMargin, y + 5, kNameWidth, 20);
        const int row_index = static_cast<int>(PanelRows().size());
        const HWND toggle = CreateButtonControl(
            panel,
            L"\u542f\u7528",
            kPageMargin + kNameWidth + 8,
            y + 2,
            kToggleWidth,
            20,
            kHookToggleBaseId + row_index);
        const HWND combo = CreateComboControl(
            panel,
            kPageMargin + kNameWidth + kToggleWidth,
            y,
            kComboWidth,
            240,
            kHookComboBaseId + row_index);
        PopulateHookModeCombo(combo);
        const HWND status = CreateStaticControl(
            panel,
            L"",
            kPageMargin + kNameWidth + kToggleWidth + kComboWidth + 10,
            y + 5,
            kStatusWidth,
            20);
        PanelRows().push_back({row, label, toggle, combo, status});
        y += kRowHeight;
    }
}

void BuildHookPage(const HWND panel, const InjectControlPanelPage page) {
    int y = kPageMargin;
    y = BuildHookPageHeader(panel, page, y, page == InjectControlPanelPage::render_visual);
    BuildHookRowsForPage(panel, page, y);
}

void BuildPanelControls(const HWND hwnd) {
    PanelRows().clear();
    g_tab_hwnd = nullptr;
    g_page_panels.fill(nullptr);
    g_overview_status = nullptr;
    g_script_mode_status = nullptr;
    g_msaa_combo = nullptr;
    g_msaa_status = nullptr;

    RECT client_rect{};
    GetClientRect(hwnd, &client_rect);
    const int footer_top = client_rect.bottom - kFooterHeight;

    g_tab_hwnd = CreateWindowExW(
        0,
        WC_TABCONTROLW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
        kWindowMargin,
        kWindowMargin,
        client_rect.right - kWindowMargin * 2,
        footer_top - kWindowMargin * 2,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabControlId)),
        nullptr,
        nullptr);
    ApplyFont(g_tab_hwnd);

    for (int i = 0; i < static_cast<int>(kPageOrder.size()); ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(BuildInjectControlPanelPageLabel(kPageOrder[static_cast<std::size_t>(i)]).data());
        SendMessageW(
            g_tab_hwnd,
            TCM_INSERTITEMW,
            static_cast<WPARAM>(i),
            reinterpret_cast<LPARAM>(&item));
    }

    const RECT page_rect = GetPageContentRect();
    for (const auto page : kPageOrder) {
        const HWND panel = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE,
            page_rect.left,
            page_rect.top,
            page_rect.right - page_rect.left,
            page_rect.bottom - page_rect.top,
            g_tab_hwnd,
            nullptr,
            nullptr,
            nullptr);
        ApplyFont(panel);
        g_page_panels[static_cast<std::size_t>(PageIndex(page))] = panel;
    }

    BuildOverviewPage(g_page_panels[static_cast<std::size_t>(PageIndex(InjectControlPanelPage::overview))]);
    BuildHookPage(g_page_panels[static_cast<std::size_t>(PageIndex(InjectControlPanelPage::input_ui))], InjectControlPanelPage::input_ui);
    BuildHookPage(g_page_panels[static_cast<std::size_t>(PageIndex(InjectControlPanelPage::script_text))], InjectControlPanelPage::script_text);
    BuildHookPage(g_page_panels[static_cast<std::size_t>(PageIndex(InjectControlPanelPage::render_visual))], InjectControlPanelPage::render_visual);
    BuildHookPage(g_page_panels[static_cast<std::size_t>(PageIndex(InjectControlPanelPage::camera))], InjectControlPanelPage::camera);

    CreateStaticControl(
        hwnd,
        L"",
        kWindowMargin,
        footer_top + 6,
        client_rect.right - kWindowMargin * 2 - 150,
        18,
        kFooterLabelId);
    CreateButtonControl(
        hwnd,
        L"\u5173\u95ed\u6ce8\u5165",
        client_rect.right - 132 - kWindowMargin,
        footer_top + 24,
        132,
        24,
        kShutdownButtonId,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON);

    UpdatePageVisibility();
}

LRESULT CALLBACK PanelWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&controls);
        BuildPanelControls(hwnd);
        SetTimer(hwnd, kRefreshTimerId, 300, nullptr);
        RegisterHotKey(hwnd, kToggleHotkeyId, MOD_CONTROL | MOD_NOREPEAT, VK_F10);
        RefreshPanelPlacement(hwnd);
        RefreshPanelContent(hwnd);
        return 0;
    }
    case WM_TIMER:
        if (wparam == kRefreshTimerId) {
            RefreshPanelContent(hwnd);
            RefreshPanelPlacement(hwnd);
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        g_follow_game_window = false;
        return 0;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header && header->idFrom == kTabControlId && header->code == TCN_SELCHANGE) {
            UpdatePageVisibility();
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        const int control_id = LOWORD(wparam);
        const int notify_code = HIWORD(wparam);
        if (control_id == kShutdownButtonId && notify_code == BN_CLICKED) {
            GetRuntimeState().RequestShutdown();
            DestroyWindow(hwnd);
            return 0;
        }
        if (control_id == kMsaaComboId && notify_code == CBN_SELCHANGE) {
            const int selected = static_cast<int>(SendMessageW(g_msaa_combo, CB_GETCURSEL, 0, 0));
            ApplyMsaaPreference(MsaaLevelFromIndex(selected), true, true);
            RefreshPanelContent(hwnd);
            return 0;
        }
        if (control_id >= kHookToggleBaseId &&
            control_id < kHookToggleBaseId + static_cast<int>(PanelRows().size()) &&
            notify_code == BN_CLICKED) {
            const int row_index = control_id - kHookToggleBaseId;
            auto& row = PanelRows()[static_cast<std::size_t>(row_index)];
            const bool enabled =
                SendMessageW(row.toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const HookMode next_mode = enabled
                ? ResolveEnabledHookMode(row.row.id)
                : HookMode::observe_only;
            ApplyHookModePreference(row.row.id, next_mode, true, true);
            RefreshPanelContent(hwnd);
            return 0;
        }
        if (control_id >= kHookComboBaseId &&
            control_id < kHookComboBaseId + static_cast<int>(PanelRows().size()) &&
            notify_code == CBN_SELCHANGE) {
            const int row_index = control_id - kHookComboBaseId;
            auto& row = PanelRows()[static_cast<std::size_t>(row_index)];
            const int selected = static_cast<int>(SendMessageW(row.combo, CB_GETCURSEL, 0, 0));
            ApplyHookModePreference(
                row.row.id,
                InjectControlPanelModeFromIndex(selected),
                true,
                true);
            RefreshPanelContent(hwnd);
            return 0;
        }
        break;
    }
    case WM_HOTKEY:
        if (wparam == kToggleHotkeyId) {
            ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW);
            if (IsWindowVisible(hwnd)) {
                EnsureGameWindowOwner(hwnd);
                RefreshPanelPlacement(hwnd);
            }
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case kForceCloseMessage:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kRefreshTimerId);
        UnregisterHotKey(hwnd, kToggleHotkeyId);
        g_control_hwnd = nullptr;
        g_owner_game_hwnd = nullptr;
        g_tab_hwnd = nullptr;
        g_page_panels.fill(nullptr);
        g_overview_status = nullptr;
        g_script_mode_status = nullptr;
        g_msaa_combo = nullptr;
        g_msaa_status = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

DWORD WINAPI ControlWindowThreadProc(LPVOID) {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &PanelWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(
        nullptr,
        reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32512)));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&window_class);

    std::wstring window_title = L"PAL4 \u6ce8\u5165\u63a7\u5236\u9762\u677f [";
    window_title += WideFromNarrow(kPal4InjectBuildId);
    window_title += L"]";
    g_owner_game_hwnd = FindGameWindow();
    g_control_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName,
        window_title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kPanelWidth,
        kPanelHeight,
        g_owner_game_hwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!g_control_hwnd) {
        return 1;
    }

    ShowWindow(g_control_hwnd, SW_SHOW);
    UpdateWindow(g_control_hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (GetRuntimeState().ShutdownRequested() && g_control_hwnd) {
            DestroyWindow(g_control_hwnd);
        }
    }
    return 0;
}

}  // namespace

bool StartInjectControlWindow(std::string* error) {
    if (g_control_thread) {
        return true;
    }
    g_control_thread = CreateThread(nullptr, 0, &ControlWindowThreadProc, nullptr, 0, &g_control_thread_id);
    if (!g_control_thread) {
        if (error) {
            *error = "CreateThread(control window) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }
    return true;
}

void StopInjectControlWindow() {
    if (g_control_hwnd) {
        PostMessageW(g_control_hwnd, kForceCloseMessage, 0, 0);
    }
    if (g_control_thread && GetThreadId(g_control_thread) != GetCurrentThreadId()) {
        WaitForSingleObject(g_control_thread, 5000);
    }
    if (g_control_thread && GetThreadId(g_control_thread) != GetCurrentThreadId()) {
        CloseHandle(g_control_thread);
    }
    g_control_thread = nullptr;
    g_control_thread_id = 0;
    g_control_hwnd = nullptr;
    g_owner_game_hwnd = nullptr;
    g_tab_hwnd = nullptr;
    g_page_panels.fill(nullptr);
    g_overview_status = nullptr;
    g_script_mode_status = nullptr;
    g_msaa_combo = nullptr;
    g_msaa_status = nullptr;
    g_follow_game_window = true;
}

bool IsInjectControlWindowRelated(const HWND hwnd) {
    if (!hwnd || !g_control_hwnd || !IsWindow(g_control_hwnd) || !IsWindow(hwnd)) {
        return false;
    }
    if (hwnd == g_control_hwnd) {
        return true;
    }
    if (IsChild(g_control_hwnd, hwnd)) {
        return true;
    }
    const HWND root_owner = GetAncestor(hwnd, GA_ROOTOWNER);
    return root_owner == g_control_hwnd;
}

}  // namespace pal4::inject

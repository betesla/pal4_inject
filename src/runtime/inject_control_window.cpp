#include "inject_control_window.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "d3d9_quality_hooks.h"
#include "pal4inject/inject_control_panel.h"
#include "runtime_preferences.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

constexpr char kWindowClassName[] = "PAL4InjectControlPanel";
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr int kPanelWidth = 980;
constexpr int kPanelMargin = 12;
constexpr int kNameWidth = 300;
constexpr int kToggleWidth = 70;
constexpr int kComboWidth = 180;
constexpr int kStatusWidth = 380;
constexpr int kRowHeight = 26;
constexpr int kSectionHeight = 22;
constexpr int kColumnHeaderHeight = 18;
constexpr int kHookToggleBaseId = 1000;
constexpr int kHookComboBaseId = 2000;
constexpr int kMsaaComboId = 3000;
constexpr int kMsaaStatusId = 3001;
constexpr int kFooterLabelId = 4000;
constexpr int kShutdownButtonId = 4001;
constexpr int kToggleHotkeyId = 1;
constexpr UINT kForceCloseMessage = WM_APP + 1;

struct PanelRowRuntime {
    InjectControlPanelRow row{};
    HWND toggle = nullptr;
    HWND combo = nullptr;
    HWND status = nullptr;
};

HANDLE g_control_thread = nullptr;
DWORD g_control_thread_id = 0;
HWND g_control_hwnd = nullptr;
HWND g_owner_game_hwnd = nullptr;
HWND g_msaa_combo = nullptr;
HWND g_msaa_status = nullptr;
bool g_follow_game_window = true;

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
    char class_name[128]{};
    GetClassNameA(hwnd, class_name, static_cast<int>(std::size(class_name)));
    if (std::string_view(class_name) == kWindowClassName) {
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
        SetWindowLongPtrA(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(game_window));
    }
}

void ApplyFont(const HWND hwnd) {
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (font) {
        SendMessageA(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

bool IsComboInteractionActive(const HWND combo) {
    if (!combo || !IsWindow(combo)) {
        return false;
    }
    if (SendMessageA(combo, CB_GETDROPPEDSTATE, 0, 0) != 0) {
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
        game_rect.right - kPanelWidth - kPanelMargin,
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
        SendMessageA(runtime.toggle, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextA(runtime.status, "unavailable");
        return;
    }

    SendMessageA(
        runtime.toggle,
        BM_SETCHECK,
        status->mode == HookMode::observe_only ? BST_UNCHECKED : BST_CHECKED,
        0);

    if (!combo_interaction_active) {
        const int expected_index = FindInjectControlPanelModeIndex(status->mode);
        const int current_index = static_cast<int>(
            SendMessageA(runtime.combo, CB_GETCURSEL, 0, 0));
        if (current_index != expected_index) {
            SendMessageA(runtime.combo, CB_SETCURSEL, expected_index, 0);
        }
    }

    std::string status_text = installed
        ? (status->mode == HookMode::observe_only ? "observe" : "active")
        : "not installed";
    status_text += " | calls=" + std::to_string(status->call_count);
    if (!status->last_error.empty()) {
        status_text += " | err=" + status->last_error;
    }
    SetWindowTextA(runtime.status, status_text.c_str());
}

void RefreshPanelContent(const HWND hwnd) {
    auto& state = GetRuntimeState();
    const auto statuses = state.CopyHookStatuses();

    const bool msaa_combo_interaction = IsComboInteractionActive(g_msaa_combo);
    if (g_msaa_combo && !msaa_combo_interaction) {
        const int expected_index = FindMsaaLevelIndex(state.GetMsaaLevel());
        const int current_index = static_cast<int>(
            SendMessageA(g_msaa_combo, CB_GETCURSEL, 0, 0));
        if (current_index != expected_index) {
            SendMessageA(g_msaa_combo, CB_SETCURSEL, expected_index, 0);
        }
    }

    if (g_msaa_status) {
        const auto* msaa_hook = FindStatus(statuses, HookId::d3d9_set_present_parameters);
        D3d9MsaaSnapshot snapshot{};
        std::string text;
        if (msaa_hook && msaa_hook->mode == HookMode::observe_only) {
            text = "Override off | ";
        }

        if (BuildD3d9MsaaSnapshot(&snapshot)) {
            text += DescribeMsaaState(snapshot);
        } else {
            text += std::string("Desired ") + ToString(state.GetMsaaLevel()) + " | waiting for D3D9";
        }

        if (msaa_hook) {
            text += " | calls=" + std::to_string(msaa_hook->call_count);
            if (msaa_hook->call_count == 0) {
                text += " | waiting for next reset";
            }
            if (!msaa_hook->last_error.empty()) {
                text += " | err=" + msaa_hook->last_error;
            }
        }
        SetWindowTextA(g_msaa_status, text.c_str());
    }

    for (const auto& runtime : PanelRows()) {
        UpdateHookRowDisplay(runtime, FindStatus(statuses, runtime.row.id));
    }

    std::string footer =
        "Ctrl+F10 hide/show | settings=" + RuntimePreferencesPath().string() +
        " | crash=" + std::string(state.CrashHandlerReady() ? "on" : "off");
    if (const HWND footer_label = GetDlgItem(hwnd, kFooterLabelId)) {
        SetWindowTextA(footer_label, footer.c_str());
    }
}

void BuildPanelControls(const HWND hwnd) {
    auto& rows = PanelRows();
    rows.clear();
    g_msaa_combo = nullptr;
    g_msaa_status = nullptr;

    const auto panel_rows = BuildInjectControlPanelRows();
    const auto modes = BuildInjectControlPanelModes();

    int y = kPanelMargin;
    const auto create_static =
        [hwnd](const char* text, const int x, const int y_pos, const int width, const int height) {
            const HWND control = CreateWindowExA(
                0,
                "STATIC",
                text,
                WS_CHILD | WS_VISIBLE,
                x,
                y_pos,
                width,
                height,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            ApplyFont(control);
            return control;
        };

    create_static("Graphics", kPanelMargin, y, 120, kSectionHeight);
    y += kSectionHeight;

    create_static("Render MSAA", kPanelMargin, y + 4, kNameWidth, 18);
    g_msaa_combo = CreateWindowExA(
        0,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        kPanelMargin + kNameWidth + kToggleWidth,
        y,
        kComboWidth,
        180,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMsaaComboId)),
        nullptr,
        nullptr);
    ApplyFont(g_msaa_combo);
    for (const auto level : BuildMsaaLevels()) {
        SendMessageA(
            g_msaa_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(ToString(level)));
    }

    g_msaa_status = CreateWindowExA(
        0,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE,
        kPanelMargin + kNameWidth + kToggleWidth + kComboWidth + 10,
        y + 4,
        kStatusWidth,
        18,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMsaaStatusId)),
        nullptr,
        nullptr);
    ApplyFont(g_msaa_status);

    y += kRowHeight + 8;
    CreateWindowExA(
        0,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        kPanelMargin,
        y,
        kPanelWidth - kPanelMargin * 2,
        8,
        hwnd,
        nullptr,
        nullptr,
        nullptr);
    y += 12;

    create_static("Hooks", kPanelMargin, y, 120, kSectionHeight);
    y += kSectionHeight;

    create_static("Hook", kPanelMargin, y, kNameWidth, kColumnHeaderHeight);
    create_static("On", kPanelMargin + kNameWidth, y, kToggleWidth, kColumnHeaderHeight);
    create_static(
        "Mode",
        kPanelMargin + kNameWidth + kToggleWidth,
        y,
        kComboWidth,
        kColumnHeaderHeight);
    create_static(
        "Status",
        kPanelMargin + kNameWidth + kToggleWidth + kComboWidth + 10,
        y,
        kStatusWidth,
        kColumnHeaderHeight);
    y += kColumnHeaderHeight;

    int row_index = 0;
    for (const auto& row : panel_rows) {
        create_static(
            std::string(row.label).c_str(),
            kPanelMargin,
            y + 4,
            kNameWidth,
            18);

        const HWND toggle = CreateWindowExA(
            0,
            "BUTTON",
            "",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            kPanelMargin + kNameWidth + 18,
            y + 2,
            kToggleWidth,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHookToggleBaseId + row_index)),
            nullptr,
            nullptr);
        ApplyFont(toggle);

        const HWND combo = CreateWindowExA(
            0,
            "COMBOBOX",
            "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
            kPanelMargin + kNameWidth + kToggleWidth,
            y,
            kComboWidth,
            180,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHookComboBaseId + row_index)),
            nullptr,
            nullptr);
        ApplyFont(combo);
        for (const auto mode : modes) {
            SendMessageA(
                combo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(ToString(mode)));
        }

        const HWND status = CreateWindowExA(
            0,
            "STATIC",
            "",
            WS_CHILD | WS_VISIBLE,
            kPanelMargin + kNameWidth + kToggleWidth + kComboWidth + 10,
            y + 4,
            kStatusWidth,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr);
        ApplyFont(status);

        rows.push_back({row, toggle, combo, status});
        y += kRowHeight;
        ++row_index;
    }

    const HWND footer = CreateWindowExA(
        0,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE,
        kPanelMargin,
        y + 4,
        kPanelWidth - 150 - kPanelMargin * 2,
        18,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFooterLabelId)),
        nullptr,
        nullptr);
    ApplyFont(footer);

    const HWND shutdown_button = CreateWindowExA(
        0,
        "BUTTON",
        "Shutdown Inject",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        kPanelWidth - 120 - kPanelMargin,
        y + 24,
        120,
        24,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShutdownButtonId)),
        nullptr,
        nullptr);
    ApplyFont(shutdown_button);
}

LRESULT CALLBACK PanelWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        BuildPanelControls(hwnd);
        SetTimer(hwnd, kRefreshTimerId, 300, nullptr);
        RegisterHotKey(hwnd, kToggleHotkeyId, MOD_CONTROL | MOD_NOREPEAT, VK_F10);
        RefreshPanelPlacement(hwnd);
        RefreshPanelContent(hwnd);
        return 0;
    case WM_TIMER:
        if (wparam == kRefreshTimerId) {
            RefreshPanelContent(hwnd);
            RefreshPanelPlacement(hwnd);
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        g_follow_game_window = false;
        return 0;
    case WM_COMMAND: {
        const int control_id = LOWORD(wparam);
        const int notify_code = HIWORD(wparam);
        if (control_id == kShutdownButtonId && notify_code == BN_CLICKED) {
            GetRuntimeState().RequestShutdown();
            DestroyWindow(hwnd);
            return 0;
        }
        if (control_id == kMsaaComboId && notify_code == CBN_SELCHANGE) {
            const int selected = static_cast<int>(SendMessageA(g_msaa_combo, CB_GETCURSEL, 0, 0));
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
                SendMessageA(row.toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
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
            const int selected = static_cast<int>(SendMessageA(row.combo, CB_GETCURSEL, 0, 0));
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
        g_msaa_combo = nullptr;
        g_msaa_status = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

DWORD WINAPI ControlWindowThreadProc(LPVOID) {
    WNDCLASSA window_class{};
    window_class.lpfnWndProc = &PanelWindowProc;
    window_class.hInstance = GetModuleHandleA(nullptr);
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&window_class);

    const int panel_height =
        kPanelMargin +
        kSectionHeight +
        kRowHeight +
        8 +
        12 +
        kSectionHeight +
        kColumnHeaderHeight +
        static_cast<int>(BuildInjectControlPanelRows().size()) * kRowHeight +
        72;
    g_owner_game_hwnd = FindGameWindow();
    g_control_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName,
        "PAL4 Inject Control",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kPanelWidth,
        panel_height,
        g_owner_game_hwnd,
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr);
    if (!g_control_hwnd) {
        return 1;
    }

    ShowWindow(g_control_hwnd, SW_SHOW);
    UpdateWindow(g_control_hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
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
        PostMessageA(g_control_hwnd, kForceCloseMessage, 0, 0);
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

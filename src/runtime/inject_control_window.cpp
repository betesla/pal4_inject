#include "inject_control_window.h"

#include <string>
#include <string_view>
#include <vector>
#include <iterator>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_renderer_hooks.h"
#include "pal4inject/inject_control_panel.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

constexpr char kWindowClassName[] = "PAL4InjectControlPanel";
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr int kPanelWidth = 520;
constexpr int kPanelMargin = 12;
constexpr int kLabelWidth = 220;
constexpr int kComboWidth = 150;
constexpr int kStatusWidth = 110;
constexpr int kRowHeight = 24;
constexpr int kHeaderHeight = 28;
constexpr int kFooterHeight = 34;
constexpr int kControlBaseId = 1000;
constexpr int kFooterLabelId = 3000;
constexpr int kShutdownButtonId = 3001;
constexpr int kToggleHotkeyId = 1;
constexpr UINT kForceCloseMessage = WM_APP + 1;

struct PanelRowRuntime {
    InjectControlPanelRow row{};
    HWND combo = nullptr;
    HWND status = nullptr;
};

HANDLE g_control_thread = nullptr;
DWORD g_control_thread_id = 0;
HWND g_control_hwnd = nullptr;
HWND g_owner_game_hwnd = nullptr;
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

void ApplyHookModeSideEffect(const HookId id, const HookMode mode) {
    if (id == HookId::cegui_renderer_constructor_2) {
        ApplyCeguiRendererHookMode(mode);
    }
}

void SetHookModeFromUi(const HookId id, const HookMode mode) {
    GetRuntimeState().SetHookMode(id, mode);
    ApplyHookModeSideEffect(id, mode);
    GetRuntimeState().SetLastUiEvent(
        std::string("inject_control:") + ToString(id) + "=" + ToString(mode));
}

void RefreshPanelContent(const HWND hwnd) {
    const auto statuses = GetRuntimeState().CopyHookStatuses();
    for (auto& runtime : PanelRows()) {
        const auto* status = FindStatus(statuses, runtime.row.id);
        const bool installed = status && status->installed;
        EnableWindow(runtime.combo, runtime.row.allow_mode_change && installed);
        if (status) {
            const int expected_index = FindInjectControlPanelModeIndex(status->mode);
            const int current_index = static_cast<int>(
                SendMessageA(runtime.combo, CB_GETCURSEL, 0, 0));
            if (current_index != expected_index) {
                SendMessageA(runtime.combo, CB_SETCURSEL, expected_index, 0);
            }

            std::string status_text = installed ? "installed" : "not installed";
            status_text += " / calls=" + std::to_string(status->call_count);
            SetWindowTextA(runtime.status, status_text.c_str());
        } else {
            SetWindowTextA(runtime.status, "unavailable");
        }
    }

    std::string footer =
        "Ctrl+F10 hide/show panel | crash=" +
        std::string(GetRuntimeState().CrashHandlerReady() ? "on" : "off");
    if (const HWND footer_label = GetDlgItem(hwnd, kFooterLabelId)) {
        SetWindowTextA(footer_label, footer.c_str());
    }
}

void BuildPanelControls(const HWND hwnd) {
    auto& rows = PanelRows();
    rows.clear();
    const auto panel_rows = BuildInjectControlPanelRows();
    const auto modes = BuildInjectControlPanelModes();

    int y = kHeaderHeight;
    int row_index = 0;
    for (const auto& row : panel_rows) {
        CreateWindowExA(
            0,
            "STATIC",
            std::string(row.label).c_str(),
            WS_CHILD | WS_VISIBLE,
            kPanelMargin,
            y + 4,
            kLabelWidth,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr);

        const HWND combo = CreateWindowExA(
            0,
            "COMBOBOX",
            "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
            kPanelMargin + kLabelWidth,
            y,
            kComboWidth,
            180,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlBaseId + row_index)),
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
            kPanelMargin + kLabelWidth + kComboWidth + 10,
            y + 4,
            kStatusWidth,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr);
        ApplyFont(status);

        rows.push_back({row, combo, status});
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
        kLabelWidth + kComboWidth + kStatusWidth,
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
        if (control_id >= kControlBaseId &&
            control_id < kControlBaseId + static_cast<int>(PanelRows().size()) &&
            notify_code == CBN_SELCHANGE) {
            const int row_index = control_id - kControlBaseId;
            auto& row = PanelRows()[static_cast<std::size_t>(row_index)];
            const int selected = static_cast<int>(SendMessageA(row.combo, CB_GETCURSEL, 0, 0));
            SetHookModeFromUi(row.row.id, InjectControlPanelModeFromIndex(selected));
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
        kHeaderHeight +
        static_cast<int>(BuildInjectControlPanelRows().size()) * kRowHeight +
        kFooterHeight + 32;
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
    g_follow_game_window = true;
}

}  // namespace pal4::inject

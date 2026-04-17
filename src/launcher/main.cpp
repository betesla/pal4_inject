#include <filesystem>
#include <iostream>
#include <cstring>
#include <string>

#include "pal4inject/launcher.h"

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

struct GuiLaunchState {
    std::filesystem::path game_exe;
    std::filesystem::path runtime_dll;
    pal4::inject::ScriptMode script_mode = pal4::inject::ScriptMode::cs;
    bool accepted = false;
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

void ShowGuiError(const std::wstring& error) {
    MessageBoxW(
        nullptr,
        error.empty() ? L"启动失败。" : error.c_str(),
        L"PAL4 注入启动器",
        MB_ICONERROR | MB_OK);
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
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        SetPropW(hwnd, L"PAL4InjectTitleFont", title_font);

        CreateLabel(hwnd, L"PAL4 注入启动器", 24, 20, 360, 34, title_font);
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
        SendMessageW(cs_radio, BM_SETCHECK, BST_CHECKED, 0);

        CreateLabel(hwnd, L"游戏程序", 28, 210, 80, 20, default_font);
        CreateLabel(hwnd, ShortenPathForDisplay(state->game_exe), 108, 210, 456, 20, default_font);
        CreateLabel(hwnd, L"注入运行库", 28, 236, 80, 20, default_font);
        CreateLabel(hwnd, ShortenPathForDisplay(state->runtime_dll), 108, 236, 456, 20, default_font);
        CreateLabel(
            hwnd,
            L"发布安装：把 PAL4_inject.exe 和 pal4_inject 文件夹复制到 PAL4.exe 所在目录。",
            28,
            270,
            540,
            20,
            default_font);

        const HWND launch_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"启动游戏",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            336,
            314,
            110,
            32,
            hwnd,
            reinterpret_cast<HMENU>(kLaunchButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        const HWND exit_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"退出",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            462,
            314,
            90,
            32,
            hwnd,
            reinterpret_cast<HMENU>(kExitButtonId),
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(launch_button, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        SendMessageW(exit_button, WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
        return 0;
    }
    case WM_COMMAND: {
        const int control_id = LOWORD(wparam);
        if (control_id == kRadioCsId) {
            state->script_mode = pal4::inject::ScriptMode::cs;
            return 0;
        }
        if (control_id == kRadioCsbId) {
            state->script_mode = pal4::inject::ScriptMode::csb;
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
        PostQuitMessage(0);
        return 0;
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

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &LaunchWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"PAL4InjectLauncherWindow";
    window_class.hCursor = LoadCursorW(
        nullptr,
        reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32512)));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&window_class);

    const int width = 620;
    const int height = 410;
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

    if (gui_mode) {
        const std::wstring message =
            L"游戏已启动。\n\n进程 ID: " + std::to_wstring(result.process_id) +
            L"\n脚本模式: " + std::wstring(
                pal4::inject::ToString(result.script_mode),
                pal4::inject::ToString(result.script_mode) + std::strlen(pal4::inject::ToString(result.script_mode))) +
            L"\n\n游戏内按 Ctrl+J 可以显示或隐藏注入面板。";
        MessageBoxW(nullptr, message.c_str(), L"PAL4 注入启动器", MB_ICONINFORMATION | MB_OK);
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

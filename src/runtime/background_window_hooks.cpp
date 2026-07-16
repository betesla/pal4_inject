#include "background_window_hooks.h"

#include <algorithm>
#include <csignal>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>

#include "pal4inject/launcher.h"
#include "hook_logging.h"
#include "runtime_state.h"
#include "mssrsx_abort_hook.h"
#include "system_message_box_hook.h"

namespace pal4::inject {
namespace {

using SetForegroundWindowFn = BOOL(WINAPI*)(HWND);
using SetFocusFn = HWND(WINAPI*)(HWND);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindowFn = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
using MessageBoxAFn = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using CrtSignalHandler = void(__cdecl*)(int);
using CrtSignalFn = CrtSignalHandler(__cdecl*)(int, CrtSignalHandler);
using CrtSetAbortBehaviorFn = unsigned int(__cdecl*)(unsigned int, unsigned int);
using CrtSetErrorModeFn = int(__cdecl*)(int);
using CrtTerminateFn = void(__cdecl*)();
using CrtPurecallFn = int(__cdecl*)();
using MssrsxAbortFn = void(__cdecl*)();
using MssrsxTerminateFn = void(__cdecl*)();
using RaiseExceptionFn = VOID(WINAPI*)(DWORD, DWORD, DWORD, const ULONG_PTR*);
using CreateWindowExAFn = HWND(WINAPI*)(
    DWORD,
    LPCSTR,
    LPCSTR,
    DWORD,
    int,
    int,
    int,
    int,
    HWND,
    HMENU,
    HINSTANCE,
    LPVOID);

SetForegroundWindowFn g_original_set_foreground_window = nullptr;
SetFocusFn g_original_set_focus = nullptr;
SetWindowPosFn g_original_set_window_pos = nullptr;
MoveWindowFn g_original_move_window = nullptr;
ShowWindowFn g_original_show_window = nullptr;
MessageBoxAFn g_original_message_box_a = nullptr;
GetProcAddressFn g_original_get_proc_address = nullptr;
CrtTerminateFn g_original_cegui_terminate = nullptr;
CrtPurecallFn g_original_cegui_purecall = nullptr;
MssrsxAbortFn g_original_mssrsx_abort = nullptr;
MssrsxTerminateFn g_original_mssrsx_terminate = nullptr;
RaiseExceptionFn g_original_mssrsx_raise_exception = nullptr;
CreateWindowExAFn g_original_create_window_ex_a = nullptr;
HANDLE g_background_window_controller = nullptr;
HWINEVENTHOOK g_background_window_event_hook = nullptr;
bool g_pal4_get_proc_address_hooked = false;
bool g_oiramlook_get_proc_address_hooked = false;
bool g_mss_get_proc_address_hooked = false;
bool g_mss_message_box_hooked = false;
bool g_pal4_cached_message_box_hooked = false;
bool g_oiramlook_cached_message_box_hooked = false;
bool g_mss_cached_message_box_hooked = false;
bool g_mssrsx_abort_hooked = false;
bool g_mssrsx_terminate_hooked = false;
bool g_mssrsx_raise_exception_hooked = false;
PVOID g_process_exception_observer = nullptr;
std::atomic_uint32_t g_process_exception_count{0};

constexpr int kBackgroundWindowX = -32000;
constexpr int kBackgroundWindowY = -32000;
constexpr int kMinimumGameWindowWidth = 640;
constexpr int kMinimumGameWindowHeight = 480;

struct BackgroundWindowScan {
    std::size_t window_count = 0;
    std::size_t corrected_count = 0;
};

void InstallAdditionalDialogHooks();

BOOL CALLBACK KeepProcessWindowOffscreen(const HWND window, const LPARAM parameter) {
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != GetCurrentProcessId() || GetAncestor(window, GA_ROOT) != window) {
        return TRUE;
    }

    RECT rect{};
    if (!GetWindowRect(window, &rect)) {
        return TRUE;
    }

    const bool visible = IsWindowVisible(window) != FALSE;
    const bool is_game_sized =
        rect.right - rect.left >= kMinimumGameWindowWidth &&
        rect.bottom - rect.top >= kMinimumGameWindowHeight;
    // Keep PAL4's render window alive off-screen, and also catch every visible
    // small modal/dialog window. Do not reveal PAL4's many intentionally hidden
    // helper windows.
    if (!visible && !is_game_sized) {
        return TRUE;
    }

    auto* scan = reinterpret_cast<BackgroundWindowScan*>(parameter);
    if (scan) {
        ++scan->window_count;
    }

    const LONG_PTR old_extended_style = GetWindowLongPtrA(window, GWL_EXSTYLE);
    const LONG_PTR new_extended_style =
        (old_extended_style | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
    if (new_extended_style != old_extended_style) {
        SetWindowLongPtrA(window, GWL_EXSTYLE, new_extended_style);
    }

    const bool needs_move =
        rect.left != kBackgroundWindowX || rect.top != kBackgroundWindowY;
    if (needs_move) {
        SetWindowPos(
            window,
            nullptr,
            kBackgroundWindowX,
            kBackgroundWindowY,
            0,
            0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    const bool needs_show = !visible && is_game_sized;
    if (needs_show) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    if (scan && (needs_move || needs_show || new_extended_style != old_extended_style)) {
        ++scan->corrected_count;
    }
    return TRUE;
}

void CALLBACK KeepShownProcessWindowOffscreen(
    HWINEVENTHOOK,
    const DWORD event,
    const HWND window,
    const LONG object_id,
    const LONG child_id,
    DWORD,
    DWORD) {
    if (event != EVENT_OBJECT_SHOW || object_id != OBJID_WINDOW ||
        child_id != CHILDID_SELF) {
        return;
    }
    KeepProcessWindowOffscreen(window, 0);
}

DWORD WINAPI BackgroundWindowControllerThread(LPVOID) {
    bool logged_ready = false;
    ULONGLONG next_dialog_hook_refresh = 0;
    while (!GetRuntimeState().ShutdownRequested()) {
        const ULONGLONG now = GetTickCount64();
        if (now >= next_dialog_hook_refresh) {
            InstallAdditionalDialogHooks();
            next_dialog_hook_refresh = now + 10;
        }
        BackgroundWindowScan scan{};
        EnumWindows(&KeepProcessWindowOffscreen, reinterpret_cast<LPARAM>(&scan));
        if (scan.window_count > 0 && !logged_ready) {
            GetRuntimeState().AppendEventLog(
                "background_window controller=offscreen windows=" +
                std::to_string(scan.window_count) +
                " corrected=" + std::to_string(scan.corrected_count));
            logged_ready = true;
        }
        Sleep(1);
    }
    return 0;
}

bool IsCurrentProcessRootWindow(const HWND window) noexcept {
    if (!window || GetAncestor(window, GA_ROOT) != window) {
        return false;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    return process_id == GetCurrentProcessId();
}

BOOL WINAPI HookSetForegroundWindow(const HWND window) {
    if (IsCurrentProcessRootWindow(window)) {
        GetRuntimeState().AppendEventLog("background_window suppress=SetForegroundWindow");
        return TRUE;
    }
    return g_original_set_foreground_window
        ? g_original_set_foreground_window(window)
        : FALSE;
}

HWND WINAPI HookSetFocus(const HWND window) {
    if (IsCurrentProcessRootWindow(window)) {
        GetRuntimeState().AppendEventLog("background_window suppress=SetFocus");
        return GetFocus();
    }
    return g_original_set_focus ? g_original_set_focus(window) : nullptr;
}

BOOL WINAPI HookSetWindowPos(
    const HWND window,
    const HWND insert_after,
    int x,
    int y,
    const int width,
    const int height,
    UINT flags) {
    if (IsCurrentProcessRootWindow(window)) {
        flags |= SWP_NOACTIVATE;
        flags &= ~SWP_NOMOVE;
        x = kBackgroundWindowX;
        y = kBackgroundWindowY;
    }
    return g_original_set_window_pos
        ? g_original_set_window_pos(window, insert_after, x, y, width, height, flags)
        : FALSE;
}

BOOL WINAPI HookMoveWindow(
    const HWND window,
    int x,
    int y,
    const int width,
    const int height,
    const BOOL repaint) {
    if (IsCurrentProcessRootWindow(window)) {
        x = kBackgroundWindowX;
        y = kBackgroundWindowY;
        GetRuntimeState().AppendEventLog("background_window rewrite=MoveWindowOffscreen");
    }
    return g_original_move_window
        ? g_original_move_window(window, x, y, width, height, repaint)
        : FALSE;
}

BOOL WINAPI HookShowWindow(const HWND window, int command) {
    if (IsCurrentProcessRootWindow(window)) {
        if (command != SW_HIDE) {
            auto move_window = g_original_set_window_pos
                ? g_original_set_window_pos
                : &SetWindowPos;
            move_window(
                window,
                nullptr,
                kBackgroundWindowX,
                kBackgroundWindowY,
                0,
                0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        switch (command) {
        case SW_SHOW:
        case SW_SHOWNORMAL:
        case SW_SHOWDEFAULT:
        case SW_RESTORE:
        case SW_SHOWMINIMIZED:
        case SW_MINIMIZE:
        case SW_SHOWMINNOACTIVE:
        case SW_FORCEMINIMIZE:
            command = SW_SHOWNOACTIVATE;
            GetRuntimeState().AppendEventLog("background_window rewrite=ShowWindowNoActivate");
            break;
        default:
            break;
        }
    }
    return g_original_show_window ? g_original_show_window(window, command) : FALSE;
}

std::string OneLineSnippet(const char* value) {
    if (!value) {
        return {};
    }
    std::string text(value);
    if (text.size() > 160) {
        text.resize(160);
    }
    for (char& character : text) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    return text;
}

void AppendAddressDescription(
    std::ostringstream& out,
    const std::uintptr_t address) {
    out << "0x" << std::hex << std::uppercase << address;
    if (address != 0) {
        HMODULE frame_module = nullptr;
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address),
                &frame_module) &&
            frame_module) {
            char path[MAX_PATH]{};
            GetModuleFileNameA(frame_module, path, MAX_PATH);
            const char* name = std::strrchr(path, '\\');
            name = name ? name + 1 : path;
            out << '(' << name << "+0x"
                << (address - reinterpret_cast<std::uintptr_t>(frame_module))
                << ')';
        }
    }
}

void RecordFailureObservation(
    const std::string_view kind,
    const std::string_view detail) {
    void* frames[32]{};
    const USHORT frame_count = CaptureStackBackTrace(
        0,
        static_cast<DWORD>(std::size(frames)),
        frames,
        nullptr);
    std::ostringstream out;
    out << "process_failure kind=" << kind << " detail=\"" << detail
        << "\" stack=";
    for (USHORT index = 0; index < frame_count; ++index) {
        if (index != 0) {
            out << ',';
        }
        AppendAddressDescription(
            out,
            reinterpret_cast<std::uintptr_t>(frames[index]));
    }
    AppendCriticalHookEventLog(out.str());
}

bool IsReadableRange(const void* const address, const std::size_t size) {
    if (!address || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_end = reinterpret_cast<std::uintptr_t>(
        memory.BaseAddress) + memory.RegionSize;
    return begin <= region_end && size <= region_end - begin;
}

void AppendUtf8CodePoint(std::string& text, const std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        text.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        text.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        text.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0x10FFFF) {
        text.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::string ReadCeguiExceptionMessage(
    const EXCEPTION_RECORD& exception_record) {
    constexpr DWORD kMsvcCppExceptionCode = 0xE06D7363;
    if (exception_record.ExceptionCode != kMsvcCppExceptionCode ||
        exception_record.NumberParameters < 3) {
        return {};
    }
    const auto throw_info = static_cast<std::uintptr_t>(
        exception_record.ExceptionInformation[2]);
    HMODULE throw_module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(throw_info),
            &throw_module) ||
        throw_module != GetModuleHandleA("CEGUIBase.dll")) {
        return {};
    }

    // CEGUI 0.4.1 Exception is a vtable pointer followed by its 0x98-byte
    // CEGUI::String. The string stores UTF-32 inline at +0x18 while capacity
    // is <= 32, otherwise +0x98 contains the heap buffer pointer.
    const auto object = static_cast<std::uintptr_t>(
        exception_record.ExceptionInformation[1]);
    if (!IsReadableRange(reinterpret_cast<const void*>(object), 0x9C)) {
        return {};
    }
    const auto length = *reinterpret_cast<const std::uint32_t*>(object + 4);
    const auto capacity = *reinterpret_cast<const std::uint32_t*>(object + 8);
    if (length > capacity || length > 2048) {
        return {};
    }
    const std::uint32_t* characters = nullptr;
    if (capacity <= 32) {
        characters = reinterpret_cast<const std::uint32_t*>(object + 0x18);
    } else {
        characters = *reinterpret_cast<const std::uint32_t* const*>(
            object + 0x98);
    }
    if (length == 0 ||
        !IsReadableRange(characters, length * sizeof(std::uint32_t))) {
        return {};
    }
    std::string message;
    message.reserve(length);
    for (std::uint32_t index = 0; index < length; ++index) {
        AppendUtf8CodePoint(message, characters[index]);
    }
    return OneLineSnippet(message.c_str());
}

LONG WINAPI ObserveProcessException(EXCEPTION_POINTERS* const exception) {
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const std::uint32_t ordinal =
        g_process_exception_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (ordinal > 64) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::ostringstream detail;
    detail << "ordinal=" << ordinal << " thread=" << GetCurrentThreadId()
           << " code=0x" << std::hex << std::uppercase << code
           << " flags=0x" << exception->ExceptionRecord->ExceptionFlags
           << " address=";
    AppendAddressDescription(
        detail,
        reinterpret_cast<std::uintptr_t>(
            exception->ExceptionRecord->ExceptionAddress));
    const std::string cegui_message =
        ReadCeguiExceptionMessage(*exception->ExceptionRecord);
    if (!cegui_message.empty()) {
        detail << " cegui_message=" << cegui_message;
    }
#if defined(_M_IX86)
    detail << " eip=";
    AppendAddressDescription(detail, exception->ContextRecord->Eip);
    detail << " context_stack=";
    const auto stack_address = static_cast<std::uintptr_t>(
        exception->ContextRecord->Esp);
    MEMORY_BASIC_INFORMATION memory{};
    if (stack_address != 0 &&
        VirtualQuery(
            reinterpret_cast<const void*>(stack_address),
            &memory,
            sizeof(memory)) == sizeof(memory) &&
        memory.State == MEM_COMMIT &&
        (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0) {
        const auto region_end = reinterpret_cast<std::uintptr_t>(
            memory.BaseAddress) + memory.RegionSize;
        const auto available_words =
            (region_end - stack_address) / sizeof(std::uintptr_t);
        const std::size_t word_count =
            (std::min)(static_cast<std::size_t>(available_words), 32u);
        const auto* words = reinterpret_cast<const std::uintptr_t*>(stack_address);
        for (std::size_t index = 0; index < word_count; ++index) {
            if (index != 0) {
                detail << ',';
            }
            AppendAddressDescription(detail, words[index]);
        }
    } else {
        detail << "unavailable";
    }
#endif
    RecordFailureObservation("vectored_exception", detail.str());
    return EXCEPTION_CONTINUE_SEARCH;
}

void __cdecl ObserveMssrsxAbort() {
    std::ostringstream detail;
    detail << "caller=0x" << std::hex << std::uppercase
           << reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    RecordFailureObservation("mssrsx_abort", detail.str());
    if (g_original_mssrsx_abort) {
        g_original_mssrsx_abort();
    }
}

void __cdecl ObserveMssrsxTerminate() {
    std::ostringstream detail;
    detail << "caller=0x" << std::hex << std::uppercase
           << reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    RecordFailureObservation("mssrsx_terminate", detail.str());
    if (g_original_mssrsx_terminate) {
        g_original_mssrsx_terminate();
    }
}

VOID WINAPI ObserveMssrsxRaiseException(
    const DWORD exception_code,
    const DWORD exception_flags,
    const DWORD argument_count,
    const ULONG_PTR* const arguments) {
    std::ostringstream detail;
    detail << "code=0x" << std::hex << std::uppercase << exception_code
           << " flags=0x" << exception_flags << std::dec
           << " arguments=" << argument_count;
    RecordFailureObservation("mssrsx_RaiseException", detail.str());
    if (g_original_mssrsx_raise_exception) {
        g_original_mssrsx_raise_exception(
            exception_code,
            exception_flags,
            argument_count,
            arguments);
    }
}

void __cdecl ObserveAbortSignal(const int signal_number) {
    RecordFailureObservation(
        "SIGABRT",
        "signal=" + std::to_string(signal_number));
}

void InstallCrtAbortObservation() {
    std::signal(SIGABRT, &ObserveAbortSignal);

    const HMODULE msvcrt = GetModuleHandleA("MSVCRT.dll");
    if (!msvcrt) {
        AppendCriticalHookEventLog("process_failure msvcrt_abort_observer=unavailable");
        return;
    }
    const auto signal_fn = reinterpret_cast<CrtSignalFn>(
        GetProcAddress(msvcrt, "signal"));
    if (signal_fn) {
        signal_fn(SIGABRT, &ObserveAbortSignal);
    }
    const auto set_abort_behavior = reinterpret_cast<CrtSetAbortBehaviorFn>(
        GetProcAddress(msvcrt, "_set_abort_behavior"));
    if (set_abort_behavior) {
        // Keep the terminating semantics but prevent a modal CRT dialog from
        // blocking unattended background validation before SIGABRT is logged.
        set_abort_behavior(0, 0x3);
    }
    const auto set_error_mode = reinterpret_cast<CrtSetErrorModeFn>(
        GetProcAddress(msvcrt, "_set_error_mode"));
    if (set_error_mode) {
        set_error_mode(1);  // _OUT_TO_STDERR
    }
    AppendCriticalHookEventLog(
        std::string("process_failure msvcrt_abort_observer=") +
        (signal_fn ? "installed" : "signal_export_missing"));
}

void __cdecl ObserveCeguiTerminate() {
    RecordFailureObservation("CEGUI_MSVCRT_terminate", "terminate()");
    if (g_original_cegui_terminate) {
        g_original_cegui_terminate();
    }
}

int __cdecl ObserveCeguiPurecall() {
    RecordFailureObservation("CEGUI_MSVCRT_purecall", "_purecall()");
    return g_original_cegui_purecall ? g_original_cegui_purecall() : 0;
}

int WINAPI HookMessageBoxA(
    const HWND window,
    const LPCSTR text,
    const LPCSTR caption,
    const UINT type) {
    RecordFailureObservation(
        "MessageBoxA",
        "caption=" + OneLineSnippet(caption) +
            " text=" + OneLineSnippet(text) +
            " suppressed=" +
            (IsBackgroundWindowModeRequested() ? "1" : "0"));
    if (IsBackgroundWindowModeRequested()) {
        return IDOK;
    }
    return g_original_message_box_a
        ? g_original_message_box_a(window, text, caption, type)
        : 0;
}

FARPROC WINAPI HookGetProcAddress(
    const HMODULE module,
    const LPCSTR function_name) {
    if (reinterpret_cast<std::uintptr_t>(function_name) > 0xFFFFU &&
        function_name && std::strcmp(function_name, "MessageBoxA") == 0) {
        AppendCriticalHookEventLog(
            "background_window dynamic_dialog_resolver=MessageBoxA intercepted=1");
        return reinterpret_cast<FARPROC>(&HookMessageBoxA);
    }
    return g_original_get_proc_address
        ? g_original_get_proc_address(module, function_name)
        : nullptr;
}

HWND WINAPI HookCreateWindowExA(
    DWORD extended_style,
    const LPCSTR class_name,
    const LPCSTR window_name,
    const DWORD style,
    int x,
    int y,
    const int width,
    const int height,
    const HWND parent,
    const HMENU menu,
    const HINSTANCE instance,
    const LPVOID parameter) {
    if (!parent) {
        extended_style |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        extended_style &= ~WS_EX_APPWINDOW;
        x = kBackgroundWindowX;
        y = kBackgroundWindowY;
        GetRuntimeState().AppendEventLog(
            "background_window placement=offscreen x=-32000 y=-32000");
    }
    return g_original_create_window_ex_a
        ? g_original_create_window_ex_a(
              extended_style,
              class_name,
              window_name,
              style,
              x,
              y,
              width,
              height,
              parent,
              menu,
              instance,
              parameter)
        : nullptr;
}

bool PatchModuleImport(
    const HMODULE target_module,
    const char* imported_module,
    const char* function_name,
    void* replacement,
    void** original,
    std::string* error) {
    auto* module = reinterpret_cast<unsigned char*>(target_module);
    if (!module) {
        if (error) {
            *error = "target module is unavailable";
        }
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        if (error) {
                *error = "target module has no DOS signature";
        }
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        if (error) {
                *error = "target module has no PE signature";
        }
        return false;
    }
    const auto& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) {
        if (error) {
                *error = "target module has no import directory";
        }
        return false;
    }

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        module + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const char* module_name = reinterpret_cast<const char*>(module + descriptor->Name);
        if (_stricmp(module_name, imported_module) != 0) {
            continue;
        }
        if (descriptor->OriginalFirstThunk == 0) {
            if (error) {
                *error = std::string(imported_module) + " import names are unavailable";
            }
            return false;
        }
        auto* name_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            module + descriptor->OriginalFirstThunk);
        auto* address_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            module + descriptor->FirstThunk);
        for (; name_thunk->u1.AddressOfData != 0; ++name_thunk, ++address_thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(name_thunk->u1.Ordinal)) {
                continue;
            }
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                module + name_thunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), function_name) != 0) {
                continue;
            }
            DWORD old_protect = 0;
            if (!VirtualProtect(
                    &address_thunk->u1.Function,
                    sizeof(address_thunk->u1.Function),
                    PAGE_READWRITE,
                    &old_protect)) {
                if (error) {
                    *error = std::string("VirtualProtect failed for ") + function_name;
                }
                return false;
            }
            *original = reinterpret_cast<void*>(address_thunk->u1.Function);
            address_thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            DWORD discard = 0;
            VirtualProtect(
                &address_thunk->u1.Function,
                sizeof(address_thunk->u1.Function),
                old_protect,
                &discard);
            FlushInstructionCache(
                GetCurrentProcess(),
                &address_thunk->u1.Function,
                sizeof(address_thunk->u1.Function));
            return true;
        }
        break;
    }
    if (error) {
        *error = std::string(imported_module) + " import not found: " + function_name;
    }
    return false;
}

void InstallAdditionalDialogHooks() {
    const auto patch_get_proc_address = [](
        const HMODULE module,
        const char* label,
        bool* installed) {
        if (!module || !installed || *installed) {
            return;
        }
        void* original = nullptr;
        std::string diagnostic_error;
        if (!PatchModuleImport(
                module,
                "KERNEL32.dll",
                "GetProcAddress",
                reinterpret_cast<void*>(&HookGetProcAddress),
                &original,
                &diagnostic_error)) {
            return;
        }
        if (original != reinterpret_cast<void*>(&HookGetProcAddress) &&
            !g_original_get_proc_address) {
            g_original_get_proc_address =
                reinterpret_cast<GetProcAddressFn>(original);
        }
        *installed = true;
        AppendCriticalHookEventLog(
            std::string("background_window dialog_resolver_hook module=") +
            label + " installed=1");
    };

    const auto patch_message_box = [](
        const HMODULE module,
        const char* label,
        bool* installed) {
        if (!module || !installed || *installed) {
            return;
        }
        void* original = nullptr;
        std::string diagnostic_error;
        if (!PatchModuleImport(
                module,
                "USER32.dll",
                "MessageBoxA",
                reinterpret_cast<void*>(&HookMessageBoxA),
                &original,
                &diagnostic_error)) {
            return;
        }
        if (original != reinterpret_cast<void*>(&HookMessageBoxA) &&
            !g_original_message_box_a) {
            g_original_message_box_a = reinterpret_cast<MessageBoxAFn>(original);
        }
        *installed = true;
        AppendCriticalHookEventLog(
            std::string("background_window dialog_hook module=") + label +
            " installed=1");
    };

    const auto patch_cached_message_box = [](
        const HMODULE module,
        const std::uintptr_t relative_address,
        const char* label,
        bool* installed) {
        if (!module || !installed || *installed) {
            return;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            return;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            relative_address + sizeof(void*) > nt->OptionalHeader.SizeOfImage) {
            return;
        }

        auto** slot = reinterpret_cast<void**>(base + relative_address);
        DWORD old_protect = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                PAGE_READWRITE,
                &old_protect)) {
            return;
        }
        if (*slot != reinterpret_cast<void*>(&HookMessageBoxA)) {
            if (*slot && !g_original_message_box_a) {
                g_original_message_box_a =
                    reinterpret_cast<MessageBoxAFn>(*slot);
            }
            *slot = reinterpret_cast<void*>(&HookMessageBoxA);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        }
        DWORD discard = 0;
        VirtualProtect(slot, sizeof(*slot), old_protect, &discard);
        *installed = true;
        AppendCriticalHookEventLog(
            std::string("background_window cached_dialog_hook module=") +
            label + " installed=1");
    };

    patch_get_proc_address(
        GetModuleHandleA(nullptr),
        "PAL4.exe",
        &g_pal4_get_proc_address_hooked);
    patch_get_proc_address(
        GetModuleHandleA("OIRAMLOOK.dll"),
        "OIRAMLOOK.dll",
        &g_oiramlook_get_proc_address_hooked);
    patch_get_proc_address(
        GetModuleHandleA("Mss32.dll"),
        "Mss32.dll",
        &g_mss_get_proc_address_hooked);
    patch_message_box(
        GetModuleHandleA("Mss32.dll"),
        "Mss32.dll",
        &g_mss_message_box_hooked);
    patch_cached_message_box(
        GetModuleHandleA(nullptr),
        0x5505BC,
        "PAL4.exe",
        &g_pal4_cached_message_box_hooked);
    patch_cached_message_box(
        GetModuleHandleA("OIRAMLOOK.dll"),
        0x1F73F0,
        "OIRAMLOOK.dll",
        &g_oiramlook_cached_message_box_hooked);
    patch_cached_message_box(
        GetModuleHandleA("Mss32.dll"),
        0x55F38,
        "Mss32.dll",
        &g_mss_cached_message_box_hooked);

    const HMODULE mssrsx = GetModuleHandleA("mssrsx.m3d");
    if (mssrsx && !g_mssrsx_raise_exception_hooked) {
        void* original = nullptr;
        std::string diagnostic_error;
        if (PatchModuleImport(
                mssrsx,
                "KERNEL32.dll",
                "RaiseException",
                reinterpret_cast<void*>(&ObserveMssrsxRaiseException),
                &original,
                &diagnostic_error)) {
            g_original_mssrsx_raise_exception =
                reinterpret_cast<RaiseExceptionFn>(original);
            g_mssrsx_raise_exception_hooked = true;
            AppendCriticalHookEventLog(
                "process_failure mssrsx_RaiseException_observer=installed");
        }
    }
    if (mssrsx && !g_mssrsx_terminate_hooked) {
        void* trampoline = nullptr;
        std::string diagnostic_error;
        if (InstallMssrsxTerminateHook(
                mssrsx,
                reinterpret_cast<void*>(&ObserveMssrsxTerminate),
                &trampoline,
                &diagnostic_error)) {
            g_original_mssrsx_terminate =
                reinterpret_cast<MssrsxTerminateFn>(trampoline);
            g_mssrsx_terminate_hooked = true;
            AppendCriticalHookEventLog(
                "process_failure mssrsx_terminate_observer=installed");
        }
    }
    if (mssrsx && !g_mssrsx_abort_hooked) {
        void* trampoline = nullptr;
        std::string diagnostic_error;
        if (InstallMssrsxAbortHook(
                mssrsx,
                reinterpret_cast<void*>(&ObserveMssrsxAbort),
                &trampoline,
                &diagnostic_error)) {
            g_original_mssrsx_abort =
                reinterpret_cast<MssrsxAbortFn>(trampoline);
            g_mssrsx_abort_hooked = true;
            AppendCriticalHookEventLog(
                "process_failure mssrsx_abort_observer=installed");
        }
    }
}

}  // namespace

bool IsBackgroundWindowModeRequested() noexcept {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA(
        kInjectedBackgroundWindowEnvVar,
        value,
        static_cast<DWORD>(sizeof(value)));
    return length > 0 && std::strcmp(value, "1") == 0;
}

bool InstallBackgroundWindowHooks(std::string* error) {
    if (!IsBackgroundWindowModeRequested()) {
        if (error) {
            error->clear();
        }
        return true;
    }

    struct HookSpec {
        const char* name;
        void* replacement;
        void** original;
    };
    const HookSpec hooks[] = {
        {"CreateWindowExA", reinterpret_cast<void*>(&HookCreateWindowExA),
         reinterpret_cast<void**>(&g_original_create_window_ex_a)},
        {"SetForegroundWindow", reinterpret_cast<void*>(&HookSetForegroundWindow),
         reinterpret_cast<void**>(&g_original_set_foreground_window)},
        {"SetFocus", reinterpret_cast<void*>(&HookSetFocus),
         reinterpret_cast<void**>(&g_original_set_focus)},
        {"SetWindowPos", reinterpret_cast<void*>(&HookSetWindowPos),
         reinterpret_cast<void**>(&g_original_set_window_pos)},
        {"MoveWindow", reinterpret_cast<void*>(&HookMoveWindow),
         reinterpret_cast<void**>(&g_original_move_window)},
        {"ShowWindow", reinterpret_cast<void*>(&HookShowWindow),
         reinterpret_cast<void**>(&g_original_show_window)},
        {"MessageBoxA", reinterpret_cast<void*>(&HookMessageBoxA),
         reinterpret_cast<void**>(&g_original_message_box_a)},
    };
    for (const auto& hook : hooks) {
        if (!PatchModuleImport(
                GetModuleHandleA(nullptr),
                "USER32.dll",
                hook.name,
                hook.replacement,
                hook.original,
                error)) {
            return false;
        }
    }
    InstallAdditionalDialogHooks();
    void* message_box_trampoline = nullptr;
    if (!InstallSystemMessageBoxHook(
            reinterpret_cast<void*>(&HookMessageBoxA),
            &message_box_trampoline,
            error)) {
        return false;
    }
    g_original_message_box_a =
        reinterpret_cast<MessageBoxAFn>(message_box_trampoline);
    AppendCriticalHookEventLog(
        "background_window system_dialog_hook=user32.MessageBoxA installed=1");
    InstallCrtAbortObservation();
    if (!g_process_exception_observer) {
        g_process_exception_observer =
            AddVectoredExceptionHandler(1, &ObserveProcessException);
        AppendCriticalHookEventLog(
            std::string("process_failure vectored_exception_observer=installed=") +
            (g_process_exception_observer ? "1" : "0"));
    }
    const HMODULE cegui_base = GetModuleHandleA("CEGUIBase.dll");
    if (cegui_base) {
        std::string diagnostic_error;
        const bool terminate_ok = PatchModuleImport(
            cegui_base,
            "MSVCRT.dll",
            "?terminate@@YAXXZ",
            reinterpret_cast<void*>(&ObserveCeguiTerminate),
            reinterpret_cast<void**>(&g_original_cegui_terminate),
            &diagnostic_error);
        if (!terminate_ok) {
            AppendCriticalHookEventLog(
                "process_failure cegui_terminate_observer=failed error=" +
                diagnostic_error);
        }
        diagnostic_error.clear();
        const bool purecall_ok = PatchModuleImport(
            cegui_base,
            "MSVCRT.dll",
            "_purecall",
            reinterpret_cast<void*>(&ObserveCeguiPurecall),
            reinterpret_cast<void**>(&g_original_cegui_purecall),
            &diagnostic_error);
        if (!purecall_ok) {
            AppendCriticalHookEventLog(
                "process_failure cegui_purecall_observer=failed error=" +
                diagnostic_error);
        }
        AppendCriticalHookEventLog(
            std::string("process_failure cegui_crt_observers terminate=") +
            (terminate_ok ? "1" : "0") + " purecall=" +
            (purecall_ok ? "1" : "0"));
    }
    if (!g_background_window_controller) {
        g_background_window_controller = CreateThread(
            nullptr, 0, &BackgroundWindowControllerThread, nullptr, 0, nullptr);
        if (!g_background_window_controller) {
            if (error) {
                *error = "CreateThread failed for background window controller";
            }
            return false;
        }
        CloseHandle(g_background_window_controller);
    }
    if (!g_background_window_event_hook) {
        HMODULE runtime_module = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&KeepShownProcessWindowOffscreen),
            &runtime_module);
        g_background_window_event_hook = SetWinEventHook(
            EVENT_OBJECT_SHOW,
            EVENT_OBJECT_SHOW,
            runtime_module,
            &KeepShownProcessWindowOffscreen,
            GetCurrentProcessId(),
            0,
            WINEVENT_INCONTEXT);
        if (!g_background_window_event_hook) {
            AppendCriticalHookEventLog(
                "background_window event_hook=unavailable fallback=controller");
        }
    }
    GetRuntimeState().AppendEventLog("background_window_hooks installed=1");
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

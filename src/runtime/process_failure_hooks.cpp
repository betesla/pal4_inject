#include "process_failure_hooks.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>

#include "hook_logging.h"
#include "background_window_hooks.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using CrtRuntimeMessageFn = void (__cdecl*)(int);
using CrtMessageBoxFn = int (__cdecl*)(const char*, const char*, unsigned int);

CrtRuntimeMessageFn g_original_crt_runtime_message = nullptr;
CrtMessageBoxFn g_original_crt_message_box = nullptr;

struct MainImageRange {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
};

MainImageRange GetMainImageRange() {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (module == 0) {
        return {};
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return {};
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage == 0) {
        return {};
    }
    return {module, module + nt->OptionalHeader.SizeOfImage};
}

void AppendAddress(std::ostringstream& out, const std::uintptr_t address, const MainImageRange image) {
    out << "0x" << std::hex << std::uppercase << address << std::dec;
    if (address >= image.begin && address < image.end) {
        out << "(PAL4+0x" << std::hex << std::uppercase
            << (address - image.begin) << std::dec << ')';
    }
}

std::string ReadOneLineText(const char* source) {
    if (!source) {
        return {};
    }
    std::array<char, 161> buffer{};
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(
            GetCurrentProcess(),
            source,
            buffer.data(),
            buffer.size() - 1,
            &bytes_read) ||
        bytes_read == 0) {
        return "<unreadable>";
    }
    buffer[std::min<std::size_t>(bytes_read, buffer.size() - 1)] = '\0';
    std::string text(buffer.data());
    for (char& character : text) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    return text;
}

void __cdecl Hook_CrtRuntimeMessage(const int error_number) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::crt_runtime_message);

    const MainImageRange image = GetMainImageRange();
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    std::array<void*, 24> frames{};
    const USHORT frame_count = CaptureStackBackTrace(
        0,
        static_cast<DWORD>(frames.size()),
        frames.data(),
        nullptr);

    std::ostringstream log;
    const bool suppress_dialog = IsBackgroundWindowModeRequested();
    log << "process_failure source=pal4_static_crt"
        << " error_number=" << error_number
        << " error_hex=0x" << std::hex << std::uppercase
        << static_cast<unsigned int>(error_number) << std::dec
        << " dialog_suppressed=" << (suppress_dialog ? 1 : 0)
        << " caller=";
    AppendAddress(log, caller, image);
    log << " stack=";
    for (USHORT index = 0; index < frame_count; ++index) {
        if (index != 0) {
            log << ',';
        }
        AppendAddress(
            log,
            reinterpret_cast<std::uintptr_t>(frames[index]),
            image);
    }
    AppendCriticalHookEventLog(log.str());

    // The original routine only formats and displays the blocking CRT error
    // dialog. Its caller still performs the fatal exit. Background validation
    // keeps that exit behavior while suppressing UI that would interrupt the
    // active desktop.
    if (suppress_dialog) {
        state.ClearHookError(HookId::crt_runtime_message);
        return;
    }

    if (!g_original_crt_runtime_message) {
        state.SetHookError(
            HookId::crt_runtime_message,
            "original crt_runtime_message trampoline is null");
        state.SetLastError("original crt_runtime_message trampoline is null");
        return;
    }
    state.ClearHookError(HookId::crt_runtime_message);
    g_original_crt_runtime_message(error_number);
}

int __cdecl Hook_CrtMessageBox(
    const char* text,
    const char* caption,
    const unsigned int type) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::crt_message_box);

    const MainImageRange image = GetMainImageRange();
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    std::array<void*, 24> frames{};
    const USHORT frame_count = CaptureStackBackTrace(
        0,
        static_cast<DWORD>(frames.size()),
        frames.data(),
        nullptr);
    const bool suppress_dialog = IsBackgroundWindowModeRequested();

    std::ostringstream log;
    log << "process_failure source=pal4_crt_message_box"
        << " dialog_suppressed=" << (suppress_dialog ? 1 : 0)
        << " type=0x" << std::hex << std::uppercase << type << std::dec
        << " caption=\"" << ReadOneLineText(caption) << '\"'
        << " text=\"" << ReadOneLineText(text) << '\"'
        << " caller=";
    AppendAddress(log, caller, image);
    log << " stack=";
    for (USHORT index = 0; index < frame_count; ++index) {
        if (index != 0) {
            log << ',';
        }
        AppendAddress(
            log,
            reinterpret_cast<std::uintptr_t>(frames[index]),
            image);
    }
    AppendCriticalHookEventLog(log.str());

    if (suppress_dialog) {
        state.ClearHookError(HookId::crt_message_box);
        return 0;
    }
    if (!g_original_crt_message_box) {
        state.SetHookError(
            HookId::crt_message_box,
            "original crt_message_box trampoline is null");
        state.SetLastError("original crt_message_box trampoline is null");
        return 0;
    }
    state.ClearHookError(HookId::crt_message_box);
    return g_original_crt_message_box(text, caption, type);
}

}  // namespace

void* GetProcessFailureReplacementForHook(const HookId id) {
    if (id == HookId::crt_runtime_message) {
        return reinterpret_cast<void*>(&Hook_CrtRuntimeMessage);
    }
    if (id == HookId::crt_message_box) {
        return reinterpret_cast<void*>(&Hook_CrtMessageBox);
    }
    return nullptr;
}

void SetProcessFailureOriginalTrampoline(
    const HookId id,
    void* const trampoline) {
    if (id == HookId::crt_runtime_message) {
        g_original_crt_runtime_message =
            reinterpret_cast<CrtRuntimeMessageFn>(trampoline);
    }
    if (id == HookId::crt_message_box) {
        g_original_crt_message_box =
            reinterpret_cast<CrtMessageBoxFn>(trampoline);
    }
}

}  // namespace pal4::inject

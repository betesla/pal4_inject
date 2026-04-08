#include "crash_handler.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DbgHelp.h>

#include "pal4inject/crash_capture.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using MiniDumpWriteDumpFn = BOOL (WINAPI*)(
    HANDLE,
    DWORD,
    HANDLE,
    MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);

PVOID g_vectored_handler = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_unhandled_filter = nullptr;
LONG g_crash_capture_started = 0;

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

std::filesystem::path BuildCrashDirectory() {
    char temp_path[MAX_PATH];
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
        return std::filesystem::current_path();
    }

    std::filesystem::path path(std::string(temp_path, temp_len));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

std::filesystem::path BuildRuntimeLogPath() {
    return BuildCrashDirectory() / "pal4_inject_runtime.log";
}

void AppendRuntimeDebugLog(const std::string_view line) {
    std::ofstream file(BuildRuntimeLogPath(), std::ios::binary | std::ios::app);
    if (!file) {
        return;
    }
    file << line << "\r\n";
}

CrashContextSnapshot BuildCrashContextSnapshot(
    const EXCEPTION_POINTERS* exception_pointers) {
    CrashContextSnapshot snapshot{};
    if (!exception_pointers || !exception_pointers->ExceptionRecord || !exception_pointers->ContextRecord) {
        return snapshot;
    }

    const auto* record = exception_pointers->ExceptionRecord;
    const auto* context = exception_pointers->ContextRecord;
    snapshot.exception_code = record->ExceptionCode;
    snapshot.exception_flags = record->ExceptionFlags;
    snapshot.exception_address = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
    if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
         record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        record->NumberParameters >= 2) {
        snapshot.has_access_address = true;
        snapshot.access_type = static_cast<std::uint32_t>(record->ExceptionInformation[0]);
        snapshot.access_address = static_cast<std::uintptr_t>(record->ExceptionInformation[1]);
    }
    snapshot.eip = context->Eip;
    snapshot.esp = context->Esp;
    snapshot.ebp = context->Ebp;
    snapshot.eax = context->Eax;
    snapshot.ebx = context->Ebx;
    snapshot.ecx = context->Ecx;
    snapshot.edx = context->Edx;
    snapshot.esi = context->Esi;
    snapshot.edi = context->Edi;
    return snapshot;
}

std::string BuildHookSummary(const RuntimeSnapshot& snapshot) {
    std::ostringstream out;
    bool first = true;
    for (const auto& status : snapshot.active_hooks) {
        if (!first) {
            out << '|';
        }
        first = false;
        out
            << ToString(status.id)
            << ",installed=" << (status.installed ? 1 : 0)
            << ",mode=" << ToString(status.mode)
            << ",calls=" << status.call_count;
        if (!status.last_error.empty()) {
            out << ",error=" << status.last_error;
        }
    }
    return out.str();
}

std::string BuildCrashReportText(
    const CrashContextSnapshot& snapshot,
    const std::string_view source,
    const RuntimeSnapshot* runtime_snapshot) {
    std::ostringstream out;
    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    out
        << "timestamp="
        << local_time.wYear << '-'
        << local_time.wMonth << '-'
        << local_time.wDay << ' '
        << local_time.wHour << ':'
        << local_time.wMinute << ':'
        << local_time.wSecond << '.'
        << local_time.wMilliseconds << "\n"
        << "process_id=" << GetCurrentProcessId() << "\n"
        << "thread_id=" << GetCurrentThreadId() << "\n"
        << BuildCrashSummary(snapshot, source) << "\n";

    if (!runtime_snapshot) {
        out << "runtime_snapshot=unavailable\n";
        return out.str();
    }

    out
        << "runtime_snapshot=available\n"
        << "bootstrap_ready=" << (runtime_snapshot->bootstrap_ready ? 1 : 0) << "\n"
        << "hooks_ready=" << (runtime_snapshot->hooks_ready ? 1 : 0) << "\n"
        << "pipe_ready=" << (runtime_snapshot->pipe_ready ? 1 : 0) << "\n"
        << "ui_dispatch_ready=" << (runtime_snapshot->ui_dispatch_ready ? 1 : 0) << "\n"
        << "crash_handler_ready=" << (runtime_snapshot->crash_handler_ready ? 1 : 0) << "\n"
        << "current_paliv_entry=0x" << std::hex << std::uppercase << runtime_snapshot->current_paliv_entry << "\n"
        << "last_paliv_entry_observed=0x" << std::hex << std::uppercase << runtime_snapshot->last_paliv_entry_observed << "\n"
        << "last_ui_event=" << runtime_snapshot->last_ui_event << "\n"
        << "last_error=" << runtime_snapshot->last_error << "\n"
        << "hooks=" << BuildHookSummary(*runtime_snapshot) << "\n"
        << "event_log_tail=\n" << runtime_snapshot->event_log_tail << "\n";
    return out.str();
}

bool TryWriteTextFile(
    const std::filesystem::path& path,
    const std::string_view text,
    std::string* error) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error) {
            *error = "failed to open crash report file";
        }
        return false;
    }
    file << text;
    file.flush();
    if (!file) {
        if (error) {
            *error = "failed to flush crash report file";
        }
        return false;
    }
    return true;
}

bool TryWriteMiniDump(
    const std::filesystem::path& dump_path,
    EXCEPTION_POINTERS* exception_pointers,
    std::string* error) {
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    if (!dbghelp) {
        if (error) {
            *error = "LoadLibraryA(dbghelp.dll) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    const auto mini_dump_write_dump = reinterpret_cast<MiniDumpWriteDumpFn>(
        GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!mini_dump_write_dump) {
        if (error) {
            *error = "GetProcAddress(MiniDumpWriteDump) failed: " + FormatWindowsError(GetLastError());
        }
        FreeLibrary(dbghelp);
        return false;
    }

    HANDLE dump_file = CreateFileA(
        dump_path.string().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (dump_file == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = "CreateFileA(minidump) failed: " + FormatWindowsError(GetLastError());
        }
        FreeLibrary(dbghelp);
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exception_info{};
    exception_info.ThreadId = GetCurrentThreadId();
    exception_info.ExceptionPointers = exception_pointers;
    exception_info.ClientPointers = FALSE;
    const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal |
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpScanMemory |
        MiniDumpWithThreadInfo);

    const BOOL ok = mini_dump_write_dump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        dump_file,
        dump_type,
        &exception_info,
        nullptr,
        nullptr);
    const DWORD last_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(dump_file);
    FreeLibrary(dbghelp);

    if (!ok) {
        if (error) {
            *error = "MiniDumpWriteDump failed: " + FormatWindowsError(last_error);
        }
        return false;
    }
    return true;
}

void CaptureCrashArtifactsImpl(
    const std::string_view source,
    EXCEPTION_POINTERS* exception_pointers) {
    const auto snapshot = BuildCrashContextSnapshot(exception_pointers);
    const auto crash_dir = BuildCrashDirectory();
    const std::string stem = BuildCrashArtifactStem(
        GetCurrentProcessId(),
        GetCurrentThreadId(),
        snapshot.exception_code,
        GetTickCount64());

    RuntimeSnapshot runtime_snapshot{};
    const bool have_runtime_snapshot = GetRuntimeState().TryBuildSnapshot(&runtime_snapshot, 0);
    const std::string report_text = BuildCrashReportText(
        snapshot,
        source,
        have_runtime_snapshot ? &runtime_snapshot : nullptr);
    const std::filesystem::path report_path = crash_dir / (stem + ".txt");
    const std::filesystem::path dump_path = crash_dir / (stem + ".dmp");

    std::string report_error;
    if (!TryWriteTextFile(report_path, report_text, &report_error)) {
        AppendRuntimeDebugLog(std::string("crash_report_write_failed: ") + report_error);
    } else {
        AppendRuntimeDebugLog(std::string("crash_report_written: ") + report_path.string());
    }

    std::string dump_error;
    if (!TryWriteMiniDump(dump_path, exception_pointers, &dump_error)) {
        AppendRuntimeDebugLog(std::string("crash_dump_write_failed: ") + dump_error);
    } else {
        AppendRuntimeDebugLog(std::string("crash_dump_written: ") + dump_path.string());
    }

    AppendRuntimeDebugLog(report_text);
    GetRuntimeState().TrySetCrashArtifacts(
        report_text,
        report_path.string(),
        dump_error.empty() ? dump_path.string() : std::string());
}

void CaptureCrashArtifacts(
    const std::string_view source,
    EXCEPTION_POINTERS* exception_pointers) {
    __try {
        CaptureCrashArtifactsImpl(source, exception_pointers);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        AppendRuntimeDebugLog("crash_capture_internal_failure");
    }
}

LONG CALLBACK VectoredCrashHandler(EXCEPTION_POINTERS* exception_pointers) {
    if (!exception_pointers || !exception_pointers->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const std::uint32_t code = exception_pointers->ExceptionRecord->ExceptionCode;
    if (!IsCrashExceptionCode(code)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (InterlockedCompareExchange(&g_crash_capture_started, 1, 0) == 0) {
        CaptureCrashArtifacts("vectored_exception_handler", exception_pointers);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI UnhandledCrashFilter(EXCEPTION_POINTERS* exception_pointers) {
    if (InterlockedCompareExchange(&g_crash_capture_started, 1, 0) == 0) {
        CaptureCrashArtifacts("unhandled_exception_filter", exception_pointers);
    }
    if (g_previous_unhandled_filter &&
        g_previous_unhandled_filter != &UnhandledCrashFilter) {
        return g_previous_unhandled_filter(exception_pointers);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

bool InstallCrashCapture(std::string* error) {
    if (g_vectored_handler) {
        GetRuntimeState().SetCrashHandlerReady(true);
        return true;
    }

    g_vectored_handler = AddVectoredExceptionHandler(1, &VectoredCrashHandler);
    if (!g_vectored_handler) {
        if (error) {
            *error = "AddVectoredExceptionHandler failed: " + FormatWindowsError(GetLastError());
        }
        GetRuntimeState().SetCrashHandlerReady(false);
        return false;
    }

    g_previous_unhandled_filter = SetUnhandledExceptionFilter(&UnhandledCrashFilter);
    InterlockedExchange(&g_crash_capture_started, 0);
    GetRuntimeState().SetCrashHandlerReady(true);
    AppendRuntimeDebugLog("crash_capture_install ok");
    return true;
}

void UninstallCrashCapture() {
    if (g_vectored_handler) {
        RemoveVectoredExceptionHandler(g_vectored_handler);
        g_vectored_handler = nullptr;
    }
    SetUnhandledExceptionFilter(g_previous_unhandled_filter);
    g_previous_unhandled_filter = nullptr;
    GetRuntimeState().SetCrashHandlerReady(false);
}

}  // namespace pal4::inject

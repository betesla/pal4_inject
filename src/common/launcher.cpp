#include "pal4inject/launcher.h"

#include <filesystem>
#include <sstream>

namespace pal4::inject {
namespace {

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

std::string QuoteArgument(const std::string& value) {
    if (value.find_first_of(" \t\"") == std::string::npos) {
        return value;
    }
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

bool InjectRuntimeDll(
    const HANDLE process,
    const std::filesystem::path& dll_path,
    std::string* error) {
    const std::wstring wide_path = dll_path.wstring();
    const std::size_t byte_size = (wide_path.size() + 1) * sizeof(wchar_t);
    void* remote_buffer = VirtualAllocEx(
        process,
        nullptr,
        byte_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!remote_buffer) {
        if (error) {
            *error = "VirtualAllocEx failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    bool ok = false;
    do {
        if (!WriteProcessMemory(process, remote_buffer, wide_path.c_str(), byte_size, nullptr)) {
            if (error) {
                *error = "WriteProcessMemory failed: " + FormatWindowsError(GetLastError());
            }
            break;
        }

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32) {
            if (error) {
                *error = "GetModuleHandleW(kernel32.dll) failed: " + FormatWindowsError(GetLastError());
            }
            break;
        }
        const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(kernel32, "LoadLibraryW"));
        if (!load_library) {
            if (error) {
                *error = "GetProcAddress(LoadLibraryW) failed: " + FormatWindowsError(GetLastError());
            }
            break;
        }

        HANDLE remote_thread = CreateRemoteThread(
            process,
            nullptr,
            0,
            load_library,
            remote_buffer,
            0,
            nullptr);
        if (!remote_thread) {
            if (error) {
                *error = "CreateRemoteThread failed: " + FormatWindowsError(GetLastError());
            }
            break;
        }

        const DWORD wait_rc = WaitForSingleObject(remote_thread, 15000);
        if (wait_rc != WAIT_OBJECT_0) {
            if (error) {
                *error = "LoadLibraryW remote thread did not finish in time";
            }
            CloseHandle(remote_thread);
            break;
        }

        DWORD exit_code = 0;
        GetExitCodeThread(remote_thread, &exit_code);
        CloseHandle(remote_thread);
        if (exit_code == 0) {
            if (error) {
                *error = "LoadLibraryW returned null";
            }
            break;
        }
        ok = true;
    } while (false);

    VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
    return ok;
}

}  // namespace

void InjectedProcess::Close() noexcept {
    if (process_info.hThread) {
        CloseHandle(process_info.hThread);
        process_info.hThread = nullptr;
    }
    if (process_info.hProcess) {
        CloseHandle(process_info.hProcess);
        process_info.hProcess = nullptr;
    }
    injected = false;
}

std::string BuildReadyEventName(const DWORD process_id) {
    return "Local\\PAL4InjectReady_" + std::to_string(process_id);
}

std::string BuildPipeName(const DWORD process_id) {
    return "\\\\.\\pipe\\pal4_inject_" + std::to_string(process_id);
}

bool WaitForRuntimeReady(
    const std::string_view ready_event_name,
    const DWORD timeout_ms,
    std::string* error) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        HANDLE event_handle = OpenEventA(SYNCHRONIZE, FALSE, std::string(ready_event_name).c_str());
        if (event_handle) {
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining = now >= deadline
                ? 0
                : static_cast<DWORD>(deadline - now);
            const DWORD wait_rc = WaitForSingleObject(event_handle, remaining);
            CloseHandle(event_handle);
            if (wait_rc == WAIT_OBJECT_0) {
                return true;
            }
            if (error) {
                *error = wait_rc == WAIT_TIMEOUT
                    ? "timed out waiting for PAL4 inject ready event"
                    : "WaitForSingleObject failed: " + FormatWindowsError(GetLastError());
            }
            return false;
        }

        if (GetTickCount64() >= deadline) {
            if (error) {
                *error = "ready event not created";
            }
            return false;
        }
        Sleep(50);
    }
}

bool ResumeInjectedProcess(const InjectedProcess& process, std::string* error) {
    if (!process.process_info.hThread) {
        if (error) {
            *error = "process thread handle is null";
        }
        return false;
    }
    if (ResumeThread(process.process_info.hThread) == static_cast<DWORD>(-1)) {
        if (error) {
            *error = "ResumeThread failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }
    return true;
}

LaunchResult LaunchInjectedProcess(const LaunchOptions& options, InjectedProcess* out_process) {
    LaunchResult result{};
    if (!out_process) {
        result.error = "out_process is null";
        return result;
    }

    const std::filesystem::path exe_path = options.game_root / "launch.exe";
    if (!std::filesystem::exists(exe_path)) {
        result.error = "launch.exe not found under game_root";
        return result;
    }
    if (!std::filesystem::exists(options.dll_path)) {
        result.error = "runtime DLL not found";
        return result;
    }

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    std::ostringstream cmdline_stream;
    cmdline_stream << QuoteArgument(exe_path.string());
    for (const auto& arg : options.child_args) {
        cmdline_stream << ' ' << QuoteArgument(arg);
    }
    std::string cmdline = cmdline_stream.str();

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process_info{};
    std::string workdir = options.game_root.string();

    if (!CreateProcessA(
            exe_path.string().c_str(),
            cmdline.data(),
            nullptr,
            nullptr,
            FALSE,
            options.creation_flags,
            nullptr,
            workdir.c_str(),
            &startup,
            &process_info)) {
        result.error = "CreateProcessA failed: " + FormatWindowsError(GetLastError());
        return result;
    }

    InjectedProcess local_process{};
    local_process.process_info = process_info;
    local_process.ready_event_name = BuildReadyEventName(process_info.dwProcessId);
    local_process.pipe_name = BuildPipeName(process_info.dwProcessId);

    if (!InjectRuntimeDll(process_info.hProcess, options.dll_path, &result.error)) {
        local_process.Close();
        return result;
    }

    if (!WaitForRuntimeReady(local_process.ready_event_name, options.ready_timeout_ms, &result.error)) {
        local_process.Close();
        return result;
    }

    local_process.injected = true;
    if (options.resume_after_ready && !ResumeInjectedProcess(local_process, &result.error)) {
        local_process.Close();
        return result;
    }

    result.ok = true;
    result.process_id = process_info.dwProcessId;
    result.ready_event_name = local_process.ready_event_name;
    result.pipe_name = local_process.pipe_name;
    *out_process = local_process;
    return result;
}

bool SendPipeCommand(
    const std::string_view pipe_name,
    const std::string_view request,
    std::string* response,
    const DWORD timeout_ms,
    std::string* error) {
    if (!response) {
        if (error) {
            *error = "response output pointer is null";
        }
        return false;
    }
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    const std::string pipe_name_text(pipe_name);
    for (;;) {
        if (WaitNamedPipeA(pipe_name_text.c_str(), 100)) {
            break;
        }
        const DWORD wait_error = GetLastError();
        if (wait_error != ERROR_FILE_NOT_FOUND && wait_error != ERROR_SEM_TIMEOUT) {
            if (error) {
                *error = "WaitNamedPipeA failed: " + FormatWindowsError(wait_error);
            }
            return false;
        }
        if (GetTickCount64() >= deadline) {
            if (error) {
                *error = "WaitNamedPipeA timed out: " + FormatWindowsError(wait_error);
            }
            return false;
        }
        Sleep(25);
    }

    HANDLE pipe = CreateFileA(
        pipe_name_text.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = "CreateFileA(pipe) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    bool ok = false;
    do {
        DWORD written = 0;
        std::string payload(request);
        if (!WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)) {
            if (error) {
                *error = "WriteFile(pipe) failed: " + FormatWindowsError(GetLastError());
            }
            break;
        }

        response->clear();
        for (;;) {
            char buffer[4096];
            DWORD read = 0;
            if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr)) {
                const DWORD read_error = GetLastError();
                if (read_error == ERROR_MORE_DATA) {
                    response->append(buffer, buffer + read);
                } else {
                    if (error) {
                        *error = "ReadFile(pipe) failed: " + FormatWindowsError(read_error);
                    }
                    break;
                }
            } else {
                response->append(buffer, buffer + read);
            }

            DWORD bytes_available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes_available, nullptr)) {
                if (error) {
                    *error = "PeekNamedPipe failed: " + FormatWindowsError(GetLastError());
                }
                break;
            }
            if (bytes_available == 0) {
                break;
            }
        }
        if (response->empty()) {
            if (error) {
                *error = "ReadFile(pipe) returned an empty response";
            }
            break;
        }
        ok = true;
    } while (false);

    CloseHandle(pipe);
    return ok;
}

}  // namespace pal4::inject

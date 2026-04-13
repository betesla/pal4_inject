#include "pal4inject/launcher.h"

#include <filesystem>
#include <optional>
#include <sstream>
#include <vector>

#include "pal4inject/script_mode_override.h"

namespace pal4::inject {
namespace {

using NtQueryInformationProcessFn = LONG(WINAPI*)(
    HANDLE,
    ULONG,
    PVOID,
    ULONG,
    PULONG);

struct ProcessBasicInformationLayout {
    PVOID reserved1 = nullptr;
    PVOID peb_base_address = nullptr;
    PVOID reserved2[2]{};
    ULONG_PTR unique_process_id = 0;
    PVOID reserved3 = nullptr;
};

struct PebImageBaseLayout {
    BYTE reserved[8]{};
    PVOID image_base_address = nullptr;
};

std::string FormatWindowsError(DWORD code);

std::optional<std::string> ReadEnvironmentVariable(const char* name) {
    const DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::vector<char> buffer(static_cast<std::size_t>(required), '\0');
    const DWORD copied = GetEnvironmentVariableA(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (copied == 0 || copied >= buffer.size()) {
        return std::nullopt;
    }
    return std::string(buffer.data(), copied);
}

class ScopedChildScriptModeEnvironment {
public:
    ScopedChildScriptModeEnvironment(
        const ScriptMode mode,
        std::string* error)
        : previous_value_(ReadEnvironmentVariable(kInjectedScriptModeEnvVar)),
          restore_required_(true) {
        const char* requested_value =
            mode == ScriptMode::inherit ? nullptr : ToString(mode);
        if (!SetEnvironmentVariableA(kInjectedScriptModeEnvVar, requested_value)) {
            restore_required_ = false;
            if (error) {
                *error = std::string("SetEnvironmentVariableA(") +
                    kInjectedScriptModeEnvVar + ") failed: " +
                    FormatWindowsError(GetLastError());
            }
            ok_ = false;
            return;
        }
        ok_ = true;
        if (error) {
            error->clear();
        }
    }

    ScopedChildScriptModeEnvironment(const ScopedChildScriptModeEnvironment&) = delete;
    ScopedChildScriptModeEnvironment& operator=(const ScopedChildScriptModeEnvironment&) = delete;

    ~ScopedChildScriptModeEnvironment() {
        if (!restore_required_) {
            return;
        }
        if (previous_value_.has_value()) {
            SetEnvironmentVariableA(kInjectedScriptModeEnvVar, previous_value_->c_str());
        } else {
            SetEnvironmentVariableA(kInjectedScriptModeEnvVar, nullptr);
        }
    }

    bool ok() const noexcept {
        return ok_;
    }

private:
    std::optional<std::string> previous_value_;
    bool restore_required_ = false;
bool ok_ = false;
};

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

bool QueryRemoteMainModuleBase(
    const HANDLE process,
    std::uintptr_t* module_base,
    std::string* error) {
    if (!module_base) {
        if (error) {
            *error = "remote module base output pointer is null";
        }
        return false;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        if (error) {
            *error = "GetModuleHandleW(ntdll.dll) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    const auto nt_query_information_process =
        reinterpret_cast<NtQueryInformationProcessFn>(
            GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (!nt_query_information_process) {
        if (error) {
            *error = "GetProcAddress(NtQueryInformationProcess) failed: " +
                FormatWindowsError(GetLastError());
        }
        return false;
    }

    ProcessBasicInformationLayout process_info{};
    ULONG return_length = 0;
    const LONG nt_status = nt_query_information_process(
        process,
        0,
        &process_info,
        sizeof(process_info),
        &return_length);
    if (nt_status != 0 || !process_info.peb_base_address) {
        if (error) {
            std::ostringstream out;
            out << "NtQueryInformationProcess failed: status=0x"
                << std::hex << std::uppercase << static_cast<unsigned long>(nt_status);
            *error = out.str();
        }
        return false;
    }

    PebImageBaseLayout peb{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(
            process,
            process_info.peb_base_address,
            &peb,
            sizeof(peb),
            &read) ||
        read != sizeof(peb) ||
        !peb.image_base_address) {
        if (error) {
            *error = "ReadProcessMemory(PEB) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    *module_base = reinterpret_cast<std::uintptr_t>(peb.image_base_address);
    return true;
}

bool WriteRemoteUint32(
    const HANDLE process,
    const std::uintptr_t remote_address,
    const std::uint32_t value,
    std::string* error) {
    SIZE_T written = 0;
    if (!WriteProcessMemory(
            process,
            reinterpret_cast<void*>(remote_address),
            &value,
            sizeof(value),
            &written) ||
        written != sizeof(value)) {
        if (error) {
            *error = "WriteProcessMemory(uint32) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    std::uint32_t observed_value = 0;
    SIZE_T read = 0;
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<void*>(remote_address),
            &observed_value,
            sizeof(observed_value),
            &read) ||
        read != sizeof(observed_value)) {
        if (error) {
            *error = "ReadProcessMemory(uint32 verify) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    if (observed_value != value) {
        if (error) {
            *error =
                "remote uint32 verify mismatch at 0x" +
                [&remote_address]() {
                    std::ostringstream out;
                    out << std::hex << std::uppercase << remote_address;
                    return out.str();
                }();
        }
        return false;
    }
    return true;
}

bool ApplyScriptModeOverride(
    const HANDLE process,
    const ScriptMode script_mode,
    std::string* error) {
    const auto csb_flag = ScriptModeToCsbFlag(script_mode);
    if (!csb_flag.has_value()) {
        if (error) {
            error->clear();
        }
        return true;
    }

    std::uintptr_t module_base = 0;
    if (!QueryRemoteMainModuleBase(process, &module_base, error)) {
        return false;
    }

    const auto remote_address =
        ResolveScriptModeGlobalAddress(module_base);
    return WriteRemoteUint32(process, remote_address, *csb_flag, error);
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

bool ResolveLaunchPaths(
    const LaunchOptions& options,
    std::filesystem::path* exe_path,
    std::filesystem::path* workdir,
    std::string* error) {
    if (!exe_path || !workdir) {
        if (error) {
            *error = "launch path outputs are null";
        }
        return false;
    }

    if (!options.executable_path.empty()) {
        *exe_path = options.executable_path;
        *workdir = options.executable_path.parent_path();
        if (!std::filesystem::exists(*exe_path)) {
            if (error) {
                *error = "target executable does not exist";
            }
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    if (options.game_root.empty()) {
        if (error) {
            *error = "game_root or executable_path is required";
        }
        return false;
    }

    *exe_path = options.game_root / "launch.exe";
    *workdir = options.game_root;
    if (!std::filesystem::exists(*exe_path)) {
        if (error) {
            *error = "launch.exe not found under game_root";
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

LaunchResult LaunchInjectedProcess(const LaunchOptions& options, InjectedProcess* out_process) {
    LaunchResult result{};
    if (!out_process) {
        result.error = "out_process is null";
        return result;
    }

    std::filesystem::path exe_path;
    std::filesystem::path workdir;
    if (!ResolveLaunchPaths(options, &exe_path, &workdir, &result.error)) {
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
    std::string workdir_text = workdir.string();
    ScopedChildScriptModeEnvironment script_mode_env(options.script_mode, &result.error);
    if (!script_mode_env.ok()) {
        return result;
    }

    if (!CreateProcessA(
            exe_path.string().c_str(),
            cmdline.data(),
            nullptr,
            nullptr,
            FALSE,
            options.creation_flags,
            nullptr,
            workdir_text.empty() ? nullptr : workdir_text.c_str(),
            &startup,
            &process_info)) {
        result.error = "CreateProcessA failed: " + FormatWindowsError(GetLastError());
        return result;
    }

    InjectedProcess local_process{};
    local_process.process_info = process_info;
    local_process.ready_event_name = BuildReadyEventName(process_info.dwProcessId);
    local_process.pipe_name = BuildPipeName(process_info.dwProcessId);

    if (!ApplyScriptModeOverride(
            process_info.hProcess,
            options.script_mode,
            &result.error)) {
        local_process.Close();
        return result;
    }

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
    result.script_mode = options.script_mode;
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
    DWORD pipe_mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &pipe_mode, nullptr, nullptr)) {
        if (error) {
            *error = "SetNamedPipeHandleState failed: " + FormatWindowsError(GetLastError());
        }
        CloseHandle(pipe);
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
            char buffer[65536];
            DWORD read = 0;
            if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr)) {
                const DWORD read_error = GetLastError();
                if (read_error == ERROR_MORE_DATA) {
                    response->append(buffer, buffer + read);
                    continue;
                } else if (read_error == ERROR_BROKEN_PIPE) {
                    break;
                } else {
                    if (error) {
                        *error = "ReadFile(pipe) failed: " + FormatWindowsError(read_error);
                    }
                    break;
                }
            } else if (read != 0) {
                response->append(buffer, buffer + read);
            }
            if (read == 0) {
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

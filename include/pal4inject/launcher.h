#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {

struct LaunchOptions {
    std::filesystem::path game_root;
    std::filesystem::path dll_path;
    std::vector<std::string> child_args;
    DWORD creation_flags = CREATE_SUSPENDED;
    DWORD ready_timeout_ms = 15000;
    bool resume_after_ready = true;
};

struct InjectedProcess {
    PROCESS_INFORMATION process_info{};
    std::string ready_event_name;
    std::string pipe_name;
    bool injected = false;

    void Close() noexcept;
};

struct LaunchResult {
    bool ok = false;
    DWORD process_id = 0;
    std::string ready_event_name;
    std::string pipe_name;
    std::string error;
};

std::string BuildReadyEventName(DWORD process_id);
std::string BuildPipeName(DWORD process_id);

bool WaitForRuntimeReady(
    std::string_view ready_event_name,
    DWORD timeout_ms,
    std::string* error);
bool ResumeInjectedProcess(
    const InjectedProcess& process,
    std::string* error);
LaunchResult LaunchInjectedProcess(
    const LaunchOptions& options,
    InjectedProcess* out_process);
bool SendPipeCommand(
    std::string_view pipe_name,
    std::string_view request,
    std::string* response,
    DWORD timeout_ms,
    std::string* error);

}  // namespace pal4::inject

#include "ipc_server.h"

#include <iomanip>
#include <sstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_renderer_hooks.h"
#include "hook_manager.h"
#include "input_hooks.h"
#include "memory_debug_runtime.h"
#include "pal4inject/memory_debug.h"
#include "pal4inject/protocol.h"
#include "pal4inject/script_mode_override.h"
#include "pal4inject/ui_snapshot.h"
#include "runtime_preferences.h"
#include "runtime_state.h"
#include "ui_snapshot_runtime.h"

namespace pal4::inject {
namespace {

HANDLE g_ipc_thread = nullptr;
constexpr DWORD kPipeBufferSize = 65536;

void AppendRuntimeDebugLog(const std::string_view line) {
    char temp_path[MAX_PATH];
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
        return;
    }

    std::string log_path = std::string(temp_path, temp_len);
    if (!log_path.empty() && log_path.back() != '\\') {
        log_path.push_back('\\');
    }
    log_path += "pal4_inject_runtime.log";

    HANDLE file = CreateFileA(
        log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    const std::string payload = std::string(line) + "\r\n";
    DWORD written = 0;
    WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    CloseHandle(file);
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

std::string HexValue(const std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

void AppendMemoryRegionFields(
    const MemoryRegionInfo& region,
    ProtocolResponse* response) {
    if (!response) {
        return;
    }
    response->fields["address_space"] = ToString(region.address_space);
    response->fields["input_address"] = HexValue(region.input_address);
    response->fields["resolved_address"] = HexValue(region.resolved_address);
    response->fields["region_base"] = HexValue(region.base);
    response->fields["region_allocation_base"] = HexValue(region.allocation_base);
    response->fields["region_size"] = HexValue(region.region_size);
    response->fields["region_state"] = DescribeMemoryState(region.state);
    response->fields["region_type"] = DescribeMemoryType(region.type);
    response->fields["region_protect"] = DescribeMemoryProtect(region.protect);
    response->fields["region_readable"] = region.readable ? "1" : "0";
    response->fields["region_writable"] = region.writable ? "1" : "0";
    response->fields["region_executable"] = region.executable ? "1" : "0";
}

std::string BuildHookSummary(const std::vector<HookStatus>& statuses) {
    std::ostringstream out;
    bool first = true;
    for (const auto& status : statuses) {
        if (!first) {
            out << '|';
        }
        first = false;
        out << ToString(status.id)
            << ",installed=" << (status.installed ? 1 : 0)
            << ",mode=" << ToString(status.mode)
            << ",calls=" << status.call_count
            << ",error=" << EscapeProtocolToken(status.last_error);
    }
    return out.str();
}

bool TryReadCurrentScriptModeFlag(std::uint32_t* out_flag) {
    return TryReadLocalScriptModeFlag(GetRuntimeState().MainModuleBase(), out_flag);
}

ProtocolResponse BuildSnapshotResponse() {
    std::string dispatch_reason;
    const auto snapshot = GetRuntimeState().BuildSnapshot(0);
    ProtocolResponse response{};
    response.ok = true;
    response.status = "snapshot";
    response.fields["bootstrap_ready"] = snapshot.bootstrap_ready ? "1" : "0";
    response.fields["hooks_ready"] = snapshot.hooks_ready ? "1" : "0";
    response.fields["pipe_ready"] = snapshot.pipe_ready ? "1" : "0";
    response.fields["ui_dispatch_ready"] = snapshot.ui_dispatch_ready ? "1" : "0";
    response.fields["crash_handler_ready"] = snapshot.crash_handler_ready ? "1" : "0";
    response.fields["main_module_base"] =
        HexValue(static_cast<std::uint32_t>(snapshot.main_module_base));
    response.fields["msaa_level"] = ToString(snapshot.msaa_level);
    response.fields["current_paliv_entry"] = HexValue(snapshot.current_paliv_entry);
    response.fields["last_paliv_entry_observed"] = HexValue(snapshot.last_paliv_entry_observed);
    response.fields["last_ui_event"] = snapshot.last_ui_event;
    response.fields["last_error"] = snapshot.last_error;
    response.fields["last_font_sync_summary"] = snapshot.last_font_sync_summary;
    response.fields["last_font_sync_ok"] = snapshot.last_font_sync_ok ? "1" : "0";
    response.fields["last_crash_report_path"] = snapshot.last_crash_report_path;
    response.fields["last_crash_dump_path"] = snapshot.last_crash_dump_path;
    response.fields["last_crash_summary"] = snapshot.last_crash_summary;
    response.fields["process_ui_event_installed"] = snapshot.process_ui_event.installed ? "1" : "0";
    response.fields["process_ui_event_mode"] = ToString(snapshot.process_ui_event.mode);
    response.fields["process_ui_event_call_count"] = std::to_string(snapshot.process_ui_event.call_count);
    response.fields["process_ui_event_last_error"] = snapshot.process_ui_event.last_error;
    std::uint32_t script_mode_flag = 0;
    if (TryReadCurrentScriptModeFlag(&script_mode_flag)) {
        response.fields["script_mode"] =
            ToString(ScriptModeFromCsbFlag(script_mode_flag));
        response.fields["script_mode_flag"] = std::to_string(script_mode_flag);
    }
    std::string script_mode_error;
    const auto requested_script_mode = LoadInheritedScriptModeOverride(&script_mode_error);
    response.fields["requested_script_mode"] = requested_script_mode.has_value()
        ? ToString(*requested_script_mode)
        : ToString(ScriptMode::inherit);
    if (!script_mode_error.empty()) {
        response.fields["requested_script_mode_error"] = script_mode_error;
    }
    response.fields["hooks"] = BuildHookSummary(snapshot.active_hooks);
    if (!dispatch_reason.empty()) {
        response.fields["ui_dispatch_reason"] = dispatch_reason;
    }
    return response;
}

ProtocolResponse DispatchCommand(const ProtocolCommand& command) {
    ProtocolResponse response{};
    response.ok = true;

    switch (command.kind) {
    case ProtocolCommandKind::ping:
        response.status = "pong";
        return response;
    case ProtocolCommandKind::hook_status: {
        const auto statuses = GetRuntimeState().CopyHookStatuses();
        response.status = "hook_status";
        response.fields["hook_count"] = std::to_string(statuses.size());
        response.fields["hooks"] = BuildHookSummary(statuses);
        return response;
    }
    case ProtocolCommandKind::enqueue_ui_message: {
        std::string error;
        const bool handled = DispatchUiMessageCommand(command.ui_message, &error);
        response.ok = handled;
        response.status = handled ? "enqueue_ui_message" : "error";
        response.fields["handled"] = handled ? "1" : "0";
        if (!handled) {
            response.message = error;
        }
        response.fields["last_ui_event"] = GetRuntimeState().BuildSnapshot(0).last_ui_event;
        return response;
    }
    case ProtocolCommandKind::simulate_key: {
        std::string error;
        const bool handled = DispatchSimulatedKey(
            command.virtual_key,
            command.key_up,
            command.ui_message.bypass_os_queue,
            &error);
        response.ok = handled;
        response.status = handled ? "simulate_key" : "error";
        response.fields["handled"] = handled ? "1" : "0";
        if (!handled) {
            response.message = error;
        }
        return response;
    }
    case ProtocolCommandKind::read_ui_state:
        return BuildSnapshotResponse();
    case ProtocolCommandKind::read_paliv_state:
        response.status = "read_paliv_state";
        response.fields["current_paliv_entry"] = HexValue(ReadCurrentPalivEntry());
        return response;
    case ProtocolCommandKind::wait_for_hook_calls: {
        const bool ok = GetRuntimeState().WaitForHookCalls(
            command.hook_id,
            command.expected_call_count,
            command.timeout_ms);
        response.ok = ok;
        response.status = ok ? "wait_for_hook_calls" : "timeout";
        response.fields["hook"] = ToString(command.hook_id);
        response.fields["count"] = std::to_string(command.expected_call_count);
        return response;
    }
    case ProtocolCommandKind::wait_for_paliv_state: {
        const ULONGLONG deadline = GetTickCount64() + command.timeout_ms;
        bool ok = false;
        std::uint32_t observed = 0;
        do {
            observed = ReadCurrentPalivEntry();
            if (observed == command.expected_paliv_entry) {
                ok = true;
                break;
            }
            Sleep(25);
        } while (GetTickCount64() < deadline);
        response.ok = ok;
        response.status = ok ? "wait_for_paliv_state" : "timeout";
        response.fields["entry"] = HexValue(command.expected_paliv_entry);
        response.fields["observed"] = HexValue(observed);
        return response;
    }
    case ProtocolCommandKind::read_event_log:
        response.status = "read_event_log";
        response.fields["event_log_tail"] = GetRuntimeState().BuildEventLogTail();
        return response;
    case ProtocolCommandKind::set_hook_mode:
        ApplyHookModePreference(command.hook_id, command.hook_mode, true, true);
        response.status = "set_hook_mode";
        response.fields["hook"] = ToString(command.hook_id);
        response.fields["mode"] = ToString(command.hook_mode);
        return response;
    case ProtocolCommandKind::snapshot_ui: {
        UiSnapshotTree tree{};
        std::string error;
        const bool ok = CaptureAndCacheUiSnapshot(&tree, &error);
        response.ok = ok;
        response.status = ok ? "snapshot_ui" : "error";
        if (!ok) {
            response.message = error;
            return response;
        }
        response.fields["node_count"] = std::to_string(CountUiSnapshotNodes(tree));
        response.fields["tree"] = SerializeUiSnapshotTree(tree);
        return response;
    }
    case ProtocolCommandKind::click_ui_ref: {
        std::string error;
        const bool ok = ClickCachedUiSnapshotRef(command.ui_ref, &error);
        response.ok = ok;
        response.status = ok ? "click_ui_ref" : "error";
        response.fields["ref"] = command.ui_ref;
        if (!ok) {
            response.message = error;
        }
        return response;
    }
    case ProtocolCommandKind::fill_ui_ref: {
        std::string error;
        const bool ok = FillCachedUiSnapshotRef(command.ui_ref, command.text, &error);
        response.ok = ok;
        response.status = ok ? "fill_ui_ref" : "error";
        response.fields["ref"] = command.ui_ref;
        response.fields["text_size"] = std::to_string(command.text.size());
        if (!ok) {
            response.message = error;
        }
        return response;
    }
    case ProtocolCommandKind::type_text: {
        std::string error;
        const bool ok = TypeIntoFocusedUiWindow(command.text, &error);
        response.ok = ok;
        response.status = ok ? "type_text" : "error";
        response.fields["text_size"] = std::to_string(command.text.size());
        if (!ok) {
            response.message = error;
        }
        return response;
    }
    case ProtocolCommandKind::query_memory: {
        MemoryRegionInfo region{};
        std::string error;
        const bool ok = QueryMemoryRegion(command.address_space, command.address, &region, &error);
        response.ok = ok;
        response.status = ok ? "query_memory" : "error";
        if (!ok) {
            response.message = error;
            return response;
        }
        AppendMemoryRegionFields(region, &response);
        return response;
    }
    case ProtocolCommandKind::read_memory: {
        MemoryRegionInfo region{};
        std::vector<std::uint8_t> bytes;
        std::string error;
        const bool ok = ReadMemoryRegion(
            command.address_space,
            command.address,
            command.size,
            &bytes,
            &region,
            &error);
        response.ok = ok;
        response.status = ok ? "read_memory" : "error";
        if (!ok) {
            response.message = error;
            return response;
        }
        AppendMemoryRegionFields(region, &response);
        response.fields["bytes"] = FormatHexBytes(bytes);
        response.fields["size"] = std::to_string(bytes.size());
        return response;
    }
    case ProtocolCommandKind::write_memory: {
        MemoryRegionInfo region{};
        std::vector<std::uint8_t> before_bytes;
        std::vector<std::uint8_t> after_bytes;
        std::vector<std::uint8_t> payload;
        std::string error;
        if (!ParseHexBytes(command.hex_bytes, &payload, &error)) {
            response.ok = false;
            response.status = "error";
            response.message = error;
            return response;
        }
        const bool ok = WriteMemoryRegion(
            command.address_space,
            command.address,
            payload,
            command.unsafe_code_write,
            &region,
            &before_bytes,
            &after_bytes,
            &error);
        response.ok = ok;
        response.status = ok ? "write_memory" : "error";
        if (!ok) {
            response.message = error;
            return response;
        }
        AppendMemoryRegionFields(region, &response);
        response.fields["bytes"] = FormatHexBytes(payload);
        response.fields["before_bytes"] = FormatHexBytes(before_bytes);
        response.fields["after_bytes"] = FormatHexBytes(after_bytes);
        response.fields["unsafe_code_write"] = command.unsafe_code_write ? "1" : "0";
        return response;
    }
    case ProtocolCommandKind::shutdown:
        response.status = "shutdown";
        response.fields["shutting_down"] = "1";
        GetRuntimeState().RequestShutdown();
        GetHookManager().UninstallAll();
        return response;
    case ProtocolCommandKind::invalid:
        break;
    }

    response.ok = false;
    response.status = "error";
    response.message = "invalid command";
    return response;
}

DWORD WINAPI IpcServerThreadProc(LPVOID) {
    auto& state = GetRuntimeState();
    for (;;) {
        if (state.ShutdownRequested()) {
            break;
        }

        AppendRuntimeDebugLog("ipc_waiting_for_client");
        const std::string pipe_name = state.PipeName();
        HANDLE pipe = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            kPipeBufferSize,
            kPipeBufferSize,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            state.SetLastError("CreateNamedPipeA failed: " + FormatWindowsError(GetLastError()));
            break;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr);
        if (!connected && GetLastError() == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        if (!connected) {
            CloseHandle(pipe);
            if (state.ShutdownRequested()) {
                break;
            }
            continue;
        }

        char buffer[kPipeBufferSize];
        DWORD read = 0;
        ProtocolResponse response{};
        if (!ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr)) {
            response.ok = false;
            response.status = "error";
            response.message = "ReadFile(pipe) failed: " + FormatWindowsError(GetLastError());
        } else {
            buffer[read] = '\0';
            ProtocolCommand command{};
            std::string parse_error;
            if (!ParseProtocolCommand(buffer, &command, &parse_error)) {
                AppendRuntimeDebugLog(std::string("ipc_parse_error: ") + parse_error);
                response.ok = false;
                response.status = "error";
                response.message = parse_error;
            } else {
                AppendRuntimeDebugLog(std::string("ipc_command: ") + ToString(command.kind));
                response = DispatchCommand(command);
                AppendRuntimeDebugLog(
                    std::string("ipc_response: ") + response.status +
                    " ok=" + (response.ok ? "1" : "0"));
            }
        }

        const std::string wire = FormatProtocolResponse(response);
        DWORD written = 0;
        WriteFile(pipe, wire.data(), static_cast<DWORD>(wire.size()), &written, nullptr);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        AppendRuntimeDebugLog("ipc_client_disconnected");
    }

    state.SetPipeReady(false);
    return 0;
}

}  // namespace

bool StartIpcServer(std::string* error) {
    if (g_ipc_thread) {
        return true;
    }

    g_ipc_thread = CreateThread(nullptr, 0, &IpcServerThreadProc, nullptr, 0, nullptr);
    if (!g_ipc_thread) {
        if (error) {
            *error = "CreateThread(ipc server) failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    GetRuntimeState().SetPipeReady(true);
    return true;
}

void StopIpcServer() {
    auto& state = GetRuntimeState();
    state.RequestShutdown();
    if (g_ipc_thread && GetThreadId(g_ipc_thread) != GetCurrentThreadId()) {
        WaitForSingleObject(g_ipc_thread, 5000);
    }
    if (g_ipc_thread && GetThreadId(g_ipc_thread) != GetCurrentThreadId()) {
        CloseHandle(g_ipc_thread);
        g_ipc_thread = nullptr;
    }
}

}  // namespace pal4::inject

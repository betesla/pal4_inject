#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/hook_inventory.h"
#include "pal4inject/ida_addresses.h"
#include "pal4inject/input_logic.h"
#include "pal4inject/input_queue.h"
#include "pal4inject/launcher.h"
#include "pal4inject/protocol.h"
#include "runtime_state.h"

namespace {

using pal4::inject::HookId;
using pal4::inject::ProtocolCommand;
using pal4::inject::ProtocolCommandKind;
using pal4::inject::ProtocolResponse;
using pal4::inject::UiInjectedAction;

std::filesystem::path CurrentExecutableDirectory() {
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return len == 0
        ? std::filesystem::current_path()
        : std::filesystem::path(std::string(buffer, len)).parent_path();
}

void TestResolveRuntimeAddress() {
    constexpr std::uintptr_t kModuleBase = 0x10000000;
    const auto resolved = pal4::inject::ida::ResolveRuntimeAddress(
        kModuleBase,
        pal4::inject::ida::kProcessUiEvent);
    assert(resolved == kModuleBase + (pal4::inject::ida::kProcessUiEvent - pal4::inject::ida::kLaunchExeBase));
}

void TestHookInventory() {
    const auto inventory = pal4::inject::BuildHookInventorySkeleton();
    assert(inventory.size() == 8);
    bool found_process_ui_event = false;
    bool found_handle_ui_message = false;
    bool found_reserved_wndproc = false;
    for (const auto& hook : inventory) {
        assert(!hook.expected_prologue.empty());
        assert(hook.patch_span >= 5);
        if (hook.id == HookId::process_ui_event) {
            found_process_ui_event = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 8);
        }
        if (hook.id == HookId::handle_ui_message) {
            found_handle_ui_message = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
        }
        if (hook.id == HookId::pal4_main_wndproc) {
            found_reserved_wndproc = true;
            assert(hook.patch_span == 8);
        }
    }
    assert(found_process_ui_event);
    assert(found_handle_ui_message);
    assert(found_reserved_wndproc);
}

void TestProtocolRoundTrip() {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::enqueue_ui_message;
    command.ui_message.msg = WM_KEYDOWN;
    command.ui_message.wparam = VK_RETURN;
    command.ui_message.lparam = 0;
    command.ui_message.bypass_os_queue = true;

    const std::string wire = pal4::inject::FormatProtocolCommand(command);
    ProtocolCommand parsed{};
    std::string error;
    assert(pal4::inject::ParseProtocolCommand(wire, &parsed, &error));
    assert(parsed.kind == ProtocolCommandKind::enqueue_ui_message);
    assert(parsed.ui_message.msg == WM_KEYDOWN);
    assert(parsed.ui_message.wparam == VK_RETURN);
    assert(parsed.ui_message.bypass_os_queue);

    command = {};
    command.kind = ProtocolCommandKind::wait_for_hook_calls;
    command.hook_id = HookId::process_ui_event;
    command.expected_call_count = 42;
    command.timeout_ms = 5000;
    assert(pal4::inject::ParseProtocolCommand(
        pal4::inject::FormatProtocolCommand(command),
        &parsed,
        &error));
    assert(parsed.kind == ProtocolCommandKind::wait_for_hook_calls);
    assert(parsed.hook_id == HookId::process_ui_event);
    assert(parsed.expected_call_count == 42);
    assert(parsed.timeout_ms == 5000);

    ProtocolResponse response{};
    response.ok = true;
    response.status = "snapshot";
    response.fields["last_ui_event"] = "WM_KEYDOWN";
    response.fields["bootstrap_ready"] = "1";
    const std::string response_wire = pal4::inject::FormatProtocolResponse(response);
    ProtocolResponse parsed_response{};
    assert(pal4::inject::ParseProtocolResponse(response_wire, &parsed_response, &error));
    assert(parsed_response.ok);
    assert(parsed_response.status == "snapshot");
    assert(parsed_response.fields["bootstrap_ready"] == "1");
}

void TestInputLogic() {
    assert(pal4::inject::NormalizeProcessUiEventKeyDown(17) == 200);
    assert(pal4::inject::NormalizeProcessUiEventKeyDown(30) == 203);
    assert(pal4::inject::NormalizeProcessUiEventKeyDown(57) == 28);
    assert(pal4::inject::ShouldSuppressMappedUiKey(1));
    assert(!pal4::inject::ShouldSuppressMappedUiKey(57));

    const auto key_down = pal4::inject::BuildUiInjectedPlan(WM_KEYDOWN, 17, 0);
    assert(key_down.action == UiInjectedAction::key_down);
    assert(key_down.code == 200);

    const auto key_up = pal4::inject::BuildUiInjectedPlan(WM_KEYUP, 32, 0);
    assert(key_up.action == UiInjectedAction::key_up);
    assert(key_up.code == 205);

    const auto mouse_move = pal4::inject::BuildUiInjectedPlan(WM_MOUSEMOVE, 0, 0);
    assert(mouse_move.action == UiInjectedAction::mouse_move);

    const auto wheel = pal4::inject::BuildUiInjectedPlan(
        WM_MOUSEWHEEL,
        0,
        static_cast<std::uint32_t>(120u << 16));
    assert(wheel.action == UiInjectedAction::mouse_wheel);
    assert(wheel.wheel_delta == 1.0F);

    const auto resize = pal4::inject::BuildUiInjectedPlan(WM_SIZE, 0, 0);
    assert(resize.action == UiInjectedAction::renderer_size_changed);

    const auto activate = pal4::inject::BuildUiInjectedPlan(WM_ACTIVATE, 0, 1);
    assert(activate.action == UiInjectedAction::renderer_size_changed_and_redraw);

    const auto nc_move = pal4::inject::BuildUiInjectedPlan(WM_NCMOUSEMOVE, 0, 0);
    assert(nc_move.action == UiInjectedAction::disable_mouse_capture);
}

void TestInputQueue() {
    pal4::inject::InputFrameQueue queue;
    pal4::inject::QueuedInputSource source(&queue);

    pal4::inject::UiMessageCommand command{};
    command.msg = WM_KEYDOWN;
    command.wparam = VK_RETURN;
    queue.PushCommand(command);

    auto frame = source.CaptureFrame();
    assert(frame.frame_index == 1);
    assert(frame.commands.size() == 1);
    assert(frame.commands.front().msg == WM_KEYDOWN);

    frame = source.CaptureFrame();
    assert(frame.frame_index == 0);
    assert(frame.commands.empty());
}

void TestRuntimeEventLog() {
    auto& state = pal4::inject::GetRuntimeState();
    state.InitializeInventory(pal4::inject::BuildHookInventorySkeleton());
    state.AppendEventLog("event-1");
    state.AppendEventLog("event-2");
    state.IncrementHookCall(HookId::process_ui_event);
    state.ObservePalivEntry(2);
    assert(state.WaitForHookCalls(HookId::process_ui_event, 1, 10));
    assert(state.WaitForPalivEntry(2, 10));
    const auto tail = state.BuildEventLogTail();
    assert(tail.find("event-1") != std::string::npos);
    assert(tail.find("event-2") != std::string::npos);
}

void TestLauncherNaming() {
    assert(pal4::inject::BuildReadyEventName(1234) == "Local\\PAL4InjectReady_1234");
    assert(pal4::inject::BuildPipeName(1234) == "\\\\.\\pipe\\pal4_inject_1234");
}

ProtocolResponse SendCommand(
    const std::string& pipe_name,
    const ProtocolCommand& command) {
    std::string response;
    std::string error;
    const std::string wire = pal4::inject::FormatProtocolCommand(command);
    const bool sent = pal4::inject::SendPipeCommand(pipe_name, wire, &response, 5000, &error);
    if (!sent) {
        std::cerr << "SendCommand failed: " << wire << " error=" << error << "\n";
    }
    assert(sent);
    ProtocolResponse parsed{};
    const bool parsed_ok = pal4::inject::ParseProtocolResponse(response, &parsed, &error);
    if (!parsed_ok) {
        std::cerr << "ParseProtocolResponse failed: " << wire << " response=" << response << " error=" << error << "\n";
    }
    assert(parsed_ok);
    return parsed;
}

ProtocolResponse ReadUiState(const std::string& pipe_name) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::read_ui_state;
    return SendCommand(pipe_name, command);
}

ProtocolResponse ReadPalivState(const std::string& pipe_name) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::read_paliv_state;
    return SendCommand(pipe_name, command);
}

std::uint64_t FindHookCallCount(const std::string& hook_summary, const HookId id) {
    const std::string needle = std::string(pal4::inject::ToString(id)) + ",installed=";
    const std::size_t start = hook_summary.find(needle);
    if (start == std::string::npos) {
        return 0;
    }
    const std::size_t calls_pos = hook_summary.find("calls=", start);
    if (calls_pos == std::string::npos) {
        return 0;
    }
    const std::size_t value_start = calls_pos + 6;
    const std::size_t value_end = hook_summary.find(',', value_start);
    return std::stoull(hook_summary.substr(value_start, value_end - value_start));
}

bool WaitForSnapshotField(
    const std::string& pipe_name,
    const std::string& field_name,
    const std::string& expected_value,
    const DWORD timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = ReadUiState(pipe_name);
        auto it = snapshot.fields.find(field_name);
        if (it != snapshot.fields.end() && it->second == expected_value) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

enum class ScenarioStepKind : std::uint8_t {
    send_ui_message = 0,
    simulate_key,
    wait_for_hook_calls,
    wait_for_paliv_state,
    assert_snapshot_field,
    sleep_ms,
};

struct ScenarioStep {
    ScenarioStepKind kind = ScenarioStepKind::sleep_ms;
    pal4::inject::UiMessageCommand ui_message{};
    std::uint32_t virtual_key = 0;
    bool key_up = false;
    bool bypass_os_queue = true;
    HookId hook_id = HookId::process_ui_event;
    std::uint64_t expected_call_count = 0;
    std::uint32_t expected_paliv_entry = 0;
    std::string field_name;
    std::string expected_value;
    DWORD duration_ms = 0;
};

void RunScenario(
    const std::string& pipe_name,
    const std::vector<ScenarioStep>& steps) {
    for (const auto& step : steps) {
        switch (step.kind) {
        case ScenarioStepKind::send_ui_message: {
            ProtocolCommand command{};
            command.kind = ProtocolCommandKind::enqueue_ui_message;
            command.ui_message = step.ui_message;
            const auto response = SendCommand(pipe_name, command);
            assert(response.ok);
            break;
        }
        case ScenarioStepKind::simulate_key: {
            ProtocolCommand command{};
            command.kind = ProtocolCommandKind::simulate_key;
            command.virtual_key = step.virtual_key;
            command.key_up = step.key_up;
            command.ui_message.bypass_os_queue = step.bypass_os_queue;
            const auto response = SendCommand(pipe_name, command);
            if (!response.ok) {
                std::cerr << "simulate_key failed: vk=" << step.virtual_key
                          << " key_up=" << step.key_up
                          << " bypass_os_queue=" << step.bypass_os_queue
                          << " status=" << response.status
                          << " message=" << response.message << "\n";
            }
            assert(response.ok);
            break;
        }
        case ScenarioStepKind::wait_for_hook_calls: {
            ProtocolCommand command{};
            command.kind = ProtocolCommandKind::wait_for_hook_calls;
            command.hook_id = step.hook_id;
            command.expected_call_count = step.expected_call_count;
            command.timeout_ms = step.duration_ms;
            const auto response = SendCommand(pipe_name, command);
            if (!response.ok) {
                std::cerr << "wait_for_hook_calls failed: hook=" << pal4::inject::ToString(step.hook_id)
                          << " count=" << step.expected_call_count
                          << " status=" << response.status << "\n";
            }
            assert(response.ok);
            break;
        }
        case ScenarioStepKind::wait_for_paliv_state: {
            ProtocolCommand command{};
            command.kind = ProtocolCommandKind::wait_for_paliv_state;
            command.expected_paliv_entry = step.expected_paliv_entry;
            command.timeout_ms = step.duration_ms;
            const auto response = SendCommand(pipe_name, command);
            if (!response.ok) {
                const auto observed = response.fields.find("observed");
                std::cerr << "wait_for_paliv_state failed: expected=" << step.expected_paliv_entry
                          << " observed=" << (observed != response.fields.end() ? observed->second : std::string("<missing>"))
                          << " status=" << response.status << "\n";
            }
            assert(response.ok);
            break;
        }
        case ScenarioStepKind::assert_snapshot_field:
            assert(WaitForSnapshotField(
                pipe_name,
                step.field_name,
                step.expected_value,
                step.duration_ms));
            break;
        case ScenarioStepKind::sleep_ms:
            std::this_thread::sleep_for(std::chrono::milliseconds(step.duration_ms));
            break;
        }
    }
}

void PressKey(
    const std::string& pipe_name,
    const std::uint32_t virtual_key,
    const bool bypass_os_queue,
    const DWORD settle_ms = 80) {
    RunScenario(pipe_name, {
        {
            ScenarioStepKind::simulate_key,
            {},
            virtual_key,
            false,
            bypass_os_queue,
            HookId::process_ui_event,
            0,
            0,
            {},
            {},
            0,
        },
        {
            ScenarioStepKind::sleep_ms,
            {},
            0,
            false,
            true,
            HookId::process_ui_event,
            0,
            0,
            {},
            {},
            settle_ms,
        },
        {
            ScenarioStepKind::simulate_key,
            {},
            virtual_key,
            true,
            bypass_os_queue,
            HookId::process_ui_event,
            0,
            0,
            {},
            {},
            0,
        },
        {
            ScenarioStepKind::sleep_ms,
            {},
            0,
            false,
            true,
            HookId::process_ui_event,
            0,
            0,
            {},
            {},
            settle_ms,
        },
    });
}

struct IntegrationHarness {
    pal4::inject::InjectedProcess process{};
    std::string pipe_name;
};

IntegrationHarness LaunchHarness() {
    char* game_root_env = nullptr;
    std::size_t game_root_len = 0;
    _dupenv_s(&game_root_env, &game_root_len, "PAL4_GAME_ROOT");
    const std::string game_root = game_root_env
        ? std::string(game_root_env, game_root_len ? game_root_len - 1 : 0)
        : std::string();
    free(game_root_env);
    assert(!game_root.empty());

    pal4::inject::LaunchOptions options;
    options.game_root = game_root;
    options.dll_path = CurrentExecutableDirectory() / "pal4_runtime_x86.dll";
    options.ready_timeout_ms = 20000;
    options.resume_after_ready = true;
    assert(std::filesystem::exists(options.dll_path));

    IntegrationHarness harness;
    const auto result = pal4::inject::LaunchInjectedProcess(options, &harness.process);
    assert(result.ok);
    harness.pipe_name = result.pipe_name;

    ProtocolCommand ping{};
    ping.kind = ProtocolCommandKind::ping;
    const auto pong = SendCommand(harness.pipe_name, ping);
    assert(pong.ok);
    assert(pong.status == "pong");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    return harness;
}

void CleanupHarness(IntegrationHarness* harness) {
    if (!harness) {
        return;
    }
    if (!harness->pipe_name.empty()) {
        ProtocolCommand shutdown{};
        shutdown.kind = ProtocolCommandKind::shutdown;
        std::string response;
        std::string error;
        pal4::inject::SendPipeCommand(
            harness->pipe_name,
            pal4::inject::FormatProtocolCommand(shutdown),
            &response,
            3000,
            &error);
    }
    if (harness->process.process_info.hProcess) {
        TerminateProcess(harness->process.process_info.hProcess, 0);
        WaitForSingleObject(harness->process.process_info.hProcess, 5000);
    }
    harness->process.Close();
}

void TestSkipLogoToMenuScenario() {
    auto harness = LaunchHarness();
    PressKey(harness.pipe_name, VK_RETURN, false);
    assert(WaitForSnapshotField(harness.pipe_name, "ui_dispatch_ready", "1", 20000));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    CleanupHarness(&harness);
}

void TestMenuNewGameTransitionScenario() {
    auto harness = LaunchHarness();
    PressKey(harness.pipe_name, VK_RETURN, false);
    assert(WaitForSnapshotField(harness.pipe_name, "ui_dispatch_ready", "1", 20000));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    PressKey(harness.pipe_name, VK_RETURN, false);
    RunScenario(harness.pipe_name, {
        {
            ScenarioStepKind::wait_for_paliv_state,
            {},
            0,
            false,
            true,
            HookId::process_ui_event,
            0,
            1,
            {},
            {},
            15000,
        },
    });

    CleanupHarness(&harness);
}

void TestMenuExitScenario() {
    auto harness = LaunchHarness();
    PressKey(harness.pipe_name, VK_RETURN, false);
    assert(WaitForSnapshotField(harness.pipe_name, "ui_dispatch_ready", "1", 20000));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    PressKey(harness.pipe_name, VK_DOWN, false);
    PressKey(harness.pipe_name, VK_DOWN, false);
    PressKey(harness.pipe_name, VK_RETURN, false);

    const DWORD wait_rc = WaitForSingleObject(harness.process.process_info.hProcess, 10000);
    assert(wait_rc == WAIT_OBJECT_0);
    harness.process.Close();
}

void MaybeRunIntegrationSmoke() {
    char* run_integration = nullptr;
    std::size_t run_integration_len = 0;
    _dupenv_s(&run_integration, &run_integration_len, "PAL4_INJECT_RUN_INTEGRATION");
    const bool should_run_integration =
        run_integration && std::string(run_integration, run_integration_len ? run_integration_len - 1 : 0) == "1";
    free(run_integration);
    if (!should_run_integration) {
        return;
    }

    TestSkipLogoToMenuScenario();

    char* run_full = nullptr;
    std::size_t run_full_len = 0;
    _dupenv_s(&run_full, &run_full_len, "PAL4_INJECT_RUN_FULL_SCENARIOS");
    const bool should_run_full =
        run_full && std::string(run_full, run_full_len ? run_full_len - 1 : 0) == "1";
    free(run_full);
    if (!should_run_full) {
        return;
    }

    TestMenuNewGameTransitionScenario();
    TestMenuExitScenario();
}

}  // namespace

int main() {
    TestResolveRuntimeAddress();
    TestHookInventory();
    TestProtocolRoundTrip();
    TestInputLogic();
    TestInputQueue();
    TestRuntimeEventLog();
    TestLauncherNaming();
    MaybeRunIntegrationSmoke();
    std::cout << "pal4_inject_tests: ok\n";
    return 0;
}

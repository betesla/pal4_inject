#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/hook_inventory.h"
#include "pal4inject/ida_addresses.h"
#include "pal4inject/dpi_awareness.h"
#include "pal4inject/inject_control_panel.h"
#include "pal4inject/input_logic.h"
#include "pal4inject/input_queue.h"
#include "pal4inject/launcher.h"
#include "pal4inject/camera_pitch_guard.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/camera_unlock_patch.h"
#include "pal4inject/crash_capture.h"
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
    assert(inventory.size() == 13);
    bool found_process_ui_event = false;
    bool found_handle_ui_message = false;
    bool found_gi_talk = false;
    bool found_cegui_renderer_ctor = false;
    bool found_cegui_system_init = false;
    bool found_setup_minimap_texture = false;
    bool found_camera_update_matrix = false;
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
        if (hook.id == HookId::gi_talk) {
            found_gi_talk = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 8);
        }
        if (hook.id == HookId::cegui_renderer_constructor_2) {
            found_cegui_renderer_ctor = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kCeguiRendererConstructor2);
        }
        if (hook.id == HookId::cegui_system_initialize) {
            found_cegui_system_init = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 13);
            assert(hook.ida_ea == pal4::inject::ida::kCeguiSystemInitialize);
        }
        if (hook.id == HookId::setup_minimap_texture) {
            found_setup_minimap_texture = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kSetupMinimapTexture);
        }
        if (hook.id == HookId::camera_update_matrix) {
            found_camera_update_matrix = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kCameraUpdateMatrix);
        }
        if (hook.id == HookId::pal4_main_wndproc) {
            found_reserved_wndproc = true;
            assert(hook.patch_span == 8);
        }
    }
    assert(found_process_ui_event);
    assert(found_handle_ui_message);
    assert(found_gi_talk);
    assert(found_cegui_renderer_ctor);
    assert(found_cegui_system_init);
    assert(found_setup_minimap_texture);
    assert(found_camera_update_matrix);
    assert(found_reserved_wndproc);
}

void TestDpiAwarenessStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::unknown)) == "unknown");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::per_monitor_aware_v2)) == "per_monitor_aware_v2");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::per_monitor_aware)) == "per_monitor_aware");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::system_aware)) == "system_aware");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::already_set)) == "already_set");
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

void TestInjectControlPanelModel() {
    const auto rows = pal4::inject::BuildInjectControlPanelRows();
    assert(rows.size() == 13);
    assert(rows.front().id == HookId::process_ui_event);
    assert(rows.front().allow_mode_change);
    assert(rows[7].id == HookId::cegui_renderer_constructor_2);
    assert(rows[8].id == HookId::cegui_system_initialize);
    assert(rows[9].id == HookId::setup_minimap_texture);
    assert(rows[10].id == HookId::camera_update_matrix);
    assert(rows[11].id == HookId::pal4_main_wndproc);
    assert(rows[11].allow_mode_change);
    assert(rows[12].id == HookId::handle_player_input_events);
    assert(!rows[12].allow_mode_change);

    const auto modes = pal4::inject::BuildInjectControlPanelModes();
    assert(modes.size() == 4);
    assert(modes[0] == pal4::inject::HookMode::observe_only);
    assert(modes[3] == pal4::inject::HookMode::replace_strict);
    assert(pal4::inject::FindInjectControlPanelModeIndex(pal4::inject::HookMode::mirror_compare) == 1);
    assert(pal4::inject::InjectControlPanelModeFromIndex(2) == pal4::inject::HookMode::replace_with_fallback);
    assert(pal4::inject::InjectControlPanelModeFromIndex(99) == pal4::inject::HookMode::observe_only);
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
    state.SetCrashHandlerReady(true);
    state.SetCrashArtifacts("summary", "report.txt", "dump.dmp");
    state.IncrementHookCall(HookId::process_ui_event);
    state.ObservePalivEntry(2);
    assert(state.WaitForHookCalls(HookId::process_ui_event, 1, 10));
    assert(state.WaitForPalivEntry(2, 10));
    const auto tail = state.BuildEventLogTail();
    assert(tail.find("event-1") != std::string::npos);
    assert(tail.find("event-2") != std::string::npos);
    const auto snapshot = state.BuildSnapshot(0);
    assert(snapshot.crash_handler_ready);
    assert(snapshot.last_crash_summary == "summary");
    assert(snapshot.last_crash_report_path == "report.txt");
    assert(snapshot.last_crash_dump_path == "dump.dmp");
}

void TestLauncherNaming() {
    assert(pal4::inject::BuildReadyEventName(1234) == "Local\\PAL4InjectReady_1234");
    assert(pal4::inject::BuildPipeName(1234) == "\\\\.\\pipe\\pal4_inject_1234");
}

void TestCameraPitchUnlockPatchMetadata() {
    assert(pal4::inject::g_camera_pitch_limit_negative == -pal4::inject::kCameraPitchLimitDegrees);
    assert(pal4::inject::g_camera_pitch_limit_positive == pal4::inject::kCameraPitchLimitDegrees);
    assert(pal4::inject::g_camera_pitch_limit_positive < 90.0F);
    assert(pal4::inject::g_camera_pitch_limit_negative > -90.0F);

    const auto patches = pal4::inject::BuildCameraPitchUnlockPatches();
    assert(patches.size() == 4);
    assert(patches[0].instruction_ea == 0x424C1A);
    assert(patches[0].expected_opcode_bytes[0] == 0xD8);
    assert(patches[0].expected_opcode_bytes[1] == 0x15);
    assert(patches[0].expected_operand_ea == 0x842690);
    assert(patches[0].replacement_operand == reinterpret_cast<std::uintptr_t>(&pal4::inject::g_camera_pitch_limit_negative));
    assert(patches[1].instruction_ea == 0x424C91);
    assert(patches[1].expected_operand_ea == 0x841E5C);
    assert(patches[1].replacement_operand == reinterpret_cast<std::uintptr_t>(&pal4::inject::g_camera_pitch_limit_positive));
    assert(patches[2].instruction_ea == 0x424CB5);
    assert(patches[2].replacement_operand == reinterpret_cast<std::uintptr_t>(&pal4::inject::g_camera_pitch_limit_positive));
    assert(patches[3].instruction_ea == 0x424CC7);
    assert(patches[3].replacement_operand == reinterpret_cast<std::uintptr_t>(&pal4::inject::g_camera_pitch_limit_positive));

    const auto scale_patches = pal4::inject::BuildCameraInputScalePatches();
    assert(scale_patches.size() == 1);
    assert(scale_patches[0].instruction_ea == 0x424BFF);
    assert(scale_patches[0].expected_bytes[0] == 0xD8);
    assert(scale_patches[0].expected_bytes[1] == 0x4E);
    assert(scale_patches[0].expected_bytes[2] == 0x30);
    assert(scale_patches[0].replacement_displacement == 0x34);
}

void TestCameraPitchGuardMath() {
    assert(pal4::inject::NormalizeAngle360(0.0F) == 0.0F);
    assert(pal4::inject::NormalizeAngle360(360.0F) == 0.0F);
    assert(pal4::inject::NormalizeAngle360(-10.0F) == 350.0F);
    assert(pal4::inject::NormalizeAngle360(725.0F) == 5.0F);

    assert(pal4::inject::IsSafeCameraPitchAngle(0.0F));
    assert(pal4::inject::IsSafeCameraPitchAngle(89.0F));
    assert(!pal4::inject::IsSafeCameraPitchAngle(90.0F));
    assert(!pal4::inject::IsSafeCameraPitchAngle(180.0F));
    assert(!pal4::inject::IsSafeCameraPitchAngle(270.0F));
    assert(pal4::inject::IsSafeCameraPitchAngle(271.0F));
    assert(pal4::inject::IsSafeCameraPitchAngle(350.0F));

    assert(pal4::inject::ClampCameraPitchAngle(45.0F) == 45.0F);
    assert(pal4::inject::ClampCameraPitchAngle(120.0F) == 89.0F);
    assert(pal4::inject::ClampCameraPitchAngle(260.0F) == 271.0F);
    assert(pal4::inject::ClampCameraPitchAngle(-100.0F) == 271.0F);
}

void TestCeguiWidescreenPlanMath() {
    assert(!pal4::inject::IsWideAspectResolution(800, 600));
    assert(!pal4::inject::IsWideAspectResolution(1024, 768));
    assert(pal4::inject::IsWideAspectResolution(1920, 1080));
    assert(pal4::inject::UsesOriginalWideRendererVariant(1280, 800));
    assert(!pal4::inject::UsesOriginalWideRendererVariant(1920, 1080));

    const auto plan_1280_800 = pal4::inject::BuildCeguiWidescreenPlan(1280, 800);
    assert(plan_1280_800.apply);
    assert(plan_1280_800.use_original_variant);
    assert(plan_1280_800.uniform_scale > 1.3333F && plan_1280_800.uniform_scale < 1.3334F);
    assert(plan_1280_800.horizontal_bias_pixels > 106.66F && plan_1280_800.horizontal_bias_pixels < 106.67F);
    assert(plan_1280_800.logical_horizontal_padding > 79.99F && plan_1280_800.logical_horizontal_padding < 80.01F);

    const auto plan_1920_1080 = pal4::inject::BuildCeguiWidescreenPlan(1920, 1080);
    assert(plan_1920_1080.apply);
    assert(!plan_1920_1080.use_original_variant);
    assert(plan_1920_1080.uniform_scale == 1.8F);
    assert(plan_1920_1080.horizontal_bias_pixels == 240.0F);
    assert(plan_1920_1080.logical_horizontal_padding > 133.33F && plan_1920_1080.logical_horizontal_padding < 133.34F);

    float mouse_x = 0.0F;
    float mouse_y = 0.0F;
    assert(pal4::inject::ApplyCeguiWidescreenMouseTransform(
        plan_1920_1080,
        240.0F,
        0.0F,
        &mouse_x,
        &mouse_y));
    assert(mouse_x > -0.001F && mouse_x < 0.001F);
    assert(mouse_y > -0.001F && mouse_y < 0.001F);

    assert(pal4::inject::ApplyCeguiWidescreenMouseTransform(
        plan_1920_1080,
        1680.0F,
        1080.0F,
        &mouse_x,
        &mouse_y));
    assert(mouse_x > 799.999F && mouse_x < 800.001F);
    assert(mouse_y > 599.999F && mouse_y < 600.001F);

    assert(pal4::inject::ApplyCeguiWidescreenMouseTransform(
        plan_1920_1080,
        0.0F,
        540.0F,
        &mouse_x,
        &mouse_y));
    assert(mouse_x < -133.3F && mouse_x > -133.4F);
    assert(mouse_y > 299.999F && mouse_y < 300.001F);

    const auto minimap_1920_1080 = pal4::inject::BuildWidescreenMinimapPlacement(1920, 1080);
    assert(minimap_1920_1080.apply);
    assert(minimap_1920_1080.x == 244);
    assert(minimap_1920_1080.y == 719);
    assert(minimap_1920_1080.width == 311);
    assert(minimap_1920_1080.height == 311);

    const auto minimap_1280_800 = pal4::inject::BuildWidescreenMinimapPlacement(1280, 800);
    assert(minimap_1280_800.apply);
    assert(minimap_1280_800.x == 109);
    assert(minimap_1280_800.y == 532);
    assert(minimap_1280_800.width == 231);
    assert(minimap_1280_800.height == 231);
}

void TestCrashCaptureHelpers() {
    assert(pal4::inject::IsCrashExceptionCode(0xC0000005));
    assert(pal4::inject::IsCrashExceptionCode(0xC00000FD));
    assert(!pal4::inject::IsCrashExceptionCode(0xE06D7363));
    assert(std::string(pal4::inject::DescribeExceptionCode(0xC0000005)) == "EXCEPTION_ACCESS_VIOLATION");
    assert(std::string(pal4::inject::DescribeExceptionCode(0xE06D7363)) == "MSVC_CPP_EXCEPTION");
    assert(pal4::inject::FormatExceptionCode(0xC0000005) == "0xC0000005");

    const auto stem = pal4::inject::BuildCrashArtifactStem(123, 456, 0xC0000005, 789);
    assert(stem.find("pid123") != std::string::npos);
    assert(stem.find("tid456") != std::string::npos);
    assert(stem.find("0xC0000005") != std::string::npos);

    pal4::inject::CrashContextSnapshot snapshot{};
    snapshot.exception_code = 0xC0000005;
    snapshot.exception_flags = 0;
    snapshot.exception_address = 0x401000;
    snapshot.has_access_address = true;
    snapshot.access_type = 1;
    snapshot.access_address = 0xDEADBEEF;
    snapshot.eip = 0x401000;
    snapshot.esp = 0x12FF00;
    snapshot.ebp = 0x12FF40;
    snapshot.eax = 1;
    snapshot.ebx = 2;
    snapshot.ecx = 3;
    snapshot.edx = 4;
    snapshot.esi = 5;
    snapshot.edi = 6;
    const auto summary = pal4::inject::BuildCrashSummary(snapshot, "unit_test");
    assert(summary.find("source=unit_test") != std::string::npos);
    assert(summary.find("exception_name=EXCEPTION_ACCESS_VIOLATION") != std::string::npos);
    assert(summary.find("access_type=write") != std::string::npos);
    assert(summary.find("register_eip=0x401000") != std::string::npos);
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

struct MenuButtonTarget {
    std::string window_name;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

std::filesystem::path ResolveMainWindowLayoutPath() {
    char* game_root_env = nullptr;
    std::size_t game_root_len = 0;
    _dupenv_s(&game_root_env, &game_root_len, "PAL4_GAME_ROOT");
    const std::filesystem::path game_root = game_root_env
        ? std::filesystem::path(std::string(game_root_env, game_root_len ? game_root_len - 1 : 0))
        : std::filesystem::path();
    free(game_root_env);
    return game_root / "gamedata" / "decompressedData" / "ui" / "layouts" / "MainWindow.xml";
}

std::filesystem::path ResolveMenuWindowLayoutPath() {
    char* game_root_env = nullptr;
    std::size_t game_root_len = 0;
    _dupenv_s(&game_root_env, &game_root_len, "PAL4_GAME_ROOT");
    const std::filesystem::path game_root = game_root_env
        ? std::filesystem::path(std::string(game_root_env, game_root_len ? game_root_len - 1 : 0))
        : std::filesystem::path();
    free(game_root_env);
    return game_root / "gamedata" / "decompressedData" / "ui" / "layouts" / "menuWindow.xml";
}

std::optional<MenuButtonTarget> ParseMenuButtonTarget(
    const std::filesystem::path& xml_path,
    const std::vector<std::string>& candidate_names) {
    if (!std::filesystem::exists(xml_path)) {
        return std::nullopt;
    }

    std::ifstream file(xml_path);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string xml = buffer.str();

    for (const auto& name : candidate_names) {
        const std::string marker = "Name=\"" + name + "\"";
        const auto name_pos = xml.find(marker);
        if (name_pos == std::string::npos) {
            continue;
        }

        const auto window_start = xml.rfind("<Window", name_pos);
        const auto window_end = xml.find("</Window>", name_pos);
        if (window_start == std::string::npos || window_end == std::string::npos) {
            continue;
        }

        const auto window_xml = xml.substr(window_start, window_end - window_start);
        const auto rect_marker = "UnifiedAreaRect\" Value=\"{{0.000000,";
        const auto rect_pos = window_xml.find(rect_marker);
        if (rect_pos == std::string::npos) {
            continue;
        }
        const auto value_begin = rect_pos + std::string("UnifiedAreaRect\" Value=\"").size();
        const auto value_end = window_xml.find('"', value_begin);
        if (value_end == std::string::npos) {
            continue;
        }

        const std::string rect_value = window_xml.substr(value_begin, value_end - value_begin);
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        if (std::sscanf(
                rect_value.c_str(),
                "{{%*f,%f},{%*f,%f},{%*f,%f},{%*f,%f}}",
                &x0,
                &y0,
                &x1,
                &y1) != 4) {
            continue;
        }

        MenuButtonTarget target{};
        target.window_name = name;
        target.left = static_cast<int>(x0);
        target.top = static_cast<int>(y0);
        target.right = static_cast<int>(x1);
        target.bottom = static_cast<int>(y1);
        return target;
    }

    return std::nullopt;
}

std::optional<MenuButtonTarget> ResolveButtonTarget(const std::string& logical_name) {
    if (logical_name == "new_game") {
        return ParseMenuButtonTarget(ResolveMainWindowLayoutPath(), {"BtnNewGame", "NewGameBtn"});
    }
    if (logical_name == "exit_game") {
        return ParseMenuButtonTarget(ResolveMainWindowLayoutPath(), {"BtnExit", "ExitBtn"});
    }
    if (logical_name == "confirm_exit") {
        const auto layout = ResolveMenuWindowLayoutPath();
        auto target = ParseMenuButtonTarget(layout, {"BtnModel"});
        const auto parent = ParseMenuButtonTarget(layout, {"PanelMenu"});
        if (target.has_value() && parent.has_value()) {
            target->left += parent->left;
            target->right += parent->left;
            target->top += parent->top;
            target->bottom += parent->top;
        }
        return target;
    }
    return std::nullopt;
}

std::uint32_t PackClientPoint(const int x, const int y) {
    return static_cast<std::uint32_t>((y & 0xFFFF) << 16) |
        static_cast<std::uint32_t>(x & 0xFFFF);
}

void ClickClientPoint(
    const std::string& pipe_name,
    const int x,
    const int y,
    const DWORD settle_ms = 80) {
    const auto lparam = PackClientPoint(x, y);
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::enqueue_ui_message;

    command.ui_message = {WM_MOUSEMOVE, 0, lparam, false};
    auto response = SendCommand(pipe_name, command);
    assert(response.ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));

    command.ui_message = {WM_LBUTTONDOWN, MK_LBUTTON, lparam, false};
    response = SendCommand(pipe_name, command);
    assert(response.ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));

    command.ui_message = {WM_LBUTTONUP, 0, lparam, false};
    response = SendCommand(pipe_name, command);
    assert(response.ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
}

void ClickMenuButton(const std::string& pipe_name, const std::string& logical_name) {
    const auto target = ResolveButtonTarget(logical_name);
    if (!target.has_value()) {
        std::cerr << "failed to resolve menu target: " << logical_name << "\n";
    }
    assert(target.has_value());
    const int center_x = (target->left + target->right) / 2;
    const int center_y = (target->top + target->bottom) / 2;
    ClickClientPoint(pipe_name, center_x, center_y);
}

void SendWindowClose(const std::string& pipe_name) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::enqueue_ui_message;
    command.ui_message = {WM_CLOSE, 0, 0, false};
    const auto response = SendCommand(pipe_name, command);
    assert(response.ok);
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

bool WaitForHookCountByPolling(
    const std::string& pipe_name,
    const HookId hook_id,
    const std::uint64_t expected_count,
    const DWORD timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = ReadUiState(pipe_name);
        const auto hooks = snapshot.fields.find("hooks");
        if (hooks != snapshot.fields.end() &&
            FindHookCallCount(hooks->second, hook_id) >= expected_count) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

bool WaitForProcessExit(HANDLE process, const DWORD timeout_ms) {
    return WaitForSingleObject(process, timeout_ms) == WAIT_OBJECT_0;
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
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
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
    CleanupHarness(&harness);
}

void TestMenuNewGameTransitionScenario() {
    auto harness = LaunchHarness();
    ClickMenuButton(harness.pipe_name, "new_game");

    for (int i = 0; i < 24; ++i) {
        PressKey(harness.pipe_name, VK_ESCAPE, false, 120);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const DWORD wait_rc = WaitForSingleObject(harness.process.process_info.hProcess, 0);
    assert(wait_rc == WAIT_TIMEOUT);

    CleanupHarness(&harness);
}

void TestMenuExitScenario() {
    auto harness = LaunchHarness();
    ClickMenuButton(harness.pipe_name, "exit_game");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ClickMenuButton(harness.pipe_name, "confirm_exit");

    if (!WaitForProcessExit(harness.process.process_info.hProcess, 3000)) {
        SendWindowClose(harness.pipe_name);
    }
    assert(WaitForProcessExit(harness.process.process_info.hProcess, 10000));
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
    TestDpiAwarenessStrings();
    TestInjectControlPanelModel();
    TestProtocolRoundTrip();
    TestInputLogic();
    TestInputQueue();
    TestRuntimeEventLog();
    TestLauncherNaming();
    TestCameraPitchUnlockPatchMetadata();
    TestCameraPitchGuardMath();
    TestCeguiWidescreenPlanMath();
    TestCrashCaptureHelpers();
    MaybeRunIntegrationSmoke();
    std::cout << "pal4_inject_tests: ok\n";
    return 0;
}

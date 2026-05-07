#include <cassert>
#include <chrono>
#include <cstring>
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
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
#include <windows.h>

#include "pal4inject/hook_inventory.h"
#include "pal4inject/ida_addresses.h"
#include "pal4inject/dpi_awareness.h"
#include "pal4inject/gamepad.h"
#include "pal4inject/inject_control_panel.h"
#include "pal4inject/inject_settings.h"
#include "pal4inject/input_logic.h"
#include "pal4inject/input_queue.h"
#include "pal4inject/launcher.h"
#include "pal4inject/camera_pitch_guard.h"
#include "pal4inject/cegui_font_resync.h"
#include "pal4inject/cegui_font_experiment.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/camera_unlock_patch.h"
#include "pal4inject/crash_capture.h"
#include "pal4inject/memory_debug.h"
#include "pal4inject/protocol.h"
#include "pal4inject/script_mode_override.h"
#include "pal4inject/ui_snapshot.h"
#include "memory_debug_runtime.h"
#include "runtime_state.h"
#include "pal4inject_build_info.h"

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
    assert(inventory.size() == 15);
    bool found_process_ui_event = false;
    bool found_handle_ui_message = false;
    bool found_gi_talk = false;
    bool found_cegui_renderer_ctor = false;
    bool found_load_font_file = false;
    bool found_setup_minimap_texture = false;
    bool found_combat_console_set_image_position = false;
    bool found_combat_console_set_image_position_2 = false;
    bool found_ui_show_combat_result = false;
    bool found_camera_update_matrix = false;
    bool found_game_render_frame = false;
    bool found_d3d9_present = false;
    bool found_reserved_wndproc = false;
    for (const auto& hook : inventory) {
        assert(!hook.expected_prologue.empty());
        assert(hook.patch_span >= 5);
        if (hook.id == HookId::process_ui_event) {
            found_process_ui_event = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 8);
        }
        if (hook.id == HookId::handle_ui_message) {
            found_handle_ui_message = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
        }
        if (hook.id == HookId::gi_talk) {
            found_gi_talk = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 8);
        }
        if (hook.id == HookId::cegui_renderer_constructor_2) {
            found_cegui_renderer_ctor = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kCeguiRendererConstructor2);
        }
        if (hook.id == HookId::load_font_file) {
            found_load_font_file = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kLoadFontFile);
            assert(!hook.bootstrap_required);
            assert(hook.bootstrap_order == 900);
        }
        if (hook.id == HookId::setup_minimap_texture) {
            found_setup_minimap_texture = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kSetupMinimapTexture);
            assert(hook.bootstrap_order < 900);
        }
        if (hook.id == HookId::combat_console_set_image_position) {
            found_combat_console_set_image_position = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kCombatConsoleSetImageAndPosition);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::combat_console_set_image_position_2) {
            found_combat_console_set_image_position_2 = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kCombatConsoleSetImageAndPosition2);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::ui_show_combat_result) {
            found_ui_show_combat_result = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kUiShowCombatResult);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::camera_update_matrix) {
            found_camera_update_matrix = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kCameraUpdateMatrix);
        }
        if (hook.id == HookId::game_render_frame) {
            found_game_render_frame = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 5);
            assert(hook.ida_ea == pal4::inject::ida::kGameRenderFrame);
        }
        if (hook.id == HookId::d3d9_set_present_parameters) {
            found_d3d9_present = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 10);
            assert(hook.ida_ea == pal4::inject::ida::kD3d9SetPresentParameters);
        }
        if (hook.id == HookId::pal4_main_wndproc) {
            found_reserved_wndproc = true;
            assert(hook.patch_span == 8);
            assert(hook.bootstrap_order < 900);
        }
    }
    assert(found_process_ui_event);
    assert(found_handle_ui_message);
    assert(found_gi_talk);
    assert(found_cegui_renderer_ctor);
    assert(found_load_font_file);
    assert(found_setup_minimap_texture);
    assert(found_combat_console_set_image_position);
    assert(found_combat_console_set_image_position_2);
    assert(found_ui_show_combat_result);
    assert(found_camera_update_matrix);
    assert(found_game_render_frame);
    assert(found_d3d9_present);
    assert(found_reserved_wndproc);
}

void TestDpiAwarenessStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::unknown)) == "unknown");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::per_monitor_aware_v2)) == "per_monitor_aware_v2");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::per_monitor_aware)) == "per_monitor_aware");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::system_aware)) == "system_aware");
    assert(std::string(pal4::inject::ToString(pal4::inject::DpiAwarenessMode::already_set)) == "already_set");
}

void TestMsaaLevelStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::MsaaLevel::off)) == "off");
    assert(std::string(pal4::inject::ToString(pal4::inject::MsaaLevel::x2)) == "2x");
    assert(std::string(pal4::inject::ToString(pal4::inject::MsaaLevel::x4)) == "4x");
    assert(std::string(pal4::inject::ToString(pal4::inject::MsaaLevel::x8)) == "8x");

    pal4::inject::MsaaLevel parsed = pal4::inject::MsaaLevel::off;
    assert(pal4::inject::TryParseMsaaLevel("4x", &parsed));
    assert(parsed == pal4::inject::MsaaLevel::x4);
    assert(!pal4::inject::TryParseMsaaLevel("16x", &parsed));
}

void TestShadowResolutionStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::ShadowResolution::x64)) == "64");
    assert(std::string(pal4::inject::ToString(pal4::inject::ShadowResolution::x128)) == "128");
    assert(std::string(pal4::inject::ToString(pal4::inject::ShadowResolution::x256)) == "256");
    assert(std::string(pal4::inject::ToString(pal4::inject::ShadowResolution::x512)) == "512");

    pal4::inject::ShadowResolution parsed = pal4::inject::ShadowResolution::x64;
    assert(pal4::inject::TryParseShadowResolution("256", &parsed));
    assert(parsed == pal4::inject::ShadowResolution::x256);
    assert(!pal4::inject::TryParseShadowResolution("1024", &parsed));
}

void TestUiTextureFilterStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::UiTextureFilter::linear)) == "linear");
    assert(std::string(pal4::inject::ToString(pal4::inject::UiTextureFilter::nearest)) == "nearest");

    pal4::inject::UiTextureFilter parsed = pal4::inject::UiTextureFilter::linear;
    assert(pal4::inject::TryParseUiTextureFilter("nearest", &parsed));
    assert(parsed == pal4::inject::UiTextureFilter::nearest);
    assert(!pal4::inject::TryParseUiTextureFilter("point", &parsed));
}

void TestVrModeStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::VrMode::off)) == "off");
    assert(std::string(pal4::inject::ToString(pal4::inject::VrMode::seated_experimental)) ==
           "seated_experimental");

    pal4::inject::VrMode parsed = pal4::inject::VrMode::off;
    assert(pal4::inject::TryParseVrMode("seated_experimental", &parsed));
    assert(parsed == pal4::inject::VrMode::seated_experimental);
    assert(!pal4::inject::TryParseVrMode("openxr", &parsed));
}

void TestScriptModeStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::ScriptMode::inherit)) == "inherit");
    assert(std::string(pal4::inject::ToString(pal4::inject::ScriptMode::cs)) == "cs");
    assert(std::string(pal4::inject::ToString(pal4::inject::ScriptMode::csb)) == "csb");

    pal4::inject::ScriptMode parsed = pal4::inject::ScriptMode::inherit;
    assert(pal4::inject::TryParseScriptMode("cs", &parsed));
    assert(parsed == pal4::inject::ScriptMode::cs);
    assert(pal4::inject::TryParseScriptMode("csb", &parsed));
    assert(parsed == pal4::inject::ScriptMode::csb);
    assert(!pal4::inject::TryParseScriptMode("text", &parsed));

    const auto inherit_flag = pal4::inject::ScriptModeToCsbFlag(pal4::inject::ScriptMode::inherit);
    const auto cs_flag = pal4::inject::ScriptModeToCsbFlag(pal4::inject::ScriptMode::cs);
    const auto csb_flag = pal4::inject::ScriptModeToCsbFlag(pal4::inject::ScriptMode::csb);
    assert(!inherit_flag.has_value());
    assert(cs_flag.has_value() && *cs_flag == 0U);
    assert(csb_flag.has_value() && *csb_flag == 1U);

    constexpr std::uintptr_t kModuleBase = 0x10000000;
    const auto resolved = pal4::inject::ResolveScriptModeGlobalAddress(kModuleBase);
    assert(resolved == kModuleBase + (pal4::inject::ida::kIsCsbModeGlobal - pal4::inject::ida::kLaunchExeBase));
    assert(pal4::inject::ScriptModeFromCsbFlag(0) == pal4::inject::ScriptMode::cs);
    assert(pal4::inject::ScriptModeFromCsbFlag(1) == pal4::inject::ScriptMode::csb);
    assert(pal4::inject::ScriptModeFromCsbFlag(99) == pal4::inject::ScriptMode::csb);
}

void TestInheritedScriptModeOverride() {
    const auto original_required =
        GetEnvironmentVariableA(pal4::inject::kInjectedScriptModeEnvVar, nullptr, 0);
    std::optional<std::string> original_value;
    if (original_required != 0) {
        std::string buffer(static_cast<std::size_t>(original_required), '\0');
        const DWORD copied = GetEnvironmentVariableA(
            pal4::inject::kInjectedScriptModeEnvVar,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (copied != 0 && copied < buffer.size()) {
            original_value = std::string(buffer.data(), copied);
        }
    }

    const auto restore_env = [&]() {
        if (original_value.has_value()) {
            SetEnvironmentVariableA(
                pal4::inject::kInjectedScriptModeEnvVar,
                original_value->c_str());
        } else {
            SetEnvironmentVariableA(pal4::inject::kInjectedScriptModeEnvVar, nullptr);
        }
    };

    std::string error;
    SetEnvironmentVariableA(pal4::inject::kInjectedScriptModeEnvVar, nullptr);
    auto inherited = pal4::inject::LoadInheritedScriptModeOverride(&error);
    assert(!inherited.has_value());
    assert(error.empty());

    SetEnvironmentVariableA(pal4::inject::kInjectedScriptModeEnvVar, "cs");
    inherited = pal4::inject::LoadInheritedScriptModeOverride(&error);
    assert(inherited.has_value());
    assert(*inherited == pal4::inject::ScriptMode::cs);
    assert(error.empty());

    SetEnvironmentVariableA(pal4::inject::kInjectedScriptModeEnvVar, "csb");
    inherited = pal4::inject::LoadInheritedScriptModeOverride(&error);
    assert(inherited.has_value());
    assert(*inherited == pal4::inject::ScriptMode::csb);
    assert(error.empty());

    SetEnvironmentVariableA(pal4::inject::kInjectedScriptModeEnvVar, "inherit");
    inherited = pal4::inject::LoadInheritedScriptModeOverride(&error);
    assert(!inherited.has_value());
    assert(!error.empty());

    restore_env();
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

    command = {};
    command.kind = ProtocolCommandKind::fill_ui_ref;
    command.ui_ref = "e7";
    command.text = "hello world";
    assert(pal4::inject::ParseProtocolCommand(
        pal4::inject::FormatProtocolCommand(command),
        &parsed,
        &error));
    assert(parsed.kind == ProtocolCommandKind::fill_ui_ref);
    assert(parsed.ui_ref == "e7");
    assert(parsed.text == "hello world");

    command = {};
    command.kind = ProtocolCommandKind::write_memory;
    command.address_space = pal4::inject::AddressSpace::ida_ea;
    command.address = pal4::inject::ida::kIsCsbModeGlobal;
    command.hex_bytes = "01000000";
    command.unsafe_code_write = true;
    assert(pal4::inject::ParseProtocolCommand(
        pal4::inject::FormatProtocolCommand(command),
        &parsed,
        &error));
    assert(parsed.kind == ProtocolCommandKind::write_memory);
    assert(parsed.address_space == pal4::inject::AddressSpace::ida_ea);
    assert(parsed.address == pal4::inject::ida::kIsCsbModeGlobal);
    assert(parsed.hex_bytes == "01000000");
    assert(parsed.unsafe_code_write);

    command = {};
    command.kind = ProtocolCommandKind::set_vr_pose;
    command.vr_head_pose = {true, 1.5F, -2.5F, 0.25F, 0.1F, 0.2F, -0.3F};
    assert(pal4::inject::ParseProtocolCommand(
        pal4::inject::FormatProtocolCommand(command),
        &parsed,
        &error));
    assert(parsed.kind == ProtocolCommandKind::set_vr_pose);
    assert(parsed.vr_head_pose.active);
    assert(parsed.vr_head_pose.yaw_degrees > 1.4F && parsed.vr_head_pose.yaw_degrees < 1.6F);
    assert(parsed.vr_head_pose.offset_z < -0.2F && parsed.vr_head_pose.offset_z > -0.4F);

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

void TestDynamicFontOversamplePlan() {
    const auto dialog_plan =
        pal4::inject::BuildDynamicFontOversamplePlan("dialog_simsun", 20);
    assert(dialog_plan.apply);
    assert(dialog_plan.oversampled_point_size == 40);
    assert(dialog_plan.draw_scale == 0.5F);
    assert(dialog_plan.extent_scale == 0.5F);
    assert(dialog_plan.line_spacing_scale == 1.0F);
    assert(dialog_plan.baseline_scale == 1.0F);
    assert(pal4::inject::ComputeDialogRichTextGlyphHeight(dialog_plan, 40, 43.45F) > 21.7F);
    assert(pal4::inject::ComputeDialogRichTextGlyphHeight(dialog_plan, 40, 43.45F) < 21.8F);
    assert(pal4::inject::ComputeDialogRichTextGlyphHeight(dialog_plan, 40, 17.38F) == 17.38F);

    const auto system_plan =
        pal4::inject::BuildDynamicFontOversamplePlan("system", 13);
    assert(system_plan.apply);
    assert(system_plan.oversampled_point_size == 20);
    assert(system_plan.draw_scale == 0.65F);
    assert(system_plan.extent_scale == 0.65F);
    assert(system_plan.glyph_offset_y == 0.0F);
    assert(system_plan.observe_glyph_image_offsets);
    assert(system_plan.line_spacing_scale == 0.92F);
    assert(system_plan.baseline_scale == 1.0F);
    assert(system_plan.preserve_original_vertical_metrics);

    const auto system_bold_plan =
        pal4::inject::BuildDynamicFontOversamplePlan("systemBold", 13);
    assert(system_bold_plan.apply);
    assert(system_bold_plan.oversampled_point_size == 26);
    assert(system_bold_plan.draw_scale == 0.5F);
    assert(system_bold_plan.extent_scale == 0.5F);
    assert(system_bold_plan.glyph_offset_y == 0.0F);
    assert(!system_bold_plan.observe_glyph_image_offsets);
    assert(system_bold_plan.line_spacing_scale == 1.0F);
    assert(system_bold_plan.baseline_scale == 1.0F);
    assert(system_bold_plan.preserve_original_vertical_metrics);

    const auto zero_plan =
        pal4::inject::BuildDynamicFontOversamplePlan("dialog_simsun", 0);
    assert(!zero_plan.apply);

    assert(pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll", 0x17602, 2));
    assert(pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll", 0x17872, 2));
    assert(pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll", 0x17391, 2));
    assert(pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll", 0x17AE2, 2));
    assert(pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll", 0x17CA2, 2));
    assert(pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll+0x17ae2", 2));
    assert(pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll+0x17ca2", 2));
    assert(!pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "systemBold", "OIRAMLOOK.dll", 0x17602, 2));
    assert(!pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "CEGUIBase.dll", 0x17602, 2));
    assert(!pal4::inject::ShouldApplyOiramlookOlButtonTextRectYOffset(
        "system", "OIRAMLOOK.dll", 0x17602, 0));
    assert(pal4::inject::GetOiramlookOlButtonTextRectYOffset() == 2.0F);
}

void TestUiSnapshotSerialization() {
    pal4::inject::UiSnapshotTree tree{};
    tree.root.ref = "e1";
    tree.root.type = "gui_sheet";
    tree.root.name = "Desktop";
    tree.root.path = "Desktop";
    tree.root.visible = true;
    tree.root.enabled = true;

    pal4::inject::UiSnapshotNode child{};
    child.ref = "e2";
    child.type = "button";
    child.name = "BtnNewGame";
    child.path = "Desktop/BtnNewGame";
    child.text = "New Game";
    child.rect = {10, 20, 110, 60};
    child.visible = true;
    child.enabled = true;
    child.clickable = true;
    tree.root.children.push_back(child);

    const std::string payload = pal4::inject::SerializeUiSnapshotTree(tree);
    pal4::inject::UiSnapshotTree parsed{};
    std::string error;
    assert(pal4::inject::ParseUiSnapshotTree(payload, &parsed, &error));
    assert(pal4::inject::CountUiSnapshotNodes(parsed) == 2);
    const auto* by_ref = pal4::inject::FindUiSnapshotNodeByRef(parsed, "e2");
    assert(by_ref);
    assert(by_ref->name == "BtnNewGame");
    const auto* by_path = pal4::inject::FindUiSnapshotNodeByPath(parsed, "Desktop/BtnNewGame");
    assert(by_path);
    assert(by_path->clickable);
    assert(pal4::inject::UiSnapshotTreeContainsText(parsed, "Game"));
    const auto display = pal4::inject::FormatUiSnapshotTreeForDisplay(parsed);
    assert(display.find("[ref=e2]") != std::string::npos);
    assert(display.find("BtnNewGame") != std::string::npos);
}

void TestMemoryDebugHelpers() {
    pal4::inject::AddressSpace address_space = pal4::inject::AddressSpace::runtime_va;
    assert(pal4::inject::TryParseAddressSpace("ida_ea", &address_space));
    assert(address_space == pal4::inject::AddressSpace::ida_ea);

    pal4::inject::MemoryScalarType scalar_type = pal4::inject::MemoryScalarType::u32;
    assert(pal4::inject::TryParseMemoryScalarType("f64", &scalar_type));
    assert(scalar_type == pal4::inject::MemoryScalarType::f64);
    assert(pal4::inject::SizeOfMemoryScalarType(pal4::inject::MemoryScalarType::ptr) == 4);

    std::uint32_t address = 0;
    assert(pal4::inject::ParseAddressValue("0x1234", &address));
    assert(address == 0x1234U);

    std::vector<std::uint8_t> bytes;
    std::string error;
    assert(pal4::inject::ParseHexBytes("DEADBEEF", &bytes, &error));
    assert(bytes.size() == 4);
    assert(bytes[0] == 0xDE);
    assert(pal4::inject::FormatHexBytes(bytes) == "DEADBEEF");

    assert(pal4::inject::EncodeScalarValue(
        pal4::inject::MemoryScalarType::u32,
        "305419896",
        &bytes,
        &error));
    assert(bytes.size() == 4);
    std::string decoded;
    assert(pal4::inject::DecodeScalarValue(
        pal4::inject::MemoryScalarType::u32,
        bytes,
        &decoded,
        &error));
    assert(decoded == "305419896");
}

void TestMemoryRuntimeHelpers() {
    std::string error;
    auto* writable_page = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        4096,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE));
    assert(writable_page);
    std::memset(writable_page, 0x11, 16);
    const auto writable_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(writable_page));

    pal4::inject::MemoryRegionInfo region{};
    assert(pal4::inject::QueryMemoryRegion(
        pal4::inject::AddressSpace::runtime_va,
        writable_address,
        &region,
        &error));
    assert(region.readable);
    assert(region.writable);
    assert(!region.executable);

    std::vector<std::uint8_t> read_bytes;
    assert(pal4::inject::ReadMemoryRegion(
        pal4::inject::AddressSpace::runtime_va,
        writable_address,
        4,
        &read_bytes,
        &region,
        &error));
    assert(read_bytes.size() == 4);
    assert(read_bytes[0] == 0x11);

    std::vector<std::uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD};
    std::vector<std::uint8_t> before_bytes;
    std::vector<std::uint8_t> after_bytes;
    assert(pal4::inject::WriteMemoryRegion(
        pal4::inject::AddressSpace::runtime_va,
        writable_address,
        payload,
        false,
        &region,
        &before_bytes,
        &after_bytes,
        &error));
    assert(before_bytes[0] == 0x11);
    assert(after_bytes == payload);
    assert(std::memcmp(writable_page, payload.data(), payload.size()) == 0);
    VirtualFree(writable_page, 0, MEM_RELEASE);

    auto* executable_page = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        4096,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    assert(executable_page);
    executable_page[0] = 0x90;
    DWORD old_protect = 0;
    assert(VirtualProtect(executable_page, 4096, PAGE_EXECUTE_READ, &old_protect));
    const auto executable_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(executable_page));
    error.clear();
    assert(!pal4::inject::WriteMemoryRegion(
        pal4::inject::AddressSpace::runtime_va,
        executable_address,
        std::vector<std::uint8_t>{0xCC},
        false,
        &region,
        &before_bytes,
        &after_bytes,
        &error));
    assert(error.find("unsafe_code_write") != std::string::npos);
    assert(pal4::inject::WriteMemoryRegion(
        pal4::inject::AddressSpace::runtime_va,
        executable_address,
        std::vector<std::uint8_t>{0xCC},
        true,
        &region,
        &before_bytes,
        &after_bytes,
        &error));
    assert(executable_page[0] == 0xCC);
    VirtualFree(executable_page, 0, MEM_RELEASE);
}

void TestInjectControlPanelModel() {
    const auto rows = pal4::inject::BuildInjectControlPanelRows();
    assert(rows.size() == 15);

    const auto find_row =
        [&rows](const HookId id) -> const pal4::inject::InjectControlPanelRow* {
            for (const auto& row : rows) {
                if (row.id == id) {
                    return &row;
                }
            }
            return nullptr;
        };

    const auto* process_ui_row = find_row(HookId::process_ui_event);
    assert(process_ui_row);
    assert(process_ui_row->page == pal4::inject::InjectControlPanelPage::input_interaction);
    assert(process_ui_row->group_label == std::wstring_view(L"\u8f93\u5165\u4e0e\u4ea4\u4e92"));
    assert(process_ui_row->label == std::wstring_view(L"\u83dc\u5355\u70b9\u51fb\u4e0e\u6309\u952e\u63a5\u7ba1"));
    const auto* vr_row = find_row(HookId::game_render_frame);
    assert(vr_row);
    assert(vr_row->page == pal4::inject::InjectControlPanelPage::camera);
    assert(process_ui_row->hook_name == std::string_view("process_ui_event"));
    assert(process_ui_row->allow_mode_change);

    const auto* wndproc_row = find_row(HookId::pal4_main_wndproc);
    assert(wndproc_row);
    assert(wndproc_row->allow_mode_change);

    const auto* process_inputs_row = find_row(HookId::process_inputs);
    assert(process_inputs_row);
    assert(process_inputs_row->label == std::wstring_view(L"\u624b\u67c4\u8f6e\u8be2\u4e0e HUD \u5237\u65b0"));
    assert(process_inputs_row->allow_mode_change);

    assert(!find_row(HookId::update_input_device_state));
    assert(!find_row(HookId::initialize_direct_input));
    assert(!find_row(HookId::handle_player_input_events));
    assert(!find_row(HookId::cegui_system_initialize));
    assert(!find_row(HookId::dialog_handle_text_display));

    const auto* gi_talk_row = find_row(HookId::gi_talk);
    assert(gi_talk_row);
    assert(gi_talk_row->page == pal4::inject::InjectControlPanelPage::script_runtime);
    assert(gi_talk_row->group_label == std::wstring_view(L"\u811a\u672c\u4e0e\u5267\u60c5"));
    assert(gi_talk_row->hook_name == std::string_view("gi_talk"));

    const auto* load_font_file_row = find_row(HookId::load_font_file);
    assert(load_font_file_row);
    assert(load_font_file_row->page == pal4::inject::InjectControlPanelPage::ui_resolution);
    assert(load_font_file_row->hook_name == std::string_view("load_font_file"));
    assert(load_font_file_row->group_label == std::wstring_view(L"UI \u5206\u8fa8\u7387\u81ea\u9002\u5e94"));

    const auto* renderer_row = find_row(HookId::cegui_renderer_constructor_2);
    assert(renderer_row);
    assert(renderer_row->page == pal4::inject::InjectControlPanelPage::ui_resolution);
    assert(renderer_row->group_label == std::wstring_view(L"UI \u5206\u8fa8\u7387\u81ea\u9002\u5e94"));
    assert(renderer_row->hook_name == std::string_view("cegui_renderer_constructor_2"));

    const auto* combat_number_row = find_row(HookId::combat_console_set_image_position);
    assert(combat_number_row);
    assert(!combat_number_row->label.empty());
    assert(combat_number_row->allow_mode_change);

    const auto* combat_result_row = find_row(HookId::ui_show_combat_result);
    assert(combat_result_row);
    assert(combat_result_row->group_label == std::wstring_view(L"UI \u5206\u8fa8\u7387\u81ea\u9002\u5e94"));

    const auto* camera_row = find_row(HookId::camera_update_matrix);
    assert(camera_row);
    assert(camera_row->page == pal4::inject::InjectControlPanelPage::camera);
    assert(camera_row->group_label == std::wstring_view(L"\u76f8\u673a"));

    const auto modes = pal4::inject::BuildInjectControlPanelModes();
    assert(modes.size() == 2);
    assert(modes[0] == pal4::inject::HookMode::observe_only);
    assert(modes[1] == pal4::inject::HookMode::replace_with_fallback);
    assert(pal4::inject::BuildInjectControlPanelPageLabel(pal4::inject::InjectControlPanelPage::overview) ==
           std::wstring_view(L"\u6982\u89c8"));
    assert(pal4::inject::BuildInjectControlPanelPageLabel(pal4::inject::InjectControlPanelPage::ui_resolution) ==
           std::wstring_view(L"UI \u5206\u8fa8\u7387\u81ea\u9002\u5e94"));
    assert(pal4::inject::BuildInjectControlPanelPageLabel(pal4::inject::InjectControlPanelPage::render_visual) ==
           std::wstring_view(L"\u6e32\u67d3\u4e0e\u753b\u9762"));
    assert(pal4::inject::BuildInjectControlPanelPageLabel(pal4::inject::InjectControlPanelPage::input_interaction) ==
           std::wstring_view(L"\u8f93\u5165\u4e0e\u4ea4\u4e92"));
    assert(pal4::inject::BuildInjectControlPanelPageLabel(pal4::inject::InjectControlPanelPage::script_runtime) ==
           std::wstring_view(L"\u811a\u672c\u4e0e\u5267\u60c5"));
    assert(pal4::inject::BuildInjectControlPanelModeLabel(pal4::inject::HookMode::observe_only) ==
           std::wstring_view(L"\u4ec5\u89c2\u5bdf"));
    assert(pal4::inject::BuildInjectControlPanelModeLabel(pal4::inject::HookMode::replace_with_fallback) ==
           std::wstring_view(L"\u66ff\u6362\uff08\u53ef\u56de\u9000\uff09"));
    assert(pal4::inject::BuildInjectControlPanelModeLabel(pal4::inject::HookMode::replace_strict) ==
           std::wstring_view(L"\u66ff\u6362\uff08\u53ef\u56de\u9000\uff09"));
    assert(pal4::inject::FindInjectControlPanelModeIndex(pal4::inject::HookMode::mirror_compare) == 0);
    assert(pal4::inject::InjectControlPanelModeFromIndex(1) == pal4::inject::HookMode::replace_with_fallback);
    assert(pal4::inject::InjectControlPanelModeFromIndex(99) == pal4::inject::HookMode::observe_only);
}

void TestInjectSettingsRoundTrip() {
    pal4::inject::InjectPersistedSettings settings{};
    settings.msaa_level = pal4::inject::MsaaLevel::x4;
    settings.shadow_resolution = pal4::inject::ShadowResolution::x256;
    settings.ui_texture_filter = pal4::inject::UiTextureFilter::linear;
    settings.vr_mode = pal4::inject::VrMode::seated_experimental;
    settings.launcher_script_mode = pal4::inject::ScriptMode::cs;
    settings.dialog_font_hd_enabled = false;
    settings.system_font_oversample_enabled = true;
    settings.gamepad_enabled = true;
    settings.gamepad_log_enabled = false;
    settings.hooks.push_back({
        HookId::process_ui_event,
        pal4::inject::HookMode::replace_with_fallback,
        pal4::inject::HookMode::replace_with_fallback,
        true,
    });
    settings.hooks.push_back({
        HookId::d3d9_set_present_parameters,
        pal4::inject::HookMode::observe_only,
        pal4::inject::HookMode::replace_with_fallback,
        false,
    });
    settings.hooks.push_back({
        HookId::load_font_file,
        pal4::inject::HookMode::replace_with_fallback,
        pal4::inject::HookMode::replace_with_fallback,
        true,
    });

    std::string error;
    const auto text = pal4::inject::FormatInjectPersistedSettings(settings);
    pal4::inject::InjectPersistedSettings parsed{};
    assert(pal4::inject::ParseInjectPersistedSettings(text, &parsed, &error));
    assert(parsed.msaa_level == pal4::inject::MsaaLevel::x4);
    assert(parsed.shadow_resolution == pal4::inject::ShadowResolution::x256);
    assert(parsed.ui_texture_filter == pal4::inject::UiTextureFilter::linear);
    assert(parsed.vr_mode == pal4::inject::VrMode::seated_experimental);
    assert(parsed.launcher_script_mode == pal4::inject::ScriptMode::cs);
    assert(!parsed.dialog_font_hd_enabled);
    assert(parsed.system_font_oversample_enabled);
    assert(parsed.gamepad_enabled);
    assert(!parsed.gamepad_log_enabled);
    assert(parsed.hooks.size() == 3);
    const auto find_hook =
        [&parsed](const HookId id) -> const pal4::inject::PersistedHookSetting* {
            for (const auto& hook : parsed.hooks) {
                if (hook.id == id) {
                    return &hook;
                }
            }
            return nullptr;
        };
    const auto* process_ui_event = find_hook(HookId::process_ui_event);
    assert(process_ui_event);
    assert(process_ui_event->mode == pal4::inject::HookMode::replace_with_fallback);
    const auto* d3d9_present = find_hook(HookId::d3d9_set_present_parameters);
    assert(d3d9_present);
    assert(d3d9_present->active_mode == pal4::inject::HookMode::replace_with_fallback);
    assert(!d3d9_present->log_enabled);
    const auto* load_font_file = find_hook(HookId::load_font_file);
    assert(load_font_file);
    assert(load_font_file->mode == pal4::inject::HookMode::replace_with_fallback);
    assert(load_font_file->log_enabled);

    const auto temp_path =
        std::filesystem::temp_directory_path() / "pal4_inject_settings_unit_test.ini";
    assert(pal4::inject::SaveInjectPersistedSettings(temp_path, settings, &error));
    pal4::inject::InjectPersistedSettings loaded{};
    assert(pal4::inject::LoadInjectPersistedSettings(temp_path, &loaded, &error));
    assert(loaded.msaa_level == pal4::inject::MsaaLevel::x4);
    assert(loaded.shadow_resolution == pal4::inject::ShadowResolution::x256);
    assert(loaded.ui_texture_filter == pal4::inject::UiTextureFilter::linear);
    assert(loaded.vr_mode == pal4::inject::VrMode::seated_experimental);
    assert(loaded.launcher_script_mode == pal4::inject::ScriptMode::cs);
    assert(!loaded.dialog_font_hd_enabled);
    assert(loaded.system_font_oversample_enabled);
    assert(loaded.gamepad_enabled);
    assert(!loaded.gamepad_log_enabled);
    std::filesystem::remove(temp_path);

    const std::string legacy_settings =
        "version=3\n"
        "msaa_level=2x\n"
        "hd_shadow_enabled=1\n"
        "ui_texture_filter=nearest\n";
    pal4::inject::InjectPersistedSettings legacy{};
    assert(pal4::inject::ParseInjectPersistedSettings(legacy_settings, &legacy, &error));
    assert(legacy.msaa_level == pal4::inject::MsaaLevel::x2);
    assert(legacy.shadow_resolution == pal4::inject::ShadowResolution::x256);
    assert(legacy.ui_texture_filter == pal4::inject::UiTextureFilter::nearest);
    assert(legacy.dialog_font_hd_enabled);
}

void TestGamepadHelpers() {
    assert(std::string(pal4::inject::ToString(pal4::inject::GamepadInputContext::gameplay)) == "gameplay");
    assert(std::string(pal4::inject::ToString(pal4::inject::GamepadInputContext::system_menu)) == "system_menu");

    pal4::inject::GamepadInputContext parsed = pal4::inject::GamepadInputContext::gameplay;
    assert(pal4::inject::TryParseGamepadInputContext("menu", &parsed));
    assert(parsed == pal4::inject::GamepadInputContext::menu);
    assert(!pal4::inject::TryParseGamepadInputContext("unknown", &parsed));

    const auto idle = pal4::inject::BuildGamepadDigitalAxes(1000, 1000, 5000);
    assert(!idle.up && !idle.down && !idle.left && !idle.right);

    const auto forward_left = pal4::inject::BuildGamepadDigitalAxes(-20000, 20000, 5000);
    assert(forward_left.up);
    assert(forward_left.left);
    assert(!forward_left.down);
    assert(!forward_left.right);

    assert(pal4::inject::WrapGamepadCycleIndex(0, -1, 7) == 6);
    assert(pal4::inject::WrapGamepadCycleIndex(6, 1, 7) == 0);
    assert(pal4::inject::WrapGamepadCycleIndex(0, -1, 6) == 5);

    pal4::inject::GamepadRepeatState repeat{};
    assert(pal4::inject::ConsumeGamepadRepeat(true, 100, 300, 100, &repeat));
    assert(!pal4::inject::ConsumeGamepadRepeat(true, 200, 300, 100, &repeat));
    assert(pal4::inject::ConsumeGamepadRepeat(true, 400, 300, 100, &repeat));
    assert(!pal4::inject::ConsumeGamepadRepeat(false, 450, 300, 100, &repeat));
    assert(pal4::inject::ConsumeGamepadRepeat(true, 500, 300, 100, &repeat));
}

void TestInputLogic() {
    assert(pal4::inject::NormalizeProcessUiEventKeyDown(17) == 200);
    assert(pal4::inject::NormalizeProcessUiEventKeyDown(30) == 203);
    assert(pal4::inject::NormalizeProcessUiEventKeyDown(57) == 28);
    assert(!pal4::inject::ShouldSuppressMappedUiKey(1));
    assert(!pal4::inject::ShouldSuppressMappedUiKey(57));

    const auto key_down = pal4::inject::BuildUiInjectedPlan(WM_KEYDOWN, 17, 0);
    assert(key_down.action == UiInjectedAction::key_down);
    assert(key_down.code == 200);

    const auto escape_down = pal4::inject::BuildUiInjectedPlan(WM_KEYDOWN, 1, 0);
    assert(escape_down.action == UiInjectedAction::key_down);
    assert(escape_down.code == 1);

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
    assert(!state.GetHookLogEnabled(HookId::process_ui_event));
    assert(!state.GetHookLogEnabled(HookId::load_font_file));
    state.SetMsaaLevel(pal4::inject::MsaaLevel::x2);
    state.SetShadowResolution(pal4::inject::ShadowResolution::x256);
    state.SetUiTextureFilter(pal4::inject::UiTextureFilter::linear);
    state.SetVrMode(pal4::inject::VrMode::seated_experimental);
    state.SetVrHeadPose({true, 3.0F, -2.0F, 1.0F, 0.1F, 0.2F, -0.3F});
    state.SetVrCameraState({true, 0x1234, 0x5678, 12.0F, 34.0F, 56.0F, 78.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F});
    state.SetDialogFontHdEnabled(false);
    state.SetSystemFontOversampleEnabled(true);
    state.AppendEventLog("event-1");
    state.AppendEventLog("event-2");
    state.SetCrashHandlerReady(true);
    state.SetGamepadEnabled(true);
    state.SetGamepadLogEnabled(false);
    state.SetGamepadConnected(true);
    state.SetGamepadContext(pal4::inject::GamepadInputContext::system_menu);
    state.SetCrashArtifacts("summary", "report.txt", "dump.dmp");
    state.SetLastFontSync("hook=load_font_file action=resynced", true);
    state.IncrementHookCall(HookId::process_ui_event);
    state.ObservePalivEntry(2);
    assert(state.WaitForHookCalls(HookId::process_ui_event, 1, 10));
    assert(state.WaitForPalivEntry(2, 10));
    const auto tail = state.BuildEventLogTail();
    assert(tail.find("event-1") != std::string::npos);
    assert(tail.find("event-2") != std::string::npos);
    const auto snapshot = state.BuildSnapshot(0);
    assert(snapshot.crash_handler_ready);
    assert(snapshot.vr_mode == pal4::inject::VrMode::seated_experimental);
    assert(snapshot.vr_head_pose.active);
    assert(snapshot.vr_head_pose.yaw_degrees == 3.0F);
    assert(snapshot.vr_camera_state.valid);
    assert(snapshot.vr_camera_state.camera_internal == 0x5678);
    assert(snapshot.msaa_level == pal4::inject::MsaaLevel::x2);
    assert(snapshot.shadow_resolution == pal4::inject::ShadowResolution::x256);
    assert(snapshot.ui_texture_filter == pal4::inject::UiTextureFilter::linear);
    assert(!snapshot.dialog_font_hd_enabled);
    assert(snapshot.system_font_oversample_enabled);
    assert(snapshot.gamepad_enabled);
    assert(!snapshot.gamepad_log_enabled);
    assert(snapshot.gamepad_connected);
    assert(snapshot.gamepad_context == pal4::inject::GamepadInputContext::system_menu);
    assert(snapshot.last_crash_summary == "summary");
    assert(snapshot.last_crash_report_path == "report.txt");
    assert(snapshot.last_crash_dump_path == "dump.dmp");
    assert(snapshot.last_font_sync_ok);
    assert(snapshot.last_font_sync_summary == "hook=load_font_file action=resynced");
}

void ConfigureNonInteractiveCrashDialogs() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
#endif
}

void TestLauncherNaming() {
    assert(pal4::inject::BuildReadyEventName(1234) == "Local\\PAL4InjectReady_1234");
    assert(pal4::inject::BuildPipeName(1234) == "\\\\.\\pipe\\pal4_inject_1234");
    assert(std::string_view(pal4::inject::kPal4InjectBuildId).size() >= 8);

    pal4::inject::LaunchOptions options{};
    assert(options.script_mode == pal4::inject::ScriptMode::inherit);
    options.game_root = "I:\\Games\\PAL4_game";
    std::filesystem::path exe_path;
    std::filesystem::path workdir;
    std::string error;
    assert(pal4::inject::ResolveLaunchPaths(options, &exe_path, &workdir, &error));
    assert(exe_path == std::filesystem::path("I:\\Games\\PAL4_game\\launch.exe"));
    assert(workdir == std::filesystem::path("I:\\Games\\PAL4_game"));

    const auto direct_exe =
        std::filesystem::temp_directory_path() / "pal4_inject_launcher_target.exe";
    {
        std::ofstream output(direct_exe, std::ios::binary | std::ios::trunc);
        output << "stub";
    }
    options = {};
    options.executable_path = direct_exe;
    assert(pal4::inject::ResolveLaunchPaths(options, &exe_path, &workdir, &error));
    assert(exe_path == direct_exe);
    assert(workdir == direct_exe.parent_path());
    std::filesystem::remove(direct_exe);
}

void TestInjectControlPanelLabels() {
    const auto rows = pal4::inject::BuildInjectControlPanelRows();
    auto find_label = [&rows](const pal4::inject::HookId id) -> std::wstring_view {
        for (const auto& row : rows) {
            if (row.id == id) {
                return row.label;
            }
        }
        return {};
    };

    assert(find_label(pal4::inject::HookId::process_ui_event) == L"\u83dc\u5355\u70b9\u51fb\u4e0e\u6309\u952e\u63a5\u7ba1");
    assert(find_label(pal4::inject::HookId::handle_ui_message) == L"\u83dc\u5355\u6d88\u606f\u8865\u6295\u9012");
    assert(find_label(pal4::inject::HookId::process_inputs) == L"\u624b\u67c4\u8f6e\u8be2\u4e0e HUD \u5237\u65b0");
    assert(find_label(pal4::inject::HookId::load_font_file) == L"\u9ad8\u6e05\u5b57\u4f53\u4e0e\u7f29\u653e\u91cd\u540c\u6b65");
    assert(find_label(pal4::inject::HookId::d3d9_set_present_parameters) ==
        L"\u6297\u952f\u9f7f\u8bbe\u5907\u53c2\u6570\u8986\u5199");
    assert(find_label(pal4::inject::HookId::update_input_device_state).empty());
    assert(find_label(pal4::inject::HookId::initialize_direct_input).empty());
    assert(find_label(pal4::inject::HookId::handle_player_input_events).empty());
    assert(find_label(pal4::inject::HookId::cegui_system_initialize).empty());
    assert(find_label(pal4::inject::HookId::dialog_handle_text_display).empty());
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
    const float centered_ui_x =
        pal4::inject::ComputeCenteredUiLogicalX(plan_1920_1080, 102.0F);
    assert(centered_ui_x > 235.33F && centered_ui_x < 235.34F);

    const auto plan_1920_1080_native_wide =
        pal4::inject::BuildCeguiWidescreenPlanForLogicalSize(
            1920,
            1080,
            1920.0F / 1.8F,
            600.0F);
    assert(plan_1920_1080_native_wide.apply);
    assert(!plan_1920_1080_native_wide.use_original_variant);
    assert(plan_1920_1080_native_wide.uniform_scale == 1.8F);
    assert(plan_1920_1080_native_wide.horizontal_bias_pixels > -0.001F);
    assert(plan_1920_1080_native_wide.horizontal_bias_pixels < 0.001F);
    assert(plan_1920_1080_native_wide.logical_horizontal_padding > -0.001F);
    assert(plan_1920_1080_native_wide.logical_horizontal_padding < 0.001F);
    assert(pal4::inject::ComputeCenteredUiLogicalX(
        plan_1920_1080_native_wide,
        102.0F) == 102.0F);

    const auto plan_1920_1080_root_1067 =
        pal4::inject::BuildCeguiWidescreenPlanForLogicalSize(
            1920,
            1080,
            1067.0F,
            600.0F);
    assert(plan_1920_1080_root_1067.apply);
    assert(plan_1920_1080_root_1067.horizontal_bias_pixels > -0.5F);
    assert(plan_1920_1080_root_1067.horizontal_bias_pixels < 0.5F);

    const float minimap_logical_x = pal4::inject::ComputeWidescreenHudLogicalX(
        plan_1920_1080,
        0.0F,
        pal4::inject::WidescreenHudAnchor::left_edge);
    assert(minimap_logical_x < -133.3F && minimap_logical_x > -133.4F);
    const float minimap_screen_x =
        pal4::inject::ProjectWidescreenLogicalXToPhysicalPixels(
            plan_1920_1080,
            minimap_logical_x);
    assert(minimap_screen_x > -0.001F && minimap_screen_x < 0.001F);

    const float portrait_logical_x = pal4::inject::ComputeWidescreenHudLogicalX(
        plan_1920_1080,
        704.0F,
        pal4::inject::WidescreenHudAnchor::right_edge);
    assert(portrait_logical_x > 837.33F && portrait_logical_x < 837.34F);
    const float portrait_screen_x =
        pal4::inject::ProjectWidescreenLogicalXToPhysicalPixels(
            plan_1920_1080,
            portrait_logical_x);
    assert(portrait_screen_x > 1747.19F && portrait_screen_x < 1747.21F);

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
    assert(minimap_1920_1080.x == 4);
    assert(minimap_1920_1080.y == 719);
    assert(minimap_1920_1080.width == 311);
    assert(minimap_1920_1080.height == 311);

    const auto minimap_1280_800 = pal4::inject::BuildWidescreenMinimapPlacement(1280, 800);
    assert(!minimap_1280_800.apply);
}

void TestCeguiDynamicFontResyncMath() {
    assert(pal4::inject::IsKnownDynamicUiFont("system"));
    assert(pal4::inject::IsKnownDynamicUiFont("SystemBold"));
    assert(pal4::inject::IsKnownDynamicUiFont("DIALOG_SIMSUN"));
    assert(!pal4::inject::IsKnownDynamicUiFont("unknown_font"));

    assert(
        pal4::inject::CanonicalKnownDynamicUiFontName("system") ==
        std::string_view("system"));
    assert(
        pal4::inject::CanonicalKnownDynamicUiFontName("SystemBold") ==
        std::string_view("systemBold"));
    assert(
        pal4::inject::CanonicalKnownDynamicUiFontName("dialog_simsun") ==
        std::string_view("dialog_simsun"));
    assert(
        pal4::inject::BuildKnownDynamicUiFontAtlasName("system") ==
        std::string("system_auto_glyph_images"));
    assert(
        pal4::inject::BuildKnownDynamicUiFontAtlasName("SystemBold") ==
        std::string("systemBold_auto_glyph_images"));
    assert(
        pal4::inject::BuildKnownDynamicUiFontAtlasName("dialog_simsun") ==
        std::string("dialog_simsun_auto_glyph_images"));
    assert(pal4::inject::BuildKnownDynamicUiFontAtlasName("unknown_font").empty());

    const auto target_1920_1080 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "system",
            pal4::inject::BuildCeguiWidescreenPlan(1920, 1080));
    assert(target_1920_1080.apply);
    assert(target_1920_1080.native_width == 800.0F);
    assert(target_1920_1080.native_height == 600.0F);
    assert(target_1920_1080.oversample_scale == 2.0F);
    assert(target_1920_1080.notify_width == 2880.0F);
    assert(target_1920_1080.notify_height == 2160.0F);

    const auto target_1600_900 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "systemBold",
            pal4::inject::BuildCeguiWidescreenPlan(1600, 900));
    assert(target_1600_900.apply);
    assert(target_1600_900.native_width == 800.0F);
    assert(target_1600_900.native_height == 600.0F);
    assert(target_1600_900.oversample_scale == 2.0F);
    assert(target_1600_900.notify_width == 2400.0F);
    assert(target_1600_900.notify_height == 1800.0F);

    const auto target_1024_768 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "system",
            pal4::inject::BuildCeguiWidescreenPlan(1024, 768));
    assert(!target_1024_768.apply);
    assert(target_1024_768.notify_width == 800.0F);
    assert(target_1024_768.notify_height == 600.0F);
    assert(target_1024_768.oversample_scale == 1.0F);

    const auto dialog_target_1920_1080 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "dialog_simsun",
            pal4::inject::BuildCeguiWidescreenPlan(1920, 1080));
    assert(dialog_target_1920_1080.apply);
    assert(dialog_target_1920_1080.native_width == 800.0F);
    assert(dialog_target_1920_1080.native_height == 600.0F);
    assert(dialog_target_1920_1080.oversample_scale == 2.0F);
    assert(dialog_target_1920_1080.notify_width == 2880.0F);
    assert(dialog_target_1920_1080.notify_height == 2160.0F);
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

pal4::inject::UiSnapshotTree ReadUiSnapshot(const std::string& pipe_name) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::snapshot_ui;
    const auto response = SendCommand(pipe_name, command);
    assert(response.ok);
    const auto tree_it = response.fields.find("tree");
    assert(tree_it != response.fields.end());

    pal4::inject::UiSnapshotTree tree{};
    std::string error;
    const bool parsed = pal4::inject::ParseUiSnapshotTree(tree_it->second, &tree, &error);
    if (!parsed) {
        std::cerr << "ParseUiSnapshotTree failed: " << error << "\n";
    }
    assert(parsed);
    return tree;
}

const pal4::inject::UiSnapshotNode* FindUiNodeByName(
    const pal4::inject::UiSnapshotNode& node,
    const std::string_view name) {
    if (node.name == name) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* found = FindUiNodeByName(child, name)) {
            return found;
        }
    }
    return nullptr;
}

std::string WaitForUiNodeRefByName(
    const std::string& pipe_name,
    const std::string_view name,
    const DWORD timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto tree = ReadUiSnapshot(pipe_name);
        if (const auto* node = FindUiNodeByName(tree.root, name)) {
            return node->ref;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return {};
}

void ClickUiRef(const std::string& pipe_name, const std::string& ref) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::click_ui_ref;
    command.ui_ref = ref;
    const auto response = SendCommand(pipe_name, command);
    if (!response.ok) {
        std::cerr << "click_ui_ref failed: ref=" << ref
                  << " message=" << response.message << "\n";
    }
    assert(response.ok);
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

ProtocolResponse ReadMemoryViaProtocol(
    const std::string& pipe_name,
    const pal4::inject::AddressSpace address_space,
    const std::uint32_t address,
    const std::uint32_t size) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::read_memory;
    command.address_space = address_space;
    command.address = address;
    command.size = size;
    return SendCommand(pipe_name, command);
}

ProtocolResponse WriteMemoryViaProtocol(
    const std::string& pipe_name,
    const pal4::inject::AddressSpace address_space,
    const std::uint32_t address,
    const std::string_view bytes,
    const bool unsafe_code_write) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::write_memory;
    command.address_space = address_space;
    command.address = address;
    command.hex_bytes = std::string(bytes);
    command.unsafe_code_write = unsafe_code_write;
    return SendCommand(pipe_name, command);
}

void TestSkipLogoToMenuScenario() {
    auto harness = LaunchHarness();
    assert(WaitForSnapshotField(harness.pipe_name, "bootstrap_ready", "1", 5000));
    assert(WaitForHookCountByPolling(
        harness.pipe_name,
        HookId::load_font_file,
        1,
        15000));
    assert(WaitForSnapshotField(harness.pipe_name, "last_font_sync_ok", "1", 5000));
    const auto new_game_ref = WaitForUiNodeRefByName(harness.pipe_name, "BtnNewGame", 20000);
    const auto exit_ref = WaitForUiNodeRefByName(harness.pipe_name, "BtnExit", 20000);
    assert(!new_game_ref.empty());
    assert(!exit_ref.empty());
    CleanupHarness(&harness);
}

void TestMenuNewGameTransitionScenario() {
    auto harness = LaunchHarness();
    const auto new_game_ref = WaitForUiNodeRefByName(harness.pipe_name, "BtnNewGame", 20000);
    assert(!new_game_ref.empty());
    ClickUiRef(harness.pipe_name, new_game_ref);

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
    const auto exit_ref = WaitForUiNodeRefByName(harness.pipe_name, "BtnExit", 20000);
    assert(!exit_ref.empty());
    ClickUiRef(harness.pipe_name, exit_ref);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto confirm_ref = WaitForUiNodeRefByName(harness.pipe_name, "BtnModel", 10000);
    assert(!confirm_ref.empty());
    ClickUiRef(harness.pipe_name, confirm_ref);

    if (!WaitForProcessExit(harness.process.process_info.hProcess, 3000)) {
        SendWindowClose(harness.pipe_name);
    }
    assert(WaitForProcessExit(harness.process.process_info.hProcess, 10000));
    harness.process.Close();
}

void TestMemoryDebugScenario() {
    auto harness = LaunchHarness();
    assert(WaitForSnapshotField(harness.pipe_name, "bootstrap_ready", "1", 5000));
    const auto state = ReadUiState(harness.pipe_name);
    const auto module_base_it = state.fields.find("main_module_base");
    assert(module_base_it != state.fields.end());

    std::uint32_t module_base = 0;
    assert(pal4::inject::ParseAddressValue(module_base_it->second, &module_base));
    const auto runtime_address = static_cast<std::uint32_t>(
        pal4::inject::ResolveScriptModeGlobalAddress(module_base));

    const auto by_ida = ReadMemoryViaProtocol(
        harness.pipe_name,
        pal4::inject::AddressSpace::ida_ea,
        pal4::inject::ida::kIsCsbModeGlobal,
        4);
    assert(by_ida.ok);
    const auto by_va = ReadMemoryViaProtocol(
        harness.pipe_name,
        pal4::inject::AddressSpace::runtime_va,
        runtime_address,
        4);
    assert(by_va.ok);
    assert(by_ida.fields.at("bytes") == by_va.fields.at("bytes"));

    std::vector<std::uint8_t> current_bytes;
    std::string error;
    assert(pal4::inject::ParseHexBytes(by_ida.fields.at("bytes"), &current_bytes, &error));
    const bool enable_csb = current_bytes[0] == 0;
    const std::string new_value = enable_csb ? "01000000" : "00000000";
    const auto write_response = WriteMemoryViaProtocol(
        harness.pipe_name,
        pal4::inject::AddressSpace::runtime_va,
        runtime_address,
        new_value,
        false);
    assert(write_response.ok);
    const auto verify_response = ReadMemoryViaProtocol(
        harness.pipe_name,
        pal4::inject::AddressSpace::runtime_va,
        runtime_address,
        4);
    assert(verify_response.ok);
    assert(verify_response.fields.at("bytes") == new_value);

    const auto restore_response = WriteMemoryViaProtocol(
        harness.pipe_name,
        pal4::inject::AddressSpace::runtime_va,
        runtime_address,
        by_ida.fields.at("bytes"),
        false);
    assert(restore_response.ok);

    const auto unsafe_rejected = WriteMemoryViaProtocol(
        harness.pipe_name,
        pal4::inject::AddressSpace::ida_ea,
        pal4::inject::ida::kProcessUiEvent,
        "90",
        false);
    assert(!unsafe_rejected.ok);
    assert(unsafe_rejected.message.find("unsafe_code_write") != std::string::npos);

    CleanupHarness(&harness);
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
    TestMemoryDebugScenario();

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
    ConfigureNonInteractiveCrashDialogs();
    TestResolveRuntimeAddress();
    TestHookInventory();
    TestDpiAwarenessStrings();
    TestMsaaLevelStrings();
    TestShadowResolutionStrings();
    TestUiTextureFilterStrings();
    TestVrModeStrings();
    TestScriptModeStrings();
    TestInheritedScriptModeOverride();
    TestInjectControlPanelModel();
    TestInjectSettingsRoundTrip();
    TestGamepadHelpers();
    TestProtocolRoundTrip();
    TestDynamicFontOversamplePlan();
    TestUiSnapshotSerialization();
    TestMemoryDebugHelpers();
    TestMemoryRuntimeHelpers();
    TestInputLogic();
    TestInputQueue();
    TestRuntimeEventLog();
    TestLauncherNaming();
    TestInjectControlPanelLabels();
    TestCameraPitchUnlockPatchMetadata();
    TestCameraPitchGuardMath();
    TestCeguiWidescreenPlanMath();
    TestCeguiDynamicFontResyncMath();
    TestCrashCaptureHelpers();
    MaybeRunIntegrationSmoke();
    std::cout << "pal4_inject_tests: ok\n";
    return 0;
}

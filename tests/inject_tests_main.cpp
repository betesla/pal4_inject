#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <chrono>
#include <cmath>
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
#include "pal4inject/aspect_ratio_layout.h"
#include "pal4inject/borderless_window.h"
#include "pal4inject/ida_addresses.h"
#include "pal4inject/dpi_awareness.h"
#include "pal4inject/dialogue_voice_volume.h"
#include "pal4inject/inject_feature_catalog.h"
#include "pal4inject/inject_settings.h"
#include "pal4inject/input_logic.h"
#include "pal4inject/input_queue.h"
#include "pal4inject/launcher.h"
#include "pal4inject/loose_file_overlay.h"
#include "pal4inject/main_menu_branding.h"
#include "pal4inject/camera_pitch_guard.h"
#include "pal4inject/cegui_font_resync.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/widescreen_ui_layout.h"
#include "pal4inject/ui_coordinate_space.h"
#include "pal4inject/ui_input_plan.h"
#include "pal4inject/camera_unlock_patch.h"
#include "pal4inject/bug_report.h"
#include "pal4inject/crash_capture.h"
#include "pal4inject/memory_debug.h"
#include "pal4inject/protocol.h"
#include "pal4inject/runtime_paths.h"
#include "pal4inject/script_mode_override.h"
#include "pal4inject/ui_snapshot.h"
#include "memory_debug_runtime.h"
#include "hook_manager.h"
#include "loose_file_load_log.h"
#include "runtime_state.h"
#include "widescreen_ui_profiles.h"
#include "x86_trampoline.h"
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

void TestPackagedRuntimeLayoutPaths() {
    const std::filesystem::path install_dir = R"(I:\Games\original)";
    assert(
        pal4::inject::PackagedPayloadDirectory(install_dir) ==
        install_dir / "pal4_inject");
    assert(
        pal4::inject::PackagedRuntimeDllPath(install_dir) ==
        install_dir / "pal4_inject" / "runtime.dll");
    assert(
        pal4::inject::PackagedCliPath(install_dir) ==
        install_dir / "pal4_inject" / "cli.exe");
}

void TestLooseFileOverlayPaths() {
    assert(!pal4::inject::IsLooseFileOverlayActiveMode(
        pal4::inject::HookMode::observe_only));
    assert(!pal4::inject::IsLooseFileOverlayActiveMode(
        pal4::inject::HookMode::mirror_compare));
    assert(pal4::inject::IsLooseFileOverlayActiveMode(
        pal4::inject::HookMode::replace_with_fallback));
    assert(pal4::inject::IsLooseFileOverlayActiveMode(
        pal4::inject::HookMode::replace_strict));

    const std::filesystem::path game_root = R"(I:\Games\original)";
    const auto normalized = pal4::inject::NormalizeLooseResourcePath(
        R"(.\GameData/PALWorld/Q99/Q99/test.dff)");
    assert(normalized);
    assert(*normalized == std::filesystem::path(R"(GameData\PALWorld\Q99\Q99\test.dff)"));
    assert(!pal4::inject::NormalizeLooseResourcePath(R"(..\gamedata\test.bin)"));
    assert(!pal4::inject::NormalizeLooseResourcePath(R"(C:\gamedata\test.bin)"));
    assert(!pal4::inject::NormalizeLooseResourcePath(R"(PALWorld\Q99\test.dff)"));

    const auto candidates = pal4::inject::BuildLooseFileCandidates(
        game_root,
        R"(gamedata\PALWorld\Q99\Q99\test.dff)");
    assert(candidates.size() == 1);
    assert(
        candidates.front().path ==
        game_root / "gamepatch" /
            "gamedata" / "PALWorld" / "Q99" / "Q99" / "test.dff");
    const auto script_candidates = pal4::inject::BuildLooseFileCandidates(
        game_root,
        R"(gamedata\editData\script\M10.cs)");
    assert(script_candidates.size() == 1);
    assert(
        script_candidates.front().path ==
        game_root / "gamepatch" /
            "gamedata" / "editData" / "script" / "M10.cs");
    const auto misnamed_script_candidates =
        pal4::inject::BuildLooseTextScriptCandidates(
            game_root,
            R"(gamedata\editData\script\Music.csb)");
    assert(misnamed_script_candidates.size() == 1);
    assert(
        misnamed_script_candidates.front().path ==
        game_root / "gamepatch" /
            "gamedata" / "editData" / "script" / "Music.cs");
    const auto ordinary_text_script_candidates =
        pal4::inject::BuildLooseTextScriptCandidates(
            game_root,
            R"(gamedata\editData\script\M10.cs)");
    assert(ordinary_text_script_candidates.size() == 1);
    assert(ordinary_text_script_candidates.front().path == script_candidates.front().path);
    assert(
        pal4::inject::LooseFileLoadLogPath(game_root) ==
        game_root / "gamepatch" / "loose_file_load.log");

    const auto temp_root =
        std::filesystem::temp_directory_path() / "pal4_inject_loose_file_overlay_test";
    std::error_code ignored;
    std::filesystem::remove_all(temp_root, ignored);
    const auto loose_file = pal4::inject::GamePatchRoot(temp_root) /
        "gamedata" / "PALWorld" / "Q99" / "Q99" / "test.dff";
    std::filesystem::create_directories(loose_file.parent_path());
    {
        std::ofstream out(loose_file, std::ios::binary | std::ios::trunc);
        out << "PAL4 loose resource";
    }
    const auto found = pal4::inject::FindExistingLooseFile(
        temp_root,
        R"(gamedata\PALWorld\Q99\Q99\test.dff)");
    assert(found);
    assert(found->path == loose_file);

    const auto script_directory = pal4::inject::GamePatchRoot(temp_root) /
        "gamedata" / "editData" / "script";
    std::filesystem::create_directories(script_directory);
    const auto text_script = script_directory / "Music.cs";
    {
        std::ofstream out(text_script, std::ios::binary | std::ios::trunc);
        out << "// PAL4 text script";
    }
    const auto found_text_script =
        pal4::inject::FindExistingLooseTextScriptFile(
            temp_root,
            R"(gamedata\editData\script\Music.csb)");
    assert(found_text_script);
    assert(found_text_script->path == text_script);

    const auto invalid_csb = script_directory / "Music.csb";
    {
        std::ofstream out(invalid_csb, std::ios::binary | std::ios::trunc);
        out << "// This is source text, not compiled CSB";
    }
    std::string rejection_reason;
    assert(!pal4::inject::ValidateLoosePackageFile(
        R"(gamedata\editData\script\Music.csb)",
        invalid_csb,
        &rejection_reason));
    assert(rejection_reason.find("invalid_csb_header") != std::string::npos);

    const auto valid_csb = script_directory / "Valid.csb";
    {
        constexpr std::array<char, 7> bytes{
            '\x03', '\x00', '\x00', '\x00', 'C', 'S', 'B',
        };
        std::ofstream out(valid_csb, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    rejection_reason.clear();
    assert(pal4::inject::ValidateLoosePackageFile(
        R"(gamedata\editData\script\Valid.csb)",
        valid_csb,
        &rejection_reason));
    assert(rejection_reason.empty());
    assert(pal4::inject::ValidateLoosePackageFile(
        R"(gamedata\PALWorld\Q99\Q99\test.dff)",
        loose_file,
        &rejection_reason));
    std::filesystem::remove_all(temp_root, ignored);
}

void TestMainMenuBrandingPlan() {
    using namespace pal4::inject;
    assert(kOriginalMainMenuVersionText == "PAL4 v1.1");
    assert(kInjectedMainMenuVersionText == "PAL V1.2.1");
    assert(kMainMenuVersionSlotSize == 12);
    assert(kOriginalMainMenuVersionSlot[0] == 'P');
    assert(kOriginalMainMenuVersionSlot[3] == '4');
    assert(kOriginalMainMenuVersionSlot[9] == 0);
    assert(kInjectedMainMenuVersionSlot[3] == ' ');
    assert(kInjectedMainMenuVersionSlot[4] == 'V');
    assert(kInjectedMainMenuVersionSlot[9] == '1');
    assert(kInjectedMainMenuVersionSlot[10] == 0);
    assert(kInjectedMainMenuVersionSlot[11] == 0);
    assert(ida::kMainMenuVersionText == 0x8B9494);
}

void TestX86TrampolineCopiesLargeImmediateStackFrame() {
    const std::array<std::uint8_t, 7> source{
        0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x56,
    };
    std::array<std::uint8_t, 7> destination{};
    std::string error;
    assert(pal4::inject::CopyRelocatingX86Bytes(
        source.data(),
        destination.data(),
        source.size(),
        &error));
    assert(destination == source);
    assert(error.empty());
}

void TestX86TrampolineCopiesTextScriptPrologue() {
    const std::array<std::uint8_t, 5> source{
        0x8B, 0x44, 0x24, 0x04, 0x56,
    };
    std::array<std::uint8_t, 5> destination{};
    std::string error;
    assert(pal4::inject::CopyRelocatingX86Bytes(
        source.data(),
        destination.data(),
        source.size(),
        &error));
    assert(destination == source);
    assert(error.empty());
}

void TestX86TrampolineCopiesRwCameraDispatchThunk() {
    const std::array<std::uint8_t, 8> source{
        0x8B, 0x44, 0x24, 0x04, 0x89, 0x44, 0x24, 0x04,
    };
    std::array<std::uint8_t, 8> destination{};
    std::string error;
    assert(pal4::inject::CopyRelocatingX86Bytes(
        source.data(),
        destination.data(),
        source.size(),
        &error));
    assert(destination == source);
    assert(error.empty());
}

void TestLooseFileLoadLogFormatting() {
    pal4::inject::LooseFileLoadLogEntry entry{};
    entry.event = "override";
    entry.loader = "package";
    entry.mode = pal4::inject::HookMode::replace_with_fallback;
    entry.resource_path = R"(gamedata\PALActor\101\101_2.png)";
    entry.file_path = R"(I:\Games\PAL4\gamepatch\gamedata\PALActor\101\101_2.png)";
    entry.size = 490778;
    entry.has_size = true;

    const auto record = pal4::inject::FormatLooseFileLoadLogEntry(entry);
    assert(!record.empty());
    assert(record.front() == '{');
    assert(record.back() == '}');
    assert(record.find(R"("event":"override")") != std::string::npos);
    assert(record.find(R"("loader":"package")") != std::string::npos);
    assert(record.find(R"("mode":"replace_with_fallback")") != std::string::npos);
    assert(record.find(R"("resource":"gamedata\\PALActor\\101\\101_2.png")") !=
           std::string::npos);
    assert(record.find(R"("size":490778)") != std::string::npos);

    pal4::inject::LooseFileLoadLogEntry package_fallback{};
    package_fallback.event = "cpk_fallback";
    package_fallback.loader = "package";
    package_fallback.mode = pal4::inject::HookMode::replace_with_fallback;
    package_fallback.resource_path = R"(gamedata\database\pal4db.db)";
    package_fallback.fallback_source = "cpk";
    package_fallback.fallback_result_known = true;
    package_fallback.fallback_opened = true;
    package_fallback.fallback_used = true;
    const auto package_record =
        pal4::inject::FormatLooseFileLoadLogEntry(package_fallback);
    assert(package_record.find(R"("fallback_source":"cpk")") !=
           std::string::npos);
    assert(package_record.find(R"("cpk_opened":true)") != std::string::npos);
    assert(package_record.find(R"("fallback_to_cpk":true)") !=
           std::string::npos);

    pal4::inject::LooseFileLoadLogEntry script_entry{};
    script_entry.event = "gamepatch_required_missing";
    script_entry.loader = "text_script";
    script_entry.mode = pal4::inject::HookMode::replace_with_fallback;
    script_entry.resource_path = R"(gamedata\editData\script\M10.cs)";
    script_entry.file_path =
        R"(I:\Games\PAL4\gamepatch\gamedata\editData\script\M10.cs)";
    script_entry.reason = "gamepatch_file_required";
    const auto script_record =
        pal4::inject::FormatLooseFileLoadLogEntry(script_entry);
    assert(script_record.find(R"("loader":"text_script")") != std::string::npos);
    assert(script_record.find(R"("event":"gamepatch_required_missing")") !=
           std::string::npos);
    assert(script_record.find(R"("reason":"gamepatch_file_required")") !=
           std::string::npos);
    assert(script_record.find("fallback_source") == std::string::npos);
    assert(script_record.find("fallback_opened") == std::string::npos);
    assert(script_record.find("fallback_used") == std::string::npos);
    assert(script_record.find("cpk_opened") == std::string::npos);
}

void TestHookInventory() {
    const auto inventory = pal4::inject::BuildHookInventorySkeleton();
    assert(inventory.size() == 37);
    bool found_process_ui_event = false;
    bool found_handle_ui_message = false;
    bool found_gi_talk = false;
    bool found_cegui_renderer_ctor = false;
    bool found_cegui_system_init = false;
    bool found_load_font_file = false;
    bool found_setup_minimap_texture = false;
    bool found_combat_console_set_image_position = false;
    bool found_combat_console_set_image_position_2 = false;
    bool found_ui_show_combat_result = false;
    bool found_render_text_and_image = false;
    bool found_camera_prepare = false;
    bool found_camera_run_single = false;
    bool found_camera_update_matrix = false;
    bool found_d3d9_present = false;
    bool found_bink_player_update_and_render = false;
    bool found_loose_file_overlay = false;
    bool found_loose_text_script_overlay = false;
    bool found_audio_system_play_music = false;
    bool found_bink_player_open_video = false;
    bool found_gi_play_movie = false;
    bool found_combat_handle_action = false;
    bool found_combat_create_stunt_action = false;
    bool found_combat_execute_stunt = false;
    bool found_combat_skill_damage = false;
    bool found_combat_system_end = false;
    bool found_crt_runtime_message = false;
    bool found_crt_message_box = false;
    bool found_movement_collision_check = false;
    bool found_player_control_update = false;
    bool found_set_camera_mode_script = false;
    bool found_ui_frame_manager_set_cursor = false;
    for (const auto& hook : inventory) {
        assert(hook.id != HookId::rw_camera_begin_update);
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
            assert(hook.mode == pal4::inject::HookMode::observe_only);
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
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 13);
            assert(hook.ida_ea == pal4::inject::ida::kCeguiSystemInitialize);
            assert(hook.bootstrap_required);
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
        if (hook.id == HookId::render_text_and_image) {
            found_render_text_and_image = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 13);
            assert(hook.ida_ea == pal4::inject::ida::kRenderTextAndImage);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::camera_prepare) {
            found_camera_prepare = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kCameraPrepare);
            assert(hook.bootstrap_required);
        }
        if (hook.id == HookId::camera_run_single) {
            found_camera_run_single = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kCameraRunSingle);
            assert(hook.bootstrap_required);
        }
        if (hook.id == HookId::camera_update_matrix) {
            found_camera_update_matrix = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kCameraUpdateMatrix);
        }
        if (hook.id == HookId::d3d9_set_present_parameters) {
            found_d3d9_present = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 10);
            assert(hook.ida_ea == pal4::inject::ida::kD3d9SetPresentParameters);
        }
        if (hook.id == HookId::bink_player_update_and_render) {
            found_bink_player_update_and_render = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kBinkPlayerUpdateAndRender);
            assert(hook.bootstrap_required);
        }
        if (hook.id == HookId::loose_file_overlay) {
            found_loose_file_overlay = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kOpenPackageResourceFile);
            assert(hook.bootstrap_order == 5);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::loose_text_script_overlay) {
            found_loose_text_script_overlay = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 5);
            assert(
                hook.ida_ea ==
                pal4::inject::ida::kTextScriptInterpreterInitialize);
            assert(hook.bootstrap_order == 6);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::audio_system_play_music) {
            found_audio_system_play_music = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kAudioSystemPlayMusic);
        }
        if (hook.id == HookId::bink_player_open_video) {
            found_bink_player_open_video = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kBinkPlayerOpenVideo);
        }
        if (hook.id == HookId::gi_play_movie) {
            found_gi_play_movie = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kGiPlayMovieScriptCallback);
        }
        if (hook.id == HookId::combat_handle_action) {
            found_combat_handle_action = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 5);
            assert(hook.ida_ea == pal4::inject::ida::kCombatHandleAction);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::combat_create_stunt_action) {
            found_combat_create_stunt_action = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 8);
            assert(hook.ida_ea == pal4::inject::ida::kCombatCreateStuntAction);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::combat_execute_stunt) {
            found_combat_execute_stunt = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kCombatExecuteStunt);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::combat_skill_damage) {
            found_combat_skill_damage = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 5);
            assert(hook.ida_ea == pal4::inject::ida::kCombatSkillDamage);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::combat_system_end) {
            found_combat_system_end = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 5);
            assert(hook.ida_ea == pal4::inject::ida::kCombatSystemEnd);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::crt_runtime_message) {
            found_crt_runtime_message = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 9);
            assert(hook.ida_ea == pal4::inject::ida::kCrtRuntimeMessage);
            assert(hook.bootstrap_order == 131);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::crt_message_box) {
            found_crt_message_box = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 9);
            assert(hook.ida_ea == pal4::inject::ida::kCrtMessageBox);
            assert(hook.bootstrap_order == 131);
            assert(!hook.bootstrap_required);
        }
        if (hook.id == HookId::movement_collision_check) {
            found_movement_collision_check = true;
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kMovementCollisionCheck);
        }
        if (hook.id == HookId::ui_frame_manager_set_cursor) {
            found_ui_frame_manager_set_cursor = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 7);
            assert(hook.ida_ea == pal4::inject::ida::kUiFrameManagerSetCursor);
            assert(hook.bootstrap_required);
        }
        if (hook.id == HookId::player_control_update) {
            found_player_control_update = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 6);
            assert(hook.ida_ea == pal4::inject::ida::kPlayerControlUpdate);
        }
        if (hook.id == HookId::set_camera_mode_script) {
            found_set_camera_mode_script = true;
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
            assert(hook.patch_span == 6);
            assert(hook.ida_ea == pal4::inject::ida::kSetCameraModeScript);
        }
    }
    assert(found_process_ui_event);
    assert(found_handle_ui_message);
    assert(found_gi_talk);
    assert(found_cegui_renderer_ctor);
    assert(found_cegui_system_init);
    assert(found_load_font_file);
    assert(found_setup_minimap_texture);
    assert(found_combat_console_set_image_position);
    assert(found_combat_console_set_image_position_2);
    assert(found_ui_show_combat_result);
    assert(found_render_text_and_image);
    assert(found_camera_prepare);
    assert(found_camera_run_single);
    assert(found_camera_update_matrix);
    assert(found_d3d9_present);
    assert(found_bink_player_update_and_render);
    assert(found_loose_file_overlay);
    assert(found_loose_text_script_overlay);
    assert(found_audio_system_play_music);
    assert(found_bink_player_open_video);
    assert(found_gi_play_movie);
    assert(found_combat_handle_action);
    assert(found_combat_create_stunt_action);
    assert(found_combat_execute_stunt);
    assert(found_combat_skill_damage);
    assert(found_combat_system_end);
    assert(found_crt_runtime_message);
    assert(found_crt_message_box);
    assert(found_movement_collision_check);
    assert(found_player_control_update);
    assert(found_set_camera_mode_script);
    assert(found_ui_frame_manager_set_cursor);
}

void TestHookManagerBootstrapReplacementCoverage() {
    std::string error;
    assert(pal4::inject::GetHookManager().Initialize(&error));
    assert(error.empty());
}

void TestAspectRatioLayoutMath() {
    const auto widescreen = pal4::inject::ComputeAspectFitRect(1920, 1080, 640, 480);
    assert(widescreen.valid);
    assert(widescreen.x == 240);
    assert(widescreen.y == 0);
    assert(widescreen.width == 1440);
    assert(widescreen.height == 1080);

    const auto taller_container = pal4::inject::ComputeAspectFitRect(1280, 1024, 640, 480);
    assert(taller_container.valid);
    assert(taller_container.x == 0);
    assert(taller_container.y == 32);
    assert(taller_container.width == 1280);
    assert(taller_container.height == 960);

    const auto invalid = pal4::inject::ComputeAspectFitRect(0, 1080, 640, 480);
    assert(!invalid.valid);

    const auto width_fill =
        pal4::inject::ComputeAspectFillWidthRect(1920, 1080, 640, 480);
    assert(width_fill.valid);
    assert(width_fill.x == 0);
    assert(width_fill.y == -180);
    assert(width_fill.width == 1920);
    assert(width_fill.height == 1440);

    const auto width_fill_tall_container =
        pal4::inject::ComputeAspectFillWidthRect(1280, 1024, 640, 480);
    assert(width_fill_tall_container.valid);
    assert(width_fill_tall_container.x == 0);
    assert(width_fill_tall_container.y == 32);
    assert(width_fill_tall_container.width == 1280);
    assert(width_fill_tall_container.height == 960);

    const auto invalid_width_fill =
        pal4::inject::ComputeAspectFillWidthRect(1920, 1080, 0, 480);
    assert(!invalid_width_fill.valid);
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

void TestBinkScalingModeStrings() {
    assert(std::string(pal4::inject::ToString(pal4::inject::BinkScalingMode::fit)) ==
           "fit");
    assert(std::string(pal4::inject::ToString(
               pal4::inject::BinkScalingMode::fill_width_crop)) ==
           "fill_width_crop");

    auto parsed = pal4::inject::BinkScalingMode::fit;
    assert(pal4::inject::TryParseBinkScalingMode("fill_width_crop", &parsed));
    assert(parsed == pal4::inject::BinkScalingMode::fill_width_crop);
    assert(!pal4::inject::TryParseBinkScalingMode("stretch", &parsed));
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
    assert(pal4::inject::FindUniqueUiSnapshotNodeByPathSuffix(
               parsed, "BtnNewGame") == by_path);
    assert(pal4::inject::FindUniqueUiSnapshotNodeByPathSuffix(
               parsed, "Desktop/BtnNewGame") == by_path);
    assert(pal4::inject::UiSnapshotTreeContainsText(parsed, "Game"));
    const auto display = pal4::inject::FormatUiSnapshotTreeForDisplay(parsed);
    assert(display.find("[ref=e2]") != std::string::npos);
    assert(display.find("BtnNewGame") != std::string::npos);

    pal4::inject::UiSnapshotTree large_tree{};
    large_tree.root.ref = "e1";
    large_tree.root.type = "gui_sheet";
    large_tree.root.name = "Desktop";
    large_tree.root.path = "Desktop";
    large_tree.root.visible = true;
    large_tree.root.enabled = true;
    for (int index = 0; index < 600; ++index) {
        pal4::inject::UiSnapshotNode large_child{};
        large_child.ref = "e" + std::to_string(index + 2);
        large_child.type = "button";
        large_child.name = "CombatRoleState/VeryLongControlName" + std::to_string(index);
        large_child.path = "Desktop/CombatMainWindow/CombatRoleState/" + large_child.name;
        large_child.text = "snapshot transport payload " + std::to_string(index);
        large_child.rect = {10, 20, 110, 60};
        large_child.visible = true;
        large_child.enabled = true;
        large_child.clickable = true;
        large_tree.root.children.push_back(std::move(large_child));
    }
    const std::string large_payload = pal4::inject::SerializeUiSnapshotTree(large_tree);
    assert(large_payload.size() > 65536);
    pal4::inject::ProtocolResponse large_response{};
    large_response.ok = true;
    large_response.status = "snapshot_ui";
    large_response.fields["tree"] = large_payload;
    const std::string large_wire = pal4::inject::FormatProtocolResponse(large_response);
    pal4::inject::ProtocolResponse parsed_large_response{};
    assert(pal4::inject::ParseProtocolResponse(
        large_wire, &parsed_large_response, &error));
    pal4::inject::UiSnapshotTree parsed_large_tree{};
    assert(pal4::inject::ParseUiSnapshotTree(
        parsed_large_response.fields["tree"], &parsed_large_tree, &error));
    assert(pal4::inject::CountUiSnapshotNodes(parsed_large_tree) == 601);
}

void TestUiInputPlan() {
    pal4::inject::UiInputDispatchMode mode{};
    assert(pal4::inject::TryParseUiInputDispatchOption("--os-queue", &mode));
    assert(mode == pal4::inject::UiInputDispatchMode::os_queue);
    assert(pal4::inject::TryParseUiInputDispatchOption("--direct-seam", &mode));
    assert(mode == pal4::inject::UiInputDispatchMode::direct_seam);
    assert(!pal4::inject::TryParseUiInputDispatchOption("--unsafe-default", &mode));

    pal4::inject::UiSnapshotTree tree{};
    tree.root.ref = "e1";
    tree.root.rect = {0, 0, 800, 600};
    tree.root.visible = true;
    tree.root.enabled = true;

    pal4::inject::UiSnapshotNode container{};
    container.ref = "e2";
    container.rect = {100, 50, 700, 550};
    container.visible = true;
    container.enabled = true;

    pal4::inject::UiSnapshotNode child{};
    child.ref = "e3";
    child.rect = {10, 20, 110, 60};
    child.visible = true;
    child.enabled = true;
    child.clickable = true;
    container.children.push_back(child);
    tree.root.children.push_back(container);

    const auto viewport = pal4::inject::BuildUiViewportPlan(
        1600,
        900,
        pal4::inject::UiProfile::centered_800x600);
    pal4::inject::UiRefClickPlan click{};
    std::string error;
    assert(pal4::inject::BuildUiRefClickPlan(tree, "e3", viewport, &click, &error));
    assert(click.logical_x == 160);
    assert(click.logical_y == 90);
    assert(click.client_x == 440);
    assert(click.client_y == 135);

    error.clear();
    assert(!pal4::inject::BuildUiRefClickPlan(tree, "stale", viewport, &click, &error));
    assert(error.find("fresh snapshot") != std::string::npos);

    tree.root.children.front().children.front().enabled = false;
    error.clear();
    assert(!pal4::inject::BuildUiRefClickPlan(tree, "e3", viewport, &click, &error));
    assert(error.find("clickable") != std::string::npos);
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

void TestInjectFeatureCatalog() {
    const auto rows = pal4::inject::BuildInjectFeatureCatalog();
    assert(!rows.empty());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        assert(!rows[index].label.empty());
        for (std::size_t other = index + 1; other < rows.size(); ++other) {
            assert(rows[index].id != rows[other].id);
        }
    }

    const auto find_row =
        [&rows](const HookId id) -> const pal4::inject::InjectFeatureDescriptor* {
            for (const auto& row : rows) {
                if (row.id == id) {
                    return &row;
                }
            }
            return nullptr;
        };

    const auto* process_ui_row = find_row(HookId::process_ui_event);
    assert(process_ui_row);
    assert(process_ui_row->category == pal4::inject::InjectFeatureCategory::input_ui);
    assert(process_ui_row->group_label == std::string_view("输入与界面"));
    assert(process_ui_row->label == std::string_view("界面事件替换"));
    assert(process_ui_row->allow_mode_change);

    const auto* bink_row = find_row(HookId::bink_player_update_and_render);
    assert(bink_row);
    assert(bink_row->category == pal4::inject::InjectFeatureCategory::render_visual);
    assert(bink_row->group_label == std::string_view("渲染与画面"));
    assert(bink_row->allow_mode_change);

    const auto* wndproc_row = find_row(HookId::pal4_main_wndproc);
    assert(!wndproc_row);

    const auto* handle_player_input_row = find_row(HookId::handle_player_input_events);
    assert(!handle_player_input_row);

    const auto* gi_talk_row = find_row(HookId::gi_talk);
    assert(gi_talk_row);
    assert(gi_talk_row->category == pal4::inject::InjectFeatureCategory::script_text);
    assert(gi_talk_row->group_label == std::string_view("脚本与文本"));

    const auto* renderer_row = find_row(HookId::cegui_renderer_constructor_2);
    assert(renderer_row);
    assert(renderer_row->category == pal4::inject::InjectFeatureCategory::render_visual);
    assert(renderer_row->group_label == std::string_view("渲染与画面"));

    const auto* combat_number_row = find_row(HookId::combat_console_set_image_position);
    assert(combat_number_row);
    assert(!combat_number_row->label.empty());
    assert(combat_number_row->allow_mode_change);
    const auto* render_text_row = find_row(HookId::render_text_and_image);
    assert(render_text_row);
    assert(render_text_row->category == pal4::inject::InjectFeatureCategory::render_visual);
    assert(render_text_row->allow_mode_change);

    const auto* combat_result_row = find_row(HookId::ui_show_combat_result);
    assert(combat_result_row);
    assert(combat_result_row->group_label == std::string_view("渲染与画面"));

    const auto* camera_row = find_row(HookId::camera_update_matrix);
    assert(camera_row);
    assert(camera_row->category == pal4::inject::InjectFeatureCategory::camera);
    assert(camera_row->group_label == std::string_view("相机"));
    assert(find_row(HookId::rw_camera_begin_update) == nullptr);

    const auto* loose_file_row = find_row(HookId::loose_file_overlay);
    assert(loose_file_row);
    assert(loose_file_row->category == pal4::inject::InjectFeatureCategory::resource);
    assert(loose_file_row->group_label == std::string_view("资源与补丁"));
    assert(loose_file_row->allow_mode_change);
    assert(!loose_file_row->allow_log_change);

    const auto* loose_text_script_row =
        find_row(HookId::loose_text_script_overlay);
    assert(!loose_text_script_row);

    std::size_t widescreen_feature_count = 0;
    pal4::inject::InjectPersistedSettings preset{};
    for (const auto& row : rows) {
        if (pal4::inject::InjectFeatureFollowsWidescreen(row.id)) {
            ++widescreen_feature_count;
        }
        preset.hooks.push_back({
            row.id,
            pal4::inject::HookMode::replace_with_fallback,
            pal4::inject::HookMode::replace_with_fallback,
            false,
        });
    }
    assert(widescreen_feature_count == 8);
    assert(pal4::inject::InjectFeatureFollowsWidescreen(
        HookId::cegui_renderer_constructor_2));
    assert(pal4::inject::InjectFeatureFollowsWidescreen(
        HookId::bink_player_update_and_render));
    assert(!pal4::inject::InjectFeatureFollowsWidescreen(
        HookId::d3d9_set_present_parameters));
    assert(!pal4::inject::InjectFeatureFollowsWidescreen(
        HookId::camera_update_matrix));

    const auto bink_preset = std::find_if(
        preset.hooks.begin(),
        preset.hooks.end(),
        [](const pal4::inject::PersistedHookSetting& hook) {
            return hook.id == HookId::bink_player_update_and_render;
        });
    assert(bink_preset != preset.hooks.end());
    bink_preset->active_mode = pal4::inject::HookMode::mirror_compare;
    pal4::inject::ApplyWidescreenFeaturePreset(&preset, false);
    for (const auto& hook : preset.hooks) {
        if (pal4::inject::InjectFeatureFollowsWidescreen(hook.id)) {
            assert(hook.mode == pal4::inject::HookMode::observe_only);
            assert(hook.active_mode == pal4::inject::HookMode::replace_with_fallback);
        } else {
            assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
        }
    }
    pal4::inject::ApplyWidescreenFeaturePreset(&preset, true);
    for (const auto& hook : preset.hooks) {
        assert(hook.mode == pal4::inject::HookMode::replace_with_fallback);
    }

    const auto modes = pal4::inject::BuildInjectFeatureModes();
    assert(modes.size() == 4);
    assert(modes[0] == pal4::inject::HookMode::observe_only);
    assert(modes[3] == pal4::inject::HookMode::replace_strict);
    assert(pal4::inject::InjectFeatureCategoryLabel(pal4::inject::InjectFeatureCategory::render_visual) ==
           std::string_view("渲染与画面"));
    assert(pal4::inject::InjectFeatureModeLabel(pal4::inject::HookMode::observe_only) ==
           std::string_view("仅观察"));
    assert(pal4::inject::InjectFeatureModeLabel(pal4::inject::HookMode::replace_strict) ==
           std::string_view("强制替换"));
    assert(pal4::inject::FindInjectFeatureModeIndex(pal4::inject::HookMode::mirror_compare) == 1);
    assert(pal4::inject::InjectFeatureModeFromIndex(2) == pal4::inject::HookMode::replace_with_fallback);
    assert(pal4::inject::InjectFeatureModeFromIndex(99) == pal4::inject::HookMode::observe_only);
}

void TestInjectSettingsRoundTrip() {
    pal4::inject::InjectPersistedSettings settings{};
    settings.script_mode = pal4::inject::ScriptMode::cs;
    settings.msaa_level = pal4::inject::MsaaLevel::x4;
    settings.bink_scaling_mode = pal4::inject::BinkScalingMode::fill_width_crop;
    settings.gi_talk_volume = 1.65F;
    settings.gamepad_enabled = true;
    settings.gamepad_log_enabled = true;
    settings.gamepad_modern_controls = true;
    settings.gamepad_invert_camera_y = true;
    settings.gamepad_preserve_free_camera = true;
    settings.gamepad_run_threshold = 0.71F;
    settings.gamepad_fast_run_threshold = 0.91F;
    settings.gamepad_camera_sensitivity = 175.0F;
    pal4::inject::SetGamepadBinding(
        &settings.gamepad_mapping,
        pal4::inject::Xbox360Button::y,
        pal4::inject::GamepadAction::map);
    settings.borderless_window = true;
    settings.borderless_monitor = R"(\\.\DISPLAY2)";
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
    assert(text.find("gamepad.binding.") == std::string::npos);
    pal4::inject::InjectPersistedSettings parsed{};
    assert(pal4::inject::ParseInjectPersistedSettings(text, &parsed, &error));
    assert(parsed.script_mode == pal4::inject::ScriptMode::cs);
    assert(parsed.msaa_level == pal4::inject::MsaaLevel::x4);
    assert(parsed.bink_scaling_mode ==
           pal4::inject::BinkScalingMode::fill_width_crop);
    assert(parsed.gi_talk_volume == 1.65F);
    assert(parsed.gamepad_enabled);
    assert(parsed.gamepad_log_enabled);
    assert(parsed.gamepad_modern_controls);
    assert(parsed.gamepad_invert_camera_y);
    assert(parsed.gamepad_preserve_free_camera);
    assert(parsed.gamepad_run_threshold == 0.71F);
    assert(parsed.gamepad_fast_run_threshold == 0.91F);
    assert(parsed.gamepad_camera_sensitivity == 175.0F);
    assert(pal4::inject::GetGamepadBinding(
        parsed.gamepad_mapping,
        pal4::inject::Xbox360Button::y) ==
        pal4::inject::GamepadAction::maze_skill);
    assert(parsed.borderless_window);
    assert(parsed.borderless_monitor == R"(\\.\DISPLAY2)");
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
    assert(loaded.script_mode == pal4::inject::ScriptMode::cs);
    assert(loaded.msaa_level == pal4::inject::MsaaLevel::x4);
    assert(loaded.bink_scaling_mode ==
           pal4::inject::BinkScalingMode::fill_width_crop);
    assert(loaded.gi_talk_volume == 1.65F);
    assert(loaded.gamepad_run_threshold == 0.71F);
    assert(loaded.gamepad_fast_run_threshold == 0.91F);
    assert(loaded.gamepad_camera_sensitivity == 175.0F);
    assert(loaded.gamepad_preserve_free_camera);
    assert(loaded.borderless_window);
    assert(loaded.borderless_monitor == R"(\\.\DISPLAY2)");
    std::filesystem::remove(temp_path);

    pal4::inject::InjectPersistedSettings legacy{};
    assert(pal4::inject::ParseInjectPersistedSettings(
        "version=1\nmsaa_level=2x\n",
        &legacy,
        &error));
    assert(legacy.script_mode == pal4::inject::ScriptMode::csb);
    assert(legacy.bink_scaling_mode == pal4::inject::BinkScalingMode::fit);
    assert(legacy.gi_talk_volume == pal4::inject::kDefaultGiTalkVolume);
    assert(legacy.gamepad_modern_controls);
    assert(!legacy.gamepad_preserve_free_camera);
    assert(legacy.gamepad_fast_run_threshold == 0.88F);
    assert(pal4::inject::GetGamepadBinding(
        legacy.gamepad_mapping,
        pal4::inject::Xbox360Button::right_thumb) ==
        pal4::inject::GamepadAction::camera_distance_cycle);
    assert(pal4::inject::GetGamepadBinding(
        legacy.gamepad_mapping,
        pal4::inject::Xbox360Button::left_thumb) ==
        pal4::inject::GamepadAction::switch_leader);

    pal4::inject::InjectPersistedSettings version9{};
    assert(pal4::inject::ParseInjectPersistedSettings(
        "version=9\ngamepad.binding.left_thumb=auto_forward\n",
        &version9,
        &error));
    assert(pal4::inject::GetGamepadBinding(
        version9.gamepad_mapping,
        pal4::inject::Xbox360Button::left_thumb) ==
        pal4::inject::GamepadAction::switch_leader);

    pal4::inject::InjectPersistedSettings version10{};
    assert(pal4::inject::ParseInjectPersistedSettings(
        "version=10\n"
        "gamepad.binding.x=mouse_left\n"
        "gamepad.binding.y=run_toggle\n"
        "gamepad.binding.start=system_menu\n",
        &version10,
        &error));
    assert(pal4::inject::GetGamepadBinding(
        version10.gamepad_mapping,
        pal4::inject::Xbox360Button::x) ==
        pal4::inject::GamepadAction::place_marker);
    assert(pal4::inject::GetGamepadBinding(
        version10.gamepad_mapping,
        pal4::inject::Xbox360Button::y) ==
        pal4::inject::GamepadAction::maze_skill);
    assert(pal4::inject::GetGamepadBinding(
        version10.gamepad_mapping,
        pal4::inject::Xbox360Button::start) ==
        pal4::inject::GamepadAction::system_page);

    pal4::inject::InjectPersistedSettings custom_version10{};
    assert(pal4::inject::ParseInjectPersistedSettings(
        "version=10\n"
        "gamepad.binding.x=map\n"
        "gamepad.binding.y=mouse_left\n"
        "gamepad.binding.start=confirm\n",
        &custom_version10,
        &error));
    assert(pal4::inject::GetGamepadBinding(
        custom_version10.gamepad_mapping,
        pal4::inject::Xbox360Button::x) ==
        pal4::inject::GamepadAction::place_marker);
    assert(pal4::inject::GetGamepadBinding(
        custom_version10.gamepad_mapping,
        pal4::inject::Xbox360Button::y) ==
        pal4::inject::GamepadAction::maze_skill);
    assert(pal4::inject::GetGamepadBinding(
        custom_version10.gamepad_mapping,
        pal4::inject::Xbox360Button::start) ==
        pal4::inject::GamepadAction::system_page);
    assert(!legacy.borderless_window);
    assert(legacy.borderless_monitor.empty());

    pal4::inject::InjectPersistedSettings invalid{};
    assert(!pal4::inject::ParseInjectPersistedSettings(
        "version=3\nscript_mode=inherit\n",
        &invalid,
        &error));
    assert(error.find("invalid script_mode") != std::string::npos);
    assert(!pal4::inject::ParseInjectPersistedSettings(
        "version=6\ngi_talk_volume=3.1\n",
        &invalid,
        &error));
    assert(error.find("invalid gi_talk_volume") != std::string::npos);
    assert(!pal4::inject::ParseInjectPersistedSettings(
        "version=8\ngamepad_run_threshold=nan\n",
        &invalid,
        &error));
    assert(error.find("invalid gamepad_run_threshold") != std::string::npos);
    assert(!pal4::inject::ParseInjectPersistedSettings(
        "version=9\ngamepad_fast_run_threshold=nan\n",
        &invalid,
        &error));
    assert(error.find("invalid gamepad_fast_run_threshold") != std::string::npos);
}

void TestGamepadLogic() {
    assert(!pal4::inject::HasGamepadInputActivity(
        0, 0, 0, 0, 0, 0, 0, 7849, 8689, 128));
    assert(pal4::inject::HasGamepadInputActivity(
        0x1000, 0, 0, 0, 0, 0, 0, 7849, 8689, 128));
    assert(pal4::inject::HasGamepadInputActivity(
        0, 128, 0, 0, 0, 0, 0, 7849, 8689, 128));
    assert(pal4::inject::HasGamepadInputActivity(
        0, 0, 0, 20000, 0, 0, 0, 7849, 8689, 128));

    const auto centered = pal4::inject::BuildGamepadAnalogStick(2000, -2000, 7849);
    assert(centered.magnitude == 0.0F);

    const auto forward = pal4::inject::BuildGamepadAnalogStick(0, 32767, 7849);
    assert(std::fabs(forward.x) < 0.001F);
    assert(std::fabs(forward.y - 1.0F) < 0.001F);
    assert(std::fabs(forward.magnitude - 1.0F) < 0.001F);

    const auto diagonal = pal4::inject::BuildGamepadAnalogStick(32767, 32767, 7849);
    assert(std::fabs(diagonal.x - 0.7071F) < 0.001F);
    assert(std::fabs(diagonal.y - 0.7071F) < 0.001F);
    assert(std::fabs(diagonal.magnitude - 1.0F) < 0.001F);
    assert(pal4::inject::SelectGamepadMovementMode(0.61F, 0.62F, 0.88F) == 0);
    assert(pal4::inject::SelectGamepadMovementMode(0.62F, 0.62F, 0.88F) == 1);
    assert(pal4::inject::SelectGamepadMovementMode(0.87F, 0.62F, 0.88F) == 1);
    assert(pal4::inject::SelectGamepadMovementMode(0.88F, 0.62F, 0.88F) == 2);
    const auto walk_tuning =
        pal4::inject::BuildGamepadMovementTuning(0.50F, 0.62F, 0.88F);
    assert(walk_tuning.mode == 0);
    assert(std::fabs(walk_tuning.speed_multiplier - 0.4F) < 0.001F);
    assert(std::fabs(walk_tuning.animation_multiplier - 1.0F) < 0.001F);
    const auto blended_tuning =
        pal4::inject::BuildGamepadMovementTuning(0.75F, 0.62F, 0.88F);
    assert(blended_tuning.mode == 1);
    assert(std::fabs(blended_tuning.speed_multiplier - 1.25F) < 0.001F);
    assert(std::fabs(blended_tuning.animation_multiplier - 1.20F) < 0.001F);
    const auto fast_tuning =
        pal4::inject::BuildGamepadMovementTuning(1.0F, 0.62F, 0.88F);
    assert(fast_tuning.mode == 2);
    assert(std::fabs(fast_tuning.speed_multiplier - 1.5F) < 0.001F);
    assert(std::fabs(fast_tuning.animation_multiplier - 1.4F) < 0.001F);
    const auto reverse_turn = pal4::inject::BuildGamepadTurnTuning(
        0.0F, 0.0F, -1.0F, 0.1F, 360.0F, 20.0F);
    assert(reverse_turn.use_walk_animation);
    assert(std::fabs(reverse_turn.remaining_angle_degrees - 180.0F) < 0.001F);
    assert(std::fabs(reverse_turn.direction_x + 0.5878F) < 0.001F);
    assert(std::fabs(reverse_turn.direction_z - 0.8090F) < 0.001F);
    const auto small_turn = pal4::inject::BuildGamepadTurnTuning(
        0.0F, 0.173648F, 0.984808F, 0.1F, 360.0F, 20.0F);
    assert(!small_turn.use_walk_animation);
    assert(std::fabs(small_turn.direction_x - 0.173648F) < 0.001F);
    assert(std::fabs(small_turn.direction_z - 0.984808F) < 0.001F);
    assert(pal4::inject::SelectNextGamepadCameraDistance(1.0F, 5.0F) == 2.5F);
    assert(pal4::inject::SelectNextGamepadCameraDistance(2.5F, 5.0F) == 5.0F);
    assert(pal4::inject::SelectNextGamepadCameraDistance(5.0F, 5.0F) == 7.5F);
    assert(pal4::inject::SelectNextGamepadCameraDistance(7.5F, 5.0F) == 1.0F);

    const auto mapping = pal4::inject::DefaultXbox360GamepadMapping();
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::a) == pal4::inject::GamepadAction::confirm);
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::b) == pal4::inject::GamepadAction::cancel);
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::x) ==
        pal4::inject::GamepadAction::place_marker);
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::y) ==
        pal4::inject::GamepadAction::maze_skill);
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::back) ==
        pal4::inject::GamepadAction::cancel);
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::start) ==
        pal4::inject::GamepadAction::system_page);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::start,
        pal4::inject::GamepadAction::system_page,
        pal4::inject::GamepadInputContext::gameplay) ==
        pal4::inject::GamepadAction::system_page);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::start,
        pal4::inject::GamepadAction::system_page,
        pal4::inject::GamepadInputContext::system_menu) ==
        pal4::inject::GamepadAction::none);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::start,
        pal4::inject::GamepadAction::system_page,
        pal4::inject::GamepadInputContext::menu) ==
        pal4::inject::GamepadAction::none);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::back,
        pal4::inject::GamepadAction::cancel,
        pal4::inject::GamepadInputContext::gameplay) ==
        pal4::inject::GamepadAction::none);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::back,
        pal4::inject::GamepadAction::cancel,
        pal4::inject::GamepadInputContext::system_menu) ==
        pal4::inject::GamepadAction::cancel);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::left_shoulder,
        pal4::inject::GamepadAction::main_page_previous,
        pal4::inject::GamepadInputContext::gameplay) ==
        pal4::inject::GamepadAction::map);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::left_shoulder,
        pal4::inject::GamepadAction::main_page_previous,
        pal4::inject::GamepadInputContext::system_menu) ==
        pal4::inject::GamepadAction::main_page_previous);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::b,
        pal4::inject::GamepadAction::cancel,
        pal4::inject::GamepadInputContext::gameplay,
        true,
        false) == pal4::inject::GamepadAction::cancel);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::x,
        pal4::inject::GamepadAction::place_marker,
        pal4::inject::GamepadInputContext::gameplay,
        true,
        true) == pal4::inject::GamepadAction::combat_attack);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::y,
        pal4::inject::GamepadAction::maze_skill,
        pal4::inject::GamepadInputContext::gameplay,
        true,
        true) == pal4::inject::GamepadAction::combat_defend);
    assert(pal4::inject::ResolveGamepadActionForContext(
        pal4::inject::Xbox360Button::x,
        pal4::inject::GamepadAction::place_marker,
        pal4::inject::GamepadInputContext::gameplay,
        true,
        false) == pal4::inject::GamepadAction::place_marker);
    std::array<bool, pal4::inject::kXbox360ButtonCount> pressed_buttons{};
    pressed_buttons[static_cast<std::size_t>(pal4::inject::Xbox360Button::a)] = true;
    assert(pal4::inject::IsMappedGamepadActionPressed(
        mapping,
        pal4::inject::GamepadAction::confirm,
        pressed_buttons));
    pressed_buttons = {};
    assert(!pal4::inject::IsMappedGamepadActionPressed(
        mapping,
        pal4::inject::GamepadAction::confirm,
        pressed_buttons));
    const auto initial_press_plan =
        pal4::inject::BuildGamepadKeyMirrorPlan(true, false, 0);
    assert(initial_press_plan.press_updates == 1);
    assert(initial_press_plan.release_updates == 0);
    const auto held_after_native_poll_plan =
        pal4::inject::BuildGamepadKeyMirrorPlan(true, true, 1);
    assert(held_after_native_poll_plan.press_updates == 2);
    assert(held_after_native_poll_plan.release_updates == 0);
    const auto held_after_just_pressed_plan =
        pal4::inject::BuildGamepadKeyMirrorPlan(true, true, 2);
    assert(held_after_just_pressed_plan.press_updates == 1);
    assert(held_after_just_pressed_plan.release_updates == 0);
    const auto already_held_plan =
        pal4::inject::BuildGamepadKeyMirrorPlan(true, true, 3);
    assert(already_held_plan.press_updates == 0);
    const auto release_plan =
        pal4::inject::BuildGamepadKeyMirrorPlan(false, true, 3);
    assert(release_plan.press_updates == 0);
    assert(release_plan.release_updates == 1);
    const auto already_released_plan =
        pal4::inject::BuildGamepadKeyMirrorPlan(false, true, 1);
    assert(already_released_plan.press_updates == 0);
    assert(already_released_plan.release_updates == 0);
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::right_thumb) ==
        pal4::inject::GamepadAction::camera_distance_cycle);
    assert(pal4::inject::GetGamepadBinding(
        mapping,
        pal4::inject::Xbox360Button::left_thumb) ==
        pal4::inject::GamepadAction::switch_leader);
    assert(pal4::inject::WrapGamepadCycleIndex(0, -1, 7) == 6);
    const bool available_pages[]{true, false, true, false, false, true};
    assert(pal4::inject::FindNextAvailableGamepadPage(
        0, 1, available_pages, 6) == 2);
    assert(pal4::inject::FindNextAvailableGamepadPage(
        0, -1, available_pages, 6) == 5);
    assert(pal4::inject::FindNextAvailableGamepadPage(
        2, 1, available_pages, 6) == 5);
    assert(pal4::inject::FindNextAvailableGamepadPage(
        5, 1, available_pages, 6) == 0);
    const bool only_current_page[]{false, true, false};
    assert(pal4::inject::FindNextAvailableGamepadPage(
        1, 1, only_current_page, 3) == 1);
    const bool no_available_pages[]{false, false};
    assert(pal4::inject::FindNextAvailableGamepadPage(
        0, 1, no_available_pages, 2) == -1);
    assert(pal4::inject::FindNextAvailableGamepadPage(
        0, 0, available_pages, 6) == -1);
    assert(
        pal4::inject::SelectGamepadCursorPresentation(true, true) ==
        pal4::inject::GamepadCursorPresentation::hide_all);
    assert(
        pal4::inject::SelectGamepadCursorPresentation(false, true) ==
        pal4::inject::GamepadCursorPresentation::cegui_only);
    assert(
        pal4::inject::SelectGamepadCursorPresentation(false, false) ==
        pal4::inject::GamepadCursorPresentation::native_only);
    assert(
        pal4::inject::SelectGameplayDpadAction(
            pal4::inject::GamepadDpadDirection::up) ==
        pal4::inject::GamepadAction::role_page);
    assert(
        pal4::inject::SelectGameplayDpadAction(
            pal4::inject::GamepadDpadDirection::down) ==
        pal4::inject::GamepadAction::magic_page);
    assert(
        pal4::inject::SelectGameplayDpadAction(
            pal4::inject::GamepadDpadDirection::left) ==
        pal4::inject::GamepadAction::item_page);
    assert(
        pal4::inject::SelectGameplayDpadAction(
            pal4::inject::GamepadDpadDirection::right) ==
        pal4::inject::GamepadAction::equipment_page);
    assert(!pal4::inject::ShouldDispatchGamepadCancel(
        pal4::inject::GamepadInputContext::gameplay));
    assert(pal4::inject::ShouldDispatchGamepadCancel(
        pal4::inject::GamepadInputContext::system_menu));
    assert(pal4::inject::ShouldDispatchGamepadCancel(
        pal4::inject::GamepadInputContext::menu));
    assert(pal4::inject::SelectGamepadDpadNavigationMode(
        pal4::inject::GamepadInputContext::gameplay,
        false, false, false) ==
        pal4::inject::GamepadDpadNavigationMode::gameplay_shortcuts);
    assert(pal4::inject::SelectGamepadDpadNavigationMode(
        pal4::inject::GamepadInputContext::gameplay,
        false, false, true) ==
        pal4::inject::GamepadDpadNavigationMode::plain_ui);
    assert(pal4::inject::SelectGamepadDpadNavigationMode(
        pal4::inject::GamepadInputContext::gameplay,
        true, false, false) ==
        pal4::inject::GamepadDpadNavigationMode::system_menu);
    assert(pal4::inject::SelectGamepadDpadNavigationMode(
        pal4::inject::GamepadInputContext::gameplay,
        false, true, false) ==
        pal4::inject::GamepadDpadNavigationMode::plain_ui);
    assert(pal4::inject::SelectGamepadDpadNavigationMode(
        pal4::inject::GamepadInputContext::menu,
        false, false, false) ==
        pal4::inject::GamepadDpadNavigationMode::plain_ui);
    assert(pal4::inject::SelectGamepadDpadNavigationMode(
        pal4::inject::GamepadInputContext::system_menu,
        false, false, false) ==
        pal4::inject::GamepadDpadNavigationMode::system_menu);
    using WheelSector = pal4::inject::GamepadCombatWheelSector;
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {0.0F, 1.0F, 1.0F}, WheelSector::none) == WheelSector::magic);
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {0.95F, 0.31F, 1.0F}, WheelSector::none) == WheelSector::stunt);
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {0.59F, -0.81F, 1.0F}, WheelSector::none) == WheelSector::flee);
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {-0.59F, -0.81F, 1.0F}, WheelSector::none) == WheelSector::defend);
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {-0.95F, 0.31F, 1.0F}, WheelSector::none) == WheelSector::article);
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {0.0F, 0.0F, 0.2F}, WheelSector::magic) == WheelSector::none);
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {0.5F, 0.866F, 1.0F}, WheelSector::stunt) == WheelSector::stunt);
    assert(pal4::inject::SelectGamepadCombatWheelSector(
        {0.342F, 0.94F, 1.0F}, WheelSector::stunt) == WheelSector::magic);
    const auto magic_plan =
        pal4::inject::BuildGamepadCombatWheelNavigationPlan(
            WheelSector::magic);
    assert(magic_plan.count == 3);
    assert(magic_plan.directions[0] ==
        pal4::inject::GamepadDpadDirection::up);
    const auto flee_plan =
        pal4::inject::BuildGamepadCombatWheelNavigationPlan(
            WheelSector::flee);
    assert(flee_plan.count == 5);
    assert(flee_plan.directions[0] ==
        pal4::inject::GamepadDpadDirection::right);
    assert(flee_plan.directions[3] ==
        pal4::inject::GamepadDpadDirection::down);
    const auto centre_plan =
        pal4::inject::BuildGamepadCombatWheelNavigationPlan(
            WheelSector::none);
    assert(centre_plan.count == 4);
    assert(centre_plan.directions[0] ==
        pal4::inject::GamepadDpadDirection::up);
    assert(centre_plan.directions[3] ==
        pal4::inject::GamepadDpadDirection::down);
    assert(pal4::inject::ShouldCenterGamepadCombatWheel(
        true, WheelSector::magic, 0.0F));
    assert(!pal4::inject::ShouldCenterGamepadCombatWheel(
        true, WheelSector::none, 0.0F));
    assert(!pal4::inject::ShouldCenterGamepadCombatWheel(
        false, WheelSector::magic, 0.0F));
    assert(!pal4::inject::ShouldCenterGamepadCombatWheel(
        true, WheelSector::magic, 0.3F));
}

void TestGiTalkVoiceVolume() {
    float parsed = -1.0F;
    assert(pal4::inject::TryParseGiTalkVolume("0", &parsed));
    assert(parsed == 0.0F);
    assert(pal4::inject::TryParseGiTalkVolume("0.375", &parsed));
    assert(parsed == 0.375F);
    assert(pal4::inject::TryParseGiTalkVolume("1", &parsed));
    assert(parsed == 1.0F);
    assert(pal4::inject::TryParseGiTalkVolume("3", &parsed));
    assert(parsed == 3.0F);
    assert(!pal4::inject::TryParseGiTalkVolume("-0.1", &parsed));
    assert(!pal4::inject::TryParseGiTalkVolume("nan", &parsed));
    assert(!pal4::inject::TryParseGiTalkVolume("50%", &parsed));
    assert(pal4::inject::ClampGiTalkVolume(-0.5F) == 0.0F);
    assert(pal4::inject::ClampGiTalkVolume(1.5F) == 1.5F);
    assert(pal4::inject::ClampGiTalkVolume(2.5F) == 2.5F);
    assert(pal4::inject::ClampGiTalkVolume(3.5F) == 3.0F);
    assert(pal4::inject::IsGiTalkVoiceResource(R"(gamedata\PALSOUND\A123.mp3)"));
    assert(pal4::inject::IsGiTalkVoiceResource("PALSOUND/A123.MP3"));
    assert(!pal4::inject::IsGiTalkVoiceResource(R"(gamedata\PALMUSIC\A123.mp3)"));
    assert(!pal4::inject::IsGiTalkVoiceResource(R"(gamedata\PALSOUND\A123.wav)"));

    const auto decrease = pal4::inject::ResolveGiTalkVolumeHotkey(
        WM_KEYDOWN, VK_OEM_4, false);
    assert(decrease.consume);
    assert(decrease.step_direction == -1);
    const auto increase = pal4::inject::ResolveGiTalkVolumeHotkey(
        WM_KEYDOWN, VK_OEM_6, false);
    assert(increase.consume);
    assert(increase.step_direction == 1);
    const auto repeated = pal4::inject::ResolveGiTalkVolumeHotkey(
        WM_KEYDOWN, VK_OEM_6, true);
    assert(repeated.consume);
    assert(repeated.step_direction == 0);
    const auto released = pal4::inject::ResolveGiTalkVolumeHotkey(
        WM_KEYUP, VK_OEM_4, false);
    assert(released.consume);
    assert(released.step_direction == 0);
    const auto character = pal4::inject::ResolveGiTalkVolumeHotkey(
        WM_CHAR, '[', false);
    assert(character.consume);
    assert(character.step_direction == 0);
    const auto unrelated = pal4::inject::ResolveGiTalkVolumeHotkey(
        WM_KEYDOWN, 'P', false);
    assert(!unrelated.consume);
    assert(unrelated.step_direction == 0);

    assert(std::fabs(pal4::inject::StepGiTalkVolume(0.50F, -1) - 0.45F) < 0.0001F);
    assert(std::fabs(pal4::inject::StepGiTalkVolume(0.50F, 1) - 0.55F) < 0.0001F);
    assert(pal4::inject::StepGiTalkVolume(0.0F, -1) == 0.0F);
    assert(pal4::inject::StepGiTalkVolume(1.0F, 1) == 1.05F);
    assert(pal4::inject::StepGiTalkVolume(2.95F, 1) == 3.0F);
    assert(pal4::inject::StepGiTalkVolume(3.0F, 1) == 3.0F);
    assert(pal4::inject::StepGiTalkVolume(0.0999998F, -1) == 0.05F);
    assert(pal4::inject::GiTalkVolumeWaveCount(0.0F) == 0);
    assert(pal4::inject::GiTalkVolumeWaveCount(0.05F) == 1);
    assert(pal4::inject::GiTalkVolumeWaveCount(1.0F) == 1);
    assert(pal4::inject::GiTalkVolumeWaveCount(1.05F) == 2);
    assert(pal4::inject::GiTalkVolumeWaveCount(2.0F) == 2);
    assert(pal4::inject::GiTalkVolumeWaveCount(2.05F) == 3);
    assert(pal4::inject::GiTalkVolumeWaveCount(3.0F) == 3);
    assert(pal4::inject::ComputeGiTalkVolumeOsdOpacity(0) == 1.0F);
    assert(pal4::inject::ComputeGiTalkVolumeOsdOpacity(1250) == 1.0F);
    assert(std::fabs(
        pal4::inject::ComputeGiTalkVolumeOsdOpacity(1425) - 0.5F) < 0.0001F);
    assert(pal4::inject::ComputeGiTalkVolumeOsdOpacity(1600) == 0.0F);
}

void TestBorderlessWindowPlan() {
    constexpr std::uint32_t kCaptionedWindowStyle =
        0x00CF0000U | 0x10000000U | 0x02000000U;
    constexpr std::uint32_t kFramedExtendedStyle =
        0x00000100U | 0x00000200U | 0x00000008U;
    const auto plan = pal4::inject::BuildBorderlessWindowPlan(
        kCaptionedWindowStyle,
        kFramedExtendedStyle,
        -1920,
        0,
        0,
        1080);
    assert((plan.style & 0x80000000U) != 0);
    assert((plan.style & 0x00C00000U) == 0);
    assert((plan.style & 0x10000000U) != 0);
    assert((plan.style & 0x02000000U) != 0);
    assert((plan.extended_style & 0x00000100U) == 0);
    assert((plan.extended_style & 0x00000200U) == 0);
    assert((plan.extended_style & 0x00000008U) == 0);
    assert((plan.extended_style & 0x00040000U) != 0);
    assert(plan.x == -1920);
    assert(plan.y == 0);
    assert(plan.width == 1920);
    assert(plan.height == 1080);

    const auto empty = pal4::inject::BuildBorderlessWindowPlan(0, 0, 10, 20, 5, 15);
    assert(empty.width == 0);
    assert(empty.height == 0);
    assert(pal4::inject::ResolveBorderlessPresentFullscreenFlag(1, true, true) == 0);
    assert(pal4::inject::ResolveBorderlessPresentFullscreenFlag(1, true, false) == 1);
    assert(pal4::inject::ResolveBorderlessPresentFullscreenFlag(1, false, true) == 1);
}

void TestInputLogic() {
    assert(pal4::inject::IsCapturedMouseRecenterPosition(960, 540, 1920, 1080));
    assert(pal4::inject::IsCapturedMouseRecenterPosition(958, 542, 1920, 1080));
    assert(!pal4::inject::IsCapturedMouseRecenterPosition(950, 540, 1920, 1080));
    assert(!pal4::inject::IsCapturedMouseRecenterPosition(0, 0, 0, 1080));

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

    pal4::inject::SynchronousUiMessageQueue ui_queue;
    const auto ticket = ui_queue.Push(command);
    pal4::inject::SynchronousUiMessageQueue::Ticket popped;
    assert(ui_queue.TryPop(&popped));
    assert(popped == ticket);
    assert(popped->command.msg == WM_KEYDOWN);
    ui_queue.Complete(popped, true, false);
    bool message_handled = true;
    std::string dispatch_error;
    assert(ui_queue.Wait(ticket, 10, &message_handled, &dispatch_error));
    assert(!message_handled);
    assert(dispatch_error.empty());
}

void TestRuntimeEventLog() {
    auto& state = pal4::inject::GetRuntimeState();
    state.InitializeInventory(pal4::inject::BuildHookInventorySkeleton());
    assert(!state.GetHookLogEnabled(HookId::process_ui_event));
    assert(!state.GetHookLogEnabled(HookId::load_font_file));
    state.SetMsaaLevel(pal4::inject::MsaaLevel::x2);
    state.SetBinkScalingMode(pal4::inject::BinkScalingMode::fill_width_crop);
    state.SetGiTalkVolume(0.55F);
    state.SetGiTalkVolumeApplied(true, "voice_key=A123 multiplier=0.55");
    state.SetBorderlessWindowEnabled(true);
    state.SetBorderlessMonitor(R"(\\.\DISPLAY2)");
    state.SetBorderlessWindowApplied(true, "monitor=0,0 1920x1080");
    state.AppendEventLog("event-1");
    state.AppendEventLog("event-2");
    state.SetCrashHandlerReady(true);
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
    assert(snapshot.msaa_level == pal4::inject::MsaaLevel::x2);
    assert(snapshot.bink_scaling_mode ==
           pal4::inject::BinkScalingMode::fill_width_crop);
    assert(snapshot.gi_talk_volume == 0.55F);
    assert(snapshot.gi_talk_volume_applied);
    assert(snapshot.gi_talk_volume_summary == "voice_key=A123 multiplier=0.55");
    assert(snapshot.borderless_window_enabled);
    assert(snapshot.borderless_window_applied);
    assert(snapshot.borderless_monitor == R"(\\.\DISPLAY2)");
    assert(snapshot.borderless_window_summary == "monitor=0,0 1920x1080");
    assert(snapshot.active_ui_profile == pal4::inject::UiProfile::centered_800x600);
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
    STARTUPINFOA startup{};
    pal4::inject::ConfigureProcessStartupInfo(options, &startup);
    assert(startup.cb == sizeof(startup));
    assert((startup.dwFlags & STARTF_USESHOWWINDOW) == 0);

    options.background_window = true;
    pal4::inject::ConfigureProcessStartupInfo(options, &startup);
    assert((startup.dwFlags & STARTF_USESHOWWINDOW) != 0);
    assert(startup.wShowWindow == SW_HIDE);
    options.background_window = false;
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
    assert(std::string(pal4::inject::ToString(pal4::inject::UiProfile::centered_800x600)) == "centered_800x600");
    assert(std::string(pal4::inject::ToString(pal4::inject::UiProfile::widescreen_1067x600)) == "widescreen_1067x600");
    assert(!pal4::inject::IsWideAspectResolution(800, 600));
    assert(!pal4::inject::IsWideAspectResolution(1024, 768));
    assert(!pal4::inject::IsWideAspectResolution(1280, 1024));
    assert(pal4::inject::IsWideAspectResolution(1280, 800));
    assert(pal4::inject::IsWideAspectResolution(1920, 1080));
    assert(pal4::inject::IsWideAspectResolution(3440, 1440));
    assert(pal4::inject::UsesOriginalWideRendererVariant(1280, 800));
    assert(!pal4::inject::UsesOriginalWideRendererVariant(1920, 1080));

    const auto plan_1280_800 = pal4::inject::BuildCeguiWidescreenPlan(1280, 800);
    assert(plan_1280_800.apply);
    assert(plan_1280_800.use_original_variant);
    assert(plan_1280_800.uniform_scale > 1.3333F && plan_1280_800.uniform_scale < 1.3334F);
    assert(plan_1280_800.horizontal_bias_pixels > 106.66F && plan_1280_800.horizontal_bias_pixels < 106.67F);
    assert(plan_1280_800.logical_horizontal_padding > 79.99F && plan_1280_800.logical_horizontal_padding < 80.01F);
    assert(!pal4::inject::ShouldDrawOriginalUiPillarboxMask(plan_1280_800));

    const auto plan_1920_1080 = pal4::inject::BuildCeguiWidescreenPlan(1920, 1080);
    assert(plan_1920_1080.apply);
    assert(!plan_1920_1080.use_original_variant);
    assert(plan_1920_1080.uniform_scale == 1.8F);
    assert(plan_1920_1080.horizontal_bias_pixels == 240.0F);
    assert(plan_1920_1080.logical_horizontal_padding > 133.33F && plan_1920_1080.logical_horizontal_padding < 133.34F);
    assert(plan_1920_1080.render_rect_left > -133.34F && plan_1920_1080.render_rect_left < -133.33F);
    assert(plan_1920_1080.render_rect_right > 933.33F && plan_1920_1080.render_rect_right < 933.34F);
    assert(pal4::inject::ShouldDrawOriginalUiPillarboxMask(plan_1920_1080));
    const float movie_frame_width =
        pal4::inject::ComputeWidescreenEdgeToEdgeLogicalWidth(
            plan_1920_1080,
            800.0F);
    assert(movie_frame_width > 1066.66F && movie_frame_width < 1066.67F);
    const float main_menu_top_center_x = pal4::inject::ComputeWidescreenHudLogicalX(
        plan_1920_1080,
        72.0F,
        pal4::inject::WidescreenHudAnchor::left_edge);
    const float main_menu_top_center_width =
        pal4::inject::ComputeWidescreenEdgeToEdgeLogicalWidth(
            plan_1920_1080,
            462.0F);
    const float main_menu_top_right_x = pal4::inject::ComputeWidescreenHudLogicalX(
        plan_1920_1080,
        534.0F,
        pal4::inject::WidescreenHudAnchor::right_edge);
    assert(main_menu_top_center_x > -61.34F && main_menu_top_center_x < -61.33F);
    assert(main_menu_top_center_width > 728.66F && main_menu_top_center_width < 728.67F);
    assert(std::fabs(
        main_menu_top_center_x + main_menu_top_center_width - main_menu_top_right_x) <
        0.01F);
    const auto main_menu_top_center_plan =
        pal4::inject::BuildWidescreenUiWindowPlan(
            plan_1920_1080,
            pal4::inject::WidescreenUiHorizontalMode::stretch_between_edges,
            72.0F,
            462.0F);
    assert(main_menu_top_center_plan.set_x);
    assert(main_menu_top_center_plan.set_width);
    assert(std::fabs(main_menu_top_center_plan.x - main_menu_top_center_x) < 0.01F);
    assert(std::fabs(main_menu_top_center_plan.width - main_menu_top_center_width) < 0.01F);
    const auto centered_dialog_plan = pal4::inject::BuildWidescreenUiWindowPlan(
        plan_1920_1080,
        pal4::inject::WidescreenUiHorizontalMode::preserve,
        200.0F,
        400.0F);
    assert(!centered_dialog_plan.set_x);
    assert(!centered_dialog_plan.set_width);
    assert(centered_dialog_plan.x == 200.0F);
    const auto padded_offset_plan = pal4::inject::BuildWidescreenUiWindowPlan(
        plan_1920_1080,
        pal4::inject::WidescreenUiHorizontalMode::offset_by_padding_factor,
        251.0F,
        298.0F,
        1.0F,
        509.0F,
        -70.0F);
    assert(padded_offset_plan.set_x);
    assert(padded_offset_plan.set_y);
    assert(!padded_offset_plan.set_width);
    assert(padded_offset_plan.x > 384.33F && padded_offset_plan.x < 384.34F);
    assert(padded_offset_plan.y == 439.0F);
    const auto load_top_center_plan = pal4::inject::BuildWidescreenUiWindowPlan(
        plan_1920_1080,
        pal4::inject::WidescreenUiHorizontalMode::stretch_between_edges,
        82.0F,
        318.0F);
    const auto load_top_right_plan = pal4::inject::BuildWidescreenUiWindowPlan(
        plan_1920_1080,
        pal4::inject::WidescreenUiHorizontalMode::right_edge,
        400.0F,
        400.0F);
    assert(load_top_center_plan.set_x);
    assert(load_top_center_plan.set_width);
    assert(load_top_right_plan.set_x);
    assert(!load_top_right_plan.set_width);
    assert(std::fabs(
        load_top_center_plan.x + load_top_center_plan.width - load_top_right_plan.x) <
        0.01F);
    const auto* picture_preview_profile =
        pal4::inject::FindWidescreenUiProfileByRootName("picturePreviewWindow/Root");
    assert(picture_preview_profile);
    assert(
        picture_preview_profile->pillarbox_policy ==
        pal4::inject::WidescreenUiPillarboxPolicy::remove);
    assert(picture_preview_profile->rule_count == 9);
    const auto* world_map_profile =
        pal4::inject::FindWidescreenUiProfileByRootName("WorldMap/Root");
    assert(world_map_profile);
    assert(
        world_map_profile->pillarbox_policy ==
        pal4::inject::WidescreenUiPillarboxPolicy::preserve);
    assert(world_map_profile->rule_count == 0);
    assert(centered_dialog_plan.width == 400.0F);
    assert(
        pal4::inject::ComputeWidescreenEdgeToEdgeLogicalWidth(
            plan_1280_800,
            800.0F) == 800.0F);
    assert(!pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        false, false, false, false, false));
    assert(!pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        false, true, false, false, false));
    assert(!pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        false, false, true, false, false));
    assert(pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        false, true, true, false, false));
    assert(pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        true, false, false, false, false));
    assert(pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        false, false, false, true, false));
    assert(!pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        false, true, true, false, true));
    assert(!pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        false, false, false, true, true));
    assert(pal4::inject::ShouldDrawOriginalUiPillarboxForVisibleRoots(
        true, true, true, false, true));
    const auto* in_game_toolbar_profile =
        pal4::inject::FindWidescreenUiProfileByRootName("sysToolBar/Root");
    assert(in_game_toolbar_profile);
    assert(
        in_game_toolbar_profile->pillarbox_policy ==
        pal4::inject::WidescreenUiPillarboxPolicy::remove);
    assert(in_game_toolbar_profile->rule_count == 2);
    struct ExpectedInGameProfile {
        const char* trigger_window_name;
        std::size_t rule_count;
    };
    constexpr auto expected_in_game_profiles = std::to_array<ExpectedInGameProfile>({
        {"sysToolBar/Root", 2},
        {"frameToolbar/Root", 1},
        {"decorator/Root", 3},
        {"gameInfo/Frame", 1},
        {"roleStateWindow/Root", 1},
        {"PropertyWindow/Root", 7},
        {"EquipmentWindow/Root", 7},
        {"magicWindow/Root", 7},
        {"SmithWindow/Root", 4},
        {"MissionWindow/Root", 3},
        {"SystemSetting/Root", 6},
    });
    for (const auto& expected : expected_in_game_profiles) {
        const auto* profile = pal4::inject::FindWidescreenUiProfileByRootName(
            expected.trigger_window_name);
        assert(profile);
        assert(profile->rule_count == expected.rule_count);
    }
    struct ExpectedTransitionalProfile {
        const char* trigger_window_name;
        std::size_t rule_count;
        pal4::inject::WidescreenUiPillarboxPolicy policy;
    };
    constexpr auto expected_transitional_profiles =
        std::to_array<ExpectedTransitionalProfile>({
            {"loading/Root", 4, pal4::inject::WidescreenUiPillarboxPolicy::remove},
            {"CombatMainWindow/Root", 4,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
            {"CombatRoleState/Root", 4,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
            {"CombatActionConsoleWindow/StaticControlPanel", 1,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
            {"CombatMagicSelectWindow/Root", 1,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
            {"CombatPropertySelectWindow/Root", 1,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
            {"CombatStuntSelectWindow/Root", 1,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
            {"CombatSelectWindow/Root", 1,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
            {"CombatEndingWindow/Root", 2,
             pal4::inject::WidescreenUiPillarboxPolicy::unchanged},
        });
    for (const auto& expected : expected_transitional_profiles) {
        const auto* profile = pal4::inject::FindWidescreenUiProfileByRootName(
            expected.trigger_window_name);
        assert(profile);
        assert(profile->rule_count == expected.rule_count);
        assert(profile->pillarbox_policy == expected.policy);
    }
    const auto profile_has_window_rule = [](
        const pal4::inject::WidescreenUiProfile& profile,
        const std::string_view window_name) {
        for (std::size_t index = 0; index < profile.rule_count; ++index) {
            if (profile.rules[index].window_name == window_name) {
                return true;
            }
        }
        return false;
    };
    const auto* combat_main_profile =
        pal4::inject::FindWidescreenUiProfileByRootName("CombatMainWindow/Root");
    assert(combat_main_profile);
    assert(!profile_has_window_rule(
        *combat_main_profile, "CombatMainWindow/StaticActionSequence"));
    assert(!profile_has_window_rule(
        *combat_main_profile, "CombatMainWindow/StaticRole1"));
    const auto* combat_ending_profile =
        pal4::inject::FindWidescreenUiProfileByRootName("CombatEndingWindow/Root");
    assert(combat_ending_profile);
    assert(!profile_has_window_rule(
        *combat_ending_profile, "CombatEndingWindow/PanelRole0"));
    assert(!profile_has_window_rule(
        *combat_ending_profile, "CombatEndingWindow/PanelRole3"));
    const float centered_ui_x =
        pal4::inject::ComputeCenteredUiLogicalX(plan_1920_1080, 102.0F);
    assert(centered_ui_x > 235.33F && centered_ui_x < 235.34F);
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

    const auto active_plan_1920_1080 =
        pal4::inject::BuildUiViewportPlan(
            1920,
            1080,
            pal4::inject::UiProfile::centered_800x600);
    float physical_x = 0.0F;
    float physical_y = 0.0F;
    assert(pal4::inject::UiLogicalToPhysical(
        active_plan_1920_1080,
        0.0F,
        0.0F,
        &physical_x,
        &physical_y));
    assert(physical_x == 240.0F);
    assert(physical_y == 0.0F);
    float logical_x = 0.0F;
    float logical_y = 0.0F;
    assert(pal4::inject::PhysicalToUiLogical(
        active_plan_1920_1080,
        240.0F,
        0.0F,
        &logical_x,
        &logical_y));
    assert(logical_x > -0.001F && logical_x < 0.001F);
    assert(logical_y > -0.001F && logical_y < 0.001F);
    assert(pal4::inject::FullscreenLogicalToPhysical(
        active_plan_1920_1080,
        133.33334F,
        0.0F,
        &physical_x,
        &physical_y));
    assert(physical_x > 239.99F && physical_x < 240.01F);
    assert(physical_y > -0.001F && physical_y < 0.001F);
    const auto active_plan_1280_720 =
        pal4::inject::BuildUiViewportPlan(
            1280,
            720,
            pal4::inject::UiProfile::centered_800x600);
    assert(active_plan_1280_720.uniform_scale == 1.2F);
    assert(pal4::inject::BattleOverlayLogicalToPhysical(
        active_plan_1280_720,
        100.0F,
        50.0F,
        &physical_x,
        &physical_y));
    assert(physical_x > 179.99F && physical_x < 180.01F);
    assert(physical_y > 89.99F && physical_y < 90.01F);

    assert(pal4::inject::BattleOverlayLogicalToPhysical(
        active_plan_1920_1080,
        100.0F,
        50.0F,
        &physical_x,
        &physical_y));
    assert(physical_x > 179.99F && physical_x < 180.01F);
    assert(physical_y > 89.99F && physical_y < 90.01F);

    const auto active_plan_3840_2160 =
        pal4::inject::BuildUiViewportPlan(
            3840,
            2160,
            pal4::inject::UiProfile::centered_800x600);
    assert(active_plan_3840_2160.uniform_scale == 3.6F);
    assert(pal4::inject::BattleOverlayLogicalToPhysical(
        active_plan_3840_2160,
        100.0F,
        50.0F,
        &physical_x,
        &physical_y));
    assert(physical_x > 179.99F && physical_x < 180.01F);
    assert(physical_y > 89.99F && physical_y < 90.01F);

    assert(pal4::inject::CombatResultOverlayLogicalToUiLogical(
        active_plan_1920_1080,
        400.0F,
        280.0F,
        &logical_x,
        &logical_y));
    assert(logical_x > 533.32F && logical_x < 533.34F);
    assert(logical_y > 279.99F && logical_y < 280.01F);
    assert(pal4::inject::CombatResultOverlayLogicalToUiLogical(
        active_plan_3840_2160,
        400.0F,
        280.0F,
        &logical_x,
        &logical_y));
    assert(logical_x > 533.32F && logical_x < 533.34F);
    assert(logical_y > 279.99F && logical_y < 280.01F);

    const auto widescreen_plan =
        pal4::inject::BuildUiViewportPlan(
            1920,
            1080,
            pal4::inject::UiProfile::widescreen_1067x600);
    assert(widescreen_plan.logical_width == 1067.0F);
    assert(widescreen_plan.logical_height == 600.0F);
    assert(widescreen_plan.physical_width > 1919.0F);
    assert(widescreen_plan.physical_width <= 1920.0F);
    assert(widescreen_plan.physical_height > 1079.0F);
    assert(widescreen_plan.physical_height <= 1080.0F);
    assert(pal4::inject::UiLogicalToPhysical(
        widescreen_plan,
        533.5F,
        300.0F,
        &physical_x,
        &physical_y));
    assert(physical_x > 959.0F && physical_x < 961.0F);
    assert(physical_y > 539.0F && physical_y < 541.0F);
    assert(pal4::inject::PhysicalToUiLogical(
        widescreen_plan,
        physical_x,
        physical_y,
        &logical_x,
        &logical_y));
    assert(logical_x > 533.49F && logical_x < 533.51F);
    assert(logical_y > 299.99F && logical_y < 300.01F);
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

    const auto target_1920_1080 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "system",
            pal4::inject::BuildCeguiWidescreenPlan(1920, 1080));
    assert(target_1920_1080.apply);
    assert(target_1920_1080.native_width == 800.0F);
    assert(target_1920_1080.native_height == 600.0F);
    assert(target_1920_1080.notify_width == 1440.0F);
    assert(target_1920_1080.notify_height == 1080.0F);

    const auto target_1600_900 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "systemBold",
            pal4::inject::BuildCeguiWidescreenPlan(1600, 900));
    assert(target_1600_900.apply);
    assert(target_1600_900.native_width == 800.0F);
    assert(target_1600_900.native_height == 600.0F);
    assert(target_1600_900.notify_width == 1200.0F);
    assert(target_1600_900.notify_height == 900.0F);

    const auto target_1024_768 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "system",
            pal4::inject::BuildCeguiWidescreenPlan(1024, 768));
    assert(!target_1024_768.apply);
    assert(target_1024_768.notify_width == 800.0F);
    assert(target_1024_768.notify_height == 600.0F);

    const auto dialog_target_1920_1080 =
        pal4::inject::BuildKnownDynamicFontResyncTarget(
            "dialog_simsun",
            pal4::inject::BuildCeguiWidescreenPlan(1920, 1080));
    assert(dialog_target_1920_1080.apply);
    assert(dialog_target_1920_1080.native_width == 800.0F);
    assert(dialog_target_1920_1080.native_height == 600.0F);
    assert(dialog_target_1920_1080.notify_width == 1440.0F);
    assert(dialog_target_1920_1080.notify_height == 1080.0F);
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

void TestBugReportHelpers() {
    const auto temp_root =
        std::filesystem::temp_directory_path() / "pal4_inject_bug_report_unit_test";
    std::error_code ignored;
    std::filesystem::remove_all(temp_root, ignored);
    std::filesystem::create_directories(temp_root);

    const auto older_report = temp_root / "pal4_inject_crash_pid1_tid1_code0x1_tick1.txt";
    const auto latest_report = temp_root / "pal4_inject_crash_pid2_tid2_code0x2_tick2.txt";
    const auto latest_dump = temp_root / "pal4_inject_crash_pid2_tid2_code0x2_tick2.dmp";
    const auto runtime_log = temp_root / "pal4_inject_runtime.log";
    {
        std::ofstream(older_report) << "exception_code=old\n";
        std::ofstream(latest_report)
            << "exception_code=0xC0000005\n"
            << "file=C:\\Users\\Alice\\PAL4\\gamepatch\\asset.dds\n"
            << "access_token=do-not-share\n";
        std::ofstream(latest_dump, std::ios::binary) << "dump";
        std::ofstream(runtime_log)
            << "runtime path C:\\Users\\Alice\\PAL4\\runtime.dll\n"
            << "last line\n";
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(older_report, now - std::chrono::seconds(5));
    std::filesystem::last_write_time(latest_report, now);

    const auto data = pal4::inject::LoadLatestBugReportData(
        temp_root,
        {{"C:\\Users\\Alice\\PAL4", "<游戏目录>"}});
    assert(data.crash_report_path == latest_report);
    assert(data.crash_dump_path == latest_dump);
    assert(data.runtime_log_path == runtime_log);
    assert(data.HasCrashReport());
    assert(data.HasRuntimeLog());
    assert(data.sanitized_crash_report.find("<游戏目录>") != std::string::npos);
    assert(data.sanitized_crash_report.find("Alice") == std::string::npos);
    assert(data.sanitized_crash_report.find("do-not-share") == std::string::npos);
    assert(data.sanitized_crash_report.find("access_token=<已隐藏>") != std::string::npos);

    pal4::inject::BugReportBodyOptions options{};
    options.description = "载入存档后闪退";
    options.version = "v0.2.0";
    options.build_id = "unit-test";
    auto body = pal4::inject::BuildBugReportBody(data, options);
    assert(body.find("载入存档后闪退") != std::string::npos);
    assert(body.find("exception_code") == std::string::npos);
    options.include_crash_report = true;
    options.include_runtime_log = true;
    body = pal4::inject::BuildBugReportBody(data, options);
    assert(body.find("exception_code=0xC0000005") != std::string::npos);
    assert(body.find("last line") != std::string::npos);
    assert(body.find("未上传 minidump") != std::string::npos);

    const auto url = pal4::inject::BuildGiteeNewIssueUrl(
        "https://gitee.com/betesla/pal4_inject/issues/new",
        "崩溃反馈",
        body,
        3000);
    assert(url.starts_with("https://gitee.com/betesla/pal4_inject/issues/new?"));
    assert(url.find("issue%5Btitle%5D=") != std::string::npos);
    assert(url.find("issue%5Bdescription%5D=") != std::string::npos);
    assert(url.size() <= 3000);
    assert(url.find("do-not-share") == std::string::npos);

    std::filesystem::remove_all(temp_root, ignored);
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
    const auto tree = ReadUiSnapshot(pipe_name);
    const int width = tree.root.rect.right - tree.root.rect.left;
    const int height = tree.root.rect.bottom - tree.root.rect.top;
    assert(width > 0 && height > 0);

    const auto viewport = pal4::inject::BuildUiViewportPlan(
        width,
        height,
        pal4::inject::UiProfile::centered_800x600);
    pal4::inject::UiRefClickPlan click{};
    std::string error;
    assert(pal4::inject::BuildUiRefClickPlan(tree, ref, viewport, &click, &error));

    const std::uint32_t lparam =
        ((click.client_y & 0xFFFFu) << 16) | (click.client_x & 0xFFFFu);
    const struct {
        std::uint32_t message;
        std::uint32_t wparam;
    } messages[] = {
        {WM_MOUSEMOVE, 0},
        {WM_LBUTTONDOWN, MK_LBUTTON},
        {WM_LBUTTONUP, 0},
    };
    for (const auto& item : messages) {
        ProtocolCommand command{};
        command.kind = ProtocolCommandKind::enqueue_ui_message;
        command.ui_message = {item.message, item.wparam, lparam, false};
        const auto response = SendCommand(pipe_name, command);
        if (!response.ok) {
            std::cerr << "OS-queue click failed: ref=" << ref
                      << " message=" << response.message << "\n";
        }
        assert(response.ok);
    }
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
    TestMainMenuBrandingPlan();
    TestPackagedRuntimeLayoutPaths();
    TestLooseFileOverlayPaths();
    TestX86TrampolineCopiesLargeImmediateStackFrame();
    TestX86TrampolineCopiesTextScriptPrologue();
    TestX86TrampolineCopiesRwCameraDispatchThunk();
    TestLooseFileLoadLogFormatting();
    TestHookInventory();
    TestHookManagerBootstrapReplacementCoverage();
    TestDpiAwarenessStrings();
    TestMsaaLevelStrings();
    TestBinkScalingModeStrings();
    TestScriptModeStrings();
    TestInheritedScriptModeOverride();
    TestInjectFeatureCatalog();
    TestInjectSettingsRoundTrip();
    TestGamepadLogic();
    TestGiTalkVoiceVolume();
    TestBorderlessWindowPlan();
    TestProtocolRoundTrip();
    TestUiSnapshotSerialization();
    TestUiInputPlan();
    TestMemoryDebugHelpers();
    TestMemoryRuntimeHelpers();
    TestInputLogic();
    TestInputQueue();
    TestRuntimeEventLog();
    TestLauncherNaming();
    TestCameraPitchUnlockPatchMetadata();
    TestCameraPitchGuardMath();
    TestCeguiWidescreenPlanMath();
    TestCeguiDynamicFontResyncMath();
    TestCrashCaptureHelpers();
    TestBugReportHelpers();
    TestAspectRatioLayoutMath();
    MaybeRunIntegrationSmoke();
    std::cout << "pal4_inject_tests: ok\n";
    return 0;
}

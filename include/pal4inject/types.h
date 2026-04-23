#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pal4inject/gamepad.h"

namespace pal4::inject {

enum class CallingConvention : std::uint8_t {
    cdecl_call = 0,
    stdcall_call,
    thiscall_call,
    fastcall_call,
};

enum class HookMode : std::uint8_t {
    observe_only = 0,
    mirror_compare,
    replace_with_fallback,
    replace_strict,
};

enum class MsaaLevel : std::uint8_t {
    off = 0,
    x2,
    x4,
    x8,
};

enum class UiTextureFilter : std::uint8_t {
    linear = 0,
    nearest,
};

enum class ScriptMode : std::uint8_t {
    inherit = 0,
    cs,
    csb,
};

enum class HookId : std::uint8_t {
    process_ui_event = 0,
    handle_ui_message,
    simulate_key_press_and_release,
    process_inputs,
    update_input_device_state,
    initialize_direct_input,
    gi_talk,
    cegui_renderer_constructor_2,
    cegui_system_initialize,
    load_font_file,
    dialog_handle_text_display,
    setup_minimap_texture,
    camera_update_matrix,
    d3d9_set_present_parameters,
    pal4_main_wndproc,
    handle_player_input_events,
    combat_console_set_image_position,
    combat_console_set_image_position_2,
    ui_show_combat_result,
};

struct HookDescriptor {
    HookId id = HookId::process_ui_event;
    std::uint32_t ida_ea = 0;
    CallingConvention cc = CallingConvention::cdecl_call;
    HookMode mode = HookMode::observe_only;
    std::vector<std::uint8_t> expected_prologue;
    std::size_t patch_span = 0;
    void* replacement = nullptr;
    void* original_trampoline = nullptr;
    std::uint32_t bootstrap_order = 1000;
    bool bootstrap_required = false;
};

struct HookStatus {
    HookId id = HookId::process_ui_event;
    HookMode mode = HookMode::observe_only;
    HookMode preferred_active_mode = HookMode::replace_with_fallback;
    bool log_enabled = false;
    bool installed = false;
    std::uint64_t call_count = 0;
    std::string last_error;
};

struct UiMessageCommand {
    std::uint32_t msg = 0;
    std::uint32_t wparam = 0;
    std::uint32_t lparam = 0;
    bool bypass_os_queue = true;
};

struct RuntimeSnapshot {
    bool bootstrap_ready = false;
    bool hooks_ready = false;
    bool pipe_ready = false;
    bool ui_dispatch_ready = false;
    bool crash_handler_ready = false;
    bool system_font_oversample_enabled = false;
    bool gamepad_enabled = true;
    bool gamepad_log_enabled = false;
    bool gamepad_connected = false;
    GamepadInputContext gamepad_context{};
    std::uintptr_t main_module_base = 0;
    MsaaLevel msaa_level = MsaaLevel::off;
    UiTextureFilter ui_texture_filter = UiTextureFilter::nearest;
    HookStatus process_ui_event{};
    std::uint32_t current_paliv_entry = 0;
    std::uint32_t last_paliv_entry_observed = 0;
    std::string last_ui_event;
    std::string last_error;
    std::string last_font_sync_summary;
    bool last_font_sync_ok = false;
    std::string last_crash_summary;
    std::string last_crash_report_path;
    std::string last_crash_dump_path;
    std::string event_log_tail;
    std::vector<HookStatus> active_hooks;
};

struct InputFrame {
    std::uint32_t frame_index = 0;
    std::vector<UiMessageCommand> commands;
};

struct InputManagerSnapshot {
    bool available = false;
    std::int32_t mouse_x = 0;
    std::int32_t mouse_y = 0;
    std::uint32_t button_mask = 0;
    std::uint32_t last_virtual_key = 0;
};

class IInputSource {
public:
    virtual ~IInputSource() = default;
    virtual InputFrame CaptureFrame() = 0;
};

const char* ToString(CallingConvention cc) noexcept;
const char* ToString(HookMode mode) noexcept;
const char* ToString(MsaaLevel level) noexcept;
const char* ToString(UiTextureFilter filter) noexcept;
const char* ToString(ScriptMode mode) noexcept;
const char* ToString(HookId id) noexcept;

bool TryParseHookMode(std::string_view text, HookMode* out) noexcept;
bool TryParseMsaaLevel(std::string_view text, MsaaLevel* out) noexcept;
bool TryParseUiTextureFilter(std::string_view text, UiTextureFilter* out) noexcept;
bool TryParseScriptMode(std::string_view text, ScriptMode* out) noexcept;
bool TryParseHookId(std::string_view text, HookId* out) noexcept;
std::optional<std::uint32_t> ScriptModeToCsbFlag(ScriptMode mode) noexcept;

}  // namespace pal4::inject

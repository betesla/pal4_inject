#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

enum class HookId : std::uint8_t {
    process_ui_event = 0,
    handle_ui_message,
    simulate_key_press_and_release,
    process_inputs,
    update_input_device_state,
    initialize_direct_input,
    pal4_main_wndproc,
    handle_player_input_events,
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
};

struct HookStatus {
    HookId id = HookId::process_ui_event;
    HookMode mode = HookMode::observe_only;
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
    HookStatus process_ui_event{};
    std::uint32_t current_paliv_entry = 0;
    std::uint32_t last_paliv_entry_observed = 0;
    std::string last_ui_event;
    std::string last_error;
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
const char* ToString(HookId id) noexcept;

bool TryParseHookMode(std::string_view text, HookMode* out) noexcept;
bool TryParseHookId(std::string_view text, HookId* out) noexcept;

}  // namespace pal4::inject

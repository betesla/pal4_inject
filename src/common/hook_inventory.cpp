#include "pal4inject/hook_inventory.h"

#include "pal4inject/ida_addresses.h"

namespace pal4::inject {

std::vector<HookDescriptor> BuildHookInventorySkeleton() {
    return {
        {
            HookId::process_ui_event,
            ida::kProcessUiEvent,
            CallingConvention::thiscall_call,
            HookMode::replace_with_fallback,
            {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF,
             0x68, 0x72, 0x86, 0x81, 0x00, 0x50, 0x8B, 0x44},
            8,
            nullptr,
            nullptr,
        },
        {
            HookId::handle_ui_message,
            ida::kHandleUiMessageAndProcess,
            CallingConvention::thiscall_call,
            HookMode::replace_with_fallback,
            {0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7C, 0x24, 0x10,
             0x8B, 0x8E, 0x98, 0x01, 0x00, 0x00, 0xC6, 0x86},
            8,
            nullptr,
            nullptr,
        },
        {
            HookId::simulate_key_press_and_release,
            ida::kSimulateKeyPressAndRelease,
            CallingConvention::thiscall_call,
            HookMode::observe_only,
            {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8B, 0xF1,
             0x6A, 0x00, 0x57, 0x8B, 0x8E, 0x98, 0x01, 0x00},
            6,
            nullptr,
            nullptr,
        },
        {
            HookId::process_inputs,
            ida::kProcessInputs,
            CallingConvention::cdecl_call,
            HookMode::observe_only,
            {0x56, 0x57, 0xE8, 0xA9, 0xFF, 0xFF, 0xFF, 0x8B,
             0xC8, 0xE8, 0x52, 0xF8, 0xFF, 0xFF, 0x8B, 0x3D},
            7,
            nullptr,
            nullptr,
        },
        {
            HookId::update_input_device_state,
            ida::kUpdateInputDeviceState,
            CallingConvention::thiscall_call,
            HookMode::observe_only,
            {0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x8B, 0xF1,
             0x57, 0x8B, 0x86, 0x70, 0x18, 0x00, 0x00, 0x85},
            5,
            nullptr,
            nullptr,
        },
        {
            HookId::initialize_direct_input,
            ida::kInitializeDirectInput,
            CallingConvention::thiscall_call,
            HookMode::observe_only,
            {0x8B, 0x44, 0x24, 0x04, 0x55, 0x56, 0x8B, 0xF1,
             0x6A, 0x00, 0x56, 0x68, 0x18, 0xD0, 0x86, 0x00},
            5,
            nullptr,
            nullptr,
        },
        {
            HookId::pal4_main_wndproc,
            ida::kPal4MainWndProc,
            CallingConvention::stdcall_call,
            HookMode::observe_only,
            {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF,
             0x68, 0x56, 0x83, 0x81, 0x00, 0x50, 0x64, 0x89},
            8,
            nullptr,
            nullptr,
        },
        {
            HookId::handle_player_input_events,
            ida::kHandlePlayerInputEvents,
            CallingConvention::thiscall_call,
            HookMode::observe_only,
            {0x83, 0xEC, 0x18, 0x53, 0x55, 0x8B, 0x6C, 0x24,
             0x24, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7C, 0x24},
            9,
            nullptr,
            nullptr,
        },
    };
}

}  // namespace pal4::inject

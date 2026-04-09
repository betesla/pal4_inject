#include "pal4inject/types.h"

#include <array>

namespace pal4::inject {
namespace {

template <typename Enum>
struct EnumName {
    Enum value;
    const char* name;
};

constexpr std::array<EnumName<CallingConvention>, 4> kCallingConventions{{
    {CallingConvention::cdecl_call, "cdecl"},
    {CallingConvention::stdcall_call, "stdcall"},
    {CallingConvention::thiscall_call, "thiscall"},
    {CallingConvention::fastcall_call, "fastcall"},
}};

constexpr std::array<EnumName<HookMode>, 4> kHookModes{{
    {HookMode::observe_only, "observe_only"},
    {HookMode::mirror_compare, "mirror_compare"},
    {HookMode::replace_with_fallback, "replace_with_fallback"},
    {HookMode::replace_strict, "replace_strict"},
}};

constexpr std::array<EnumName<MsaaLevel>, 4> kMsaaLevels{{
    {MsaaLevel::off, "off"},
    {MsaaLevel::x2, "2x"},
    {MsaaLevel::x4, "4x"},
    {MsaaLevel::x8, "8x"},
}};

constexpr std::array<EnumName<HookId>, 14> kHookIds{{
    {HookId::process_ui_event, "process_ui_event"},
    {HookId::handle_ui_message, "handle_ui_message"},
    {HookId::simulate_key_press_and_release, "simulate_key_press_and_release"},
    {HookId::process_inputs, "process_inputs"},
    {HookId::update_input_device_state, "update_input_device_state"},
    {HookId::initialize_direct_input, "initialize_direct_input"},
    {HookId::gi_talk, "gi_talk"},
    {HookId::cegui_renderer_constructor_2, "cegui_renderer_constructor_2"},
    {HookId::cegui_system_initialize, "cegui_system_initialize"},
    {HookId::setup_minimap_texture, "setup_minimap_texture"},
    {HookId::camera_update_matrix, "camera_update_matrix"},
    {HookId::d3d9_set_present_parameters, "d3d9_set_present_parameters"},
    {HookId::pal4_main_wndproc, "pal4_main_wndproc"},
    {HookId::handle_player_input_events, "handle_player_input_events"},
}};

template <typename Enum, std::size_t N>
const char* FindEnumName(
    const Enum value,
    const std::array<EnumName<Enum>, N>& table) noexcept {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
bool TryParseEnum(
    const std::string_view text,
    const std::array<EnumName<Enum>, N>& table,
    Enum* out) noexcept {
    if (!out) {
        return false;
    }
    for (const auto& entry : table) {
        if (text == entry.name) {
            *out = entry.value;
            return true;
        }
    }
    return false;
}

}  // namespace

const char* ToString(const CallingConvention cc) noexcept {
    return FindEnumName(cc, kCallingConventions);
}

const char* ToString(const HookMode mode) noexcept {
    return FindEnumName(mode, kHookModes);
}

const char* ToString(const MsaaLevel level) noexcept {
    return FindEnumName(level, kMsaaLevels);
}

const char* ToString(const HookId id) noexcept {
    return FindEnumName(id, kHookIds);
}

bool TryParseHookMode(const std::string_view text, HookMode* out) noexcept {
    return TryParseEnum(text, kHookModes, out);
}

bool TryParseMsaaLevel(const std::string_view text, MsaaLevel* out) noexcept {
    return TryParseEnum(text, kMsaaLevels, out);
}

bool TryParseHookId(const std::string_view text, HookId* out) noexcept {
    return TryParseEnum(text, kHookIds, out);
}

}  // namespace pal4::inject

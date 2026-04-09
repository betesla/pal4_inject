#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pal4::inject {

struct CeguiSizeValue {
    float width = 0.0F;
    float height = 0.0F;
};

struct OpaqueCeguiString {
    std::array<std::byte, 152> storage{};
};

struct CeguiBindings {
    using GetSingletonFn = void* (__cdecl*)();
    using GetSingletonPtrFn = void* (__cdecl*)();
    using InjectMouseButtonFn = bool (__thiscall*)(void*, unsigned int);
    using InjectMousePositionFn = bool (__thiscall*)(void*, float, float);
    using InjectMouseWheelFn = bool (__thiscall*)(void*, float);
    using InjectCharFn = bool (__thiscall*)(void*, unsigned int);
    using InjectKeyFn = bool (__thiscall*)(void*, unsigned int);
    using RequestRedrawFn = void (__thiscall*)(void*);
    using FontManagerGetFontFn = void* (__thiscall*)(void*, const void*);
    using FontNotifyScreenResolutionFn = void (__thiscall*)(void*, const CeguiSizeValue&);
    using FontSetNativeResolutionFn = void (__thiscall*)(void*, const CeguiSizeValue&);
    using FontSetAutoScalingEnabledFn = void (__thiscall*)(void*, bool);
    using FontIsAutoScaledFn = bool (__thiscall*)(void*);
    using FontGetPointSizeFn = unsigned int (__thiscall*)(void*);
    using FontGetFontHeightFn = float (__thiscall*)(void*, float);
    using CeguiStringCtorFromAnsiFn = void (__thiscall*)(void*, const char*);
    using CeguiStringDtorFn = void (__thiscall*)(void*);

    GetSingletonFn get_system_singleton = nullptr;
    GetSingletonPtrFn get_system_singleton_ptr = nullptr;
    GetSingletonPtrFn get_font_manager_singleton_ptr = nullptr;
    InjectMouseButtonFn inject_mouse_button_down = nullptr;
    InjectMouseButtonFn inject_mouse_button_up = nullptr;
    InjectMousePositionFn inject_mouse_position = nullptr;
    InjectMouseWheelFn inject_mouse_wheel_change = nullptr;
    InjectCharFn inject_char = nullptr;
    InjectKeyFn inject_key_down = nullptr;
    InjectKeyFn inject_key_up = nullptr;
    RequestRedrawFn request_redraw = nullptr;
    FontManagerGetFontFn font_manager_get_font = nullptr;
    FontNotifyScreenResolutionFn font_notify_screen_resolution = nullptr;
    FontSetNativeResolutionFn font_set_native_resolution = nullptr;
    FontSetAutoScalingEnabledFn font_set_auto_scaling_enabled = nullptr;
    FontIsAutoScaledFn font_is_auto_scaled = nullptr;
    FontGetPointSizeFn font_get_point_size = nullptr;
    FontGetFontHeightFn font_get_font_height = nullptr;
    CeguiStringCtorFromAnsiFn cegui_string_ctor_from_ansi = nullptr;
    CeguiStringDtorFn cegui_string_dtor = nullptr;
    const void* event_args_vftable = nullptr;
    const void* renderer_event_namespace = nullptr;
    const void* renderer_event_display_size_changed = nullptr;
};

bool TryGetCeguiBindings(CeguiBindings* out, std::string* error);

}  // namespace pal4::inject

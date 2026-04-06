#pragma once

#include <string>

namespace pal4::inject {

struct CeguiBindings {
    using GetSingletonFn = void* (__cdecl*)();
    using GetSingletonPtrFn = void* (__cdecl*)();
    using InjectMouseButtonFn = bool (__thiscall*)(void*, unsigned int);
    using InjectMousePositionFn = bool (__thiscall*)(void*, float, float);
    using InjectMouseWheelFn = bool (__thiscall*)(void*, float);
    using InjectCharFn = bool (__thiscall*)(void*, unsigned int);
    using InjectKeyFn = bool (__thiscall*)(void*, unsigned int);
    using RequestRedrawFn = void (__thiscall*)(void*);

    GetSingletonFn get_system_singleton = nullptr;
    GetSingletonPtrFn get_system_singleton_ptr = nullptr;
    InjectMouseButtonFn inject_mouse_button_down = nullptr;
    InjectMouseButtonFn inject_mouse_button_up = nullptr;
    InjectMousePositionFn inject_mouse_position = nullptr;
    InjectMouseWheelFn inject_mouse_wheel_change = nullptr;
    InjectCharFn inject_char = nullptr;
    InjectKeyFn inject_key_down = nullptr;
    InjectKeyFn inject_key_up = nullptr;
    RequestRedrawFn request_redraw = nullptr;
    const void* event_args_vftable = nullptr;
    const void* renderer_event_namespace = nullptr;
    const void* renderer_event_display_size_changed = nullptr;
};

bool TryGetCeguiBindings(CeguiBindings* out, std::string* error);

}  // namespace pal4::inject

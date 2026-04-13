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

struct CeguiUDim {
    float scale = 0.0F;
    float offset = 0.0F;
};

struct CeguiUVector2 {
    CeguiUDim x{};
    CeguiUDim y{};
};

struct OpaqueCeguiString {
    std::array<std::byte, 152> storage{};
};

struct CeguiBindings {
    using GetSingletonFn = void* (__cdecl*)();
    using GetSingletonPtrFn = void* (__cdecl*)();
    using SystemGetGuiSheetFn = void* (__thiscall*)(void*);
    using WindowManagerIsWindowPresentFn = bool (__thiscall*)(void*, const void*);
    using WindowManagerGetWindowFn = void* (__thiscall*)(void*, const void*);
    using InjectMouseButtonFn = bool (__thiscall*)(void*, unsigned int);
    using InjectMousePositionFn = bool (__thiscall*)(void*, float, float);
    using InjectMouseWheelFn = bool (__thiscall*)(void*, float);
    using InjectCharFn = bool (__thiscall*)(void*, unsigned int);
    using InjectKeyFn = bool (__thiscall*)(void*, unsigned int);
    using WindowGetStringFn = const OpaqueCeguiString* (__thiscall*)(const void*);
    using CeguiStringCStrFn = const char* (__thiscall*)(const void*);
    using WindowGetChildCountFn = unsigned int (__thiscall*)(const void*);
    using WindowGetChildAtIdxFn = void* (__thiscall*)(const void*, unsigned int);
    using WindowGetScalarFn = float (__thiscall*)(const void*);
    using WindowIsFlagFn = bool (__thiscall*)(const void*, bool);
    using WindowIsActiveFn = bool (__thiscall*)(const void*);
    using WindowActivateFn = void (__thiscall*)(void*);
    using WindowTestClassNameFn = bool (__thiscall*)(const void*, const void*);
    using WindowHasInputFocusFn = bool (__thiscall*)(const void*);
    using WindowIsReadOnlyFn = bool (__thiscall*)(const void*);
    using RequestRedrawFn = void (__thiscall*)(void*);
    using WindowGetWindowPositionFn = const CeguiUVector2* (__thiscall*)(void*);
    using WindowSetWindowPositionFn = void (__thiscall*)(void*, const CeguiUVector2&);
    using FontManagerGetFontFn = void* (__thiscall*)(void*, const void*);
    using FontNotifyScreenResolutionFn = void (__thiscall*)(void*, const CeguiSizeValue&);
    using FontSetNativeResolutionFn = void (__thiscall*)(void*, const CeguiSizeValue&);
    using FontSetAutoScalingEnabledFn = void (__thiscall*)(void*, bool);
    using FontGetFontHeightFn = float (__thiscall*)(void*, float);
    using CeguiStringCtorFromAnsiFn = void (__thiscall*)(void*, const char*);
    using CeguiStringDtorFn = void (__thiscall*)(void*);

    GetSingletonFn get_system_singleton = nullptr;
    GetSingletonPtrFn get_system_singleton_ptr = nullptr;
    GetSingletonPtrFn get_font_manager_singleton_ptr = nullptr;
    GetSingletonPtrFn get_window_manager_singleton_ptr = nullptr;
    SystemGetGuiSheetFn get_gui_sheet = nullptr;
    WindowManagerIsWindowPresentFn window_manager_is_window_present = nullptr;
    WindowManagerGetWindowFn window_manager_get_window = nullptr;
    InjectMouseButtonFn inject_mouse_button_down = nullptr;
    InjectMouseButtonFn inject_mouse_button_up = nullptr;
    InjectMousePositionFn inject_mouse_position = nullptr;
    InjectMouseWheelFn inject_mouse_wheel_change = nullptr;
    InjectCharFn inject_char = nullptr;
    InjectKeyFn inject_key_down = nullptr;
    InjectKeyFn inject_key_up = nullptr;
    WindowGetStringFn window_get_name = nullptr;
    WindowGetStringFn window_get_text = nullptr;
    CeguiStringCStrFn cegui_string_c_str = nullptr;
    WindowGetChildCountFn window_get_child_count = nullptr;
    WindowGetChildAtIdxFn window_get_child_at_index = nullptr;
    WindowGetScalarFn window_get_absolute_x = nullptr;
    WindowGetScalarFn window_get_absolute_y = nullptr;
    WindowGetScalarFn window_get_absolute_width = nullptr;
    WindowGetScalarFn window_get_absolute_height = nullptr;
    WindowIsFlagFn window_is_visible = nullptr;
    WindowIsFlagFn window_is_disabled = nullptr;
    WindowIsActiveFn window_is_active = nullptr;
    WindowActivateFn window_activate = nullptr;
    WindowTestClassNameFn window_test_class_name = nullptr;
    WindowHasInputFocusFn editbox_has_input_focus = nullptr;
    WindowHasInputFocusFn multiline_editbox_has_input_focus = nullptr;
    WindowIsReadOnlyFn editbox_is_read_only = nullptr;
    WindowIsReadOnlyFn multiline_editbox_is_read_only = nullptr;
    RequestRedrawFn request_redraw = nullptr;
    WindowGetWindowPositionFn window_get_window_position = nullptr;
    WindowSetWindowPositionFn window_set_window_position = nullptr;
    FontManagerGetFontFn font_manager_get_font = nullptr;
    FontNotifyScreenResolutionFn font_notify_screen_resolution = nullptr;
    FontSetNativeResolutionFn font_set_native_resolution = nullptr;
    FontSetAutoScalingEnabledFn font_set_auto_scaling_enabled = nullptr;
    FontGetFontHeightFn font_get_font_height = nullptr;
    CeguiStringCtorFromAnsiFn cegui_string_ctor_from_ansi = nullptr;
    CeguiStringDtorFn cegui_string_dtor = nullptr;
    const void* event_args_vftable = nullptr;
    const void* renderer_event_namespace = nullptr;
    const void* renderer_event_display_size_changed = nullptr;
};

bool TryGetCeguiBindings(CeguiBindings* out, std::string* error);

}  // namespace pal4::inject

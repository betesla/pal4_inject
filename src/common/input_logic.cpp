#include "pal4inject/input_logic.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {

std::uint32_t NormalizeProcessUiEventKeyDown(const std::uint32_t mapped_key) noexcept {
    switch (mapped_key) {
    case 17:
        return 200;
    case 30:
        return 203;
    case 31:
        return 208;
    case 32:
        return 205;
    case 57:
        return 28;
    default:
        return mapped_key;
    }
}

std::uint32_t NormalizeProcessUiEventKeyUp(const std::uint32_t mapped_key) noexcept {
    return NormalizeProcessUiEventKeyDown(mapped_key);
}

bool ShouldSuppressMappedUiKey(const std::uint32_t mapped_key) noexcept {
    return mapped_key == 1;
}

UiInjectedPlan BuildUiInjectedPlan(
    const std::uint32_t message,
    const std::uint32_t mapped_key,
    const std::uint32_t wparam) noexcept {
    switch (message) {
    case WM_KEYDOWN:
        if (ShouldSuppressMappedUiKey(mapped_key)) {
            return {UiInjectedAction::return_false, 0, 0.0F};
        }
        return {UiInjectedAction::key_down, NormalizeProcessUiEventKeyDown(mapped_key), 0.0F};
    case WM_KEYUP:
        if (ShouldSuppressMappedUiKey(mapped_key)) {
            return {UiInjectedAction::return_false, 0, 0.0F};
        }
        return {UiInjectedAction::key_up, NormalizeProcessUiEventKeyUp(mapped_key), 0.0F};
    case WM_CHAR:
        return {UiInjectedAction::char_input, wparam, 0.0F};
    case WM_MOUSEMOVE:
        return {UiInjectedAction::mouse_move, 0, 0.0F};
    case WM_LBUTTONDOWN:
        return {UiInjectedAction::mouse_button_down, 0, 0.0F};
    case WM_LBUTTONUP:
        return {UiInjectedAction::mouse_button_up, 0, 0.0F};
    case WM_RBUTTONDOWN:
        return {UiInjectedAction::mouse_button_down, 1, 0.0F};
    case WM_RBUTTONUP:
        return {UiInjectedAction::mouse_button_up, 1, 0.0F};
    case WM_MBUTTONDOWN:
        return {UiInjectedAction::mouse_button_down, 2, 0.0F};
    case WM_MBUTTONUP:
        return {UiInjectedAction::mouse_button_up, 2, 0.0F};
    case WM_MOUSEWHEEL:
        return {
            UiInjectedAction::mouse_wheel,
            0,
            static_cast<float>(static_cast<short>(HIWORD(wparam))) / 120.0F,
        };
    case WM_SIZE:
        return {UiInjectedAction::renderer_size_changed, 0, 0.0F};
    case WM_ACTIVATE:
        return wparam != 0
            ? UiInjectedPlan{UiInjectedAction::renderer_size_changed_and_redraw, 0, 0.0F}
            : UiInjectedPlan{UiInjectedAction::fallback_to_original, 0, 0.0F};
    case WM_NCMOUSEMOVE:
    case WM_MOUSELEAVE:
        return {UiInjectedAction::disable_mouse_capture, 0, 0.0F};
    default:
        return {UiInjectedAction::fallback_to_original, 0, 0.0F};
    }
}

const char* ToString(const UiInjectedAction action) noexcept {
    switch (action) {
    case UiInjectedAction::fallback_to_original:
        return "fallback_to_original";
    case UiInjectedAction::return_false:
        return "return_false";
    case UiInjectedAction::key_down:
        return "key_down";
    case UiInjectedAction::key_up:
        return "key_up";
    case UiInjectedAction::char_input:
        return "char_input";
    case UiInjectedAction::mouse_move:
        return "mouse_move";
    case UiInjectedAction::mouse_button_down:
        return "mouse_button_down";
    case UiInjectedAction::mouse_button_up:
        return "mouse_button_up";
    case UiInjectedAction::mouse_wheel:
        return "mouse_wheel";
    case UiInjectedAction::renderer_size_changed:
        return "renderer_size_changed";
    case UiInjectedAction::renderer_size_changed_and_redraw:
        return "renderer_size_changed_and_redraw";
    case UiInjectedAction::disable_mouse_capture:
        return "disable_mouse_capture";
    }
    return "unknown";
}

const char* DescribeWindowsMessage(const std::uint32_t message) noexcept {
    switch (message) {
    case WM_KEYDOWN:
        return "WM_KEYDOWN";
    case WM_KEYUP:
        return "WM_KEYUP";
    case WM_CHAR:
        return "WM_CHAR";
    case WM_MOUSEMOVE:
        return "WM_MOUSEMOVE";
    case WM_LBUTTONDOWN:
        return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP:
        return "WM_LBUTTONUP";
    case WM_RBUTTONDOWN:
        return "WM_RBUTTONDOWN";
    case WM_RBUTTONUP:
        return "WM_RBUTTONUP";
    case WM_MBUTTONDOWN:
        return "WM_MBUTTONDOWN";
    case WM_MBUTTONUP:
        return "WM_MBUTTONUP";
    case WM_MOUSEWHEEL:
        return "WM_MOUSEWHEEL";
    case WM_SIZE:
        return "WM_SIZE";
    case WM_ACTIVATE:
        return "WM_ACTIVATE";
    case WM_NCMOUSEMOVE:
        return "WM_NCMOUSEMOVE";
    case WM_MOUSELEAVE:
        return "WM_MOUSELEAVE";
    default:
        return "WM_OTHER";
    }
}

}  // namespace pal4::inject

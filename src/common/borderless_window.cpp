#include "pal4inject/borderless_window.h"

#include <algorithm>

namespace pal4::inject {
namespace {

constexpr std::uint32_t kWindowStylePopup = 0x80000000U;
constexpr std::uint32_t kWindowStyleVisible = 0x10000000U;
constexpr std::uint32_t kWindowStyleDisabled = 0x08000000U;
constexpr std::uint32_t kWindowStyleClipSiblings = 0x04000000U;
constexpr std::uint32_t kWindowStyleClipChildren = 0x02000000U;
constexpr std::uint32_t kPreservedWindowStyles =
    kWindowStyleVisible |
    kWindowStyleDisabled |
    kWindowStyleClipSiblings |
    kWindowStyleClipChildren;

constexpr std::uint32_t kExtendedStyleDialogModalFrame = 0x00000001U;
constexpr std::uint32_t kExtendedStyleTopmost = 0x00000008U;
constexpr std::uint32_t kExtendedStyleWindowEdge = 0x00000100U;
constexpr std::uint32_t kExtendedStyleClientEdge = 0x00000200U;
constexpr std::uint32_t kExtendedStyleStaticEdge = 0x00020000U;
constexpr std::uint32_t kExtendedStyleAppWindow = 0x00040000U;
constexpr std::uint32_t kRemovedExtendedStyles =
    kExtendedStyleDialogModalFrame |
    kExtendedStyleTopmost |
    kExtendedStyleWindowEdge |
    kExtendedStyleClientEdge |
    kExtendedStyleStaticEdge;

}  // namespace

BorderlessWindowPlan BuildBorderlessWindowPlan(
    const std::uint32_t current_style,
    const std::uint32_t current_extended_style,
    const int monitor_left,
    const int monitor_top,
    const int monitor_right,
    const int monitor_bottom) noexcept {
    BorderlessWindowPlan plan{};
    plan.style = kWindowStylePopup | (current_style & kPreservedWindowStyles);
    plan.extended_style =
        (current_extended_style & ~kRemovedExtendedStyles) |
        kExtendedStyleAppWindow;
    plan.x = monitor_left;
    plan.y = monitor_top;
    plan.width = std::max(0, monitor_right - monitor_left);
    plan.height = std::max(0, monitor_bottom - monitor_top);
    return plan;
}

int ResolveBorderlessPresentFullscreenFlag(
    const int requested_fullscreen,
    const bool borderless_requested,
    const bool replacement_enabled) noexcept {
    return borderless_requested && replacement_enabled ? 0 : requested_fullscreen;
}

}  // namespace pal4::inject

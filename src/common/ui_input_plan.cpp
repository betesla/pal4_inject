#include "pal4inject/ui_input_plan.h"

#include <cmath>
#include <limits>

namespace pal4::inject {
namespace {

bool FindAbsoluteUiRect(
    const UiSnapshotNode& node,
    const std::string_view ref,
    const std::int32_t parent_x,
    const std::int32_t parent_y,
    const UiSnapshotNode** found_node,
    UiRect* found_rect) {
    const UiRect absolute{
        parent_x + node.rect.left,
        parent_y + node.rect.top,
        parent_x + node.rect.right,
        parent_y + node.rect.bottom,
    };
    if (node.ref == ref) {
        if (found_node) {
            *found_node = &node;
        }
        if (found_rect) {
            *found_rect = absolute;
        }
        return true;
    }
    for (const auto& child : node.children) {
        if (FindAbsoluteUiRect(
                child,
                ref,
                absolute.left,
                absolute.top,
                found_node,
                found_rect)) {
            return true;
        }
    }
    return false;
}

}  // namespace

const char* ToString(const UiInputDispatchMode mode) noexcept {
    switch (mode) {
    case UiInputDispatchMode::os_queue:
        return "os_queue";
    case UiInputDispatchMode::direct_seam:
        return "direct_seam";
    }
    return "unknown";
}

bool TryParseUiInputDispatchOption(
    const std::string_view option,
    UiInputDispatchMode* out_mode) noexcept {
    if (!out_mode) {
        return false;
    }
    if (option == "--os-queue") {
        *out_mode = UiInputDispatchMode::os_queue;
        return true;
    }
    if (option == "--direct-seam") {
        *out_mode = UiInputDispatchMode::direct_seam;
        return true;
    }
    return false;
}

bool BuildUiRefClickPlan(
    const UiSnapshotTree& tree,
    const std::string_view ref,
    const UiViewportPlan& viewport,
    UiRefClickPlan* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "click plan output pointer is null";
        }
        return false;
    }

    const UiSnapshotNode* node = nullptr;
    UiRect absolute_rect{};
    if (!FindAbsoluteUiRect(tree.root, ref, 0, 0, &node, &absolute_rect)) {
        if (error) {
            *error = "UI snapshot ref was not found; acquire a fresh snapshot before clicking";
        }
        return false;
    }
    if (!node->visible || !node->enabled || !node->clickable) {
        if (error) {
            *error = "UI snapshot ref is not visible, enabled, and clickable";
        }
        return false;
    }
    if (absolute_rect.right <= absolute_rect.left ||
        absolute_rect.bottom <= absolute_rect.top) {
        if (error) {
            *error = "UI snapshot ref has an invalid rectangle";
        }
        return false;
    }

    const auto logical_x =
        absolute_rect.left + (absolute_rect.right - absolute_rect.left) / 2;
    const auto logical_y =
        absolute_rect.top + (absolute_rect.bottom - absolute_rect.top) / 2;
    float client_x = 0.0F;
    float client_y = 0.0F;
    if (!UiLogicalToPhysical(
            viewport,
            static_cast<float>(logical_x),
            static_cast<float>(logical_y),
            &client_x,
            &client_y) ||
        !std::isfinite(client_x) || !std::isfinite(client_y) ||
        client_x < 0.0F || client_y < 0.0F ||
        client_x > static_cast<float>(std::numeric_limits<std::uint16_t>::max()) ||
        client_y > static_cast<float>(std::numeric_limits<std::uint16_t>::max())) {
        if (error) {
            *error = "failed to project UI snapshot ref into client coordinates";
        }
        return false;
    }

    out->logical_x = logical_x;
    out->logical_y = logical_y;
    out->client_x = static_cast<std::uint32_t>(std::lround(client_x));
    out->client_y = static_cast<std::uint32_t>(std::lround(client_y));
    return true;
}

}  // namespace pal4::inject

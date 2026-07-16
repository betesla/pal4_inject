#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "pal4inject/ui_coordinate_space.h"
#include "pal4inject/ui_snapshot.h"

namespace pal4::inject {

enum class UiInputDispatchMode : std::uint8_t {
    os_queue = 0,
    direct_seam,
};

struct UiRefClickPlan {
    std::int32_t logical_x = 0;
    std::int32_t logical_y = 0;
    std::uint32_t client_x = 0;
    std::uint32_t client_y = 0;
};

const char* ToString(UiInputDispatchMode mode) noexcept;

// --os-queue remains accepted for compatibility. The safe OS queue path is
// the default; --direct-seam is an explicit diagnostic opt-in.
bool TryParseUiInputDispatchOption(
    std::string_view option,
    UiInputDispatchMode* out_mode) noexcept;

bool BuildUiRefClickPlan(
    const UiSnapshotTree& tree,
    std::string_view ref,
    const UiViewportPlan& viewport,
    UiRefClickPlan* out,
    std::string* error);

}  // namespace pal4::inject

#pragma once

#include <cstdint>
#include <string>

namespace pal4::inject {

enum class DpiAwarenessMode : std::uint8_t {
    unknown = 0,
    per_monitor_aware_v2,
    per_monitor_aware,
    system_aware,
    already_set,
};

const char* ToString(DpiAwarenessMode mode) noexcept;
bool ApplyProcessDpiAwareness(DpiAwarenessMode* out_mode, std::string* error);

}  // namespace pal4::inject

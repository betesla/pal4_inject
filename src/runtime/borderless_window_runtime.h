#pragma once

#include <string>
#include <string_view>

namespace pal4::inject {

bool ApplyBorderlessWindowToCurrentProcess(
    std::string_view requested_monitor,
    std::string* summary);

}  // namespace pal4::inject

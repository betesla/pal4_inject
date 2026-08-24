#pragma once

#include <string>

namespace pal4::inject {

bool IsBackgroundWindowModeRequested() noexcept;
bool InstallBackgroundWindowHooks(std::string* error);

}  // namespace pal4::inject

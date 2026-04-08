#pragma once

#include <string>

namespace pal4::inject {

bool StartInjectControlWindow(std::string* error);
void StopInjectControlWindow();

}  // namespace pal4::inject

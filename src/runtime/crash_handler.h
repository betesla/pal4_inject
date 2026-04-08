#pragma once

#include <string>

namespace pal4::inject {

bool InstallCrashCapture(std::string* error);
void UninstallCrashCapture();

}  // namespace pal4::inject

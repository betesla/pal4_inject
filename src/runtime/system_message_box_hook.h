#pragma once

#include <string>

namespace pal4::inject {

// Installs a process-local detour at the final 32-bit user32!MessageBoxA
// export. This covers callers that cached the export or resolved it without
// using a module IAT. The returned trampoline preserves normal behavior when
// background validation is not active.
bool InstallSystemMessageBoxHook(
    void* replacement,
    void** original_trampoline,
    std::string* error);

}  // namespace pal4::inject

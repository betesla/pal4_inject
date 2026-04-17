#include "hook_logging.h"

#include <filesystem>
#include <fstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/runtime_paths.h"
#include "runtime_state.h"

namespace pal4::inject {

bool ShouldEmitHookLog(const HookId id) {
    return GetRuntimeState().GetHookLogEnabled(id);
}

void AppendHookEventLog(const HookId id, const std::string_view text) {
    if (!ShouldEmitHookLog(id)) {
        return;
    }

    GetRuntimeState().AppendEventLog(text);

    const auto log_path = RuntimeLogPath();
    std::error_code ec;
    std::filesystem::create_directories(log_path.parent_path(), ec);
    if (ec) {
        return;
    }

    std::ofstream output(log_path, std::ios::app | std::ios::binary);
    if (!output) {
        return;
    }
    output << text << "\r\n";
}

}  // namespace pal4::inject

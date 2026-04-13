#include "hook_logging.h"

#include <fstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

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

    char temp_path[MAX_PATH]{};
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
        return;
    }

    std::string log_path(temp_path, temp_len);
    if (!log_path.empty() && log_path.back() != '\\') {
        log_path.push_back('\\');
    }
    log_path += "pal4_inject_runtime.log";

    std::ofstream output(log_path, std::ios::app | std::ios::binary);
    if (!output) {
        return;
    }
    output << text << "\r\n";
}

}  // namespace pal4::inject

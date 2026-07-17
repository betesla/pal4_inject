#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "pal4inject/types.h"

namespace pal4::inject {

struct LooseFileLoadLogEntry {
    std::string_view event;
    std::string_view loader;
    HookMode mode = HookMode::observe_only;
    std::string_view resource_path;
    std::filesystem::path file_path;
    std::string_view reason;
    std::string_view fallback_source;
    std::uint64_t size = 0;
    bool has_size = false;
    bool fallback_result_known = false;
    bool fallback_opened = false;
    bool fallback_used = false;
};

std::string FormatLooseFileLoadLogEntry(
    const LooseFileLoadLogEntry& entry) noexcept;
void AppendLooseFileLoadLog(
    const std::filesystem::path& game_root,
    const LooseFileLoadLogEntry& entry) noexcept;

}  // namespace pal4::inject

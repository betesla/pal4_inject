#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "pal4inject/types.h"

namespace pal4::inject {

struct LooseFileCandidate {
    std::filesystem::path path;
};

bool IsLooseFileOverlayActiveMode(HookMode mode) noexcept;
std::optional<std::filesystem::path> NormalizeLooseResourcePath(
    std::string_view resource_path);
std::filesystem::path GamePatchRoot(const std::filesystem::path& game_root);
std::filesystem::path LooseFileLoadLogPath(const std::filesystem::path& game_root);
std::vector<LooseFileCandidate> BuildLooseFileCandidates(
    const std::filesystem::path& game_root,
    std::string_view resource_path);
std::optional<LooseFileCandidate> FindExistingLooseFile(
    const std::filesystem::path& game_root,
    std::string_view resource_path);

}  // namespace pal4::inject

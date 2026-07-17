#include "pal4inject/loose_file_overlay.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace pal4::inject {
namespace {

bool EqualsAsciiCaseInsensitive(
    const std::string_view lhs,
    const std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto left = static_cast<unsigned char>(lhs[i]);
        const auto right = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool IsLooseFileOverlayActiveMode(const HookMode mode) noexcept {
    return mode == HookMode::replace_with_fallback ||
        mode == HookMode::replace_strict;
}

std::optional<std::filesystem::path> NormalizeLooseResourcePath(
    const std::string_view resource_path) {
    if (resource_path.empty()) {
        return std::nullopt;
    }

    std::string normalized(resource_path);
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    while (normalized.starts_with(".\\")) {
        normalized.erase(0, 2);
    }
    if (normalized.empty() || normalized.front() == '\\' || normalized.find(':') != std::string::npos) {
        return std::nullopt;
    }

    std::vector<std::string> segments;
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        const auto end = normalized.find('\\', begin);
        const auto count = end == std::string::npos ? normalized.size() - begin : end - begin;
        const std::string segment = normalized.substr(begin, count);
        if (!segment.empty() && segment != ".") {
            if (segment == "..") {
                return std::nullopt;
            }
            segments.push_back(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }

    if (segments.size() < 2 || !EqualsAsciiCaseInsensitive(segments.front(), "gamedata")) {
        return std::nullopt;
    }

    std::filesystem::path result;
    for (const auto& segment : segments) {
        result /= segment;
    }
    return result;
}

std::filesystem::path GamePatchRoot(const std::filesystem::path& game_root) {
    return game_root / "gamepatch";
}

std::filesystem::path LooseFileLoadLogPath(const std::filesystem::path& game_root) {
    return GamePatchRoot(game_root) / "loose_file_load.log";
}

std::vector<LooseFileCandidate> BuildLooseFileCandidates(
    const std::filesystem::path& game_root,
    const std::string_view resource_path) {
    const auto normalized = NormalizeLooseResourcePath(resource_path);
    if (!normalized) {
        return {};
    }
    return {{GamePatchRoot(game_root) / *normalized}};
}

std::optional<LooseFileCandidate> FindExistingLooseFile(
    const std::filesystem::path& game_root,
    const std::string_view resource_path) {
    for (const auto& candidate : BuildLooseFileCandidates(game_root, resource_path)) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate.path, error) && !error) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace pal4::inject

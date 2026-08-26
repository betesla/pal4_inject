#include "pal4inject/loose_file_overlay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
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

bool HasExtensionAsciiCaseInsensitive(
    const std::filesystem::path& path,
    const std::string_view extension) {
    return EqualsAsciiCaseInsensitive(path.extension().string(), extension);
}

std::optional<LooseFileCandidate> FindExistingCandidate(
    const std::vector<LooseFileCandidate>& candidates) {
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate.path, error) && !error) {
            return candidate;
        }
    }
    return std::nullopt;
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

std::vector<LooseFileCandidate> BuildLooseTextScriptCandidates(
    const std::filesystem::path& game_root,
    const std::string_view resource_path) {
    auto normalized = NormalizeLooseResourcePath(resource_path);
    if (!normalized) {
        return {};
    }

    // The original game accidentally requests Music.csb and worldMap.csb from
    // the text interpreter in CS mode. Loose text sources still use .cs; .csb
    // is reserved for compiled package payloads.
    if (HasExtensionAsciiCaseInsensitive(*normalized, ".csb")) {
        normalized->replace_extension(".cs");
    }
    return {{GamePatchRoot(game_root) / *normalized}};
}

std::optional<LooseFileCandidate> FindExistingLooseFile(
    const std::filesystem::path& game_root,
    const std::string_view resource_path) {
    return FindExistingCandidate(BuildLooseFileCandidates(game_root, resource_path));
}

std::optional<LooseFileCandidate> FindExistingLooseTextScriptFile(
    const std::filesystem::path& game_root,
    const std::string_view resource_path) {
    return FindExistingCandidate(
        BuildLooseTextScriptCandidates(game_root, resource_path));
}

bool ValidateLoosePackageFile(
    const std::string_view resource_path,
    const std::filesystem::path& file_path,
    std::string* rejection_reason) {
    const auto normalized = NormalizeLooseResourcePath(resource_path);
    if (!normalized || !HasExtensionAsciiCaseInsensitive(*normalized, ".csb")) {
        return true;
    }

    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(file_path, size_error);
    if (size_error || file_size < sizeof(std::uint32_t)) {
        if (rejection_reason) {
            *rejection_reason = "invalid_csb_header: file is smaller than 4 bytes";
        }
        return false;
    }

    std::ifstream input(file_path, std::ios::binary);
    std::array<unsigned char, sizeof(std::uint32_t)> header{};
    if (!input.read(
            reinterpret_cast<char*>(header.data()),
            static_cast<std::streamsize>(header.size()))) {
        if (rejection_reason) {
            *rejection_reason = "invalid_csb_header: cannot read payload length";
        }
        return false;
    }

    const std::uint32_t payload_size =
        static_cast<std::uint32_t>(header[0]) |
        (static_cast<std::uint32_t>(header[1]) << 8U) |
        (static_cast<std::uint32_t>(header[2]) << 16U) |
        (static_cast<std::uint32_t>(header[3]) << 24U);
    const auto expected_payload_size = file_size - sizeof(std::uint32_t);
    if (expected_payload_size > UINT32_MAX || payload_size != expected_payload_size) {
        if (rejection_reason) {
            std::ostringstream reason;
            reason << "invalid_csb_header: declared_payload=" << payload_size
                   << " actual_payload=" << expected_payload_size;
            *rejection_reason = reason.str();
        }
        return false;
    }
    return true;
}

}  // namespace pal4::inject

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pal4/cpk_archive.h"

namespace pal4 {

struct CpkMountResult {
    bool ok = false;
    bool ui_opened = false;
    bool database_opened = false;
    std::size_t ui_entry_count = 0;
    std::size_t database_entry_count = 0;
};

struct CpkExtractedFileResult {
    std::string requested_path;
    std::string resolved_path;
    std::filesystem::path output_path;
    bool extracted = false;
};

class CpkRepository {
public:
    CpkMountResult Mount(const std::string& game_root);
    void Clear() noexcept;

    bool IsReady() const noexcept;
    bool HasArchive(std::string_view tag) const;
    const CpkArchive* GetArchive(std::string_view tag) const;
    std::vector<CpkEntryView> EnumerateByPrefix(std::string_view logical_prefix) const;
    bool FileExists(std::string_view logical_path) const;
    bool ReadFile(std::string_view logical_path, std::vector<std::byte>& output, std::string* resolved_path = nullptr) const;
    bool ExtractFileToDisk(
        std::string_view logical_path,
        const std::filesystem::path& output_path,
        std::string* resolved_path = nullptr) const;
    bool ExtractFileToLogicalDiskPath(
        std::string_view logical_path,
        const std::filesystem::path& game_root,
        std::filesystem::path* output_path = nullptr,
        std::string* resolved_path = nullptr) const;
    bool ExtractFilesToLogicalDiskPaths(
        const std::vector<std::string_view>& logical_paths,
        const std::filesystem::path& game_root,
        std::vector<CpkExtractedFileResult>* results = nullptr) const;
    bool ExtractFileAndDirectDependenciesToLogicalDiskPath(
        std::string_view logical_path,
        const std::filesystem::path& game_root,
        std::vector<CpkExtractedFileResult>* results = nullptr) const;
    bool ExtractFileAndDependencyClosureToLogicalDiskPath(
        std::string_view logical_path,
        const std::filesystem::path& game_root,
        std::vector<CpkExtractedFileResult>* results = nullptr) const;

private:
    static std::string NormalizePath(std::string_view logical_path);
    static std::string ToArchiveRelativePath(std::string_view normalized_path);
    static std::string ToLogicalResolvedPath(std::string_view archive_tag, std::string_view archive_path);
    const CpkArchive* RouteArchive(std::string_view normalized_path) const;
    const char* RouteArchiveTag(std::string_view normalized_path) const;

    std::unordered_map<std::string, CpkArchive> archives_;
};

}  // namespace pal4

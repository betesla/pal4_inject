#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pal4::inject::update {

struct Version {
    std::uint32_t major = 0, minor = 0, patch = 0;
    auto operator<=>(const Version&) const = default;
};

struct File {
    std::string path;
    std::uint64_t size = 0;
    std::string sha256;
};

struct Manifest {
    std::string version;
    std::string notes;
    std::string package_name;
    std::uint64_t package_size = 0;
    std::string package_sha256;
    std::vector<File> files;
    std::vector<std::string> remove;
};

struct Release {
    std::string version;
    std::string notes;
    std::string manifest_url;
    std::vector<std::pair<std::string, std::string>> assets;
};

bool ParseVersion(std::string_view text, Version* out) noexcept;
bool IsNewerVersion(std::string_view candidate, std::string_view installed) noexcept;
bool IsManagedPath(std::string_view path) noexcept;
bool IsHttpsUrl(std::string_view url) noexcept;
Manifest ParseManifest(std::string_view json);
Release ParseRelease(std::string_view json);
std::string FormatManifest(const Manifest& manifest);
std::string Sha256(const std::filesystem::path& path);
void VerifyFile(const std::filesystem::path& path, std::uint64_t size, std::string_view hash);

}  // namespace pal4::inject::update

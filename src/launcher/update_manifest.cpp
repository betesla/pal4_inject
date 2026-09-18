#include "update_manifest.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>
#include <windows.h>
#include <bcrypt.h>
#include "nlohmann/json.hpp"

namespace pal4::inject::update {
namespace {
using Json = nlohmann::json;
constexpr std::uint64_t kMaxPackageBytes = 256ULL * 1024 * 1024;
constexpr std::uint64_t kMaxInstalledBytes = 512ULL * 1024 * 1024;

void Require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool IsHash(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::uint64_t Size(const Json& object, const char* key) {
    Require(object.at(key).is_number_unsigned(), "更新清单的文件大小无效。");
    return object.at(key).get<std::uint64_t>();
}
}  // namespace

bool ParseVersion(std::string_view text, Version* const out) noexcept {
    if (!out) return false;
    if (text.starts_with('v')) text.remove_prefix(1);
    Version parsed{};
    std::array<std::uint32_t*, 3> parts{&parsed.major, &parsed.minor, &parsed.patch};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto dot = text.find('.');
        const auto part = text.substr(0, dot);
        if (part.empty() || (part.size() > 1 && part.front() == '0')) return false;
        const auto result = std::from_chars(part.data(), part.data() + part.size(), *parts[i]);
        if (result.ec != std::errc{} || result.ptr != part.data() + part.size()) return false;
        if (i == 2) {
            if (dot != std::string_view::npos) return false;
        } else {
            if (dot == std::string_view::npos) return false;
            text.remove_prefix(dot + 1);
        }
    }
    *out = parsed;
    return true;
}

bool IsNewerVersion(const std::string_view candidate, const std::string_view installed) noexcept {
    Version next{}, current{};
    return ParseVersion(candidate, &next) && ParseVersion(installed, &current) && next > current;
}

bool IsManagedPath(const std::string_view path) noexcept {
    // Explicit ownership list: never accept game executables, saves, user INIs,
    // arbitrary DLLs or paths supplied by a remote manifest.
    return path == "PAL4Plus.exe" ||
        path == "pal4_inject/runtime.dll" || path == "pal4_inject/cli.exe" ||
        path == "pal4_inject/THIRD_PARTY_NOTICES.txt" ||
        path == "pal4_inject/rtx_remix_compatibility.conf" || path == "PAL4_inject.exe";
}

bool IsHttpsUrl(const std::string_view url) noexcept {
    if (!url.starts_with("https://")) return false;
    const auto host_end = url.find_first_of("/?#", 8);
    const auto host = url.substr(8, host_end == std::string_view::npos ? url.size() - 8 : host_end - 8);
    return !host.empty() && host.find('@') == std::string_view::npos &&
        url.find_first_of("\r\n\t ") == std::string_view::npos;
}

Manifest ParseManifest(const std::string_view text) {
    const auto json = Json::parse(text);
    Require(json.at("schema_version") == 1, "不支持此更新清单格式。");
    Require(json.at("channel") == "stable" && json.at("platform") == "win32", "更新渠道或平台不匹配。");
    Manifest result{};
    result.version = json.at("version").get<std::string>();
    Version version{};
    Require(ParseVersion(result.version, &version), "更新版本号无效。");
    result.notes = json.value("notes", std::string{});
    const auto& package = json.at("package");
    result.package_name = package.at("name").get<std::string>();
    Require(result.package_name == "PAL4Plus_v" + std::to_string(version.major) + "." +
        std::to_string(version.minor) + "." + std::to_string(version.patch) + "_update_win32.zip",
        "更新包名称与版本不匹配。");
    result.package_size = Size(package, "size");
    result.package_sha256 = package.at("sha256").get<std::string>();
    Require(result.package_size > 0 && result.package_size <= kMaxPackageBytes && IsHash(result.package_sha256),
        "更新包大小或校验值无效。");
    std::set<std::string> seen;
    std::uint64_t total = 0;
    for (const auto& entry : json.at("files")) {
        File file{entry.at("path").get<std::string>(), Size(entry, "size"), entry.at("sha256").get<std::string>()};
        Require(IsManagedPath(file.path) && file.path != "PAL4_inject.exe", "更新清单包含非托管文件。");
        Require(seen.insert(file.path).second, "更新清单包含重复文件。");
        Require(file.size > 0 && file.size <= kMaxInstalledBytes && IsHash(file.sha256), "文件校验信息无效。");
        total += file.size;
        Require(total <= kMaxInstalledBytes, "更新包解压大小超限。");
        result.files.push_back(std::move(file));
    }
    Require(seen.contains("PAL4Plus.exe") && seen.contains("pal4_inject/runtime.dll") &&
        seen.contains("pal4_inject/cli.exe"), "更新包必须包含配套的启动器、运行库和 CLI。");
    for (const auto& entry : json.value("remove", Json::array())) {
        const auto path = entry.get<std::string>();
        Require(IsManagedPath(path) && seen.insert(path).second, "更新清单包含无效删除项。");
        result.remove.push_back(path);
    }
    return result;
}

Release ParseRelease(const std::string_view text) {
    const auto json = Json::parse(text);
    Require(!json.value("draft", false) && !json.value("prerelease", false), "跳过非正式发布版本。");
    Release result{};
    result.version = json.at("tag_name").get<std::string>();
    Version version{};
    Require(ParseVersion(result.version, &version), "发布版本号无效。");
    result.notes = json.value("body", std::string{});
    const auto& assets = json.at("assets");
    const auto& links = assets.is_array() ? assets : assets.at("links");
    for (const auto& asset : links) {
        auto name = asset.at("name").get<std::string>();
        auto url = asset.value("browser_download_url", asset.value("url", std::string{}));
        if (!IsHttpsUrl(url)) continue;
        Require(std::none_of(result.assets.begin(), result.assets.end(), [&](const auto& item) {
            return item.first == name;
        }), "发布附件名称重复。");
        if (name == "update.json") result.manifest_url = url;
        result.assets.emplace_back(std::move(name), std::move(url));
    }
    return result;
}

std::string FormatManifest(const Manifest& manifest) {
    Json files = Json::array();
    for (const auto& file : manifest.files) files.push_back({{"path", file.path}, {"size", file.size}, {"sha256", file.sha256}});
    return Json{{"schema_version", 1}, {"version", manifest.version}, {"channel", "stable"}, {"platform", "win32"},
        {"notes", manifest.notes}, {"package", {{"name", manifest.package_name}, {"size", manifest.package_size},
        {"sha256", manifest.package_sha256}}}, {"files", files}, {"remove", manifest.remove}}.dump(2);
}

std::string Sha256(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "无法初始化 SHA256。");
    struct AlgorithmGuard { BCRYPT_ALG_HANDLE value; ~AlgorithmGuard() { BCryptCloseAlgorithmProvider(value, 0); } } ag{algorithm};
    BCRYPT_HASH_HANDLE hash = nullptr;
    Require(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0, "无法创建 SHA256。");
    struct HashGuard { BCRYPT_HASH_HANDLE value; ~HashGuard() { BCryptDestroyHash(value); } } hg{hash};
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "无法读取待校验文件。");
    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        Require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) >= 0,
            "SHA256 计算失败。");
    }
    Require(input.eof(), "读取待校验文件失败。");
    std::array<unsigned char, 32> digest{};
    Require(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0, "SHA256 计算失败。");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

void VerifyFile(const std::filesystem::path& path, const std::uint64_t size, const std::string_view hash) {
    Require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) == size && Sha256(path) == hash,
        "下载文件或解压文件校验失败，请重新下载。");
}
}  // namespace pal4::inject::update

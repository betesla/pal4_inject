#include "update_client.h"
#include "update_installer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <windows.h>
#include <winhttp.h>

namespace pal4::inject::update {
namespace {
constexpr std::array kSources{
    "https://gitee.com/api/v5/repos/betesla/pal4_inject/releases/latest",
    "https://api.github.com/repos/betesla/pal4_inject/releases/latest",
};

class Http final : public Transport {
public:
    void Cancel() {
        std::scoped_lock lock(mutex_);
        cancelled_ = true;
        if (request_) { WinHttpCloseHandle(request_); request_ = nullptr; }
    }
    void Reset() { cancelled_ = false; }
    bool Cancelled() const { return cancelled_; }

    std::string Get(const std::string& url) {
        std::string text;
        Read(url, 2 * 1024 * 1024, [&](const char* bytes, std::size_t count, std::uint64_t) { text.append(bytes, count); });
        return text;
    }

    void Download(const std::string& url, const std::filesystem::path& path, std::uint64_t size,
        const std::function<void(float)>& progress) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("无法创建下载文件，请检查游戏目录写入权限。");
        Read(url, size, [&](const char* bytes, std::size_t count, std::uint64_t total) {
            output.write(bytes, static_cast<std::streamsize>(count));
            if (!output) throw std::runtime_error("写入下载文件失败。");
            progress(static_cast<float>(total) / static_cast<float>(size));
        });
        output.close();
        if (!output) throw std::runtime_error("保存下载文件失败。");
    }

private:
    struct InternetHandle {
        HINTERNET value = nullptr;
        ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    };
    void Read(const std::string& url, std::uint64_t limit,
        const std::function<void(const char*, std::size_t, std::uint64_t)>& consume) {
        if (!IsHttpsUrl(url)) throw std::runtime_error("更新地址必须使用 HTTPS。");
        if (cancelled_) throw std::runtime_error("更新已取消。");
        const auto wide = Wide(url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
            throw std::runtime_error("更新地址无效。");
        InternetHandle session{WinHttpOpen(L"PAL4Plus-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
        if (!session.value) throw std::runtime_error("无法初始化更新网络连接。");
        WinHttpSetTimeouts(session.value, 3000, 3000, 5000, 5000);
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        InternetHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
        if (!connection.value) throw std::runtime_error("无法连接更新服务器。");
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        HINTERNET request = nullptr;
        {
            std::scoped_lock lock(mutex_);
            if (cancelled_) throw std::runtime_error("更新已取消。");
            request_ = request = WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        }
        struct RequestGuard {
            Http* owner;
            ~RequestGuard() {
                std::scoped_lock lock(owner->mutex_);
                if (owner->request_) { WinHttpCloseHandle(owner->request_); owner->request_ = nullptr; }
            }
        } guard{this};
        if (!request) throw std::runtime_error("无法创建更新请求。");
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        if (!WinHttpSendRequest(request, L"Accept: application/json, application/octet-stream\r\n", static_cast<DWORD>(-1),
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr))
            throw std::runtime_error(cancelled_ ? "更新已取消。" : "更新服务器连接失败或超时。");
        DWORD status = 0, length = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) || status != 200)
            throw std::runtime_error("更新服务器返回 HTTP " + std::to_string(status) + "。");
        std::array<char, 65536> buffer{};
        std::uint64_t total = 0;
        const auto start = GetTickCount64();
        for (;;) {
            if (cancelled_) throw std::runtime_error("更新已取消。");
            if (GetTickCount64() - start > 10 * 60 * 1000) throw std::runtime_error("下载超时，请重试。");
            DWORD received = 0;
            if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &received))
                throw std::runtime_error(cancelled_ ? "更新已取消。" : "下载连接中断，请重试。");
            if (!received) break;
            total += received;
            if (total > limit) throw std::runtime_error("服务器返回的数据超过清单大小。");
            consume(buffer.data(), received, total);
        }
    }
    std::atomic_bool cancelled_ = false;
    std::mutex mutex_;
    HINTERNET request_ = nullptr;
};
}  // namespace

struct Client::Impl {
    std::filesystem::path root;
    std::string installed;
    mutable std::mutex mutex;
    Status status;
    Manifest manifest;
    std::vector<std::string> package_urls;
    std::unique_ptr<Transport> http;
    std::thread worker;

    void Set(Phase phase, std::string message = {}) {
        std::scoped_lock lock(mutex);
        status.phase = phase;
        status.message = std::move(message);
    }
    void Stop() {
        http->Cancel();
        if (worker.joinable()) worker.join();
    }
    void Check() {
        std::vector<Release> releases;
        for (const auto* source : kSources) {
            if (http->Cancelled()) return;
            try {
                auto release = ParseRelease(http->Get(source));
                if (IsNewerVersion(release.version, installed) && !release.manifest_url.empty()) releases.push_back(std::move(release));
            } catch (const std::exception&) { /* Automatic checks remain quiet. */ }
        }
        std::stable_sort(releases.begin(), releases.end(), [](const Release& a, const Release& b) {
            return IsNewerVersion(a.version, b.version);
        });
        for (const auto& release : releases) {
            if (http->Cancelled()) return;
            try {
                auto candidate = ParseManifest(http->Get(release.manifest_url));
                Version tag{}, version{};
                if (!ParseVersion(release.version, &tag) || !ParseVersion(candidate.version, &version) || tag != version)
                    throw std::runtime_error("清单版本与发布版本不匹配。");
                const auto asset = std::find_if(release.assets.begin(), release.assets.end(), [&](const auto& item) {
                    return item.first == candidate.package_name;
                });
                if (asset == release.assets.end()) continue;
                if (package_urls.empty()) {
                    manifest = std::move(candidate);
                    package_urls.push_back(asset->second);
                    std::scoped_lock lock(mutex);
                    status.version = manifest.version;
                    status.notes = manifest.notes.empty() ? release.notes : manifest.notes;
                } else if (candidate.package_sha256 == manifest.package_sha256 &&
                    candidate.package_size == manifest.package_size && candidate.version == manifest.version) {
                    package_urls.push_back(asset->second);
                }
            } catch (const std::exception&) { /* Try the other release mirror. */ }
        }
        Set(package_urls.empty() ? Phase::idle : Phase::available);
    }
    void Download() {
        std::filesystem::path stage;
        try {
            stage = CreateStage(root);
            for (std::size_t i = 0; i < package_urls.size(); ++i) {
                try {
                    http->Download(package_urls[i], stage / L"package.zip", manifest.package_size, [&](const float progress) {
                        std::scoped_lock lock(mutex);
                        status.progress = progress;
                    });
                    VerifyFile(stage / L"package.zip", manifest.package_size, manifest.package_sha256);
                    break;
                } catch (...) {
                    if (http->Cancelled() || i + 1 == package_urls.size()) throw;
                    Set(Phase::downloading, "下载失败，正在尝试另一发布源…");
                }
            }
            Set(Phase::verifying, "正在校验并解压更新包…");
            ExtractPackage(stage, manifest, [&] { return http->Cancelled(); });
            while (IsGameRunning(root)) {
                if (http->Cancelled()) throw std::runtime_error("更新已取消。");
                Set(Phase::waiting_for_game, "更新已准备好，请保存进度并退出游戏。退出后将继续更新。");
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (http->Cancelled()) throw std::runtime_error("更新已取消。");
            std::scoped_lock lock(mutex);
            status.stage = stage;
            status.phase = Phase::ready;
        } catch (const std::exception& error) {
            if (!stage.empty()) {
                std::error_code ignored;
                std::filesystem::remove_all(stage, ignored);
            }
            Set(http->Cancelled() ? Phase::available : Phase::failed, error.what());
        }
    }
};

Client::Client(std::filesystem::path root, std::string version, std::unique_ptr<Transport> transport)
    : impl_(std::make_unique<Impl>()) {
    impl_->root = std::move(root);
    impl_->installed = std::move(version);
    impl_->http = transport ? std::move(transport) : std::make_unique<Http>();
    Check();
}
Client::~Client() { Cancel(); }
Status Client::Snapshot() const { std::scoped_lock lock(impl_->mutex); return impl_->status; }
void Client::Cancel() {
    impl_->Stop();
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->status.stage.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(impl_->status.stage, ignored);
        impl_->status.stage.clear();
        impl_->status.phase = Phase::available;
    }
}
std::filesystem::path Client::TakeReadyStage() {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->status.phase != Phase::ready) return {};
    return std::exchange(impl_->status.stage, {});
}
void Client::Check() {
    Cancel();
    impl_->http->Reset();
    impl_->package_urls.clear();
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->status = {};
    }
    impl_->Set(Phase::checking);
    impl_->worker = std::thread([this] {
        try { impl_->Check(); } catch (...) { impl_->Set(Phase::idle); }
    });
}
void Client::Download() {
    impl_->Stop();
    if (impl_->package_urls.empty()) return;
    impl_->http->Reset();
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->status.progress = 0.0F;
        impl_->status.phase = Phase::downloading;
        impl_->status.message = "正在下载更新…";
    }
    impl_->worker = std::thread([this] { impl_->Download(); });
}
}  // namespace pal4::inject::update

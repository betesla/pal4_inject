#pragma once
#include "update_manifest.h"
#include <memory>
#include <functional>

namespace pal4::inject::update {
// Injectable I/O boundary: production uses WinHTTP; tests use offline fixtures.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void Cancel() = 0;
    virtual void Reset() = 0;
    virtual bool Cancelled() const = 0;
    virtual std::string Get(const std::string& url) = 0;
    virtual void Download(const std::string& url, const std::filesystem::path& path,
        std::uint64_t size, const std::function<void(float)>& progress) = 0;
};
enum class Phase { checking, idle, available, downloading, verifying, waiting_for_game, ready, failed };
struct Status {
    Phase phase = Phase::checking;
    std::string version;
    std::string notes;
    std::string message;
    float progress = 0.0F;
    std::filesystem::path stage;
};

class Client {
public:
    Client(std::filesystem::path root, std::string installed_version,
        std::unique_ptr<Transport> transport = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Status Snapshot() const;
    void Check();
    void Download();
    void Cancel();
    std::filesystem::path TakeReadyStage();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace pal4::inject::update

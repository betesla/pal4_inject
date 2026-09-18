#pragma once
#include "update_manifest.h"
#include <functional>
#include <windows.h>

namespace pal4::inject::update {
std::wstring Wide(std::string_view text);
std::string Utf8(std::wstring_view text);
std::wstring QuoteArgument(std::wstring_view text);
std::filesystem::path ExecutablePath();
std::filesystem::path CreateStage(const std::filesystem::path& root);
bool IsGameRunning(const std::filesystem::path& root);
void ExtractPackage(const std::filesystem::path& stage, const Manifest& manifest,
    const std::function<bool()>& cancelled);
void VerifyPayload(const std::filesystem::path& stage, const Manifest& manifest);

// Files and backup journal are kept until the new launcher confirms startup.
void InstallTransaction(const std::filesystem::path& root, const std::filesystem::path& stage,
    const Manifest& manifest);
void RollbackTransaction(const std::filesystem::path& root, const std::filesystem::path& stage);
void CommitTransaction(const std::filesystem::path& root, const std::filesystem::path& stage);
// True means a recovery helper took over; the calling launcher must exit.
bool RecoverInterruptedUpdate(const std::filesystem::path& root);

void LaunchInstaller(const std::filesystem::path& root, const std::filesystem::path& stage);
int RunInstaller(const std::filesystem::path& job,
    const std::function<void(const std::wstring&)>& report_error = {});
void SetReadyEvent(std::wstring name);
void SignalReady();
}  // namespace pal4::inject::update

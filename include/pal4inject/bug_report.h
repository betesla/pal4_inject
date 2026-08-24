#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pal4::inject {

struct BugReportData {
    std::filesystem::path crash_report_path;
    std::filesystem::path crash_dump_path;
    std::filesystem::path runtime_log_path;
    std::string sanitized_crash_report;
    std::string sanitized_runtime_log;

    bool HasCrashReport() const noexcept {
        return !crash_report_path.empty() && !sanitized_crash_report.empty();
    }

    bool HasRuntimeLog() const noexcept {
        return !runtime_log_path.empty() && !sanitized_runtime_log.empty();
    }
};

struct BugReportBodyOptions {
    std::string description;
    std::string version;
    std::string build_id;
    bool include_crash_report = false;
    bool include_runtime_log = false;
};

using BugReportRedaction = std::pair<std::string, std::string>;

BugReportData LoadLatestBugReportData(
    const std::filesystem::path& data_directory,
    const std::vector<BugReportRedaction>& redactions = {});

std::string SanitizeDiagnosticText(
    std::string text,
    const std::vector<BugReportRedaction>& redactions = {});

std::string BuildBugReportBody(
    const BugReportData& data,
    const BugReportBodyOptions& options);

std::string BuildGiteeNewIssueUrl(
    std::string_view base_url,
    std::string_view title,
    std::string_view body,
    std::size_t maximum_url_length = 30000);

}  // namespace pal4::inject

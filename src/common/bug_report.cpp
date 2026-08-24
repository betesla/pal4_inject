#include "pal4inject/bug_report.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace pal4::inject {
namespace {

constexpr std::size_t kCrashReportLimit = 24 * 1024;
constexpr std::size_t kRuntimeLogTailLimit = 16 * 1024;

std::string LowerAscii(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

void ReplaceAllCaseInsensitive(
    std::string* const text,
    const std::string_view needle,
    const std::string_view replacement) {
    if (!text || needle.empty()) {
        return;
    }
    std::string lowered = LowerAscii(*text);
    const std::string lowered_needle = LowerAscii(needle);
    std::size_t position = 0;
    while ((position = lowered.find(lowered_needle, position)) != std::string::npos) {
        text->replace(position, needle.size(), replacement);
        lowered.replace(position, needle.size(), LowerAscii(replacement));
        position += replacement.size();
    }
}

std::string ReadFileHead(const std::filesystem::path& path, const std::size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::string result(limit, '\0');
    file.read(result.data(), static_cast<std::streamsize>(result.size()));
    result.resize(static_cast<std::size_t>(file.gcount()));
    if (file.peek() != std::char_traits<char>::eof()) {
        result += "\n[崩溃报告因长度限制已截断]\n";
    }
    return result;
}

std::string ReadFileTail(const std::filesystem::path& path, const std::size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const auto end = file.tellg();
    if (end <= 0) {
        return {};
    }
    const auto size = static_cast<std::size_t>(end);
    const auto read_size = std::min(size, limit);
    file.seekg(static_cast<std::streamoff>(size - read_size), std::ios::beg);
    std::string result(read_size, '\0');
    file.read(result.data(), static_cast<std::streamsize>(result.size()));
    result.resize(static_cast<std::size_t>(file.gcount()));
    if (read_size < size) {
        const auto first_line_end = result.find('\n');
        if (first_line_end != std::string::npos) {
            result.erase(0, first_line_end + 1);
        }
        result.insert(0, "[仅包含运行日志末尾]\n");
    }
    return result;
}

bool IsCrashReportFilename(const std::filesystem::path& path) {
    const auto filename = LowerAscii(path.filename().string());
    return filename.starts_with("pal4_inject_crash_") &&
        LowerAscii(path.extension().string()) == ".txt";
}

std::filesystem::path FindLatestCrashReport(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return {};
    }
    std::filesystem::path latest;
    std::filesystem::file_time_type latest_time{};
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error || !IsCrashReportFilename(iterator->path())) {
            error.clear();
            continue;
        }
        const auto write_time = iterator->last_write_time(error);
        if (error) {
            error.clear();
            continue;
        }
        if (latest.empty() || write_time > latest_time) {
            latest = iterator->path();
            latest_time = write_time;
        }
    }
    return latest;
}

bool IsSensitiveLine(const std::string_view line) {
    const auto lowered = LowerAscii(line);
    constexpr std::array sensitive_keys{
        "access_token",
        "authorization",
        "password",
        "passwd",
        "api_key",
        "client_secret",
    };
    return std::any_of(sensitive_keys.begin(), sensitive_keys.end(), [&](const char* key) {
        return lowered.find(key) != std::string::npos;
    });
}

std::string RedactSensitiveLines(const std::string_view text) {
    std::istringstream input{std::string(text)};
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        if (IsSensitiveLine(line)) {
            const auto delimiter = line.find_first_of("=:");
            line = delimiter == std::string::npos
                ? "[敏感字段已隐藏]"
                : line.substr(0, delimiter + 1) + "<已隐藏>";
        }
        output << line;
        if (!input.eof()) {
            output << '\n';
        }
    }
    return output.str();
}

std::string IndentDiagnostic(const std::string_view text) {
    std::ostringstream output;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        output << "    " << line << '\n';
    }
    return output.str();
}

bool IsUnreservedUrlByte(const unsigned char value) {
    return std::isalnum(value) != 0 || value == '-' || value == '_' || value == '.' || value == '~';
}

std::string PercentEncode(const std::string_view text) {
    constexpr char hexadecimal[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size() * 2);
    for (const unsigned char value : text) {
        if (IsUnreservedUrlByte(value)) {
            encoded.push_back(static_cast<char>(value));
        } else {
            encoded.push_back('%');
            encoded.push_back(hexadecimal[value >> 4]);
            encoded.push_back(hexadecimal[value & 0x0F]);
        }
    }
    return encoded;
}

void TrimIncompleteUtf8Suffix(std::string* const text) {
    if (!text || text->empty()) {
        return;
    }
    std::size_t sequence_start = text->size() - 1;
    while (sequence_start > 0 &&
           (static_cast<unsigned char>((*text)[sequence_start]) & 0xC0U) == 0x80U) {
        --sequence_start;
    }
    const unsigned char lead = static_cast<unsigned char>((*text)[sequence_start]);
    std::size_t expected_length = 1;
    if ((lead & 0xE0U) == 0xC0U) {
        expected_length = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
        expected_length = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
        expected_length = 4;
    }
    if (text->size() - sequence_start < expected_length) {
        text->resize(sequence_start);
    }
}

std::string TrimBodyToUrlLimit(
    const std::string_view prefix,
    const std::string_view body,
    const std::size_t maximum_url_length) {
    std::string candidate(body);
    while (!candidate.empty() && prefix.size() + PercentEncode(candidate).size() > maximum_url_length) {
        const std::size_t reduced_size = candidate.size() > 512 ? candidate.size() - 512 : 0;
        candidate.resize(reduced_size);
        TrimIncompleteUtf8Suffix(&candidate);
    }
    if (candidate.size() < body.size()) {
        constexpr std::string_view suffix =
            "\n\n> 预填内容受 URL 长度限制已截断；完整内容已复制到剪贴板，可全选后粘贴。";
        while (!candidate.empty() &&
               prefix.size() + PercentEncode(candidate).size() + PercentEncode(suffix).size() >
                   maximum_url_length) {
            candidate.resize(candidate.size() - 1);
            TrimIncompleteUtf8Suffix(&candidate);
        }
        candidate += suffix;
    }
    return candidate;
}

}  // namespace

BugReportData LoadLatestBugReportData(
    const std::filesystem::path& data_directory,
    const std::vector<BugReportRedaction>& redactions) {
    BugReportData data{};
    data.crash_report_path = FindLatestCrashReport(data_directory);
    data.runtime_log_path = data_directory / "pal4_inject_runtime.log";
    if (!data.crash_report_path.empty()) {
        data.crash_dump_path = data.crash_report_path;
        data.crash_dump_path.replace_extension(".dmp");
        std::error_code error;
        if (!std::filesystem::is_regular_file(data.crash_dump_path, error)) {
            data.crash_dump_path.clear();
        }
        data.sanitized_crash_report = SanitizeDiagnosticText(
            ReadFileHead(data.crash_report_path, kCrashReportLimit),
            redactions);
    }
    std::error_code error;
    if (std::filesystem::is_regular_file(data.runtime_log_path, error)) {
        data.sanitized_runtime_log = SanitizeDiagnosticText(
            ReadFileTail(data.runtime_log_path, kRuntimeLogTailLimit),
            redactions);
    } else {
        data.runtime_log_path.clear();
    }
    return data;
}

std::string SanitizeDiagnosticText(
    std::string text,
    const std::vector<BugReportRedaction>& redactions) {
    for (const auto& [needle, replacement] : redactions) {
        ReplaceAllCaseInsensitive(&text, needle, replacement);
        std::string slash_needle = needle;
        std::replace(slash_needle.begin(), slash_needle.end(), '\\', '/');
        if (slash_needle != needle) {
            ReplaceAllCaseInsensitive(&text, slash_needle, replacement);
        }
    }
    return RedactSensitiveLines(text);
}

std::string BuildBugReportBody(
    const BugReportData& data,
    const BugReportBodyOptions& options) {
    std::ostringstream output;
    output << "## 问题描述\n\n"
           << (options.description.empty() ? "请在这里补充问题现象和复现步骤。" : options.description)
           << "\n\n## 环境\n\n"
           << "- PAL4 Inject：" << options.version << '\n'
           << "- Build：" << options.build_id << '\n';
    if (options.include_crash_report && data.HasCrashReport()) {
        output << "\n## 崩溃报告（已脱敏）\n\n"
               << IndentDiagnostic(data.sanitized_crash_report);
    }
    if (options.include_runtime_log && data.HasRuntimeLog()) {
        output << "\n## 运行日志末尾（已脱敏）\n\n"
               << IndentDiagnostic(data.sanitized_runtime_log);
    }
    if (!data.crash_dump_path.empty()) {
        output << "\n> 本次反馈未上传 minidump；第一版仅提交用户确认过的文本内容。\n";
    }
    output << "\n---\n由 PAL4 Inject 启动器整理，提交前已由用户确认。\n";
    return output.str();
}

std::string BuildGiteeNewIssueUrl(
    const std::string_view base_url,
    const std::string_view title,
    const std::string_view body,
    const std::size_t maximum_url_length) {
    const std::string prefix = std::string(base_url) +
        "?issue%5Btitle%5D=" + PercentEncode(title) +
        "&issue%5Bdescription%5D=";
    const std::string fitted_body = TrimBodyToUrlLimit(prefix, body, maximum_url_length);
    return prefix + PercentEncode(fitted_body);
}

}  // namespace pal4::inject

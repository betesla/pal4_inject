#include "loose_file_load_log.h"

#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/loose_file_overlay.h"

namespace pal4::inject {
namespace {

constexpr std::uintmax_t kMaxLogSizeBeforeRotation = 8U * 1024U * 1024U;

struct LooseFileLoggerState {
    std::mutex mutex;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path path;

    ~LooseFileLoggerState() {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
};

LooseFileLoggerState& LoggerState() {
    static LooseFileLoggerState state;
    return state;
}

std::string WideToUtf8(const std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    return result;
}

std::string AnsiToUtf8(const std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_ACP,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::string(text);
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_ACP,
        0,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        required);
    return WideToUtf8(wide);
}

std::string JsonEscape(const std::string_view text) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char ch : text) {
        switch (ch) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (ch < 0x20) {
                output << "\\u" << std::setw(4) << static_cast<unsigned int>(ch);
            } else {
                output << static_cast<char>(ch);
            }
            break;
        }
    }
    return output.str();
}

std::string UtcTimestamp() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char buffer[32]{};
    sprintf_s(
        buffer,
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    return buffer;
}

void WriteLine(const HANDLE file, const std::string& line) noexcept {
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::string record = line + "\r\n";
    DWORD written = 0;
    WriteFile(
        file,
        record.data(),
        static_cast<DWORD>(record.size()),
        &written,
        nullptr);
}

std::string CommonRecordPrefix(const std::string_view event) {
    std::ostringstream output;
    output << "{\"time_utc\":\"" << UtcTimestamp()
           << "\",\"pid\":" << GetCurrentProcessId()
           << ",\"event\":\"" << JsonEscape(event) << '"';
    return output.str();
}

bool EnsureLogOpen(
    LooseFileLoggerState* const state,
    const std::filesystem::path& game_root,
    const HookMode mode) noexcept {
    if (!state) {
        return false;
    }
    const auto log_path = LooseFileLoadLogPath(game_root);
    if (state->file != INVALID_HANDLE_VALUE && state->path == log_path) {
        return true;
    }
    if (state->file != INVALID_HANDLE_VALUE) {
        CloseHandle(state->file);
        state->file = INVALID_HANDLE_VALUE;
    }

    std::error_code error;
    std::filesystem::create_directories(log_path.parent_path(), error);
    if (error) {
        return false;
    }
    const auto size = std::filesystem::file_size(log_path, error);
    if (!error && size >= kMaxLogSizeBeforeRotation) {
        auto backup_path = log_path;
        backup_path += L".1";
        std::filesystem::remove(backup_path, error);
        error.clear();
        std::filesystem::rename(log_path, backup_path, error);
    }

    state->file = CreateFileW(
        log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (state->file == INVALID_HANDLE_VALUE) {
        state->path.clear();
        return false;
    }
    state->path = log_path;

    std::ostringstream session;
    session << CommonRecordPrefix("session_start")
            << ",\"mode\":\"" << ToString(mode)
            << "\",\"gamepatch_root\":\""
            << JsonEscape(WideToUtf8(GamePatchRoot(game_root).wstring()))
            << "\",\"format_version\":3}";
    WriteLine(state->file, session.str());
    return true;
}

std::string FormatEntry(const LooseFileLoadLogEntry& entry) {
    std::ostringstream output;
    output << CommonRecordPrefix(entry.event)
           << ",\"loader\":\"" << JsonEscape(entry.loader) << '"'
           << ",\"mode\":\"" << ToString(entry.mode) << '"'
           << ",\"resource\":\""
           << JsonEscape(AnsiToUtf8(entry.resource_path)) << '"';
    if (!entry.file_path.empty()) {
        output << ",\"file\":\""
               << JsonEscape(WideToUtf8(entry.file_path.wstring())) << '"';
    }
    if (!entry.reason.empty()) {
        output << ",\"reason\":\""
               << JsonEscape(AnsiToUtf8(entry.reason)) << '"';
    }
    if (entry.has_size) {
        output << ",\"size\":" << entry.size;
    }
    if (!entry.fallback_source.empty()) {
        output << ",\"fallback_source\":\""
               << JsonEscape(entry.fallback_source) << '"';
    }
    if (entry.fallback_result_known) {
        output << ",\"fallback_opened\":"
               << (entry.fallback_opened ? "true" : "false");
    }
    if (entry.fallback_used) {
        output << ",\"fallback_used\":true";
    }
    if (entry.fallback_source == "cpk") {
        if (entry.fallback_result_known) {
            output << ",\"cpk_opened\":"
                   << (entry.fallback_opened ? "true" : "false");
        }
        if (entry.fallback_used) {
            output << ",\"fallback_to_cpk\":true";
        }
    }
    output << '}';
    return output.str();
}

}  // namespace

std::string FormatLooseFileLoadLogEntry(
    const LooseFileLoadLogEntry& entry) noexcept {
    try {
        return FormatEntry(entry);
    } catch (...) {
        return {};
    }
}

void AppendLooseFileLoadLog(
    const std::filesystem::path& game_root,
    const LooseFileLoadLogEntry& entry) noexcept {
    try {
        auto& state = LoggerState();
        std::scoped_lock lock(state.mutex);
        if (EnsureLogOpen(&state, game_root, entry.mode)) {
            const auto record = FormatLooseFileLoadLogEntry(entry);
            if (!record.empty()) {
                WriteLine(state.file, record);
            }
        }
    } catch (...) {
        // Logging must never change the game's resource-loading result.
    }
}

}  // namespace pal4::inject

#include "pal4inject/protocol.h"

#include <charconv>
#include <cctype>
#include <string>
#include <vector>

namespace pal4::inject {
namespace {

std::vector<std::string> Tokenize(const std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    for (const char ch : text) {
        if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool ParseUint32Value(const std::string_view text, std::uint32_t* out) {
    if (!out || text.empty()) {
        return false;
    }

    const char* begin = text.data();
    const char* end = text.data() + text.size();
    unsigned long value = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        auto [ptr, ec] = std::from_chars(begin + 2, end, value, 16);
        if (ec != std::errc{} || ptr != end) {
            return false;
        }
    } else {
        auto [ptr, ec] = std::from_chars(begin, end, value, 10);
        if (ec != std::errc{} || ptr != end) {
            return false;
        }
    }
    *out = static_cast<std::uint32_t>(value);
    return true;
}

std::string HexByte(const unsigned char value) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.push_back(kHex[(value >> 4) & 0xF]);
    out.push_back(kHex[value & 0xF]);
    return out;
}

int DecodeHexNibble(const char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

}  // namespace

std::string EscapeProtocolToken(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == ':' || ch == '\\' || ch == '/') {
            out.push_back(static_cast<char>(ch));
            continue;
        }
        out.push_back('%');
        out += HexByte(ch);
    }
    return out;
}

std::string UnescapeProtocolToken(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%' || i + 2 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        const int high = DecodeHexNibble(text[i + 1]);
        const int low = DecodeHexNibble(text[i + 2]);
        if (high < 0 || low < 0) {
            out.push_back(text[i]);
            continue;
        }
        out.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return out;
}

const char* ToString(const ProtocolCommandKind kind) noexcept {
    switch (kind) {
    case ProtocolCommandKind::invalid:
        return "invalid";
    case ProtocolCommandKind::ping:
        return "ping";
    case ProtocolCommandKind::hook_status:
        return "hook_status";
    case ProtocolCommandKind::enqueue_ui_message:
        return "enqueue_ui_message";
    case ProtocolCommandKind::simulate_key:
        return "simulate_key";
    case ProtocolCommandKind::read_ui_state:
        return "read_ui_state";
    case ProtocolCommandKind::read_paliv_state:
        return "read_paliv_state";
    case ProtocolCommandKind::wait_for_hook_calls:
        return "wait_for_hook_calls";
    case ProtocolCommandKind::wait_for_paliv_state:
        return "wait_for_paliv_state";
    case ProtocolCommandKind::read_event_log:
        return "read_event_log";
    case ProtocolCommandKind::set_hook_mode:
        return "set_hook_mode";
    case ProtocolCommandKind::shutdown:
        return "shutdown";
    }
    return "invalid";
}

bool TryParseProtocolCommandKind(
    const std::string_view text,
    ProtocolCommandKind* out) noexcept {
    if (!out) {
        return false;
    }
    for (int raw = static_cast<int>(ProtocolCommandKind::invalid);
         raw <= static_cast<int>(ProtocolCommandKind::shutdown);
         ++raw) {
        const auto kind = static_cast<ProtocolCommandKind>(raw);
        if (text == ToString(kind)) {
            *out = kind;
            return true;
        }
    }
    return false;
}

std::string FormatProtocolCommand(const ProtocolCommand& command) {
    std::string out = ToString(command.kind);
    switch (command.kind) {
    case ProtocolCommandKind::enqueue_ui_message:
        out += " msg=" + std::to_string(command.ui_message.msg);
        out += " wparam=" + std::to_string(command.ui_message.wparam);
        out += " lparam=" + std::to_string(command.ui_message.lparam);
        out += " bypass_os_queue=" + std::string(command.ui_message.bypass_os_queue ? "1" : "0");
        break;
    case ProtocolCommandKind::simulate_key:
        out += " vk=" + std::to_string(command.virtual_key);
        out += " key_up=" + std::string(command.key_up ? "1" : "0");
        out += " bypass_os_queue=" + std::string(command.ui_message.bypass_os_queue ? "1" : "0");
        break;
    case ProtocolCommandKind::wait_for_hook_calls:
        out += " hook=" + std::string(ToString(command.hook_id));
        out += " count=" + std::to_string(command.expected_call_count);
        out += " timeout_ms=" + std::to_string(command.timeout_ms);
        break;
    case ProtocolCommandKind::wait_for_paliv_state:
        out += " entry=" + std::to_string(command.expected_paliv_entry);
        out += " timeout_ms=" + std::to_string(command.timeout_ms);
        break;
    case ProtocolCommandKind::set_hook_mode:
        out += " hook=" + std::string(ToString(command.hook_id));
        out += " mode=" + std::string(ToString(command.hook_mode));
        break;
    default:
        break;
    }
    return out;
}

bool ParseProtocolCommand(
    const std::string_view text,
    ProtocolCommand* command,
    std::string* error) {
    if (!command) {
        if (error) {
            *error = "command output pointer is null";
        }
        return false;
    }

    const auto tokens = Tokenize(text);
    if (tokens.empty()) {
        if (error) {
            *error = "empty command";
        }
        return false;
    }

    ProtocolCommand parsed{};
    if (!TryParseProtocolCommandKind(tokens.front(), &parsed.kind)) {
        if (error) {
            *error = "unknown command";
        }
        return false;
    }

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const auto pivot = tokens[i].find('=');
        if (pivot == std::string::npos) {
            if (error) {
                *error = "malformed token";
            }
            return false;
        }
        const std::string key = tokens[i].substr(0, pivot);
        const std::string value = UnescapeProtocolToken(tokens[i].substr(pivot + 1));

        if (key == "msg") {
            if (!ParseUint32Value(value, &parsed.ui_message.msg)) {
                if (error) {
                    *error = "invalid msg";
                }
                return false;
            }
        } else if (key == "wparam") {
            if (!ParseUint32Value(value, &parsed.ui_message.wparam)) {
                if (error) {
                    *error = "invalid wparam";
                }
                return false;
            }
        } else if (key == "lparam") {
            if (!ParseUint32Value(value, &parsed.ui_message.lparam)) {
                if (error) {
                    *error = "invalid lparam";
                }
                return false;
            }
        } else if (key == "bypass_os_queue") {
            parsed.ui_message.bypass_os_queue = (value == "1" || value == "true");
        } else if (key == "vk") {
            if (!ParseUint32Value(value, &parsed.virtual_key)) {
                if (error) {
                    *error = "invalid vk";
                }
                return false;
            }
        } else if (key == "count") {
            std::uint32_t tmp = 0;
            if (!ParseUint32Value(value, &tmp)) {
                if (error) {
                    *error = "invalid count";
                }
                return false;
            }
            parsed.expected_call_count = tmp;
        } else if (key == "entry") {
            if (!ParseUint32Value(value, &parsed.expected_paliv_entry)) {
                if (error) {
                    *error = "invalid entry";
                }
                return false;
            }
        } else if (key == "timeout_ms") {
            if (!ParseUint32Value(value, &parsed.timeout_ms)) {
                if (error) {
                    *error = "invalid timeout_ms";
                }
                return false;
            }
        } else if (key == "key_up") {
            parsed.key_up = (value == "1" || value == "true");
        } else if (key == "hook") {
            if (!TryParseHookId(value, &parsed.hook_id)) {
                if (error) {
                    *error = "invalid hook";
                }
                return false;
            }
        } else if (key == "mode") {
            if (!TryParseHookMode(value, &parsed.hook_mode)) {
                if (error) {
                    *error = "invalid mode";
                }
                return false;
            }
        }
    }

    *command = parsed;
    return true;
}

std::string FormatProtocolResponse(const ProtocolResponse& response) {
    std::string out = response.ok ? "ok" : "error";
    if (!response.status.empty()) {
        out += " status=" + EscapeProtocolToken(response.status);
    }
    if (!response.message.empty()) {
        out += " message=" + EscapeProtocolToken(response.message);
    }
    for (const auto& [key, value] : response.fields) {
        out += ' ';
        out += key;
        out += '=';
        out += EscapeProtocolToken(value);
    }
    return out;
}

bool ParseProtocolResponse(
    const std::string_view text,
    ProtocolResponse* response,
    std::string* error) {
    if (!response) {
        if (error) {
            *error = "response output pointer is null";
        }
        return false;
    }

    const auto tokens = Tokenize(text);
    if (tokens.empty()) {
        if (error) {
            *error = "empty response";
        }
        return false;
    }

    ProtocolResponse parsed{};
    if (tokens.front() == "ok") {
        parsed.ok = true;
    } else if (tokens.front() == "error") {
        parsed.ok = false;
    } else {
        if (error) {
            *error = "response must start with ok/error";
        }
        return false;
    }

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const auto pivot = tokens[i].find('=');
        if (pivot == std::string::npos) {
            if (error) {
                *error = "malformed response token";
            }
            return false;
        }
        const std::string key = tokens[i].substr(0, pivot);
        const std::string value = UnescapeProtocolToken(tokens[i].substr(pivot + 1));
        if (key == "status") {
            parsed.status = value;
        } else if (key == "message") {
            parsed.message = value;
        } else {
            parsed.fields[key] = value;
        }
    }

    *response = parsed;
    return true;
}

}  // namespace pal4::inject

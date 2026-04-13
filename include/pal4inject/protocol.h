#pragma once

#include <map>
#include <string>
#include <string_view>

#include "pal4inject/memory_debug.h"
#include "pal4inject/types.h"

namespace pal4::inject {

enum class ProtocolCommandKind : std::uint8_t {
    invalid = 0,
    ping,
    hook_status,
    enqueue_ui_message,
    simulate_key,
    read_ui_state,
    read_paliv_state,
    wait_for_hook_calls,
    wait_for_paliv_state,
    read_event_log,
    set_hook_mode,
    snapshot_ui,
    click_ui_ref,
    fill_ui_ref,
    type_text,
    query_memory,
    read_memory,
    write_memory,
    shutdown,
};

struct ProtocolCommand {
    ProtocolCommandKind kind = ProtocolCommandKind::invalid;
    UiMessageCommand ui_message{};
    HookId hook_id = HookId::process_ui_event;
    HookMode hook_mode = HookMode::observe_only;
    std::uint32_t virtual_key = 0;
    std::uint64_t expected_call_count = 0;
    std::uint32_t expected_paliv_entry = 0;
    std::uint32_t timeout_ms = 0;
    AddressSpace address_space = AddressSpace::runtime_va;
    std::uint32_t address = 0;
    std::uint32_t size = 0;
    bool key_up = false;
    bool unsafe_code_write = false;
    std::string ui_ref;
    std::string text;
    std::string hex_bytes;
};

struct ProtocolResponse {
    bool ok = false;
    std::string status;
    std::map<std::string, std::string> fields;
    std::string message;
};

std::string EscapeProtocolToken(std::string_view text);
std::string UnescapeProtocolToken(std::string_view text);

std::string FormatProtocolCommand(const ProtocolCommand& command);
bool ParseProtocolCommand(
    std::string_view text,
    ProtocolCommand* command,
    std::string* error);

std::string FormatProtocolResponse(const ProtocolResponse& response);
bool ParseProtocolResponse(
    std::string_view text,
    ProtocolResponse* response,
    std::string* error);

const char* ToString(ProtocolCommandKind kind) noexcept;
bool TryParseProtocolCommandKind(
    std::string_view text,
    ProtocolCommandKind* out) noexcept;

}  // namespace pal4::inject

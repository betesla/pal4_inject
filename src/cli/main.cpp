#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/launcher.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/memory_debug.h"
#include "pal4inject/protocol.h"
#include "pal4inject/ui_snapshot.h"

namespace {

using pal4::inject::AddressSpace;
using pal4::inject::MemoryScalarType;
using pal4::inject::ProtocolCommand;
using pal4::inject::ProtocolCommandKind;
using pal4::inject::ProtocolResponse;
using pal4::inject::UiSnapshotTree;

struct CliOptions {
    std::string pipe_name;
    DWORD pid = 0;
    DWORD timeout_ms = 5000;
    std::vector<std::string> args;
};

void PrintUsage() {
    std::cout
        << "Usage: pal4_inject_cli (--pipe <name> | --pid <pid>) <command> [args]\n"
        << "Commands:\n"
        << "  snapshot\n"
        << "  snapshot-raw\n"
        << "  click <ref>\n"
        << "  click-pt <x> <y> [--os-queue]\n"
        << "  click-logical <x> <y> [--os-queue]\n"
        << "  fill <ref> <text>\n"
        << "  type <text>\n"
        << "  press <key> [--os-queue]\n"
        << "  state\n"
        << "  event-log\n"
        << "  wait-path <window-path> [timeout-ms]\n"
        << "  wait-text <substring> [timeout-ms]\n"
        << "  mem-query (--ida|--va) <addr>\n"
        << "  mem-read (--ida|--va) <addr> --size <n>\n"
        << "  mem-read-scalar (--ida|--va) <addr> --type <type>\n"
        << "  mem-write-bytes (--ida|--va) <addr> <hex> [--unsafe-code-write]\n"
        << "  mem-write-scalar (--ida|--va) <addr> --type <type> <value> [--unsafe-code-write]\n";
}

std::string JoinArguments(
    const std::vector<std::string>& args,
    const std::size_t begin,
    const std::size_t end) {
    std::string joined;
    for (std::size_t i = begin; i < end; ++i) {
        if (!joined.empty()) {
            joined.push_back(' ');
        }
        joined += args[i];
    }
    return joined;
}

bool ParseCliOptions(
    const int argc,
    char** argv,
    CliOptions* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "cli options output pointer is null";
        }
        return false;
    }

    CliOptions parsed{};
    int i = 1;
    for (; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--pipe" && i + 1 < argc) {
            parsed.pipe_name = argv[++i];
            continue;
        }
        if (arg == "--pid" && i + 1 < argc) {
            parsed.pid = static_cast<DWORD>(std::stoul(argv[++i]));
            parsed.pipe_name = pal4::inject::BuildPipeName(parsed.pid);
            continue;
        }
        if (arg == "--timeout-ms" && i + 1 < argc) {
            parsed.timeout_ms = static_cast<DWORD>(std::stoul(argv[++i]));
            continue;
        }
        break;
    }

    if (parsed.pipe_name.empty()) {
        if (error) {
            *error = "--pipe or --pid is required";
        }
        return false;
    }
    for (; i < argc; ++i) {
        parsed.args.push_back(argv[i]);
    }
    if (parsed.args.empty()) {
        if (error) {
            *error = "missing command";
        }
        return false;
    }

    *out = std::move(parsed);
    return true;
}

bool SendCommand(
    const CliOptions& options,
    const ProtocolCommand& command,
    ProtocolResponse* out,
    std::string* error) {
    std::string response_text;
    const bool sent = pal4::inject::SendPipeCommand(
        options.pipe_name,
        pal4::inject::FormatProtocolCommand(command),
        &response_text,
        options.timeout_ms,
        error);
    if (!sent) {
        return false;
    }

    ProtocolResponse response{};
    if (!pal4::inject::ParseProtocolResponse(response_text, &response, error)) {
        return false;
    }
    if (out) {
        *out = std::move(response);
    }
    return true;
}

bool ExpectOkResponse(
    const CliOptions& options,
    const ProtocolCommand& command,
    ProtocolResponse* out,
    std::string* error) {
    ProtocolResponse response{};
    if (!SendCommand(options, command, &response, error)) {
        return false;
    }
    if (!response.ok) {
        if (error) {
            *error = response.message.empty() ? "runtime returned an error" : response.message;
        }
        return false;
    }
    if (out) {
        *out = std::move(response);
    }
    return true;
}

bool ReadSnapshotTree(
    const CliOptions& options,
    UiSnapshotTree* out,
    std::string* error) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::snapshot_ui;
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return false;
    }

    const auto it = response.fields.find("tree");
    if (it == response.fields.end()) {
        if (error) {
            *error = "snapshot_ui response is missing the tree field";
        }
        return false;
    }
    const bool ok = pal4::inject::ParseUiSnapshotTree(it->second, out, error);
    if (!ok && error) {
        const std::size_t tail_len = std::min<std::size_t>(64, it->second.size());
        *error += " | tree_size=" + std::to_string(it->second.size()) +
            " | tail=" + it->second.substr(it->second.size() - tail_len, tail_len);
    }
    return ok;
}

void PrintFieldMap(const ProtocolResponse& response) {
    if (!response.status.empty()) {
        std::cout << "status=" << response.status << "\n";
    }
    for (const auto& [key, value] : response.fields) {
        std::cout << key << "=" << value << "\n";
    }
}

void PrintMemoryRegionFields(const ProtocolResponse& response) {
    static constexpr const char* kOrderedKeys[] = {
        "address_space",
        "input_address",
        "resolved_address",
        "region_base",
        "region_allocation_base",
        "region_size",
        "region_state",
        "region_type",
        "region_protect",
        "region_readable",
        "region_writable",
        "region_executable",
    };
    for (const auto* key : kOrderedKeys) {
        const auto it = response.fields.find(key);
        if (it != response.fields.end()) {
            std::cout << key << "=" << it->second << "\n";
        }
    }
}

bool ParseAddressSelector(
    const std::vector<std::string>& args,
    const std::size_t start,
    AddressSpace* out_space,
    std::uint32_t* out_address,
    std::size_t* next_index,
    std::string* error) {
    if (start + 1 >= args.size()) {
        if (error) {
            *error = "expected (--ida|--va) <addr>";
        }
        return false;
    }

    AddressSpace space = AddressSpace::runtime_va;
    if (args[start] == "--ida") {
        space = AddressSpace::ida_ea;
    } else if (args[start] == "--va") {
        space = AddressSpace::runtime_va;
    } else {
        if (error) {
            *error = "expected --ida or --va";
        }
        return false;
    }

    std::uint32_t address = 0;
    if (!pal4::inject::ParseAddressValue(args[start + 1], &address)) {
        if (error) {
            *error = "invalid address";
        }
        return false;
    }

    if (out_space) {
        *out_space = space;
    }
    if (out_address) {
        *out_address = address;
    }
    if (next_index) {
        *next_index = start + 2;
    }
    return true;
}

bool ParseVirtualKey(
    const std::string& text,
    std::uint32_t* out_key,
    std::string* error) {
    if (!out_key) {
        if (error) {
            *error = "virtual key output pointer is null";
        }
        return false;
    }

    if (text.size() == 1) {
        SHORT vk = VkKeyScanA(text[0]);
        if (vk != -1) {
            *out_key = static_cast<std::uint32_t>(vk & 0xFF);
            return true;
        }
    }

    std::string normalized = text;
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

    const struct {
        const char* name;
        std::uint32_t key;
    } kNamedKeys[] = {
        {"ENTER", VK_RETURN},
        {"RETURN", VK_RETURN},
        {"ESC", VK_ESCAPE},
        {"ESCAPE", VK_ESCAPE},
        {"TAB", VK_TAB},
        {"SPACE", VK_SPACE},
        {"LEFT", VK_LEFT},
        {"RIGHT", VK_RIGHT},
        {"UP", VK_UP},
        {"DOWN", VK_DOWN},
        {"HOME", VK_HOME},
        {"END", VK_END},
        {"PGUP", VK_PRIOR},
        {"PAGEUP", VK_PRIOR},
        {"PGDN", VK_NEXT},
        {"PAGEDOWN", VK_NEXT},
        {"DELETE", VK_DELETE},
        {"BACKSPACE", VK_BACK},
    };
    for (const auto& item : kNamedKeys) {
        if (normalized == item.name) {
            *out_key = item.key;
            return true;
        }
    }
    if (normalized.size() == 2 && normalized[0] == 'F' &&
        std::isdigit(static_cast<unsigned char>(normalized[1]))) {
        *out_key = VK_F1 + (normalized[1] - '1');
        return true;
    }
    if (normalized.size() == 3 && normalized[0] == 'F' &&
        normalized[1] == '1' &&
        std::isdigit(static_cast<unsigned char>(normalized[2]))) {
        *out_key = VK_F10 + (normalized[2] - '0');
        return true;
    }

    if (error) {
        *error = "unsupported key name";
    }
    return false;
}

void PrintHexDump(
    const std::vector<std::uint8_t>& bytes,
    const std::uint32_t base_address) {
    for (std::size_t offset = 0; offset < bytes.size(); offset += 16) {
        std::vector<std::uint8_t> row;
        const std::size_t row_size = std::min<std::size_t>(16, bytes.size() - offset);
        row.insert(row.end(), bytes.begin() + offset, bytes.begin() + offset + row_size);
        std::cout << pal4::inject::FormatHexValue(base_address + static_cast<std::uint32_t>(offset)) << "  ";
        for (std::size_t i = 0; i < 16; ++i) {
            if (i < row.size()) {
                std::vector<std::uint8_t> single{row[i]};
                std::cout << pal4::inject::FormatHexBytes(single) << ' ';
            } else {
                std::cout << "   ";
            }
            if (i == 7) {
                std::cout << ' ';
            }
        }
        std::cout << " |";
        for (const auto byte : row) {
            const char ch = static_cast<char>(byte);
            std::cout << (std::isprint(static_cast<unsigned char>(ch)) ? ch : '.');
        }
        std::cout << "|\n";
    }
}

bool TryParseTrailingTimeout(
    const std::vector<std::string>& args,
    const std::size_t begin,
    std::string* joined_text,
    DWORD* timeout_ms,
    std::string* error) {
    if (begin >= args.size()) {
        if (error) {
            *error = "missing wait target";
        }
        return false;
    }

    DWORD timeout = 5000;
    std::size_t end = args.size();
    if (args.size() - begin >= 2) {
        std::uint32_t parsed_timeout = 0;
        if (pal4::inject::ParseAddressValue(args.back(), &parsed_timeout)) {
            timeout = parsed_timeout;
            end = args.size() - 1;
        }
    }
    if (joined_text) {
        *joined_text = JoinArguments(args, begin, end);
    }
    if (timeout_ms) {
        *timeout_ms = timeout;
    }
    return true;
}

int HandleSnapshot(const CliOptions& options, std::string* error) {
    UiSnapshotTree tree{};
    if (!ReadSnapshotTree(options, &tree, error)) {
        return 1;
    }
    std::cout << pal4::inject::FormatUiSnapshotTreeForDisplay(tree) << "\n";
    return 0;
}

int HandleSnapshotRaw(const CliOptions& options, std::string* error) {
    UiSnapshotTree tree{};
    if (!ReadSnapshotTree(options, &tree, error)) {
        return 1;
    }
    std::cout << pal4::inject::SerializeUiSnapshotTree(tree) << "\n";
    return 0;
}

bool SendSyntheticMouseClick(
    const CliOptions& options,
    const std::uint32_t x,
    const std::uint32_t y,
    const bool bypass_os_queue,
    std::string* error) {
    const std::uint32_t lparam =
        ((y & 0xFFFFu) << 16) | (x & 0xFFFFu);

    const auto send_mouse =
        [&](const std::uint32_t msg, const std::uint32_t wparam) -> bool {
            ProtocolCommand command{};
            command.kind = ProtocolCommandKind::enqueue_ui_message;
            command.ui_message.msg = msg;
            command.ui_message.wparam = wparam;
            command.ui_message.lparam = lparam;
            command.ui_message.bypass_os_queue = bypass_os_queue;
            return ExpectOkResponse(options, command, nullptr, error);
        };

    if (!send_mouse(WM_MOUSEMOVE, 0)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    if (!send_mouse(WM_LBUTTONDOWN, MK_LBUTTON)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    if (!send_mouse(WM_LBUTTONUP, 0)) {
        return false;
    }
    return true;
}

struct WindowSearchState {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK EnumWindowsForPid(HWND hwnd, LPARAM lparam) {
    auto* state = reinterpret_cast<WindowSearchState*>(lparam);
    if (!state || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) {
        return TRUE;
    }

    DWORD window_pid = 0;
    GetWindowThreadProcessId(hwnd, &window_pid);
    if (window_pid == state->pid) {
        state->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

bool TryGetTargetClientSize(
    const CliOptions& options,
    int* out_width,
    int* out_height) {
    if (!out_width || !out_height || options.pid == 0) {
        return false;
    }

    WindowSearchState state{};
    state.pid = options.pid;
    EnumWindows(&EnumWindowsForPid, reinterpret_cast<LPARAM>(&state));
    if (!state.hwnd) {
        return false;
    }

    RECT client_rect{};
    if (!GetClientRect(state.hwnd, &client_rect)) {
        return false;
    }

    const int width = client_rect.right - client_rect.left;
    const int height = client_rect.bottom - client_rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    *out_width = width;
    *out_height = height;
    return true;
}

int HandleClick(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    if (args.size() != 2) {
        *error = "usage: click <ref>";
        return 1;
    }
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::click_ui_ref;
    command.ui_ref = args[1];
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintFieldMap(response);
    return 0;
}

int HandleClickPoint(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    if (args.size() < 3 || args.size() > 4) {
        *error = "usage: click-pt <x> <y> [--os-queue]";
        return 1;
    }

    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if (!pal4::inject::ParseAddressValue(args[1], &x) ||
        !pal4::inject::ParseAddressValue(args[2], &y)) {
        *error = "invalid click coordinates";
        return 1;
    }

    bool bypass_os_queue = true;
    if (args.size() == 4) {
        if (args[3] != "--os-queue") {
            *error = "unexpected argument";
            return 1;
        }
        bypass_os_queue = false;
    }

    if (!SendSyntheticMouseClick(options, x, y, bypass_os_queue, error)) {
        return 1;
    }

    std::cout
        << "clicked=" << x << "," << y
        << " bypass_os_queue=" << (bypass_os_queue ? 1 : 0)
        << "\n";
    return 0;
}

int HandleClickLogicalPoint(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    if (args.size() < 3 || args.size() > 4) {
        *error = "usage: click-logical <x> <y> [--os-queue]";
        return 1;
    }

    const float logical_x = std::strtof(args[1].c_str(), nullptr);
    const float logical_y = std::strtof(args[2].c_str(), nullptr);
    if (!std::isfinite(logical_x) || !std::isfinite(logical_y)) {
        *error = "invalid logical coordinates";
        return 1;
    }

    bool bypass_os_queue = true;
    if (args.size() == 4) {
        if (args[3] != "--os-queue") {
            *error = "unexpected argument";
            return 1;
        }
        bypass_os_queue = false;
    }

    UiSnapshotTree tree{};
    if (!ReadSnapshotTree(options, &tree, error)) {
        return 1;
    }

    const int logical_width = tree.root.rect.right - tree.root.rect.left;
    const int logical_height = tree.root.rect.bottom - tree.root.rect.top;
    if (logical_width <= 0 || logical_height <= 0) {
        *error = "snapshot root rect is invalid";
        return 1;
    }

    int projection_width = logical_width;
    int projection_height = logical_height;
    if (!bypass_os_queue) {
        TryGetTargetClientSize(options, &projection_width, &projection_height);
    }

    const auto plan =
        pal4::inject::BuildCeguiWidescreenPlan(projection_width, projection_height);
    float physical_x = logical_x;
    float physical_y = logical_y;
    if (plan.apply && !plan.use_original_variant) {
        physical_x = pal4::inject::ProjectWidescreenLogicalXToPhysicalPixels(plan, logical_x);
        physical_y = logical_y * plan.uniform_scale;
    }

    const auto click_x = static_cast<std::uint32_t>(std::lround(physical_x));
    const auto click_y = static_cast<std::uint32_t>(std::lround(physical_y));
    if (!SendSyntheticMouseClick(options, click_x, click_y, bypass_os_queue, error)) {
        return 1;
    }

    std::cout
        << "logical=" << logical_x << "," << logical_y
        << " clicked=" << click_x << "," << click_y
        << " logical_client=" << logical_width << "x" << logical_height
        << " projected_client=" << projection_width << "x" << projection_height
        << " scale=" << plan.uniform_scale
        << " bias=" << plan.horizontal_bias_pixels
        << " bypass_os_queue=" << (bypass_os_queue ? 1 : 0)
        << "\n";
    return 0;
}

int HandleFill(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    if (args.size() < 3) {
        *error = "usage: fill <ref> <text>";
        return 1;
    }
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::fill_ui_ref;
    command.ui_ref = args[1];
    command.text = JoinArguments(args, 2, args.size());
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintFieldMap(response);
    return 0;
}

int HandleType(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    if (args.size() < 2) {
        *error = "usage: type <text>";
        return 1;
    }
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::type_text;
    command.text = JoinArguments(args, 1, args.size());
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintFieldMap(response);
    return 0;
}

int HandlePress(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    if (args.size() < 2 || args.size() > 3) {
        *error = "usage: press <key> [--os-queue]";
        return 1;
    }
    std::uint32_t key = 0;
    if (!ParseVirtualKey(args[1], &key, error)) {
        return 1;
    }

    bool bypass_os_queue = true;
    if (args.size() == 3) {
        if (args[2] != "--os-queue") {
            *error = "unexpected argument";
            return 1;
        }
        bypass_os_queue = false;
    }

    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::simulate_key;
    command.virtual_key = key;
    command.key_up = false;
    command.ui_message.bypass_os_queue = bypass_os_queue;
    if (!ExpectOkResponse(options, command, nullptr, error)) {
        return 1;
    }

    command.key_up = true;
    if (!ExpectOkResponse(options, command, nullptr, error)) {
        return 1;
    }

    std::cout << "pressed=" << args[1] << " vk=" << key << "\n";
    return 0;
}

int HandleState(const CliOptions& options, std::string* error) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::read_ui_state;
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintFieldMap(response);
    return 0;
}

int HandleEventLog(const CliOptions& options, std::string* error) {
    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::read_event_log;
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    const auto it = response.fields.find("event_log_tail");
    if (it != response.fields.end()) {
        std::cout << it->second << "\n";
    }
    return 0;
}

int HandleWaitPath(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    std::string target_path;
    DWORD timeout_ms = 0;
    if (!TryParseTrailingTimeout(args, 1, &target_path, &timeout_ms, error)) {
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        UiSnapshotTree tree{};
        std::string snapshot_error;
        if (ReadSnapshotTree(options, &tree, &snapshot_error)) {
            if (pal4::inject::FindUiSnapshotNodeByPath(tree, target_path)) {
                std::cout << "path=" << target_path << "\n";
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    *error = "timed out waiting for path";
    return 1;
}

int HandleWaitText(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    std::string target_text;
    DWORD timeout_ms = 0;
    if (!TryParseTrailingTimeout(args, 1, &target_text, &timeout_ms, error)) {
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        UiSnapshotTree tree{};
        std::string snapshot_error;
        if (ReadSnapshotTree(options, &tree, &snapshot_error)) {
            if (pal4::inject::UiSnapshotTreeContainsText(tree, target_text)) {
                std::cout << "text=" << target_text << "\n";
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    *error = "timed out waiting for text";
    return 1;
}

int HandleMemQuery(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    AddressSpace address_space = AddressSpace::runtime_va;
    std::uint32_t address = 0;
    if (!ParseAddressSelector(args, 1, &address_space, &address, nullptr, error)) {
        return 1;
    }

    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::query_memory;
    command.address_space = address_space;
    command.address = address;
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintMemoryRegionFields(response);
    return 0;
}

int HandleMemRead(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    AddressSpace address_space = AddressSpace::runtime_va;
    std::uint32_t address = 0;
    std::size_t next_index = 0;
    if (!ParseAddressSelector(args, 1, &address_space, &address, &next_index, error)) {
        return 1;
    }
    if (next_index + 2 != args.size() || args[next_index] != "--size") {
        *error = "usage: mem-read (--ida|--va) <addr> --size <n>";
        return 1;
    }
    std::uint32_t size = 0;
    if (!pal4::inject::ParseAddressValue(args[next_index + 1], &size)) {
        *error = "invalid size";
        return 1;
    }

    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::read_memory;
    command.address_space = address_space;
    command.address = address;
    command.size = size;
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintMemoryRegionFields(response);

    std::vector<std::uint8_t> bytes;
    const auto bytes_it = response.fields.find("bytes");
    if (bytes_it == response.fields.end()) {
        *error = "read_memory response is missing the bytes field";
        return 1;
    }
    if (!pal4::inject::ParseHexBytes(bytes_it->second, &bytes, error)) {
        return 1;
    }
    const auto resolved_it = response.fields.find("resolved_address");
    std::uint32_t base_address = 0;
    if (resolved_it != response.fields.end()) {
        pal4::inject::ParseAddressValue(resolved_it->second, &base_address);
    }
    std::cout << "bytes=" << bytes_it->second << "\n";
    PrintHexDump(bytes, base_address);
    return 0;
}

int HandleMemReadScalar(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    AddressSpace address_space = AddressSpace::runtime_va;
    std::uint32_t address = 0;
    std::size_t next_index = 0;
    if (!ParseAddressSelector(args, 1, &address_space, &address, &next_index, error)) {
        return 1;
    }
    if (next_index + 2 != args.size() || args[next_index] != "--type") {
        *error = "usage: mem-read-scalar (--ida|--va) <addr> --type <type>";
        return 1;
    }
    MemoryScalarType scalar_type = MemoryScalarType::u32;
    if (!pal4::inject::TryParseMemoryScalarType(args[next_index + 1], &scalar_type)) {
        *error = "invalid scalar type";
        return 1;
    }

    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::read_memory;
    command.address_space = address_space;
    command.address = address;
    command.size = static_cast<std::uint32_t>(pal4::inject::SizeOfMemoryScalarType(scalar_type));
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }

    std::vector<std::uint8_t> bytes;
    const auto bytes_it = response.fields.find("bytes");
    if (bytes_it == response.fields.end() ||
        !pal4::inject::ParseHexBytes(bytes_it->second, &bytes, error)) {
        return 1;
    }

    std::string decoded;
    if (!pal4::inject::DecodeScalarValue(scalar_type, bytes, &decoded, error)) {
        return 1;
    }

    PrintMemoryRegionFields(response);
    std::cout << "type=" << pal4::inject::ToString(scalar_type) << "\n";
    std::cout << "value=" << decoded << "\n";
    std::cout << "bytes=" << bytes_it->second << "\n";
    return 0;
}

int HandleMemWriteBytes(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    AddressSpace address_space = AddressSpace::runtime_va;
    std::uint32_t address = 0;
    std::size_t next_index = 0;
    if (!ParseAddressSelector(args, 1, &address_space, &address, &next_index, error)) {
        return 1;
    }
    if (next_index >= args.size()) {
        *error = "usage: mem-write-bytes (--ida|--va) <addr> <hex> [--unsafe-code-write]";
        return 1;
    }

    bool unsafe_code_write = false;
    std::string hex_bytes = args[next_index++];
    if (next_index < args.size()) {
        if (args[next_index] != "--unsafe-code-write" || next_index + 1 != args.size()) {
            *error = "unexpected extra arguments";
            return 1;
        }
        unsafe_code_write = true;
    }

    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::write_memory;
    command.address_space = address_space;
    command.address = address;
    command.hex_bytes = hex_bytes;
    command.unsafe_code_write = unsafe_code_write;
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintMemoryRegionFields(response);
    std::cout << "bytes=" << response.fields["bytes"] << "\n";
    std::cout << "before_bytes=" << response.fields["before_bytes"] << "\n";
    std::cout << "after_bytes=" << response.fields["after_bytes"] << "\n";
    return 0;
}

int HandleMemWriteScalar(
    const CliOptions& options,
    const std::vector<std::string>& args,
    std::string* error) {
    AddressSpace address_space = AddressSpace::runtime_va;
    std::uint32_t address = 0;
    std::size_t next_index = 0;
    if (!ParseAddressSelector(args, 1, &address_space, &address, &next_index, error)) {
        return 1;
    }
    if (next_index + 2 >= args.size() || args[next_index] != "--type") {
        *error = "usage: mem-write-scalar (--ida|--va) <addr> --type <type> <value> [--unsafe-code-write]";
        return 1;
    }

    MemoryScalarType scalar_type = MemoryScalarType::u32;
    if (!pal4::inject::TryParseMemoryScalarType(args[next_index + 1], &scalar_type)) {
        *error = "invalid scalar type";
        return 1;
    }
    next_index += 2;

    bool unsafe_code_write = false;
    std::size_t value_end = args.size();
    if (args.back() == "--unsafe-code-write") {
        unsafe_code_write = true;
        value_end = args.size() - 1;
    }
    if (next_index >= value_end) {
        *error = "missing scalar value";
        return 1;
    }

    const std::string value_text = JoinArguments(args, next_index, value_end);
    std::vector<std::uint8_t> bytes;
    if (!pal4::inject::EncodeScalarValue(scalar_type, value_text, &bytes, error)) {
        return 1;
    }

    ProtocolCommand command{};
    command.kind = ProtocolCommandKind::write_memory;
    command.address_space = address_space;
    command.address = address;
    command.hex_bytes = pal4::inject::FormatHexBytes(bytes);
    command.unsafe_code_write = unsafe_code_write;
    ProtocolResponse response{};
    if (!ExpectOkResponse(options, command, &response, error)) {
        return 1;
    }
    PrintMemoryRegionFields(response);
    std::cout << "type=" << pal4::inject::ToString(scalar_type) << "\n";
    std::cout << "bytes=" << response.fields["bytes"] << "\n";
    std::cout << "before_bytes=" << response.fields["before_bytes"] << "\n";
    std::cout << "after_bytes=" << response.fields["after_bytes"] << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    CliOptions options{};
    std::string error;
    if (!ParseCliOptions(argc, argv, &options, &error)) {
        std::cerr << error << "\n";
        PrintUsage();
        return 1;
    }

    const std::string& command = options.args.front();
    int exit_code = 1;
    if (command == "snapshot") {
        exit_code = HandleSnapshot(options, &error);
    } else if (command == "snapshot-raw") {
        exit_code = HandleSnapshotRaw(options, &error);
    } else if (command == "click") {
        exit_code = HandleClick(options, options.args, &error);
    } else if (command == "click-pt") {
        exit_code = HandleClickPoint(options, options.args, &error);
    } else if (command == "click-logical") {
        exit_code = HandleClickLogicalPoint(options, options.args, &error);
    } else if (command == "fill") {
        exit_code = HandleFill(options, options.args, &error);
    } else if (command == "type") {
        exit_code = HandleType(options, options.args, &error);
    } else if (command == "press") {
        exit_code = HandlePress(options, options.args, &error);
    } else if (command == "state") {
        exit_code = HandleState(options, &error);
    } else if (command == "event-log") {
        exit_code = HandleEventLog(options, &error);
    } else if (command == "wait-path") {
        exit_code = HandleWaitPath(options, options.args, &error);
    } else if (command == "wait-text") {
        exit_code = HandleWaitText(options, options.args, &error);
    } else if (command == "mem-query") {
        exit_code = HandleMemQuery(options, options.args, &error);
    } else if (command == "mem-read") {
        exit_code = HandleMemRead(options, options.args, &error);
    } else if (command == "mem-read-scalar") {
        exit_code = HandleMemReadScalar(options, options.args, &error);
    } else if (command == "mem-write-bytes") {
        exit_code = HandleMemWriteBytes(options, options.args, &error);
    } else if (command == "mem-write-scalar") {
        exit_code = HandleMemWriteScalar(options, options.args, &error);
    } else {
        std::cerr << "unknown command: " << command << "\n";
        PrintUsage();
        return 1;
    }

    if (exit_code != 0 && !error.empty()) {
        std::cerr << error << "\n";
    }
    return exit_code;
}

#include "loose_file_hooks.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/loose_file_overlay.h"
#include "loose_file_load_log.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using OpenPackageResourceFileFn = int (__thiscall*)(void*, const char*);
using TextScriptInitializeFn = bool (__thiscall*)(void*, const char*, int);

constexpr std::ptrdiff_t kPackageOffsetInResourceManager = 4;
constexpr std::ptrdiff_t kPackageModeOffset = 0x4;
constexpr std::ptrdiff_t kPackageHandleSlotsOffset = 0x100088;
constexpr std::ptrdiff_t kPackageOpenHandleCountOffset = 0x1001B8;
constexpr std::size_t kPackageHandleSlotCount = 8;
constexpr std::uint32_t kPackageOpenHandleLimit = 7;
constexpr std::uint32_t kMemoryMappedPackageMode = 2;

#pragma pack(push, 4)
struct PackageFileHandleLayout {
    std::uint8_t active = 0;
    std::uint8_t active_padding[3]{};
    std::uint32_t name_hash = 0;
    std::uint32_t flags = 0;
    std::int32_t file_index = -1;
    std::uint32_t mapped_view = 0;
    std::uint32_t raw_data = 0;
    std::uint32_t mapped_offset = 0;
    std::uint8_t compressed = 0;
    std::uint8_t compressed_padding[3]{};
    std::uint32_t data = 0;
    std::uint32_t size = 0;
    std::uint32_t cursor = 0;
    std::uint32_t package_entry = 0;
};
#pragma pack(pop)

static_assert(sizeof(PackageFileHandleLayout) == 48);
static_assert(sizeof(void*) == sizeof(std::uint32_t));
static_assert(offsetof(PackageFileHandleLayout, mapped_view) == 16);
static_assert(offsetof(PackageFileHandleLayout, data) == 32);
static_assert(offsetof(PackageFileHandleLayout, size) == 36);
static_assert(offsetof(PackageFileHandleLayout, cursor) == 40);

OpenPackageResourceFileFn g_original_open_package_resource_file = nullptr;
TextScriptInitializeFn g_original_text_script_initialize = nullptr;
std::mutex g_loose_file_open_mutex;

std::string FormatWindowsError(const DWORD code) {
    char* buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string text;
    if (size && buffer) {
        text.assign(buffer, size);
        LocalFree(buffer);
    } else {
        text = "Windows error " + std::to_string(code);
    }
    while (!text.empty() &&
           (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

std::filesystem::path MainExecutableDirectory() {
    static const std::filesystem::path directory = [] {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
    }();
    return directory;
}

int CallOriginalPackage(void* self, const char* resource_path) {
    if (!g_original_open_package_resource_file) {
        auto& state = GetRuntimeState();
        const std::string error = "loose file hook original trampoline is null";
        state.SetHookError(HookId::loose_file_overlay, error);
        state.SetLastError(error);
        return 0;
    }
    return g_original_open_package_resource_file(self, resource_path);
}

bool CallOriginalTextScript(
    void* self,
    const char* file_name,
    const int unused_arg) {
    if (!g_original_text_script_initialize) {
        auto& state = GetRuntimeState();
        const std::string error =
            "loose text script hook original trampoline is null";
        state.SetHookError(HookId::loose_text_script_overlay, error);
        state.SetHookError(HookId::loose_file_overlay, error);
        state.SetLastError(error);
        return false;
    }
    return g_original_text_script_initialize(self, file_name, unused_arg);
}

bool TryPathToAnsi(
    const std::filesystem::path& path,
    std::string* const result,
    std::string* const error) noexcept {
    if (!result) {
        return false;
    }
    try {
        *result = path.string();
        if (!result->empty()) {
            return true;
        }
        if (error) {
            *error = "converted gamepatch path is empty";
        }
    } catch (const std::exception& exception) {
        if (error) {
            *error = std::string("cannot convert gamepatch path for CRT fopen: ") +
                exception.what();
        }
    } catch (...) {
        if (error) {
            *error = "cannot convert gamepatch path for CRT fopen";
        }
    }
    return false;
}

std::string PathForRuntimeLog(const std::filesystem::path& path) noexcept {
    try {
        return path.string();
    } catch (...) {
        return "<non-ANSI gamepatch path>";
    }
}

PackageFileHandleLayout* FindFreePackageHandleSlot(std::byte* package) {
    auto** slots = reinterpret_cast<PackageFileHandleLayout**>(
        package + kPackageHandleSlotsOffset);
    for (std::size_t i = 0; i < kPackageHandleSlotCount; ++i) {
        if (slots[i] && slots[i]->active == 0) {
            return slots[i];
        }
    }
    return nullptr;
}

PackageFileHandleLayout* OpenLooseFileInPackageHandlePool(
    void* resource_manager,
    const std::filesystem::path& path,
    std::string* error) {
    if (!resource_manager) {
        if (error) {
            *error = "resource manager is null";
        }
        return nullptr;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = "CreateFileW failed: " + FormatWindowsError(GetLastError());
        }
        return nullptr;
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file, &file_size)) {
        const DWORD code = GetLastError();
        CloseHandle(file);
        if (error) {
            *error = "GetFileSizeEx failed: " + FormatWindowsError(code);
        }
        return nullptr;
    }
    if (file_size.QuadPart <= 0 ||
        static_cast<unsigned long long>(file_size.QuadPart) >
            std::numeric_limits<std::uint32_t>::max()) {
        CloseHandle(file);
        if (error) {
            *error = file_size.QuadPart == 0
                ? "loose file is empty"
                : "loose file size is invalid or exceeds 4 GiB";
        }
        return nullptr;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const DWORD mapping_error = GetLastError();
    CloseHandle(file);
    if (!mapping) {
        if (error) {
            *error = "CreateFileMappingW failed: " + FormatWindowsError(mapping_error);
        }
        return nullptr;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    const DWORD view_error = GetLastError();
    CloseHandle(mapping);
    if (!view) {
        if (error) {
            *error = "MapViewOfFile failed: " + FormatWindowsError(view_error);
        }
        return nullptr;
    }

    std::scoped_lock lock(g_loose_file_open_mutex);
    auto* package = static_cast<std::byte*>(resource_manager) +
        kPackageOffsetInResourceManager;
    const auto package_mode = *reinterpret_cast<const std::uint32_t*>(
        package + kPackageModeOffset);
    auto* open_count = reinterpret_cast<std::uint32_t*>(
        package + kPackageOpenHandleCountOffset);
    if (package_mode != kMemoryMappedPackageMode) {
        UnmapViewOfFile(view);
        if (error) {
            *error = "package is not in memory-mapped mode";
        }
        return nullptr;
    }
    if (*open_count >= kPackageOpenHandleLimit) {
        UnmapViewOfFile(view);
        if (error) {
            *error = "package handle pool reached the original seven-file limit";
        }
        return nullptr;
    }

    auto* slot = FindFreePackageHandleSlot(package);
    if (!slot) {
        UnmapViewOfFile(view);
        if (error) {
            *error = "package handle pool has no free slot";
        }
        return nullptr;
    }

    std::memset(slot, 0, sizeof(*slot));
    const auto view_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(view));
    slot->active = 1;
    slot->file_index = -1;
    slot->mapped_view = view_address;
    slot->raw_data = view_address;
    slot->data = view_address;
    slot->size = static_cast<std::uint32_t>(file_size.QuadPart);
    ++*open_count;
    return slot;
}

int __fastcall Hook_OpenPackageResourceFile(
    void* self,
    void*,
    const char* resource_path) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::loose_file_overlay);

    const HookMode mode = state.GetHookMode(HookId::loose_file_overlay);
    const auto game_root = MainExecutableDirectory();
    const std::string_view safe_resource_path = resource_path
        ? std::string_view(resource_path)
        : std::string_view{};
    auto candidate = resource_path
        ? FindExistingLooseFile(game_root, resource_path)
        : std::nullopt;
    std::optional<std::filesystem::path> rejected_candidate_path;
    std::string candidate_rejection_reason;

    if (IsLooseFileOverlayActiveMode(mode) && candidate &&
        !ValidateLoosePackageFile(
            safe_resource_path,
            candidate->path,
            &candidate_rejection_reason)) {
        rejected_candidate_path = candidate->path;
        state.AppendEventLog(
            "loose_file_candidate_rejected resource=" +
            std::string(safe_resource_path) +
            " file=" + PathForRuntimeLog(candidate->path) +
            " reason=" + candidate_rejection_reason);
        candidate.reset();
    }

    if (!IsLooseFileOverlayActiveMode(mode)) {
        const int original_result = CallOriginalPackage(self, resource_path);
        LooseFileLoadLogEntry entry{};
        entry.event = mode == HookMode::observe_only
            ? "overlay_disabled"
            : "mirror_compare";
        entry.loader = "package";
        entry.mode = mode;
        entry.resource_path = safe_resource_path;
        entry.reason = candidate ? "candidate_ignored" : "feature_not_replacing";
        if (candidate) {
            entry.file_path = candidate->path;
        }
        entry.fallback_source = "cpk";
        entry.fallback_result_known = true;
        entry.fallback_opened = original_result != 0;
        entry.fallback_used = true;
        AppendLooseFileLoadLog(game_root, entry);
        return original_result;
    }
    if (!candidate) {
        const int original_result = CallOriginalPackage(self, resource_path);
        LooseFileLoadLogEntry entry{};
        entry.event = "cpk_fallback";
        entry.loader = "package";
        entry.mode = mode;
        entry.resource_path = safe_resource_path;
        entry.reason = candidate_rejection_reason.empty()
            ? "loose_file_not_found_or_unsupported_path"
            : candidate_rejection_reason;
        if (rejected_candidate_path) {
            entry.file_path = *rejected_candidate_path;
        }
        entry.fallback_source = "cpk";
        entry.fallback_result_known = true;
        entry.fallback_opened = original_result != 0;
        entry.fallback_used = true;
        AppendLooseFileLoadLog(game_root, entry);
        return original_result;
    }

    std::string error;
    auto* handle = OpenLooseFileInPackageHandlePool(self, candidate->path, &error);
    if (handle) {
        state.ClearHookError(HookId::loose_file_overlay);
        const auto display_path = PathForRuntimeLog(candidate->path);
        std::ostringstream log;
        log << "loose_file_override resource=" << resource_path
            << " file=" << display_path
            << " size=" << handle->size;
        state.AppendEventLog(log.str());

        LooseFileLoadLogEntry entry{};
        entry.event = "override";
        entry.loader = "package";
        entry.mode = mode;
        entry.resource_path = safe_resource_path;
        entry.file_path = candidate->path;
        entry.size = handle->size;
        entry.has_size = true;
        AppendLooseFileLoadLog(game_root, entry);
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(handle));
    }

    const std::string full_error =
        "loose file override failed for " + PathForRuntimeLog(candidate->path) +
        ": " + error;
    state.SetHookError(HookId::loose_file_overlay, full_error);
    state.SetLastError(full_error);
    state.AppendEventLog("loose_file_override_error=" + full_error);

    const bool fallback_to_cpk = mode != HookMode::replace_strict;
    const int original_result = fallback_to_cpk
        ? CallOriginalPackage(self, resource_path)
        : 0;
    LooseFileLoadLogEntry entry{};
    entry.event = "override_error";
    entry.loader = "package";
    entry.mode = mode;
    entry.resource_path = safe_resource_path;
    entry.file_path = candidate->path;
    entry.reason = error;
    entry.fallback_source = "cpk";
    entry.fallback_result_known = fallback_to_cpk;
    entry.fallback_opened = original_result != 0;
    entry.fallback_used = fallback_to_cpk;
    AppendLooseFileLoadLog(game_root, entry);
    return original_result;
}

bool __fastcall Hook_TextScriptInitialize(
    void* self,
    void*,
    const char* file_name,
    const int unused_arg) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::loose_text_script_overlay);
    state.IncrementHookCall(HookId::loose_file_overlay);

    const HookMode mode = state.GetHookMode(HookId::loose_file_overlay);
    const auto game_root = MainExecutableDirectory();
    const std::string_view safe_file_name = file_name
        ? std::string_view(file_name)
        : std::string_view{};
    const auto candidate = file_name
        ? FindExistingLooseTextScriptFile(game_root, file_name)
        : std::nullopt;

    if (!IsLooseFileOverlayActiveMode(mode)) {
        state.ClearHookError(HookId::loose_text_script_overlay);
        state.ClearHookError(HookId::loose_file_overlay);
        const bool original_result =
            CallOriginalTextScript(self, file_name, unused_arg);

        LooseFileLoadLogEntry entry{};
        entry.event = mode == HookMode::observe_only
            ? "overlay_disabled"
            : "mirror_compare";
        entry.loader = "text_script";
        entry.mode = mode;
        entry.resource_path = safe_file_name;
        entry.reason = candidate ? "candidate_ignored" : "feature_not_replacing";
        if (candidate) {
            entry.file_path = candidate->path;
        }
        entry.fallback_source = "game_directory";
        entry.fallback_result_known = true;
        entry.fallback_opened = original_result;
        entry.fallback_used = true;
        AppendLooseFileLoadLog(game_root, entry);
        return original_result;
    }

    if (!candidate) {
        const std::string error =
            "required gamepatch text script is missing: " +
            std::string(safe_file_name);
        state.SetHookError(HookId::loose_text_script_overlay, error);
        state.SetHookError(HookId::loose_file_overlay, error);
        state.SetLastError(error);
        state.AppendEventLog("loose_text_script_missing=" + error);

        LooseFileLoadLogEntry entry{};
        entry.event = "gamepatch_required_missing";
        entry.loader = "text_script";
        entry.mode = mode;
        entry.resource_path = safe_file_name;
        entry.reason = "gamepatch_file_required";
        const auto expected = BuildLooseTextScriptCandidates(game_root, safe_file_name);
        if (!expected.empty()) {
            entry.file_path = expected.front().path;
        }
        AppendLooseFileLoadLog(game_root, entry);
        return false;
    }

    std::string candidate_file_name;
    std::string error;
    bool loose_result = false;
    if (TryPathToAnsi(candidate->path, &candidate_file_name, &error)) {
        loose_result = CallOriginalTextScript(
            self,
            candidate_file_name.c_str(),
            unused_arg);
        if (!loose_result) {
            error = "text script loader rejected the gamepatch file";
        }
    }

    if (loose_result) {
        state.ClearHookError(HookId::loose_text_script_overlay);
        state.ClearHookError(HookId::loose_file_overlay);
        state.AppendEventLog(
            "loose_text_script_override resource=" + std::string(safe_file_name) +
            " file=" + candidate_file_name);

        LooseFileLoadLogEntry entry{};
        entry.event = "override";
        entry.loader = "text_script";
        entry.mode = mode;
        entry.resource_path = safe_file_name;
        entry.file_path = candidate->path;
        std::error_code size_error;
        entry.size = std::filesystem::file_size(candidate->path, size_error);
        entry.has_size = !size_error;
        AppendLooseFileLoadLog(game_root, entry);
        return true;
    }

    const std::string display_file_name = candidate_file_name.empty()
        ? "<unrepresentable gamepatch path>"
        : candidate_file_name;
    const std::string full_error =
        "loose text script override failed for " + display_file_name +
        ": " + error;
    state.SetHookError(HookId::loose_text_script_overlay, full_error);
    state.SetHookError(HookId::loose_file_overlay, full_error);
    state.SetLastError(full_error);
    state.AppendEventLog("loose_text_script_override_error=" + full_error);

    LooseFileLoadLogEntry entry{};
    entry.event = "override_error";
    entry.loader = "text_script";
    entry.mode = mode;
    entry.resource_path = safe_file_name;
    entry.file_path = candidate->path;
    entry.reason = error;
    AppendLooseFileLoadLog(game_root, entry);
    return false;
}

}  // namespace

void* GetLooseFileReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::loose_file_overlay:
        return reinterpret_cast<void*>(&Hook_OpenPackageResourceFile);
    case HookId::loose_text_script_overlay:
        return reinterpret_cast<void*>(&Hook_TextScriptInitialize);
    default:
        return nullptr;
    }
}

void SetLooseFileOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::loose_file_overlay:
        g_original_open_package_resource_file =
            reinterpret_cast<OpenPackageResourceFileFn>(trampoline);
        break;
    case HookId::loose_text_script_overlay:
        g_original_text_script_initialize =
            reinterpret_cast<TextScriptInitializeFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

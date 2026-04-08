#include "camera_unlock.h"

#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/camera_pitch_guard.h"
#include "pal4inject/camera_unlock_patch.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
const float g_camera_pitch_limit_negative = -kCameraPitchLimitDegrees;
const float g_camera_pitch_limit_positive = kCameraPitchLimitDegrees;

namespace {

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
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

bool ApplyAbsoluteOperandPatch(
    const std::uintptr_t module_base,
    const CameraAngleLimitPatch& patch,
    std::string* error) {
    auto* target = reinterpret_cast<unsigned char*>(
        ida::ResolveRuntimeAddress(module_base, patch.instruction_ea));
    if (!target) {
        if (error) {
            *error = "camera unlock patch target is null";
        }
        return false;
    }

    if (target[0] != patch.expected_opcode_bytes[0] ||
        target[1] != patch.expected_opcode_bytes[1]) {
        if (error) {
            *error = "camera unlock opcode mismatch at instruction";
        }
        return false;
    }

    const auto current_operand = *reinterpret_cast<std::uint32_t*>(target + 2);
    if (current_operand != patch.expected_operand_ea) {
        if (error) {
            *error = "camera unlock operand mismatch at instruction";
        }
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(target, 6, PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (error) {
            *error = "VirtualProtect failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    *reinterpret_cast<std::uint32_t*>(target + 2) =
        static_cast<std::uint32_t>(patch.replacement_operand);
    FlushInstructionCache(GetCurrentProcess(), target, 6);

    DWORD discard = 0;
    VirtualProtect(target, 6, old_protect, &discard);
    return true;
}

bool ApplyByteDisplacementPatch(
    const std::uintptr_t module_base,
    const CameraInputScalePatch& patch,
    std::string* error) {
    auto* target = reinterpret_cast<unsigned char*>(
        ida::ResolveRuntimeAddress(module_base, patch.instruction_ea));
    if (!target) {
        if (error) {
            *error = "camera scale patch target is null";
        }
        return false;
    }

    if (std::memcmp(target, patch.expected_bytes.data(), patch.expected_bytes.size()) != 0) {
        if (error) {
            *error = "camera scale patch bytes mismatch";
        }
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(target, patch.expected_bytes.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (error) {
            *error = "VirtualProtect failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    target[2] = patch.replacement_displacement;
    FlushInstructionCache(GetCurrentProcess(), target, patch.expected_bytes.size());

    DWORD discard = 0;
    VirtualProtect(target, patch.expected_bytes.size(), old_protect, &discard);
    return true;
}

}  // namespace

bool ApplyCameraPitchUnlockPatch(std::string* error) {
    auto& state = GetRuntimeState();
    const std::uintptr_t module_base = state.MainModuleBase();
    if (module_base == 0) {
        if (error) {
            *error = "main module base is not initialized";
        }
        return false;
    }

    for (const auto& patch : BuildCameraPitchUnlockPatches()) {
        if (!ApplyAbsoluteOperandPatch(module_base, patch, error)) {
            return false;
        }
    }

    for (const auto& patch : BuildCameraInputScalePatches()) {
        if (!ApplyByteDisplacementPatch(module_base, patch, error)) {
            return false;
        }
    }

    state.AppendEventLog("camera_pitch_unlock_patch=1 range=+/-89 vertical_scale=horizontal");
    return true;
}

}  // namespace pal4::inject

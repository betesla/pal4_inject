#include "mssrsx_abort_hook.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace pal4::inject {
namespace {

constexpr std::uintptr_t kAbortRva = 0x2BB0D;
constexpr std::size_t kPatchSize = 7;
constexpr std::array<std::uint8_t, kPatchSize> kExpectedPrologue{
    0x6A, 0x0A, 0xE8, 0x15, 0x95, 0xFF, 0xFF};
constexpr std::uintptr_t kTerminateRva = 0x25C2C;
constexpr std::size_t kTerminatePatchSize = 5;
constexpr std::array<std::uint8_t, kTerminatePatchSize> kExpectedTerminatePrologue{
    0x55, 0x8B, 0xEC, 0x6A, 0xFF};

void* g_abort_target = nullptr;
void* g_abort_trampoline = nullptr;
void* g_terminate_target = nullptr;
void* g_terminate_trampoline = nullptr;

bool WriteRelativeBranch(
    void* const source,
    void* const destination,
    const std::uint8_t opcode,
    std::string* error) {
    const auto delta = reinterpret_cast<std::intptr_t>(destination) -
        (reinterpret_cast<std::intptr_t>(source) + 5);
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max()) {
        if (error) {
            *error = "mssrsx abort detour branch is outside rel32 range";
        }
        return false;
    }
    auto* bytes = static_cast<std::uint8_t*>(source);
    bytes[0] = opcode;
    *reinterpret_cast<std::int32_t*>(bytes + 1) =
        static_cast<std::int32_t>(delta);
    return true;
}

}  // namespace

bool InstallMssrsxTerminateHook(
    const HMODULE module,
    void* const replacement,
    void** const original_trampoline,
    std::string* error) {
    if (!module || !replacement || !original_trampoline) {
        if (error) {
            *error = "mssrsx terminate hook arguments are null";
        }
        return false;
    }
    if (g_terminate_trampoline) {
        *original_trampoline = g_terminate_trampoline;
        if (error) {
            error->clear();
        }
        return true;
    }

    g_terminate_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(module) + kTerminateRva);
    if (std::memcmp(
            g_terminate_target,
            kExpectedTerminatePrologue.data(),
            kExpectedTerminatePrologue.size()) != 0) {
        if (error) {
            *error = "unexpected mssrsx terminate() prologue";
        }
        return false;
    }

    g_terminate_trampoline = VirtualAlloc(
        nullptr,
        kTerminatePatchSize + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!g_terminate_trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for mssrsx terminate trampoline";
        }
        return false;
    }

    std::memcpy(
        g_terminate_trampoline,
        g_terminate_target,
        kTerminatePatchSize);
    if (!WriteRelativeBranch(
            static_cast<std::uint8_t*>(g_terminate_trampoline) +
                kTerminatePatchSize,
            static_cast<std::uint8_t*>(g_terminate_target) +
                kTerminatePatchSize,
            0xE9,
            error)) {
        VirtualFree(g_terminate_trampoline, 0, MEM_RELEASE);
        g_terminate_trampoline = nullptr;
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(
            g_terminate_target,
            kTerminatePatchSize,
            PAGE_EXECUTE_READWRITE,
            &old_protect)) {
        VirtualFree(g_terminate_trampoline, 0, MEM_RELEASE);
        g_terminate_trampoline = nullptr;
        if (error) {
            *error = "VirtualProtect failed for mssrsx terminate()";
        }
        return false;
    }
    const bool jump_written = WriteRelativeBranch(
        g_terminate_target,
        replacement,
        0xE9,
        error);
    if (jump_written) {
        FlushInstructionCache(
            GetCurrentProcess(),
            g_terminate_target,
            kTerminatePatchSize);
    }
    DWORD discard = 0;
    VirtualProtect(
        g_terminate_target,
        kTerminatePatchSize,
        old_protect,
        &discard);
    if (!jump_written) {
        VirtualFree(g_terminate_trampoline, 0, MEM_RELEASE);
        g_terminate_trampoline = nullptr;
        return false;
    }

    *original_trampoline = g_terminate_trampoline;
    if (error) {
        error->clear();
    }
    return true;
}

bool InstallMssrsxAbortHook(
    const HMODULE module,
    void* const replacement,
    void** const original_trampoline,
    std::string* error) {
    if (!module || !replacement || !original_trampoline) {
        if (error) {
            *error = "mssrsx abort hook arguments are null";
        }
        return false;
    }
    if (g_abort_trampoline) {
        *original_trampoline = g_abort_trampoline;
        if (error) {
            error->clear();
        }
        return true;
    }

    g_abort_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(module) + kAbortRva);
    if (std::memcmp(
            g_abort_target,
            kExpectedPrologue.data(),
            kExpectedPrologue.size()) != 0) {
        if (error) {
            *error = "unexpected mssrsx abort() prologue";
        }
        return false;
    }

    g_abort_trampoline = VirtualAlloc(
        nullptr,
        kPatchSize + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!g_abort_trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for mssrsx abort trampoline";
        }
        return false;
    }

    auto* target_bytes = static_cast<std::uint8_t*>(g_abort_target);
    auto* trampoline_bytes = static_cast<std::uint8_t*>(g_abort_trampoline);
    std::memcpy(trampoline_bytes, target_bytes, 2);
    const auto original_call_displacement =
        *reinterpret_cast<const std::int32_t*>(target_bytes + 3);
    void* const original_call_target =
        target_bytes + kPatchSize + original_call_displacement;
    if (!WriteRelativeBranch(
            trampoline_bytes + 2,
            original_call_target,
            0xE8,
            error) ||
        !WriteRelativeBranch(
            trampoline_bytes + kPatchSize,
            target_bytes + kPatchSize,
            0xE9,
            error)) {
        VirtualFree(g_abort_trampoline, 0, MEM_RELEASE);
        g_abort_trampoline = nullptr;
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(
            g_abort_target,
            kPatchSize,
            PAGE_EXECUTE_READWRITE,
            &old_protect)) {
        VirtualFree(g_abort_trampoline, 0, MEM_RELEASE);
        g_abort_trampoline = nullptr;
        if (error) {
            *error = "VirtualProtect failed for mssrsx abort()";
        }
        return false;
    }
    const bool jump_written = WriteRelativeBranch(
        g_abort_target,
        replacement,
        0xE9,
        error);
    if (jump_written) {
        target_bytes[5] = 0x90;
        target_bytes[6] = 0x90;
        FlushInstructionCache(GetCurrentProcess(), g_abort_target, kPatchSize);
    }
    DWORD discard = 0;
    VirtualProtect(g_abort_target, kPatchSize, old_protect, &discard);
    if (!jump_written) {
        VirtualFree(g_abort_trampoline, 0, MEM_RELEASE);
        g_abort_trampoline = nullptr;
        return false;
    }

    *original_trampoline = g_abort_trampoline;
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

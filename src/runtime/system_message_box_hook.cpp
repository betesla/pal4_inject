#include "system_message_box_hook.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {
namespace {

constexpr std::size_t kPatchSize = 5;
constexpr std::array<std::uint8_t, kPatchSize> kExpectedPrologue{
    0x8B, 0xFF, 0x55, 0x8B, 0xEC};

void* g_target = nullptr;
void* g_trampoline = nullptr;

bool WriteRelativeJump(void* source, void* destination, std::string* error) {
    const auto delta = reinterpret_cast<std::intptr_t>(destination) -
        (reinterpret_cast<std::intptr_t>(source) +
         static_cast<std::intptr_t>(kPatchSize));
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max()) {
        if (error) {
            *error = "MessageBoxA detour target is outside rel32 range";
        }
        return false;
    }

    auto* bytes = static_cast<std::uint8_t*>(source);
    bytes[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(bytes + 1) =
        static_cast<std::int32_t>(delta);
    return true;
}

}  // namespace

bool InstallSystemMessageBoxHook(
    void* const replacement,
    void** const original_trampoline,
    std::string* error) {
    if (!replacement || !original_trampoline) {
        if (error) {
            *error = "MessageBoxA hook arguments are null";
        }
        return false;
    }
    if (g_trampoline) {
        *original_trampoline = g_trampoline;
        if (error) {
            error->clear();
        }
        return true;
    }

    const HMODULE user32 = GetModuleHandleA("user32.dll");
    g_target = user32
        ? reinterpret_cast<void*>(GetProcAddress(user32, "MessageBoxA"))
        : nullptr;
    if (!g_target) {
        if (error) {
            *error = "could not resolve user32!MessageBoxA";
        }
        return false;
    }
    if (std::memcmp(
            g_target,
            kExpectedPrologue.data(),
            kExpectedPrologue.size()) != 0) {
        if (error) {
            *error = "unexpected user32!MessageBoxA prologue";
        }
        return false;
    }

    g_trampoline = VirtualAlloc(
        nullptr,
        kPatchSize * 2,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!g_trampoline) {
        if (error) {
            *error = "VirtualAlloc failed for MessageBoxA trampoline";
        }
        return false;
    }
    std::memcpy(g_trampoline, g_target, kPatchSize);
    if (!WriteRelativeJump(
            static_cast<std::uint8_t*>(g_trampoline) + kPatchSize,
            static_cast<std::uint8_t*>(g_target) + kPatchSize,
            error)) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(
            g_target,
            kPatchSize,
            PAGE_EXECUTE_READWRITE,
            &old_protect)) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
        if (error) {
            *error = "VirtualProtect failed for user32!MessageBoxA";
        }
        return false;
    }
    const bool jump_written = WriteRelativeJump(g_target, replacement, error);
    if (jump_written) {
        FlushInstructionCache(GetCurrentProcess(), g_target, kPatchSize);
    }
    DWORD discard = 0;
    VirtualProtect(g_target, kPatchSize, old_protect, &discard);
    if (!jump_written) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
        return false;
    }

    *original_trampoline = g_trampoline;
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace pal4::inject

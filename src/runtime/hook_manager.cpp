#include "hook_manager.h"

#include <algorithm>
#include <cstring>
#include <limits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "input_hooks.h"
#include "pal4inject/hook_inventory.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
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

bool WriteJumpPatch(
    void* target,
    void* destination,
    const std::size_t patch_span,
    std::string* error) {
    if (patch_span < 5) {
        if (error) {
            *error = "patch span must be at least 5 bytes";
        }
        return false;
    }

    const auto delta = reinterpret_cast<std::intptr_t>(destination) -
        (reinterpret_cast<std::intptr_t>(target) + 5);
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max()) {
        if (error) {
            *error = "detour destination is out of 32-bit relative jump range";
        }
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(target, patch_span, PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (error) {
            *error = "VirtualProtect failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    auto* patch = static_cast<unsigned char*>(target);
    patch[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(patch + 1) = static_cast<std::int32_t>(delta);
    for (std::size_t i = 5; i < patch_span; ++i) {
        patch[i] = 0x90;
    }
    FlushInstructionCache(GetCurrentProcess(), target, patch_span);

    DWORD discard = 0;
    VirtualProtect(target, patch_span, old_protect, &discard);
    return true;
}

bool CopyRelocatingX86Bytes(
    const void* source,
    void* destination,
    const std::size_t size,
    std::string* error) {
    auto* src = static_cast<const unsigned char*>(source);
    auto* dst = static_cast<unsigned char*>(destination);
    std::memcpy(dst, src, size);

    auto decode_instruction_length =
        [error](const unsigned char* code, const std::size_t remaining, std::size_t* opcode_offset) -> std::size_t {
        if (!code || remaining == 0 || !opcode_offset) {
            if (error) {
                *error = "invalid x86 instruction decode request";
            }
            return 0;
        }

        std::size_t prefix_len = 0;
        if (code[0] == 0x64) {
            if (remaining < 2) {
                if (error) {
                    *error = "truncated segment-prefixed instruction";
                }
                return 0;
            }
            prefix_len = 1;
        }

        const auto modrm_length =
            [error](const unsigned char* bytes, const std::size_t left, const std::size_t opcode_bytes, const std::size_t immediate_bytes) -> std::size_t {
            if (left < opcode_bytes + 1) {
                if (error) {
                    *error = "truncated modrm instruction";
                }
                return 0;
            }
            const unsigned char modrm = bytes[opcode_bytes];
            const unsigned char mod = (modrm >> 6) & 0x3;
            const unsigned char rm = modrm & 0x7;
            std::size_t len = opcode_bytes + 1;

            unsigned char sib = 0;
            if (mod != 3 && rm == 4) {
                if (left < len + 1) {
                    if (error) {
                        *error = "truncated sib instruction";
                    }
                    return 0;
                }
                sib = bytes[len];
                ++len;
            }

            if (mod == 0) {
                if (rm == 5 || (rm == 4 && (sib & 0x7) == 5)) {
                    len += 4;
                }
            } else if (mod == 1) {
                len += 1;
            } else if (mod == 2) {
                len += 4;
            }

            len += immediate_bytes;
            if (len > left) {
                if (error) {
                    *error = "modrm instruction extends past trampoline patch span";
                }
                return 0;
            }
            return len;
        };

        *opcode_offset = prefix_len;
        const unsigned char opcode = code[prefix_len];
        switch (opcode) {
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x90:
            return prefix_len + 1;
        case 0x6A:
            return remaining >= prefix_len + 2 ? prefix_len + 2 : 0;
        case 0x68:
        case 0xA1:
            return remaining >= prefix_len + 5 ? prefix_len + 5 : 0;
        case 0x8A:
        case 0x8B:
        case 0x8D: {
            const std::size_t len = modrm_length(code + prefix_len, remaining - prefix_len, 1, 0);
            return len == 0 ? 0 : len + prefix_len;
        }
        case 0x83: {
            const std::size_t len = modrm_length(code + prefix_len, remaining - prefix_len, 1, 1);
            return len == 0 ? 0 : len + prefix_len;
        }
        case 0xE8:
        case 0xE9:
            return remaining >= prefix_len + 5 ? prefix_len + 5 : 0;
        case 0xEB:
            if (error) {
                *error = "unsupported short jump inside trampoline patch span";
            }
            return 0;
        default:
            if (opcode >= 0x70 && opcode <= 0x7F) {
                if (error) {
                    *error = "unsupported short conditional jump inside trampoline patch span";
                }
                return 0;
            }
            if (error) {
                *error = "unsupported x86 instruction in trampoline patch span";
            }
            return 0;
        }
    };

    std::size_t offset = 0;
    while (offset < size) {
        std::size_t opcode_offset = 0;
        const std::size_t instruction_length =
            decode_instruction_length(src + offset, size - offset, &opcode_offset);
        if (instruction_length == 0 || offset + instruction_length > size) {
            return false;
        }

        const unsigned char opcode = src[offset + opcode_offset];
        if (opcode == 0xE8 || opcode == 0xE9) {
            const auto original_disp =
                *reinterpret_cast<const std::int32_t*>(src + offset + opcode_offset + 1);
            const auto original_target =
                reinterpret_cast<std::intptr_t>(src + offset + opcode_offset + 5) + original_disp;
            const auto relocated_disp =
                original_target -
                reinterpret_cast<std::intptr_t>(dst + offset + opcode_offset + 5);
            if (relocated_disp < std::numeric_limits<std::int32_t>::min() ||
                relocated_disp > std::numeric_limits<std::int32_t>::max()) {
                if (error) {
                    *error = "relocated relative branch is out of 32-bit range";
                }
                return false;
            }
            *reinterpret_cast<std::int32_t*>(dst + offset + opcode_offset + 1) =
                static_cast<std::int32_t>(relocated_disp);
        }
        offset += instruction_length;
    }
    return true;
}

}  // namespace

bool HookManager::Initialize(std::string* error) {
    if (initialized_) {
        return true;
    }

    registrations_.clear();
    const auto inventory = BuildHookInventorySkeleton();
    registrations_.reserve(inventory.size());
    for (const auto& descriptor : inventory) {
        HookRegistration registration{};
        registration.descriptor = descriptor;
        switch (descriptor.id) {
        case HookId::process_ui_event:
        case HookId::handle_ui_message:
        case HookId::simulate_key_press_and_release:
        case HookId::process_inputs:
        case HookId::update_input_device_state:
        case HookId::initialize_direct_input:
        case HookId::gi_talk:
        case HookId::cegui_renderer_constructor_2:
        case HookId::cegui_system_initialize:
        case HookId::load_font_file:
        case HookId::setup_minimap_texture:
        case HookId::camera_update_matrix:
        case HookId::d3d9_set_present_parameters:
        case HookId::pal4_main_wndproc:
            registration.install_on_bootstrap = true;
            registration.descriptor.replacement = GetReplacementForHook(descriptor.id);
            if (!registration.descriptor.replacement) {
                if (error) {
                    *error = std::string("missing replacement for hook ") + ToString(descriptor.id);
                }
                return false;
            }
            break;
        default:
            registration.install_on_bootstrap = false;
            break;
        }
        registrations_.push_back(registration);
    }

    std::vector<HookDescriptor> descriptors;
    descriptors.reserve(registrations_.size());
    for (const auto& registration : registrations_) {
        descriptors.push_back(registration.descriptor);
    }
    GetRuntimeState().InitializeInventory(descriptors);
    std::stable_sort(
        registrations_.begin(),
        registrations_.end(),
        [](const HookRegistration& lhs, const HookRegistration& rhs) {
            if (lhs.install_on_bootstrap != rhs.install_on_bootstrap) {
                return lhs.install_on_bootstrap && !rhs.install_on_bootstrap;
            }
            if (lhs.descriptor.bootstrap_order != rhs.descriptor.bootstrap_order) {
                return lhs.descriptor.bootstrap_order < rhs.descriptor.bootstrap_order;
            }
            return static_cast<int>(lhs.descriptor.id) < static_cast<int>(rhs.descriptor.id);
        });
    initialized_ = true;
    return true;
}

bool HookManager::InstallBootstrapHooks(std::string* error) {
    if (!initialized_ && !Initialize(error)) {
        return false;
    }

    std::vector<std::string> optional_failures;
    for (auto& registration : registrations_) {
        if (!registration.install_on_bootstrap) {
            continue;
        }
        if (!InstallHook(registration, error)) {
            const std::string base_error = error ? *error : "install failed";
            const std::string full_error =
                std::string(ToString(registration.descriptor.id)) + ": " + base_error;
            auto& state = GetRuntimeState();
            state.SetHookError(registration.descriptor.id, full_error);
            if (registration.descriptor.bootstrap_required) {
                if (error) {
                    *error = full_error;
                }
                return false;
            }

            state.AppendEventLog(
                std::string("optional_hook_install_failed hook=") +
                ToString(registration.descriptor.id) +
                " error=" + full_error);
            optional_failures.push_back(full_error);
            if (error) {
                error->clear();
            }
        }
    }
    if (!optional_failures.empty()) {
        std::string joined = "optional bootstrap hook failures: ";
        for (std::size_t i = 0; i < optional_failures.size(); ++i) {
            if (i != 0) {
                joined += " | ";
            }
            joined += optional_failures[i];
        }
        GetRuntimeState().SetLastError(joined);
    }
    return true;
}

void HookManager::UninstallAll() {
    for (auto& registration : registrations_) {
        if (registration.installed) {
            UninstallHook(registration);
        }
    }
}

std::vector<HookDescriptor> HookManager::CopyInventory() const {
    std::vector<HookDescriptor> descriptors;
    descriptors.reserve(registrations_.size());
    for (const auto& registration : registrations_) {
        descriptors.push_back(registration.descriptor);
    }
    return descriptors;
}

HookManager::HookRegistration* HookManager::FindRegistration(const HookId id) {
    for (auto& registration : registrations_) {
        if (registration.descriptor.id == id) {
            return &registration;
        }
    }
    return nullptr;
}

bool HookManager::InstallHook(HookRegistration& hook, std::string* error) {
    if (hook.installed) {
        return true;
    }

    auto& runtime_state = GetRuntimeState();
    const std::uintptr_t base = runtime_state.MainModuleBase();
    if (base == 0) {
        if (error) {
            *error = "main module base is not set";
        }
        return false;
    }

    hook.target = reinterpret_cast<void*>(
        ida::ResolveRuntimeAddress(base, hook.descriptor.ida_ea));
    if (!hook.target) {
        if (error) {
            *error = "resolved target address is null";
        }
        return false;
    }

    if (std::memcmp(
            hook.target,
            hook.descriptor.expected_prologue.data(),
            hook.descriptor.expected_prologue.size()) != 0) {
        if (error) {
            *error = std::string("prologue mismatch for ") + ToString(hook.descriptor.id);
        }
        return false;
    }

    hook.original_bytes.resize(hook.descriptor.patch_span);
    std::memcpy(hook.original_bytes.data(), hook.target, hook.descriptor.patch_span);

    void* trampoline = VirtualAlloc(
        nullptr,
        hook.descriptor.patch_span + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        if (error) {
            *error = "VirtualAlloc trampoline failed: " + FormatWindowsError(GetLastError());
        }
        return false;
    }

    if (!CopyRelocatingX86Bytes(
            hook.target,
            trampoline,
            hook.descriptor.patch_span,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    void* continue_at = static_cast<unsigned char*>(hook.target) + hook.descriptor.patch_span;
    if (!WriteJumpPatch(
            static_cast<unsigned char*>(trampoline) + hook.descriptor.patch_span,
            continue_at,
            5,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteJumpPatch(
            hook.target,
            hook.descriptor.replacement,
            hook.descriptor.patch_span,
            error)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    hook.descriptor.original_trampoline = trampoline;
    SetOriginalTrampoline(hook.descriptor.id, trampoline);
    hook.installed = true;
    runtime_state.SetHookInstalled(hook.descriptor.id, true);
    runtime_state.ClearHookError(hook.descriptor.id);
    return true;
}

void HookManager::UninstallHook(HookRegistration& hook) {
    if (!hook.installed || !hook.target || hook.original_bytes.empty()) {
        return;
    }

    DWORD old_protect = 0;
    if (VirtualProtect(hook.target, hook.original_bytes.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        std::memcpy(hook.target, hook.original_bytes.data(), hook.original_bytes.size());
        FlushInstructionCache(GetCurrentProcess(), hook.target, hook.original_bytes.size());
        DWORD discard = 0;
        VirtualProtect(hook.target, hook.original_bytes.size(), old_protect, &discard);
    }

    if (hook.descriptor.original_trampoline) {
        VirtualFree(hook.descriptor.original_trampoline, 0, MEM_RELEASE);
        hook.descriptor.original_trampoline = nullptr;
        SetOriginalTrampoline(hook.descriptor.id, nullptr);
    }

    hook.installed = false;
    GetRuntimeState().SetHookInstalled(hook.descriptor.id, false);
}

HookManager& GetHookManager() {
    static HookManager manager;
    return manager;
}

}  // namespace pal4::inject

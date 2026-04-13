#include "memory_debug_runtime.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

bool ResolveMemoryAddress(
    const AddressSpace address_space,
    const std::uint32_t input_address,
    std::uint32_t* resolved_address,
    std::string* error) {
    if (!resolved_address) {
        if (error) {
            *error = "resolved address output pointer is null";
        }
        return false;
    }

    if (address_space == AddressSpace::runtime_va) {
        *resolved_address = input_address;
        return true;
    }

    const auto module_base = GetRuntimeState().MainModuleBase();
    if (module_base == 0) {
        if (error) {
            *error = "main module base is not set";
        }
        return false;
    }

    const auto resolved =
        ida::ResolveRuntimeAddress(module_base, input_address);
    if (resolved > static_cast<std::uintptr_t>(std::numeric_limits<std::uint32_t>::max())) {
        if (error) {
            *error = "resolved runtime address exceeds 32-bit range";
        }
        return false;
    }

    *resolved_address = static_cast<std::uint32_t>(resolved);
    return true;
}

bool IsReadableProtection(const DWORD protect) noexcept {
    const DWORD base = protect & 0xFFU;
    return base == PAGE_READONLY ||
        base == PAGE_READWRITE ||
        base == PAGE_WRITECOPY ||
        base == PAGE_EXECUTE_READ ||
        base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritableProtection(const DWORD protect) noexcept {
    const DWORD base = protect & 0xFFU;
    return base == PAGE_READWRITE ||
        base == PAGE_WRITECOPY ||
        base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY;
}

bool IsExecutableProtection(const DWORD protect) noexcept {
    const DWORD base = protect & 0xFFU;
    return base == PAGE_EXECUTE ||
        base == PAGE_EXECUTE_READ ||
        base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY;
}

void PopulateRegionInfo(
    const AddressSpace address_space,
    const std::uint32_t input_address,
    const std::uint32_t resolved_address,
    const MEMORY_BASIC_INFORMATION& mbi,
    MemoryRegionInfo* out) {
    if (!out) {
        return;
    }

    out->address_space = address_space;
    out->input_address = input_address;
    out->resolved_address = resolved_address;
    out->base = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(mbi.BaseAddress));
    out->allocation_base = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(mbi.AllocationBase));
    out->region_size = static_cast<std::uint32_t>(std::min<std::size_t>(
        mbi.RegionSize,
        std::numeric_limits<std::uint32_t>::max()));
    out->state = mbi.State;
    out->type = mbi.Type;
    out->protect = mbi.Protect;
    const bool committed = mbi.State == MEM_COMMIT;
    const bool guarded = (mbi.Protect & PAGE_GUARD) != 0;
    const bool inaccessible = (mbi.Protect & 0xFFU) == PAGE_NOACCESS;
    out->readable = committed && !guarded && !inaccessible && IsReadableProtection(mbi.Protect);
    out->writable = committed && !guarded && !inaccessible && IsWritableProtection(mbi.Protect);
    out->executable = committed && !guarded && IsExecutableProtection(mbi.Protect);
}

void LogMemoryFailure(
    const std::string_view operation,
    const AddressSpace address_space,
    const std::uint32_t input_address,
    const std::uint32_t resolved_address,
    const std::string_view error_text) {
    auto& state = GetRuntimeState();
    const std::string line =
        std::string("memory_error op=") + std::string(operation) +
        " space=" + ToString(address_space) +
        " input=" + FormatHexValue(input_address) +
        " resolved=" + FormatHexValue(resolved_address) +
        " error=" + std::string(error_text);
    state.SetLastError(error_text);
    state.AppendEventLog(line);
}

bool QueryMemoryRegionInternal(
    const AddressSpace address_space,
    const std::uint32_t input_address,
    MemoryRegionInfo* out,
    std::string* error) {
    std::uint32_t resolved_address = 0;
    if (!ResolveMemoryAddress(address_space, input_address, &resolved_address, error)) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(resolved_address)),
            &mbi,
            sizeof(mbi)) == 0) {
        if (error) {
            *error = "VirtualQuery failed";
        }
        return false;
    }

    PopulateRegionInfo(address_space, input_address, resolved_address, mbi, out);
    return true;
}

bool ValidateMemoryRange(
    const MemoryRegionInfo& region,
    const std::uint32_t size,
    std::string* error) {
    if (size == 0) {
        if (error) {
            *error = "memory size must be greater than 0";
        }
        return false;
    }

    const std::uint64_t region_end =
        static_cast<std::uint64_t>(region.base) + region.region_size;
    const std::uint64_t requested_end =
        static_cast<std::uint64_t>(region.resolved_address) + size;
    if (requested_end > region_end) {
        if (error) {
            *error = "requested memory range spans multiple regions";
        }
        return false;
    }
    return true;
}

bool ReadBytesUnchecked(
    const std::uint32_t resolved_address,
    const std::uint32_t size,
    std::vector<std::uint8_t>* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "memory read output pointer is null";
        }
        return false;
    }

    out->resize(size);
    auto* const destination = out->data();
    const void* const source =
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(resolved_address));
    const auto raw_copy =
        [](void* dst, const void* src, const std::size_t byte_count) -> bool {
            __try {
                std::memcpy(dst, src, byte_count);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return true;
        };
    if (!raw_copy(destination, source, size)) {
        if (error) {
            *error = "memory read trapped an SEH exception";
        }
        return false;
    }
    return true;
}

bool WriteBytesUnchecked(
    const std::uint32_t resolved_address,
    const std::vector<std::uint8_t>& bytes,
    std::string* error) {
    void* const destination =
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(resolved_address));
    const auto raw_copy =
        [](void* dst, const void* src, const std::size_t byte_count) -> bool {
            __try {
                std::memcpy(dst, src, byte_count);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return true;
        };
    if (!raw_copy(destination, bytes.data(), bytes.size())) {
        if (error) {
            *error = "memory write trapped an SEH exception";
        }
        return false;
    }
    return true;
}

std::string FormatBytesPrefix(const std::vector<std::uint8_t>& bytes) {
    const std::size_t count = std::min<std::size_t>(bytes.size(), 8);
    std::vector<std::uint8_t> prefix(bytes.begin(), bytes.begin() + count);
    return FormatHexBytes(prefix);
}

}  // namespace

bool QueryMemoryRegion(
    const AddressSpace address_space,
    const std::uint32_t address,
    MemoryRegionInfo* out,
    std::string* error) {
    if (!QueryMemoryRegionInternal(address_space, address, out, error)) {
        const std::uint32_t resolved = out ? out->resolved_address : 0;
        LogMemoryFailure("query_memory", address_space, address, resolved, error ? *error : "query failed");
        return false;
    }
    return true;
}

bool ReadMemoryRegion(
    const AddressSpace address_space,
    const std::uint32_t address,
    const std::uint32_t size,
    std::vector<std::uint8_t>* out_bytes,
    MemoryRegionInfo* out_region,
    std::string* error) {
    if (size > 1024) {
        if (error) {
            *error = "memory read size exceeds the 1024-byte limit";
        }
        LogMemoryFailure("read_memory", address_space, address, 0, error ? *error : "read too large");
        return false;
    }

    MemoryRegionInfo region{};
    if (!QueryMemoryRegionInternal(address_space, address, &region, error)) {
        LogMemoryFailure("read_memory", address_space, address, region.resolved_address, error ? *error : "query failed");
        return false;
    }
    if (!ValidateMemoryRange(region, size, error)) {
        LogMemoryFailure("read_memory", address_space, address, region.resolved_address, error ? *error : "invalid range");
        return false;
    }
    if (!region.readable) {
        if (error) {
            *error = "memory region is not readable";
        }
        LogMemoryFailure("read_memory", address_space, address, region.resolved_address, error ? *error : "not readable");
        return false;
    }
    if (!ReadBytesUnchecked(region.resolved_address, size, out_bytes, error)) {
        LogMemoryFailure("read_memory", address_space, address, region.resolved_address, error ? *error : "read failed");
        return false;
    }
    if (out_region) {
        *out_region = region;
    }
    return true;
}

bool WriteMemoryRegion(
    const AddressSpace address_space,
    const std::uint32_t address,
    const std::vector<std::uint8_t>& bytes,
    const bool unsafe_code_write,
    MemoryRegionInfo* out_region,
    std::vector<std::uint8_t>* out_before_bytes,
    std::vector<std::uint8_t>* out_after_bytes,
    std::string* error) {
    if (bytes.empty()) {
        if (error) {
            *error = "memory write payload is empty";
        }
        LogMemoryFailure("write_memory", address_space, address, 0, error ? *error : "empty payload");
        return false;
    }
    if (bytes.size() > 512) {
        if (error) {
            *error = "memory write size exceeds the 512-byte limit";
        }
        LogMemoryFailure("write_memory", address_space, address, 0, error ? *error : "write too large");
        return false;
    }

    MemoryRegionInfo region{};
    if (!QueryMemoryRegionInternal(address_space, address, &region, error)) {
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "query failed");
        return false;
    }
    if (!ValidateMemoryRange(region, static_cast<std::uint32_t>(bytes.size()), error)) {
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "invalid range");
        return false;
    }
    if (region.executable && !unsafe_code_write) {
        if (error) {
            *error = "memory region is executable; pass unsafe_code_write=1 to allow patching code";
        }
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "unsafe code write rejected");
        return false;
    }
    if (!region.readable) {
        if (error) {
            *error = "memory region is not readable";
        }
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "not readable");
        return false;
    }

    std::vector<std::uint8_t> before_bytes;
    if (!ReadBytesUnchecked(
            region.resolved_address,
            static_cast<std::uint32_t>(bytes.size()),
            &before_bytes,
            error)) {
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "pre-read failed");
        return false;
    }

    const bool need_protect_change = region.executable || !region.writable;
    DWORD old_protect = 0;
    if (need_protect_change) {
        const DWORD new_protect = region.executable
            ? PAGE_EXECUTE_READWRITE
            : PAGE_READWRITE;
        if (!VirtualProtect(
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(region.resolved_address)),
                bytes.size(),
                new_protect,
                &old_protect)) {
            if (error) {
                *error = "VirtualProtect failed before memory write";
            }
            LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "virtual protect failed");
            return false;
        }
    }

    const bool wrote = WriteBytesUnchecked(region.resolved_address, bytes, error);

    if (region.executable) {
        FlushInstructionCache(
            GetCurrentProcess(),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(region.resolved_address)),
            bytes.size());
    }
    if (need_protect_change) {
        DWORD discard = 0;
        VirtualProtect(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(region.resolved_address)),
            bytes.size(),
            old_protect,
            &discard);
    }
    if (!wrote) {
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "write failed");
        return false;
    }

    std::vector<std::uint8_t> after_bytes;
    if (!ReadBytesUnchecked(
            region.resolved_address,
            static_cast<std::uint32_t>(bytes.size()),
            &after_bytes,
            error)) {
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "verify read failed");
        return false;
    }
    if (after_bytes != bytes) {
        if (error) {
            *error = "memory write verify mismatch";
        }
        LogMemoryFailure("write_memory", address_space, address, region.resolved_address, error ? *error : "verify mismatch");
        return false;
    }

    if (out_region) {
        *out_region = region;
    }
    if (out_before_bytes) {
        *out_before_bytes = before_bytes;
    }
    if (out_after_bytes) {
        *out_after_bytes = after_bytes;
    }

    GetRuntimeState().AppendEventLog(
        std::string("memory_write space=") + ToString(address_space) +
        " resolved=" + FormatHexValue(region.resolved_address) +
        " size=" + std::to_string(bytes.size()) +
        " unsafe=" + (unsafe_code_write ? "1" : "0") +
        " before=" + FormatBytesPrefix(before_bytes) +
        " after=" + FormatBytesPrefix(after_bytes));
    return true;
}

}  // namespace pal4::inject

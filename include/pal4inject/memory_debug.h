#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pal4::inject {

enum class AddressSpace : std::uint8_t {
    ida_ea = 0,
    runtime_va,
};

enum class MemoryScalarType : std::uint8_t {
    u8 = 0,
    u16,
    u32,
    u64,
    i32,
    f32,
    f64,
    ptr,
};

struct MemoryRegionInfo {
    AddressSpace address_space = AddressSpace::runtime_va;
    std::uint32_t input_address = 0;
    std::uint32_t resolved_address = 0;
    std::uint32_t base = 0;
    std::uint32_t allocation_base = 0;
    std::uint32_t region_size = 0;
    std::uint32_t state = 0;
    std::uint32_t type = 0;
    std::uint32_t protect = 0;
    bool readable = false;
    bool writable = false;
    bool executable = false;
};

const char* ToString(AddressSpace space) noexcept;
const char* ToString(MemoryScalarType type) noexcept;
bool TryParseAddressSpace(std::string_view text, AddressSpace* out) noexcept;
bool TryParseMemoryScalarType(std::string_view text, MemoryScalarType* out) noexcept;
std::size_t SizeOfMemoryScalarType(MemoryScalarType type) noexcept;

bool ParseAddressValue(std::string_view text, std::uint32_t* out) noexcept;
bool ParseHexBytes(
    std::string_view text,
    std::vector<std::uint8_t>* out,
    std::string* error);
std::string FormatHexBytes(const std::vector<std::uint8_t>& bytes);
std::string FormatHexValue(std::uint32_t value, std::size_t width = 8);

std::string DescribeMemoryState(std::uint32_t state);
std::string DescribeMemoryType(std::uint32_t type);
std::string DescribeMemoryProtect(std::uint32_t protect);
std::string FormatMemoryRegionSummary(const MemoryRegionInfo& region);

bool EncodeScalarValue(
    MemoryScalarType type,
    std::string_view text,
    std::vector<std::uint8_t>* out,
    std::string* error);
bool DecodeScalarValue(
    MemoryScalarType type,
    const std::vector<std::uint8_t>& bytes,
    std::string* out,
    std::string* error);

}  // namespace pal4::inject

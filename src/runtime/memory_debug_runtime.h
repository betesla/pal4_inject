#pragma once

#include <string>
#include <vector>

#include "pal4inject/memory_debug.h"

namespace pal4::inject {

bool QueryMemoryRegion(
    AddressSpace address_space,
    std::uint32_t address,
    MemoryRegionInfo* out,
    std::string* error);
bool ReadMemoryRegion(
    AddressSpace address_space,
    std::uint32_t address,
    std::uint32_t size,
    std::vector<std::uint8_t>* out_bytes,
    MemoryRegionInfo* out_region,
    std::string* error);
bool WriteMemoryRegion(
    AddressSpace address_space,
    std::uint32_t address,
    const std::vector<std::uint8_t>& bytes,
    bool unsafe_code_write,
    MemoryRegionInfo* out_region,
    std::vector<std::uint8_t>* out_before_bytes,
    std::vector<std::uint8_t>* out_after_bytes,
    std::string* error);

}  // namespace pal4::inject

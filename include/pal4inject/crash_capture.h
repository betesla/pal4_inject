#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace pal4::inject {

struct CrashContextSnapshot {
    std::uint32_t exception_code = 0;
    std::uint32_t exception_flags = 0;
    std::uintptr_t exception_address = 0;
    bool has_access_address = false;
    std::uint32_t access_type = 0;
    std::uintptr_t access_address = 0;
    std::uint32_t eip = 0;
    std::uint32_t esp = 0;
    std::uint32_t ebp = 0;
    std::uint32_t eax = 0;
    std::uint32_t ebx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t edx = 0;
    std::uint32_t esi = 0;
    std::uint32_t edi = 0;
};

bool IsCrashExceptionCode(std::uint32_t exception_code) noexcept;
const char* DescribeExceptionCode(std::uint32_t exception_code) noexcept;
std::string FormatExceptionCode(std::uint32_t exception_code);
std::string BuildCrashArtifactStem(
    std::uint32_t process_id,
    std::uint32_t thread_id,
    std::uint32_t exception_code,
    std::uint64_t tick_count);
std::string BuildCrashSummary(
    const CrashContextSnapshot& snapshot,
    std::string_view source);

}  // namespace pal4::inject

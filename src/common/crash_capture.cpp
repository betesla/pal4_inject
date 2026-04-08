#include "pal4inject/crash_capture.h"

#include <iomanip>
#include <sstream>

namespace pal4::inject {
namespace {

const char* AccessTypeName(const std::uint32_t access_type) noexcept {
    switch (access_type) {
    case 0:
        return "read";
    case 1:
        return "write";
    case 8:
        return "execute";
    default:
        return "unknown";
    }
}

}  // namespace

bool IsCrashExceptionCode(const std::uint32_t exception_code) noexcept {
    switch (exception_code) {
    case 0xC0000005:  // EXCEPTION_ACCESS_VIOLATION
    case 0xC0000006:  // EXCEPTION_IN_PAGE_ERROR
    case 0x80000002:  // EXCEPTION_DATATYPE_MISALIGNMENT
    case 0xC000001D:  // EXCEPTION_ILLEGAL_INSTRUCTION
    case 0xC0000025:  // EXCEPTION_NONCONTINUABLE_EXCEPTION
    case 0xC0000026:  // EXCEPTION_INVALID_DISPOSITION
    case 0xC000008C:  // EXCEPTION_ARRAY_BOUNDS_EXCEEDED
    case 0xC0000094:  // EXCEPTION_INT_DIVIDE_BY_ZERO
    case 0xC0000096:  // EXCEPTION_PRIV_INSTRUCTION
    case 0xC00000FD:  // EXCEPTION_STACK_OVERFLOW
    case 0xC0000409:  // STATUS_STACK_BUFFER_OVERRUN
    case 0xC000041D:  // STATUS_FATAL_USER_CALLBACK_EXCEPTION
        return true;
    default:
        return false;
    }
}

const char* DescribeExceptionCode(const std::uint32_t exception_code) noexcept {
    switch (exception_code) {
    case 0xC0000005:
        return "EXCEPTION_ACCESS_VIOLATION";
    case 0xC0000006:
        return "EXCEPTION_IN_PAGE_ERROR";
    case 0x80000002:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case 0xC000001D:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case 0xC0000025:
        return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case 0xC0000026:
        return "EXCEPTION_INVALID_DISPOSITION";
    case 0xC000008C:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case 0xC0000094:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case 0xC0000096:
        return "EXCEPTION_PRIV_INSTRUCTION";
    case 0xC00000FD:
        return "EXCEPTION_STACK_OVERFLOW";
    case 0xC0000409:
        return "STATUS_STACK_BUFFER_OVERRUN";
    case 0xC000041D:
        return "STATUS_FATAL_USER_CALLBACK_EXCEPTION";
    case 0xE06D7363:
        return "MSVC_CPP_EXCEPTION";
    default:
        return "UNKNOWN_EXCEPTION";
    }
}

std::string FormatExceptionCode(const std::uint32_t exception_code) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
        << exception_code;
    return out.str();
}

std::string BuildCrashArtifactStem(
    const std::uint32_t process_id,
    const std::uint32_t thread_id,
    const std::uint32_t exception_code,
    const std::uint64_t tick_count) {
    std::ostringstream out;
    out
        << "pal4_inject_crash"
        << "_pid" << process_id
        << "_tid" << thread_id
        << "_code" << FormatExceptionCode(exception_code)
        << "_tick" << tick_count;
    return out.str();
}

std::string BuildCrashSummary(
    const CrashContextSnapshot& snapshot,
    const std::string_view source) {
    std::ostringstream out;
    out
        << "source=" << source << "\n"
        << "exception_code=" << FormatExceptionCode(snapshot.exception_code) << "\n"
        << "exception_name=" << DescribeExceptionCode(snapshot.exception_code) << "\n"
        << "exception_flags=0x" << std::hex << std::uppercase << snapshot.exception_flags << "\n"
        << "exception_address=0x" << std::hex << std::uppercase << snapshot.exception_address << "\n";
    if (snapshot.has_access_address) {
        out
            << "access_type=" << AccessTypeName(snapshot.access_type) << "\n"
            << "access_address=0x" << std::hex << std::uppercase << snapshot.access_address << "\n";
    }
    out
        << "register_eip=0x" << std::hex << std::uppercase << snapshot.eip << "\n"
        << "register_esp=0x" << std::hex << std::uppercase << snapshot.esp << "\n"
        << "register_ebp=0x" << std::hex << std::uppercase << snapshot.ebp << "\n"
        << "register_eax=0x" << std::hex << std::uppercase << snapshot.eax << "\n"
        << "register_ebx=0x" << std::hex << std::uppercase << snapshot.ebx << "\n"
        << "register_ecx=0x" << std::hex << std::uppercase << snapshot.ecx << "\n"
        << "register_edx=0x" << std::hex << std::uppercase << snapshot.edx << "\n"
        << "register_esi=0x" << std::hex << std::uppercase << snapshot.esi << "\n"
        << "register_edi=0x" << std::hex << std::uppercase << snapshot.edi;
    return out.str();
}

}  // namespace pal4::inject

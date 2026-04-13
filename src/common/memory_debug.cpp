#include "pal4inject/memory_debug.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {
namespace {

template <typename Enum>
struct EnumName {
    Enum value;
    const char* name;
};

constexpr std::array<EnumName<AddressSpace>, 2> kAddressSpaces{{
    {AddressSpace::ida_ea, "ida_ea"},
    {AddressSpace::runtime_va, "runtime_va"},
}};

constexpr std::array<EnumName<MemoryScalarType>, 8> kMemoryScalarTypes{{
    {MemoryScalarType::u8, "u8"},
    {MemoryScalarType::u16, "u16"},
    {MemoryScalarType::u32, "u32"},
    {MemoryScalarType::u64, "u64"},
    {MemoryScalarType::i32, "i32"},
    {MemoryScalarType::f32, "f32"},
    {MemoryScalarType::f64, "f64"},
    {MemoryScalarType::ptr, "ptr"},
}};

template <typename Enum, std::size_t N>
const char* FindEnumName(
    const Enum value,
    const std::array<EnumName<Enum>, N>& names) noexcept {
    for (const auto& entry : names) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
bool TryParseEnum(
    const std::string_view text,
    const std::array<EnumName<Enum>, N>& names,
    Enum* out) noexcept {
    if (!out) {
        return false;
    }
    for (const auto& entry : names) {
        if (text == entry.name) {
            *out = entry.value;
            return true;
        }
    }
    return false;
}

bool ParseUnsignedInteger(
    const std::string_view text,
    std::uint64_t* out) noexcept {
    if (!out || text.empty()) {
        return false;
    }

    std::uint64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        auto [ptr, ec] = std::from_chars(begin + 2, end, value, 16);
        if (ec != std::errc{} || ptr != end) {
            return false;
        }
    } else {
        auto [ptr, ec] = std::from_chars(begin, end, value, 10);
        if (ec != std::errc{} || ptr != end) {
            return false;
        }
    }
    *out = value;
    return true;
}

bool ParseSignedInteger(
    const std::string_view text,
    std::int64_t* out) noexcept {
    if (!out || text.empty()) {
        return false;
    }

    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        std::uint64_t unsigned_value = 0;
        if (!ParseUnsignedInteger(text, &unsigned_value) ||
            unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        *out = static_cast<std::int32_t>(static_cast<std::uint32_t>(unsigned_value));
        return true;
    }

    std::int64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, value, 10);
    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    *out = value;
    return true;
}

int DecodeHexNibble(const char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

template <typename T>
void CopyScalarBytes(const T value, std::vector<std::uint8_t>* out) {
    out->resize(sizeof(T));
    std::memcpy(out->data(), &value, sizeof(T));
}

template <typename T>
std::optional<T> ReadScalarBytes(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != sizeof(T)) {
        return std::nullopt;
    }
    T value{};
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

}  // namespace

const char* ToString(const AddressSpace space) noexcept {
    return FindEnumName(space, kAddressSpaces);
}

const char* ToString(const MemoryScalarType type) noexcept {
    return FindEnumName(type, kMemoryScalarTypes);
}

bool TryParseAddressSpace(const std::string_view text, AddressSpace* out) noexcept {
    return TryParseEnum(text, kAddressSpaces, out);
}

bool TryParseMemoryScalarType(
    const std::string_view text,
    MemoryScalarType* out) noexcept {
    return TryParseEnum(text, kMemoryScalarTypes, out);
}

std::size_t SizeOfMemoryScalarType(const MemoryScalarType type) noexcept {
    switch (type) {
    case MemoryScalarType::u8:
        return 1;
    case MemoryScalarType::u16:
        return 2;
    case MemoryScalarType::u32:
    case MemoryScalarType::i32:
    case MemoryScalarType::f32:
    case MemoryScalarType::ptr:
        return 4;
    case MemoryScalarType::u64:
    case MemoryScalarType::f64:
        return 8;
    }
    return 0;
}

bool ParseAddressValue(const std::string_view text, std::uint32_t* out) noexcept {
    std::uint64_t value = 0;
    if (!ParseUnsignedInteger(text, &value) ||
        value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    if (out) {
        *out = static_cast<std::uint32_t>(value);
    }
    return true;
}

bool ParseHexBytes(
    const std::string_view text,
    std::vector<std::uint8_t>* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "hex bytes output pointer is null";
        }
        return false;
    }

    std::string sanitized;
    sanitized.reserve(text.size());
    for (const char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '_') {
            continue;
        }
        sanitized.push_back(ch);
    }
    if (sanitized.rfind("0x", 0) == 0 || sanitized.rfind("0X", 0) == 0) {
        sanitized.erase(0, 2);
    }
    if (sanitized.empty()) {
        if (error) {
            *error = "hex bytes payload is empty";
        }
        return false;
    }
    if ((sanitized.size() % 2) != 0) {
        if (error) {
            *error = "hex bytes payload must have an even number of digits";
        }
        return false;
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(sanitized.size() / 2);
    for (std::size_t i = 0; i < sanitized.size(); i += 2) {
        const int high = DecodeHexNibble(sanitized[i]);
        const int low = DecodeHexNibble(sanitized[i + 1]);
        if (high < 0 || low < 0) {
            if (error) {
                *error = "hex bytes payload contains a non-hex digit";
            }
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }

    *out = std::move(bytes);
    return true;
}

std::string FormatHexBytes(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out.push_back(kHex[(byte >> 4) & 0xF]);
        out.push_back(kHex[byte & 0xF]);
    }
    return out;
}

std::string FormatHexValue(const std::uint32_t value, const std::size_t width) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase
        << std::setw(static_cast<int>(width))
        << std::setfill('0') << value;
    return out.str();
}

std::string DescribeMemoryState(const std::uint32_t state) {
    switch (state) {
    case MEM_COMMIT:
        return "MEM_COMMIT";
    case MEM_FREE:
        return "MEM_FREE";
    case MEM_RESERVE:
        return "MEM_RESERVE";
    default:
        return FormatHexValue(state);
    }
}

std::string DescribeMemoryType(const std::uint32_t type) {
    switch (type) {
    case MEM_IMAGE:
        return "MEM_IMAGE";
    case MEM_MAPPED:
        return "MEM_MAPPED";
    case MEM_PRIVATE:
        return "MEM_PRIVATE";
    default:
        return type == 0 ? "0" : FormatHexValue(type);
    }
}

std::string DescribeMemoryProtect(const std::uint32_t protect) {
    if (protect == 0) {
        return "0";
    }

    std::vector<std::string> parts;
    const auto base = protect & 0xFFU;
    switch (base) {
    case PAGE_NOACCESS:
        parts.emplace_back("PAGE_NOACCESS");
        break;
    case PAGE_READONLY:
        parts.emplace_back("PAGE_READONLY");
        break;
    case PAGE_READWRITE:
        parts.emplace_back("PAGE_READWRITE");
        break;
    case PAGE_WRITECOPY:
        parts.emplace_back("PAGE_WRITECOPY");
        break;
    case PAGE_EXECUTE:
        parts.emplace_back("PAGE_EXECUTE");
        break;
    case PAGE_EXECUTE_READ:
        parts.emplace_back("PAGE_EXECUTE_READ");
        break;
    case PAGE_EXECUTE_READWRITE:
        parts.emplace_back("PAGE_EXECUTE_READWRITE");
        break;
    case PAGE_EXECUTE_WRITECOPY:
        parts.emplace_back("PAGE_EXECUTE_WRITECOPY");
        break;
    default:
        parts.emplace_back(FormatHexValue(base));
        break;
    }
    if (protect & PAGE_GUARD) {
        parts.emplace_back("PAGE_GUARD");
    }
    if (protect & PAGE_NOCACHE) {
        parts.emplace_back("PAGE_NOCACHE");
    }
    if (protect & PAGE_WRITECOMBINE) {
        parts.emplace_back("PAGE_WRITECOMBINE");
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out << '|';
        }
        out << parts[i];
    }
    return out.str();
}

std::string FormatMemoryRegionSummary(const MemoryRegionInfo& region) {
    std::ostringstream out;
    out
        << "space=" << ToString(region.address_space)
        << " input=" << FormatHexValue(region.input_address)
        << " resolved=" << FormatHexValue(region.resolved_address)
        << " base=" << FormatHexValue(region.base)
        << " alloc_base=" << FormatHexValue(region.allocation_base)
        << " size=" << FormatHexValue(region.region_size)
        << " state=" << DescribeMemoryState(region.state)
        << " type=" << DescribeMemoryType(region.type)
        << " protect=" << DescribeMemoryProtect(region.protect)
        << " readable=" << (region.readable ? 1 : 0)
        << " writable=" << (region.writable ? 1 : 0)
        << " executable=" << (region.executable ? 1 : 0);
    return out.str();
}

bool EncodeScalarValue(
    const MemoryScalarType type,
    const std::string_view text,
    std::vector<std::uint8_t>* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "scalar bytes output pointer is null";
        }
        return false;
    }

    switch (type) {
    case MemoryScalarType::u8: {
        std::uint64_t value = 0;
        if (!ParseUnsignedInteger(text, &value) || value > 0xFFU) {
            if (error) {
                *error = "u8 value is out of range";
            }
            return false;
        }
        CopyScalarBytes(static_cast<std::uint8_t>(value), out);
        return true;
    }
    case MemoryScalarType::u16: {
        std::uint64_t value = 0;
        if (!ParseUnsignedInteger(text, &value) || value > 0xFFFFU) {
            if (error) {
                *error = "u16 value is out of range";
            }
            return false;
        }
        CopyScalarBytes(static_cast<std::uint16_t>(value), out);
        return true;
    }
    case MemoryScalarType::u32:
    case MemoryScalarType::ptr: {
        std::uint64_t value = 0;
        if (!ParseUnsignedInteger(text, &value) || value > 0xFFFFFFFFULL) {
            if (error) {
                *error = "u32/ptr value is out of range";
            }
            return false;
        }
        CopyScalarBytes(static_cast<std::uint32_t>(value), out);
        return true;
    }
    case MemoryScalarType::u64: {
        std::uint64_t value = 0;
        if (!ParseUnsignedInteger(text, &value)) {
            if (error) {
                *error = "u64 value is invalid";
            }
            return false;
        }
        CopyScalarBytes(value, out);
        return true;
    }
    case MemoryScalarType::i32: {
        std::int64_t value = 0;
        if (!ParseSignedInteger(text, &value) ||
            value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) {
            if (error) {
                *error = "i32 value is out of range";
            }
            return false;
        }
        CopyScalarBytes(static_cast<std::int32_t>(value), out);
        return true;
    }
    case MemoryScalarType::f32: {
        const std::string owned(text);
        char* end = nullptr;
        const float value = std::strtof(owned.c_str(), &end);
        if (!end || *end != '\0') {
            if (error) {
                *error = "f32 value is invalid";
            }
            return false;
        }
        CopyScalarBytes(value, out);
        return true;
    }
    case MemoryScalarType::f64: {
        const std::string owned(text);
        char* end = nullptr;
        const double value = std::strtod(owned.c_str(), &end);
        if (!end || *end != '\0') {
            if (error) {
                *error = "f64 value is invalid";
            }
            return false;
        }
        CopyScalarBytes(value, out);
        return true;
    }
    }
    if (error) {
        *error = "unsupported scalar type";
    }
    return false;
}

bool DecodeScalarValue(
    const MemoryScalarType type,
    const std::vector<std::uint8_t>& bytes,
    std::string* out,
    std::string* error) {
    if (!out) {
        if (error) {
            *error = "decoded scalar output pointer is null";
        }
        return false;
    }

    std::ostringstream value;
    switch (type) {
    case MemoryScalarType::u8: {
        const auto parsed = ReadScalarBytes<std::uint8_t>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        value << static_cast<unsigned int>(*parsed);
        *out = value.str();
        return true;
    }
    case MemoryScalarType::u16: {
        const auto parsed = ReadScalarBytes<std::uint16_t>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        value << *parsed;
        *out = value.str();
        return true;
    }
    case MemoryScalarType::u32: {
        const auto parsed = ReadScalarBytes<std::uint32_t>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        value << *parsed;
        *out = value.str();
        return true;
    }
    case MemoryScalarType::u64: {
        const auto parsed = ReadScalarBytes<std::uint64_t>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        value << *parsed;
        *out = value.str();
        return true;
    }
    case MemoryScalarType::i32: {
        const auto parsed = ReadScalarBytes<std::int32_t>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        value << *parsed;
        *out = value.str();
        return true;
    }
    case MemoryScalarType::f32: {
        const auto parsed = ReadScalarBytes<float>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        value << std::setprecision(9) << *parsed;
        *out = value.str();
        return true;
    }
    case MemoryScalarType::f64: {
        const auto parsed = ReadScalarBytes<double>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        value << std::setprecision(17) << *parsed;
        *out = value.str();
        return true;
    }
    case MemoryScalarType::ptr: {
        const auto parsed = ReadScalarBytes<std::uint32_t>(bytes);
        if (!parsed.has_value()) {
            break;
        }
        *out = FormatHexValue(*parsed);
        return true;
    }
    }

    if (error) {
        *error = "scalar byte width does not match requested type";
    }
    return false;
}

}  // namespace pal4::inject

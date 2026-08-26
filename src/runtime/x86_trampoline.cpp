#include "x86_trampoline.h"

#include <cstdint>
#include <cstring>
#include <limits>

namespace pal4::inject {

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
        case 0x33:
        case 0x39:
        case 0x8A:
        case 0x8B:
        case 0x89:
        case 0x8D: {
            const std::size_t len = modrm_length(code + prefix_len, remaining - prefix_len, 1, 0);
            return len == 0 ? 0 : len + prefix_len;
        }
        case 0x80:
        case 0x83: {
            const std::size_t len = modrm_length(code + prefix_len, remaining - prefix_len, 1, 1);
            return len == 0 ? 0 : len + prefix_len;
        }
        case 0x81: {
            const std::size_t len = modrm_length(code + prefix_len, remaining - prefix_len, 1, 4);
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

}  // namespace pal4::inject

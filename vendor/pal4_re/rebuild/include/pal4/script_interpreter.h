#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pal4/evidence_status.h"

namespace pal4 {

struct CSCSBBaseLayout {
    static constexpr std::uint32_t kInitAddress = 0x7DE2C0;
    static constexpr std::uint32_t kBaseVtableEa = 0x86BCB8;
    static constexpr std::size_t kSizeX86 = 20;
    static constexpr EvidenceStatus kEvidenceStatus = EvidenceStatus::verified_in_ida;

    void* vtable = nullptr;   // +0x00
    void* slot_04 = nullptr;  // +0x04: script full path in scriptSetFullPath
    void* slot_08 = nullptr;  // +0x08: script path in scriptSetPath
    void* slot_0C = nullptr;  // +0x0C: script name in scriptSetName
    void* slot_10 = nullptr;  // +0x10: field4 / errorInfo slot (CSB)

    void InitFromCscsbBase() noexcept;
};

class ScriptInterpreter {
public:
    static constexpr std::uint32_t kFactoryAddress = 0x7DE4C0;
    static constexpr std::uint32_t kCreateCsAddress = 0x7E0CE0;
    static constexpr std::uint32_t kSetFullPathAddress = 0x7DE360;
    static constexpr std::uint32_t kSetPathAddress = 0x7DE3B0;
    static constexpr std::uint32_t kSetNameAddress = 0x7DE400;
    static constexpr std::uint32_t kCsVtableEa = 0x86BCE0;
    static constexpr std::size_t kSizeX86 = 28;
    static constexpr EvidenceStatus kEvidenceStatus = EvidenceStatus::partially_verified;

    static ScriptInterpreter* Factory() noexcept;
    static EvidenceStatus LayoutStatus() noexcept { return kEvidenceStatus; }

    ScriptInterpreter* ConstructAsCsInterpreter() noexcept;
    void ResetNamedPathSlots() noexcept;

    char*& FullPath() noexcept;
    char*& Path() noexcept;
    char*& Name() noexcept;

    CSCSBBaseLayout base;
    std::uint32_t field_14 = 0;  // +0x14: this[5] in CS_CreateCSInterpreter
    std::uint32_t field_18 = 0;  // +0x18: this[6] in CS_CreateCSInterpreter
};

struct CSBDataContext {
    static constexpr std::uint32_t kInitAddress = 0x7EC6A0;
    static constexpr std::uint32_t kVtableEa = 0x86BF00;
    static constexpr std::size_t kSizeX86 = 20;

    void* vtable = nullptr;       // +0x00
    void* csb_data = nullptr;     // +0x04
    void* csb_bytecode = nullptr; // +0x08
    void* symbol_table = nullptr; // +0x0C
    void* misc_data = nullptr;    // +0x10

    void InitFromCsbDataInit() noexcept;
};

struct CSBExecutionContext {
    static constexpr std::uint32_t kTypeEvidenceAddress = 0x0;
    static constexpr std::size_t kSizeX86 = 108;
    static constexpr EvidenceStatus kEvidenceStatus = EvidenceStatus::partially_verified;
    static constexpr std::size_t kScriptDataOffset = 0x10;
    static constexpr std::size_t kStateOffset = 0x14;
    static constexpr std::size_t kErrorFlagOffset = 0x18;
    static constexpr std::size_t kCurrentInstructionOffset = 0x1C;
    static constexpr std::size_t kBytecodeStartOffset = 0x20;
    static constexpr std::size_t kStackBaseOffset = 0x24;
    static constexpr std::size_t kExecutionLevelOffset = 0x28;
    static constexpr std::size_t kStackPointerOffset = 0x50;
    static constexpr std::size_t kStackLevelOffset = 0x54;
    static constexpr std::size_t kStackSizeOffset = 0x58;
    static constexpr std::size_t kDebugEnabledOffset = 0x5C;

    void* vtable = nullptr;                   // +0x00
    std::array<std::byte, 12> padding_04{};  // +0x04..0x0F
    void* script_data = nullptr;              // +0x10
    std::int32_t state = 0;                   // +0x14
    std::uint8_t error_flag = 0;              // +0x18
    std::array<std::byte, 3> padding_19{};    // +0x19..0x1B
    void* current_instruction = nullptr;      // +0x1C
    void* bytecode_start = nullptr;           // +0x20
    void* stack_base = nullptr;               // +0x24
    std::uint8_t execution_level = 0;         // +0x28
    std::array<std::byte, 39> padding_29{};   // +0x29..0x4F
    void* stack_pointer = nullptr;            // +0x50
    std::int32_t stack_level = 0;             // +0x54
    std::int32_t stack_size = 0;              // +0x58
    std::uint8_t debug_enabled = 0;           // +0x5C
    std::array<std::byte, 15> padding_5D{};   // +0x5D..0x6B

    void ResetFromEvidence() noexcept;
};

class CSBInterpreter {
public:
    static constexpr std::uint32_t kFactoryAddress = 0x7DE4C0;
    static constexpr std::uint32_t kCreateCsbAddress = 0x7E0E80;
    static constexpr std::uint32_t kCsbVtableEa = 0x86BCF8;
    static constexpr std::size_t kSizeX86 = 40;
    static constexpr EvidenceStatus kEvidenceStatus = EvidenceStatus::partially_verified;

    static CSBInterpreter* Factory() noexcept;
    static EvidenceStatus LayoutStatus() noexcept { return kEvidenceStatus; }

    CSBInterpreter* ConstructAsCsbInterpreter() noexcept;
    void ResetNamedPathSlots() noexcept;
    void ClearContext() noexcept;

    char*& FullPath() noexcept;
    char*& Path() noexcept;
    char*& Name() noexcept;
    void*& ErrorInfo() noexcept;

    CSCSBBaseLayout base;
    CSBDataContext csb_context;
};

static_assert(sizeof(CSCSBBaseLayout) == CSCSBBaseLayout::kSizeX86, "CSCSBBaseLayout size mismatch");
static_assert(sizeof(CSBDataContext) == CSBDataContext::kSizeX86, "CSBDataContext size mismatch");
static_assert(sizeof(CSBExecutionContext) == CSBExecutionContext::kSizeX86, "CSBExecutionContext size mismatch");
static_assert(offsetof(CSBExecutionContext, script_data) == CSBExecutionContext::kScriptDataOffset, "script_data offset mismatch");
static_assert(offsetof(CSBExecutionContext, state) == CSBExecutionContext::kStateOffset, "state offset mismatch");
static_assert(offsetof(CSBExecutionContext, error_flag) == CSBExecutionContext::kErrorFlagOffset, "error_flag offset mismatch");
static_assert(offsetof(CSBExecutionContext, current_instruction) == CSBExecutionContext::kCurrentInstructionOffset, "current_instruction offset mismatch");
static_assert(offsetof(CSBExecutionContext, bytecode_start) == CSBExecutionContext::kBytecodeStartOffset, "bytecode_start offset mismatch");
static_assert(offsetof(CSBExecutionContext, stack_base) == CSBExecutionContext::kStackBaseOffset, "stack_base offset mismatch");
static_assert(offsetof(CSBExecutionContext, execution_level) == CSBExecutionContext::kExecutionLevelOffset, "execution_level offset mismatch");
static_assert(offsetof(CSBExecutionContext, stack_pointer) == CSBExecutionContext::kStackPointerOffset, "stack_pointer offset mismatch");
static_assert(offsetof(CSBExecutionContext, stack_level) == CSBExecutionContext::kStackLevelOffset, "stack_level offset mismatch");
static_assert(offsetof(CSBExecutionContext, stack_size) == CSBExecutionContext::kStackSizeOffset, "stack_size offset mismatch");
static_assert(offsetof(CSBExecutionContext, debug_enabled) == CSBExecutionContext::kDebugEnabledOffset, "debug_enabled offset mismatch");
static_assert(sizeof(ScriptInterpreter) == ScriptInterpreter::kSizeX86, "ScriptInterpreter size mismatch");
static_assert(sizeof(CSBInterpreter) == CSBInterpreter::kSizeX86, "CSBInterpreter size mismatch");

}  // namespace pal4

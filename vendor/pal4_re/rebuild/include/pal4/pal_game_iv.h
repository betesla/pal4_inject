#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "pal4/evidence_status.h"

namespace pal4 {

struct PalGameIvStateCatalogEntry {
    std::string_view paliv_label;
    int token = -1;
    std::uint32_t entry_ea = 0;
    EvidenceStatus status = EvidenceStatus::hypothesis;
    std::string_view notes;
};

struct PalGameIvStateCatalogSnapshot {
    bool label_registration_ready = false;
    bool current_state_slot_ready = false;
    bool state_collection_slot_ready = false;
    bool logo_path_known = false;
    bool scene_path_known = false;
    bool system_path_known = false;
    bool combat_path_known = false;
    bool trade_path_known = false;
    bool foundry_path_known = false;
    std::size_t catalog_entry_count = 0;
    EvidenceStatus status = EvidenceStatus::hypothesis;
};

class PALGameIV {
public:
    static constexpr std::uint32_t kGetInstanceAddress = 0x5B5AF0;
    static constexpr std::uint32_t kInstanceFactoryAddress = 0x5B5B30;
    static constexpr std::uint32_t kConstructorAddress = 0x5B5BB0;
    static constexpr std::uint32_t kDestructorAddress = 0x5B5B90;
    static constexpr std::uint32_t kFullDestructorAddress = 0x5B5D30;
    static constexpr std::size_t kSizeX86 = 444;
    static constexpr std::uint32_t kSingletonId = 1046;
    static constexpr std::uint32_t kInstancePointerEa = 0x8FDEDC;
    static constexpr std::uint32_t kCacheFlagEa = 0x8FDEE0;
    static constexpr std::size_t kBoneRegionOffset = 0x174;
    static constexpr std::size_t kAuxRegionOffset = 0x18C;
    static constexpr std::size_t kRwTexDictionaryOffset = 0x1A8;

    static PALGameIV* GetInstance() noexcept;
    static PALGameIV* InstanceFactory() noexcept;
    static EvidenceStatus LayoutStatus() noexcept;
    static EvidenceStatus StateCatalogStatus() noexcept;
    static std::size_t StateInitializedFlagIndex() noexcept;
    static std::size_t StateCollectionIndex() noexcept;
    static std::size_t CurrentStateEntryIndex() noexcept;
    static std::size_t KnownStateCatalogCount() noexcept;
    static const std::array<PalGameIvStateCatalogEntry, 6>& KnownStateCatalog() noexcept;
    static std::optional<PalGameIvStateCatalogEntry> FindStateCatalogEntryByToken(int token) noexcept;
    static std::optional<PalGameIvStateCatalogEntry> FindStateCatalogEntryByLabel(std::string_view label) noexcept;
    static std::string_view ResolvePalivLabel(int token) noexcept;
    static PalGameIvStateCatalogSnapshot BuildStateCatalogSnapshot() noexcept;

    PALGameIV* Construct() noexcept;
    void Destruct() noexcept;
    void ClearBoneRegion() noexcept;
    void ClearAuxRegion() noexcept;
    void ClearRwTexDictionary() noexcept;

    void* vftable = nullptr;
    std::array<std::byte, 0x170 - 4> gap_004{};
    std::uint32_t field_170 = 0;
    std::array<std::byte, 0x184 - 0x174> bone_region_174{};
    std::uint32_t field_184 = 0;
    std::uint32_t field_188 = 0;
    std::array<std::byte, 0x198 - 0x18C> aux_object_18C{};
    std::uint8_t flag_198 = 0;
    std::array<std::byte, 3> pad_199{};
    std::uint32_t field_19C = 0;
    std::uint32_t field_1A0 = 0;
    std::uint32_t field_1A4 = 0;
    std::array<std::byte, kSizeX86 - 0x1A8> rw_tex_dictionary_1A8{};
};

static_assert(sizeof(PALGameIV) == PALGameIV::kSizeX86, "PALGameIV shell must match IDA-backed x86 size");

}  // namespace pal4

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pal4/evidence_status.h"
#include "pal4/runtime_config.h"

namespace pal4 {

struct PAL4CeguiSize {
    float width = 0.0f;
    float height = 0.0f;
};

struct PAL4CeguiRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct PAL4CeguiRendererSlotOverride {
    int slot = -1;
    std::string_view name{};
    bool planned = false;
};

struct PAL4CeguiRendererMetricsOverridePlan {
    std::uint32_t renderer_ctor_ea = 0;
    std::uint32_t renderer_vtable_ea = 0;
    std::array<PAL4CeguiRendererSlotOverride, 5> slots{};
};

struct PAL4CeguiRendererCriticalSlotPlan {
    std::array<PAL4CeguiRendererSlotOverride, 5> slots{};
};

struct PAL4CeguiFakeDerivedMetricsSurface {
    bool queueing_enabled = true;
    PAL4CeguiSize size{};
    PAL4CeguiRect rect{};
};

struct PAL4CeguiFakeDerivedSlotBehavior {
    int slot = -1;
    std::string_view name{};
    bool implemented = false;
};

struct PAL4CeguiFakeDerivedBindingExperiment {
    std::string_view name{};
    bool ready = false;
    std::array<PAL4CeguiRendererSlotOverride, 5> target_slots{};
};

struct PAL4CeguiFakeDerivedSlotInvocation {
    int slot = -1;
    bool success = false;
    bool bool_value = false;
    float float_value = 0.0f;
    PAL4CeguiSize size_value{};
    PAL4CeguiRect rect_value{};
};

struct PAL4CeguiSyntheticVtableEntry {
    int slot = -1;
    std::string_view name{};
    std::uintptr_t synthetic_target = 0;
};

struct PAL4CeguiSyntheticVtableBinding {
    std::uintptr_t synthetic_vtable_tag = 0;
    std::array<PAL4CeguiSyntheticVtableEntry, 5> entries{};
};

struct PAL4CeguiSyntheticVtableImage {
    static constexpr std::size_t kSlotCount = 20;
    std::array<std::uintptr_t, kSlotCount> slots{};
};

struct PAL4CeguiSyntheticObjectImage {
    std::uintptr_t synthetic_vtable_ptr = 0;
    float width = 0.0f;
    float height = 0.0f;
    PAL4CeguiRect rect{};
};

struct PAL4CeguiBindingExperimentReport {
    bool ready = false;
    int successful_slots = 0;
    std::array<PAL4CeguiFakeDerivedSlotInvocation, 5> invocations{};
};

class PAL4CeguiRenderer {
public:
    static constexpr std::uint32_t kConstructorAddress = 0x413270;
    static constexpr std::uint32_t kVtableEa = 0x841EC8;
    static constexpr std::size_t kSizeX86 = 0x110;
    static constexpr std::string_view kDefaultIdentifier = "Unknown renderer (vendor did not set the ID string!)";
    static constexpr std::size_t kVertexBufferOffset = 0xBC;
    static constexpr std::size_t kDisplayHeightOffset = 0xF8;
    static constexpr std::size_t kDisplayWidthOffset = 0x100;
    static constexpr std::size_t kFlagsRegionOffset = 0xB8;
    static constexpr std::size_t kListHeadOffset = 0xEC;

    static EvidenceStatus LayoutStatus() noexcept;

    PAL4CeguiRenderer* Construct(const GameConfigSnapshot& config) noexcept;
    float Width() const noexcept;
    float Height() const noexcept;
    PAL4CeguiSize Size() const noexcept;
    PAL4CeguiRect Rect() const noexcept;
    std::string_view IdentifierString() const noexcept;

    void* vftable = nullptr;
    std::array<std::byte, kFlagsRegionOffset - 4> gap_004{};
    std::uint8_t flag_b8 = 0;
    std::array<std::byte, 3> pad_b9{};
    std::uint32_t vertex_begin_bc = 0;
    std::uint32_t vertex_cur_c0 = 0;
    std::uint32_t vertex_end_c4 = 0;
    std::uint8_t flag_c8 = 0;
    std::array<std::byte, 3> pad_c9{};
    std::uint32_t index_begin_cc = 0;
    std::uint32_t index_cur_d0 = 0;
    std::uint32_t index_end_d4 = 0;
    std::uint8_t flag_d8 = 0;
    std::array<std::byte, 3> pad_d9{};
    std::uint32_t wide_begin_dc = 0;
    std::uint32_t wide_cur_e0 = 0;
    std::uint32_t wide_end_e4 = 0;
    std::array<std::byte, 4> gap_e8{};
    std::uint32_t list_head_ec = 0;
    std::uint32_t list_count_f0 = 0;
    std::uint32_t reserved_f4 = 0;
    float display_height_f8 = 0.0f;
    std::uint32_t reserved_fc = 0;
    float display_width_100 = 0.0f;
    std::array<std::byte, kSizeX86 - 0x104> tail_104{};
};

class PAL4CeguiFakeDerivedRenderer : public PAL4CeguiRenderer {
public:
    static constexpr std::uint32_t kSyntheticVtableTag = 0xF4A6E001;

    PAL4CeguiFakeDerivedRenderer* ConstructFakeDerived(const GameConfigSnapshot& config) noexcept;
    bool QueueingEnabled() const noexcept;
    float SlotGetWidth() const noexcept;
    float SlotGetHeight() const noexcept;
    PAL4CeguiSize SlotGetSize() const noexcept;
    PAL4CeguiRect SlotGetRect() const noexcept;
    PAL4CeguiFakeDerivedMetricsSurface MetricsSurface() const noexcept;
    std::array<PAL4CeguiFakeDerivedSlotBehavior, 5> SlotBehaviors() const noexcept;
    PAL4CeguiFakeDerivedBindingExperiment BindingExperiment() const noexcept;
    PAL4CeguiFakeDerivedSlotInvocation InvokeCriticalSlot(int slot) const noexcept;
    std::array<PAL4CeguiFakeDerivedSlotInvocation, 5> CriticalSlotInvocations() const noexcept;
    PAL4CeguiSyntheticVtableBinding SyntheticBinding() const noexcept;
    PAL4CeguiSyntheticVtableImage SyntheticVtableImage() const noexcept;
    PAL4CeguiSyntheticObjectImage SyntheticObjectImage() const noexcept;
    PAL4CeguiBindingExperimentReport BindingExperimentReport() const noexcept;

    bool queueing_enabled = true;
    PAL4CeguiRendererCriticalSlotPlan critical_plan{};
};

class PAL4CeguiRenderer1024x768 : public PAL4CeguiRenderer {
public:
    static constexpr std::uint32_t kConstructorAddress = 0x413580;
    static constexpr std::uint32_t kVtableEa = 0x841F40;
    static constexpr std::size_t kSizeX86 = 0x118;
    static constexpr std::size_t kScaleXOffset = 0x110;
    static constexpr std::size_t kScaleYOffset = 0x114;

    static EvidenceStatus LayoutStatus() noexcept;

    PAL4CeguiRenderer1024x768* ConstructVariant(const GameConfigSnapshot& config) noexcept;

    union {
        float scale_x_110 = 0.0f;
        // Compatibility alias for older tests; IDA still places this slot at 0x110.
        float aspect_scale_x_118;
    };
    union {
        float scale_y_114 = 0.0f;
        // Compatibility alias for older tests; IDA still places this slot at 0x114.
        float aspect_scale_y_11C;
    };
};

class PAL4CeguiRenderer1280x960 : public PAL4CeguiRenderer1024x768 {
public:
    static constexpr std::uint32_t kConstructorAddress = 0x413620;
    static constexpr std::uint32_t kVtableEa = 0x842020;
    static constexpr std::size_t kSizeX86 = 0x120;
    static constexpr std::size_t kVariantScaleXOffset = 0x118;
    static constexpr std::size_t kVariantScaleYOffset = 0x11C;

    static EvidenceStatus LayoutStatus() noexcept;

    PAL4CeguiRenderer1280x960* ConstructVariant(const GameConfigSnapshot& config) noexcept;

    float variant_scale_x_118 = 0.0f;
    float variant_scale_y_11C = 0.0f;
};

class PAL4CeguiRenderer1280x800 : public PAL4CeguiRenderer1024x768 {
public:
    static constexpr std::uint32_t kConstructorAddress = 0x413670;
    static constexpr std::uint32_t kVtableEa = 0x842090;
    static constexpr std::size_t kSizeX86 = 0x12C;
    static constexpr std::size_t kAspectScaleXOffset = 0x110;
    static constexpr std::size_t kAspectScaleYOffset = 0x114;
    static constexpr std::size_t kViewportBiasOffset = 0x118;
    static constexpr std::size_t kViewportRectOffset = 0x11C;

    static EvidenceStatus LayoutStatus() noexcept;

    PAL4CeguiRenderer1280x800* ConstructVariant(const GameConfigSnapshot& config) noexcept;

    float viewport_bias_118 = 0.0f;
    // Partial semantics: IDA shows a 16-byte copy from +0xF4 into +0x11C,
    // then bias applied to the last two floats.
    PAL4CeguiRect viewport_rect_11C{};
};

struct PAL4CeguiRendererSelection {
    std::uint32_t ctor_ea = PAL4CeguiRenderer::kConstructorAddress;
    std::size_t object_size = PAL4CeguiRenderer::kSizeX86;
    EvidenceStatus status = EvidenceStatus::partially_verified;
};

PAL4CeguiRendererSelection SelectPAL4CeguiRenderer(const GameConfigSnapshot& config) noexcept;
std::uint32_t ResolvePAL4CeguiRendererVtableEa(std::uint32_t ctor_ea) noexcept;
PAL4CeguiRendererMetricsOverridePlan BuildPAL4CeguiRendererMetricsOverridePlan(const GameConfigSnapshot& config) noexcept;
PAL4CeguiRendererCriticalSlotPlan BuildPAL4CeguiRendererCriticalSlotPlan() noexcept;

}  // namespace pal4

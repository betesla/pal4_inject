#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pal4/cegui_renderer.h"
#include "pal4/evidence_status.h"
#include "pal4/runtime_config.h"

namespace pal4 {

class PAL4CeguiSystemWrapper {
public:
    static constexpr std::uint32_t kInitializeAddress = 0x410450;
    static constexpr std::size_t kSizeX86 = 0x20;
    static constexpr std::size_t kObjectRegionOffset = 0x4;
    static constexpr std::size_t kScaleXOffset = 0x14;
    static constexpr std::size_t kScaleYOffset = 0x18;
    static constexpr std::size_t kScaleBiasOffset = 0x1C;
    static constexpr std::size_t kAllocatedSystemSize = 0x1B0;
    static constexpr const char* kLogName = "CEGUI.log";

    static EvidenceStatus LayoutStatus() noexcept;

    PAL4CeguiSystemWrapper* Construct(
        const GameConfigSnapshot& config,
        const PAL4CeguiRendererSelection& renderer,
        bool has_resource_provider) noexcept;

    std::uint8_t initialized_flag = 0;
    std::array<std::byte, 3> pad_001{};
    std::array<std::byte, 0x10> object_region_004{};
    float scale_x_014 = 0.0f;
    float scale_y_018 = 0.0f;
    float scale_bias_01C = 0.0f;
};

struct PAL4CeguiSystemInitResult {
    std::uint32_t renderer_ctor_ea = 0;
    std::size_t renderer_size = 0;
    bool has_resource_provider = false;
    bool uses_special_1280x800_scaling = false;
    float scale_x = 0.0f;
    float scale_y = 0.0f;
    float scale_bias = 0.0f;
};

struct PAL4CeguiSystemProbeSupport {
    bool renderer_construct_ready = false;
    bool system_ctor_export_ready = false;
    bool system_ctor_completed = false;
    bool original_ctor_return_reached = false;
    bool original_initialize_cegui_returned_to_caller = false;
    bool system_singleton_ready = false;
    bool set_default_font_ready = false;
    bool default_window_factory_ready = false;
    bool desktop_creation_ready = false;
    bool gui_sheet_ready = false;
    bool font_resource_load_ready = false;
    bool font_runtime_load_ready = false;
    bool system_font_missing_from_log = false;
    bool system_font_load_failed_from_log = false;
    bool system_font_get_missing_from_log = false;
    bool imageset_runtime_available = false;
    bool oiramlook_imageset_missing_from_log = false;
    bool oiramlook_widget_factory_missing_from_log = false;
    bool oiramlook_load_scheme_failed = false;
    bool oiramlook_create_imageset_failed = false;
    bool oiramlook_get_imageset_failed = false;
    bool oiramlook_widget_ready = false;
    bool original_get_singleton_ptr_non_null = false;
    bool original_get_singleton_ptr_matches_ctor_object = false;
};

struct PAL4CeguiSystemChainSnapshot {
    bool renderer_construct_ready = false;
    bool system_ctor_export_ready = false;
    bool system_ctor_completed = false;
    bool original_ctor_return_reached = false;
    bool original_initialize_cegui_returned_to_caller = false;
    bool system_singleton_ready = false;
    bool set_uv_alignment_ready = false;
    bool set_default_font_ready = false;
    bool default_window_factory_ready = false;
    bool desktop_creation_ready = false;
    bool gui_sheet_ready = false;
    bool font_resource_load_ready = false;
    bool font_runtime_load_ready = false;
    const char* font_runtime_blocker_detail = "";
    bool imageset_runtime_available = false;
    const char* imageset_runtime_blocker_detail = "";
    bool oiramlook_widget_ready = false;
    const char* blocker_detail = "";
    const char* blocker = "";
};

struct PAL4CeguiSingletonPublishSnapshot {
    bool system_ctor_completed = false;
    bool original_ctor_return_reached = false;
    bool original_initialize_cegui_returned_to_caller = false;
    bool ui_manager_wrapper_slot_written = false;
    bool logger_head_zeroed = false;
    bool scale_fields_written = false;
    bool immediate_get_singleton_ptr_after_slot_write = false;
    bool no_explicit_exe_publish_step_observed = false;
    bool singleton_getter_import_available = false;
    bool explicit_singleton_publish_import_observed = false;
    bool original_get_singleton_ptr_non_null = false;
    bool original_get_singleton_ptr_matches_ctor_object = false;
    bool original_get_singleton_ptr_differs_from_ui_manager_wrapper = false;
    bool exe_receives_wrapper_not_singleton = false;
    bool dll_ctor_must_self_publish_singleton = false;
    bool singleton_visible = false;
    const char* blocker = "";
};

PAL4CeguiSystemInitResult DescribePAL4CeguiSystemInit(
    const GameConfigSnapshot& config,
    bool has_resource_provider) noexcept;
PAL4CeguiSystemChainSnapshot DerivePAL4CeguiSystemChain(
    const PAL4CeguiSystemProbeSupport& support) noexcept;
PAL4CeguiSingletonPublishSnapshot DerivePAL4CeguiSingletonPublish(
    const PAL4CeguiSystemProbeSupport& support,
    const PAL4CeguiSystemChainSnapshot& chain) noexcept;

}  // namespace pal4

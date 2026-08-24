#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pal4/evidence_status.h"
#include "pal4/ui_resource_loader.h"

namespace pal4 {

enum class CeguiWindowManagerBackendKind : int;
const char* ToString(CeguiWindowManagerBackendKind kind) noexcept;

enum class UiLayoutRootResolution {
    none,
    explicit_root_name,
    unique_top_level,
    first_top_level,
    first_window,
};

struct UiLayoutInstance {
    std::string source_layout;
    std::string alias_name;
    std::string group_name;
    std::string root_name;
    std::string root_type;
    std::size_t window_count = 0;
    std::vector<ParsedCeguiWindowNode> windows;
    std::unordered_map<std::string, std::size_t> window_name_to_index;
    std::vector<std::size_t> top_level_indices;
    std::optional<std::size_t> root_index;
    UiLayoutRootResolution root_resolution = UiLayoutRootResolution::none;
    std::string new_game_window_name;
    std::string exit_window_name;
    bool has_new_game = false;
    bool has_exit = false;
};

struct UiWindowObject {
    std::string name;
    std::string type;
    std::size_t source_index = 0;
    std::optional<std::size_t> parent_index;
    std::vector<std::size_t> child_indices;
};

struct UiLayoutAttachEdge {
    std::size_t parent_index = 0;
    std::size_t child_index = 0;
};

struct UiWindowCreationStep {
    std::size_t node_index = 0;
    std::string window_name;
    std::string window_type;
    std::optional<std::size_t> parent_index;
    bool is_root = false;
    bool attach_after_create = false;
};

struct UiWindowCreationPlan {
    std::string source_layout;
    std::optional<std::size_t> root_index;
    std::vector<UiWindowCreationStep> steps;
    std::size_t attach_edge_count = 0;
    bool has_new_game_target = false;
    bool has_exit_target = false;
};

struct UiCreatedWindowNode {
    std::size_t node_index = 0;
    std::string window_name;
    std::string window_type;
    std::optional<std::size_t> parent_index;
    bool created = false;
    bool attached = false;
    bool is_root = false;
};

struct UiWindowRuntimeState {
    std::string source_layout;
    std::optional<std::size_t> root_index;
    std::size_t created_window_count = 0;
    std::size_t attached_window_count = 0;
    bool root_created = false;
    bool root_attached_to_gui_sheet = false;
    bool new_game_target_created = false;
    bool exit_target_created = false;
    std::vector<UiCreatedWindowNode> nodes;
};

struct UiWindowManagerBridgeState {
    std::string source_layout;
    CeguiWindowManagerBackendKind backend_kind;
    bool backend_ready = false;
    bool can_create_window = false;
    bool can_add_child_window = false;
    bool can_set_gui_sheet = false;
    std::string backend_blocker;
    std::size_t create_window_call_count = 0;
    std::size_t add_child_call_count = 0;
    bool root_window_created = false;
    bool gui_sheet_set = false;
    std::string gui_sheet_root_name;
    bool new_game_target_available = false;
    bool exit_target_available = false;
};

enum class UiWindowManagerOperationKind {
    create_window,
    add_child_window,
    set_gui_sheet,
};

struct UiWindowManagerOperation {
    UiWindowManagerOperationKind kind = UiWindowManagerOperationKind::create_window;
    std::string window_name;
    std::string window_type;
    std::optional<std::size_t> node_index;
    std::optional<std::size_t> parent_index;
};

struct UiWindowManagerExecutionTrace {
    std::string source_layout;
    std::vector<UiWindowManagerOperation> operations;
    bool root_create_seen = false;
    bool gui_sheet_set_seen = false;
    std::size_t create_window_count = 0;
    std::size_t add_child_count = 0;
};

struct UiLayoutObjectTree {
    std::string source_layout;
    std::vector<UiWindowObject> nodes;
    std::unordered_map<std::string, std::size_t> node_name_to_index;
    std::vector<std::size_t> top_level_indices;
    std::optional<std::size_t> root_index;
    UiLayoutRootResolution root_resolution = UiLayoutRootResolution::none;
    std::vector<std::size_t> creation_order;
    std::vector<UiLayoutAttachEdge> attach_edges;
    UiWindowCreationPlan creation_plan;
    std::string new_game_window_name;
    std::string exit_window_name;
    std::optional<std::size_t> new_game_index;
    std::optional<std::size_t> exit_index;
    bool has_new_game = false;
    bool has_exit = false;
};

UiLayoutInstance BuildUiLayoutInstance(const LoadedLayoutResource& resource);
UiLayoutObjectTree BuildUiLayoutObjectTree(const UiLayoutInstance& instance);

struct UiGuiSheetState {
    bool desktop_created = false;
    std::string desktop_name;
    std::string desktop_type;
    bool gui_sheet_attached = false;
    std::optional<std::size_t> attached_layout_index;
    std::string attached_layout_name;
    std::string attached_root_name;
};

struct UiEventTargetBinding {
    std::string action_name;
    std::string layout_name;
    std::string window_name;
    std::optional<std::size_t> window_index;
    std::uint32_t handler_ea = 0;
};

struct UiStaticImageBinding {
    std::string layout_name;
    std::string window_name;
    std::string widget_type;
    std::optional<std::size_t> window_index;
    std::string imageset_name;
    std::string image_name;
    bool image_bound = false;
};

struct UiDependencyGap {
    std::string owner_kind;
    std::string owner_name;
    std::string dependency_kind;
    std::string dependency_name;
    bool package_path_available = false;
};

struct UiMainMenuReadinessState {
    bool system_font_resource_selected = false;
    bool system_font_dependency_files_ready = false;
    bool oiramlook_imageset_resource_selected = false;
    bool oiramlook_imageset_image_file_ready = false;
    bool oiramlook_scheme_resource_selected = false;
    bool oiramlook_scheme_file_dependencies_ready = false;
    bool oiramlook_scheme_fonts_ready = false;
    bool oiramlook_scheme_imagesets_ready = false;
    bool main_window_layout_parsed = false;
    bool menu_window_layout_parsed = false;
    bool main_window_targets_identified = false;
    bool desktop_ready = false;
    bool gui_sheet_ready = false;
    bool desktop_gui_chain_reached = false;
    bool font_runtime_load_ready = false;
    bool imageset_runtime_available = false;
    bool oiramlook_imageset_runtime_probe_ready = false;
    bool oiramlook_widget_prerequisites_ready = false;
    bool main_menu_resource_stack_ready = false;
    bool real_main_menu_ready = false;
    UiRuntimeResourceBlocker system_font_runtime_blocker = UiRuntimeResourceBlocker::none;
    std::string system_font_runtime_primary_blocker;
    UiRuntimeResourceBlocker oiramlook_imageset_runtime_blocker = UiRuntimeResourceBlocker::none;
    UiRuntimeResourceBlocker oiramlook_scheme_runtime_blocker = UiRuntimeResourceBlocker::none;
    std::string oiramlook_imageset_runtime_primary_blocker;
    std::string oiramlook_scheme_primary_blocker;
    std::string primary_blocker;
    std::vector<std::string> blockers;
    std::vector<UiDependencyGap> dependency_gaps;
};

struct UiFrameRegistrationStep {
    std::size_t ordinal = 0;
    std::string frame_name;
    std::uint32_t register_ui_windows_ea = 0;
    std::uint32_t register_frame_ea = 0;
    bool singleton_manager_catalog_registration = false;
    bool inserted_into_rebuild_catalog = false;
};

struct UiFrameRegistrationState {
    bool register_ui_windows_called = false;
    bool singleton_manager_catalog_modeled = false;
    bool catalog_population_complete = false;
    std::size_t invocation_count = 0;
    std::size_t expected_frame_count = 0;
    std::size_t inserted_frame_count = 0;
    std::vector<UiFrameRegistrationStep> steps;
};

struct UiInitEnvironment {
    std::string skin_path;
    bool load_skin = false;
    bool skin_path_modeled = false;
    bool package_mode = true;
    std::size_t package_init_count = 0;
    std::size_t package_enumeration_count = 0;
    int runtime_width = 0;
    int runtime_height = 0;
    bool ida_resource_enumeration_order_verified = false;
    bool game_state_ui_resource_gate_modeled = false;
    bool game_state_ui_resource_gate_enabled = true;
    bool game_state_ui_resource_gate_allows_enumeration = true;
    bool window_manager_frame_slot_modeled = false;
    bool window_manager_frame_slot_written = false;
    std::uint32_t window_manager_frame_slot_value = 0;
    bool real_window_manager_backend_taken_over = false;
    bool simulated_window_manager_replay_active = false;
    bool cursor_package_processed = false;
    std::vector<std::string> cursor_resources;
    bool cursor_ready = false;
    std::string cursor_name;
};

std::optional<std::size_t> SelectAttachedLayoutIndex(const std::vector<UiLayoutInstance>& instances) noexcept;
std::vector<UiEventTargetBinding> BuildUiEventBindings(
    const std::vector<UiLayoutObjectTree>& trees,
    std::optional<std::size_t> attached_index);
std::vector<UiStaticImageBinding> BuildUiStaticImageBindings(
    const std::vector<UiLayoutObjectTree>& trees,
    std::optional<std::size_t> attached_index,
    const InitializeUiFrameManagerResourcesResult& resources);
std::optional<std::string> ResolveUiCursorResource(
    const UiInitEnvironment& environment,
    std::string_view requested_cursor_name);

enum class UiInitStage {
    none,
    skin_path,
    font_enum,
    imageset_enum,
    scheme_enum,
    sequence_enum,
    layout_load,
    desktop_attach,
};

struct UiInitStageProgress {
    UiInitStage stage = UiInitStage::none;
    std::size_t enumerated_count = 0;
    std::size_t attempted_count = 0;
    std::size_t loaded_count = 0;
    std::size_t registered_count = 0;
    bool complete = false;
};

class uiFrameManager {
public:
    static constexpr std::uint32_t kGetInstanceAddress = 0x4BB650;
    static constexpr std::uint32_t kThinWrapperAddress = 0x44A240;
    static constexpr std::uint32_t kInstanceFactoryAddress = 0x4BB690;
    static constexpr std::uint32_t kConstructorAddress = 0x4BBD00;
    static constexpr std::uint32_t kCleanupAddress = 0x4BF2E0;
    static constexpr std::uint32_t kInitializeResourcesAddress = 0x4BDEF0;
    static constexpr std::uint32_t kRegisterLikeMethodAddress = 0x4BEFF0;
    static constexpr std::size_t kSizeX86 = 548;
    static constexpr std::uint32_t kSingletonId = 1023;
    static constexpr std::uint32_t kInstancePointerEa = 0x8EDFB0;
    static constexpr std::uint32_t kCacheFlagEa = 0x8EDFB4;
    static constexpr std::size_t kVerifiedRegionOffset = 0x104;
    static constexpr std::size_t kVectorRegionOffset = 0x184;
    static constexpr std::size_t kVectorRegionSize = 0x14;
    static constexpr std::size_t kPointerRegionOffset = 0x198;
    static constexpr std::size_t kPointerRegionSize = 0x30;
    static constexpr std::size_t kSlotRegionOffset = 0x1A4;
    static constexpr std::size_t kSlotRegionSize = 0x20;
    static constexpr std::size_t kTailFlagsOffset = 0x21C;

    static uiFrameManager* GetInstance() noexcept;
    static uiFrameManager* InstanceFactory() noexcept;
    static EvidenceStatus LayoutStatus() noexcept;
    static EvidenceStatus VerifiedRegionStatus() noexcept;
    using BlockRange = std::pair<std::size_t, std::size_t>;
    static constexpr std::array<BlockRange, 3> VerifiedBlocks() noexcept {
        return {{
            {kVectorRegionOffset, kVectorRegionSize},
            {kPointerRegionOffset, kPointerRegionSize},
            {kSlotRegionOffset, kSlotRegionSize},
        }};
    }

    uiFrameManager* Construct() noexcept;
    void Cleanup() noexcept;
    InitializeUiFrameManagerResourcesResult InitializeUIFrameManager(CpkRepository& repository) noexcept;
    InitializeUiFrameManagerResourcesResult InitializeResources(CpkRepository& repository) noexcept;
    std::size_t InitializeFontResources(CpkRepository& repository) noexcept;
    std::size_t InitializeImagesetResources(CpkRepository& repository) noexcept;
    std::size_t InitializeSchemeResources(CpkRepository& repository) noexcept;
    std::size_t InitializeSequenceImageResources(CpkRepository& repository) noexcept;
    LoadedLayoutResource LoadLayout(CpkRepository& repository, std::string_view short_name, std::string_view alias_name = {}, std::string_view user_hint = {}) noexcept;
    LoadedLayoutResource LoadMainWindowLayout(CpkRepository& repository) noexcept;
    LoadedLayoutResource LoadMenuWindowLayout(CpkRepository& repository) noexcept;
    const InitializeUiFrameManagerResourcesResult& LoadedResources() const noexcept;
    const std::vector<std::string>& RegisteredLayouts() const noexcept;
    const std::vector<std::string>& RegisteredFrames() const noexcept;
    const std::vector<std::string>& RegisteredFonts() const noexcept;
    const std::vector<std::string>& RegisteredImagesets() const noexcept;
    const std::vector<std::string>& RegisteredSchemes() const noexcept;
    const std::vector<std::string>& RegisteredSequenceImages() const noexcept;
    const std::vector<UiLayoutInstance>& LayoutInstances() const noexcept;
    const std::vector<UiLayoutObjectTree>& LayoutObjectTrees() const noexcept;
    const std::vector<UiWindowCreationPlan>& CreationPlans() const noexcept;
    const std::vector<UiWindowRuntimeState>& WindowRuntimeStates() const noexcept;
    const std::vector<UiWindowManagerBridgeState>& WindowManagerBridgeStates() const noexcept;
    const std::vector<UiWindowManagerExecutionTrace>& WindowManagerExecutionTraces() const noexcept;
    const UiGuiSheetState& GuiSheetState() const noexcept;
    const std::vector<UiEventTargetBinding>& EventBindings() const noexcept;
    const std::vector<UiStaticImageBinding>& StaticImageBindings() const noexcept;
    const UiFrameRegistrationState& FrameRegistrationState() const noexcept;
    const UiMainMenuReadinessState& MainMenuReadiness() const noexcept;
    const UiInitEnvironment& InitEnvironment() const noexcept;
    UiInitStage LastInitStage() const noexcept;
    const std::vector<UiInitStageProgress>& InitStageProgress() const noexcept;
    std::size_t RegisterUIWindows() noexcept;
    void SetGameStateUiResourceGateEnabled(bool enabled) noexcept;
    void SetRuntimeResolution(int width, int height) noexcept;
    bool SetCursor(std::string_view cursor_name) noexcept;

    void* vftable = nullptr;
    std::array<std::byte, 0x100 - 4> gap_004{};
    std::array<std::byte, 0x10> rb_tree_100{};
    std::array<std::byte, 0x38> state_block_110{};
    std::array<std::byte, 0x3C> xml_doc_148{};
    std::uint32_t vector_begin_184 = 0;
    std::uint32_t vector_cur_188 = 0;
    std::uint32_t vector_end_18C = 0;
    std::uint32_t vector_capacity_end_190 = 0;
    std::array<std::byte, 4> gap_194{};
    std::uint32_t string_or_buffer_198 = 0;
    std::uint32_t collection_19C = 0;
    std::uint32_t frame_container_1A0 = 0;
    std::uint32_t slot_1A4 = 0;
    std::uint32_t slot_1A8 = 0;
    std::uint32_t slot_1AC = 0;
    std::uint32_t slot_1B0 = 0;
    std::uint32_t slot_1B4 = 0;
    std::uint32_t slot_1B8 = 0;
    std::uint32_t slot_1BC = 0;
    std::uint32_t slot_1C0 = 0;
    std::uint32_t slot_1C4 = 0;
    std::uint32_t slot_1C8 = 0;
    std::uint32_t slot_1CC = 0;
    std::uint32_t slot_1D0 = 0;
    std::array<std::byte, 4> gap_1D4{};
    std::array<std::byte, 0x44> struct_1D8{};
    std::uint8_t flag_21C = 0;
    std::uint8_t flag_21D = 0;
    std::uint8_t pad_21E = 0;
    std::uint8_t flag_21F = 0;
    std::array<std::byte, 4> tail_220{};
};

static_assert(sizeof(uiFrameManager) == uiFrameManager::kSizeX86, "uiFrameManager shell must match IDA-backed x86 size");

}  // namespace pal4

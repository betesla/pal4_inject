#pragma once

#include <optional>
#include <string>

#include "pal4/cegui_window_manager_bridge.h"
#include "pal4/context.h"
#include "pal4/runtime_config.h"

namespace pal4 {

enum class WinMainPhase {
    pre_bootstrap,
    single_instance_exit,
    thread_bootstrap_ready,
    config_initialized,
    window_created,
    render_system_callbacks_bound,
    systems_initialized,
    state_ui_initialized,
    viewport_centered,
    loop_active,
    shutdown_started,
    shutdown_complete,
};

struct WinMainGateSnapshot {
    bool quit_requested = false;
    bool should_pump_messages = false;
    bool window_ready = false;
    int frame_gate_value = 0;
};

struct WinMainLoopSnapshot {
    bool message_pending = false;
    bool quit_message_seen = false;
    bool parameter_latch_ready = true;
    bool frame_gate_seeded = false;
};

struct ThreadBootstrapSnapshot {
    bool thread_count_seeded = false;
    bool worker_threads_spawned = false;
    bool parameter_thread_spawned = false;
    bool thread_ready_wait_completed = false;
    bool randomized_memory_layout_ready = false;
    bool threads_initialized_flag = false;
    bool parameter_zeroed = false;
    int randomized_thread_seed = 1;
    int game_config_offset = 0;
    int game_state_offset = 0;
};

struct SingleInstanceSnapshot {
    bool existing_window_found = false;
    bool existing_window_shown = false;
};

struct PresentationSnapshot {
    bool show_window_called = false;
    bool update_window_called = false;
    bool viewport_center_called = false;
};

struct FirstFrameSnapshot {
    bool frame_callback_bound = false;
    bool state_entry_ready = false;
    bool viewport_centered = false;
    bool frame_update_entered = false;
    bool frame_gate_seeded = false;
    bool parameter_latch_pending = false;
    bool first_frame_ready = false;
};

struct RuntimeFrameSnapshot {
    bool render_frame_callback_seen = false;
    bool video_mode_gate_seen = false;
    bool camera_audio_update_seen = false;
    bool camera_begin_and_scene_draw_seen = false;
    bool overlay_and_present_seen = false;
    bool device_loss_tail_seen = false;
    int render_frame_stage_count = 0;
    bool stable_frame_loop_ready = false;
};

struct ShutdownSnapshot {
    bool game_manager_shutdown_called = false;
    bool render_system_cleanup_called = false;
    bool finalize_game_exit_called = false;
    bool window_not_ready_after_shutdown = false;
    int shutdown_stage_count = 0;
};

struct StateLifecycleSnapshot {
    bool state_collection_lookup_ready = false;
    bool previous_state_leave_ready = false;
    int previous_state_token = 13;
    bool previous_state_token_from_current_state = false;
    bool previous_state_token_passed_to_enter = false;
    int current_state_token = 13;
    bool target_state_enter_ready = false;
    bool target_state_enter_failed = false;
    bool current_state_preserved_on_enter_fail = false;
    bool current_state_active = false;
    bool shutdown_gate_ready = false;
    bool transition_requested = false;
    bool transition_consumed = false;
    std::string transition_target_label;
    std::string transition_target_symbol;
    bool transition_target_resolved = false;
    std::string resolved_target_label;
    std::uint32_t resolved_target_entry_ea = 0;
    int enter_arg2_value = 0;
    bool enter_arg2_nonzero = false;
    bool enter_arg2_from_evidence = false;
    bool enter_arg2_pattern_verified = false;
    bool target_state_entry_found = false;
    bool transition_call_returned_success = false;
    bool exception_context_prepared = false;
    std::string exception_previous_label;
    std::string exception_target_label;
    int lifecycle_stage_count = 0;
};

struct StateCatalogSnapshot {
    bool label_registration_ready = false;
    bool logo_path_known = false;
    bool scene_path_known = false;
    bool system_path_known = false;
    bool combat_path_known = false;
    bool trade_path_known = false;
    bool foundry_path_known = false;
    int catalog_entry_count = 0;
};

struct MainMenuPrerequisitesSnapshot {
    bool state_catalog_ready = false;
    bool logo_path_known = false;
    bool system_path_known = false;
    bool state_entry_active = false;
    bool ui_state_bootstrap_ready = false;
    bool stable_frame_loop_ready = false;
    bool resource_root_available = false;
    bool cpk_layer_ready = false;
    bool ui_resource_pack_available = false;
    bool database_resource_pack_available = false;
};

enum class MainMenuPathHint {
    unknown,
    logo_path,
    system_path,
};

enum class UiProbeStage {
    none,
    load_only,
    system_singleton_ptr,
    window_manager_singleton_ptr,
    window_factory_manager_singleton_ptr,
    renderer_construct,
    system_initialize,
    set_default_font,
    create_desktop,
    set_gui_sheet,
    imageset_oiramlook,
    register_factory,
    register_all_factories,
};

const char* ToString(UiProbeStage stage) noexcept;

struct MainMenuReadinessSnapshot {
    bool prerequisites_ready = false;
    bool logo_route_reachable = false;
    bool system_route_reachable = false;
    bool missing_ui_resources = false;
    bool missing_database_resources = false;
    bool missing_state_catalog = false;
    MainMenuPathHint preferred_path = MainMenuPathHint::unknown;
};

struct StartupRouteSnapshot {
    bool route_selected = false;
    bool prefers_logo_boot = false;
    bool allows_system_boot = false;
    bool needs_more_ui_bootstrap = false;
    MainMenuPathHint selected_path = MainMenuPathHint::unknown;
};

struct LogoPathBridgeSnapshot {
    bool logo_label_known = false;
    bool leave_entry_known = false;
    bool leave_wrapper_known = false;
    bool ui_event_queue_participates = false;
    bool cegui_toggle_participates = false;
    bool effect_cleanup_participates = false;
    int bridge_stage_count = 0;
};

struct MainMenuUiBridgeSnapshot {
    bool cegui_initialized = false;
    bool ui_event_queue_dispatch_ready = false;
    bool factory_registration_ready = false;
    bool core_desktop_factory_ready = false;
    bool gui_sheet_ready = false;
    bool oiramlook_imageset_ready = false;
    bool desktop_window_ready = false;
    bool real_gui_sheet_ready = false;
    bool real_oiramlook_imageset_ready = false;
    bool menu_root_created = false;
    bool menu_root_visible = false;
    bool new_game_bound = false;
    bool exit_bound = false;
    bool new_game_handler_known = false;
    bool load_game_handler_known = false;
    bool exit_game_handler_known = false;
    int registered_frame_count = 0;
    int bridge_stage_count = 0;
};

struct UiRealDllBackendSnapshot {
    CeguiWindowManagerBackendKind kind = CeguiWindowManagerBackendKind::simulated;
    bool backend_ready = false;
    bool can_create_window = false;
    bool can_add_child_window = false;
    bool can_set_gui_sheet = false;
    std::string blocker;
};

struct UiCeguiInitChainSnapshot {
    bool system_singleton_ready = false;
    bool window_manager_singleton_ready = false;
    bool window_factory_manager_singleton_ready = false;
    bool cursor_preload_ready = false;
    bool cursor_set_ready = false;
    bool desktop_ready = false;
    bool gui_sheet_ready = false;
    bool oiramlook_imageset_ready = false;
    std::string blocker;
};

struct UiCeguiSystemChainSnapshot {
    bool renderer_construct_ready = false;
    bool system_ctor_export_ready = false;
    bool system_ctor_completed = false;
    bool original_ctor_return_reached = false;
    bool original_initialize_cegui_returned_to_caller = false;
    bool system_singleton_ready = false;
    bool set_uv_alignment_ready = false;
    std::string blocker;
};

struct UiCeguiSingletonPublishSnapshot {
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
    std::string blocker;
};

struct UiInitializationChainSnapshot {
    bool ida_initialize_cegui_before_register_ui_windows = false;
    bool ida_initialize_ui_frame_manager_inside_initialize_cegui = false;
    bool rebuilt_preloads_ui_resources_before_initialize_cegui = false;
    bool rebuilt_initialize_cegui_called = false;
    bool rebuilt_register_ui_windows_called = false;
    bool rebuilt_register_ui_windows_after_initialize_cegui = false;
    bool rebuilt_main_layout_loaded = false;
    bool rebuilt_menu_layout_loaded = false;
    bool rebuilt_real_ui_attempt_attempted = false;
    std::string blocker;
};

struct UiRuntimeProbeSnapshot {
    bool cpk_runtime_available = false;
    bool ui_cpk_opened = false;
    bool ui_resource_entry_found = false;
    bool ui_resource_read_ok = false;
    bool ui_font_resource_parsed = false;
    bool ui_scheme_resource_parsed = false;
    bool ui_imageset_resource_parsed = false;
    bool ui_layout_resource_parsed = false;
    bool ui_layout_has_push_button = false;
    bool ui_layout_has_new_game_button = false;
    bool ui_layout_has_exit_button = false;
    bool cegui_base_loaded = false;
    bool oiramlook_loaded = false;
    bool register_all_factories_found = false;
    bool register_factory_found = false;
    bool register_all_factories_called = false;
    unsigned long cegui_base_error = 0;
    unsigned long oiramlook_error = 0;
    int isolated_probe_exit_code = 0;
    bool isolated_probe_reached_register_all_factories = false;
    bool system_singleton_probe_ok = false;
    bool window_manager_singleton_probe_ok = false;
    bool window_factory_manager_singleton_probe_ok = false;
    bool load_only_probe_ok = false;
    bool renderer_construct_probe_ok = false;
    bool system_initialize_probe_ok = false;
    bool system_ctor_export_found = false;
    unsigned long system_ctor_seh_code = 0;
    bool fake_metrics_15_17_attempted = false;
    unsigned long fake_metrics_15_17_seh_code = 0;
    bool fake_metrics_15_19_attempted = false;
    unsigned long fake_metrics_15_19_seh_code = 0;
    bool cegui_log_seen = false;
    bool cegui_log_reached_scheme_manager = false;
    bool cegui_log_reached_mouse_cursor = false;
    bool cegui_log_system_font_missing = false;
    bool cegui_log_system_font_load_failed = false;
    bool cegui_log_oiramlook_static_image_factory_missing = false;
    bool cegui_log_oiramlook_imageset_missing = false;
    bool oiramlook_get_imageset_failed = false;
    bool original_ctor_break_reached = false;
    bool original_ctor_return_reached = false;
    bool original_initialize_cegui_returned_to_caller = false;
    bool original_get_singleton_ptr_non_null = false;
    bool original_get_singleton_ptr_matches_ctor_object = false;
    bool original_mouse_cursor_break_reached = false;
    bool original_reached_post_audio_stage = false;
    bool original_reached_register_ui_windows = false;
    bool original_reached_console_init_stage = false;
    bool original_reached_state_ui_init_stage = false;
    bool original_state_ui_transition_completed = false;
    bool original_reached_viewport_center_stage = false;
    bool original_reached_update_game_frame = false;
    bool original_reached_game_render_frame = false;
    bool original_reached_render_frame_scene_draw_stage = false;
    bool original_reached_render_frame_overlay_stage = false;
    bool original_reached_render_frame_present_tail_stage = false;
    unsigned long original_post_ctor_exception_eip = 0;
    bool original_post_ctor_exception_is_system_noise = false;
    bool original_mouse_cursor_likely_blocker = false;
    bool set_default_font_probe_ok = false;
    bool create_desktop_probe_ok = false;
    bool set_gui_sheet_probe_ok = false;
    bool imageset_oiramlook_probe_ok = false;
    bool register_factory_probe_attempted = false;
    bool register_factory_probe_succeeded = false;
    bool register_all_factories_probe_attempted = false;
    bool register_all_factories_probe_succeeded = false;
    bool register_all_factories_probe_crashed = false;
    bool register_factory_probe_crashed = false;
    UiProbeStage last_attempted_stage = UiProbeStage::none;
    UiProbeStage deepest_success_stage = UiProbeStage::none;
};

enum class MainMenuAction {
    none,
    new_game,
    load_game,
    exit_game,
    refresh,
};

struct MainMenuInteractionSnapshot {
    bool new_game_clicked = false;
    bool load_game_clicked = false;
    bool exit_game_clicked = false;
    bool refresh_clicked = false;
    std::string dispatched_window_name;
    std::optional<std::size_t> dispatched_window_index;
    std::uint32_t dispatched_handler_ea = 0;
    MainMenuAction last_action = MainMenuAction::none;
};

struct InitPipelineSnapshot {
    bool render_system_callbacks_bound = false;
    bool systems_core_initialized = false;
    bool state_and_ui_initialized = false;
    bool pal_game_iv_state_initialized = false;
    bool frame_callback_bound = false;
    bool current_state_entry_ready = false;
    bool viewport_centered = false;
    int systems_stage_count = 0;
    int state_ui_stage_count = 0;
};

enum class WinMainLoopBranch {
    inactive,
    message_dispatch,
    frame_update,
    wait_message,
};

class PAL4App {
public:
    explicit PAL4App(Context& ctx);

    bool Bootstrap(const BootConfig& config);
    void TickOnce();
    void Shutdown();
    WinMainPhase Phase() const noexcept;
    WinMainGateSnapshot Gates() const noexcept;
    InitPipelineSnapshot InitPipeline() const noexcept;
    SingleInstanceSnapshot SingleInstance() const noexcept;
    PresentationSnapshot Presentation() const noexcept;
    FirstFrameSnapshot FirstFrame() const noexcept;
    RuntimeFrameSnapshot RuntimeFrame() const noexcept;
    ShutdownSnapshot ShutdownFlow() const noexcept;
    StateLifecycleSnapshot StateLifecycle() const noexcept;
    StateCatalogSnapshot StateCatalog() const noexcept;
    MainMenuPrerequisitesSnapshot MainMenuPrerequisites() const noexcept;
    MainMenuReadinessSnapshot MainMenuReadiness() const noexcept;
    StartupRouteSnapshot StartupRoute() const noexcept;
    LogoPathBridgeSnapshot LogoPathBridge() const noexcept;
    MainMenuUiBridgeSnapshot MainMenuUiBridge() const noexcept;
    UiRealDllBackendSnapshot UiRealDllBackend() const noexcept;
    UiCeguiInitChainSnapshot UiCeguiInitChain() const noexcept;
    UiCeguiSystemChainSnapshot UiCeguiSystemChain() const noexcept;
    UiCeguiSingletonPublishSnapshot UiCeguiSingletonPublish() const noexcept;
    UiInitializationChainSnapshot UiInitializationChain() const noexcept;
    UiRuntimeProbeSnapshot UiRuntimeProbe() const noexcept;
    MainMenuInteractionSnapshot MainMenuInteraction() const noexcept;
    ThreadBootstrapSnapshot ThreadBootstrap() const noexcept;
    WinMainLoopSnapshot LoopSnapshot() const noexcept;
    WinMainLoopBranch LastLoopBranch() const noexcept;
    bool AttemptRealUiInitialization();
    void RefreshUiRuntimeProbeFromLogs(const std::string& probe_directory);
    void SetParameterLatchForTesting(bool is_ready) noexcept;
    void SetMessageQueueForTesting(bool has_message, bool is_quit_message = false) noexcept;
    void SetSingleInstanceForTesting(bool has_existing_window) noexcept;
    void TriggerNewGameAction() noexcept;
    void TriggerLoadGameAction() noexcept;
    void TriggerExitGameAction() noexcept;
    void TriggerRefreshAction() noexcept;

private:
    void AdvancePhase(WinMainPhase phase, const char* phase_name);
    void ApplyUiProbeDerivedState() noexcept;

    Context& ctx_;
    BootConfig boot_config_{};
    WinMainPhase phase_ = WinMainPhase::pre_bootstrap;
    InitPipelineSnapshot init_pipeline_{};
    SingleInstanceSnapshot single_instance_{};
    PresentationSnapshot presentation_{};
    FirstFrameSnapshot first_frame_{};
    RuntimeFrameSnapshot runtime_frame_{};
    ShutdownSnapshot shutdown_snapshot_{};
    StateLifecycleSnapshot state_lifecycle_{};
    StateCatalogSnapshot state_catalog_{};
    MainMenuPrerequisitesSnapshot main_menu_prerequisites_{};
    MainMenuReadinessSnapshot main_menu_readiness_{};
    StartupRouteSnapshot startup_route_{};
    LogoPathBridgeSnapshot logo_path_bridge_{};
    MainMenuUiBridgeSnapshot main_menu_ui_bridge_{};
    UiRealDllBackendSnapshot ui_real_dll_backend_{};
    UiCeguiInitChainSnapshot ui_cegui_init_chain_{};
    UiCeguiSystemChainSnapshot ui_cegui_system_chain_{};
    UiCeguiSingletonPublishSnapshot ui_cegui_singleton_publish_{};
    UiInitializationChainSnapshot ui_initialization_chain_{};
    UiRuntimeProbeSnapshot ui_runtime_probe_{};
    MainMenuInteractionSnapshot main_menu_interaction_{};
    ThreadBootstrapSnapshot thread_bootstrap_{};
    WinMainLoopBranch last_loop_branch_ = WinMainLoopBranch::inactive;
    WinMainLoopSnapshot loop_snapshot_{};
    bool parameter_latch_ready_ = true;
    bool has_existing_window_for_testing_ = false;
    int frame_gate_seed_ = 1;  // `v17` in WinMain is partially reconstructed.
};

}  // namespace pal4

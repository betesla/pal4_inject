#pragma once

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "pal4inject/types.h"

namespace pal4::inject {

class RuntimeState {
public:
    void InitializeInventory(const std::vector<HookDescriptor>& inventory);

    void SetBootstrapReady(bool ready);
    void SetHooksReady(bool ready);
    void SetPipeReady(bool ready);
    void SetUiDispatchReady(bool ready);
    void SetCrashHandlerReady(bool ready);
    bool BootstrapReady() const;
    bool HooksReady() const;
    bool PipeReady() const;
    bool UiDispatchReady() const;
    bool CrashHandlerReady() const;

    void SetMainModuleBase(std::uintptr_t base);
    std::uintptr_t MainModuleBase() const;

    void ConfigureNames(std::string_view ready_event_name, std::string_view pipe_name);
    std::string ReadyEventName() const;
    std::string PipeName() const;

    void SetHookInstalled(HookId id, bool installed);
    void IncrementHookCall(HookId id);
    void SetHookMode(HookId id, HookMode mode);
    HookMode GetHookMode(HookId id) const;
    void SetPreferredActiveHookMode(HookId id, HookMode mode);
    HookMode GetPreferredActiveHookMode(HookId id) const;
    void SetHookLogEnabled(HookId id, bool enabled);
    bool GetHookLogEnabled(HookId id) const;
    void SetHookError(HookId id, std::string_view error);
    void ClearHookError(HookId id);
    void SetMsaaLevel(MsaaLevel level);
    MsaaLevel GetMsaaLevel() const;
    void SetBinkScalingMode(BinkScalingMode mode);
    BinkScalingMode GetBinkScalingMode() const;
    void SetGiTalkVolume(float volume);
    float GetGiTalkVolume() const;
    void SetGiTalkVolumeApplied(bool applied, std::string_view summary);
    void SetGamepadEnabled(bool enabled);
    bool GamepadEnabled() const;
    void SetGamepadLogEnabled(bool enabled);
    bool GamepadLogEnabled() const;
    void SetGamepadModernControls(bool enabled);
    bool GamepadModernControls() const;
    void SetGamepadInvertCameraY(bool enabled);
    bool GamepadInvertCameraY() const;
    void SetGamepadPreserveFreeCamera(bool enabled);
    bool GamepadPreserveFreeCamera() const;
    void SetGamepadRunThreshold(float threshold);
    float GamepadRunThreshold() const;
    void SetGamepadFastRunThreshold(float threshold);
    float GamepadFastRunThreshold() const;
    void SetGamepadCameraSensitivity(float degrees_per_second);
    float GamepadCameraSensitivity() const;
    void SetGamepadMapping(const Xbox360GamepadMapping& mapping);
    Xbox360GamepadMapping GetGamepadMapping() const;
    void SetGamepadConnected(bool connected);
    void SetGamepadContext(GamepadInputContext context);
    void SetBorderlessWindowEnabled(bool enabled);
    bool BorderlessWindowEnabled() const;
    void SetBorderlessMonitor(std::string_view device_name);
    std::string BorderlessMonitor() const;
    void SetBorderlessWindowApplied(bool applied, std::string_view summary);
    void SetActiveUiProfile(UiProfile profile);
    UiProfile GetActiveUiProfile() const;

    void SetLastFontSync(std::string_view summary, bool ok);
    void SetLastUiEvent(std::string_view text);
    void SetLastError(std::string_view text);
    void SetCrashArtifacts(
        std::string_view summary,
        std::string_view report_path,
        std::string_view dump_path);
    bool TrySetCrashArtifacts(
        std::string_view summary,
        std::string_view report_path,
        std::string_view dump_path);
    void ObservePalivEntry(std::uint32_t entry);
    std::uint32_t LastPalivEntryObserved() const;
    void AppendEventLog(std::string_view text);
    std::string BuildEventLogTail(std::size_t max_entries = 32) const;

    RuntimeSnapshot BuildSnapshot(std::uint32_t current_paliv_entry) const;
    bool TryBuildSnapshot(RuntimeSnapshot* out, std::uint32_t current_paliv_entry) const;
    std::vector<HookStatus> CopyHookStatuses() const;
    bool WaitForHookCalls(HookId id, std::uint64_t expected_calls, std::uint32_t timeout_ms);
    bool WaitForPalivEntry(std::uint32_t expected_entry, std::uint32_t timeout_ms);

    void RequestShutdown();
    bool ShutdownRequested() const;

private:
    HookStatus* FindHookStatusUnlocked(HookId id);
    const HookStatus* FindHookStatusUnlocked(HookId id) const;
    RuntimeSnapshot BuildSnapshotUnlocked(std::uint32_t current_paliv_entry) const;

    mutable std::mutex mutex_;
    std::condition_variable state_cv_;
    bool bootstrap_ready_ = false;
    bool hooks_ready_ = false;
    bool pipe_ready_ = false;
    bool ui_dispatch_ready_ = false;
    bool crash_handler_ready_ = false;
    std::uintptr_t main_module_base_ = 0;
    std::string ready_event_name_;
    std::string pipe_name_;
    std::vector<HookStatus> hook_statuses_;
    MsaaLevel msaa_level_ = MsaaLevel::off;
    BinkScalingMode bink_scaling_mode_ = BinkScalingMode::fit;
    float gi_talk_volume_ = 1.0F;
    bool gi_talk_volume_applied_ = false;
    std::string gi_talk_volume_summary_;
    bool gamepad_enabled_ = true;
    bool gamepad_log_enabled_ = false;
    bool gamepad_modern_controls_ = true;
    bool gamepad_invert_camera_y_ = false;
    bool gamepad_preserve_free_camera_ = false;
    float gamepad_run_threshold_ = 0.62F;
    float gamepad_fast_run_threshold_ = 0.88F;
    float gamepad_camera_sensitivity_ = 120.0F;
    Xbox360GamepadMapping gamepad_mapping_ = DefaultXbox360GamepadMapping();
    bool gamepad_connected_ = false;
    GamepadInputContext gamepad_context_ = GamepadInputContext::gameplay;
    bool borderless_window_enabled_ = false;
    bool borderless_window_applied_ = false;
    std::string borderless_monitor_;
    std::string borderless_window_summary_;
    UiProfile active_ui_profile_ = UiProfile::centered_800x600;
    std::uint32_t last_paliv_entry_observed_ = 0;
    std::string last_ui_event_;
    std::string last_error_;
    std::string last_font_sync_summary_;
    bool last_font_sync_ok_ = false;
    std::string last_crash_summary_;
    std::string last_crash_report_path_;
    std::string last_crash_dump_path_;
    std::deque<std::string> event_log_;
    bool shutdown_requested_ = false;
};

RuntimeState& GetRuntimeState();

}  // namespace pal4::inject

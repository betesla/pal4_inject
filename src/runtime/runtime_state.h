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
    void SetVrMode(VrMode mode);
    VrMode GetVrMode() const;
    void SetVrHeadPose(const VrHeadPose& pose);
    VrHeadPose GetVrHeadPose() const;
    void SetVrCameraState(const VrCameraState& state);
    VrCameraState GetVrCameraState() const;
    void SetDialogFontHdEnabled(bool enabled);
    bool DialogFontHdEnabled() const;
    void SetSystemFontOversampleEnabled(bool enabled);
    bool SystemFontOversampleEnabled() const;
    void SetGamepadEnabled(bool enabled);
    bool GamepadEnabled() const;
    void SetGamepadLogEnabled(bool enabled);
    bool GamepadLogEnabled() const;
    void SetGamepadConnected(bool connected);
    bool GamepadConnected() const;
    void SetGamepadContext(GamepadInputContext context);
    GamepadInputContext GetGamepadContext() const;

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
    void SetShadowResolution(ShadowResolution resolution);
    ShadowResolution GetShadowResolution() const;
    void SetUiTextureFilter(UiTextureFilter filter);
    UiTextureFilter GetUiTextureFilter() const;

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
    VrMode vr_mode_ = VrMode::off;
    VrHeadPose vr_head_pose_{};
    VrCameraState vr_camera_state_{};
    bool dialog_font_hd_enabled_ = true;
    bool system_font_oversample_enabled_ = false;
    bool gamepad_enabled_ = true;
    bool gamepad_log_enabled_ = false;
    bool gamepad_connected_ = false;
    GamepadInputContext gamepad_context_ = GamepadInputContext::gameplay;
    std::uintptr_t main_module_base_ = 0;
    std::string ready_event_name_;
    std::string pipe_name_;
    std::vector<HookStatus> hook_statuses_;
    MsaaLevel msaa_level_ = MsaaLevel::off;
    ShadowResolution shadow_resolution_ = ShadowResolution::x64;
    UiTextureFilter ui_texture_filter_ = UiTextureFilter::nearest;
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

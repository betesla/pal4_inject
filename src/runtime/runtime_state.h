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
    bool BootstrapReady() const;
    bool HooksReady() const;
    bool PipeReady() const;
    bool UiDispatchReady() const;

    void SetMainModuleBase(std::uintptr_t base);
    std::uintptr_t MainModuleBase() const;

    void ConfigureNames(std::string_view ready_event_name, std::string_view pipe_name);
    std::string ReadyEventName() const;
    std::string PipeName() const;

    void SetHookInstalled(HookId id, bool installed);
    void IncrementHookCall(HookId id);
    void SetHookMode(HookId id, HookMode mode);
    HookMode GetHookMode(HookId id) const;
    void SetHookError(HookId id, std::string_view error);
    void ClearHookError(HookId id);

    void SetLastUiEvent(std::string_view text);
    void SetLastError(std::string_view text);
    void ObservePalivEntry(std::uint32_t entry);
    std::uint32_t LastPalivEntryObserved() const;
    void AppendEventLog(std::string_view text);
    std::string BuildEventLogTail(std::size_t max_entries = 32) const;

    RuntimeSnapshot BuildSnapshot(std::uint32_t current_paliv_entry) const;
    std::vector<HookStatus> CopyHookStatuses() const;
    bool WaitForHookCalls(HookId id, std::uint64_t expected_calls, std::uint32_t timeout_ms);
    bool WaitForPalivEntry(std::uint32_t expected_entry, std::uint32_t timeout_ms);

    void RequestShutdown();
    bool ShutdownRequested() const;

private:
    HookStatus* FindHookStatusUnlocked(HookId id);
    const HookStatus* FindHookStatusUnlocked(HookId id) const;

    mutable std::mutex mutex_;
    std::condition_variable state_cv_;
    bool bootstrap_ready_ = false;
    bool hooks_ready_ = false;
    bool pipe_ready_ = false;
    bool ui_dispatch_ready_ = false;
    std::uintptr_t main_module_base_ = 0;
    std::string ready_event_name_;
    std::string pipe_name_;
    std::vector<HookStatus> hook_statuses_;
    std::uint32_t last_paliv_entry_observed_ = 0;
    std::string last_ui_event_;
    std::string last_error_;
    std::deque<std::string> event_log_;
    bool shutdown_requested_ = false;
};

RuntimeState& GetRuntimeState();

}  // namespace pal4::inject

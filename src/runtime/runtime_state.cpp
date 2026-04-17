#include "runtime_state.h"

#include <chrono>

namespace pal4::inject {
namespace {

HookMode DefaultPreferredActiveMode(const HookDescriptor& descriptor) {
    if (descriptor.mode != HookMode::observe_only) {
        return descriptor.mode;
    }
    return HookMode::replace_with_fallback;
}

}  // namespace

void RuntimeState::InitializeInventory(const std::vector<HookDescriptor>& inventory) {
    std::scoped_lock lock(mutex_);
    hook_statuses_.clear();
    hook_statuses_.reserve(inventory.size());
    for (const auto& descriptor : inventory) {
        HookStatus status{};
        status.id = descriptor.id;
        status.mode = descriptor.mode;
        status.preferred_active_mode = DefaultPreferredActiveMode(descriptor);
        status.log_enabled = false;
        hook_statuses_.push_back(status);
    }
}

void RuntimeState::SetBootstrapReady(const bool ready) {
    std::scoped_lock lock(mutex_);
    bootstrap_ready_ = ready;
    state_cv_.notify_all();
}

void RuntimeState::SetHooksReady(const bool ready) {
    std::scoped_lock lock(mutex_);
    hooks_ready_ = ready;
    state_cv_.notify_all();
}

void RuntimeState::SetPipeReady(const bool ready) {
    std::scoped_lock lock(mutex_);
    pipe_ready_ = ready;
    state_cv_.notify_all();
}

void RuntimeState::SetUiDispatchReady(const bool ready) {
    std::scoped_lock lock(mutex_);
    ui_dispatch_ready_ = ready;
    state_cv_.notify_all();
}

void RuntimeState::SetCrashHandlerReady(const bool ready) {
    std::scoped_lock lock(mutex_);
    crash_handler_ready_ = ready;
    state_cv_.notify_all();
}

bool RuntimeState::BootstrapReady() const {
    std::scoped_lock lock(mutex_);
    return bootstrap_ready_;
}

bool RuntimeState::HooksReady() const {
    std::scoped_lock lock(mutex_);
    return hooks_ready_;
}

bool RuntimeState::PipeReady() const {
    std::scoped_lock lock(mutex_);
    return pipe_ready_;
}

bool RuntimeState::UiDispatchReady() const {
    std::scoped_lock lock(mutex_);
    return ui_dispatch_ready_;
}

bool RuntimeState::CrashHandlerReady() const {
    std::scoped_lock lock(mutex_);
    return crash_handler_ready_;
}

void RuntimeState::SetMainModuleBase(const std::uintptr_t base) {
    std::scoped_lock lock(mutex_);
    main_module_base_ = base;
}

std::uintptr_t RuntimeState::MainModuleBase() const {
    std::scoped_lock lock(mutex_);
    return main_module_base_;
}

void RuntimeState::ConfigureNames(
    const std::string_view ready_event_name,
    const std::string_view pipe_name) {
    std::scoped_lock lock(mutex_);
    ready_event_name_ = ready_event_name;
    pipe_name_ = pipe_name;
}

std::string RuntimeState::ReadyEventName() const {
    std::scoped_lock lock(mutex_);
    return ready_event_name_;
}

std::string RuntimeState::PipeName() const {
    std::scoped_lock lock(mutex_);
    return pipe_name_;
}

void RuntimeState::SetHookInstalled(const HookId id, const bool installed) {
    std::scoped_lock lock(mutex_);
    if (auto* status = FindHookStatusUnlocked(id)) {
        status->installed = installed;
    }
    state_cv_.notify_all();
}

void RuntimeState::IncrementHookCall(const HookId id) {
    std::scoped_lock lock(mutex_);
    if (auto* status = FindHookStatusUnlocked(id)) {
        ++status->call_count;
    }
    state_cv_.notify_all();
}

void RuntimeState::SetHookMode(const HookId id, const HookMode mode) {
    std::scoped_lock lock(mutex_);
    if (auto* status = FindHookStatusUnlocked(id)) {
        status->mode = mode;
        if (mode != HookMode::observe_only) {
            status->preferred_active_mode = mode;
        }
    }
    state_cv_.notify_all();
}

HookMode RuntimeState::GetHookMode(const HookId id) const {
    std::scoped_lock lock(mutex_);
    if (const auto* status = FindHookStatusUnlocked(id)) {
        return status->mode;
    }
    return HookMode::observe_only;
}

void RuntimeState::SetPreferredActiveHookMode(const HookId id, const HookMode mode) {
    std::scoped_lock lock(mutex_);
    if (auto* status = FindHookStatusUnlocked(id)) {
        status->preferred_active_mode =
            mode == HookMode::observe_only ? HookMode::replace_with_fallback : mode;
    }
    state_cv_.notify_all();
}

HookMode RuntimeState::GetPreferredActiveHookMode(const HookId id) const {
    std::scoped_lock lock(mutex_);
    if (const auto* status = FindHookStatusUnlocked(id)) {
        return status->preferred_active_mode;
    }
    return HookMode::replace_with_fallback;
}

void RuntimeState::SetHookLogEnabled(const HookId id, const bool enabled) {
    std::scoped_lock lock(mutex_);
    if (auto* status = FindHookStatusUnlocked(id)) {
        status->log_enabled = enabled;
    }
    state_cv_.notify_all();
}

bool RuntimeState::GetHookLogEnabled(const HookId id) const {
    std::scoped_lock lock(mutex_);
    if (const auto* status = FindHookStatusUnlocked(id)) {
        return status->log_enabled;
    }
    return false;
}

void RuntimeState::SetHookError(const HookId id, const std::string_view error) {
    std::scoped_lock lock(mutex_);
    if (auto* status = FindHookStatusUnlocked(id)) {
        status->last_error = error;
    }
    state_cv_.notify_all();
}

void RuntimeState::ClearHookError(const HookId id) {
    std::scoped_lock lock(mutex_);
    if (auto* status = FindHookStatusUnlocked(id)) {
        status->last_error.clear();
    }
    state_cv_.notify_all();
}

void RuntimeState::SetMsaaLevel(const MsaaLevel level) {
    std::scoped_lock lock(mutex_);
    msaa_level_ = level;
    state_cv_.notify_all();
}

MsaaLevel RuntimeState::GetMsaaLevel() const {
    std::scoped_lock lock(mutex_);
    return msaa_level_;
}

void RuntimeState::SetLastFontSync(
    const std::string_view summary,
    const bool ok) {
    std::scoped_lock lock(mutex_);
    last_font_sync_summary_ = summary;
    last_font_sync_ok_ = ok;
    state_cv_.notify_all();
}

void RuntimeState::SetLastUiEvent(const std::string_view text) {
    std::scoped_lock lock(mutex_);
    last_ui_event_ = text;
    state_cv_.notify_all();
}

void RuntimeState::SetLastError(const std::string_view text) {
    std::scoped_lock lock(mutex_);
    last_error_ = text;
    state_cv_.notify_all();
}

void RuntimeState::SetCrashArtifacts(
    const std::string_view summary,
    const std::string_view report_path,
    const std::string_view dump_path) {
    std::scoped_lock lock(mutex_);
    last_crash_summary_ = summary;
    last_crash_report_path_ = report_path;
    last_crash_dump_path_ = dump_path;
    state_cv_.notify_all();
}

bool RuntimeState::TrySetCrashArtifacts(
    const std::string_view summary,
    const std::string_view report_path,
    const std::string_view dump_path) {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        return false;
    }
    last_crash_summary_ = summary;
    last_crash_report_path_ = report_path;
    last_crash_dump_path_ = dump_path;
    state_cv_.notify_all();
    return true;
}

void RuntimeState::ObservePalivEntry(const std::uint32_t entry) {
    std::scoped_lock lock(mutex_);
    last_paliv_entry_observed_ = entry;
    state_cv_.notify_all();
}

std::uint32_t RuntimeState::LastPalivEntryObserved() const {
    std::scoped_lock lock(mutex_);
    return last_paliv_entry_observed_;
}

void RuntimeState::AppendEventLog(const std::string_view text) {
    std::scoped_lock lock(mutex_);
    constexpr std::size_t kMaxEventLogEntries = 128;
    if (event_log_.size() == kMaxEventLogEntries) {
        event_log_.pop_front();
    }
    event_log_.emplace_back(text);
    state_cv_.notify_all();
}

std::string RuntimeState::BuildEventLogTail(const std::size_t max_entries) const {
    std::scoped_lock lock(mutex_);
    std::string tail;
    const std::size_t start =
        event_log_.size() > max_entries ? event_log_.size() - max_entries : 0;
    for (std::size_t i = start; i < event_log_.size(); ++i) {
        if (!tail.empty()) {
            tail += '\n';
        }
        tail += event_log_[i];
    }
    return tail;
}

RuntimeSnapshot RuntimeState::BuildSnapshotUnlocked(const std::uint32_t current_paliv_entry) const {
    RuntimeSnapshot snapshot{};
    snapshot.bootstrap_ready = bootstrap_ready_;
    snapshot.hooks_ready = hooks_ready_;
    snapshot.pipe_ready = pipe_ready_;
    snapshot.ui_dispatch_ready = ui_dispatch_ready_;
    snapshot.crash_handler_ready = crash_handler_ready_;
    snapshot.main_module_base = main_module_base_;
    snapshot.msaa_level = msaa_level_;
    snapshot.current_paliv_entry = current_paliv_entry;
    snapshot.last_paliv_entry_observed = last_paliv_entry_observed_;
    snapshot.last_ui_event = last_ui_event_;
    snapshot.last_error = last_error_;
    snapshot.last_font_sync_summary = last_font_sync_summary_;
    snapshot.last_font_sync_ok = last_font_sync_ok_;
    snapshot.last_crash_summary = last_crash_summary_;
    snapshot.last_crash_report_path = last_crash_report_path_;
    snapshot.last_crash_dump_path = last_crash_dump_path_;
    const std::size_t start =
        event_log_.size() > 32 ? event_log_.size() - 32 : 0;
    for (std::size_t i = start; i < event_log_.size(); ++i) {
        if (!snapshot.event_log_tail.empty()) {
            snapshot.event_log_tail += '\n';
        }
        snapshot.event_log_tail += event_log_[i];
    }
    snapshot.active_hooks = hook_statuses_;
    if (const auto* status = FindHookStatusUnlocked(HookId::process_ui_event)) {
        snapshot.process_ui_event = *status;
    } else {
        snapshot.process_ui_event.id = HookId::process_ui_event;
    }
    return snapshot;
}

RuntimeSnapshot RuntimeState::BuildSnapshot(const std::uint32_t current_paliv_entry) const {
    std::scoped_lock lock(mutex_);
    return BuildSnapshotUnlocked(current_paliv_entry);
}

bool RuntimeState::TryBuildSnapshot(
    RuntimeSnapshot* out,
    const std::uint32_t current_paliv_entry) const {
    if (!out) {
        return false;
    }
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        return false;
    }
    *out = BuildSnapshotUnlocked(current_paliv_entry);
    return true;
}

std::vector<HookStatus> RuntimeState::CopyHookStatuses() const {
    std::scoped_lock lock(mutex_);
    return hook_statuses_;
}

bool RuntimeState::WaitForHookCalls(
    const HookId id,
    const std::uint64_t expected_calls,
    const std::uint32_t timeout_ms) {
    std::unique_lock lock(mutex_);
    return state_cv_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this, id, expected_calls]() {
            const auto* status = FindHookStatusUnlocked(id);
            return status && status->call_count >= expected_calls;
        });
}

bool RuntimeState::WaitForPalivEntry(
    const std::uint32_t expected_entry,
    const std::uint32_t timeout_ms) {
    std::unique_lock lock(mutex_);
    return state_cv_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this, expected_entry]() {
            return last_paliv_entry_observed_ == expected_entry;
        });
}

void RuntimeState::RequestShutdown() {
    std::scoped_lock lock(mutex_);
    shutdown_requested_ = true;
    state_cv_.notify_all();
}

bool RuntimeState::ShutdownRequested() const {
    std::scoped_lock lock(mutex_);
    return shutdown_requested_;
}

HookStatus* RuntimeState::FindHookStatusUnlocked(const HookId id) {
    for (auto& status : hook_statuses_) {
        if (status.id == id) {
            return &status;
        }
    }
    return nullptr;
}

const HookStatus* RuntimeState::FindHookStatusUnlocked(const HookId id) const {
    for (const auto& status : hook_statuses_) {
        if (status.id == id) {
            return &status;
        }
    }
    return nullptr;
}

RuntimeState& GetRuntimeState() {
    static RuntimeState state;
    return state;
}

}  // namespace pal4::inject

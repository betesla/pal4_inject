#pragma once

#include "pal4/ui_frame_manager.h"

namespace pal4 {

enum class CeguiWindowManagerBackendKind : int {
    simulated,
    real_dll_stub,
};

const char* ToString(CeguiWindowManagerBackendKind kind) noexcept;

struct CeguiWindowManagerBackendSelection {
    CeguiWindowManagerBackendKind requested_kind = CeguiWindowManagerBackendKind::simulated;
    CeguiWindowManagerBackendKind kind = CeguiWindowManagerBackendKind::simulated;
    bool backend_ready = false;
    bool can_create_window = false;
    bool can_add_child_window = false;
    bool can_set_gui_sheet = false;
    std::string blocker;
};

CeguiWindowManagerBackendSelection SelectCeguiWindowManagerBackend() noexcept;

struct CeguiWindowManagerProbeSupport {
    bool system_singleton_ok = false;
    bool system_ctor_completed = false;
    bool original_ctor_return_reached = false;
    bool original_initialize_cegui_returned_to_caller = false;
    bool window_manager_singleton_ok = false;
    bool window_factory_manager_singleton_ok = false;
    bool create_desktop_ok = false;
    bool set_gui_sheet_ok = false;
    bool imageset_oiramlook_ok = false;
};

CeguiWindowManagerBackendSelection DeriveRealDllStubSelection(
    const CeguiWindowManagerProbeSupport& support) noexcept;

struct UiWindowManagerReplayResult {
    CeguiWindowManagerBackendSelection backend{};
    UiWindowRuntimeState runtime_state{};
    UiWindowManagerBridgeState bridge_state{};
    UiWindowManagerExecutionTrace execution_trace{};
};

class CeguiWindowManagerBackend {
public:
    virtual ~CeguiWindowManagerBackend() = default;

    virtual void CreateWindowNode(
        const UiLayoutObjectTree& tree,
        const UiWindowCreationStep& step,
        UiWindowManagerReplayResult& result) = 0;
    virtual void AddChildWindowNode(
        const UiLayoutObjectTree& tree,
        const UiWindowCreationStep& step,
        UiWindowManagerReplayResult& result) = 0;
    virtual void SetGuiSheetNode(
        const UiLayoutObjectTree& tree,
        std::size_t root_index,
        UiWindowManagerReplayResult& result) = 0;
};

class SimulatedCeguiWindowManagerBridge {
public:
    static UiWindowManagerReplayResult Replay(const UiLayoutObjectTree& tree) noexcept;
};

}  // namespace pal4

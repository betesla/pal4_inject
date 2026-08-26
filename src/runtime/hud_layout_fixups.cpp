#include "hud_layout_fixups.h"

#include <cmath>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cegui_bindings.h"
#include "hook_logging.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "pal4inject/widescreen_ui_layout.h"
#include "runtime_state.h"
#include "widescreen_ui_profiles.h"

namespace pal4::inject {
namespace {

constexpr float kPositionEpsilon = 0.05F;

struct ScopedCeguiString {
    const CeguiBindings* bindings = nullptr;
    OpaqueCeguiString storage{};
    bool constructed = false;

    ~ScopedCeguiString() {
        if (constructed && bindings && bindings->cegui_string_dtor) {
            bindings->cegui_string_dtor(&storage);
        }
    }
};

struct HudWindowOriginalState {
    void* window = nullptr;
    CeguiUVector2 position{};
    CeguiUVector2 maximum_size{};
    float width = 0.0F;
    float height = 0.0F;
    bool clipped_by_parent = true;
    bool captured = false;
};

std::unordered_map<const WidescreenUiWindowRule*, HudWindowOriginalState>
    g_original_window_states;

bool IsCloseEnough(const float lhs, const float rhs) noexcept {
    return std::fabs(lhs - rhs) <= kPositionEpsilon;
}

bool IsRendererWidescreenFixEnabled(const HookMode mode) noexcept {
    return mode != HookMode::observe_only && mode != HookMode::mirror_compare;
}

int* ReadGameConfigPointer() {
    const auto base = GetRuntimeState().MainModuleBase();
    if (base == 0) {
        return nullptr;
    }

    auto* config_ptr_address = reinterpret_cast<int**>(
        ida::ResolveRuntimeAddress(base, ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

bool BuildCeguiAnsiString(
    const CeguiBindings& bindings,
    const std::string_view text,
    ScopedCeguiString* out) {
    if (!out || !bindings.cegui_string_ctor_from_ansi || !bindings.cegui_string_dtor) {
        return false;
    }

    out->bindings = &bindings;
    out->constructed = false;
    out->bindings->cegui_string_ctor_from_ansi(
        &out->storage,
        std::string(text).c_str());
    out->constructed = true;
    return true;
}

void* ResolveWindowByName(
    const CeguiBindings& bindings,
    void* const window_manager,
    const std::string_view window_name) {
    if (!window_manager ||
        window_name.empty() ||
        !bindings.window_manager_is_window_present ||
        !bindings.window_manager_get_window) {
        return nullptr;
    }

    ScopedCeguiString name{};
    if (!BuildCeguiAnsiString(bindings, window_name, &name)) {
        return nullptr;
    }

    if (!bindings.window_manager_is_window_present(window_manager, &name.storage)) {
        return nullptr;
    }

    return bindings.window_manager_get_window(window_manager, &name.storage);
}

void* ResolveHudWindow(
    const CeguiBindings& bindings,
    void* const window_manager,
    const WidescreenUiWindowRule& rule) {
    if (void* window = ResolveWindowByName(bindings, window_manager, rule.window_name)) {
        return window;
    }
    return ResolveWindowByName(bindings, window_manager, rule.fallback_window_name);
}

bool IsWindowInActiveGuiSheet(
    const CeguiBindings& bindings,
    void* const active_gui_sheet,
    void* window) {
    if (!active_gui_sheet || !window || !bindings.window_get_parent) {
        return false;
    }

    constexpr unsigned int kMaxParentDepth = 64;
    for (unsigned int depth = 0; window && depth <= kMaxParentDepth; ++depth) {
        if (window == active_gui_sheet) {
            return true;
        }
        window = bindings.window_get_parent(window);
    }
    return false;
}

bool ApplyHudWindowLogicalX(
    const CeguiBindings& bindings,
    void* const window,
    const float target_x) {
    if (!window ||
        !bindings.window_get_window_position ||
        !bindings.window_set_window_position) {
        return false;
    }

    const CeguiUVector2* current = bindings.window_get_window_position(window);
    if (!current) {
        return false;
    }

    if (IsCloseEnough(current->x.offset, target_x) &&
        IsCloseEnough(current->x.scale, 0.0F)) {
        return false;
    }

    CeguiUVector2 updated = *current;
    updated.x.scale = 0.0F;
    updated.x.offset = target_x;
    bindings.window_set_window_position(window, updated);
    if (bindings.request_redraw) {
        bindings.request_redraw(window);
    }
    return true;
}

bool ApplyHudWindowLogicalY(
    const CeguiBindings& bindings,
    void* const window,
    const float target_y) {
    if (!window ||
        !bindings.window_get_window_position ||
        !bindings.window_set_window_position) {
        return false;
    }

    const CeguiUVector2* current = bindings.window_get_window_position(window);
    if (!current) {
        return false;
    }

    if (IsCloseEnough(current->y.offset, target_y) &&
        IsCloseEnough(current->y.scale, 0.0F)) {
        return false;
    }

    CeguiUVector2 updated = *current;
    updated.y.scale = 0.0F;
    updated.y.offset = target_y;
    bindings.window_set_window_position(window, updated);
    if (bindings.request_redraw) {
        bindings.request_redraw(window);
    }
    return true;
}

bool RestoreHudWindowPosition(
    const CeguiBindings& bindings,
    void* const window,
    const CeguiUVector2& position) {
    if (!window ||
        !bindings.window_get_window_position ||
        !bindings.window_set_window_position) {
        return false;
    }

    const CeguiUVector2* current = bindings.window_get_window_position(window);
    if (!current ||
        (IsCloseEnough(current->x.scale, position.x.scale) &&
         IsCloseEnough(current->x.offset, position.x.offset) &&
         IsCloseEnough(current->y.scale, position.y.scale) &&
         IsCloseEnough(current->y.offset, position.y.offset))) {
        return false;
    }

    bindings.window_set_window_position(window, position);
    if (bindings.request_redraw) {
        bindings.request_redraw(window);
    }
    return true;
}

bool ApplyHudWindowLogicalSize(
    const CeguiBindings& bindings,
    void* const window,
    const float target_width,
    const float target_height) {
    if (!window ||
        !bindings.window_get_absolute_width ||
        !bindings.window_get_absolute_height ||
        !bindings.window_set_window_size) {
        return false;
    }

    if (IsCloseEnough(bindings.window_get_absolute_width(window), target_width) &&
        IsCloseEnough(bindings.window_get_absolute_height(window), target_height)) {
        return false;
    }

    CeguiUVector2 updated{};
    updated.x.offset = target_width;
    updated.y.offset = target_height;
    bindings.window_set_window_size(window, updated);
    if (bindings.request_redraw) {
        bindings.request_redraw(window);
    }
    return true;
}

bool ApplyHudWindowMaximumWidth(
    const CeguiBindings& bindings,
    void* const window,
    const CeguiUVector2& base_maximum_size,
    const float target_width) {
    if (!window ||
        !bindings.window_get_window_max_size ||
        !bindings.window_set_window_max_size) {
        return false;
    }

    const CeguiUVector2* current = bindings.window_get_window_max_size(window);
    if (!current) {
        return false;
    }

    CeguiUVector2 updated = base_maximum_size;
    updated.x.scale = 0.0F;
    updated.x.offset = target_width;
    if (IsCloseEnough(current->x.scale, updated.x.scale) &&
        IsCloseEnough(current->x.offset, updated.x.offset) &&
        IsCloseEnough(current->y.scale, updated.y.scale) &&
        IsCloseEnough(current->y.offset, updated.y.offset)) {
        return false;
    }

    bindings.window_set_window_max_size(window, updated);
    return true;
}

bool RestoreHudWindowMaximumSize(
    const CeguiBindings& bindings,
    void* const window,
    const CeguiUVector2& maximum_size) {
    if (!window ||
        !bindings.window_get_window_max_size ||
        !bindings.window_set_window_max_size) {
        return false;
    }

    const CeguiUVector2* current = bindings.window_get_window_max_size(window);
    if (!current ||
        (IsCloseEnough(current->x.scale, maximum_size.x.scale) &&
         IsCloseEnough(current->x.offset, maximum_size.x.offset) &&
         IsCloseEnough(current->y.scale, maximum_size.y.scale) &&
         IsCloseEnough(current->y.offset, maximum_size.y.offset))) {
        return false;
    }

    bindings.window_set_window_max_size(window, maximum_size);
    return true;
}

bool ApplyHudWindowParentClipping(
    const CeguiBindings& bindings,
    void* const window,
    const bool clipped_by_parent) {
    if (!window ||
        !bindings.window_is_clipped_by_parent ||
        !bindings.window_set_clipped_by_parent ||
        bindings.window_is_clipped_by_parent(window) == clipped_by_parent) {
        return false;
    }

    bindings.window_set_clipped_by_parent(window, clipped_by_parent);
    if (bindings.request_redraw) {
        bindings.request_redraw(window);
    }
    return true;
}

bool CaptureHudWindowOriginalState(
    const CeguiBindings& bindings,
    void* const window,
    HudWindowOriginalState* const state) {
    if (!window || !state ||
        !bindings.window_get_window_position ||
        !bindings.window_get_window_max_size ||
        !bindings.window_get_absolute_width ||
        !bindings.window_get_absolute_height ||
        !bindings.window_is_clipped_by_parent) {
        return false;
    }

    if (state->captured && state->window == window) {
        return true;
    }

    const CeguiUVector2* maximum_size = bindings.window_get_window_max_size(window);
    const CeguiUVector2* position = bindings.window_get_window_position(window);
    if (!maximum_size || !position) {
        return false;
    }

    state->window = window;
    state->position = *position;
    state->maximum_size = *maximum_size;
    state->width = bindings.window_get_absolute_width(window);
    state->height = bindings.window_get_absolute_height(window);
    state->clipped_by_parent = bindings.window_is_clipped_by_parent(window);
    state->captured = true;
    return true;
}

void LogHudLayoutFixups(
    const CeguiWidescreenPlan& plan,
    const bool enabled,
    const int changed_count) {
    std::ostringstream out;
    out
        << "hook=widescreen_ui_layout_profiles"
        << " enabled=" << (enabled ? 1 : 0)
        << " width=" << plan.width
        << " height=" << plan.height
        << " changed=" << changed_count;
    AppendHookEventLog(HookId::cegui_renderer_constructor_2, out.str());
}

}  // namespace

void RefreshWidescreenUiLayoutProfiles() {
    const int* const config = ReadGameConfigPointer();
    if (!config) {
        return;
    }

    const auto plan = BuildActiveUiViewportPlan(config[0], config[1]);
    const bool enabled =
        plan.apply &&
        !plan.use_original_variant &&
        IsRendererWidescreenFixEnabled(
            GetRuntimeState().GetHookMode(HookId::cegui_renderer_constructor_2));

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) ||
        !bindings.get_system_singleton_ptr ||
        !bindings.get_gui_sheet ||
        !bindings.get_window_manager_singleton_ptr ||
        !bindings.window_manager_is_window_present ||
        !bindings.window_manager_get_window ||
        !bindings.window_get_parent ||
        !bindings.window_get_window_position ||
        !bindings.window_set_window_position ||
        !bindings.window_get_absolute_width ||
        !bindings.window_get_absolute_height ||
        !bindings.window_get_window_max_size ||
        !bindings.window_set_window_max_size ||
        !bindings.window_set_window_size ||
        !bindings.window_is_clipped_by_parent ||
        !bindings.window_set_clipped_by_parent) {
        return;
    }

    void* const window_manager = bindings.get_window_manager_singleton_ptr();
    if (!window_manager) {
        return;
    }

    void* const system = bindings.get_system_singleton_ptr();
    void* const active_gui_sheet = system ? bindings.get_gui_sheet(system) : nullptr;
    if (!active_gui_sheet) {
        return;
    }

    int changed_count = 0;
    std::size_t profile_count = 0;
    const WidescreenUiProfile* const profiles = GetWidescreenUiProfiles(&profile_count);
    for (std::size_t profile_index = 0; profile_index < profile_count; ++profile_index) {
        const auto& profile = profiles[profile_index];
        if (!profile.rules || profile.rule_count == 0) {
            continue;
        }

        void* const trigger = ResolveWindowByName(
            bindings,
            window_manager,
            profile.trigger_window_name);
        const bool profile_active =
            trigger && IsWindowInActiveGuiSheet(bindings, active_gui_sheet, trigger);
        if (enabled && !profile_active) {
            continue;
        }

        for (std::size_t rule_index = 0; rule_index < profile.rule_count; ++rule_index) {
            const auto& rule = profile.rules[rule_index];
            void* const window = ResolveHudWindow(bindings, window_manager, rule);
            auto state_it = g_original_window_states.find(&rule);
            if (!window) {
                if (state_it != g_original_window_states.end()) {
                    g_original_window_states.erase(state_it);
                }
                continue;
            }

            if (!profile_active &&
                (state_it == g_original_window_states.end() ||
                 state_it->second.window != window)) {
                continue;
            }

            auto& original_state = g_original_window_states[&rule];
            if (!CaptureHudWindowOriginalState(bindings, window, &original_state)) {
                continue;
            }

            if (enabled) {
                if (rule.allow_outside_parent &&
                    ApplyHudWindowParentClipping(bindings, window, false)) {
                    ++changed_count;
                }

                const auto window_plan = BuildWidescreenUiWindowPlan(
                    plan,
                    rule.horizontal_mode,
                    original_state.position.x.offset,
                    original_state.width,
                    rule.horizontal_padding_factor,
                    original_state.position.y.offset,
                    rule.widescreen_y_offset);
                if (window_plan.set_width &&
                    ApplyHudWindowMaximumWidth(
                        bindings,
                        window,
                        original_state.maximum_size,
                        window_plan.width)) {
                    ++changed_count;
                }
                if (window_plan.set_x &&
                    ApplyHudWindowLogicalX(bindings, window, window_plan.x)) {
                    ++changed_count;
                }
                if (window_plan.set_y &&
                    ApplyHudWindowLogicalY(bindings, window, window_plan.y)) {
                    ++changed_count;
                }
                if (window_plan.set_width &&
                    ApplyHudWindowLogicalSize(
                        bindings,
                        window,
                        window_plan.width,
                        original_state.height)) {
                    ++changed_count;
                }
                continue;
            }

            if (RestoreHudWindowPosition(bindings, window, original_state.position)) {
                ++changed_count;
            }
            if (rule.horizontal_mode == WidescreenUiHorizontalMode::stretch_between_edges &&
                ApplyHudWindowLogicalSize(
                    bindings,
                    window,
                    original_state.width,
                    original_state.height)) {
                ++changed_count;
            }
            if (RestoreHudWindowMaximumSize(
                    bindings,
                    window,
                    original_state.maximum_size)) {
                ++changed_count;
            }
            if (ApplyHudWindowParentClipping(
                    bindings,
                    window,
                    original_state.clipped_by_parent)) {
                ++changed_count;
            }
            g_original_window_states.erase(&rule);
        }
    }

    if (changed_count > 0) {
        LogHudLayoutFixups(plan, enabled, changed_count);
    }
}

}  // namespace pal4::inject

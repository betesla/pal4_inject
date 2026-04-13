#include "hud_layout_fixups.h"

#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>

#include "cegui_bindings.h"
#include "hook_logging.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

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

struct HudWindowFixupRule {
    std::string_view debug_name;
    std::string_view prefixed_name;
    std::string_view raw_name;
    float base_x = 0.0F;
    WidescreenHudAnchor anchor = WidescreenHudAnchor::none;
};

constexpr auto kHudWindowFixupRules = std::to_array<HudWindowFixupRule>({
    {
        "minimap_root",
        "minimap/zhujiemian",
        "",
        0.0F,
        WidescreenHudAnchor::left_edge,
    },
    {
        "portrait_root",
        "portrait/zhujiemian",
        "",
        0.0F,
        WidescreenHudAnchor::right_edge,
    },
    {
        "portrait_jump_hint",
        "portrait/ImgJumpHint",
        "ImgJumpHint",
        161.0F,
        WidescreenHudAnchor::right_edge,
    },
    {
        "portrait_timer",
        "portrait/StaticTimer",
        "StaticTimer",
        600.0F,
        WidescreenHudAnchor::right_edge,
    },
});

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
    const HudWindowFixupRule& rule) {
    if (void* window = ResolveWindowByName(bindings, window_manager, rule.prefixed_name)) {
        return window;
    }
    return ResolveWindowByName(bindings, window_manager, rule.raw_name);
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

void LogHudLayoutFixups(
    const CeguiWidescreenPlan& plan,
    const bool enabled,
    const int changed_count) {
    std::ostringstream out;
    out
        << "hook=hud_layout_fixups"
        << " enabled=" << (enabled ? 1 : 0)
        << " width=" << plan.width
        << " height=" << plan.height
        << " changed=" << changed_count;
    AppendHookEventLog(HookId::cegui_renderer_constructor_2, out.str());
}

}  // namespace

void RefreshWidescreenHudLayoutFixups() {
    const int* const config = ReadGameConfigPointer();
    if (!config) {
        return;
    }

    const auto plan = BuildCeguiWidescreenPlan(config[0], config[1]);
    const bool enabled =
        plan.apply &&
        !plan.use_original_variant &&
        IsRendererWidescreenFixEnabled(
            GetRuntimeState().GetHookMode(HookId::cegui_renderer_constructor_2));

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) ||
        !bindings.get_window_manager_singleton_ptr ||
        !bindings.window_manager_is_window_present ||
        !bindings.window_manager_get_window ||
        !bindings.window_get_window_position ||
        !bindings.window_set_window_position) {
        return;
    }

    void* const window_manager = bindings.get_window_manager_singleton_ptr();
    if (!window_manager) {
        return;
    }

    int changed_count = 0;
    for (const auto& rule : kHudWindowFixupRules) {
        void* const window = ResolveHudWindow(bindings, window_manager, rule);
        if (!window) {
            continue;
        }

        const float target_x = enabled
            ? ComputeWidescreenHudLogicalX(plan, rule.base_x, rule.anchor)
            : rule.base_x;
        if (ApplyHudWindowLogicalX(bindings, window, target_x)) {
            ++changed_count;
        }
    }

    if (changed_count > 0) {
        LogHudLayoutFixups(plan, enabled, changed_count);
    }
}

}  // namespace pal4::inject

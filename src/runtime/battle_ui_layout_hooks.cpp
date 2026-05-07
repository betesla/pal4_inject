#include "battle_ui_layout_hooks.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <intrin.h>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_bindings.h"
#include "cegui_renderer_hooks.h"
#include "hook_logging.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using SetProperties4C2550Fn = int (__thiscall*)(void*, int, int);
using UiShowCombatHintFn = int (__fastcall*)(void*, void*, const char*, int, int, int, int);

constexpr std::ptrdiff_t kCombatHintWindowOffset = 0x24;
constexpr float kPositionEpsilon = 0.05F;

struct SetPropertiesCallsiteRule {
    std::uint32_t return_ea = 0;
    std::string_view debug_name;
    enum class TransformMode : std::uint8_t {
        none = 0,
        translate_only = 1,
        scale_x_only = 2,
        project_full = 3,
    } transform_mode = TransformMode::translate_only;
    float output_x_offset_pixels = 0.0F;
    float output_y_offset_pixels = 0.0F;
};

constexpr auto kSetPropertiesCallsiteRules = std::to_array<SetPropertiesCallsiteRule>({
    SetPropertiesCallsiteRule{0x426AF8, "player_hp_delta", SetPropertiesCallsiteRule::TransformMode::scale_x_only},
    SetPropertiesCallsiteRule{0x540098, "combat_console_image_1", SetPropertiesCallsiteRule::TransformMode::scale_x_only},
    SetPropertiesCallsiteRule{0x5402EE, "combat_console_image_2", SetPropertiesCallsiteRule::TransformMode::scale_x_only},
    SetPropertiesCallsiteRule{0x54092E, "combat_console_image_3", SetPropertiesCallsiteRule::TransformMode::scale_x_only},
    SetPropertiesCallsiteRule{0x5405BF, "combat_message_hp_gain", SetPropertiesCallsiteRule::TransformMode::scale_x_only},
    SetPropertiesCallsiteRule{0x5406EE, "combat_message_hp_loss", SetPropertiesCallsiteRule::TransformMode::scale_x_only},
    SetPropertiesCallsiteRule{0x540827, "combat_message_mp_gain", SetPropertiesCallsiteRule::TransformMode::scale_x_only},
    SetPropertiesCallsiteRule{0x548447, "combat_win_banner", SetPropertiesCallsiteRule::TransformMode::project_full},
    SetPropertiesCallsiteRule{0x548626, "combat_rank_tedeng", SetPropertiesCallsiteRule::TransformMode::project_full},
    SetPropertiesCallsiteRule{0x5486C3, "combat_rank_yideng", SetPropertiesCallsiteRule::TransformMode::project_full},
    SetPropertiesCallsiteRule{0x548760, "combat_rank_erdeng", SetPropertiesCallsiteRule::TransformMode::project_full},
    SetPropertiesCallsiteRule{0x5487FD, "combat_rank_sandeng", SetPropertiesCallsiteRule::TransformMode::project_full},
    SetPropertiesCallsiteRule{0x54D716, "combat_dialog_victory_icon", SetPropertiesCallsiteRule::TransformMode::project_full},
    SetPropertiesCallsiteRule{0x57ADBD, "combat_fail_banner", SetPropertiesCallsiteRule::TransformMode::translate_only, -80.0F, 0.0F},
});

SetProperties4C2550Fn g_original_combat_console_set_image_position = nullptr;
UiShowCombatHintFn g_original_combat_console_set_image_position_2 = nullptr;
UiShowCombatHintFn g_original_ui_show_combat_result = nullptr;

bool IsRendererWidescreenFixEnabled(const HookMode mode) noexcept {
    return mode != HookMode::observe_only && mode != HookMode::mirror_compare;
}

std::uintptr_t MainModuleBase() {
    auto& state = GetRuntimeState();
    std::uintptr_t base = state.MainModuleBase();
    if (base == 0) {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        state.SetMainModuleBase(base);
    }
    return base;
}

void* ResolveRuntimeData(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(ida::ResolveRuntimeAddress(base, ida_ea));
}

int* ReadGameConfigPointer() {
    auto* config_ptr_address =
        static_cast<int**>(ResolveRuntimeData(ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

bool TryBuildActiveCenteredUiPlan(CeguiWidescreenPlan* out) {
    if (!out) {
        return false;
    }

    const int* const config = ReadGameConfigPointer();
    if (!config) {
        return false;
    }

    auto plan = BuildCeguiWidescreenPlan(config[0], config[1]);
    CeguiWidescreenPlan active_plan{};
    if (TryGetActiveCeguiWidescreenPlan(&active_plan)) {
        plan = active_plan;
    }
    if (!plan.apply || plan.use_original_variant) {
        return false;
    }

    if (!IsRendererWidescreenFixEnabled(
            GetRuntimeState().GetHookMode(HookId::cegui_renderer_constructor_2))) {
        return false;
    }

    *out = plan;
    return true;
}

std::uintptr_t ResolveRuntimeAddress(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    return base == 0 ? 0 : ida::ResolveRuntimeAddress(base, ida_ea);
}

const SetPropertiesCallsiteRule* MatchSetPropertiesCallsite(const void* return_address) {
    const auto runtime_return =
        reinterpret_cast<std::uintptr_t>(return_address);
    for (const auto& rule : kSetPropertiesCallsiteRules) {
        if (runtime_return == ResolveRuntimeAddress(rule.return_ea)) {
            return &rule;
        }
    }
    return nullptr;
}

bool TryGetBattleUiBindings(CeguiBindings* out) {
    if (!out) {
        return false;
    }

    std::string error;
    if (!TryGetCeguiBindings(out, &error)) {
        return false;
    }

    return out->window_get_window_position &&
        out->window_set_window_position;
}

bool IsCloseEnough(const float lhs, const float rhs) noexcept {
    return std::fabs(lhs - rhs) <= kPositionEpsilon;
}

bool ApplyWindowLogicalX(
    const CeguiBindings& bindings,
    void* const window,
    const float target_x) {
    if (!window ||
        !bindings.window_get_window_position ||
        !bindings.window_set_window_position) {
        return false;
    }

    const CeguiUVector2* const current = bindings.window_get_window_position(window);
    if (!current) {
        return false;
    }

    if (IsCloseEnough(current->x.offset, target_x)) {
        return false;
    }

    CeguiUVector2 updated = *current;
    updated.x.offset = target_x;
    bindings.window_set_window_position(window, updated);
    if (bindings.request_redraw) {
        bindings.request_redraw(window);
    }
    return true;
}

void* ReadWindowPointer(void* const object, const std::ptrdiff_t offset) {
    if (!object) {
        return nullptr;
    }
    auto* const bytes = static_cast<unsigned char*>(object);
    return *reinterpret_cast<void**>(bytes + offset);
}

const char* ToString(const SetPropertiesCallsiteRule::TransformMode mode) noexcept {
    switch (mode) {
    case SetPropertiesCallsiteRule::TransformMode::none:
        return "none";
    case SetPropertiesCallsiteRule::TransformMode::translate_only:
        return "translate";
    case SetPropertiesCallsiteRule::TransformMode::scale_x_only:
        return "scale_x";
    case SetPropertiesCallsiteRule::TransformMode::project_full:
        return "project";
    default:
        return "unknown";
    }
}

void LogBattleUiAdjustment(
    const HookId id,
    const std::string_view reason,
    const SetPropertiesCallsiteRule::TransformMode mode,
    const float before_x,
    const float before_y,
    const float after_x,
    const float after_y) {
    static int s_log_budget = 24;
    if (s_log_budget <= 0) {
        return;
    }
    --s_log_budget;

    std::ostringstream out;
    out
        << "hook=" << ToString(id)
        << " reason=" << reason
        << " transform=" << ToString(mode)
        << " before_x=" << before_x
        << " before_y=" << before_y
        << " after_x=" << after_x
        << " after_y=" << after_y;
    AppendHookEventLog(id, out.str());
}

void LogUnknownSetPropertiesCallsite(
    const void* return_address,
    const float x,
    const float y) {
    static int s_unknown_log_budget = 16;
    if (s_unknown_log_budget <= 0) {
        return;
    }
    --s_unknown_log_budget;

    std::ostringstream out;
    out
        << "hook=" << ToString(HookId::combat_console_set_image_position)
        << " reason=unmatched_return"
        << " return=" << return_address
        << " x=" << x
        << " y=" << y;
    AppendHookEventLog(HookId::combat_console_set_image_position, out.str());
}

bool IsTransformableUiCoordinate(const float value) noexcept {
    return std::isfinite(value) && std::fabs(value) <= 5000.0F;
}

int __fastcall Hook_SetProperties4C2550(
    void* self,
    void*,
    int x_bits,
    int y_bits) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::combat_console_set_image_position);

    if (!g_original_combat_console_set_image_position) {
        state.SetHookError(
            HookId::combat_console_set_image_position,
            "original SetProperties_4C2550 trampoline is null");
        state.SetLastError(
            "original SetProperties_4C2550 trampoline is null");
        return 0;
    }

    const HookMode mode = state.GetHookMode(HookId::combat_console_set_image_position);
    const float original_x = std::bit_cast<float>(static_cast<std::uint32_t>(x_bits));
    const float original_y = std::bit_cast<float>(static_cast<std::uint32_t>(y_bits));
    void* const return_address = _ReturnAddress();
    const auto* callsite_rule = MatchSetPropertiesCallsite(return_address);
    if (mode != HookMode::observe_only &&
        mode != HookMode::mirror_compare &&
        callsite_rule) {
        CeguiWidescreenPlan plan{};
        if (TryBuildActiveCenteredUiPlan(&plan) &&
            IsTransformableUiCoordinate(original_x) &&
            IsTransformableUiCoordinate(original_y)) {
            float adjusted_x = original_x;
            float adjusted_y = original_y;
            switch (callsite_rule->transform_mode) {
            case SetPropertiesCallsiteRule::TransformMode::none:
                break;
            case SetPropertiesCallsiteRule::TransformMode::translate_only:
                adjusted_x = original_x + plan.horizontal_bias_pixels;
                break;
            case SetPropertiesCallsiteRule::TransformMode::scale_x_only:
                // Floating combat numbers are rendered inside an already centered UI
                // hierarchy, so they need width scaling but not an extra center bias.
                adjusted_x = original_x * plan.uniform_scale;
                break;
            case SetPropertiesCallsiteRule::TransformMode::project_full:
                adjusted_x = ProjectWidescreenLogicalXToPhysicalPixels(plan, original_x);
                adjusted_y = original_y * plan.uniform_scale;
                break;
            }
            adjusted_x += callsite_rule->output_x_offset_pixels;
            adjusted_y += callsite_rule->output_y_offset_pixels;
            LogBattleUiAdjustment(
                HookId::combat_console_set_image_position,
                callsite_rule->debug_name,
                callsite_rule->transform_mode,
                original_x,
                original_y,
                adjusted_x,
                adjusted_y);
            x_bits = static_cast<int>(std::bit_cast<std::uint32_t>(adjusted_x));
            y_bits = static_cast<int>(std::bit_cast<std::uint32_t>(adjusted_y));
        }
    } else if (mode != HookMode::observe_only && mode != HookMode::mirror_compare) {
        LogUnknownSetPropertiesCallsite(return_address, original_x, original_y);
    }
    const int result = g_original_combat_console_set_image_position(self, x_bits, y_bits);
    state.ClearHookError(HookId::combat_console_set_image_position);
    return result;
}

int __fastcall Hook_UiShowCombatHint(
    void* self,
    void*,
    const char* text,
    const int a2,
    const int a3,
    const int a4,
    const int a5) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::combat_console_set_image_position_2);

    if (!g_original_combat_console_set_image_position_2) {
        state.SetHookError(
            HookId::combat_console_set_image_position_2,
            "original ui_showCombatHint trampoline is null");
        state.SetLastError(
            "original ui_showCombatHint trampoline is null");
        return 0;
    }

    const int result =
        g_original_combat_console_set_image_position_2(self, nullptr, text, a2, a3, a4, a5);
    state.ClearHookError(HookId::combat_console_set_image_position_2);
    return result;
}

int __fastcall Hook_UiShowCombatHint2(
    void* self,
    void*,
    const char* text,
    const int a2,
    const int a3,
    const int a4,
    const int a5) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::ui_show_combat_result);

    if (!g_original_ui_show_combat_result) {
        state.SetHookError(
            HookId::ui_show_combat_result,
            "original ui_showCombatHint2 trampoline is null");
        state.SetLastError("original ui_showCombatHint2 trampoline is null");
        return 0;
    }

    const int result =
        g_original_ui_show_combat_result(self, nullptr, text, a2, a3, a4, a5);
    state.ClearHookError(HookId::ui_show_combat_result);
    return result;
}

}  // namespace

void* GetBattleUiLayoutReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::combat_console_set_image_position:
        return reinterpret_cast<void*>(&Hook_SetProperties4C2550);
    case HookId::combat_console_set_image_position_2:
        return reinterpret_cast<void*>(&Hook_UiShowCombatHint);
    case HookId::ui_show_combat_result:
        return reinterpret_cast<void*>(&Hook_UiShowCombatHint2);
    default:
        return nullptr;
    }
}

void SetBattleUiLayoutOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::combat_console_set_image_position:
        g_original_combat_console_set_image_position =
            reinterpret_cast<SetProperties4C2550Fn>(trampoline);
        break;
    case HookId::combat_console_set_image_position_2:
        g_original_combat_console_set_image_position_2 =
            reinterpret_cast<UiShowCombatHintFn>(trampoline);
        break;
    case HookId::ui_show_combat_result:
        g_original_ui_show_combat_result =
            reinterpret_cast<UiShowCombatHintFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

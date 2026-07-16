#include "battle_ui_layout_hooks.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/ida_addresses.h"
#include "pal4inject/ui_coordinate_space.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using SetProperties4C2550Fn = int (__thiscall*)(void*, int, int);
using UiShowCombatHintFn = int (__fastcall*)(void*, void*, const char*, int, int, int, int);
using RenderTextAndImageFn = void (__thiscall*)(void*, int);

constexpr std::ptrdiff_t kRenderObjectPositionXOffset = 0x1C;
constexpr std::ptrdiff_t kRenderObjectPositionYOffset = 0x20;
constexpr std::uint32_t kCombatResultDefaultImageVtable = 0x844450;
constexpr std::uint32_t kCombatWorldTextVtable = 0x842698;
constexpr std::uint32_t kCombatFailIconVtable = 0x844460;
constexpr std::uint32_t kCombatWorldImageVtable = 0x844470;

SetProperties4C2550Fn g_original_combat_console_set_image_position = nullptr;
UiShowCombatHintFn g_original_combat_console_set_image_position_2 = nullptr;
UiShowCombatHintFn g_original_ui_show_combat_result = nullptr;
RenderTextAndImageFn g_original_render_text_and_image = nullptr;

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

std::uintptr_t ResolveRuntimeAddress(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    return base == 0 ? 0 : ida::ResolveRuntimeAddress(base, ida_ea);
}

int* ReadGameConfigPointer() {
    auto* config_ptr_address =
        static_cast<int**>(ResolveRuntimeData(ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

bool TryBuildActiveUiPlan(UiViewportPlan* out) {
    if (!out) {
        return false;
    }

    const int* const config = ReadGameConfigPointer();
    if (!config) {
        return false;
    }

    const auto plan = BuildActiveUiViewportPlan(config[0], config[1]);
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

bool IsTransformableUiCoordinate(const float value) noexcept {
    return std::isfinite(value) && std::fabs(value) <= 5000.0F;
}

bool IsApprox(const float lhs, const float rhs) noexcept {
    return std::fabs(lhs - rhs) < 0.01F;
}

float* FloatField(void* const object, const std::ptrdiff_t offset) {
    if (!object) {
        return nullptr;
    }
    return reinterpret_cast<float*>(static_cast<unsigned char*>(object) + offset);
}

enum class RenderTextAndImageSpace {
    none = 0,
    legacy_battle_overlay,
    active_ui_logical,
};

RenderTextAndImageSpace ClassifyRenderTextAndImageObject(void* const object) {
    if (!object) {
        return RenderTextAndImageSpace::none;
    }

    const auto vtable = *reinterpret_cast<std::uintptr_t*>(object);
    if (vtable == ResolveRuntimeAddress(kCombatWorldTextVtable) ||
        vtable == ResolveRuntimeAddress(kCombatWorldImageVtable)) {
        return RenderTextAndImageSpace::legacy_battle_overlay;
    }
    if (vtable == ResolveRuntimeAddress(kCombatFailIconVtable)) {
        return RenderTextAndImageSpace::active_ui_logical;
    }

    if (vtable == ResolveRuntimeAddress(kCombatResultDefaultImageVtable)) {
        const float* const x = FloatField(object, kRenderObjectPositionXOffset);
        const float* const y = FloatField(object, kRenderObjectPositionYOffset);
        if (x && y && IsApprox(*x, 400.0F) && IsApprox(*y, 280.0F)) {
            return RenderTextAndImageSpace::active_ui_logical;
        }
    }
    return RenderTextAndImageSpace::none;
}

struct RenderTextAndImagePatchState {
    bool applied = false;
    float original_x = 0.0F;
    float original_y = 0.0F;
    float patched_x = 0.0F;
    float patched_y = 0.0F;
};

bool ApplyRenderTextAndImageViewportTransform(
    void* const object,
    const UiViewportPlan& plan,
    RenderTextAndImagePatchState* const patch_state) {
    if (!object || !patch_state) {
        return false;
    }
    const auto space = ClassifyRenderTextAndImageObject(object);
    if (space == RenderTextAndImageSpace::none) {
        return false;
    }

    float* const x = FloatField(object, kRenderObjectPositionXOffset);
    float* const y = FloatField(object, kRenderObjectPositionYOffset);
    if (!x || !y) {
        return false;
    }

    patch_state->original_x = *x;
    patch_state->original_y = *y;
    patch_state->patched_x = *x;
    patch_state->patched_y = *y;

    if (!IsTransformableUiCoordinate(*x) || !IsTransformableUiCoordinate(*y)) {
        return false;
    }

    const bool transformed =
        space == RenderTextAndImageSpace::legacy_battle_overlay
            ? BattleOverlayLogicalToPhysical(
                plan,
                *x,
                *y,
                &patch_state->patched_x,
                &patch_state->patched_y)
            : CombatResultOverlayLogicalToUiLogical(
                plan,
                *x,
                *y,
                &patch_state->patched_x,
                &patch_state->patched_y);
    if (!transformed) {
        return false;
    }

    *x = patch_state->patched_x;
    *y = patch_state->patched_y;
    patch_state->applied = true;
    return true;
}

void RestoreRenderTextAndImageViewportTransform(
    void* const object,
    const RenderTextAndImagePatchState& patch_state) {
    if (!object || !patch_state.applied) {
        return;
    }

    if (float* const x = FloatField(object, kRenderObjectPositionXOffset)) {
        *x = patch_state.original_x;
    }
    if (float* const y = FloatField(object, kRenderObjectPositionYOffset)) {
        *y = patch_state.original_y;
    }
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

void __fastcall Hook_RenderTextAndImage(
    void* self,
    void*,
    const int arg1) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::render_text_and_image);

    if (!g_original_render_text_and_image) {
        state.SetHookError(
            HookId::render_text_and_image,
            "original RenderTextAndImage trampoline is null");
        state.SetLastError("original RenderTextAndImage trampoline is null");
        return;
    }

    const HookMode mode = state.GetHookMode(HookId::render_text_and_image);
    UiViewportPlan plan{};
    RenderTextAndImagePatchState patch_state{};
    if (mode != HookMode::observe_only &&
        mode != HookMode::mirror_compare &&
        TryBuildActiveUiPlan(&plan)) {
        ApplyRenderTextAndImageViewportTransform(self, plan, &patch_state);
    }

    g_original_render_text_and_image(self, arg1);
    RestoreRenderTextAndImageViewportTransform(self, patch_state);
    state.ClearHookError(HookId::render_text_and_image);
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
    case HookId::render_text_and_image:
        return reinterpret_cast<void*>(&Hook_RenderTextAndImage);
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
    case HookId::render_text_and_image:
        g_original_render_text_and_image =
            reinterpret_cast<RenderTextAndImageFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

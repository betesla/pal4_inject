#include "minimap_hooks.h"

#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_renderer_hooks.h"
#include "hook_logging.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using SetupMinimapTextureFn = void (__thiscall*)(void*, int, int, int, int);

SetupMinimapTextureFn g_original_setup_minimap_texture = nullptr;

int* ReadGameConfigPointer() {
    const auto base = GetRuntimeState().MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    auto* config_ptr_address = reinterpret_cast<int**>(
        ida::ResolveRuntimeAddress(base, ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

void LogMinimapLayoutEvent(
    const int width,
    const int height,
    const WidescreenMinimapPlacement& placement) {
    std::ostringstream out;
    out
        << "hook=setup_minimap_texture"
        << " width=" << width
        << " height=" << height
        << " x=" << placement.x
        << " y=" << placement.y
        << " w=" << placement.width
        << " h=" << placement.height;
    AppendHookEventLog(HookId::setup_minimap_texture, out.str());
}

void __fastcall Hook_SetupMinimapTexture(
    void* self,
    void*,
    const int arg_x,
    const int arg_y,
    const int arg_w,
    const int arg_h) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::setup_minimap_texture);

    if (!g_original_setup_minimap_texture) {
        state.SetHookError(
            HookId::setup_minimap_texture,
            "original SetupMinimapTexture trampoline is null");
        state.SetLastError("original SetupMinimapTexture trampoline is null");
        return;
    }

    const HookMode mode = state.GetHookMode(HookId::setup_minimap_texture);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        g_original_setup_minimap_texture(self, arg_x, arg_y, arg_w, arg_h);
        return;
    }

    const HookMode renderer_mode = state.GetHookMode(HookId::cegui_renderer_constructor_2);
    if (renderer_mode == HookMode::observe_only || renderer_mode == HookMode::mirror_compare) {
        g_original_setup_minimap_texture(self, arg_x, arg_y, arg_w, arg_h);
        return;
    }

    if (const int* config = ReadGameConfigPointer()) {
        auto placement = BuildWidescreenMinimapPlacement(config[0], config[1]);
        CeguiWidescreenPlan active_plan{};
        if (TryGetActiveCeguiWidescreenPlan(&active_plan) &&
            active_plan.logical_horizontal_padding <= 0.0F) {
            placement.apply = false;
        }
        if (placement.apply) {
            LogMinimapLayoutEvent(config[0], config[1], placement);
            g_original_setup_minimap_texture(
                self,
                placement.x,
                placement.y,
                placement.width,
                placement.height);
            return;
        }
    }

    g_original_setup_minimap_texture(self, arg_x, arg_y, arg_w, arg_h);
}

}  // namespace

void* GetMinimapReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::setup_minimap_texture:
        return reinterpret_cast<void*>(&Hook_SetupMinimapTexture);
    default:
        return nullptr;
    }
}

void SetMinimapOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::setup_minimap_texture:
        g_original_setup_minimap_texture =
            reinterpret_cast<SetupMinimapTextureFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

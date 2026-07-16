#include "bink_video_hooks.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

#include "hook_logging.h"
#include "pal4inject/aspect_ratio_layout.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using BinkPlayerUpdateAndRenderFn = void (__thiscall*)(void*, int);
using BinkUpdateVideoFn = bool (__stdcall*)(float*);
using BinkPlayerCloseVideoFn = char (__stdcall*)(int);
using DrawTexturedRectangleFn = void (__cdecl*)(
    int,
    int,
    int,
    int,
    int,
    void*,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t);

BinkPlayerUpdateAndRenderFn g_original_bink_player_update_and_render = nullptr;

template <typename Fn>
Fn ResolveRuntimeFunction(const std::uint32_t ida_ea) {
    const auto base = GetRuntimeState().MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(ida::ResolveRuntimeAddress(base, ida_ea));
}

void* ResolveRuntimeData(const std::uint32_t ida_ea) {
    const auto base = GetRuntimeState().MainModuleBase();
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

void LogVideoRect(
    const HookId hook_id,
    const int screen_width,
    const int screen_height,
    const int video_width,
    const int video_height,
    const AspectRatioRect& rect) {
    std::ostringstream out;
    out
        << "hook=" << ToString(hook_id)
        << " screen=" << screen_width << "x" << screen_height
        << " video=" << video_width << "x" << video_height
        << " rect=" << rect.x << "," << rect.y
        << "," << rect.width << "x" << rect.height;
    AppendHookEventLog(hook_id, out.str());
}

bool TryBuildVideoRect(
    void* const self,
    AspectRatioRect* const out_rect,
    int* const out_screen_width,
    int* const out_screen_height,
    int* const out_video_width,
    int* const out_video_height,
    std::string* const error) {
    if (!self || !out_rect || !out_screen_width || !out_screen_height ||
        !out_video_width || !out_video_height) {
        if (error) {
            *error = "video layout request received null storage";
        }
        return false;
    }

    const auto* player_bytes = static_cast<const unsigned char*>(self);
    const auto* video_state =
        reinterpret_cast<const std::uint32_t*>(player_bytes + 4);
    const std::uint32_t bink_handle = video_state[0];
    if (bink_handle == 0) {
        if (error) {
            *error = "bink handle is null";
        }
        return false;
    }

    const auto* config = ReadGameConfigPointer();
    if (!config) {
        if (error) {
            *error = "game config pointer is null";
        }
        return false;
    }

    *out_screen_width = config[0];
    *out_screen_height = config[1];
    *out_video_width = static_cast<int>(*reinterpret_cast<const std::uint32_t*>(bink_handle + 0));
    *out_video_height = static_cast<int>(*reinterpret_cast<const std::uint32_t*>(bink_handle + 4));
    *out_rect = ComputeAspectFitRect(
        *out_screen_width,
        *out_screen_height,
        *out_video_width,
        *out_video_height);
    if (!out_rect->valid) {
        if (error) {
            *error = "video aspect-fit rect is invalid";
        }
        return false;
    }
    return true;
}

void FallbackToOriginal(
    const HookId hook_id,
    const HookMode mode,
    void* const self,
    const int render_context,
    const std::string_view error) {
    auto& state = GetRuntimeState();
    state.SetHookError(hook_id, error);
    state.SetLastError(std::string(error));
    if (mode == HookMode::replace_with_fallback &&
        g_original_bink_player_update_and_render) {
        g_original_bink_player_update_and_render(self, render_context);
    }
}

}  // namespace

void __fastcall Hook_BinkPlayer_UpdateAndRender(
    void* self,
    void*,
    const int render_context) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::bink_player_update_and_render);

    const HookMode mode = state.GetHookMode(HookId::bink_player_update_and_render);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        if (g_original_bink_player_update_and_render) {
            g_original_bink_player_update_and_render(self, render_context);
        } else {
            state.SetHookError(
                HookId::bink_player_update_and_render,
                "original bink update-and-render trampoline is null");
        }
        return;
    }

    const auto bink_update_video =
        ResolveRuntimeFunction<BinkUpdateVideoFn>(ida::kBinkUpdateVideo);
    const auto bink_player_close_video =
        ResolveRuntimeFunction<BinkPlayerCloseVideoFn>(ida::kBinkPlayerCloseVideo);
    const auto draw_textured_rectangle =
        ResolveRuntimeFunction<DrawTexturedRectangleFn>(ida::kDrawTexturedRectangle);
    if (!bink_update_video || !bink_player_close_video || !draw_textured_rectangle) {
        FallbackToOriginal(
            HookId::bink_player_update_and_render,
            mode,
            self,
            render_context,
            "bink render helpers are unavailable");
        return;
    }

    AspectRatioRect rect{};
    int screen_width = 0;
    int screen_height = 0;
    int video_width = 0;
    int video_height = 0;
    std::string error;
    if (!TryBuildVideoRect(
            self,
            &rect,
            &screen_width,
            &screen_height,
            &video_width,
            &video_height,
            &error)) {
        FallbackToOriginal(
            HookId::bink_player_update_and_render,
            mode,
            self,
            render_context,
            error);
        return;
    }

    auto* const player_bytes = static_cast<unsigned char*>(self);
    if (player_bytes[0] == 0) {
        state.ClearHookError(HookId::bink_player_update_and_render);
        return;
    }

    if (bink_update_video(reinterpret_cast<float*>(player_bytes + 4))) {
        void* const texture = *reinterpret_cast<void**>(player_bytes + 24);
        draw_textured_rectangle(
            render_context,
            rect.x,
            rect.y,
            rect.width,
            rect.height,
            texture,
            255,
            255,
            255,
            255);
        LogVideoRect(
            HookId::bink_player_update_and_render,
            screen_width,
            screen_height,
            video_width,
            video_height,
            rect);
    } else {
        void* const texture = *reinterpret_cast<void**>(player_bytes + 24);
        if (texture) {
            bink_player_close_video(reinterpret_cast<int>(self));
        }
    }

    state.ClearHookError(HookId::bink_player_update_and_render);
}

void* GetBinkVideoReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::bink_player_update_and_render:
        return reinterpret_cast<void*>(&Hook_BinkPlayer_UpdateAndRender);
    default:
        return nullptr;
    }
}

void SetBinkVideoOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::bink_player_update_and_render:
        g_original_bink_player_update_and_render =
            reinterpret_cast<BinkPlayerUpdateAndRenderFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

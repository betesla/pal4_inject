#include "save_thumbnail_hooks.h"

#include <array>
#include <cstdint>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hook_logging.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

struct CeguiRect {
    float top = 0.0F;
    float bottom = 0.0F;
    float left = 0.0F;
    float right = 0.0F;
};

using NumberedListItemRenderFn =
    void (__thiscall*)(void* self, void* render_cache, const CeguiRect* rect, float z, int enabled, const float* clipper);
using RenderCacheCacheImageFn =
    void (__thiscall*)(void* render_cache, const void* image, const CeguiRect* rect, float z, const void* colours, const CeguiRect* clipper, bool clip_to_display, bool unknown);
using CeguiColourCtorFn = void* (__thiscall*)(void* self, float alpha, float red, float green, float blue);
using CeguiColourRectCtorFn = void* (__thiscall*)(void* self, const void* colour);

constexpr std::ptrdiff_t kNumberedListItemHasSaveDataOffset = 0x1EC;
constexpr std::ptrdiff_t kNumberedListItemSaveImageOffset = 0x1FC;
constexpr float kSaveThumbnailTopInset = 7.0F;
constexpr float kSaveThumbnailBottomInset = 82.0F;
constexpr float kSaveThumbnailLeftInset = 495.0F;
constexpr float kSaveThumbnailRightInset = 593.0F;

NumberedListItemRenderFn g_original_numbered_list_item_render = nullptr;
RenderCacheCacheImageFn g_render_cache_cache_image = nullptr;
CeguiColourCtorFn g_cegui_colour_ctor = nullptr;
CeguiColourRectCtorFn g_cegui_colour_rect_ctor = nullptr;

RenderCacheCacheImageFn ResolveRenderCacheCacheImage() {
    if (g_render_cache_cache_image) {
        return g_render_cache_cache_image;
    }

    HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        module = GetModuleHandleA("CEGUIBase_d.dll");
    }
    if (!module) {
        return nullptr;
    }

    auto* const proc = GetProcAddress(
        module,
        "?cacheImage@RenderCache@CEGUI@@QAEXABVImage@2@ABVRect@2@MABVColourRect@2@PBV42@_N4@Z");
    g_render_cache_cache_image = reinterpret_cast<RenderCacheCacheImageFn>(proc);
    return g_render_cache_cache_image;
}

bool ResolveOpaqueColourRectConstructors(
    CeguiColourCtorFn* const colour_ctor,
    CeguiColourRectCtorFn* const colour_rect_ctor) {
    if (!colour_ctor || !colour_rect_ctor) {
        return false;
    }
    if (g_cegui_colour_ctor && g_cegui_colour_rect_ctor) {
        *colour_ctor = g_cegui_colour_ctor;
        *colour_rect_ctor = g_cegui_colour_rect_ctor;
        return true;
    }

    HMODULE module = GetModuleHandleA("CEGUIBase.dll");
    if (!module) {
        module = GetModuleHandleA("CEGUIBase_d.dll");
    }
    if (!module) {
        return false;
    }

    auto* const colour_proc = GetProcAddress(module, "??0colour@CEGUI@@QAE@MMMM@Z");
    auto* const colour_rect_proc =
        GetProcAddress(module, "??0ColourRect@CEGUI@@QAE@ABVcolour@1@@Z");
    if (!colour_proc || !colour_rect_proc) {
        return false;
    }

    g_cegui_colour_ctor = reinterpret_cast<CeguiColourCtorFn>(colour_proc);
    g_cegui_colour_rect_ctor = reinterpret_cast<CeguiColourRectCtorFn>(colour_rect_proc);
    *colour_ctor = g_cegui_colour_ctor;
    *colour_rect_ctor = g_cegui_colour_rect_ctor;
    return true;
}

bool BuildOpaqueWhiteColourRect(std::array<unsigned char, 96>* const colour_rect) {
    if (!colour_rect) {
        return false;
    }

    CeguiColourCtorFn colour_ctor = nullptr;
    CeguiColourRectCtorFn colour_rect_ctor = nullptr;
    if (!ResolveOpaqueColourRectConstructors(&colour_ctor, &colour_rect_ctor)) {
        return false;
    }

    std::array<unsigned char, 24> white_colour{};
    colour_ctor(white_colour.data(), 1.0F, 1.0F, 1.0F, 1.0F);
    colour_rect_ctor(colour_rect->data(), white_colour.data());
    return true;
}

void LogThumbnailRecache(const void* const image, const CeguiRect& rect) {
    static int s_log_budget = 8;
    if (s_log_budget <= 0) {
        return;
    }
    --s_log_budget;

    std::ostringstream out;
    out
        << "hook=numbered_list_item_render"
        << " action=recache_save_thumbnail"
        << " image=0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(image)
        << std::dec
        << " rect=" << rect.left << "," << rect.top << "," << rect.right << "," << rect.bottom;
    AppendHookEventLog(HookId::numbered_list_item_render, out.str());
}

void RecacheSaveThumbnail(
    void* const self,
    void* const render_cache,
    const CeguiRect* const item_rect,
    const float z) {
    if (!self || !render_cache || !item_rect) {
        return;
    }
    auto* const bytes = static_cast<const unsigned char*>(self);
    if (!*reinterpret_cast<const int*>(bytes + kNumberedListItemHasSaveDataOffset)) {
        return;
    }

    const void* const image =
        *reinterpret_cast<void* const*>(bytes + kNumberedListItemSaveImageOffset);
    if (!image) {
        return;
    }

    auto* const cache_image = ResolveRenderCacheCacheImage();
    if (!cache_image) {
        GetRuntimeState().SetHookError(
            HookId::numbered_list_item_render,
            "CEGUI RenderCache::cacheImage export is unavailable");
        return;
    }

    const CeguiRect thumbnail_rect{
        item_rect->top + kSaveThumbnailTopInset,
        item_rect->top + kSaveThumbnailBottomInset,
        item_rect->left + kSaveThumbnailLeftInset,
        item_rect->left + kSaveThumbnailRightInset,
    };
    std::array<unsigned char, 96> white{};
    if (!BuildOpaqueWhiteColourRect(&white)) {
        GetRuntimeState().SetHookError(
            HookId::numbered_list_item_render,
            "CEGUI colour / ColourRect constructors are unavailable");
        return;
    }

    cache_image(render_cache, image, &thumbnail_rect, z, white.data(), nullptr, false, false);
    LogThumbnailRecache(image, thumbnail_rect);
    GetRuntimeState().ClearHookError(HookId::numbered_list_item_render);
}

void __fastcall Hook_NumberedListItemRender(
    void* self,
    void*,
    void* render_cache,
    const CeguiRect* rect,
    const float z,
    const int enabled,
    const float* clipper) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::numbered_list_item_render);
    if (!g_original_numbered_list_item_render) {
        state.SetHookError(
            HookId::numbered_list_item_render,
            "original numberedListItem::render trampoline is null");
        return;
    }

    g_original_numbered_list_item_render(self, render_cache, rect, z, enabled, clipper);
    const HookMode mode = state.GetHookMode(HookId::numbered_list_item_render);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        return;
    }
    RecacheSaveThumbnail(self, render_cache, rect, z);
}

}  // namespace

void* GetSaveThumbnailReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::numbered_list_item_render:
        return reinterpret_cast<void*>(&Hook_NumberedListItemRender);
    default:
        return nullptr;
    }
}

void SetSaveThumbnailOriginalTrampoline(const HookId id, void* const trampoline) {
    switch (id) {
    case HookId::numbered_list_item_render:
        g_original_numbered_list_item_render =
            reinterpret_cast<NumberedListItemRenderFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

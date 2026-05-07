#include "cegui_renderer_hooks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_bindings.h"
#include "hook_logging.h"
#include "cegui_font_texture_registry.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "hud_layout_fixups.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using CeguiRendererConstructor2Fn = void* (__thiscall*)(void*);
using SetRenderStatesFn = void (__cdecl*)();
using PalGameIvInitCameraSubsystemFn = int (__cdecl*)();
using CameraGetActiveCameraInternalIdFn = int (__thiscall*)(void*);
using RenderGeometryAndResetCounterFn = void (__thiscall*)(void*);
using RenderStateSetFn = void (__cdecl*)(int, unsigned int);

struct CeguiRenderRectCopy {
    float top = 0.0F;
    float bottom = 0.0F;
    float left = 0.0F;
    float right = 0.0F;
};

struct PatchedRendererState {
    CeguiWidescreenPlan plan{};
    std::array<std::uintptr_t, 40> synthetic_vtable{};
    void** original_vtable = nullptr;
    float original_scale_x = 1.0F;
    float original_scale_y = 1.0F;
    bool applied = false;
    CeguiRenderRectCopy original_render_rect{};
    CeguiRenderRectCopy render_rect{};
};

constexpr std::size_t kDoRenderSlot = 7;
constexpr std::size_t kGetRenderRectSlot = 20;
constexpr unsigned int kRwRenderStateTextureRaster = 1;
constexpr unsigned int kRwRenderStateTextureFilter = 9;
constexpr unsigned int kRwTextureFilterNearest = 1;
constexpr unsigned int kRwTextureFilterLinear = 2;
constexpr std::ptrdiff_t kRendererQueuedRectsBeginOffset = 0xBC;
constexpr std::ptrdiff_t kRendererQueuedRectsEndOffset = 0xC0;
constexpr std::ptrdiff_t kRendererVertexBufferOffset = 0xCC;
constexpr std::ptrdiff_t kRendererVertexCountOffset = 0x108;
constexpr std::ptrdiff_t kRendererRenderRectTopOffset = 0xF4;
constexpr std::ptrdiff_t kRendererRenderRectBottomOffset = 0xF8;
constexpr std::ptrdiff_t kRendererRenderRectLeftOffset = 0xFC;
constexpr std::ptrdiff_t kRendererRenderRectRightOffset = 0x100;
constexpr std::ptrdiff_t kRendererScaleXOffset = 0x110;
constexpr std::ptrdiff_t kRendererScaleYOffset = 0x114;

CeguiRendererConstructor2Fn g_original_cegui_renderer_constructor_2 = nullptr;
SetRenderStatesFn g_set_render_states = nullptr;
PalGameIvInitCameraSubsystemFn g_pal_game_iv_init_camera_subsystem = nullptr;
CameraGetActiveCameraInternalIdFn g_camera_get_active_camera_internal_id = nullptr;
RenderGeometryAndResetCounterFn g_render_geometry_and_reset_counter = nullptr;
RenderStateSetFn g_render_state_set = nullptr;

std::mutex g_patched_renderer_mutex;
std::unordered_map<std::uintptr_t, std::unique_ptr<PatchedRendererState>> g_patched_renderers;
std::mutex g_layout_debug_mutex;
std::string g_layout_debug_summary = "not_run";

std::string FormatPointer(const void* value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(value);
    return out.str();
}

void SetLayoutDebugSummary(const std::string_view summary) {
    std::scoped_lock lock(g_layout_debug_mutex);
    g_layout_debug_summary = summary;
}

void LogWidescreenPlanRefreshed(
    const CeguiWidescreenPlan& old_plan,
    const CeguiWidescreenPlan& new_plan);

float AlignToHalfPixel(const float value) noexcept {
    return std::floor(value + 0.5F) - 0.5F;
}

float AlignFontLeadingEdge(const float value) noexcept {
    return std::floor(value);
}

float AlignFontTrailingEdge(const float value) noexcept {
    return std::ceil(value);
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

template <typename T>
T ResolveRuntimeFunction(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<T>(ida::ResolveRuntimeAddress(base, ida_ea));
}

void* ResolveRuntimeData(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(ida::ResolveRuntimeAddress(base, ida_ea));
}

bool EnsureRendererDependencies(std::string* error) {
    if (!g_set_render_states) {
        g_set_render_states = ResolveRuntimeFunction<SetRenderStatesFn>(ida::kSetRenderStates);
    }
    if (!g_pal_game_iv_init_camera_subsystem) {
        g_pal_game_iv_init_camera_subsystem =
            ResolveRuntimeFunction<PalGameIvInitCameraSubsystemFn>(
                ida::kPalGameIvInitCameraSubsystem);
    }
    if (!g_camera_get_active_camera_internal_id) {
        g_camera_get_active_camera_internal_id =
            ResolveRuntimeFunction<CameraGetActiveCameraInternalIdFn>(
                ida::kCameraGetActiveCameraInternalId);
    }
    if (!g_render_geometry_and_reset_counter) {
        g_render_geometry_and_reset_counter =
            ResolveRuntimeFunction<RenderGeometryAndResetCounterFn>(
                ida::kRenderGeometryAndResetCounter);
    }
    if (!g_render_state_set) {
        auto* render_state_table_ptr =
            static_cast<std::uintptr_t*>(ResolveRuntimeData(ida::kRenderStateInterfaceGlobal));
        if (render_state_table_ptr && *render_state_table_ptr) {
            g_render_state_set =
                *reinterpret_cast<RenderStateSetFn*>(*render_state_table_ptr + 0x20);
        }
    }

    if (!g_set_render_states ||
        !g_pal_game_iv_init_camera_subsystem ||
        !g_camera_get_active_camera_internal_id ||
        !g_render_geometry_and_reset_counter ||
        !g_render_state_set) {
        if (error) {
            *error = "renderer widescreen dependencies are unavailable";
        }
        return false;
    }
    return true;
}

int* ReadGameConfigPointer() {
    auto* config_ptr_address =
        static_cast<int**>(ResolveRuntimeData(ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

bool TryReadGuiSheetLogicalSize(float* out_width, float* out_height) {
    if (!out_width || !out_height) {
        return false;
    }

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) ||
        !bindings.get_system_singleton_ptr ||
        !bindings.get_gui_sheet ||
        !bindings.window_get_area) {
        return false;
    }

    void* const system = bindings.get_system_singleton_ptr();
    if (!system) {
        return false;
    }
    void* const gui_sheet = bindings.get_gui_sheet(system);
    if (!gui_sheet) {
        return false;
    }
    const CeguiURect* const area = bindings.window_get_area(gui_sheet);
    if (!area) {
        return false;
    }

    const float logical_width = area->max.x.offset - area->min.x.offset;
    const float logical_height = area->max.y.offset - area->min.y.offset;
    if (!std::isfinite(logical_width) ||
        !std::isfinite(logical_height) ||
        logical_width <= 0.0F ||
        logical_height <= 0.0F) {
        return false;
    }

    *out_width = logical_width;
    *out_height = logical_height;
    return true;
}

bool TryBuildGuiSheetAwareWidescreenPlan(
    const int width,
    const int height,
    CeguiWidescreenPlan* out) {
    if (!out) {
        return false;
    }

    float logical_width = 0.0F;
    float logical_height = 0.0F;
    if (!TryReadGuiSheetLogicalSize(&logical_width, &logical_height)) {
        return false;
    }

    *out = BuildCeguiWidescreenPlanForLogicalSize(
        width,
        height,
        logical_width,
        logical_height);
    return true;
}

bool CaptureWindowTreeMaxRect(
    const CeguiBindings& bindings,
    const void* window,
    float* max_right,
    float* max_bottom) {
    if (!window || !max_right || !max_bottom) {
        return false;
    }

    const float left = bindings.window_get_absolute_x
        ? bindings.window_get_absolute_x(window)
        : 0.0F;
    const float top = bindings.window_get_absolute_y
        ? bindings.window_get_absolute_y(window)
        : 0.0F;
    const float width = bindings.window_get_absolute_width
        ? bindings.window_get_absolute_width(window)
        : 0.0F;
    const float height = bindings.window_get_absolute_height
        ? bindings.window_get_absolute_height(window)
        : 0.0F;
    if (std::isfinite(left) &&
        std::isfinite(top) &&
        std::isfinite(width) &&
        std::isfinite(height) &&
        width > 0.0F &&
        height > 0.0F) {
        *max_right = std::max(*max_right, left + width);
        *max_bottom = std::max(*max_bottom, top + height);
    }

    const unsigned int child_count = bindings.window_get_child_count
        ? bindings.window_get_child_count(window)
        : 0;
    for (unsigned int i = 0; i < child_count; ++i) {
        const void* const child = bindings.window_get_child_at_index
            ? bindings.window_get_child_at_index(window, i)
            : nullptr;
        if (child) {
            CaptureWindowTreeMaxRect(bindings, child, max_right, max_bottom);
        }
    }
    return true;
}

bool CaptureDesktopAnchoredWindowTreeMaxRect(
    const CeguiBindings& bindings,
    const void* gui_sheet,
    const float sheet_height,
    float* max_right,
    float* max_bottom) {
    if (!gui_sheet || !max_right || !max_bottom) {
        return false;
    }

    bool captured = false;
    const unsigned int child_count = bindings.window_get_child_count
        ? bindings.window_get_child_count(gui_sheet)
        : 0;
    for (unsigned int i = 0; i < child_count; ++i) {
        const void* const child = bindings.window_get_child_at_index
            ? bindings.window_get_child_at_index(gui_sheet, i)
            : nullptr;
        if (!child) {
            continue;
        }

        const float left = bindings.window_get_absolute_x(child);
        const float top = bindings.window_get_absolute_y(child);
        const float height = bindings.window_get_absolute_height(child);
        if (!std::isfinite(left) ||
            !std::isfinite(top) ||
            !std::isfinite(height) ||
            std::fabs(left) > 1.0F ||
            std::fabs(top) > 1.0F ||
            height < sheet_height - 1.0F) {
            continue;
        }

        CaptureWindowTreeMaxRect(bindings, child, max_right, max_bottom);
        captured = true;
    }

    return captured;
}

bool RefreshWideWindowParentClipping(
    const CeguiBindings& bindings,
    void* const window,
    const float parent_left,
    const float parent_top,
    const float parent_right,
    const float parent_bottom,
    const bool has_parent,
    float* subtree_right,
    float* subtree_bottom,
    int* changed_count) {
    if (!window || !subtree_right || !subtree_bottom || !changed_count) {
        return false;
    }

    const float left = bindings.window_get_absolute_x
        ? bindings.window_get_absolute_x(window)
        : 0.0F;
    const float top = bindings.window_get_absolute_y
        ? bindings.window_get_absolute_y(window)
        : 0.0F;
    const float width = bindings.window_get_absolute_width
        ? bindings.window_get_absolute_width(window)
        : 0.0F;
    const float height = bindings.window_get_absolute_height
        ? bindings.window_get_absolute_height(window)
        : 0.0F;
    const float right = left + width;
    const float bottom = top + height;
    float max_right = std::isfinite(right) ? right : 0.0F;
    float max_bottom = std::isfinite(bottom) ? bottom : 0.0F;

    const unsigned int child_count = bindings.window_get_child_count
        ? bindings.window_get_child_count(window)
        : 0;
    for (unsigned int i = 0; i < child_count; ++i) {
        void* const child = bindings.window_get_child_at_index
            ? bindings.window_get_child_at_index(window, i)
            : nullptr;
        if (!child) {
            continue;
        }
        float child_right = 0.0F;
        float child_bottom = 0.0F;
        if (RefreshWideWindowParentClipping(
                bindings,
                child,
                left,
                top,
                right,
                bottom,
                true,
                &child_right,
                &child_bottom,
                changed_count)) {
            max_right = std::max(max_right, child_right);
            max_bottom = std::max(max_bottom, child_bottom);
        }
    }

    const bool exceeds_parent =
        has_parent &&
        (left < parent_left - 1.0F ||
         top < parent_top - 1.0F ||
         right > parent_right + 1.0F ||
         bottom > parent_bottom + 1.0F);
    const bool has_wide_child =
        max_right > right + 1.0F ||
        max_bottom > bottom + 1.0F;
    if ((exceeds_parent || has_wide_child) && bindings.window_set_clipped_by_parent) {
        bindings.window_set_clipped_by_parent(window, false);
        if (bindings.request_redraw) {
            bindings.request_redraw(window);
        }
        ++(*changed_count);
    }

    *subtree_right = max_right;
    *subtree_bottom = max_bottom;
    return true;
}

void RefreshWindowTreeLayoutDebugSummary() {
    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) ||
        !bindings.get_system_singleton_ptr ||
        !bindings.get_gui_sheet ||
        !bindings.window_get_absolute_x ||
        !bindings.window_get_absolute_y ||
        !bindings.window_get_absolute_width ||
        !bindings.window_get_absolute_height ||
        !bindings.window_get_child_count ||
        !bindings.window_get_child_at_index ||
        !bindings.window_set_clipped_by_parent) {
        std::ostringstream out;
        out
            << "bindings_missing"
            << " get_system=" << (bindings.get_system_singleton_ptr ? 1 : 0)
            << " get_sheet=" << (bindings.get_gui_sheet ? 1 : 0)
            << " get_abs_w=" << (bindings.window_get_absolute_width ? 1 : 0)
            << " child_count=" << (bindings.window_get_child_count ? 1 : 0)
            << " set_clip_parent=" << (bindings.window_set_clipped_by_parent ? 1 : 0)
            << " error=" << error;
        SetLayoutDebugSummary(out.str());
        return;
    }

    void* const system = bindings.get_system_singleton_ptr();
    if (!system) {
        SetLayoutDebugSummary("system_null");
        return;
    }
    void* const gui_sheet = bindings.get_gui_sheet(system);
    if (!gui_sheet) {
        SetLayoutDebugSummary("gui_sheet_null");
        return;
    }

    float max_right = 0.0F;
    float max_bottom = 0.0F;
    float anchored_right = 0.0F;
    float anchored_bottom = 0.0F;
    int changed_count = 0;
    const float sheet_width = bindings.window_get_absolute_width(gui_sheet);
    const float sheet_height = bindings.window_get_absolute_height(gui_sheet);
    const bool have_anchored_tree =
        CaptureDesktopAnchoredWindowTreeMaxRect(
            bindings,
            gui_sheet,
            sheet_height,
            &anchored_right,
            &anchored_bottom);
    RefreshWideWindowParentClipping(
        bindings,
        gui_sheet,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        false,
        &max_right,
        &max_bottom,
        &changed_count);
    std::ostringstream out;
    out
        << "ok"
        << " tree=" << max_right << "x" << max_bottom
        << " anchored=" << (have_anchored_tree ? anchored_right : 0.0F)
        << "x" << (have_anchored_tree ? anchored_bottom : 0.0F)
        << " sheet=" << sheet_width << "x" << sheet_height
        << " unclipped=" << changed_count;
    SetLayoutDebugSummary(out.str());
}

bool TryBuildWindowTreeAwareWidescreenPlan(
    const int width,
    const int height,
    CeguiWidescreenPlan* out) {
    if (!out) {
        return false;
    }

    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) ||
        !bindings.get_system_singleton_ptr ||
        !bindings.get_gui_sheet ||
        !bindings.window_get_absolute_x ||
        !bindings.window_get_absolute_y ||
        !bindings.window_get_absolute_width ||
        !bindings.window_get_absolute_height ||
        !bindings.window_get_child_count ||
        !bindings.window_get_child_at_index) {
        return false;
    }

    void* const system = bindings.get_system_singleton_ptr();
    if (!system) {
        return false;
    }
    const void* const gui_sheet = bindings.get_gui_sheet(system);
    if (!gui_sheet) {
        return false;
    }

    const float sheet_width = bindings.window_get_absolute_width(gui_sheet);
    const float sheet_height = bindings.window_get_absolute_height(gui_sheet);
    const float logical_height =
        std::isfinite(sheet_height) && sheet_height > 0.0F ? sheet_height : 600.0F;
    float max_right = 0.0F;
    float max_bottom = 0.0F;
    if (!CaptureDesktopAnchoredWindowTreeMaxRect(
            bindings,
            gui_sheet,
            logical_height,
            &max_right,
            &max_bottom)) {
        max_right =
            std::isfinite(sheet_width) && sheet_width > 0.0F ? sheet_width : 800.0F;
    }
    if (max_right <= 0.0F) {
        return false;
    }

    *out = BuildCeguiWidescreenPlanForLogicalSize(width, height, max_right, logical_height);
    return true;
}

PatchedRendererState* FindPatchedRendererState(const void* renderer) {
    std::scoped_lock lock(g_patched_renderer_mutex);
    const auto it = g_patched_renderers.find(reinterpret_cast<std::uintptr_t>(renderer));
    return it == g_patched_renderers.end() ? nullptr : it->second.get();
}

CeguiRenderRectCopy ReadRendererRenderRectFromObject(const void* const renderer) {
    auto* const bytes = static_cast<const unsigned char*>(renderer);
    CeguiRenderRectCopy rect{};
    rect.top = *reinterpret_cast<const float*>(bytes + kRendererRenderRectTopOffset);
    rect.bottom = *reinterpret_cast<const float*>(bytes + kRendererRenderRectBottomOffset);
    rect.left = *reinterpret_cast<const float*>(bytes + kRendererRenderRectLeftOffset);
    rect.right = *reinterpret_cast<const float*>(bytes + kRendererRenderRectRightOffset);
    return rect;
}

void WriteRendererRenderRectToObject(
    void* const renderer,
    const CeguiRenderRectCopy& rect) {
    auto* const bytes = static_cast<unsigned char*>(renderer);
    *reinterpret_cast<float*>(bytes + kRendererRenderRectTopOffset) = rect.top;
    *reinterpret_cast<float*>(bytes + kRendererRenderRectBottomOffset) = rect.bottom;
    *reinterpret_cast<float*>(bytes + kRendererRenderRectLeftOffset) = rect.left;
    *reinterpret_cast<float*>(bytes + kRendererRenderRectRightOffset) = rect.right;
}

void ApplyRendererStateToObject(
    void* renderer,
    PatchedRendererState& state,
    const bool enabled) {
    auto* bytes = static_cast<unsigned char*>(renderer);
    if (enabled) {
        WriteRendererRenderRectToObject(renderer, state.render_rect);
        *reinterpret_cast<float*>(bytes + kRendererScaleXOffset) = state.plan.uniform_scale;
        *reinterpret_cast<float*>(bytes + kRendererScaleYOffset) = state.plan.uniform_scale;
        *reinterpret_cast<void***>(renderer) =
            reinterpret_cast<void**>(state.synthetic_vtable.data());
    } else {
        WriteRendererRenderRectToObject(renderer, state.original_render_rect);
        *reinterpret_cast<float*>(bytes + kRendererScaleXOffset) = state.original_scale_x;
        *reinterpret_cast<float*>(bytes + kRendererScaleYOffset) = state.original_scale_y;
        *reinterpret_cast<void***>(renderer) = state.original_vtable;
    }
    state.applied = enabled;
}

void SetPatchedRendererPlan(
    void* const renderer,
    PatchedRendererState& state,
    const CeguiWidescreenPlan& plan) {
    state.plan = plan;
    state.render_rect.top = 0.0F;
    state.render_rect.bottom = plan.logical_height;
    state.render_rect.left = -plan.logical_horizontal_padding;
    state.render_rect.right = plan.logical_width + plan.logical_horizontal_padding;
    if (state.applied && renderer) {
        auto* const bytes = static_cast<unsigned char*>(renderer);
        WriteRendererRenderRectToObject(renderer, state.render_rect);
        *reinterpret_cast<float*>(bytes + kRendererScaleXOffset) = plan.uniform_scale;
        *reinterpret_cast<float*>(bytes + kRendererScaleYOffset) = plan.uniform_scale;
    }
}

void RefreshPatchedRendererPlanFromGuiSheet(
    void* const renderer,
    PatchedRendererState& state) {
    CeguiWidescreenPlan refreshed{};
    if (!TryBuildGuiSheetAwareWidescreenPlan(
            state.plan.width,
            state.plan.height,
            &refreshed)) {
        return;
    }

    if (refreshed.logical_width == state.plan.logical_width &&
        refreshed.logical_height == state.plan.logical_height &&
        refreshed.horizontal_bias_pixels == state.plan.horizontal_bias_pixels &&
        refreshed.logical_horizontal_padding == state.plan.logical_horizontal_padding) {
        return;
    }
    const CeguiWidescreenPlan old_plan = state.plan;
    SetPatchedRendererPlan(renderer, state, refreshed);
    LogWidescreenPlanRefreshed(old_plan, refreshed);
}

void RefreshPatchedRendererPlanFromWindowTree(
    void* const renderer,
    PatchedRendererState& state) {
    CeguiWidescreenPlan refreshed{};
    if (!TryBuildWindowTreeAwareWidescreenPlan(
            state.plan.width,
            state.plan.height,
            &refreshed)) {
        return;
    }

    if (std::fabs(refreshed.logical_width - state.plan.logical_width) <= 1.0F &&
        std::fabs(refreshed.logical_height - state.plan.logical_height) <= 1.0F &&
        std::fabs(
            refreshed.horizontal_bias_pixels - state.plan.horizontal_bias_pixels) <= 1.0F &&
        std::fabs(
            refreshed.logical_horizontal_padding -
            state.plan.logical_horizontal_padding) <= 1.0F) {
        return;
    }

    const CeguiWidescreenPlan old_plan = state.plan;
    SetPatchedRendererPlan(renderer, state, refreshed);
    LogWidescreenPlanRefreshed(old_plan, refreshed);
}

void RefreshWindowTreeLayoutDiagnostics() {
    RefreshWindowTreeLayoutDebugSummary();
}

int __fastcall Hook_CeguiRendererDoRenderWide(void* self, void*) {
    auto* patched = FindPatchedRendererState(self);
    if (!patched) {
        return 0;
    }
    if (!g_set_render_states ||
        !g_pal_game_iv_init_camera_subsystem ||
        !g_camera_get_active_camera_internal_id ||
        !g_render_geometry_and_reset_counter ||
        !g_render_state_set) {
        return 0;
    }

    auto* bytes = static_cast<unsigned char*>(self);
    g_set_render_states();
    std::string ignored_error;
    RefreshKnownDynamicFontTextures(&ignored_error);

    const int vertex_buffer = *reinterpret_cast<int*>(bytes + kRendererVertexBufferOffset);
    int current_texture = 0;
    unsigned int current_filter = kRwTextureFilterLinear;
    const unsigned int desired_filter =
        GetRuntimeState().GetUiTextureFilter() == UiTextureFilter::nearest
        ? kRwTextureFilterNearest
        : kRwTextureFilterLinear;
    const int camera_system = g_pal_game_iv_init_camera_subsystem();
    const int active_camera_internal =
        g_camera_get_active_camera_internal_id(reinterpret_cast<void*>(camera_system));
    auto* quad = *reinterpret_cast<unsigned char**>(bytes + kRendererQueuedRectsBeginOffset);
    const auto* quad_end =
        *reinterpret_cast<unsigned char* const*>(bytes + kRendererQueuedRectsEndOffset);
    RefreshPatchedRendererPlanFromGuiSheet(self, *patched);
    RefreshPatchedRendererPlanFromWindowTree(self, *patched);
    RefreshWindowTreeLayoutDiagnostics();
    const float reciprocal_camera_scale =
        1.0F / *reinterpret_cast<const float*>(
            *reinterpret_cast<const int*>(active_camera_internal) + 128);

    if (quad != quad_end) {
        auto* quad_fields = quad + 41;
        do {
            const int texture_handle = *reinterpret_cast<const int*>(quad);
            const bool is_font_texture =
                IsKnownDynamicFontTexture(
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(texture_handle)));
            // IDA-confirmed RenderWare state 9 is the texture filter. Keep it
            // runtime-selectable so the panel can compare linear vs nearest UI scaling.
            if (current_texture != texture_handle) {
                g_render_geometry_and_reset_counter(self);
                const unsigned int texture_stage =
                    **reinterpret_cast<const unsigned int* const*>(*reinterpret_cast<const int*>(quad) + 8);
                g_render_state_set(kRwRenderStateTextureRaster, texture_stage);
                current_texture = texture_handle;
            }
            if (current_filter != desired_filter) {
                g_render_geometry_and_reset_counter(self);
                g_render_state_set(kRwRenderStateTextureFilter, desired_filter);
                current_filter = desired_filter;
            }

            const auto vertex_index =
                *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset);
            const int target = vertex_buffer + 28 * vertex_index;
            const float scale_x =
                *reinterpret_cast<const float*>(bytes + kRendererScaleXOffset);
            const float scale_y =
                *reinterpret_cast<const float*>(bytes + kRendererScaleYOffset);
            const float bias_x = patched->plan.horizontal_bias_pixels;
            const float raw_left =
                *reinterpret_cast<const float*>(quad_fields - 29) * scale_x + bias_x;
            const float raw_right =
                *reinterpret_cast<const float*>(quad_fields - 25) * scale_x + bias_x;
            const float raw_top =
                *reinterpret_cast<const float*>(quad_fields - 37) * scale_y;
            const float raw_bottom =
                *reinterpret_cast<const float*>(quad_fields - 33) * scale_y;
            const float aligned_left =
                is_font_texture ? AlignFontLeadingEdge(raw_left) : AlignToHalfPixel(raw_left);
            const float aligned_right =
                is_font_texture ? AlignFontTrailingEdge(raw_right) : AlignToHalfPixel(raw_right);
            const float aligned_top =
                is_font_texture ? AlignFontLeadingEdge(raw_top) : AlignToHalfPixel(raw_top);
            const float aligned_bottom =
                is_font_texture ? AlignFontTrailingEdge(raw_bottom) : AlignToHalfPixel(raw_bottom);

            *reinterpret_cast<float*>(target) = aligned_left;
            *reinterpret_cast<float*>(target + 4) = aligned_top;
            *reinterpret_cast<float*>(target + 8) =
                *reinterpret_cast<const float*>(quad_fields - 21);
            const std::uint16_t color0 =
                static_cast<std::uint16_t>(quad_fields[-1]) |
                (static_cast<std::uint16_t>(quad_fields[2]) << 8);
            *reinterpret_cast<std::uint32_t*>(target + 16) =
                static_cast<std::uint32_t>(quad_fields[1]) |
                ((static_cast<std::uint32_t>(quad_fields[0]) |
                  (static_cast<std::uint32_t>(color0) << 8)) << 8);
            *reinterpret_cast<float*>(target + 20) =
                *reinterpret_cast<const float*>(quad_fields - 9);
            *reinterpret_cast<float*>(target + 24) =
                *reinterpret_cast<const float*>(quad_fields - 17);
            *reinterpret_cast<float*>(target + 12) = reciprocal_camera_scale;

            const int vertex_index_1 = vertex_index + 1;
            *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset) = vertex_index_1;
            const int target_1 = vertex_buffer + 28 * vertex_index_1;
            *reinterpret_cast<float*>(target_1) = aligned_left;
            *reinterpret_cast<float*>(target_1 + 4) = aligned_bottom;
            *reinterpret_cast<float*>(target_1 + 8) =
                *reinterpret_cast<const float*>(quad_fields - 21);
            const std::uint16_t color1 =
                static_cast<std::uint16_t>(quad_fields[7]) |
                (static_cast<std::uint16_t>(quad_fields[10]) << 8);
            *reinterpret_cast<std::uint32_t*>(target_1 + 16) =
                static_cast<std::uint32_t>(quad_fields[9]) |
                ((static_cast<std::uint32_t>(quad_fields[8]) |
                  (static_cast<std::uint32_t>(color1) << 8)) << 8);
            *reinterpret_cast<float*>(target_1 + 20) =
                *reinterpret_cast<const float*>(quad_fields - 9);
            *reinterpret_cast<float*>(target_1 + 24) =
                *reinterpret_cast<const float*>(quad_fields - 13);
            *reinterpret_cast<float*>(target_1 + 12) = reciprocal_camera_scale;

            const int vertex_index_2 = vertex_index_1 + 1;
            *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset) = vertex_index_2;
            const int target_2 = vertex_buffer + 28 * vertex_index_2;
            *reinterpret_cast<float*>(target_2) = aligned_right;
            *reinterpret_cast<float*>(target_2 + 4) = aligned_bottom;
            *reinterpret_cast<float*>(target_2 + 8) =
                *reinterpret_cast<const float*>(quad_fields - 21);
            const std::uint16_t color2 =
                static_cast<std::uint16_t>(quad_fields[11]) |
                (static_cast<std::uint16_t>(quad_fields[14]) << 8);
            *reinterpret_cast<std::uint32_t*>(target_2 + 16) =
                static_cast<std::uint32_t>(quad_fields[13]) |
                ((static_cast<std::uint32_t>(quad_fields[12]) |
                  (static_cast<std::uint32_t>(color2) << 8)) << 8);
            *reinterpret_cast<float*>(target_2 + 20) =
                *reinterpret_cast<const float*>(quad_fields - 5);
            *reinterpret_cast<float*>(target_2 + 24) =
                *reinterpret_cast<const float*>(quad_fields - 13);
            *reinterpret_cast<float*>(target_2 + 12) = reciprocal_camera_scale;

            const int vertex_index_3 = vertex_index_2 + 1;
            *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset) = vertex_index_3;
            const int target_3 = vertex_buffer + 28 * vertex_index_3;
            *reinterpret_cast<float*>(target_3) = aligned_right;
            *reinterpret_cast<float*>(target_3 + 4) = aligned_top;
            *reinterpret_cast<float*>(target_3 + 8) =
                *reinterpret_cast<const float*>(quad_fields - 21);
            const std::uint16_t color3 =
                static_cast<std::uint16_t>(quad_fields[3]) |
                (static_cast<std::uint16_t>(quad_fields[6]) << 8);
            *reinterpret_cast<std::uint32_t*>(target_3 + 16) =
                static_cast<std::uint32_t>(quad_fields[5]) |
                ((static_cast<std::uint32_t>(quad_fields[4]) |
                  (static_cast<std::uint32_t>(color3) << 8)) << 8);
            *reinterpret_cast<float*>(target_3 + 20) =
                *reinterpret_cast<const float*>(quad_fields - 5);
            *reinterpret_cast<float*>(target_3 + 24) =
                *reinterpret_cast<const float*>(quad_fields - 17);
            *reinterpret_cast<float*>(target_3 + 12) = reciprocal_camera_scale;

            const int queued_vertices = vertex_index_3 + 1;
            *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset) = queued_vertices;
            if (queued_vertices > 4092) {
                g_render_geometry_and_reset_counter(self);
            }

            quad += 60;
            quad_fields += 60;
        } while (quad != quad_end);
    }

    g_render_geometry_and_reset_counter(self);
    return 0;
}

CeguiRenderRectCopy* __fastcall Hook_CeguiRendererGetRenderRectWide(
    void* self,
    void*,
    CeguiRenderRectCopy* out_rect) {
    if (!out_rect) {
        return nullptr;
    }
    if (auto* patched = FindPatchedRendererState(self)) {
        *out_rect = patched->render_rect;
        return out_rect;
    }

    auto* bytes = static_cast<unsigned char*>(self);
    *out_rect = ReadRendererRenderRectFromObject(bytes);
    return out_rect;
}

bool InstallPatchedRenderer(
    void* renderer,
    const CeguiWidescreenPlan& plan,
    std::string* error) {
    if (!renderer) {
        if (error) {
            *error = "renderer pointer is null";
        }
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(renderer);
    if (!vtable) {
        if (error) {
            *error = "renderer vtable is null";
        }
        return false;
    }

    auto* bytes = static_cast<unsigned char*>(renderer);
    auto state = std::make_unique<PatchedRendererState>();
    SetPatchedRendererPlan(renderer, *state, plan);
    state->original_vtable = vtable;
    state->original_scale_x = *reinterpret_cast<const float*>(bytes + kRendererScaleXOffset);
    state->original_scale_y = *reinterpret_cast<const float*>(bytes + kRendererScaleYOffset);
    state->original_render_rect = ReadRendererRenderRectFromObject(renderer);
    for (std::size_t i = 0; i < state->synthetic_vtable.size(); ++i) {
        state->synthetic_vtable[i] = reinterpret_cast<std::uintptr_t>(vtable[i]);
    }
    state->synthetic_vtable[kDoRenderSlot] =
        reinterpret_cast<std::uintptr_t>(&Hook_CeguiRendererDoRenderWide);
    state->synthetic_vtable[kGetRenderRectSlot] =
        reinterpret_cast<std::uintptr_t>(&Hook_CeguiRendererGetRenderRectWide);
    ApplyRendererStateToObject(renderer, *state, true);

    std::scoped_lock lock(g_patched_renderer_mutex);
    g_patched_renderers[reinterpret_cast<std::uintptr_t>(renderer)] = std::move(state);
    return true;
}

void LogWidescreenPatchApplied(
    const void* renderer,
    const CeguiWidescreenPlan& plan) {
    std::ostringstream out;
    out
        << "hook=cegui_renderer_ctor_2"
        << " renderer=" << FormatPointer(renderer)
        << " width=" << plan.width
        << " height=" << plan.height
        << " uniform_scale=" << plan.uniform_scale
        << " bias_px=" << plan.horizontal_bias_pixels
        << " logical_pad=" << plan.logical_horizontal_padding;
    AppendHookEventLog(HookId::cegui_renderer_constructor_2, out.str());
}

void LogWidescreenPatchSkipped(
    const void* renderer,
    const CeguiWidescreenPlan& plan,
    const std::string_view reason) {
    std::ostringstream out;
    out
        << "hook=cegui_renderer_ctor_2"
        << " renderer=" << FormatPointer(renderer)
        << " width=" << plan.width
        << " height=" << plan.height
        << " skipped=" << reason;
    AppendHookEventLog(HookId::cegui_renderer_constructor_2, out.str());
}

void LogWidescreenPlanRefreshed(
    const CeguiWidescreenPlan& old_plan,
    const CeguiWidescreenPlan& new_plan) {
    static int s_log_budget = 8;
    if (s_log_budget <= 0) {
        return;
    }
    --s_log_budget;

    std::ostringstream out;
    out
        << "hook=cegui_renderer_ctor_2"
        << " refreshed=root_area"
        << " old_logical=" << old_plan.logical_width << "x" << old_plan.logical_height
        << " new_logical=" << new_plan.logical_width << "x" << new_plan.logical_height
        << " old_bias_px=" << old_plan.horizontal_bias_pixels
        << " new_bias_px=" << new_plan.horizontal_bias_pixels;
    AppendHookEventLog(HookId::cegui_renderer_constructor_2, out.str());
}

void* __fastcall Hook_CeguiRendererConstructor2(void* self, void*) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::cegui_renderer_constructor_2);
    if (!g_original_cegui_renderer_constructor_2) {
        state.SetHookError(
            HookId::cegui_renderer_constructor_2,
            "original CEGUI_Renderer_Constructor_2 trampoline is null");
        return nullptr;
    }

    void* renderer = g_original_cegui_renderer_constructor_2(self);
    int* config = ReadGameConfigPointer();
    if (!config) {
        state.SetHookError(
            HookId::cegui_renderer_constructor_2,
            "g_GameConfig pointer is null");
        return renderer;
    }

    auto plan = BuildCeguiWidescreenPlan(config[0], config[1]);
    CeguiWidescreenPlan gui_sheet_plan{};
    if (TryBuildGuiSheetAwareWidescreenPlan(config[0], config[1], &gui_sheet_plan)) {
        plan = gui_sheet_plan;
    }
    const HookMode mode = state.GetHookMode(HookId::cegui_renderer_constructor_2);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        LogWidescreenPatchSkipped(renderer, plan, "mode_passthrough");
        return renderer;
    }
    if (!plan.apply) {
        return renderer;
    }
    if (plan.use_original_variant) {
        LogWidescreenPatchSkipped(renderer, plan, "original_variant");
        return renderer;
    }

    std::string error;
    if (!EnsureRendererDependencies(&error)) {
        state.SetHookError(HookId::cegui_renderer_constructor_2, error);
        state.SetLastError(error);
        LogWidescreenPatchSkipped(renderer, plan, "missing_dependencies");
        return renderer;
    }
    if (!InstallPatchedRenderer(renderer, plan, &error)) {
        state.SetHookError(HookId::cegui_renderer_constructor_2, error);
        state.SetLastError(error);
        LogWidescreenPatchSkipped(renderer, plan, "install_failed");
        return renderer;
    }

    state.ClearHookError(HookId::cegui_renderer_constructor_2);
    LogWidescreenPatchApplied(renderer, plan);
    return renderer;
}

}  // namespace

void* GetCeguiRendererReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::cegui_renderer_constructor_2:
        return reinterpret_cast<void*>(&Hook_CeguiRendererConstructor2);
    default:
        return nullptr;
    }
}

void SetCeguiRendererOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::cegui_renderer_constructor_2:
        g_original_cegui_renderer_constructor_2 =
            reinterpret_cast<CeguiRendererConstructor2Fn>(trampoline);
        break;
    default:
        break;
    }
}

void ApplyCeguiRendererHookMode(const HookMode mode) {
    const bool enabled =
        mode == HookMode::replace_with_fallback || mode == HookMode::replace_strict;
    std::scoped_lock lock(g_patched_renderer_mutex);
    for (auto& [renderer_key, state] : g_patched_renderers) {
        if (!state) {
            continue;
        }
        ApplyRendererStateToObject(reinterpret_cast<void*>(renderer_key), *state, enabled);
    }
    RefreshWidescreenHudLayoutFixups();
}

bool TryGetActiveCeguiWidescreenPlan(CeguiWidescreenPlan* out) {
    if (!out) {
        return false;
    }

    std::scoped_lock lock(g_patched_renderer_mutex);
    for (const auto& [renderer_key, state] : g_patched_renderers) {
        if (state && state->applied) {
            RefreshPatchedRendererPlanFromGuiSheet(
                reinterpret_cast<void*>(renderer_key),
                *state);
            RefreshPatchedRendererPlanFromWindowTree(
                reinterpret_cast<void*>(renderer_key),
                *state);
            *out = state->plan;
            return true;
        }
    }
    return false;
}

void RefreshCeguiWidescreenRuntimeLayout() {
    const HookMode mode =
        GetRuntimeState().GetHookMode(HookId::cegui_renderer_constructor_2);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        return;
    }

    std::scoped_lock lock(g_patched_renderer_mutex);
    for (const auto& [renderer_key, state] : g_patched_renderers) {
        if (!state || !state->applied) {
            continue;
        }
        auto* const renderer = reinterpret_cast<void*>(renderer_key);
        RefreshPatchedRendererPlanFromGuiSheet(renderer, *state);
        RefreshPatchedRendererPlanFromWindowTree(renderer, *state);
    }
    RefreshWindowTreeLayoutDiagnostics();
}

std::string BuildCeguiWidescreenRuntimeLayoutDebugSummary() {
    std::string layout_summary;
    {
        std::scoped_lock lock(g_layout_debug_mutex);
        layout_summary = g_layout_debug_summary;
    }

    std::scoped_lock lock(g_patched_renderer_mutex);
    for (const auto& [renderer_key, state] : g_patched_renderers) {
        if (!state || !state->applied) {
            continue;
        }
        const auto object_rect =
            ReadRendererRenderRectFromObject(reinterpret_cast<const void*>(renderer_key));
        std::ostringstream out;
        out
            << layout_summary
            << " renderer=" << FormatPointer(reinterpret_cast<const void*>(renderer_key))
            << " plan=" << state->plan.logical_width << "x" << state->plan.logical_height
            << " scale=" << state->plan.uniform_scale
            << " rect="
            << object_rect.left << "," << object_rect.top << ","
            << object_rect.right << "," << object_rect.bottom;
        return out.str();
    }
    return layout_summary;
}

}  // namespace pal4::inject

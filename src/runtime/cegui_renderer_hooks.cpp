#include "cegui_renderer_hooks.h"

#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cegui_bindings.h"
#include "hook_logging.h"
#include "input_hooks.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "hud_layout_fixups.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using CeguiRendererConstructor2Fn = void* (__thiscall*)(void*);
using CeguiSystemInitializeFn = float* (__thiscall*)(float*, void*, void*);
using SetRenderStatesFn = void (__cdecl*)();
using PalGameIvInitCameraSubsystemFn = int (__cdecl*)();
using CameraGetActiveCameraInternalIdFn = int (__thiscall*)(void*);
using RenderGeometryAndResetCounterFn = void (__thiscall*)(void*);
using RenderStateSetTextureFn = void (__cdecl*)(int, unsigned int);

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
    CeguiRenderRectCopy render_rect{};
};

constexpr std::size_t kDoRenderSlot = 7;
constexpr std::size_t kGetRenderRectSlot = 20;
constexpr std::ptrdiff_t kRendererQueuedRectsBeginOffset = 0xBC;
constexpr std::ptrdiff_t kRendererQueuedRectsEndOffset = 0xC0;
constexpr std::ptrdiff_t kRendererVertexBufferOffset = 0xCC;
constexpr std::ptrdiff_t kRendererVertexCountOffset = 0x108;
constexpr std::ptrdiff_t kRendererScaleXOffset = 0x110;
constexpr std::ptrdiff_t kRendererScaleYOffset = 0x114;
constexpr std::uint32_t kOpaqueBlackColor = 0xFF000000;

CeguiRendererConstructor2Fn g_original_cegui_renderer_constructor_2 = nullptr;
CeguiSystemInitializeFn g_original_cegui_system_initialize = nullptr;
SetRenderStatesFn g_set_render_states = nullptr;
PalGameIvInitCameraSubsystemFn g_pal_game_iv_init_camera_subsystem = nullptr;
CameraGetActiveCameraInternalIdFn g_camera_get_active_camera_internal_id = nullptr;
RenderGeometryAndResetCounterFn g_render_geometry_and_reset_counter = nullptr;
RenderStateSetTextureFn g_render_state_set_texture = nullptr;

std::mutex g_patched_renderer_mutex;
std::unordered_map<std::uintptr_t, std::unique_ptr<PatchedRendererState>> g_patched_renderers;

std::string FormatPointer(const void* value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(value);
    return out.str();
}

float AlignToHalfPixel(const float value) noexcept {
    return std::floor(value + 0.5F) - 0.5F;
}

struct PillarboxUiMarkers {
    bool main_menu_family_root = false;
    bool sys_toolbar_root = false;
    bool toolbar_overlay_root = false;
    bool btn_system_setting = false;
    bool setting_window_0 = false;
    bool setting_window_1 = false;
};

bool WindowNameMatches(
    const std::string_view actual,
    const std::string_view expected_leaf) noexcept {
    if (actual == expected_leaf) {
        return true;
    }
    return actual.size() > expected_leaf.size() &&
        actual[actual.size() - expected_leaf.size() - 1] == '/' &&
        actual.ends_with(expected_leaf);
}

constexpr std::array<std::string_view, 10> kMainMenuFamilyRootNames = {
    "MainWindow/Root",
    "loadWindow/Root",
    "CastWindow/Root",
    "IntroductionWindow/Root",
    "HelpWindow/Root",
    "gameInfo/Root",
    "moviePreviewWindow/Root",
    "PalTestWindow/Root",
    "PictureViewWindow/Root",
    "picturePreviewWindow/Root",
};

bool IsMainMenuFamilyRootName(const std::string_view name) noexcept {
    for (const std::string_view candidate : kMainMenuFamilyRootNames) {
        if (WindowNameMatches(name, candidate)) {
            return true;
        }
    }
    return false;
}

bool IsInGameToolbarBaseRootName(const std::string_view name) noexcept {
    return WindowNameMatches(name, "sysToolBar/Root");
}

constexpr std::array<std::string_view, 7> kToolbarOverlayRootNames = {
    "roleStateWindow/Root",
    "PropertyWindow/Root",
    "EquipmentWindow/Root",
    "magicWindow/Root",
    "SmithWindow/Root",
    "MissionWindow/Root",
    "SystemSetting/Root",
};

bool IsToolbarOverlayRootName(const std::string_view name) noexcept {
    for (const std::string_view candidate : kToolbarOverlayRootNames) {
        if (WindowNameMatches(name, candidate)) {
            return true;
        }
    }
    return false;
}

void CollectVisiblePillarboxUiMarkers(
    const CeguiBindings& bindings,
    void* const window,
    const unsigned int depth,
    PillarboxUiMarkers* const markers) {
    constexpr unsigned int kMaxUiTreeDepth = 64;
    if (!window ||
        !markers ||
        depth > kMaxUiTreeDepth ||
        !bindings.window_is_visible ||
        !bindings.window_get_name ||
        !bindings.cegui_string_c_str ||
        !bindings.window_get_child_count ||
        !bindings.window_get_child_at_index) {
        return;
    }

    const bool locally_visible = bindings.window_is_visible(window, true);
    const OpaqueCeguiString* const name_string = bindings.window_get_name(window);
    const char* const name_chars = name_string ? bindings.cegui_string_c_str(name_string) : nullptr;
    const std::string_view name = name_chars ? std::string_view(name_chars) : std::string_view();
    if (locally_visible) {
        if (IsMainMenuFamilyRootName(name)) {
            markers->main_menu_family_root = true;
        } else if (IsInGameToolbarBaseRootName(name)) {
            markers->sys_toolbar_root = true;
        } else if (IsToolbarOverlayRootName(name)) {
            markers->toolbar_overlay_root = true;
        } else if (WindowNameMatches(name, "BtnSystemSetting")) {
            markers->btn_system_setting = true;
        } else if (WindowNameMatches(name, "SettingWindow0")) {
            markers->setting_window_0 = true;
        } else if (WindowNameMatches(name, "SettingWindow1")) {
            markers->setting_window_1 = true;
        }
    }

    const unsigned int child_count = bindings.window_get_child_count(window);
    for (unsigned int index = 0; index < child_count; ++index) {
        CollectVisiblePillarboxUiMarkers(
            bindings,
            bindings.window_get_child_at_index(window, index),
            depth + 1,
            markers);
    }
}

bool HasVisiblePillarboxWhitelistedUi() {
    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error) ||
        !bindings.get_system_singleton_ptr ||
        !bindings.get_gui_sheet) {
        return false;
    }

    void* const system = bindings.get_system_singleton_ptr();
    void* const gui_sheet = system ? bindings.get_gui_sheet(system) : nullptr;
    PillarboxUiMarkers markers{};
    CollectVisiblePillarboxUiMarkers(bindings, gui_sheet, 0, &markers);

    const bool main_menu_context =
        markers.main_menu_family_root || ReadCurrentPalivEntry() == 0;
    const bool in_game_toolbar_context = markers.sys_toolbar_root;
    const bool toolbar_overlay_visible = markers.toolbar_overlay_root;
    const bool system_setting_visible =
        markers.btn_system_setting && (markers.setting_window_0 || markers.setting_window_1);
    return main_menu_context ||
        in_game_toolbar_context ||
        toolbar_overlay_visible ||
        system_setting_visible;
}

void WriteUiVertex(
    unsigned char* const target,
    const float x,
    const float y,
    const float z,
    const float reciprocal_camera_scale,
    const std::uint32_t color,
    const float u,
    const float v) {
    *reinterpret_cast<float*>(target) = x;
    *reinterpret_cast<float*>(target + 4) = y;
    *reinterpret_cast<float*>(target + 8) = z;
    *reinterpret_cast<float*>(target + 12) = reciprocal_camera_scale;
    *reinterpret_cast<std::uint32_t*>(target + 16) = color;
    *reinterpret_cast<float*>(target + 20) = u;
    *reinterpret_cast<float*>(target + 24) = v;
}

void AppendSolidQuad(
    void* const renderer,
    const int vertex_buffer,
    const float left,
    const float top,
    const float right,
    const float bottom,
    const float z,
    const float reciprocal_camera_scale,
    const std::uint32_t color) {
    auto* const bytes = static_cast<unsigned char*>(renderer);
    const int vertex_index =
        *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset);
    WriteUiVertex(
        reinterpret_cast<unsigned char*>(vertex_buffer + 28 * vertex_index),
        left,
        top,
        z,
        reciprocal_camera_scale,
        color,
        0.0F,
        0.0F);
    WriteUiVertex(
        reinterpret_cast<unsigned char*>(vertex_buffer + 28 * (vertex_index + 1)),
        left,
        bottom,
        z,
        reciprocal_camera_scale,
        color,
        0.0F,
        1.0F);
    WriteUiVertex(
        reinterpret_cast<unsigned char*>(vertex_buffer + 28 * (vertex_index + 2)),
        right,
        bottom,
        z,
        reciprocal_camera_scale,
        color,
        1.0F,
        1.0F);
    WriteUiVertex(
        reinterpret_cast<unsigned char*>(vertex_buffer + 28 * (vertex_index + 3)),
        right,
        top,
        z,
        reciprocal_camera_scale,
        color,
        1.0F,
        0.0F);
    *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset) = vertex_index + 4;
}

void DrawOriginalUiPillarboxMasks(
    void* const renderer,
    const PatchedRendererState& patched,
    const int vertex_buffer,
    const float reciprocal_camera_scale) {
    if (!ShouldDrawOriginalUiPillarboxMask(patched.plan)) {
        return;
    }
    if (!HasVisiblePillarboxWhitelistedUi()) {
        return;
    }

    const float left_width = patched.plan.horizontal_bias_pixels;
    const float right_start =
        static_cast<float>(patched.plan.width) - patched.plan.horizontal_bias_pixels;
    if (left_width <= 0.0F || right_start >= static_cast<float>(patched.plan.width)) {
        return;
    }

    g_render_state_set_texture(1, 0);
    AppendSolidQuad(
        renderer,
        vertex_buffer,
        0.0F,
        0.0F,
        AlignToHalfPixel(left_width),
        static_cast<float>(patched.plan.height),
        0.0F,
        reciprocal_camera_scale,
        kOpaqueBlackColor);
    AppendSolidQuad(
        renderer,
        vertex_buffer,
        AlignToHalfPixel(right_start),
        0.0F,
        static_cast<float>(patched.plan.width),
        static_cast<float>(patched.plan.height),
        0.0F,
        reciprocal_camera_scale,
        kOpaqueBlackColor);
    g_render_geometry_and_reset_counter(renderer);
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
    if (!g_render_state_set_texture) {
        auto* render_state_table_ptr =
            static_cast<std::uintptr_t*>(ResolveRuntimeData(ida::kRenderStateInterfaceGlobal));
        if (render_state_table_ptr && *render_state_table_ptr) {
            g_render_state_set_texture =
                *reinterpret_cast<RenderStateSetTextureFn*>(*render_state_table_ptr + 0x20);
        }
    }

    if (!g_set_render_states ||
        !g_pal_game_iv_init_camera_subsystem ||
        !g_camera_get_active_camera_internal_id ||
        !g_render_geometry_and_reset_counter ||
        !g_render_state_set_texture) {
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

PatchedRendererState* FindPatchedRendererState(const void* renderer) {
    std::scoped_lock lock(g_patched_renderer_mutex);
    const auto it = g_patched_renderers.find(reinterpret_cast<std::uintptr_t>(renderer));
    return it == g_patched_renderers.end() ? nullptr : it->second.get();
}

void ApplyRendererStateToObject(
    void* renderer,
    PatchedRendererState& state,
    const bool enabled) {
    auto* bytes = static_cast<unsigned char*>(renderer);
    if (enabled) {
        *reinterpret_cast<float*>(bytes + kRendererScaleXOffset) = state.plan.uniform_scale;
        *reinterpret_cast<float*>(bytes + kRendererScaleYOffset) = state.plan.uniform_scale;
        *reinterpret_cast<void***>(renderer) =
            reinterpret_cast<void**>(state.synthetic_vtable.data());
    } else {
        *reinterpret_cast<float*>(bytes + kRendererScaleXOffset) = state.original_scale_x;
        *reinterpret_cast<float*>(bytes + kRendererScaleYOffset) = state.original_scale_y;
        *reinterpret_cast<void***>(renderer) = state.original_vtable;
    }
    state.applied = enabled;
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
        !g_render_state_set_texture) {
        return 0;
    }

    auto* bytes = static_cast<unsigned char*>(self);
    g_set_render_states();

    const int vertex_buffer = *reinterpret_cast<int*>(bytes + kRendererVertexBufferOffset);
    int current_texture = 0;
    const int camera_system = g_pal_game_iv_init_camera_subsystem();
    const int active_camera_internal =
        g_camera_get_active_camera_internal_id(reinterpret_cast<void*>(camera_system));
    auto* quad = *reinterpret_cast<unsigned char**>(bytes + kRendererQueuedRectsBeginOffset);
    const auto* quad_end =
        *reinterpret_cast<unsigned char* const*>(bytes + kRendererQueuedRectsEndOffset);
    const float reciprocal_camera_scale =
        1.0F / *reinterpret_cast<const float*>(
            *reinterpret_cast<const int*>(active_camera_internal) + 128);

    DrawOriginalUiPillarboxMasks(
        self,
        *patched,
        vertex_buffer,
        reciprocal_camera_scale);
    current_texture = 0;

    if (quad != quad_end) {
        auto* quad_fields = quad + 41;
        do {
            const int texture_handle = *reinterpret_cast<const int*>(quad);
            if (current_texture != texture_handle) {
                g_render_geometry_and_reset_counter(self);
                const unsigned int texture_stage =
                    **reinterpret_cast<const unsigned int* const*>(*reinterpret_cast<const int*>(quad) + 8);
                g_render_state_set_texture(1, texture_stage);
                current_texture = texture_handle;
            }

            const auto vertex_index =
                *reinterpret_cast<int*>(bytes + kRendererVertexCountOffset);
            const int target = vertex_buffer + 28 * vertex_index;
            const float scale_x =
                *reinterpret_cast<const float*>(bytes + kRendererScaleXOffset);
            const float scale_y =
                *reinterpret_cast<const float*>(bytes + kRendererScaleYOffset);
            const float bias_x = patched->plan.horizontal_bias_pixels;

            *reinterpret_cast<float*>(target) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 29) * scale_x + bias_x);
            *reinterpret_cast<float*>(target + 4) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 37) * scale_y);
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
            *reinterpret_cast<float*>(target_1) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 29) * scale_x + bias_x);
            *reinterpret_cast<float*>(target_1 + 4) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 33) * scale_y);
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
            *reinterpret_cast<float*>(target_2) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 25) * scale_x + bias_x);
            *reinterpret_cast<float*>(target_2 + 4) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 33) * scale_y);
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
            *reinterpret_cast<float*>(target_3) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 25) * scale_x + bias_x);
            *reinterpret_cast<float*>(target_3 + 4) = AlignToHalfPixel(
                *reinterpret_cast<const float*>(quad_fields - 37) * scale_y);
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
    out_rect->top = *reinterpret_cast<const float*>(bytes + 0xF4);
    out_rect->bottom = *reinterpret_cast<const float*>(bytes + 0xF8);
    out_rect->left = *reinterpret_cast<const float*>(bytes + 0xFC);
    out_rect->right = *reinterpret_cast<const float*>(bytes + 0x100);
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
    state->plan = plan;
    state->original_vtable = vtable;
    state->original_scale_x = *reinterpret_cast<const float*>(bytes + kRendererScaleXOffset);
    state->original_scale_y = *reinterpret_cast<const float*>(bytes + kRendererScaleYOffset);
    for (std::size_t i = 0; i < state->synthetic_vtable.size(); ++i) {
        state->synthetic_vtable[i] = reinterpret_cast<std::uintptr_t>(vtable[i]);
    }
    state->synthetic_vtable[kDoRenderSlot] =
        reinterpret_cast<std::uintptr_t>(&Hook_CeguiRendererDoRenderWide);
    state->synthetic_vtable[kGetRenderRectSlot] =
        reinterpret_cast<std::uintptr_t>(&Hook_CeguiRendererGetRenderRectWide);
    state->render_rect.top = 0.0F;
    state->render_rect.bottom = plan.logical_height;
    state->render_rect.left = -plan.logical_horizontal_padding;
    state->render_rect.right = plan.logical_width + plan.logical_horizontal_padding;

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

    const auto plan = BuildCeguiWidescreenPlan(config[0], config[1]);
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

float* __fastcall Hook_CeguiSystemInitialize(
    float* self,
    void*,
    void* renderer,
    void* resource_provider) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::cegui_system_initialize);
    if (!g_original_cegui_system_initialize) {
        state.SetHookError(
            HookId::cegui_system_initialize,
            "original CEGUI_System_Initialize trampoline is null");
        return nullptr;
    }
    return g_original_cegui_system_initialize(self, renderer, resource_provider);
}

}  // namespace

void* GetCeguiRendererReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::cegui_renderer_constructor_2:
        return reinterpret_cast<void*>(&Hook_CeguiRendererConstructor2);
    case HookId::cegui_system_initialize:
        return reinterpret_cast<void*>(&Hook_CeguiSystemInitialize);
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
    case HookId::cegui_system_initialize:
        g_original_cegui_system_initialize =
            reinterpret_cast<CeguiSystemInitializeFn>(trampoline);
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

}  // namespace pal4::inject

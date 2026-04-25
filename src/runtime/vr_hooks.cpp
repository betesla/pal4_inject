#include "vr_hooks.h"

#include <cstdint>

#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using GameRenderFrameFn = int (__cdecl*)(double, int);
using PalGameIvInitCameraSubsystemFn = int (__cdecl*)();
using CameraGetActiveCameraObjectFn = void* (__thiscall*)(void*);
using ObjectGetInternalIdFn = void* (__thiscall*)(void*);

GameRenderFrameFn g_original_game_render_frame = nullptr;
PalGameIvInitCameraSubsystemFn g_pal_game_iv_init_camera_subsystem = nullptr;
CameraGetActiveCameraObjectFn g_camera_get_active_camera_object = nullptr;
ObjectGetInternalIdFn g_object_get_internal_id = nullptr;
VrHeadPose g_last_applied_vr_pose{};
float* g_last_applied_camera = nullptr;

template <typename Fn>
Fn ResolveRuntimeFunction(const std::uint32_t ida_ea) {
    const auto module_base = GetRuntimeState().MainModuleBase();
    if (module_base == 0) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(ida::ResolveRuntimeAddress(module_base, ida_ea));
}

void EnsureRuntimeBindings() {
    if (!g_pal_game_iv_init_camera_subsystem) {
        g_pal_game_iv_init_camera_subsystem =
            ResolveRuntimeFunction<PalGameIvInitCameraSubsystemFn>(
                ida::kPalGameIvInitCameraSubsystem);
    }
    if (!g_camera_get_active_camera_object) {
        g_camera_get_active_camera_object =
            ResolveRuntimeFunction<CameraGetActiveCameraObjectFn>(
                ida::kCameraGetActiveCameraObject);
    }
    if (!g_object_get_internal_id) {
        g_object_get_internal_id =
            ResolveRuntimeFunction<ObjectGetInternalIdFn>(ida::kObjectGetInternalId);
    }
}

void CaptureActiveCameraSnapshot() {
    EnsureRuntimeBindings();

    VrCameraState state{};
    if (!g_pal_game_iv_init_camera_subsystem ||
        !g_camera_get_active_camera_object ||
        !g_object_get_internal_id) {
        GetRuntimeState().SetVrCameraState(state);
        return;
    }

    void* manager = reinterpret_cast<void*>(g_pal_game_iv_init_camera_subsystem());
    if (!manager) {
        GetRuntimeState().SetVrCameraState(state);
        return;
    }

    void* camera_object = g_camera_get_active_camera_object(manager);
    if (!camera_object) {
        GetRuntimeState().SetVrCameraState(state);
        return;
    }

    auto* camera = static_cast<float*>(g_object_get_internal_id(camera_object));
    if (!camera) {
        GetRuntimeState().SetVrCameraState(state);
        return;
    }

    state.valid = true;
    state.camera_object = reinterpret_cast<std::uintptr_t>(camera_object);
    state.camera_internal = reinterpret_cast<std::uintptr_t>(camera);
    state.yaw_degrees = camera[15];
    state.pitch_degrees = camera[16];
    state.roll_degrees = camera[17];
    state.distance = camera[18];
    state.position_x = camera[26];
    state.position_y = camera[27];
    state.position_z = camera[28];
    state.look_at_x = camera[29];
    state.look_at_y = camera[30];
    state.look_at_z = camera[31];
    GetRuntimeState().SetVrCameraState(state);
}

void RemoveVrPoseFromCamera(float* camera, const VrHeadPose& pose) {
    if (!camera || !pose.active) {
        return;
    }
    camera[15] -= pose.yaw_degrees;
    camera[16] -= pose.pitch_degrees;
    camera[17] -= pose.roll_degrees;
    camera[26] -= pose.offset_x;
    camera[27] -= pose.offset_y;
    camera[28] -= pose.offset_z;
    camera[29] -= pose.offset_x;
    camera[30] -= pose.offset_y;
    camera[31] -= pose.offset_z;
}

void ApplyVrPoseToCamera(float* camera, const VrHeadPose& pose) {
    if (!camera || !pose.active) {
        return;
    }
    camera[15] += pose.yaw_degrees;
    camera[16] += pose.pitch_degrees;
    camera[17] += pose.roll_degrees;
    camera[26] += pose.offset_x;
    camera[27] += pose.offset_y;
    camera[28] += pose.offset_z;
    camera[29] += pose.offset_x;
    camera[30] += pose.offset_y;
    camera[31] += pose.offset_z;
}

float* ResolveActiveCameraInternal() {
    EnsureRuntimeBindings();
    if (!g_pal_game_iv_init_camera_subsystem ||
        !g_camera_get_active_camera_object ||
        !g_object_get_internal_id) {
        return nullptr;
    }
    void* manager = reinterpret_cast<void*>(g_pal_game_iv_init_camera_subsystem());
    if (!manager) {
        return nullptr;
    }
    void* camera_object = g_camera_get_active_camera_object(manager);
    if (!camera_object) {
        return nullptr;
    }
    return static_cast<float*>(g_object_get_internal_id(camera_object));
}

int __cdecl Hook_GameRenderFrame(const double delta_seconds, const int external_camera) {
    auto& runtime = GetRuntimeState();
    runtime.IncrementHookCall(HookId::game_render_frame);

    if (!g_original_game_render_frame) {
        runtime.SetHookError(
            HookId::game_render_frame,
            "original Game_RenderFrame trampoline is null");
        runtime.SetLastError("original Game_RenderFrame trampoline is null");
        return 0;
    }

    const HookMode mode = runtime.GetHookMode(HookId::game_render_frame);
    const bool vr_enabled = runtime.GetVrMode() == VrMode::seated_experimental;
    float* active_camera = nullptr;
    if (vr_enabled || mode != HookMode::observe_only) {
        active_camera = ResolveActiveCameraInternal();
        if (g_last_applied_camera && g_last_applied_camera == active_camera && g_last_applied_vr_pose.active) {
            RemoveVrPoseFromCamera(g_last_applied_camera, g_last_applied_vr_pose);
            g_last_applied_vr_pose = {};
            g_last_applied_camera = nullptr;
        }
        if (vr_enabled && active_camera) {
            const auto vr_pose = runtime.GetVrHeadPose();
            if (vr_pose.active) {
                ApplyVrPoseToCamera(active_camera, vr_pose);
                g_last_applied_vr_pose = vr_pose;
                g_last_applied_camera = active_camera;
            }
        }
        CaptureActiveCameraSnapshot();
    }
    runtime.ClearHookError(HookId::game_render_frame);
    return g_original_game_render_frame(delta_seconds, external_camera);
}

}  // namespace

void* GetVrReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::game_render_frame:
        return reinterpret_cast<void*>(&Hook_GameRenderFrame);
    default:
        return nullptr;
    }
}

void SetVrOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::game_render_frame:
        g_original_game_render_frame =
            reinterpret_cast<GameRenderFrameFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

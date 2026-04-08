#include "camera_hooks.h"

#include <cmath>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/camera_pitch_guard.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using CameraUpdateMatrixFn = int (__thiscall*)(float*, int);

CameraUpdateMatrixFn g_original_camera_update_matrix = nullptr;
DWORD g_last_camera_clamp_log_tick = 0;
float g_last_logged_original_pitch = 0.0F;
float g_last_logged_clamped_pitch = 0.0F;

bool NearlyEqual(const float lhs, const float rhs) noexcept {
    return std::fabs(lhs - rhs) < 0.001F;
}

void MaybeLogCameraPitchClamp(
    const float original_pitch,
    const float clamped_pitch) {
    if (NearlyEqual(original_pitch, clamped_pitch)) {
        return;
    }

    const DWORD tick = GetTickCount();
    if (tick - g_last_camera_clamp_log_tick < 500 &&
        NearlyEqual(original_pitch, g_last_logged_original_pitch) &&
        NearlyEqual(clamped_pitch, g_last_logged_clamped_pitch)) {
        return;
    }

    g_last_camera_clamp_log_tick = tick;
    g_last_logged_original_pitch = original_pitch;
    g_last_logged_clamped_pitch = clamped_pitch;

    std::ostringstream out;
    out
        << "hook=camera_update_matrix"
        << " clamp=1"
        << " original_second_angle=" << original_pitch
        << " normalized_second_angle=" << NormalizeAngle360(original_pitch)
        << " applied_second_angle=" << clamped_pitch;
    GetRuntimeState().AppendEventLog(out.str());
}

int __fastcall Hook_CameraUpdateMatrix(
    float* self,
    void*,
    const int update_position_mode) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::camera_update_matrix);

    if (!g_original_camera_update_matrix) {
        state.SetHookError(
            HookId::camera_update_matrix,
            "original Camera_UpdateMatrix trampoline is null");
        state.SetLastError("original Camera_UpdateMatrix trampoline is null");
        return 0;
    }
    if (!self) {
        state.SetHookError(HookId::camera_update_matrix, "camera pointer is null");
        state.SetLastError("camera pointer is null");
        return 0;
    }

    const HookMode mode = state.GetHookMode(HookId::camera_update_matrix);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        return g_original_camera_update_matrix(self, update_position_mode);
    }

    const float original_pitch = self[16];
    const float clamped_pitch = ClampCameraPitchAngle(original_pitch);
    if (!NearlyEqual(original_pitch, clamped_pitch)) {
        self[16] = clamped_pitch;
        MaybeLogCameraPitchClamp(original_pitch, clamped_pitch);
    }

    state.ClearHookError(HookId::camera_update_matrix);
    return g_original_camera_update_matrix(self, update_position_mode);
}

}  // namespace

void* GetCameraReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::camera_update_matrix:
        return reinterpret_cast<void*>(&Hook_CameraUpdateMatrix);
    default:
        return nullptr;
    }
}

void SetCameraOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::camera_update_matrix:
        g_original_camera_update_matrix =
            reinterpret_cast<CameraUpdateMatrixFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

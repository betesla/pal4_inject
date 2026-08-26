#include "camera_hooks.h"

#include <cmath>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hook_logging.h"
#include "gamepad_control_hooks.h"
#include "pal4inject/camera_pitch_guard.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using CameraUpdateMatrixFn = int (__thiscall*)(float*, int);
using CameraPrepareFn = BOOL (__cdecl*)(int);
using CameraRunSingleFn = int (__cdecl*)(int, void*);
using RwCameraBeginUpdateFn = int (__cdecl*)(void*);

CameraUpdateMatrixFn g_original_camera_update_matrix = nullptr;
CameraPrepareFn g_original_camera_prepare = nullptr;
CameraRunSingleFn g_original_camera_run_single = nullptr;
RwCameraBeginUpdateFn g_original_rw_camera_begin_update = nullptr;
int g_capture_pose_after_run_single = 0;
DWORD g_last_camera_clamp_log_tick = 0;
float g_last_logged_original_pitch = 0.0F;
float g_last_logged_clamped_pitch = 0.0F;

bool NearlyEqual(const float lhs, const float rhs) noexcept {
    return std::fabs(lhs - rhs) < 0.001F;
}

bool CopyCameraPrepareName(
    const int script_string_arg,
    char* output,
    const std::size_t output_size) {
    if (!output || output_size == 0U) {
        return false;
    }
    output[0] = '\0';
    if (script_string_arg == 0) {
        return false;
    }
    __try {
        const auto* value = *reinterpret_cast<const char* const*>(
            script_string_arg + 4);
        if (!value) {
            return true;
        }
        std::size_t index = 0;
        for (; index + 1U < output_size && value[index] != '\0'; ++index) {
            output[index] = value[index];
        }
        output[index] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        output[0] = '\0';
        return false;
    }
}

BOOL __cdecl Hook_CameraPrepare(const int script_string_arg) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::camera_prepare);
    if (!g_original_camera_prepare) {
        state.SetHookError(
            HookId::camera_prepare,
            "original Camera_Prepare trampoline is null");
        state.SetLastError("original Camera_Prepare trampoline is null");
        return FALSE;
    }

    char name_buffer[128]{};
    const bool readable = CopyCameraPrepareName(
        script_string_arg,
        name_buffer,
        sizeof(name_buffer));
    const BOOL loaded = g_original_camera_prepare(script_string_arg);
    std::ostringstream out;
    out << "hook=camera_prepare"
        << " camera=" << (readable ? name_buffer : "<unreadable>")
        << " load_success=" << (loaded ? 1 : 0);
    // Camera load success is validation evidence, so keep it available to the
    // headless CLI even when verbose per-hook file logging is disabled.
    if (ShouldEmitHookLog(HookId::camera_prepare)) {
        AppendHookEventLog(HookId::camera_prepare, out.str());
    } else {
        state.AppendEventLog(out.str());
    }
    state.ClearHookError(HookId::camera_prepare);
    return loaded;
}

int __cdecl Hook_CameraRunSingle(
    const int script_string_arg,
    void* wait_until_done) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::camera_run_single);
    if (!g_original_camera_run_single) {
        state.SetHookError(
            HookId::camera_run_single,
            "original Camera_RunSingle trampoline is null");
        state.SetLastError("original Camera_RunSingle trampoline is null");
        return 0;
    }

    char name_buffer[128]{};
    const bool readable = CopyCameraPrepareName(
        script_string_arg,
        name_buffer,
        sizeof(name_buffer));
    const int result = g_original_camera_run_single(
        script_string_arg,
        wait_until_done);
    // The immediate camera task invokes Camera_UpdateMatrix several times
    // while applying target, angles and distance. Arm the capture only after
    // the task returns so the next frame exposes the fully settled pose.
    g_capture_pose_after_run_single = result != 0 ? 8 : 0;
    std::ostringstream out;
    out << "hook=camera_run_single"
        << " record=" << (readable ? name_buffer : "<unreadable>")
        << " result=" << result;
    if (ShouldEmitHookLog(HookId::camera_run_single)) {
        AppendHookEventLog(HookId::camera_run_single, out.str());
    } else {
        state.AppendEventLog(out.str());
    }
    state.ClearHookError(HookId::camera_run_single);
    return result;
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
    AppendHookEventLog(HookId::camera_update_matrix, out.str());
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

    ApplyGamepadCameraYawGuard(self);

    if (g_capture_pose_after_run_single > 0) {
        const int sample = 9 - g_capture_pose_after_run_single;
        --g_capture_pose_after_run_single;
        std::ostringstream out;
        out << "hook=camera_update_matrix after_run_single=1"
            << " sample=" << sample
            << " update_position_mode=" << update_position_mode
            << " angles=" << self[15] << ',' << self[16] << ',' << self[17]
            << " distance=" << self[18]
            << " eye=" << self[26] << ',' << self[27] << ',' << self[28]
            << " target=" << self[29] << ',' << self[30] << ',' << self[31];
        state.AppendEventLog(out.str());
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

int __cdecl Hook_RwCameraBeginUpdate(void* const rw_camera) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::rw_camera_begin_update);
    if (!g_original_rw_camera_begin_update) {
        state.SetHookError(
            HookId::rw_camera_begin_update,
            "original RwCameraBeginUpdate trampoline is null");
        state.SetLastError("original RwCameraBeginUpdate trampoline is null");
        return 0;
    }

    const HookMode mode = state.GetHookMode(HookId::rw_camera_begin_update);
    if (mode != HookMode::observe_only && mode != HookMode::mirror_compare) {
        ApplyGamepadBattleCameraBeforeRender(rw_camera);
    }
    state.ClearHookError(HookId::rw_camera_begin_update);
    return g_original_rw_camera_begin_update(rw_camera);
}

}  // namespace

void* GetCameraReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::camera_prepare:
        return reinterpret_cast<void*>(&Hook_CameraPrepare);
    case HookId::camera_run_single:
        return reinterpret_cast<void*>(&Hook_CameraRunSingle);
    case HookId::camera_update_matrix:
        return reinterpret_cast<void*>(&Hook_CameraUpdateMatrix);
    case HookId::rw_camera_begin_update:
        return reinterpret_cast<void*>(&Hook_RwCameraBeginUpdate);
    default:
        return nullptr;
    }
}

void SetCameraOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::camera_prepare:
        g_original_camera_prepare = reinterpret_cast<CameraPrepareFn>(trampoline);
        break;
    case HookId::camera_run_single:
        g_original_camera_run_single = reinterpret_cast<CameraRunSingleFn>(trampoline);
        break;
    case HookId::camera_update_matrix:
        g_original_camera_update_matrix =
            reinterpret_cast<CameraUpdateMatrixFn>(trampoline);
        break;
    case HookId::rw_camera_begin_update:
        g_original_rw_camera_begin_update =
            reinterpret_cast<RwCameraBeginUpdateFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject

#include "gamepad_control_hooks.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <sstream>

#include <intrin.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "gamepad_runtime.h"
#include "hook_logging.h"
#include "pal4inject/camera_pitch_guard.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using PlayerControlUpdateFn = int (__thiscall*)(void*, void*, float);
using MainCameraTailFollowUpdateFn = int (__thiscall*)(
    void*, void*, void*, float);
using FlushMainCameraTailYawFn = void (__thiscall*)(void*);
using MovePlayerInDirectionFn = int (__thiscall*)(void*, void*, const float*, float);
using SetPlayerMovementModeFn = int (__thiscall*)(void*, int, int);
using AnimationSetPlaybackRateFn = int (__thiscall*)(void*, float);
using MinimapGetInstanceFn = void* (__cdecl*)();
using MinimapUpdateGridDisplayFn = void (__thiscall*)(void*, float*, void*);
using SetCameraModeScriptFn = int (__cdecl*)(char*);
using GetCameraManagerFn = void* (__cdecl*)();
using GetActiveCameraFn = float* (__thiscall*)(void*);
using SetCameraModeFn = int (__thiscall*)(void*, unsigned int);
using SetCameraAngleFn = int (__thiscall*)(float*, float, int);
using SetCameraDistanceFn = int (__thiscall*)(float*, float, int);

PlayerControlUpdateFn g_original_player_control_update = nullptr;
MainCameraTailFollowUpdateFn g_original_main_camera_tail_follow_update = nullptr;
FlushMainCameraTailYawFn g_original_flush_main_camera_tail_yaw = nullptr;
SetCameraModeScriptFn g_original_set_camera_mode_script = nullptr;
CameraYawGuard g_camera_yaw_guard;
bool g_camera_yaw_tracking = false;
std::uintptr_t g_camera_yaw_tracking_target = 0;
float g_controlled_camera_yaw = 0.0F;
std::uint32_t g_suppressed_tail_yaw_calls = 0;
std::uint32_t g_suppressed_tail_follow_calls = 0;

constexpr float kNativeMovementBaseSpeed = 169.89999F;
constexpr std::ptrdiff_t kPlayerPositionOffset = 172;
constexpr std::ptrdiff_t kPlayerRotationOffset = 160;
constexpr std::ptrdiff_t kPlayerYawOffset = 164;
constexpr std::ptrdiff_t kPlayerAnimationOffset = 368;
constexpr float kGamepadTurnSpeedDegreesPerSecond = 360.0F;
constexpr float kGamepadWalkTurnThresholdDegrees = 20.0F;
constexpr std::uint32_t kMainLoopTailFollowReturnA = 0x42498F;
constexpr std::uint32_t kMainLoopTailFollowReturnB = 0x424A4A;
constexpr std::uint32_t kMainLoopTailYawReturnA = 0x4249A4;
constexpr std::uint32_t kMainLoopTailYawReturnB = 0x424A5F;

std::uintptr_t MainModuleBase() {
    auto& state = GetRuntimeState();
    std::uintptr_t base = state.MainModuleBase();
    if (base == 0) {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        state.SetMainModuleBase(base);
    }
    return base;
}

template <typename Fn>
Fn ResolveRuntimeFunction(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    return base == 0
        ? nullptr
        : reinterpret_cast<Fn>(ida::ResolveRuntimeAddress(base, ida_ea));
}

void* GetCameraManager() {
    const auto get_manager =
        ResolveRuntimeFunction<GetCameraManagerFn>(ida::kPalGameIvInitCameraSubsystem);
    return get_manager ? get_manager() : nullptr;
}

float* GetActiveCamera() {
    const auto get_active_camera =
        ResolveRuntimeFunction<GetActiveCameraFn>(ida::kCameraGetActiveCameraInternalId);
    if (!get_active_camera) {
        return nullptr;
    }
    void* const manager = GetCameraManager();
    return manager ? get_active_camera(manager) : nullptr;
}

void ResetCameraYawTracking() noexcept {
    g_camera_yaw_guard.Disarm();
    g_camera_yaw_tracking = false;
    g_camera_yaw_tracking_target = 0;
    g_suppressed_tail_yaw_calls = 0;
    g_suppressed_tail_follow_calls = 0;
}

void UpdateModernCamera(
    float* const camera,
    const GamepadAnalogStick& stick,
    const bool movement_active,
    const float delta_seconds) {
    const bool camera_active = stick.magnitude > 0.0F;
    if (!camera) {
        ResetCameraYawTracking();
        return;
    }
    const auto camera_address = reinterpret_cast<std::uintptr_t>(camera);
    if (!g_camera_yaw_tracking ||
        g_camera_yaw_tracking_target != camera_address) {
        g_controlled_camera_yaw = NormalizeAngle360(camera[15]);
        g_camera_yaw_tracking = true;
        g_camera_yaw_tracking_target = camera_address;
        g_suppressed_tail_yaw_calls = 0;
    }
    if (!movement_active && !camera_active) {
        return;
    }

    const auto set_yaw = ResolveRuntimeFunction<SetCameraAngleFn>(ida::kCameraSetYaw);
    const auto set_pitch = ResolveRuntimeFunction<SetCameraAngleFn>(ida::kCameraSetPitch);
    if (!set_yaw || !set_pitch) {
        ResetCameraYawTracking();
        return;
    }

    const auto& state = GetRuntimeState();
    const float sensitivity = state.GamepadCameraSensitivity();
    const float safe_delta = std::isfinite(delta_seconds)
        ? std::clamp(delta_seconds, 0.0F, 0.1F)
        : 0.0F;
    if (camera_active && safe_delta > 0.0F) {
        const float yaw_direction = state.GamepadInvertCameraX() ? 1.0F : -1.0F;
        g_controlled_camera_yaw = NormalizeAngle360(
            g_controlled_camera_yaw + stick.x * sensitivity * safe_delta * yaw_direction);
    }

    // PAL4's camera setters synchronously reach Camera_UpdateMatrix. Keep the
    // guard scoped to those calls so later gameplay and story-camera updates
    // cannot inherit this yaw.
    g_camera_yaw_guard.Arm(camera, g_controlled_camera_yaw);
    set_yaw(camera, g_controlled_camera_yaw, 1);

    if (camera_active && safe_delta > 0.0F) {
        const float pitch_direction = state.GamepadInvertCameraY() ? 1.0F : -1.0F;
        const float pitch = ClampCameraPitchAngle(
            camera[16] + stick.y * sensitivity * safe_delta * pitch_direction);
        set_pitch(camera, pitch, 1);
    }
    g_camera_yaw_guard.Disarm();
}

bool BuildCameraRelativeMovement(
    const float* const camera,
    const GamepadAnalogStick& stick,
    float* const direction) {
    if (!direction || stick.magnitude <= 0.0F) {
        return false;
    }
    if (!camera) {
        return false;
    }

    // PAL4's camera matrix stores its forward vector at [20..22] and right
    // vector at [23..25]. The stock camera-relative input path subtracts the
    // right vector for D, hence the sign below.
    direction[0] = camera[20] * stick.y - camera[23] * stick.x;
    direction[1] = 0.0F;
    direction[2] = camera[22] * stick.y - camera[25] * stick.x;
    if (!std::isfinite(direction[0]) || !std::isfinite(direction[2])) {
        direction[0] = 0.0F;
        direction[2] = 0.0F;
        return false;
    }
    const float length = std::sqrt(
        direction[0] * direction[0] + direction[2] * direction[2]);
    if (length <= 0.0001F) {
        return false;
    }
    direction[0] /= length;
    direction[2] /= length;
    return true;
}

void SetPlayerAnimationPlaybackRate(
    void* const player,
    const float multiplier) {
    if (!player) {
        return;
    }
    const auto set_playback_rate =
        ResolveRuntimeFunction<AnimationSetPlaybackRateFn>(
            ida::kAnimationSetPlaybackRate);
    if (set_playback_rate) {
        set_playback_rate(
            static_cast<unsigned char*>(player) + kPlayerAnimationOffset,
            multiplier);
    }
}

void RestoreNativeMovementSpeed(void* const control) {
    if (!control) {
        return;
    }
    auto* const control_bytes = static_cast<unsigned char*>(control);
    const float native_multiplier = *reinterpret_cast<const float*>(
        control_bytes + 44);
    *reinterpret_cast<float*>(control_bytes + 4) =
        kNativeMovementBaseSpeed * native_multiplier;
}

void UpdateMinimapPlayerMarker(void* const player) {
    if (!player) {
        return;
    }
    const auto get_minimap = ResolveRuntimeFunction<MinimapGetInstanceFn>(
        ida::kMinimapGetInstance);
    const auto update_grid = ResolveRuntimeFunction<MinimapUpdateGridDisplayFn>(
        ida::kMinimapUpdateGridDisplay);
    void* const minimap = get_minimap ? get_minimap() : nullptr;
    if (!minimap || !update_grid) {
        return;
    }
    auto* const player_bytes = static_cast<unsigned char*>(player);
    update_grid(
        minimap,
        reinterpret_cast<float*>(player_bytes + kPlayerPositionOffset),
        player_bytes + kPlayerRotationOffset);
}

bool ReadCurrentCameraModeDefaultDistance(
    const void* const manager,
    float* const mode_default) noexcept {
    if (!manager || !mode_default) {
        return false;
    }
    __try {
        const auto* const manager_bytes =
            static_cast<const unsigned char*>(manager);
        const void* const profile = *reinterpret_cast<void* const*>(
            manager_bytes + 188);
        if (!profile) {
            return false;
        }
        *mode_default = *reinterpret_cast<const float*>(
            static_cast<const unsigned char*>(profile) + 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int __fastcall Hook_PlayerControlUpdate(
    void* const self,
    void*,
    void* const player,
    const float delta_seconds) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::player_control_update);
    if (!g_original_player_control_update) {
        state.SetHookError(
            HookId::player_control_update,
            "original player-control trampoline is null");
        return 0;
    }

    const auto controls = GetGamepadModernControlState();
    if (!state.GamepadEnabled() || !state.GamepadModernControls() ||
        !controls.active) {
        ResetCameraYawTracking();
        RestoreNativeMovementSpeed(self);
        SetPlayerAnimationPlaybackRate(player, 1.0F);
        return g_original_player_control_update(self, player, delta_seconds);
    }

    float* const camera = GetActiveCamera();
    const bool movement_active = controls.movement.magnitude > 0.0F;
    UpdateModernCamera(
        camera,
        controls.camera,
        movement_active,
        delta_seconds);
    float direction[3]{};
    if (!BuildCameraRelativeMovement(camera, controls.movement, direction)) {
        RestoreNativeMovementSpeed(self);
        SetPlayerAnimationPlaybackRate(player, 1.0F);
        return g_original_player_control_update(self, player, delta_seconds);
    }

    const auto set_movement_mode = ResolveRuntimeFunction<SetPlayerMovementModeFn>(
        ida::kSetPlayerMovementMode);
    const auto move_player = ResolveRuntimeFunction<MovePlayerInDirectionFn>(
        ida::kMovePlayerInDirection);
    if (!set_movement_mode || !move_player || !self || !player) {
        RestoreNativeMovementSpeed(self);
        SetPlayerAnimationPlaybackRate(player, 1.0F);
        return g_original_player_control_update(self, player, delta_seconds);
    }

    const auto* const player_bytes = static_cast<const unsigned char*>(player);
    const auto turn_tuning = BuildGamepadTurnTuning(
        *reinterpret_cast<const float*>(player_bytes + kPlayerYawOffset),
        direction[0],
        direction[2],
        delta_seconds,
        kGamepadTurnSpeedDegreesPerSecond,
        kGamepadWalkTurnThresholdDegrees);
    direction[0] = turn_tuning.direction_x;
    direction[2] = turn_tuning.direction_z;

    auto tuning = BuildGamepadMovementTuning(
        controls.movement.magnitude,
        state.GamepadRunThreshold(),
        state.GamepadFastRunThreshold());
    if (turn_tuning.use_walk_animation) {
        tuning = {0, 0.4F, 1.0F};
    }
    const int movement_mode = tuning.mode;
    const int current_mode = *reinterpret_cast<const int*>(
        static_cast<const unsigned char*>(self) + 40);
    if (current_mode != movement_mode) {
        set_movement_mode(self, movement_mode, 0);
        if (state.GamepadLogEnabled()) {
            std::ostringstream event;
            event << "gamepad:movement_mode previous=" << current_mode
                  << " current=" << movement_mode
                  << " magnitude=" << controls.movement.magnitude
                  << " speed_multiplier=" << tuning.speed_multiplier
                  << " animation_multiplier=" << tuning.animation_multiplier;
            state.AppendEventLog(event.str());
        }
    }
    *reinterpret_cast<float*>(static_cast<unsigned char*>(self) + 4) =
        kNativeMovementBaseSpeed * tuning.speed_multiplier;
    SetPlayerAnimationPlaybackRate(player, tuning.animation_multiplier);
    const int moved = move_player(self, player, direction, delta_seconds);
    if (moved) {
        *reinterpret_cast<int*>(static_cast<unsigned char*>(self) + 16) = 1;
        // MovePlayerInDirection omits the UpdateGridDisplay call made by the
        // stock forward/back dispatcher, so preserve that native side effect.
        UpdateMinimapPlayerMarker(player);
    }
    return moved;
}

int __fastcall Hook_MainCameraTailFollowUpdate(
    void* const self,
    void*,
    void* const camera,
    void* const target,
    const float delta_seconds) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::main_camera_tail_follow_update);
    if (!g_original_main_camera_tail_follow_update) {
        state.SetHookError(
            HookId::main_camera_tail_follow_update,
            "original camera tail-follow trampoline is null");
        return 0;
    }

    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto module_base = MainModuleBase();
    const bool main_loop_auto_recenter_stage =
        return_address == ida::ResolveRuntimeAddress(
            module_base,
            kMainLoopTailFollowReturnA) ||
        return_address == ida::ResolveRuntimeAddress(
            module_base,
            kMainLoopTailFollowReturnB);
    const bool tracked_camera =
        reinterpret_cast<std::uintptr_t>(camera) == g_camera_yaw_tracking_target;
    const HookMode mode = state.GetHookMode(
        HookId::main_camera_tail_follow_update);
    const bool replacement_active =
        mode == HookMode::replace_with_fallback ||
        mode == HookMode::replace_strict;
    const bool suppress = ShouldSuppressGamepadCameraAutoRecenter(
        replacement_active,
        main_loop_auto_recenter_stage,
        state.GamepadEnabled(),
        state.GamepadModernControls(),
        g_camera_yaw_tracking && tracked_camera);
    if (suppress) {
        // The original routine returns zero and clears this interpolation
        // accumulator when the camera-tail correction completes. Complete it
        // immediately so the main loop cannot render one native recenter frame
        // before the final FlushMainCameraTailYaw stage is suppressed.
        if (self) {
            *reinterpret_cast<float*>(
                static_cast<unsigned char*>(self) + 20) = 0.0F;
        }
        const std::uint32_t sequence = ++g_suppressed_tail_follow_calls;
        if (state.GamepadLogEnabled() &&
            (sequence == 1 || sequence % 120 == 0)) {
            std::ostringstream event;
            event << "gamepad:camera_tail_follow_suppressed"
                  << " sequence=" << sequence
                  << " caller_ida=0x" << std::hex
                  << (return_address - module_base + ida::kLaunchExeBase)
                  << std::dec
                  << " controlled_yaw=" << g_controlled_camera_yaw;
            AppendCriticalHookEventLog(event.str());
        }
        state.ClearHookError(HookId::main_camera_tail_follow_update);
        return 0;
    }

    const int result = g_original_main_camera_tail_follow_update(
        self,
        camera,
        target,
        delta_seconds);
    state.ClearHookError(HookId::main_camera_tail_follow_update);
    return result;
}

void __fastcall Hook_FlushMainCameraTailYaw(
    void* const self,
    void*) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::flush_main_camera_tail_yaw);
    if (!g_original_flush_main_camera_tail_yaw) {
        state.SetHookError(
            HookId::flush_main_camera_tail_yaw,
            "original camera tail-yaw trampoline is null");
        return;
    }

    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto module_base = MainModuleBase();
    const bool main_loop_auto_recenter =
        return_address == ida::ResolveRuntimeAddress(
            module_base,
            kMainLoopTailYawReturnA) ||
        return_address == ida::ResolveRuntimeAddress(
            module_base,
            kMainLoopTailYawReturnB);
    const HookMode mode = state.GetHookMode(HookId::flush_main_camera_tail_yaw);
    const bool replacement_active =
        mode == HookMode::replace_with_fallback ||
        mode == HookMode::replace_strict;
    const bool suppress = ShouldSuppressGamepadCameraAutoRecenter(
        replacement_active,
        main_loop_auto_recenter,
        state.GamepadEnabled(),
        state.GamepadModernControls(),
        g_camera_yaw_tracking);
    if (suppress) {
        const std::uint32_t sequence = ++g_suppressed_tail_yaw_calls;
        if (state.GamepadLogEnabled() &&
            (sequence == 1 || sequence % 120 == 0)) {
            std::ostringstream event;
            event << "gamepad:camera_tail_yaw_suppressed"
                  << " sequence=" << sequence
                  << " caller_ida=0x" << std::hex
                  << (return_address - module_base + ida::kLaunchExeBase)
                  << std::dec
                  << " controlled_yaw=" << g_controlled_camera_yaw;
            AppendCriticalHookEventLog(event.str());
        }
        state.ClearHookError(HookId::flush_main_camera_tail_yaw);
        return;
    }

    g_original_flush_main_camera_tail_yaw(self);
    state.ClearHookError(HookId::flush_main_camera_tail_yaw);
}

int __cdecl Hook_SetCameraModeScript(char* const arguments) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::set_camera_mode_script);
    if (!g_original_set_camera_mode_script) {
        state.SetHookError(
            HookId::set_camera_mode_script,
            "original SetCameraMode trampoline is null");
        return 0;
    }
    // A script camera mode switch transfers camera ownership away from the
    // gameplay controls. Re-acquire yaw from the resulting camera pose after
    // control returns instead of restoring a pre-cutscene angle.
    ResetCameraYawTracking();
    if (!state.GamepadEnabled() || !state.GamepadModernControls() ||
        !state.GamepadPreserveFreeCamera()) {
        return g_original_set_camera_mode_script(arguments);
    }

    const auto get_manager =
        ResolveRuntimeFunction<GetCameraManagerFn>(ida::kPalGameIvInitCameraSubsystem);
    const auto set_mode = ResolveRuntimeFunction<SetCameraModeFn>(ida::kCameraSetMode);
    void* const manager = get_manager ? get_manager() : nullptr;
    const unsigned int previous_mode = manager
        ? *reinterpret_cast<unsigned int*>(static_cast<unsigned char*>(manager) + 192)
        : 0;
    const int result = g_original_set_camera_mode_script(arguments);
    if (manager && set_mode) {
        const unsigned int requested_mode =
            *reinterpret_cast<unsigned int*>(static_cast<unsigned char*>(manager) + 192);
        if (requested_mode != previous_mode) {
            set_mode(manager, previous_mode);
            if (state.GamepadLogEnabled()) {
                std::ostringstream event;
                event << "gamepad:camera_mode_preserved previous=" << previous_mode
                      << " requested=" << requested_mode;
                state.AppendEventLog(event.str());
            }
        }
    }
    return result;
}

}  // namespace

void CameraYawGuard::Arm(float* const camera, const float yaw) noexcept {
    Disarm();
    if (!camera || !std::isfinite(yaw)) {
        return;
    }
    target_.store(
        reinterpret_cast<std::uintptr_t>(camera),
        std::memory_order_relaxed);
    angle_.store(yaw, std::memory_order_relaxed);
    enabled_.store(true, std::memory_order_release);
}

void CameraYawGuard::Disarm() noexcept {
    enabled_.store(false, std::memory_order_release);
    target_.store(0, std::memory_order_relaxed);
}

bool CameraYawGuard::Apply(float* const camera) const noexcept {
    if (!camera || !enabled_.load(std::memory_order_acquire) ||
        target_.load(std::memory_order_relaxed) !=
            reinterpret_cast<std::uintptr_t>(camera)) {
        return false;
    }
    const float guarded_yaw = angle_.load(std::memory_order_relaxed);
    if (!std::isfinite(guarded_yaw)) {
        return false;
    }
    camera[15] = NormalizeAngle360(guarded_yaw);
    return true;
}

bool CycleGamepadCameraDistance() {
    void* const manager = GetCameraManager();
    const auto get_active_camera =
        ResolveRuntimeFunction<GetActiveCameraFn>(ida::kCameraGetActiveCameraInternalId);
    float* const camera = manager && get_active_camera
        ? get_active_camera(manager)
        : nullptr;
    const auto set_distance =
        ResolveRuntimeFunction<SetCameraDistanceFn>(ida::kCameraSetDistance);
    if (!camera || !set_distance) {
        return false;
    }

    float mode_default = 0.0F;
    if (!ReadCurrentCameraModeDefaultDistance(manager, &mode_default) ||
        !std::isfinite(mode_default) || mode_default <= 0.0F) {
        return false;
    }
    const float current = camera[18];
    const float target = SelectNextGamepadCameraDistance(
        current,
        mode_default);
    const int result = set_distance(camera, target, 1);

    auto& state = GetRuntimeState();
    if (state.GamepadLogEnabled()) {
        std::ostringstream event;
        event << "gamepad:camera_distance current=" << current
              << " target=" << target
              << " mode_default=" << mode_default
              << " result=" << result;
        state.AppendEventLog(event.str());
    }
    return result != 0;
}

void* GetGamepadControlReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::player_control_update:
        return reinterpret_cast<void*>(&Hook_PlayerControlUpdate);
    case HookId::main_camera_tail_follow_update:
        return reinterpret_cast<void*>(&Hook_MainCameraTailFollowUpdate);
    case HookId::flush_main_camera_tail_yaw:
        return reinterpret_cast<void*>(&Hook_FlushMainCameraTailYaw);
    case HookId::set_camera_mode_script:
        return reinterpret_cast<void*>(&Hook_SetCameraModeScript);
    default:
        return nullptr;
    }
}

void SetGamepadControlOriginalTrampoline(const HookId id, void* const trampoline) {
    switch (id) {
    case HookId::player_control_update:
        g_original_player_control_update =
            reinterpret_cast<PlayerControlUpdateFn>(trampoline);
        break;
    case HookId::main_camera_tail_follow_update:
        g_original_main_camera_tail_follow_update =
            reinterpret_cast<MainCameraTailFollowUpdateFn>(trampoline);
        break;
    case HookId::flush_main_camera_tail_yaw:
        g_original_flush_main_camera_tail_yaw =
            reinterpret_cast<FlushMainCameraTailYawFn>(trampoline);
        break;
    case HookId::set_camera_mode_script:
        g_original_set_camera_mode_script =
            reinterpret_cast<SetCameraModeScriptFn>(trampoline);
        break;
    default:
        break;
    }
}

bool ApplyGamepadCameraYawGuard(float* const camera) noexcept {
    return g_camera_yaw_guard.Apply(camera);
}

void ResetGamepadCameraYawControl() noexcept {
    ResetCameraYawTracking();
}

}  // namespace pal4::inject

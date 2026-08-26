#include "gamepad_control_hooks.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "gamepad_runtime.h"
#include "pal4inject/camera_pitch_guard.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using PlayerControlUpdateFn = int (__thiscall*)(void*, void*, float);
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
SetCameraModeScriptFn g_original_set_camera_mode_script = nullptr;
std::atomic<bool> g_camera_yaw_guard_enabled{false};
std::atomic<std::uintptr_t> g_camera_yaw_guard_target{0};
std::atomic<float> g_camera_yaw_guard_angle{0.0F};
bool g_camera_yaw_tracking = false;
float g_controlled_camera_yaw = 0.0F;

constexpr float kNativeMovementBaseSpeed = 169.89999F;
constexpr std::ptrdiff_t kPlayerPositionOffset = 172;
constexpr std::ptrdiff_t kPlayerRotationOffset = 160;
constexpr std::ptrdiff_t kPlayerYawOffset = 164;
constexpr std::ptrdiff_t kPlayerAnimationOffset = 368;
constexpr float kGamepadTurnSpeedDegreesPerSecond = 360.0F;
constexpr float kGamepadWalkTurnThresholdDegrees = 20.0F;

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

void DisableCameraYawGuard() noexcept {
    g_camera_yaw_guard_enabled.store(false, std::memory_order_release);
    g_camera_yaw_guard_target.store(0, std::memory_order_relaxed);
    g_camera_yaw_tracking = false;
}

void UpdateModernCamera(
    float* const camera,
    const GamepadAnalogStick& stick,
    const bool movement_active,
    const float delta_seconds) {
    const bool camera_active = stick.magnitude > 0.0F;
    if (!camera || (!movement_active && !camera_active)) {
        DisableCameraYawGuard();
        return;
    }
    const auto set_yaw = ResolveRuntimeFunction<SetCameraAngleFn>(ida::kCameraSetYaw);
    const auto set_pitch = ResolveRuntimeFunction<SetCameraAngleFn>(ida::kCameraSetPitch);
    if (!camera || !set_yaw || !set_pitch) {
        DisableCameraYawGuard();
        return;
    }

    const auto& state = GetRuntimeState();
    const float sensitivity = state.GamepadCameraSensitivity();
    const float safe_delta = std::isfinite(delta_seconds)
        ? std::clamp(delta_seconds, 0.0F, 0.1F)
        : 0.0F;
    const auto camera_address = reinterpret_cast<std::uintptr_t>(camera);
    if (!g_camera_yaw_tracking ||
        g_camera_yaw_guard_target.load(std::memory_order_relaxed) != camera_address) {
        g_controlled_camera_yaw = NormalizeAngle360(camera[15]);
        g_camera_yaw_tracking = true;
    }
    if (camera_active && safe_delta > 0.0F) {
        g_controlled_camera_yaw = NormalizeAngle360(
            g_controlled_camera_yaw - stick.x * sensitivity * safe_delta);
    }

    // Publish the target before calling PAL4's setter: SetYaw immediately
    // reaches Camera_UpdateMatrix, where the guard consumes this value.
    g_camera_yaw_guard_target.store(camera_address, std::memory_order_relaxed);
    g_camera_yaw_guard_angle.store(g_controlled_camera_yaw, std::memory_order_relaxed);
    g_camera_yaw_guard_enabled.store(true, std::memory_order_release);
    set_yaw(camera, g_controlled_camera_yaw, 1);

    if (camera_active && safe_delta > 0.0F) {
        const float pitch_direction = state.GamepadInvertCameraY() ? 1.0F : -1.0F;
        const float pitch = ClampCameraPitchAngle(
            camera[16] + stick.y * sensitivity * safe_delta * pitch_direction);
        set_pitch(camera, pitch, 1);
    }
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
        DisableCameraYawGuard();
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

int __cdecl Hook_SetCameraModeScript(char* const arguments) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::set_camera_mode_script);
    if (!g_original_set_camera_mode_script) {
        state.SetHookError(
            HookId::set_camera_mode_script,
            "original SetCameraMode trampoline is null");
        return 0;
    }
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
    case HookId::set_camera_mode_script:
        g_original_set_camera_mode_script =
            reinterpret_cast<SetCameraModeScriptFn>(trampoline);
        break;
    default:
        break;
    }
}

void ApplyGamepadCameraYawGuard(float* const camera) noexcept {
    if (!camera ||
        !g_camera_yaw_guard_enabled.load(std::memory_order_acquire) ||
        g_camera_yaw_guard_target.load(std::memory_order_relaxed) !=
            reinterpret_cast<std::uintptr_t>(camera)) {
        return;
    }
    const float guarded_yaw =
        g_camera_yaw_guard_angle.load(std::memory_order_relaxed);
    if (std::isfinite(guarded_yaw)) {
        camera[15] = NormalizeAngle360(guarded_yaw);
    }
}

}  // namespace pal4::inject

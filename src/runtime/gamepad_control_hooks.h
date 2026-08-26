#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetGamepadControlReplacementForHook(HookId id);
void SetGamepadControlOriginalTrampoline(HookId id, void* trampoline);
void ApplyGamepadCameraYawGuard(float* camera) noexcept;
void UpdateGamepadCameraOnly(
    const GamepadAnalogStick& stick,
    float delta_seconds);
void DisableGamepadBattleCamera() noexcept;
void ApplyGamepadBattleCameraBeforeRender(void* rw_camera) noexcept;
bool CycleGamepadCameraDistance();

}  // namespace pal4::inject

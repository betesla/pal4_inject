#pragma once

#include <atomic>
#include <cstdint>

#include "pal4inject/types.h"

namespace pal4::inject {

class CameraYawGuard final {
public:
    void Arm(float* camera, float yaw) noexcept;
    void Disarm() noexcept;
    bool Apply(float* camera) const noexcept;

private:
    std::atomic<bool> enabled_{false};
    std::atomic<std::uintptr_t> target_{0};
    std::atomic<float> angle_{0.0F};
};

void* GetGamepadControlReplacementForHook(HookId id);
void SetGamepadControlOriginalTrampoline(HookId id, void* trampoline);
bool ApplyGamepadCameraYawGuard(float* camera) noexcept;
void ResetGamepadCameraYawControl() noexcept;
bool CycleGamepadCameraDistance();

}  // namespace pal4::inject

#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetGamepadControlReplacementForHook(HookId id);
void SetGamepadControlOriginalTrampoline(HookId id, void* trampoline);
void ApplyGamepadCameraYawGuard(float* camera) noexcept;
bool CycleGamepadCameraDistance();

}  // namespace pal4::inject

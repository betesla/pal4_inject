#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetCameraReplacementForHook(HookId id);
void SetCameraOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

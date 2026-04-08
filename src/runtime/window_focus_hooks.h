#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetWindowFocusReplacementForHook(HookId id);
void SetWindowFocusOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

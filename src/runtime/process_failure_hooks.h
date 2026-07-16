#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetProcessFailureReplacementForHook(HookId id);
void SetProcessFailureOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

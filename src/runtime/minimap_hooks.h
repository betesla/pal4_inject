#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetMinimapReplacementForHook(HookId id);
void SetMinimapOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

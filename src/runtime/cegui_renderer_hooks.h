#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetCeguiRendererReplacementForHook(HookId id);
void SetCeguiRendererOriginalTrampoline(HookId id, void* trampoline);
void ApplyCeguiRendererHookMode(HookMode mode);

}  // namespace pal4::inject

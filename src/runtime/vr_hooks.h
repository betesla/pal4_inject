#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetVrReplacementForHook(HookId id);
void SetVrOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetBinkVideoReplacementForHook(HookId id);
void SetBinkVideoOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

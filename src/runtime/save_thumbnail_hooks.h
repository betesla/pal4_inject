#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetSaveThumbnailReplacementForHook(HookId id);
void SetSaveThumbnailOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

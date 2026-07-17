#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetLooseFileReplacementForHook(HookId id);
void SetLooseFileOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

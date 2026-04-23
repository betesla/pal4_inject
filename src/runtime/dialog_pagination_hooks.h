#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetDialogPaginationReplacementForHook(HookId id);
void SetDialogPaginationOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

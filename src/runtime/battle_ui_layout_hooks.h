#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetBattleUiLayoutReplacementForHook(HookId id);
void SetBattleUiLayoutOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

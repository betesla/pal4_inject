#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetCombatObservationReplacementForHook(HookId id);
void SetCombatObservationOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

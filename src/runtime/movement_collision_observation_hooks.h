#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetMovementCollisionReplacementForHook(HookId id);
void SetMovementCollisionOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

#pragma once

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetMediaObservationReplacementForHook(HookId id);
void SetMediaObservationOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

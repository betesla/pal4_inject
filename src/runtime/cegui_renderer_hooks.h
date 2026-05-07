#pragma once

#include <string>

#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/types.h"

namespace pal4::inject {

void* GetCeguiRendererReplacementForHook(HookId id);
void SetCeguiRendererOriginalTrampoline(HookId id, void* trampoline);
void ApplyCeguiRendererHookMode(HookMode mode);
bool TryGetActiveCeguiWidescreenPlan(CeguiWidescreenPlan* out);
void RefreshCeguiWidescreenRuntimeLayout();
std::string BuildCeguiWidescreenRuntimeLayoutDebugSummary();

}  // namespace pal4::inject

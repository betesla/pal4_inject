#pragma once

#include <string>
#include <string_view>

#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/types.h"

namespace pal4::inject {

bool ApplyKnownDynamicFontResync(
    std::string_view short_name,
    const CeguiWidescreenPlan& plan,
    std::string* error);
void* GetCeguiFontReplacementForHook(HookId id);
void SetCeguiFontOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject

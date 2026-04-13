#pragma once

#include <string_view>

#include "pal4inject/types.h"

namespace pal4::inject {

bool ShouldEmitHookLog(HookId id);
void AppendHookEventLog(HookId id, std::string_view text);

}  // namespace pal4::inject

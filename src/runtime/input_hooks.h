#pragma once

#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetReplacementForHook(HookId id);
void SetOriginalTrampoline(HookId id, void* trampoline);

bool DispatchUiMessageCommand(const UiMessageCommand& command, std::string* error);
bool DispatchSimulatedKey(
    std::uint32_t virtual_key,
    bool key_up,
    bool bypass_os_queue,
    std::string* error);
bool RefreshUiDispatchReady(std::string* reason);
std::uint32_t ReadCurrentPalivEntry() noexcept;

}  // namespace pal4::inject

#pragma once

#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

void* GetReplacementForHook(HookId id);
void SetOriginalTrampoline(HookId id, void* trampoline);

// Returns whether the message was delivered to the selected dispatch path.
// For the direct seam, out_message_handled reports the game's Win32-style
// handled result; a false handled result does not mean delivery failed.
bool DispatchUiMessageCommand(
    const UiMessageCommand& command,
    std::string* error,
    bool* out_message_handled = nullptr);
bool DispatchSimulatedKey(
    std::uint32_t virtual_key,
    bool key_up,
    bool bypass_os_queue,
    std::string* error,
    bool* out_message_handled = nullptr);
bool RefreshUiDispatchReady(std::string* reason);
std::uint32_t ReadCurrentPalivEntry() noexcept;

}  // namespace pal4::inject

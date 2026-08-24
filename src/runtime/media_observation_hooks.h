#pragma once

#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

class ScopedGiTalkVoiceRequest {
public:
    ScopedGiTalkVoiceRequest() noexcept;
    ~ScopedGiTalkVoiceRequest();
    ScopedGiTalkVoiceRequest(const ScopedGiTalkVoiceRequest&) = delete;
    ScopedGiTalkVoiceRequest& operator=(const ScopedGiTalkVoiceRequest&) = delete;
};

bool IsGiTalkVoiceRequestActive() noexcept;
bool RefreshActiveGiTalkVoiceVolume(std::string* error);

void* GetMediaObservationReplacementForHook(HookId id);
void SetMediaObservationOriginalTrampoline(HookId id, void* trampoline);

}  // namespace pal4::inject
